/*
 * Copyright (c) 2018 Virtuozzo International GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 *
 */

#ifndef ADDRSPACE_H
#define ADDRSPACE_H

#include "qemu_elf.h"

#define ELF2DMP_PAGE_BITS 12
#define ELF2DMP_PAGE_SIZE (1ULL << ELF2DMP_PAGE_BITS)
#define ELF2DMP_PFN_MASK (~(ELF2DMP_PAGE_SIZE - 1))

#define INVALID_PA  UINT64_MAX

struct pa_block {
    uint8_t *addr;
    uint64_t paddr;
    uint64_t size;
};

struct pa_space {
    size_t block_nr;
    struct pa_block *block;
};

struct va_space {
    uint64_t dtb;
    struct pa_space *ps;
};

int pa_space_create(struct pa_space *ps, QEMU_Elf *qemu_elf);
void pa_space_destroy(struct pa_space *ps);

void va_space_create(struct va_space *vs, struct pa_space *ps, uint64_t dtb);
void va_space_set_dtb(struct va_space *vs, uint64_t dtb);
void *va_space_resolve(struct va_space *vs, uint64_t va);
int va_space_rw(struct va_space *vs, uint64_t addr,
        void *buf, size_t size, int is_write);

#endif /* ADDRSPACE_H */
