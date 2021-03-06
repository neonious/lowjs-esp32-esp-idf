// Copyright 2015-2018 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdint.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_err.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_ota_ops.h"

#include "sdkconfig.h"

#include "soc/soc_caps.h"
#include "hal/wdt_hal.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_heap_caps_init.h"
#include "esp_spi_flash.h"
#include "esp_flash_internal.h"
#include "esp_newlib.h"
#include "esp_vfs_dev.h"
#include "esp_timer.h"
#include "esp_efuse.h"
#include "esp_flash_encrypt.h"

/***********************************************/
// Headers for other components init functions
#include "nvs_flash.h"
#include "esp_phy_init.h"
#include "esp_coexist_internal.h"
#include "esp_core_dump.h"
#include "esp_app_trace.h"
#include "esp_private/dbg_stubs.h"
#include "esp_flash_encrypt.h"
#include "esp_pm.h"
#include "esp_private/pm_impl.h"
#include "esp_pthread.h"
#include "esp_private/usb_console.h"
#include "esp_vfs_cdcacm.h"


// [refactor-todo] make this file completely target-independent
#if CONFIG_IDF_TARGET_ESP32
#include "esp32/rom/uart.h"
#include "esp32/rom/ets_sys.h"
#include "esp32/spiram.h"
#include "esp32/brownout.h"
#elif CONFIG_IDF_TARGET_ESP32S2
#include "esp32s2/rom/uart.h"
#include "esp32s2/rom/ets_sys.h"
#include "esp32s2/spiram.h"
#include "esp32s2/brownout.h"
#endif
/***********************************************/

#include "esp_private/startup_internal.h"

// Ensure that system configuration matches the underlying number of cores.
// This should enable us to avoid checking for both everytime.
#if !(SOC_CPU_CORES_NUM > 1) && !CONFIG_ESP_SYSTEM_SINGLE_CORE_MODE
    #error "System has been configured to run on multiple cores, but target SoC only has a single core."
#endif

#define STRINGIFY(s) STRINGIFY2(s)
#define STRINGIFY2(s) #s

// App entry point for core 0
extern void start_app(void);

// Entry point for core 0 from hardware init (port layer)
void start_cpu0(void) __attribute__((weak, alias("start_cpu0_default"))) __attribute__((noreturn));

#if !CONFIG_ESP_SYSTEM_SINGLE_CORE_MODE
// Entry point for core [1..X] from hardware init (port layer)
void start_cpu_other_cores(void) __attribute__((weak, alias("start_cpu_other_cores_default"))) __attribute__((noreturn));

// App entry point for core [1..X]
void start_app_other_cores(void) __attribute__((weak, alias("start_app_other_cores_default"))) __attribute__((noreturn));

static volatile bool s_system_inited[SOC_CPU_CORES_NUM] = { false };

sys_startup_fn_t g_startup_fn[SOC_CPU_CORES_NUM] = { [0] = start_cpu0,
#if SOC_CPU_CORES_NUM > 1
    [1 ... SOC_CPU_CORES_NUM - 1] = start_cpu_other_cores
#endif
};

static volatile bool s_system_full_inited = false;
#else
sys_startup_fn_t g_startup_fn[1] = { start_cpu0 };
#endif

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
// workaround for C++ exception crashes
void _Unwind_SetNoFunctionContextInstall(unsigned char enable);
// workaround for C++ exception large memory allocation
void _Unwind_SetEnableExceptionFdeSorting(unsigned char enable);
#endif // CONFIG_COMPILER_CXX_EXCEPTIONS

static const char* TAG = "cpu_start";

static void IRAM_ATTR do_global_ctors(void)
{
    extern void (*__init_array_start)(void);
    extern void (*__init_array_end)(void);

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    struct object { long placeholder[ 10 ]; };
    void __register_frame_info (const void *begin, struct object *ob);
    extern char __eh_frame[];

    static struct object ob;
    __register_frame_info( __eh_frame, &ob );
#endif // CONFIG_COMPILER_CXX_EXCEPTIONS

    void (**p)(void);
    for (p = &__init_array_end - 1; p >= &__init_array_start; --p) {
        (*p)();
    }
}

