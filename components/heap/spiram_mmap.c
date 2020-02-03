
#include "malloc.h"

#include <freertos/FreeRTOS.h>

#include <sys/mman.h>

uint32_t gSPIRAMMMappedPages[(SPIRAM_MMAP_NUM_PAGES + 31) / 32];
portMUX_TYPE gSPIRAMMMapMutex;

void *mmap(void *addr, size_t length, int prot, int flags,
           int fd, int offset)
{
    portENTER_CRITICAL(&gSPIRAMMMapMutex);

    int pages = length / SPIRAM_MMAP_PAGE_SIZE;
    int page = 0;
    int ok = 0, okI, okJ;

    for(int i = 0; i < sizeof(gSPIRAMMMappedPages) / sizeof(uint32_t); i++)
    {
        if(gSPIRAMMMappedPages[i] == 0xFFFFFFFF)
        {
            ok = 0;
            page += 32;
        }
        else
        {
            for(int j = 0; page < SPIRAM_MMAP_NUM_PAGES && j < 32; page++, j++)
            {
                if(gSPIRAMMMappedPages[i] & (1 << j))
                    ok = 0;
                else 
                {
                    if(ok == 0)
                    {
                        addr = (void *)(SPIRAM_MMAP_ADDR0 + page * SPIRAM_MMAP_PAGE_SIZE);
                        okI = i;
                        okJ = j;
                    }
                    if(++ok == pages)
                    {
                        for(i = 0; i < pages; i++)
                        {
                            gSPIRAMMMappedPages[okI] |= 1 << okJ;
                            if(okJ == 31)
                            {
                                okI++;
                                okJ = 0;
                            }
                            else
                                okJ++;
                        }

                        portEXIT_CRITICAL(&gSPIRAMMMapMutex);
                        return addr;
                    }
                }
            }
        }
    }

    portEXIT_CRITICAL(&gSPIRAMMMapMutex);
    return MAP_FAILED;
}

extern uint8_t s_mmap_page_refcnt[];
extern mspace gSysAllocAllowedMSpace;

void *mmap_instruction_bus_aligned(int pages, int instruction_pages)
{
    if(gSysAllocAllowedMSpace)
        mspace_trim(gSysAllocAllowedMSpace, 0);
    portENTER_CRITICAL(&gSPIRAMMMapMutex);

    int ok = 0;
    for(int instruction_page = 63; instruction_page >= 0; instruction_page--)
    {
        if(s_mmap_page_refcnt[64 + instruction_page])
            ok = 0;
        else if(++ok == instruction_pages)
        {
            void *addr = (void *)(0x3F800000 + instruction_page * (64 * 1024));
            int page = (((uintptr_t)addr) - SPIRAM_MMAP_ADDR0) / SPIRAM_MMAP_PAGE_SIZE;
            if(page + pages > SPIRAM_MMAP_NUM_PAGES)
            {
                ok--;
                continue;
            }
            if(page < 0)
                return MAP_FAILED;

            int i = page / 32;
            int j = page - i * 32;
            int k;

            for(k = 0; k < pages; k++)
            {
                if(gSPIRAMMMappedPages[i] & (1 << j))
                    break;

                if(i == 31)
                {
                    i++;
                    j = 0;
                }
                else
                    j++;
            }

            if(k == pages)
            {
                void *addr = (void *)(SPIRAM_MMAP_ADDR0 + page * SPIRAM_MMAP_PAGE_SIZE);
                i = page / 32;
                j = page - i * 32;

                for(k = 0; k < pages; k++)
                {
                    gSPIRAMMMappedPages[i] |= 1 << j;

                    if(i == 31)
                    {
                        i++;
                        j = 0;
                    }
                    else
                        j++;
                }

                portEXIT_CRITICAL(&gSPIRAMMMapMutex);
                return addr;
            }
            else
                ok--;
        }
    }

    portEXIT_CRITICAL(&gSPIRAMMMapMutex);
    return MAP_FAILED;
}

int munmap_pages(void *addr, int pages)
{
    int page = (((uintptr_t)addr) - SPIRAM_MMAP_ADDR0) / SPIRAM_MMAP_PAGE_SIZE;
    int i = page / 32;
    int j = page - i * 32;

    for(page = 0; page < pages; page++)
    {
        gSPIRAMMMappedPages[i] &= ~(1 << j);

        if(j == 31)
        {
            i++;
            j = 0;
        }
        else
            j++;
    }

    return 0;
}