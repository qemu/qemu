#ifndef VM_H
#define VM_H

#include "processor.h"

#define PAGE_SIZE 4096ul
#ifdef __x86_64__
#define LARGE_PAGE_SIZE (512 * PAGE_SIZE)
#else
#define LARGE_PAGE_SIZE (1024 * PAGE_SIZE)
#endif

#define PTE_PRESENT (1ull << 0)
#define PTE_PSE     (1ull << 7)
#define PTE_WRITE   (1ull << 1)
#define PTE_USER    (1ull << 2)
#define PTE_ADDR    (0xffffffffff000ull)

void setup_vm();

void *vmalloc(unsigned long size);
void vfree(void *mem);
void *vmap(unsigned long long phys, unsigned long size);
void *alloc_vpage(void);
void *alloc_vpages(ulong nr);
uint64_t virt_to_phys_cr3(void *mem);

unsigned long *get_pte(unsigned long *cr3, void *virt);
unsigned long *install_pte(unsigned long *cr3,
                           int pte_level,
                           void *virt,
                           unsigned long pte,
                           unsigned long *pt_page);

void *alloc_page();

unsigned long *install_large_page(unsigned long *cr3,unsigned long phys,
                                  void *virt);
unsigned long *install_page(unsigned long *cr3, unsigned long phys, void *virt);

static inline unsigned long virt_to_phys(const void *virt)
{
    return (unsigned long)virt;
}

static inline void *phys_to_virt(unsigned long phys)
{
    return (void *)phys;
}

#endif