static void IRAM_ATTR do_system_init_fn(void)
{
    extern esp_system_init_fn_t _esp_system_init_fn_array_start;
    extern esp_system_init_fn_t _esp_system_init_fn_array_end;

    esp_system_init_fn_t *p;

    for (p = &_esp_system_init_fn_array_end - 1; p >= &_esp_system_init_fn_array_start; --p) {
        if (p->cores & BIT(cpu_hal_get_core_id())) {
            (*(p->fn))();
        }
    }

#if !CONFIG_ESP_SYSTEM_SINGLE_CORE_MODE
    s_system_inited[cpu_hal_get_core_id()] = true;
#endif
}

#if !CONFIG_ESP_SYSTEM_SINGLE_CORE_MODE
static void IRAM_ATTR start_app_other_cores_default(void)
{
    while (1) {
        ets_delay_us(UINT32_MAX);
    }
}

static void IRAM_ATTR start_cpu_other_cores_default(void)
{
    do_system_init_fn();

    while (!s_system_full_inited) {
        ets_delay_us(100);
    }

    start_app_other_cores();
}
#endif

static void IRAM_ATTR do_core_init(void)
{
    /* Initialize heap allocator. WARNING: This *needs* to happen *after* the app cpu has booted.
       If the heap allocator is initialized first, it will put free memory linked list items into
       memory also used by the ROM. Starting the app cpu will let its ROM initialize that memory,
       corrupting those linked lists. Initializing the allocator *after* the app cpu has booted
       works around this problem.
       With SPI RAM enabled, there's a second reason: half of the SPI RAM will be managed by the
       app CPU, and when that is not up yet, the memory will be inaccessible and heap_caps_init may
       fail initializing it properly. */
    heap_caps_init();
    esp_setup_syscall_table();

    if (g_spiram_ok) {
#if CONFIG_SPIRAM_BOOT_INIT && (CONFIG_SPIRAM_USE_CAPS_ALLOC || CONFIG_SPIRAM_USE_MALLOC)
        esp_err_t r=esp_spiram_add_to_heapalloc();
        if (r != ESP_OK) {
            ESP_EARLY_LOGE(TAG, "External RAM could not be added to heap!");
            abort();
        }
#if CONFIG_SPIRAM_USE_MALLOC
        heap_caps_malloc_extmem_enable(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL);
#endif
#endif
    }

#if CONFIG_ESP32_BROWNOUT_DET || CONFIG_ESP32S2_BROWNOUT_DET
    // [refactor-todo] leads to call chain rtc_is_register (driver) -> esp_intr_alloc (esp32/esp32s2) ->
    // malloc (newlib) -> heap_caps_malloc (heap), so heap must be at least initialized
    esp_brownout_init();
#endif

#ifdef CONFIG_VFS_SUPPORT_IO
#ifdef CONFIG_ESP_CONSOLE_UART
    esp_vfs_dev_uart_register();
    const char *default_stdio_dev = "/dev/uart/" STRINGIFY(CONFIG_ESP_CONSOLE_UART_NUM);
#endif // CONFIG_ESP_CONSOLE_UART
#ifdef CONFIG_ESP_CONSOLE_USB_CDC
    ESP_ERROR_CHECK(esp_usb_console_init());
    ESP_ERROR_CHECK(esp_vfs_dev_cdcacm_register());
    const char *default_stdio_dev = "/dev/cdcacm";
#endif // CONFIG_ESP_CONSOLE_USB_CDC
#endif // CONFIG_VFS_SUPPORT_IO

#if defined(CONFIG_VFS_SUPPORT_IO) && !defined(CONFIG_ESP_CONSOLE_NONE)
    esp_reent_init(_GLOBAL_REENT);
    _GLOBAL_REENT->_stdin  = fopen(default_stdio_dev, "r");
    _GLOBAL_REENT->_stdout = fopen(default_stdio_dev, "w");
    _GLOBAL_REENT->_stderr = fopen(default_stdio_dev, "w");
#else // defined(CONFIG_VFS_SUPPORT_IO) && !defined(CONFIG_ESP_CONSOLE_NONE)
    _REENT_SMALL_CHECK_INIT(_GLOBAL_REENT);
#endif // defined(CONFIG_VFS_SUPPORT_IO) && !defined(CONFIG_ESP_CONSOLE_NONE)

#ifdef CONFIG_SECURE_FLASH_ENC_ENABLED
    esp_flash_encryption_init_checks();
#endif

#if CONFIG_SECURE_DISABLE_ROM_DL_MODE
    err = esp_efuse_disable_rom_download_mode();
    assert(err == ESP_OK && "Failed to disable ROM download mode");
#endif

#if CONFIG_SECURE_ENABLE_SECURE_ROM_DL_MODE
    err = esp_efuse_enable_rom_secure_download_mode();
    assert(err == ESP_OK && "Failed to enable Secure Download mode");
#endif

#if CONFIG_ESP32_DISABLE_BASIC_ROM_CONSOLE
    esp_efuse_disable_basic_rom_console();
#endif

    esp_err_t err;

    esp_timer_init();
    esp_set_time_from_rtc();

    // [refactor-todo] move this to secondary init
#if CONFIG_APPTRACE_ENABLE
    err = esp_apptrace_init();
    assert(err == ESP_OK && "Failed to init apptrace module on PRO CPU!");
#endif
#if CONFIG_SYSVIEW_ENABLE
    SEGGER_SYSVIEW_Conf();
#endif

#if CONFIG_ESP_DEBUG_STUBS_ENABLE
    esp_dbg_stubs_init();
#endif

    err = esp_pthread_init();
    assert(err == ESP_OK && "Failed to init pthread module!");

    spi_flash_init();
    /* init default OS-aware flash access critical section */
    spi_flash_guard_set(&g_flash_guard_default_ops);

    esp_flash_app_init();
    esp_err_t flash_ret = esp_flash_init_default_chip();
    assert(flash_ret == ESP_OK);
}

