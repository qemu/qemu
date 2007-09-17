/*
 * KQEMU header
 *
 * Copyright (c) 2004-2006 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef KQEMU_H
#define KQEMU_H

#define KQEMU_VERSION 0x010300

struct kqemu_segment_cache {
    uint32_t selector;
    unsigned long base;
    uint32_t limit;
    uint32_t flags;
};

struct kqemu_cpu_state {
#ifdef __x86_64__
    unsigned long regs[16];
#else
    unsigned long regs[8];
#endif
    unsigned long eip;
    unsigned long eflags;

    uint32_t dummy0, dummy1, dumm2, dummy3, dummy4;

    struct kqemu_segment_cache segs[6]; /* selector values */
    struct kqemu_segment_cache ldt;
    struct kqemu_segment_cache tr;
    struct kqemu_segment_cache gdt; /* only base and limit are used */
    struct kqemu_segment_cache idt; /* only base and limit are used */

    unsigned long cr0;
    unsigned long dummy5;
    unsigned long cr2;
    unsigned long cr3;
    unsigned long cr4;
    uint32_t a20_mask;

    /* sysenter registers */
    uint32_t sysenter_cs;
    uint32_t sysenter_esp;
    uint32_t sysenter_eip;
    uint64_t efer __attribute__((aligned(8)));
    uint64_t star;
#ifdef __x86_64__
    unsigned long lstar;
    unsigned long cstar;
    unsigned long fmask;
    unsigned long kernelgsbase;
#endif
    uint64_t tsc_offset;

    unsigned long dr0;
    unsigned long dr1;
    unsigned long dr2;
    unsigned long dr3;
    unsigned long dr6;
    unsigned long dr7;

    uint8_t cpl;
    uint8_t user_only;

    uint32_t error_code; /* error_code when exiting with an exception */
    unsigned long next_eip; /* next eip value when exiting with an interrupt */
    unsigned int nb_pages_to_flush; /* number of pages to flush,
                                       KQEMU_FLUSH_ALL means full flush */
#define KQEMU_MAX_PAGES_TO_FLUSH 512
#define KQEMU_FLUSH_ALL (KQEMU_MAX_PAGES_TO_FLUSH + 1)

    long retval;

    /* number of ram_dirty entries to update */
    unsigned int nb_ram_pages_to_update;
#define KQEMU_MAX_RAM_PAGES_TO_UPDATE 512
#define KQEMU_RAM_PAGES_UPDATE_ALL (KQEMU_MAX_RAM_PAGES_TO_UPDATE + 1)

#define KQEMU_MAX_MODIFIED_RAM_PAGES 512
    unsigned int nb_modified_ram_pages;
};

struct kqemu_init {
    uint8_t *ram_base; /* must be page aligned */
    unsigned long ram_size; /* must be multiple of 4 KB */
    uint8_t *ram_dirty; /* must be page aligned */
    uint32_t **phys_to_ram_map; /* must be page aligned */
    unsigned long *pages_to_flush; /* must be page aligned */
    unsigned long *ram_pages_to_update; /* must be page aligned */
    unsigned long *modified_ram_pages; /* must be page aligned */
};

#define KQEMU_RET_ABORT    (-1)
#define KQEMU_RET_EXCEPTION 0x0000 /* 8 low order bit are the exception */
#define KQEMU_RET_INT       0x0100 /* 8 low order bit are the interrupt */
#define KQEMU_RET_SOFTMMU   0x0200 /* emulation needed (I/O or
                                      unsupported INSN) */
#define KQEMU_RET_INTR      0x0201 /* interrupted by a signal */
#define KQEMU_RET_SYSCALL   0x0300 /* syscall insn */

#ifdef _WIN32
#define KQEMU_EXEC             CTL_CODE(FILE_DEVICE_UNKNOWN, 1, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define KQEMU_INIT             CTL_CODE(FILE_DEVICE_UNKNOWN, 2, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define KQEMU_GET_VERSION      CTL_CODE(FILE_DEVICE_UNKNOWN, 3, METHOD_BUFFERED, FILE_READ_ACCESS)
#define KQEMU_MODIFY_RAM_PAGES CTL_CODE(FILE_DEVICE_UNKNOWN, 4, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#else
#define KQEMU_EXEC             _IOWR('q', 1, struct kqemu_cpu_state)
#define KQEMU_INIT             _IOW('q', 2, struct kqemu_init)
#define KQEMU_GET_VERSION      _IOR('q', 3, int)
#define KQEMU_MODIFY_RAM_PAGES _IOW('q', 4, int)
#endif

#endif /* KQEMU_H */
