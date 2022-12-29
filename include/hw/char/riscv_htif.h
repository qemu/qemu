/*
 * QEMU RISCV Host Target Interface (HTIF) Emulation
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_RISCV_HTIF_H
#define HW_RISCV_HTIF_H

#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "exec/memory.h"
#include "target/riscv/cpu.h"

#define TYPE_HTIF_UART "riscv.htif.uart"

typedef struct HTIFState {
    int allow_tohost;
    int fromhost_inprogress;

    hwaddr tohost_offset;
    hwaddr fromhost_offset;
    MemoryRegion mmio;
    MemoryRegion *address_space;
    MemoryRegion *main_mem;
    void *main_mem_ram_ptr;

    CPURISCVState *env;
    CharBackend chr;
    uint64_t pending_read;
} HTIFState;

extern const VMStateDescription vmstate_htif;
extern const MemoryRegionOps htif_io_ops;

/* HTIF symbol callback */
void htif_symbol_callback(const char *st_name, int st_info, uint64_t st_value,
    uint64_t st_size);

/* Check if HTIF uses ELF symbols */
bool htif_uses_elf_symbols(void);

/* legacy pre qom */
HTIFState *htif_mm_init(MemoryRegion *address_space, MemoryRegion *main_mem,
    CPURISCVState *env, Chardev *chr, uint64_t nonelf_base);

#endif
