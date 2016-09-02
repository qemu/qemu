/*
 * Linux Boot Option ROM for fw_cfg DMA
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (c) 2015-2016 Red Hat Inc.
 *   Authors:
 *     Marc Marí <marc.mari.barcelo@gmail.com>
 *     Richard W.M. Jones <rjones@redhat.com>
 */

asm(
".text\n"
".global _start\n"
"_start:\n"
"   .short 0xaa55\n"
"   .byte 3\n" /* desired size in 512 units; signrom.py adds padding */
"   .byte 0xcb\n" /* far return without prefix */
"   .org 0x18\n"
"   .short 0\n"
"   .short _pnph\n"
"_pnph:\n"
"   .ascii \"$PnP\"\n"
"   .byte 0x01\n"
"   .byte (_pnph_len / 16)\n"
"   .short 0x0000\n"
"   .byte 0x00\n"
"   .byte 0x00\n"
"   .long 0x00000000\n"
"   .short _manufacturer\n"
"   .short _product\n"
"   .long 0x00000000\n"
"   .short 0x0000\n"
"   .short 0x0000\n"
"   .short _bev\n"
"   .short 0x0000\n"
"   .short 0x0000\n"
"   .equ _pnph_len, . - _pnph\n"
"_manufacturer:\n"
"   .asciz \"QEMU\"\n"
"_product:\n"
"   .asciz \"Linux loader DMA\"\n"
"   .align 4, 0\n"
"_bev:\n"
"   cli\n"
"   cld\n"
"   jmp load_kernel\n"
);

#include "../../include/hw/nvram/fw_cfg_keys.h"

/* QEMU_CFG_DMA_CONTROL bits */
#define BIOS_CFG_DMA_CTL_ERROR   0x01
#define BIOS_CFG_DMA_CTL_READ    0x02
#define BIOS_CFG_DMA_CTL_SKIP    0x04
#define BIOS_CFG_DMA_CTL_SELECT  0x08

#define BIOS_CFG_DMA_ADDR_HIGH 0x514
#define BIOS_CFG_DMA_ADDR_LOW  0x518

#define uint64_t unsigned long long
#define uint32_t unsigned int
#define uint16_t unsigned short

#define barrier() asm("" : : : "memory")

typedef struct FWCfgDmaAccess {
    uint32_t control;
    uint32_t length;
    uint64_t address;
} __attribute__((packed)) FWCfgDmaAccess;

static inline void outl(uint32_t value, uint16_t port)
{
    asm("outl %0, %w1" : : "a"(value), "Nd"(port));
}

static inline void set_es(void *addr)
{
    uint32_t seg = (uint32_t)addr >> 4;
    asm("movl %0, %%es" : : "r"(seg));
}

#ifdef __clang__
#define ADDR32
#else
#define ADDR32 "addr32 "
#endif

static inline uint16_t readw_es(uint16_t offset)
{
    uint16_t val;
    asm(ADDR32 "movw %%es:(%1), %0" : "=r"(val) : "r"((uint32_t)offset));
    barrier();
    return val;
}

static inline uint32_t readl_es(uint16_t offset)
{
    uint32_t val;
    asm(ADDR32 "movl %%es:(%1), %0" : "=r"(val) : "r"((uint32_t)offset));
    barrier();
    return val;
}

static inline void writel_es(uint16_t offset, uint32_t val)
{
    barrier();
    asm(ADDR32 "movl %0, %%es:(%1)" : : "r"(val), "r"((uint32_t)offset));
}

static inline uint32_t bswap32(uint32_t x)
{
    asm("bswapl %0" : "=r" (x) : "0" (x));
    return x;
}

static inline uint64_t bswap64(uint64_t x)
{
    asm("bswapl %%eax; bswapl %%edx; xchg %%eax, %%edx" : "=A" (x) : "0" (x));
    return x;
}

static inline uint64_t cpu_to_be64(uint64_t x)
{
    return bswap64(x);
}

static inline uint32_t cpu_to_be32(uint32_t x)
{
    return bswap32(x);
}

static inline uint32_t be32_to_cpu(uint32_t x)
{
    return bswap32(x);
}

/* clang is happy to inline this function, and bloats the
 * ROM.
 */
static __attribute__((__noinline__))
void bios_cfg_read_entry(void *buf, uint16_t entry, uint32_t len)
{
    FWCfgDmaAccess access;
    uint32_t control = (entry << 16) | BIOS_CFG_DMA_CTL_SELECT
                        | BIOS_CFG_DMA_CTL_READ;

    access.address = cpu_to_be64((uint64_t)(uint32_t)buf);
    access.length = cpu_to_be32(len);
    access.control = cpu_to_be32(control);

    barrier();

    outl(cpu_to_be32((uint32_t)&access), BIOS_CFG_DMA_ADDR_LOW);

    while (be32_to_cpu(access.control) & ~BIOS_CFG_DMA_CTL_ERROR) {
        barrier();
    }
}

