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

multi_heap_handle_t gSysAllocAllowedMSpace;
extern void *gCodeStartMutex;

#ifndef MULTI_HEAP_POISONING

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
}


void *multi_heap_malloc_impl(multi_heap_handle_t heap, size_t size)
{
    if (size == 0 || heap == NULL)
        return NULL;

    // HACK: Seems like Espressif allocates a large emergency buffer
    // in eh_frame.cc. Returning NULL disables the emergency buffer
    // Code in comment can be used to verify we are doing the right thing
    if(size == 1310720 && !gCodeStartMutex)
       return NULL;
/*
        {
    ESP_EARLY_LOGI(TAG, "ALLOC %d\n", size);
    void *alloced_by[STACK_DEPTH];
get_call_stack(alloced_by);
            for (int j = 1; j < STACK_DEPTH; j++) {
                ESP_EARLY_LOGI(TAG,"stack%d %p:", j, alloced_by[j]);
            }
            return NULL;
        }
*/
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

#else

// Old version
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

/* Note: Keep platform-specific parts in this header, this source
   file should depend on libc only */
#include "multi_heap_platform.h"

/* Defines compile-time configuration macros */
#include "multi_heap_config.h"

#ifndef MULTI_HEAP_POISONING
/* if no heap poisoning, public API aliases directly to these implementations */
void *multi_heap_malloc(multi_heap_handle_t heap, size_t size)
    __attribute__((alias("multi_heap_malloc_impl")));

void *multi_heap_aligned_alloc(multi_heap_handle_t heap, size_t size, size_t alignment)
    __attribute__((alias("multi_heap_aligned_alloc_impl")));

void multi_heap_free(multi_heap_handle_t heap, void *p)
    __attribute__((alias("multi_heap_free_impl")));

void multi_heap_aligned_free(multi_heap_handle_t heap, void *p)
    __attribute__((alias("multi_heap_aligned_free_impl")));

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

void *multi_heap_get_block_address(multi_heap_block_handle_t block)
    __attribute__((alias("multi_heap_get_block_address_impl")));

void *multi_heap_get_block_owner(multi_heap_block_handle_t block)
{
    return NULL;
}

#endif

#define ALIGN(X) ((X) & ~(sizeof(void *)-1))
#define ALIGN_UP(X) ALIGN((X)+sizeof(void *)-1)
#define ALIGN_UP_BY(num, align) (((num) + ((align) - 1)) & ~((align) - 1))

struct heap_block;

/* Block in the heap

   Heap implementation uses two single linked lists, a block list (all blocks) and a free list (free blocks).

   'header' holds a pointer to the next block (used or free) ORed with a free flag (the LSB of the pointer.) is_free() and get_next_block() utility functions allow typed access to these values.

   'next_free' is valid if the block is free and is a pointer to the next block in the free list.
*/
typedef struct heap_block {
    intptr_t header;                  /* Encodes next block in heap (used or unused) and also free/used flag */
    union {
        uint8_t data[1];              /* First byte of data, valid if block is used. Actual size of data is 'block_data_size(block)' */
        struct heap_block *next_free; /* Pointer to next free block, valid if block is free */
    };
} heap_block_t;

/* These masks apply to the 'header' field of heap_block_t */
#define BLOCK_FREE_FLAG 0x1  /* If set, this block is free & next_free pointer is valid */
#define NEXT_BLOCK_MASK (~3) /* AND header with this mask to get pointer to next block (free or used) */

/* Metadata header for the heap, stored at the beginning of heap space.

   'first_block' is a "fake" first block, minimum length, used to provide a pointer to the first used & free block in
   the heap. This block is never allocated or merged into an adjacent block.

   'last_block' is a pointer to a final free block of length 0, which is added at the end of the heap when it is
   registered. This block is also never allocated or merged into an adjacent block.
 */
typedef struct multi_heap_info {
    void *lock;
    size_t free_bytes;
    size_t minimum_free_bytes;
    heap_block_t *last_block;
    heap_block_t first_block; /* initial 'free block', never allocated */
} heap_t;

/* Given a pointer to the 'data' field of a block (ie the previous malloc/realloc result), return a pointer to the
   containing block.
*/
static inline heap_block_t *get_block(const void *data_ptr)
{
    return (heap_block_t *)((char *)data_ptr - offsetof(heap_block_t, data));
}

/* Return the next sequential block in the heap.
 */
static inline heap_block_t *get_next_block(const heap_block_t *block)
{
    intptr_t next = block->header & NEXT_BLOCK_MASK;
    if (next == 0) {
        return NULL; /* last_block */
    }
    assert(next > (intptr_t)block);
    return (heap_block_t *)next;
}

/* Return true if this block is free. */
static inline bool is_free(const heap_block_t *block)
{
    return block->header & BLOCK_FREE_FLAG;
}

/* Return true if this block is the first in the heap */
static inline bool is_first_block(const heap_t *heap, const heap_block_t *block)
{
    return (block == &heap->first_block);
}

/* Return true if this block is the last_block in the heap
   (the only block with no next pointer) */
static inline bool is_last_block(const heap_block_t *block)
{
    return (block->header & NEXT_BLOCK_MASK) == 0;
}

/* Data size of the block (excludes this block's header) */
static inline size_t block_data_size(const heap_block_t *block)
{
    intptr_t next = (intptr_t)block->header & NEXT_BLOCK_MASK;
    intptr_t this = (intptr_t)block;
    if (next == 0) {
        return 0; /* this is the last block in the heap */
    }
    return next - this - sizeof(block->header);
}

/* Check a block is valid for this heap. Used to verify parameters. */
static void assert_valid_block(const heap_t *heap, const heap_block_t *block)
{
    MULTI_HEAP_ASSERT(block >= &heap->first_block && block <= heap->last_block,
                      block); // block not in heap
    if (heap < (const heap_t *)heap->last_block) {
        const heap_block_t *next = get_next_block(block);
        MULTI_HEAP_ASSERT(next >= &heap->first_block && next <= heap->last_block, block); // Next block not in heap
        if (is_free(block)) {
            // Check block->next_free is valid
            MULTI_HEAP_ASSERT(block->next_free >= &heap->first_block && block->next_free <= heap->last_block, &block->next_free);
        }
    }
}

/* Get the first free block before 'block' in the heap. 'block' can be a free block or in use.

   Result is always the closest free block to 'block' in the heap, that is located before 'block'. There may be multiple
   allocated blocks between the result and 'block'.

   If 'block' is free, the result's 'next_free' pointer will already point to 'block'.

   Result will never be NULL, but it may be the header block heap->first_block.
*/
static heap_block_t *get_prev_free_block(heap_t *heap, const heap_block_t *block)
{
    assert(!is_first_block(heap, block)); /* can't look for a block before first_block */

    for (heap_block_t *b = &heap->first_block; b != NULL && b < block; b = b->next_free) {
        MULTI_HEAP_ASSERT(is_free(b), b); // Block should be free
        if (b->next_free == NULL || b->next_free >= block) {
            if (is_free(block)) {
                 /* if block is on freelist, 'b' should be the item before it. */
                MULTI_HEAP_ASSERT(b->next_free == block, &b->next_free);
            }
            return b; /* b is the last free block before 'block' */
        }
    }
    abort(); /* There should always be a previous free block, even if it's heap->first_block */
}

/* Merge some block 'a' into the following block 'b'.

   If both blocks are free, resulting block is marked free.
   If only one block is free, resulting block is marked in use. No data is moved.

   This operation may fail if block 'a' is the first block or 'b' is the last block,
   the caller should check block_data_size() to know if anything happened here or not.
*/
static heap_block_t *merge_adjacent(heap_t *heap, heap_block_t *a, heap_block_t *b)
{
    assert(a < b);

    /* Can't merge header blocks, just return the non-header block as-is */
    if (is_last_block(b)) {
        return a;
    }
    if (is_first_block(heap, a)) {
        return b;
    }

    MULTI_HEAP_ASSERT(get_next_block(a) == b, a); // Blocks should be in order

    bool free = is_free(a) && is_free(b); /* merging two free blocks creates a free block */
    if (!free && (is_free(a) || is_free(b))) {
        /* only one of these blocks is free, so resulting block will be a used block.
           means we need to take the free block out of the free list
         */
        heap_block_t *free_block = is_free(a) ? a : b;
        heap_block_t *prev_free = get_prev_free_block(heap, free_block);
        MULTI_HEAP_ASSERT(free_block->next_free > prev_free, &free_block->next_free); // Next free block should be after prev one
        prev_free->next_free = free_block->next_free;

        heap->free_bytes -= block_data_size(free_block);
    }

    a->header = b->header & NEXT_BLOCK_MASK;
    MULTI_HEAP_ASSERT(a->header != 0, a);
    if (free) {
        a->header |= BLOCK_FREE_FLAG;
        if (b->next_free != NULL) {
            MULTI_HEAP_ASSERT(b->next_free > a, &b->next_free);
            MULTI_HEAP_ASSERT(b->next_free > b, &b->next_free);
        }
        a->next_free = b->next_free;

        /* b's header can be put into the pool of free bytes */
        heap->free_bytes += sizeof(a->header);
    }

#ifdef MULTI_HEAP_POISONING_SLOW
    /* b's former block header needs to be replaced with a fill pattern */
    multi_heap_internal_poison_fill_region(b, sizeof(heap_block_t), free);
#endif

    return a;
}

/* Split a block so it can hold at least 'size' bytes of data, making any spare
   space into a new free block.

   'block' should be marked in-use when this function is called (implementation detail, this function
   doesn't set the next_free pointer).

   'prev_free_block' is the free block before 'block', if already known. Can be NULL if not yet known.
   (This is a performance optimisation to avoid walking the freelist twice when possible.)
*/
static void split_if_necessary(heap_t *heap, heap_block_t *block, size_t size, heap_block_t *prev_free_block)
{
    const size_t block_size = block_data_size(block);
    MULTI_HEAP_ASSERT(!is_free(block), block); // split block shouldn't be free
    MULTI_HEAP_ASSERT(size <= block_size, block); // size should be valid
    size = ALIGN_UP(size);

    /* can't split the head or tail block */
    assert(!is_first_block(heap, block));
    assert(!is_last_block(block));

    heap_block_t *new_block = (heap_block_t *)(block->data + size);
    heap_block_t *next_block = get_next_block(block);

    if (is_free(next_block) && !is_last_block(next_block)) {
        /* The next block is free, just extend it upwards. */
        new_block->header = next_block->header;
        new_block->next_free = next_block->next_free;
        if (prev_free_block == NULL) {
            prev_free_block = get_prev_free_block(heap, block);
        }
        /* prev_free_block should point to the next block (which we found to be free). */
        MULTI_HEAP_ASSERT(prev_free_block->next_free == next_block,
                          &prev_free_block->next_free); // free blocks should be in order
        /* Note: We have not introduced a new block header, hence the simple math. */
        heap->free_bytes += block_size - size;
#ifdef MULTI_HEAP_POISONING_SLOW
        /* next_block header needs to be replaced with a fill pattern */
        multi_heap_internal_poison_fill_region(next_block, sizeof(heap_block_t), true /* free */);
#endif
    } else {
        /* Insert a free block between the current and the next one. */
        if (block_data_size(block) < size + sizeof(heap_block_t)) {
            /* Can't split 'block' if we're not going to get a usable free block afterwards */
            return;
        }
        if (prev_free_block == NULL) {
            prev_free_block = get_prev_free_block(heap, block);
        }
        new_block->header = block->header | BLOCK_FREE_FLAG;
        new_block->next_free = prev_free_block->next_free;
        /* prev_free_block should point to a free block after new_block */
        MULTI_HEAP_ASSERT(prev_free_block->next_free > new_block,
                          &prev_free_block->next_free); // free blocks should be in order
        heap->free_bytes += block_data_size(new_block);
    }
    block->header = (intptr_t)new_block;
    prev_free_block->next_free = new_block;
}

void *multi_heap_get_block_address_impl(multi_heap_block_handle_t block)
{
    return ((char *)block + offsetof(heap_block_t, data));
}

size_t multi_heap_get_allocated_size_impl(multi_heap_handle_t heap, void *p)
{
    heap_block_t *pb = get_block(p);

    assert_valid_block(heap, pb);
    MULTI_HEAP_ASSERT(!is_free(pb), pb); // block shouldn't be free
    return block_data_size(pb);
}

multi_heap_handle_t multi_heap_register_impl(void *start_ptr, size_t size)
{
    uintptr_t start = ALIGN_UP((uintptr_t)start_ptr);
    uintptr_t end = ALIGN((uintptr_t)start_ptr + size);
    heap_t *heap = (heap_t *)start;
    size = end - start;

    if (end < start || size < sizeof(heap_t) + 2*sizeof(heap_block_t)) {
        return NULL; /* 'size' is too small to fit a heap here */
    }
    heap->lock = NULL;
    heap->last_block = (heap_block_t *)(end - sizeof(heap_block_t));

    /* first 'real' (allocatable) free block goes after the heap structure */
    heap_block_t *first_free_block = (heap_block_t *)(start + sizeof(heap_t));
    first_free_block->header = (intptr_t)heap->last_block | BLOCK_FREE_FLAG;
    first_free_block->next_free = heap->last_block;

    /* last block is 'free' but has a NULL next pointer */
    heap->last_block->header = BLOCK_FREE_FLAG;
    heap->last_block->next_free = NULL;

    /* first block also 'free' but has legitimate length,
       malloc will never allocate into this block. */
    heap->first_block.header = (intptr_t)first_free_block | BLOCK_FREE_FLAG;
    heap->first_block.next_free = first_free_block;

    /* free bytes is:
       - total bytes in heap
       - minus heap_t header at top (includes heap->first_block)
       - minus header of first_free_block
       - minus whole block at heap->last_block
    */
    heap->free_bytes = size - sizeof(heap_t) - sizeof(first_free_block->header) - sizeof(heap_block_t);
    heap->minimum_free_bytes = heap->free_bytes;

    return heap;
}

void multi_heap_set_lock(multi_heap_handle_t heap, void *lock)
{
    heap->lock = lock;
}

void inline multi_heap_internal_lock(multi_heap_handle_t heap)
{
    MULTI_HEAP_LOCK(heap->lock);
}

void inline multi_heap_internal_unlock(multi_heap_handle_t heap)
{
    MULTI_HEAP_UNLOCK(heap->lock);
}

multi_heap_block_handle_t multi_heap_get_first_block(multi_heap_handle_t heap)
{
    return &heap->first_block;
}

multi_heap_block_handle_t multi_heap_get_next_block(multi_heap_handle_t heap, multi_heap_block_handle_t block)
{
    heap_block_t *next = get_next_block(block);
    /* check for valid free last block to avoid assert in assert_valid_block */
    if (next == heap->last_block && is_last_block(next) && is_free(next)) {
        return NULL;
    }
    assert_valid_block(heap, next);
    return next;
}

bool multi_heap_is_free(multi_heap_block_handle_t block)
{
    return is_free(block);
}

void *multi_heap_malloc_impl(multi_heap_handle_t heap, size_t size)
{
    heap_block_t *best_block = NULL;
    heap_block_t *prev_free = NULL;
    heap_block_t *prev = NULL;
    size_t best_size = SIZE_MAX;
    size = ALIGN_UP(size);

    if (size == 0 || heap == NULL) {
        return NULL;
    }

    multi_heap_internal_lock(heap);

    /* Note: this check must be done while holding the lock as both
       malloc & realloc may temporarily shrink the free_bytes value
       before they split a large block. This can result in false negatives,
       especially if the heap is unfragmented.
    */
    if (heap->free_bytes < size) {
        MULTI_HEAP_UNLOCK(heap->lock);
        return NULL;
    }

    /* Find best free block to perform the allocation in */
    prev = &heap->first_block;
    for (heap_block_t *b = heap->first_block.next_free; b != NULL; b = b->next_free) {
        MULTI_HEAP_ASSERT(b > prev, &prev->next_free); // free blocks should be ascending in address
        MULTI_HEAP_ASSERT(is_free(b), b); // block should be free
        size_t bs = block_data_size(b);
        if (bs >= size && bs < best_size) {
            best_block = b;
            best_size = bs;
            prev_free = prev;
            if (bs == size) {
                break; /* we've found a perfect sized block */
            }
        }
        prev = b;
    }

    if (best_block == NULL) {
        multi_heap_internal_unlock(heap);
        return NULL; /* No room in heap */
    }

    prev_free->next_free = best_block->next_free;
    best_block->header &= ~BLOCK_FREE_FLAG;

    heap->free_bytes -= block_data_size(best_block);

    split_if_necessary(heap, best_block, size, prev_free);

    if (heap->free_bytes < heap->minimum_free_bytes) {
        heap->minimum_free_bytes = heap->free_bytes;
    }

    multi_heap_internal_unlock(heap);

    return best_block->data;
}

void *multi_heap_aligned_alloc_impl(multi_heap_handle_t heap, size_t size, size_t alignment)
{
    if (heap == NULL) {
        return NULL;
    }

    if (!size) {
        return NULL;
    }

    if (!alignment) {
        return NULL;
    }

    //Alignment must be a power of two...
    if ((alignment & (alignment - 1)) != 0) {
        return NULL;
    }

    uint32_t overhead = (sizeof(uint32_t) + (alignment - 1));

    multi_heap_internal_lock(heap);
    void *head = multi_heap_malloc_impl(heap, size + overhead);
    if (head == NULL) {
        multi_heap_internal_unlock(heap);
        return NULL;
    }

    //Lets align our new obtained block address:
    //and save information to recover original block pointer
    //to allow us to deallocate the memory when needed
    void *ptr = (void *)ALIGN_UP_BY((uintptr_t)head + sizeof(uint32_t), alignment);
    *((uint32_t *)ptr - 1) = (uint32_t)((uintptr_t)ptr - (uintptr_t)head);

    multi_heap_internal_unlock(heap);
    return ptr;
}

void multi_heap_aligned_free_impl(multi_heap_handle_t heap, void *p)
{
    if (p == NULL) {
        return;
    }

    multi_heap_internal_lock(heap);
    uint32_t offset = *((uint32_t *)p - 1);
    void *block_head = (void *)((uint8_t *)p - offset);

#ifdef MULTI_HEAP_POISONING_SLOW
        multi_heap_internal_poison_fill_region(block_head, multi_heap_get_allocated_size_impl(heap, block_head), true /* free */);
#endif

    multi_heap_free_impl(heap, block_head);
    multi_heap_internal_unlock(heap);
}

void multi_heap_free_impl(multi_heap_handle_t heap, void *p)
{
    heap_block_t *pb = get_block(p);

    if (heap == NULL || p == NULL) {
        return;
    }

    multi_heap_internal_lock(heap);

    assert_valid_block(heap, pb);
    MULTI_HEAP_ASSERT(!is_free(pb), pb); // block should not be free
    MULTI_HEAP_ASSERT(!is_last_block(pb), pb); // block should not be last block
    MULTI_HEAP_ASSERT(!is_first_block(heap, pb), pb); // block should not be first block

    heap_block_t *next = get_next_block(pb);

    /* Update freelist pointers */
    heap_block_t *prev_free = get_prev_free_block(heap, pb);
    // freelist validity check
    MULTI_HEAP_ASSERT(prev_free->next_free == NULL || prev_free->next_free > pb, &prev_free->next_free);
    pb->next_free = prev_free->next_free;
    prev_free->next_free = pb;

    /* Mark this block as free */
    pb->header |= BLOCK_FREE_FLAG;

    heap->free_bytes += block_data_size(pb);

    /* Try and merge previous free block into this one */
    if (get_next_block(prev_free) == pb) {
        pb = merge_adjacent(heap, prev_free, pb);
    }

    /* If next block is free, try to merge the two */
    if (is_free(next)) {
        pb = merge_adjacent(heap, pb, next);
    }

    multi_heap_internal_unlock(heap);
}


void *multi_heap_realloc_impl(multi_heap_handle_t heap, void *p, size_t size)
{
    heap_block_t *pb = get_block(p);
    void *result;
    size = ALIGN_UP(size);

    assert(heap != NULL);

    if (p == NULL) {
        return multi_heap_malloc_impl(heap, size);
    }

    assert_valid_block(heap, pb);
    // non-null realloc arg should be allocated
    MULTI_HEAP_ASSERT(!is_free(pb), pb);

    if (size == 0) {
        /* note: calling multi_free_impl() here as we've already been
           through any poison-unwrapping */
        multi_heap_free_impl(heap, p);
        return NULL;
    }

    if (heap == NULL) {
        return NULL;
    }

    multi_heap_internal_lock(heap);
    result = NULL;

    if (size <= block_data_size(pb)) {
        // Shrinking....
        split_if_necessary(heap, pb, size, NULL);
        result = pb->data;
    }
    else if (heap->free_bytes < size - block_data_size(pb)) {
        // Growing, but there's not enough total free space in the heap
        multi_heap_internal_unlock(heap);
        return NULL;
    }

    // New size is larger than existing block
    if (result == NULL) {
        // See if we can grow into one or both adjacent blocks
        heap_block_t *orig_pb = pb;
        size_t orig_size = block_data_size(orig_pb);
        heap_block_t *next = get_next_block(pb);
        heap_block_t *prev = get_prev_free_block(heap, pb);

        // Can only grow into the previous free block if it's adjacent
        size_t prev_grow_size = (get_next_block(prev) == pb) ? block_data_size(prev) : 0;

        // Can grow into next block? (we may also need to grow into 'prev' to get to our desired size)
        if (is_free(next) && (block_data_size(pb) + block_data_size(next) + prev_grow_size >= size)) {
            pb = merge_adjacent(heap, pb, next);
        }

        // Can grow into previous block?
        // (try this even if we're already big enough from growing into 'next', as it reduces fragmentation)
        if (prev_grow_size > 0 && (block_data_size(pb) + prev_grow_size >= size)) {
            pb = merge_adjacent(heap, prev, pb);
            // this doesn't guarantee we'll be left with a big enough block, as it's
            // possible for the merge to fail if prev == heap->first_block
        }

        if (block_data_size(pb) >= size) {
            memmove(pb->data, orig_pb->data, orig_size);
            split_if_necessary(heap, pb, size, NULL);
            result = pb->data;
        }
    }

    if (result == NULL) {
        // Need to allocate elsewhere and copy data over
        //
        // (Calling _impl versions here as we've already been through any
        // unwrapping for heap poisoning features.)
        result = multi_heap_malloc_impl(heap, size);
        if (result != NULL) {
            memcpy(result, pb->data, block_data_size(pb));
            multi_heap_free_impl(heap, pb->data);
        }
    }

    if (heap->free_bytes < heap->minimum_free_bytes) {
        heap->minimum_free_bytes = heap->free_bytes;
    }

    multi_heap_internal_unlock(heap);
    return result;
}

#define FAIL_PRINT(MSG, ...) do {                                       \
        if (print_errors) {                                             \
            MULTI_HEAP_STDERR_PRINTF(MSG, __VA_ARGS__);                 \
        }                                                               \
        valid = false;                                                  \
    }                                                                   \
    while(0)

