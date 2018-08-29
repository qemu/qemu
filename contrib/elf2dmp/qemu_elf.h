/*
 * Copyright (c) 2018 Virtuozzo International GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 *
 */

#ifndef QEMU_ELF_H
#define QEMU_ELF_H

#include <stdint.h>
#include <elf.h>

typedef struct QEMUCPUSegment {
    uint32_t selector;
    uint32_t limit;
    uint32_t flags;
    uint32_t pad;
    uint64_t base;
} QEMUCPUSegment;

typedef struct QEMUCPUState {
    uint32_t version;
    uint32_t size;
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rsp, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rip, rflags;
    QEMUCPUSegment cs, ds, es, fs, gs, ss;
    QEMUCPUSegment ldt, tr, gdt, idt;
    uint64_t cr[5];
    uint64_t kernel_gs_base;
} QEMUCPUState;

int is_system(QEMUCPUState *s);

typedef struct QEMU_Elf {
    int fd;
    size_t size;
    void *map;
    QEMUCPUState **state;
    size_t state_nr;
    int has_kernel_gs_base;
} QEMU_Elf;

int QEMU_Elf_init(QEMU_Elf *qe, const char *filename);
void QEMU_Elf_exit(QEMU_Elf *qe);

Elf64_Phdr *elf64_getphdr(void *map);
Elf64_Half elf_getphdrnum(void *map);

#endif /* QEMU_ELF_H */
