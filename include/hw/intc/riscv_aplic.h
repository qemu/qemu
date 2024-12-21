/*
 * RISC-V APLIC (Advanced Platform Level Interrupt Controller) interface
 *
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

#ifndef HW_RISCV_APLIC_H
#define HW_RISCV_APLIC_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_RISCV_APLIC "riscv.aplic"

typedef struct RISCVAPLICState RISCVAPLICState;
DECLARE_INSTANCE_CHECKER(RISCVAPLICState, RISCV_APLIC, TYPE_RISCV_APLIC)

#define APLIC_MIN_SIZE            0x4000
#define APLIC_SIZE_ALIGN(__x)     (((__x) + (APLIC_MIN_SIZE - 1)) & \
                                   ~(APLIC_MIN_SIZE - 1))
#define APLIC_SIZE(__num_harts)   (APLIC_MIN_SIZE + \
                                   APLIC_SIZE_ALIGN(32 * (__num_harts)))

struct RISCVAPLICState {
    /*< private >*/
    SysBusDevice parent_obj;
    qemu_irq *external_irqs;

    /*< public >*/
    MemoryRegion mmio;
    uint32_t bitfield_words;
    uint32_t domaincfg;
    uint32_t mmsicfgaddr;
    uint32_t mmsicfgaddrH;
    uint32_t smsicfgaddr;
    uint32_t smsicfgaddrH;
    uint32_t genmsi;
    uint32_t *sourcecfg;
    uint32_t *state;
    uint32_t *target;
    uint32_t *idelivery;
    uint32_t *iforce;
    uint32_t *ithreshold;

    /* topology */
#define QEMU_APLIC_MAX_CHILDREN        16
    struct RISCVAPLICState *parent;
    struct RISCVAPLICState *children[QEMU_APLIC_MAX_CHILDREN];
    uint16_t num_children;

    /* config */
    uint32_t aperture_size;
    uint32_t hartid_base;
    uint32_t num_harts;
    uint32_t iprio_mask;
    uint32_t num_irqs;
    bool msimode;
    bool mmode;

    /* To support KVM aia=aplic-imsic with irqchip split mode */
    bool kvm_splitmode;
    uint32_t kvm_msicfgaddr;
    uint32_t kvm_msicfgaddrH;
};

void riscv_aplic_add_child(DeviceState *parent, DeviceState *child);
bool riscv_is_kvm_aia_aplic_imsic(bool msimode);
bool riscv_use_emulated_aplic(bool msimode);
void riscv_aplic_set_kvm_msicfgaddr(RISCVAPLICState *aplic, hwaddr addr);

DeviceState *riscv_aplic_create(hwaddr addr, hwaddr size,
    uint32_t hartid_base, uint32_t num_harts, uint32_t num_sources,
    uint32_t iprio_bits, bool msimode, bool mmode, DeviceState *parent);

#endif
