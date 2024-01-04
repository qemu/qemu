#include "fwcfg.h"
#include "vm.h"
#include "libcflat.h"

#define PAGE_SIZE 4096ul
#ifdef __x86_64__
#define LARGE_PAGE_SIZE (512 * PAGE_SIZE)
#else
#define LARGE_PAGE_SIZE (1024 * PAGE_SIZE)
#endif

static void *free = 0;
static void *vfree_top = 0;

static void free_memory(void *mem, unsigned long size)
{
    while (size >= PAGE_SIZE) {
	*(void **)mem = free;
	free = mem;
	mem += PAGE_SIZE;
	size -= PAGE_SIZE;
    }
}

void *alloc_page()
{
    void *p;

    if (!free)
	return 0;

    p = free;
    free = *(void **)free;

    return p;
}

void free_page(void *page)
{
    *(void **)page = free;
    free = page;
}

extern char edata;
static unsigned long end_of_memory;

#ifdef __x86_64__
#define	PAGE_LEVEL	4
#define	PGDIR_WIDTH	9
#define	PGDIR_MASK	511
#else
#define	PAGE_LEVEL	2
#define	PGDIR_WIDTH	10
#define	PGDIR_MASK	1023
#endif

unsigned long *install_pte(unsigned long *cr3,
			   int pte_level,
			   void *virt,
			   unsigned long pte,
			   unsigned long *pt_page)
{
    int level;
    unsigned long *pt = cr3;
    unsigned offset;

    for (level = PAGE_LEVEL; level > pte_level; --level) {
	offset = ((unsigned long)virt >> ((level-1) * PGDIR_WIDTH + 12)) & PGDIR_MASK;
	if (!(pt[offset] & PTE_PRESENT)) {
	    unsigned long *new_pt = pt_page;
            if (!new_pt)
                new_pt = alloc_page();
            else
                pt_page = 0;
	    memset(new_pt, 0, PAGE_SIZE);
	    pt[offset] = virt_to_phys(new_pt) | PTE_PRESENT | PTE_WRITE | PTE_USER;
	}
	pt = phys_to_virt(pt[offset] & 0xffffffffff000ull);
    }
    offset = ((unsigned long)virt >> ((level-1) * PGDIR_WIDTH + 12)) & PGDIR_MASK;
    pt[offset] = pte;
    return &pt[offset];
}

unsigned long *get_pte(unsigned long *cr3, void *virt)
{
    int level;
    unsigned long *pt = cr3, pte;
    unsigned offset;

    for (level = PAGE_LEVEL; level > 1; --level) {
	offset = ((unsigned long)virt >> (((level-1) * PGDIR_WIDTH) + 12)) & PGDIR_MASK;
	pte = pt[offset];
	if (!(pte & PTE_PRESENT))
	    return NULL;
	if (level == 2 && (pte & PTE_PSE))
	    return &pt[offset];
	pt = phys_to_virt(pte & 0xffffffffff000ull);
    }
    offset = ((unsigned long)virt >> (((level-1) * PGDIR_WIDTH) + 12)) & PGDIR_MASK;
    return &pt[offset];
}

unsigned long *install_large_page(unsigned long *cr3,
				  unsigned long phys,
				  void *virt)
{
    return install_pte(cr3, 2, virt,
		       phys | PTE_PRESENT | PTE_WRITE | PTE_USER | PTE_PSE, 0);
}

unsigned long *install_page(unsigned long *cr3,
			    unsigned long phys,
			    void *virt)
{
    return install_pte(cr3, 1, virt, phys | PTE_PRESENT | PTE_WRITE | PTE_USER, 0);
}


static void setup_mmu_range(unsigned long *cr3, unsigned long start,
			    unsigned long len)
{
	u64 max = (u64)len + (u64)start;
	u64 phys = start;

	while (phys + LARGE_PAGE_SIZE <= max) {
		install_large_page(cr3, phys, (void *)(ulong)phys);
		phys += LARGE_PAGE_SIZE;
	}
	while (phys + PAGE_SIZE <= max) {
		install_page(cr3, phys, (void *)(ulong)phys);
		phys += PAGE_SIZE;
	}
}

static void setup_mmu(unsigned long len)
{
    unsigned long *cr3 = alloc_page();

    memset(cr3, 0, PAGE_SIZE);

#ifdef __x86_64__
    if (len < (1ul << 32))
        len = (1ul << 32);  /* map mmio 1:1 */

    setup_mmu_range(cr3, 0, len);
#else
    if (len > (1ul << 31))
	    len = (1ul << 31);

    /* 0 - 2G memory, 2G-3G valloc area, 3G-4G mmio */
    setup_mmu_range(cr3, 0, len);
    setup_mmu_range(cr3, 3ul << 30, (1ul << 30));
    vfree_top = (void*)(3ul << 30);
#endif

    write_cr3(virt_to_phys(cr3));
#ifndef __x86_64__
    write_cr4(X86_CR4_PSE);
#endif
    write_cr0(X86_CR0_PG |X86_CR0_PE | X86_CR0_WP);

    printf("paging enabled\n");
    printf("cr0 = %x\n", read_cr0());
    printf("cr3 = %x\n", read_cr3());
    printf("cr4 = %x\n", read_cr4());
}

void setup_vm()
{
    end_of_memory = fwcfg_get_u64(FW_CFG_RAM_SIZE);
    free_memory(&edata, end_of_memory - (unsigned long)&edata);
    setup_mmu(end_of_memory);
}

void *vmalloc(unsigned long size)
{
    void *mem, *p;
    unsigned pages;

    size += sizeof(unsigned long);

    size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    vfree_top -= size;
    mem = p = vfree_top;
    pages = size / PAGE_SIZE;
    while (pages--) {
	install_page(phys_to_virt(read_cr3()), virt_to_phys(alloc_page()), p);
	p += PAGE_SIZE;
    }
    *(unsigned long *)mem = size;
    mem += sizeof(unsigned long);
    return mem;
}

uint64_t virt_to_phys_cr3(void *mem)
{
    return (*get_pte(phys_to_virt(read_cr3()), mem) & PTE_ADDR) + ((ulong)mem & (PAGE_SIZE - 1));
}

void vfree(void *mem)
{
    unsigned long size = ((unsigned long *)mem)[-1];

    while (size) {
	free_page(phys_to_virt(*get_pte(phys_to_virt(read_cr3()), mem) & PTE_ADDR));
	mem += PAGE_SIZE;
	size -= PAGE_SIZE;
    }
}

void *vmap(unsigned long long phys, unsigned long size)
{
    void *mem, *p;
    unsigned pages;

    size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    vfree_top -= size;
    phys &= ~(unsigned long long)(PAGE_SIZE - 1);

    mem = p = vfree_top;
    pages = size / PAGE_SIZE;
    while (pages--) {
	install_page(phys_to_virt(read_cr3()), phys, p);
	phys += PAGE_SIZE;
	p += PAGE_SIZE;
    }
    return mem;
}

void *alloc_vpages(ulong nr)
{
	vfree_top -= PAGE_SIZE * nr;
	return vfree_top;
}

void *alloc_vpage(void)
{
    return alloc_vpages(1);
}
