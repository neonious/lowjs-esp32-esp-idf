

#ifndef __MMAN_H__
#define __MMAN_H__

#include <stdint.h>
#include <stddef.h>

// Sync with malloc_getpagesize in component.mk
#define SPIRAM_MMAP_PAGE_SIZE       32768

// First 1 MB is always in malloc, next 3 MB in pages
#define SPIRAM_MMAP_NUM_PAGES       ((3 * 1024 * 1024) / SPIRAM_MMAP_PAGE_SIZE)

#define SPIRAM_MMAP_ADDR0           (0x3FC00000 - SPIRAM_MMAP_NUM_PAGES * SPIRAM_MMAP_PAGE_SIZE)

/*
 * Protections are chosen from these bits, or-ed together
 */
#define	PROT_READ	0x04	/* pages can be read */
#define	PROT_WRITE	0x02	/* pages can be written */
#define	PROT_EXEC	0x01	/* pages can be executed */

/*
 * Flags contain mapping type, sharing type and options.
 * Mapping type; choose one
 */
#define	MAP_FILE	0x0001	/* mapped from a file or device */
#define	MAP_ANON	0x0002	/* allocated from memory, swap space */
#define	MAP_TYPE	0x000f	/* mask for type field */

/*
 * Sharing types; choose one
 */
#define	MAP_COPY	0x0020	/* "copy" region at mmap time */
#define	MAP_SHARED	0x0010	/* share changes */
#define	MAP_PRIVATE	0x0000	/* changes are private */

/*
 * Other flags
 */
#define	MAP_FIXED	0x0100	/* map addr must be exactly as requested */
#define	MAP_NOEXTEND	0x0200	/* for MAP_FILE, don't change file size */
#define	MAP_HASSEMPHORE	0x0400	/* region may contain semaphores */
#define	MAP_INHERIT	0x0800	/* region is retained after exec */

/*
 * Advice to madvise
 */
#define	MADV_NORMAL	0	/* no further special treatment */
#define	MADV_RANDOM	1	/* expect random page references */
#define	MADV_SEQUENTIAL	2	/* expect sequential page references */
#define	MADV_WILLNEED	3	/* will need these pages */
#define	MADV_DONTNEED	4	/* dont need these pages */

#define MAP_FAILED ((void *) -1)

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void *mmap(void *addr, size_t length, int prot, int flags,
           int fd, int offset);
#define munmap(a, l) munmap_pages(a, (l) / SPIRAM_MMAP_PAGE_SIZE)

void *mmap_instruction_bus_aligned(int pages, int instruction_pages);
int munmap_pages(void *addr, int pages);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MMAN_H__ */