static void IRAM_ATTR do_secondary_init(void)
{
#if !CONFIG_ESP_SYSTEM_SINGLE_CORE_MODE
    // The port layer transferred control to this function with other cores 'paused',
    // resume execution so that cores might execute component initialization functions.
    startup_resume_other_cores();
#endif

    // Execute initialization functions esp_system_init_fn_t assigned to the main core. While
    // this is happening, all other cores are executing the initialization functions
    // assigned to them since they have been resumed already.
    do_system_init_fn();

#if !CONFIG_ESP_SYSTEM_SINGLE_CORE_MODE
    // Wait for all cores to finish secondary init.
    volatile bool system_inited = false;

    while (!system_inited) {
        system_inited = true;
        for (int i = 0; i < SOC_CPU_CORES_NUM; i++) {
            system_inited &= s_system_inited[i];
        }
        ets_delay_us(100);
    }
#endif
}

void IRAM_ATTR start_cpu0_default(void)
{
    ESP_EARLY_LOGI(TAG, "Pro cpu start user code");

    // Display information about the current running image.
    if (LOG_LOCAL_LEVEL >= ESP_LOG_INFO) {
        const esp_app_desc_t *app_desc = esp_ota_get_app_description();
        ESP_EARLY_LOGI(TAG, "Application information:");
#ifndef CONFIG_APP_EXCLUDE_PROJECT_NAME_VAR
        ESP_EARLY_LOGI(TAG, "Project name:     %s", app_desc->project_name);
#endif
#ifndef CONFIG_APP_EXCLUDE_PROJECT_VER_VAR
        ESP_EARLY_LOGI(TAG, "App version:      %s", app_desc->version);
#endif
#ifdef CONFIG_BOOTLOADER_APP_SECURE_VERSION
        ESP_EARLY_LOGI(TAG, "Secure version:   %d", app_desc->secure_version);
#endif
#ifdef CONFIG_APP_COMPILE_TIME_DATE
        ESP_EARLY_LOGI(TAG, "Compile time:     %s %s", app_desc->date, app_desc->time);
#endif
        char buf[17];
        esp_ota_get_app_elf_sha256(buf, sizeof(buf));
        ESP_EARLY_LOGI(TAG, "ELF file SHA256:  %s...", buf);
        ESP_EARLY_LOGI(TAG, "ESP-IDF:          %s", app_desc->idf_ver);
    }

    // Initialize core components and services.
    do_core_init();

    // Execute constructors.
    do_global_ctors();

    // Execute init functions of other components; blocks
    // until all cores finish (when !CONFIG_ESP_SYSTEM_SINGLE_CORE_MODE).
    do_secondary_init();

    // Now that the application is about to start, disable boot watchdog
#ifndef CONFIG_BOOTLOADER_WDT_DISABLE_IN_USER_CODE
    wdt_hal_context_t rtc_wdt_ctx = {.inst = WDT_RWDT, .rwdt_dev = &RTCCNTL};
    wdt_hal_write_protect_disable(&rtc_wdt_ctx);
    wdt_hal_disable(&rtc_wdt_ctx);
    wdt_hal_write_protect_enable(&rtc_wdt_ctx);
#endif

#if SOC_CPU_CORES_NUM > 1 && !CONFIG_ESP_SYSTEM_SINGLE_CORE_MODE
    s_system_full_inited = true;
#endif

    start_app();
    while (1);
}

