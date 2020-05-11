#
# Component Makefile
#

COMPONENT_OBJS := heap_caps_init.o heap_caps.o multi_heap.o multi_heap_poisoning.o malloc.o spiram_mmap.o

# Flags for dlmalloc
# Sync malloc_getpagesize with SPIRAM_MMAP_PAGE_SIZE
CFLAGS += -DMSPACES=1 -DONLY_MSPACES=1 -DUSE_LOCKS=2 -DUSE_SPIN_LOCKS=0 -DHAVE_MMAP=1 -Dmalloc_getpagesize=32768 -DMALLOC_FAILURE_ACTION= -DLACKS_TIME_H=1

COMPONENT_ADD_LDFRAGMENTS += linker.lf

CFLAGS += -DMULTI_HEAP_FREERTOS
