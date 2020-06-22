/*
 * QEMU RISC-V lowRISC Ibex PLIC
 *
 * Copyright (c) 2020 Western Digital
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

#ifndef HW_IBEX_PLIC_H
#define HW_IBEX_PLIC_H

#include "hw/sysbus.h"

#define TYPE_IBEX_PLIC "ibex-plic"
#define IBEX_PLIC(obj) \
    OBJECT_CHECK(IbexPlicState, (obj), TYPE_IBEX_PLIC)

typedef struct IbexPlicState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;

    uint32_t *pending;
    uint32_t *source;
    uint32_t *priority;
    uint32_t *enable;
    uint32_t threshold;
    uint32_t claim;

    /* config */
    uint32_t num_cpus;
    uint32_t num_sources;

    uint32_t pending_base;
    uint32_t pending_num;

    uint32_t source_base;
    uint32_t source_num;

    uint32_t priority_base;
    uint32_t priority_num;

    uint32_t enable_base;
    uint32_t enable_num;

    uint32_t threshold_base;

    uint32_t claim_base;
} IbexPlicState;

#endif /* HW_IBEX_PLIC_H */