IRAM_ATTR ESP_SYSTEM_INIT_FN(init_components0, BIT(0))
{
#if defined(CONFIG_PM_ENABLE) && defined(CONFIG_ESP_CONSOLE_UART)
    const int uart_clk_freq = REF_CLK_FREQ;
    /* When DFS is enabled, use REFTICK as UART clock source */
    CLEAR_PERI_REG_MASK(UART_CONF0_REG(CONFIG_ESP_CONSOLE_UART_NUM), UART_TICK_REF_ALWAYS_ON);
    uart_div_modify(CONFIG_ESP_CONSOLE_UART_NUM, (uart_clk_freq << 4) / CONFIG_ESP_CONSOLE_UART_BAUDRATE);
#endif // CONFIG_ESP_CONSOLE_UART_NONE

#ifdef CONFIG_PM_ENABLE
    esp_pm_impl_init();
#ifdef CONFIG_PM_DFS_INIT_AUTO
    int xtal_freq = (int) rtc_clk_xtal_freq_get();
    esp_pm_config_esp32_t cfg = {
        .max_freq_mhz = CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = xtal_freq,
    };
    esp_pm_configure(&cfg);
#endif //CONFIG_PM_DFS_INIT_AUTO
#endif //CONFIG_PM_ENABLE

#if CONFIG_IDF_TARGET_ESP32
#if CONFIG_ESP32_ENABLE_COREDUMP
    esp_core_dump_init();
#endif
#endif

#if CONFIG_IDF_TARGET_ESP32
#if CONFIG_ESP32_WIFI_SW_COEXIST_ENABLE
    esp_coex_adapter_register(&g_coex_adapter_funcs);
    coex_pre_init();
#endif
#endif

#ifdef CONFIG_BOOTLOADER_EFUSE_SECURE_VERSION_EMULATE
    const esp_partition_t *efuse_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_EFUSE_EM, NULL);
    if (efuse_partition) {
        esp_efuse_init(efuse_partition->address, efuse_partition->size);
    }
#endif

#ifdef CONFIG_COMPILER_CXX_EXCEPTIONS
    // Not needed with new toolchain
    //ESP_EARLY_LOGD(TAG, "Setting C++ exception workarounds.");
    //_Unwind_SetNoFunctionContextInstall(1);
    //_Unwind_SetEnableExceptionFdeSorting(0);
#endif // CONFIG_COMPILER_CXX_EXCEPTIONS
}

