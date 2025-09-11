/*
 * RISC-V ACLINT (Advanced Core Local Interruptor) interface
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017 SiFive, Inc.
 * Copyright (c) 2021 Western Digital Corporation or its affiliates.
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

#ifndef HW_RISCV_ACLINT_H
#define HW_RISCV_ACLINT_H

#include "hw/sysbus.h"

#define TYPE_RISCV_ACLINT_MTIMER "riscv.aclint.mtimer"

#define RISCV_ACLINT_MTIMER(obj) \
    OBJECT_CHECK(RISCVAclintMTimerState, (obj), TYPE_RISCV_ACLINT_MTIMER)

typedef struct RISCVAclintMTimerState {
    /*< private >*/
    SysBusDevice parent_obj;
    uint64_t time_delta;
    uint64_t *timecmp;
    QEMUTimer **timers;

    /*< public >*/
    MemoryRegion mmio;
    uint32_t hartid_base;
    uint32_t num_harts;
    uint32_t timecmp_base;
    uint32_t time_base;
    uint32_t aperture_size;
    uint32_t timebase_freq;
    qemu_irq *timer_irqs;
} RISCVAclintMTimerState;

DeviceState *riscv_aclint_mtimer_create(hwaddr addr, hwaddr size,
    uint32_t hartid_base, uint32_t num_harts,
    uint32_t timecmp_base, uint32_t time_base, uint32_t timebase_freq,
    bool provide_rdtime);

#define TYPE_RISCV_ACLINT_SWI "riscv.aclint.swi"

#define RISCV_ACLINT_SWI(obj) \
    OBJECT_CHECK(RISCVAclintSwiState, (obj), TYPE_RISCV_ACLINT_SWI)

typedef struct RISCVAclintSwiState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;
    uint32_t hartid_base;
    uint32_t num_harts;
    uint32_t sswi;
    qemu_irq *soft_irqs;
} RISCVAclintSwiState;

DeviceState *riscv_aclint_swi_create(hwaddr addr, uint32_t hartid_base,
    uint32_t num_harts, bool sswi);

enum {
    RISCV_ACLINT_DEFAULT_MTIMECMP      = 0x0,
    RISCV_ACLINT_DEFAULT_MTIME         = 0x7ff8,
    RISCV_ACLINT_DEFAULT_MTIMER_SIZE   = 0x8000,
    RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ = 10000000,
    RISCV_ACLINT_MAX_HARTS             = 4095,
    RISCV_ACLINT_SWI_SIZE              = 0x4000
};

#define VMSTATE_TIMER_PTR_VARRAY(_f, _s, _f_n)                        \
VMSTATE_VARRAY_OF_POINTER_UINT32(_f, _s, _f_n, 0, vmstate_info_timer, \
                                                        QEMUTimer *)

#endif
