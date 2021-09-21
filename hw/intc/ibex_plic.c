/*
 * QEMU RISC-V lowRISC Ibex PLIC
 *
 * Copyright (c) 2020 Western Digital
 *
 * Documentation avaliable: https://docs.opentitan.org/hw/ip/rv_plic/doc/
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

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/qdev-properties.h"
#include "hw/core/cpu.h"
#include "hw/boards.h"
#include "hw/pci/msi.h"
#include "target/riscv/cpu_bits.h"
#include "target/riscv/cpu.h"
#include "hw/intc/ibex_plic.h"
#include "hw/irq.h"

static bool addr_between(uint32_t addr, uint32_t base, uint32_t num)
{
    uint32_t end = base + (num * 0x04);

    if (addr >= base && addr < end) {
        return true;
    }

    return false;
}

static void ibex_plic_irqs_set_pending(IbexPlicState *s, int irq, bool level)
{
    int pending_num = irq / 32;

    if (!level) {
        /*
         * If the level is low make sure we clear the hidden_pending.
         */
        s->hidden_pending[pending_num] &= ~(1 << (irq % 32));
    }

    if (s->claimed[pending_num] & 1 << (irq % 32)) {
        /*
         * The interrupt has been claimed, but not completed.
         * The pending bit can't be set.
         * Save the pending level for after the interrupt is completed.
         */
        s->hidden_pending[pending_num] |= level << (irq % 32);
    } else {
        s->pending[pending_num] |= level << (irq % 32);
    }
}

static bool ibex_plic_irqs_pending(IbexPlicState *s, uint32_t context)
{
    int i;
    uint32_t max_irq = 0;
    uint32_t max_prio = s->threshold;

    for (i = 0; i < s->pending_num; i++) {
        uint32_t irq_num = ctz64(s->pending[i]) + (i * 32);

        if (!(s->pending[i] & s->enable[i])) {
            /* No pending and enabled IRQ */
            continue;
        }

        if (s->priority[irq_num] > max_prio) {
            max_irq = irq_num;
            max_prio = s->priority[irq_num];
        }
    }

    if (max_irq) {
        s->claim = max_irq;
        return true;
    }

    return false;
}

static void ibex_plic_update(IbexPlicState *s)
{
    int i;

    for (i = 0; i < s->num_cpus; i++) {
        qemu_set_irq(s->external_irqs[i], ibex_plic_irqs_pending(s, 0));
    }
}

static void ibex_plic_reset(DeviceState *dev)
{
    IbexPlicState *s = IBEX_PLIC(dev);

    s->threshold = 0x00000000;
    s->claim = 0x00000000;
}

static uint64_t ibex_plic_read(void *opaque, hwaddr addr,
                               unsigned int size)
{
    IbexPlicState *s = opaque;
    int offset;
    uint32_t ret = 0;

    if (addr_between(addr, s->pending_base, s->pending_num)) {
        offset = (addr - s->pending_base) / 4;
        ret = s->pending[offset];
    } else if (addr_between(addr, s->source_base, s->source_num)) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: Interrupt source mode not supported\n", __func__);
    } else if (addr_between(addr, s->priority_base, s->priority_num)) {
        offset = (addr - s->priority_base) / 4;
        ret = s->priority[offset];
    } else if (addr_between(addr, s->enable_base, s->enable_num)) {
        offset = (addr - s->enable_base) / 4;
        ret = s->enable[offset];
    } else if (addr_between(addr, s->threshold_base, 1)) {
        ret = s->threshold;
    } else if (addr_between(addr, s->claim_base, 1)) {
        int pending_num = s->claim / 32;
        s->pending[pending_num] &= ~(1 << (s->claim % 32));

        /* Set the interrupt as claimed, but not completed */
        s->claimed[pending_num] |= 1 << (s->claim % 32);

        /* Return the current claimed interrupt */
        ret = s->claim;

        /* Clear the claimed interrupt */
        s->claim = 0x00000000;

        /* Update the interrupt status after the claim */
        ibex_plic_update(s);
    }

    return ret;
}

