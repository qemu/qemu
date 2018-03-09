/*
 * SiFive VirtIO Board
 *
 * Copyright (c) 2017 SiFive, Inc.
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

#ifndef HW_VIRT_H
#define HW_VIRT_H

#define TYPE_RISCV_VIRT_BOARD "riscv.virt"
#define VIRT(obj) \
    OBJECT_CHECK(RISCVVirtState, (obj), TYPE_RISCV_VIRT_BOARD)

enum { ROM_BASE = 0x1000 };

typedef struct {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    RISCVHartArrayState soc;
    DeviceState *plic;
    void *fdt;
    int fdt_size;
} RISCVVirtState;

enum {
    VIRT_DEBUG,
    VIRT_MROM,
    VIRT_TEST,
    VIRT_CLINT,
    VIRT_PLIC,
    VIRT_UART0,
    VIRT_VIRTIO,
    VIRT_DRAM
};


enum {
    UART0_IRQ = 10,
    VIRTIO_IRQ = 1, /* 1 to 8 */
    VIRTIO_COUNT = 8,
    VIRTIO_NDEV = 10
};

#define VIRT_PLIC_HART_CONFIG "MS"
#define VIRT_PLIC_NUM_SOURCES 127
#define VIRT_PLIC_NUM_PRIORITIES 7
#define VIRT_PLIC_PRIORITY_BASE 0x0
#define VIRT_PLIC_PENDING_BASE 0x1000
#define VIRT_PLIC_ENABLE_BASE 0x2000
#define VIRT_PLIC_ENABLE_STRIDE 0x80
#define VIRT_PLIC_CONTEXT_BASE 0x200000
#define VIRT_PLIC_CONTEXT_STRIDE 0x1000

#if defined(TARGET_RISCV32)
#define VIRT_CPU TYPE_RISCV_CPU_RV32GCSU_V1_10_0
#elif defined(TARGET_RISCV64)
#define VIRT_CPU TYPE_RISCV_CPU_RV64GCSU_V1_10_0
#endif

#endif
