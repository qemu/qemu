/*
 * KQEMU header
 * 
 * Copyright (c) 2004-2008 Fabrice Bellard
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

#if defined(__i386__)
#define KQEMU_PAD32(x) x
#else
#define KQEMU_PAD32(x)
#endif

#define KQEMU_VERSION 0x010400

struct kqemu_segment_cache {
    uint16_t selector;
    uint16_t padding1;
    uint32_t flags;
    uint64_t base;
    uint32_t limit;
    uint32_t padding2;
};

struct kqemu_cpu_state {
    uint64_t regs[16];
    uint64_t eip;
    uint64_t eflags;

    struct kqemu_segment_cache segs[6]; /* selector values */
    struct kqemu_segment_cache ldt;
    struct kqemu_segment_cache tr;
    struct kqemu_segment_cache gdt; /* only base and limit are used */
    struct kqemu_segment_cache idt; /* only base and limit are used */

    uint64_t cr0;
    uint64_t cr2;
    uint64_t cr3;
    uint64_t cr4;
    uint64_t a20_mask;

    /* sysenter registers */
    uint64_t sysenter_cs;
    uint64_t sysenter_esp;
    uint64_t sysenter_eip;
    uint64_t efer;
    uint64_t star;
    
    uint64_t lstar;
    uint64_t cstar;
    uint64_t fmask;
    uint64_t kernelgsbase;

    uint64_t tsc_offset;

    uint64_t dr0;
    uint64_t dr1;
    uint64_t dr2;
    uint64_t dr3;
    uint64_t dr6;
    uint64_t dr7;

    uint8_t cpl;
    uint8_t user_only;
    uint16_t padding1;

    uint32_t error_code; /* error_code when exiting with an exception */
    uint64_t next_eip; /* next eip value when exiting with an interrupt */
    uint32_t nb_pages_to_flush; /* number of pages to flush,
                                       KQEMU_FLUSH_ALL means full flush */
#define KQEMU_MAX_PAGES_TO_FLUSH 512
#define KQEMU_FLUSH_ALL (KQEMU_MAX_PAGES_TO_FLUSH + 1)

    int32_t retval;

    /* number of ram_dirty entries to update */
    uint32_t nb_ram_pages_to_update; 
#define KQEMU_MAX_RAM_PAGES_TO_UPDATE 512
#define KQEMU_RAM_PAGES_UPDATE_ALL (KQEMU_MAX_RAM_PAGES_TO_UPDATE + 1)

#define KQEMU_MAX_MODIFIED_RAM_PAGES 512
    uint32_t nb_modified_ram_pages;
};

struct kqemu_init {
    uint8_t *ram_base; /* must be page aligned */
    KQEMU_PAD32(uint32_t padding1;)
    uint64_t ram_size; /* must be multiple of 4 KB */
    uint8_t *ram_dirty; /* must be page aligned */
    KQEMU_PAD32(uint32_t padding2;)
    uint64_t *pages_to_flush; /* must be page aligned */
    KQEMU_PAD32(uint32_t padding4;)
    uint64_t *ram_pages_to_update; /* must be page aligned */
    KQEMU_PAD32(uint32_t padding5;)
    uint64_t *modified_ram_pages; /* must be page aligned */
    KQEMU_PAD32(uint32_t padding6;)
};

#define KQEMU_IO_MEM_RAM        0
#define KQEMU_IO_MEM_ROM        1
#define KQEMU_IO_MEM_COMM       2 /* kqemu communication page */
#define KQEMU_IO_MEM_UNASSIGNED 3 /* any device: return to application */

struct kqemu_phys_mem {
    uint64_t phys_addr; /* physical address range: phys_addr,
                           phys_addr + size */
    uint64_t size;        
    uint64_t ram_addr;  /* corresponding ram address */
    uint32_t io_index;  /* memory type: see KQEMU_IO_MEM_xxx */
    uint32_t padding1;
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
#define KQEMU_SET_PHYS_MEM     CTL_CODE(FILE_DEVICE_UNKNOWN, 5, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#else
#define KQEMU_EXEC             _IOWR('q', 1, struct kqemu_cpu_state)
#define KQEMU_INIT             _IOW('q', 2, struct kqemu_init)
#define KQEMU_GET_VERSION      _IOR('q', 3, int)
#define KQEMU_MODIFY_RAM_PAGES _IOW('q', 4, int)
#define KQEMU_SET_PHYS_MEM     _IOW('q', 5, struct kqemu_phys_mem)
#endif

#endif /* KQEMU_H */