static void ibex_plic_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned int size)
{
    IbexPlicState *s = opaque;

    if (addr_between(addr, s->pending_base, s->pending_num)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Pending registers are read only\n", __func__);
    } else if (addr_between(addr, s->source_base, s->source_num)) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: Interrupt source mode not supported\n", __func__);
    } else if (addr_between(addr, s->priority_base, s->priority_num)) {
        uint32_t irq = ((addr - s->priority_base) >> 2) + 1;
        s->priority[irq] = value & 7;
        ibex_plic_update(s);
    } else if (addr_between(addr, s->enable_base, s->enable_num)) {
        uint32_t enable_reg = (addr - s->enable_base) / 4;

        s->enable[enable_reg] = value;
    } else if (addr_between(addr, s->threshold_base, 1)) {
        s->threshold = value & 3;
    } else if (addr_between(addr, s->claim_base, 1)) {
        if (s->claim == value) {
            /* Interrupt was completed */
            s->claim = 0;
        }
        if (s->claimed[value / 32] & 1 << (value % 32)) {
            int pending_num = value / 32;

            /* This value was already claimed, clear it. */
            s->claimed[pending_num] &= ~(1 << (value % 32));

            if (s->hidden_pending[pending_num] & (1 << (value % 32))) {
                /*
                 * If the bit in hidden_pending is set then that means we
                 * received an interrupt between claiming and completing
                 * the interrupt that hasn't since been de-asserted.
                 * On hardware this would trigger an interrupt, so let's
                 * trigger one here as well.
                 */
                s->pending[pending_num] |= 1 << (value % 32);
            }
        }
    }

    ibex_plic_update(s);
}

static const MemoryRegionOps ibex_plic_ops = {
    .read = ibex_plic_read,
    .write = ibex_plic_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static void ibex_plic_irq_request(void *opaque, int irq, int level)
{
    IbexPlicState *s = opaque;

    ibex_plic_irqs_set_pending(s, irq, level > 0);
    ibex_plic_update(s);
}

static Property ibex_plic_properties[] = {
    DEFINE_PROP_UINT32("num-cpus", IbexPlicState, num_cpus, 1),
    DEFINE_PROP_UINT32("num-sources", IbexPlicState, num_sources, 176),

    DEFINE_PROP_UINT32("pending-base", IbexPlicState, pending_base, 0),
    DEFINE_PROP_UINT32("pending-num", IbexPlicState, pending_num, 6),

    DEFINE_PROP_UINT32("source-base", IbexPlicState, source_base, 0x18),
    DEFINE_PROP_UINT32("source-num", IbexPlicState, source_num, 6),

    DEFINE_PROP_UINT32("priority-base", IbexPlicState, priority_base, 0x30),
    DEFINE_PROP_UINT32("priority-num", IbexPlicState, priority_num, 177),

    DEFINE_PROP_UINT32("enable-base", IbexPlicState, enable_base, 0x300),
    DEFINE_PROP_UINT32("enable-num", IbexPlicState, enable_num, 6),

    DEFINE_PROP_UINT32("threshold-base", IbexPlicState, threshold_base, 0x318),

    DEFINE_PROP_UINT32("claim-base", IbexPlicState, claim_base, 0x31c),
    DEFINE_PROP_END_OF_LIST(),
};

static void ibex_plic_init(Object *obj)
{
    IbexPlicState *s = IBEX_PLIC(obj);

    memory_region_init_io(&s->mmio, obj, &ibex_plic_ops, s,
                          TYPE_IBEX_PLIC, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void ibex_plic_realize(DeviceState *dev, Error **errp)
{
    IbexPlicState *s = IBEX_PLIC(dev);
    int i;

    s->pending = g_new0(uint32_t, s->pending_num);
    s->hidden_pending = g_new0(uint32_t, s->pending_num);
    s->claimed = g_new0(uint32_t, s->pending_num);
    s->source = g_new0(uint32_t, s->source_num);
    s->priority = g_new0(uint32_t, s->priority_num);
    s->enable = g_new0(uint32_t, s->enable_num);

    qdev_init_gpio_in(dev, ibex_plic_irq_request, s->num_sources);

    s->external_irqs = g_malloc(sizeof(qemu_irq) * s->num_cpus);
    qdev_init_gpio_out(dev, s->external_irqs, s->num_cpus);

    /*
     * We can't allow the supervisor to control SEIP as this would allow the
     * supervisor to clear a pending external interrupt which will result in
     * a lost interrupt in the case a PLIC is attached. The SEIP bit must be
     * hardware controlled when a PLIC is attached.
     */
    MachineState *ms = MACHINE(qdev_get_machine());
    unsigned int smp_cpus = ms->smp.cpus;
    for (i = 0; i < smp_cpus; i++) {
        RISCVCPU *cpu = RISCV_CPU(qemu_get_cpu(i));
        if (riscv_cpu_claim_interrupts(cpu, MIP_SEIP) < 0) {
            error_report("SEIP already claimed");
            exit(1);
        }
    }

    msi_nonbroken = true;
}

static void ibex_plic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = ibex_plic_reset;
    device_class_set_props(dc, ibex_plic_properties);
    dc->realize = ibex_plic_realize;
}

static const TypeInfo ibex_plic_info = {
    .name          = TYPE_IBEX_PLIC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IbexPlicState),
    .instance_init = ibex_plic_init,
    .class_init    = ibex_plic_class_init,
};

static void ibex_plic_register_types(void)
{
    type_register_static(&ibex_plic_info);
}

type_init(ibex_plic_register_types)
