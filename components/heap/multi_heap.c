// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <multi_heap.h>

#include "multi_heap_internal.h"
#include <sys/mman.h>

#define DEBUG_OUTPUT_ALLOC 0

/* Defines compile-time configuration macros */
#include "multi_heap_config.h"

#include "malloc.h"

#include "esp_log.h"
const char *TAG = "multi_heap";

#ifdef MULTI_HEAP_POISONING
#error heap poisoning not supported
#endif /* MULTI_HEAP_POISONING */

void *multi_heap_malloc(multi_heap_handle_t heap, size_t size)
    __attribute__((alias("multi_heap_malloc_impl")));

void multi_heap_free(multi_heap_handle_t heap, void *p)
    __attribute__((alias("multi_heap_free_impl")));

void *multi_heap_realloc(multi_heap_handle_t heap, void *p, size_t size)
    __attribute__((alias("multi_heap_realloc_impl")));

size_t multi_heap_get_allocated_size(multi_heap_handle_t heap, void *p)
    __attribute__((alias("multi_heap_get_allocated_size_impl")));

multi_heap_handle_t multi_heap_register(void *start, size_t size)
    __attribute__((alias("multi_heap_register_impl")));

void multi_heap_get_info(multi_heap_handle_t heap, multi_heap_info_t *info)
    __attribute__((alias("multi_heap_get_info_impl")));

size_t multi_heap_free_size(multi_heap_handle_t heap)
    __attribute__((alias("multi_heap_free_size_impl")));

size_t multi_heap_minimum_free_size(multi_heap_handle_t heap)
    __attribute__((alias("multi_heap_minimum_free_size_impl")));

//void *multi_heap_get_block_address(multi_heap_block_handle_t block)
//    __attribute__((alias("multi_heap_get_block_address_impl")));

void *multi_heap_get_block_owner(multi_heap_block_handle_t block)
{
    return NULL;
}

size_t multi_heap_get_allocated_size_impl(multi_heap_handle_t heap, void *p)
{
    return mspace_usable_size(p);
}

multi_heap_handle_t gSysAllocAllowedMSpace;

multi_heap_handle_t multi_heap_register_impl(void *start_ptr, size_t size)
{
    multi_heap_handle_t heap;
    if(start_ptr == (void *)0x3F800000)
    {
        // SPI RAM
        size = SPIRAM_MMAP_ADDR0 - 0x3F800000;
        heap = gSysAllocAllowedMSpace = create_mspace_with_base(start_ptr, size, 1);
    }
    else
        heap = create_mspace_with_base(start_ptr, size, 1);

    return heap;
}

void multi_heap_set_lock(multi_heap_handle_t heap, void *lock)
{
    mspace_set_lock(heap, lock);
}

extern unsigned char gCodeInited;

#include <sdkconfig.h>
#include "soc/soc_memory_layout.h"
#include "esp_attr.h"

/* Architecture-specific return value of __builtin_return_address which
 * should be interpreted as an invalid address.
 */
#ifdef __XTENSA__
#define HEAP_ARCH_INVALID_PC  0x40000000
#else
#define HEAP_ARCH_INVALID_PC  0x00000000
#endif

// Caller is 2 stack frames deeper than we care about
#define STACK_OFFSET  2
#define STACK_DEPTH 8

#define TEST_STACK(N) do {                                              \
        if (STACK_DEPTH == N) {                                         \
            return;                                                     \
        }                                                               \
        callers[N] = __builtin_return_address(N+STACK_OFFSET);          \
        if (!esp_ptr_executable(callers[N])                             \
            || callers[N] == (void*) HEAP_ARCH_INVALID_PC) {            \
            callers[N] = 0;                                             \
        }                                                               \
    } while(0)

static IRAM_ATTR __attribute__((noinline)) void get_call_stack(void **callers)
{
    bzero(callers, sizeof(void *) * STACK_DEPTH);
    TEST_STACK(0);
    TEST_STACK(1);
    TEST_STACK(2);
    TEST_STACK(3);
    TEST_STACK(4);
    TEST_STACK(5);
    TEST_STACK(6);
    TEST_STACK(7);
    TEST_STACK(8);
    TEST_STACK(9);
}


void *multi_heap_malloc_impl(multi_heap_handle_t heap, size_t size)
{
    if (size == 0 || heap == NULL)
        return NULL;

    void *p = mspace_malloc(heap, size);
#if DEBUG_OUTPUT_ALLOC
    if(p && gCodeInited && ((uint32_t)p < 0x3f800000 || (uint32_t)p >= 0x3fc00000))
    {
    printf("ALLOC %p %d\n", p, size);
    void *alloced_by[STACK_DEPTH];
get_call_stack(alloced_by);
            for (int j = 1; j < STACK_DEPTH; j++) {
                printf("stack%d %p:", j, alloced_by[j]);
            }
            printf("\n");
    }
#endif

    return p;
}

void multi_heap_free_impl(multi_heap_handle_t heap, void *p)
{
    if (p == NULL)
        return;

#if DEBUG_OUTPUT_ALLOC
    if(p && gCodeInited && ((uint32_t)p < 0x3f800000 || (uint32_t)p >= 0x3fc00000))
    printf("FREE %p\n", p);
#endif

    mspace_free(heap, p);
}

void *multi_heap_realloc_impl(multi_heap_handle_t heap, void *p, size_t size)
{
    if (p == NULL)
        return multi_heap_malloc_impl(heap, size);
    if (size == 0)
    {
        multi_heap_free_impl(heap, p);
        return NULL;
    }

    void *p2 = mspace_realloc(heap, p, size);
#if DEBUG_OUTPUT_ALLOC
    if(p2 &&gCodeInited && ((uint32_t)p2 < 0x3f800000 || (uint32_t)p2 >= 0x3fc00000))
    printf("REALLOC %p=>%p2 %d\n", p, p2, size);
#endif
    return p2;
}

/*
bool multi_heap_check(multi_heap_handle_t heap, bool print_errors)
{
}
*/

void multi_heap_dump(multi_heap_handle_t heap)
{
    mspace_malloc_stats(heap);
}

size_t multi_heap_free_size_impl(multi_heap_handle_t heap)
{
    struct mallinfo minfo = mspace_mallinfo(heap);
    if(heap == gSysAllocAllowedMSpace)
        return 4 * 1024 * 1024 - minfo.uordblks;

    return minfo.fordblks;
}

size_t multi_heap_minimum_free_size_impl(multi_heap_handle_t heap)
{
    struct mallinfo minfo = mspace_mallinfo(heap);
    if(heap == gSysAllocAllowedMSpace)
        return 4 * 1024 * 1024 - minfo.usmblks;

    return minfo.uordblks + minfo.fordblks - minfo.usmblks;
}

void multi_heap_get_info_impl(multi_heap_handle_t heap, multi_heap_info_t *info)
{
    struct mallinfo minfo = mspace_mallinfo(heap);

    memset(info, 0, sizeof(multi_heap_info_t));
    info->total_allocated_bytes = minfo.uordblks;
    info->total_free_bytes = minfo.fordblks;
    info->largest_free_block = minfo.fordblks;     // well, could be
    info->allocated_blocks = minfo.uordblks;
    info->free_blocks = minfo.fordblks;
    info->total_blocks = minfo.uordblks + minfo.fordblks;
}