/* Return top of memory using BIOS function E801. */
static uint32_t get_e801_addr(void)
{
    uint16_t ax, bx, cx, dx;
    uint32_t ret;

    asm("int $0x15\n"
        : "=a"(ax), "=b"(bx), "=c"(cx), "=d"(dx)
        : "a"(0xe801), "b"(0), "c"(0), "d"(0));

    /* Not SeaBIOS, but in theory a BIOS could return CX=DX=0 in which
     * case we need to use the result from AX & BX instead.
     */
    if (cx == 0 && dx == 0) {
        cx = ax;
        dx = bx;
    }

    if (dx) {
        /* DX = extended memory above 16M, in 64K units.
         * Convert it to bytes and return.
         */
        ret = ((uint32_t)dx + 256 /* 16M in 64K units */) << 16;
    } else {
        /* This is a fallback path for machines with <= 16MB of RAM,
         * which probably would never be the case, but deal with it
         * anyway.
         *
         * CX = extended memory between 1M and 16M, in kilobytes
         * Convert it to bytes and return.
         */
        ret = ((uint32_t)cx + 1024 /* 1M in K */) << 10;
    }

    return ret;
}

/* Force the asm name without leading underscore, even on Win32. */
extern void load_kernel(void) asm("load_kernel");

void load_kernel(void)
{
    void *setup_addr;
    void *initrd_addr;
    void *kernel_addr;
    void *cmdline_addr;
    uint32_t setup_size;
    uint32_t initrd_size;
    uint32_t kernel_size;
    uint32_t cmdline_size;
    uint32_t initrd_end_page, max_allowed_page;
    uint32_t segment_addr, stack_addr;

    bios_cfg_read_entry(&setup_addr, FW_CFG_SETUP_ADDR, 4);
    bios_cfg_read_entry(&setup_size, FW_CFG_SETUP_SIZE, 4);
    bios_cfg_read_entry(setup_addr, FW_CFG_SETUP_DATA, setup_size);

    set_es(setup_addr);

    /* For protocol < 0x203 we don't have initrd_max ... */
    if (readw_es(0x206) < 0x203) {
        /* ... so we assume initrd_max = 0x37ffffff. */
        writel_es(0x22c, 0x37ffffff);
    }

    bios_cfg_read_entry(&initrd_addr, FW_CFG_INITRD_ADDR, 4);
    bios_cfg_read_entry(&initrd_size, FW_CFG_INITRD_SIZE, 4);

    initrd_end_page = ((uint32_t)(initrd_addr + initrd_size) & -4096);
    max_allowed_page = (readl_es(0x22c) & -4096);

    if (initrd_end_page != 0 && max_allowed_page != 0 &&
        initrd_end_page != max_allowed_page) {
        /* Initrd at the end of memory. Compute better initrd address
         * based on e801 data
         */
        initrd_addr = (void *)((get_e801_addr() - initrd_size) & -4096);
        writel_es(0x218, (uint32_t)initrd_addr);

    }

    bios_cfg_read_entry(initrd_addr, FW_CFG_INITRD_DATA, initrd_size);

    bios_cfg_read_entry(&kernel_addr, FW_CFG_KERNEL_ADDR, 4);
    bios_cfg_read_entry(&kernel_size, FW_CFG_KERNEL_SIZE, 4);
    bios_cfg_read_entry(kernel_addr, FW_CFG_KERNEL_DATA, kernel_size);

    bios_cfg_read_entry(&cmdline_addr, FW_CFG_CMDLINE_ADDR, 4);
    bios_cfg_read_entry(&cmdline_size, FW_CFG_CMDLINE_SIZE, 4);
    bios_cfg_read_entry(cmdline_addr, FW_CFG_CMDLINE_DATA, cmdline_size);

    /* Boot linux */
    segment_addr = ((uint32_t)setup_addr >> 4);
    stack_addr = (uint32_t)(cmdline_addr - setup_addr - 16);

    /* As we are changing critical registers, we cannot leave freedom to the
     * compiler.
     */
    asm("movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        "movw %%ax, %%ss\n"
        "movl %%ebx, %%esp\n"
        "addw $0x20, %%ax\n"
        "pushw %%ax\n" /* CS */
        "pushw $0\n" /* IP */
        /* Clear registers and jump to Linux */
        "xor %%ebx, %%ebx\n"
        "xor %%ecx, %%ecx\n"
        "xor %%edx, %%edx\n"
        "xor %%edi, %%edi\n"
        "xor %%ebp, %%ebp\n"
        "lretw\n"
        : : "a"(segment_addr), "b"(stack_addr));
}
