/*
 * Copyright (c) 2018 Virtuozzo International GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 *
 */

#include "qemu/osdep.h"
#include "addrspace.h"

static struct pa_block *pa_space_find_block(struct pa_space *ps, uint64_t pa)
{
    size_t i;

    for (i = 0; i < ps->block_nr; i++) {
        if (ps->block[i].paddr <= pa &&
                pa < ps->block[i].paddr + ps->block[i].size) {
            return ps->block + i;
        }
    }

    return NULL;
}

static void *pa_space_resolve(struct pa_space *ps, uint64_t pa)
{
    struct pa_block *block = pa_space_find_block(ps, pa);

    if (!block) {
        return NULL;
    }

    return block->addr + (pa - block->paddr);
}

static bool pa_space_read64(struct pa_space *ps, uint64_t pa, uint64_t *value)
{
    uint64_t *resolved = pa_space_resolve(ps, pa);

    if (!resolved) {
        return false;
    }

    *value = *resolved;

    return true;
}

static void pa_block_align(struct pa_block *b)
{
    uint64_t low_align = ((b->paddr - 1) | ELF2DMP_PAGE_MASK) + 1 - b->paddr;
    uint64_t high_align = (b->paddr + b->size) & ELF2DMP_PAGE_MASK;

    if (low_align == 0 && high_align == 0) {
        return;
    }

    if (low_align + high_align < b->size) {
        printf("Block 0x%"PRIx64"+:0x%"PRIx64" will be aligned to "
                "0x%"PRIx64"+:0x%"PRIx64"\n", b->paddr, b->size,
                b->paddr + low_align, b->size - low_align - high_align);
        b->size -= low_align + high_align;
    } else {
        printf("Block 0x%"PRIx64"+:0x%"PRIx64" is too small to align\n",
                b->paddr, b->size);
        b->size = 0;
    }

    b->addr += low_align;
    b->paddr += low_align;
}

void pa_space_create(struct pa_space *ps, QEMU_Elf *qemu_elf)
{
    Elf64_Half phdr_nr = elf_getphdrnum(qemu_elf->map);
    Elf64_Phdr *phdr = elf64_getphdr(qemu_elf->map);
    size_t block_i = 0;
    size_t i;

    ps->block_nr = 0;

    for (i = 0; i < phdr_nr; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            ps->block_nr++;
        }
    }

    ps->block = g_new(struct pa_block, ps->block_nr);

    for (i = 0; i < phdr_nr; i++) {
        if (phdr[i].p_type == PT_LOAD && phdr[i].p_offset < qemu_elf->size) {
            ps->block[block_i] = (struct pa_block) {
                .addr = (uint8_t *)qemu_elf->map + phdr[i].p_offset,
                .paddr = phdr[i].p_paddr,
                .size = MIN(phdr[i].p_filesz,
                            qemu_elf->size - phdr[i].p_offset),
            };
            pa_block_align(&ps->block[block_i]);
            block_i = ps->block[block_i].size ? (block_i + 1) : block_i;
        }
    }

    ps->block_nr = block_i;
}

void pa_space_destroy(struct pa_space *ps)
{
    ps->block_nr = 0;
    g_free(ps->block);
}

void va_space_set_dtb(struct va_space *vs, uint64_t dtb)
{
    vs->dtb = dtb & 0x00ffffffffff000;
}

void va_space_create(struct va_space *vs, struct pa_space *ps, uint64_t dtb)
{
    vs->ps = ps;
    va_space_set_dtb(vs, dtb);
}

static bool get_pml4e(struct va_space *vs, uint64_t va, uint64_t *value)
{
    uint64_t pa = (vs->dtb & 0xffffffffff000) | ((va & 0xff8000000000) >> 36);

    return pa_space_read64(vs->ps, pa, value);
}

static bool get_pdpi(struct va_space *vs, uint64_t va, uint64_t pml4e,
                    uint64_t *value)
{
    uint64_t pdpte_paddr = (pml4e & 0xffffffffff000) |
        ((va & 0x7FC0000000) >> 27);

    return pa_space_read64(vs->ps, pdpte_paddr, value);
}

static uint64_t pde_index(uint64_t va)
{
    return (va >> 21) & 0x1FF;
}

static uint64_t pdba_base(uint64_t pdpe)
{
    return pdpe & 0xFFFFFFFFFF000;
}

static bool get_pgd(struct va_space *vs, uint64_t va, uint64_t pdpe,
                   uint64_t *value)
{
    uint64_t pgd_entry = pdba_base(pdpe) + pde_index(va) * 8;

    return pa_space_read64(vs->ps, pgd_entry, value);
}

static uint64_t pte_index(uint64_t va)
{
    return (va >> 12) & 0x1FF;
}

static uint64_t ptba_base(uint64_t pde)
{
    return pde & 0xFFFFFFFFFF000;
}

static bool get_pte(struct va_space *vs, uint64_t va, uint64_t pgd,
                   uint64_t *value)
{
    uint64_t pgd_val = ptba_base(pgd) + pte_index(va) * 8;

    return pa_space_read64(vs->ps, pgd_val, value);
}

static uint64_t get_paddr(uint64_t va, uint64_t pte)
{
    return (pte & 0xFFFFFFFFFF000) | (va & 0xFFF);
}

static bool is_present(uint64_t entry)
{
    return entry & 0x1;
}

static bool page_size_flag(uint64_t entry)
{
    return entry & (1 << 7);
}

static uint64_t get_1GB_paddr(uint64_t va, uint64_t pdpte)
{
    return (pdpte & 0xfffffc0000000) | (va & 0x3fffffff);
}

static uint64_t get_2MB_paddr(uint64_t va, uint64_t pgd_entry)
{
    return (pgd_entry & 0xfffffffe00000) | (va & 0x00000001fffff);
}

static uint64_t va_space_va2pa(struct va_space *vs, uint64_t va)
{
    uint64_t pml4e, pdpe, pgd, pte;

    if (!get_pml4e(vs, va, &pml4e) || !is_present(pml4e)) {
        return INVALID_PA;
    }

    if (!get_pdpi(vs, va, pml4e, &pdpe) || !is_present(pdpe)) {
        return INVALID_PA;
    }

    if (page_size_flag(pdpe)) {
        return get_1GB_paddr(va, pdpe);
    }

    if (!get_pgd(vs, va, pdpe, &pgd) || !is_present(pgd)) {
        return INVALID_PA;
    }

    if (page_size_flag(pgd)) {
        return get_2MB_paddr(va, pgd);
    }

    if (!get_pte(vs, va, pgd, &pte) || !is_present(pte)) {
        return INVALID_PA;
    }

    return get_paddr(va, pte);
}

void *va_space_resolve(struct va_space *vs, uint64_t va)
{
    uint64_t pa = va_space_va2pa(vs, va);

    if (pa == INVALID_PA) {
        return NULL;
    }

    return pa_space_resolve(vs->ps, pa);
}

bool va_space_rw(struct va_space *vs, uint64_t addr,
                 void *buf, size_t size, int is_write)
{
    while (size) {
        uint64_t page = addr & ELF2DMP_PFN_MASK;
        size_t s = (page + ELF2DMP_PAGE_SIZE) - addr;
        void *ptr;

        s = (s > size) ? size : s;

        ptr = va_space_resolve(vs, addr);
        if (!ptr) {
            return false;
        }

        if (is_write) {
            memcpy(ptr, buf, s);
        } else {
            memcpy(buf, ptr, s);
        }

        size -= s;
        buf = (uint8_t *)buf + s;
        addr += s;
    }

    return true;
}
