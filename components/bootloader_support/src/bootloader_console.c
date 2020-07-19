// Copyright 2020 Espressif Systems (Shanghai) PTE LTD
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

#include "sdkconfig.h"
#include "bootloader_console.h"
#include "soc/uart_periph.h"
#include "soc/uart_channel.h"
#include "soc/io_mux_reg.h"
#include "soc/gpio_periph.h"
#include "soc/gpio_sig_map.h"
#include "esp32/rom/gpio.h"
#include "soc/rtc.h"
#include "hal/clk_gate_ll.h"
#ifdef CONFIG_IDF_TARGET_ESP32
#include "esp32/rom/ets_sys.h"
#include "esp32/rom/uart.h"
#elif CONFIG_IDF_TARGET_ESP32S2
#include "esp32s2/rom/ets_sys.h"
#include "esp32s2/rom/uart.h"
#include "esp32s2/rom/usb/cdc_acm.h"
#include "esp32s2/rom/usb/usb_common.h"
#endif
#include "esp_rom_gpio.h"

void bootloader_console_init(void)
{
#if CONFIG_ESP_CONSOLE_UART_NONE
    ets_install_putc1(NULL);
    ets_install_putc2(NULL);
#else // CONFIG_ESP_CONSOLE_UART_NONE
    const int uart_num = CONFIG_ESP_CONSOLE_UART_NUM;

    uartAttach();
    ets_install_uart_printf();

    // Wait for UART FIFO to be empty.
    uart_tx_wait_idle(0);

#if CONFIG_ESP_CONSOLE_UART_CUSTOM
    // Some constants to make the following code less upper-case
    const int uart_tx_gpio = CONFIG_ESP_CONSOLE_UART_TX_GPIO;
    const int uart_rx_gpio = CONFIG_ESP_CONSOLE_UART_RX_GPIO;
    // Switch to the new UART (this just changes UART number used for
    // ets_printf in ROM code).
    uart_tx_switch(uart_num);
    // If console is attached to UART1 or if non-default pins are used,
    // need to reconfigure pins using GPIO matrix
    if (uart_num != 0 || uart_tx_gpio != 1 || uart_rx_gpio != 3) {
        // Change pin mode for GPIO1/3 from UART to GPIO
        PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0RXD_U, FUNC_U0RXD_GPIO3);
        PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0TXD_U, FUNC_U0TXD_GPIO1);
        // Route GPIO signals to/from pins
        // (arrays should be optimized away by the compiler)
        const uint32_t tx_idx_list[3] = {U0TXD_OUT_IDX, U1TXD_OUT_IDX, U2TXD_OUT_IDX};
        const uint32_t rx_idx_list[3] = {U0RXD_IN_IDX, U1RXD_IN_IDX, U2RXD_IN_IDX};
        const uint32_t uart_reset[3] = {DPORT_UART_RST, DPORT_UART1_RST, DPORT_UART2_RST};
        const uint32_t tx_idx = tx_idx_list[uart_num];
        const uint32_t rx_idx = rx_idx_list[uart_num];

        if(uart_rx_gpio >= 0)
        {
                PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[uart_rx_gpio]);
                gpio_pad_pullup(uart_rx_gpio);
        }
        if(uart_tx_gpio >= 0)
                gpio_matrix_out(uart_tx_gpio, tx_idx, 0, 0);
        if(uart_rx_gpio >= 0)
                gpio_matrix_in(uart_rx_gpio, rx_idx, 0);

        DPORT_SET_PERI_REG_MASK(DPORT_PERIP_RST_EN_REG, uart_reset[uart_num]);
        DPORT_CLEAR_PERI_REG_MASK(DPORT_PERIP_RST_EN_REG, uart_reset[uart_num]);
    }
#endif // CONFIG_ESP_CONSOLE_UART_CUSTOM

    // Set configured UART console baud rate
    const int uart_baud = CONFIG_ESP_CONSOLE_UART_BAUDRATE;
    uart_div_modify(uart_num, (rtc_clk_apb_freq_get() << 4) / uart_baud);

#endif // CONFIG_ESP_CONSOLE_UART_NONE
}

#ifdef CONFIG_ESP_CONSOLE_USB_CDC
/* Buffer for CDC data structures. No RX buffer allocated. */
static char s_usb_cdc_buf[CDC_ACM_WORK_BUF_MIN];

void bootloader_console_init(void)
{
#ifdef CONFIG_IDF_TARGET_ESP32S2
    /* ESP32-S2 specific patch to set the correct serial number in the descriptor.
     * Later chips don't need this.
     */
    rom_usb_cdc_set_descriptor_patch();
#endif

    Uart_Init_USB(s_usb_cdc_buf, sizeof(s_usb_cdc_buf));
    uart_tx_switch(ROM_UART_USB);
    ets_install_putc1(bootloader_console_write_char_usb);
}
#endif //CONFIG_ESP_CONSOLE_USB_CDC