bool multi_heap_check(multi_heap_handle_t heap, bool print_errors)
{
    bool valid = true;
    size_t total_free_bytes = 0;
    assert(heap != NULL);

    multi_heap_internal_lock(heap);

    heap_block_t *prev = NULL;
    heap_block_t *prev_free = NULL;
    heap_block_t *expected_free = NULL;

    /* note: not using get_next_block() in loop, so that assertions aren't checked here */
    for(heap_block_t *b = &heap->first_block; b != NULL; b = (heap_block_t *)(b->header & NEXT_BLOCK_MASK)) {
        if (b == prev) {
            FAIL_PRINT("CORRUPT HEAP: Block %p points to itself\n", b);
            goto done;
        }
        if (b < prev) {
            FAIL_PRINT("CORRUPT HEAP: Block %p is before prev block %p\n", b, prev);
            goto done;
        }
        if (b > heap->last_block || b < &heap->first_block) {
            FAIL_PRINT("CORRUPT HEAP: Block %p is outside heap (last valid block %p)\n", b, prev);
            goto done;
        }
        if (is_free(b)) {
            if (prev != NULL && is_free(prev) && !is_first_block(heap, prev) && !is_last_block(b)) {
                FAIL_PRINT("CORRUPT HEAP: Two adjacent free blocks found, %p and %p\n", prev, b);
            }
            if (expected_free != NULL && expected_free != b) {
                FAIL_PRINT("CORRUPT HEAP: Prev free block %p pointed to next free %p but this free block is %p\n",
                       prev_free, expected_free, b);
            }
            prev_free = b;
            expected_free = b->next_free;
            if (!is_first_block(heap, b)) {
                total_free_bytes += block_data_size(b);
            }
        }
        prev = b;

#ifdef MULTI_HEAP_POISONING
        if (!is_last_block(b)) {
            /* For slow heap poisoning, any block should contain correct poisoning patterns and/or fills */
            bool poison_ok;
            if (is_free(b) && b != heap->last_block) {
                uint32_t block_len = (intptr_t)get_next_block(b) - (intptr_t)b - sizeof(heap_block_t);
                poison_ok = multi_heap_internal_check_block_poisoning(&b[1], block_len, true, print_errors);
            }
            else {
                poison_ok = multi_heap_internal_check_block_poisoning(b->data, block_data_size(b), false, print_errors);
            }
            valid = poison_ok && valid;
        }
#endif

    } /* for(heap_block_t b = ... */

    if (prev != heap->last_block) {
        FAIL_PRINT("CORRUPT HEAP: Last block %p not %p\n", prev, heap->last_block);
    }
    if (!is_free(heap->last_block)) {
        FAIL_PRINT("CORRUPT HEAP: Expected prev block %p to be free\n", heap->last_block);
    }

    if (heap->free_bytes != total_free_bytes) {
        FAIL_PRINT("CORRUPT HEAP: Expected %u free bytes counted %u\n", (unsigned)heap->free_bytes, (unsigned)total_free_bytes);
    }

 done:
    multi_heap_internal_unlock(heap);

    return valid;
}

void multi_heap_dump(multi_heap_handle_t heap)
{
    assert(heap != NULL);

    multi_heap_internal_lock(heap);
    MULTI_HEAP_STDERR_PRINTF("Heap start %p end %p\nFirst free block %p\n", &heap->first_block, heap->last_block, heap->first_block.next_free);
    for(heap_block_t *b = &heap->first_block; b != NULL; b = get_next_block(b)) {
        MULTI_HEAP_STDERR_PRINTF("Block %p data size 0x%08x bytes next block %p", b, block_data_size(b), get_next_block(b));
        if (is_free(b)) {
            MULTI_HEAP_STDERR_PRINTF(" FREE. Next free %p\n", b->next_free);
        } else {
            MULTI_HEAP_STDERR_PRINTF("%s", "\n"); /* C macros & optional __VA_ARGS__ */
        }
    }
    multi_heap_internal_unlock(heap);
}

size_t multi_heap_free_size_impl(multi_heap_handle_t heap)
{
    if (heap == NULL) {
        return 0;
    }
    return heap->free_bytes;
}

size_t multi_heap_minimum_free_size_impl(multi_heap_handle_t heap)
{
    if (heap == NULL) {
        return 0;
    }
    return heap->minimum_free_bytes;
}

void multi_heap_get_info_impl(multi_heap_handle_t heap, multi_heap_info_t *info)
{
    memset(info, 0, sizeof(multi_heap_info_t));

    if (heap == NULL) {
        return;
    }

    multi_heap_internal_lock(heap);
    for(heap_block_t *b = get_next_block(&heap->first_block); !is_last_block(b); b = get_next_block(b)) {
        info->total_blocks++;
        if (is_free(b)) {
            size_t s = block_data_size(b);
            info->total_free_bytes += s;
            if (s > info->largest_free_block) {
                info->largest_free_block = s;
            }
            info->free_blocks++;
        } else {
            info->total_allocated_bytes += block_data_size(b);
            info->allocated_blocks++;
        }
    }

    info->minimum_free_bytes = heap->minimum_free_bytes;
    // heap has wrong total size (address printed here is not indicative of the real error)
    MULTI_HEAP_ASSERT(info->total_free_bytes == heap->free_bytes, heap);

    multi_heap_internal_unlock(heap);

}

#endif
