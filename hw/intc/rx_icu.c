/*
 * RX Interrupt Control Unit
 *
 * Warning: Only ICUa is supported.
 *
 * Datasheet: RX62N Group, RX621 Group User's Manual: Hardware
 *            (Rev.1.40 R01UH0033EJ0140)
 *
 * Copyright (c) 2019 Yoshinori Sato
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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
#include "qemu/error-report.h"
#include "hw/irq.h"
#include "hw/registerfields.h"
#include "hw/qdev-properties.h"
#include "hw/intc/rx_icu.h"
#include "migration/vmstate.h"

REG8(IR, 0)
  FIELD(IR, IR,  0, 1)
REG8(DTCER, 0x100)
  FIELD(DTCER, DTCE,  0, 1)
REG8(IER, 0x200)
REG8(SWINTR, 0x2e0)
  FIELD(SWINTR, SWINT, 0, 1)
REG16(FIR, 0x2f0)
  FIELD(FIR, FVCT, 0,  8)
  FIELD(FIR, FIEN, 15, 1)
REG8(IPR, 0x300)
  FIELD(IPR, IPR, 0, 4)
REG8(DMRSR, 0x400)
REG8(IRQCR, 0x500)
  FIELD(IRQCR, IRQMD, 2, 2)
REG8(NMISR, 0x580)
  FIELD(NMISR, NMIST, 0, 1)
  FIELD(NMISR, LVDST, 1, 1)
  FIELD(NMISR, OSTST, 2, 1)
REG8(NMIER, 0x581)
  FIELD(NMIER, NMIEN, 0, 1)
  FIELD(NMIER, LVDEN, 1, 1)
  FIELD(NMIER, OSTEN, 2, 1)
REG8(NMICLR, 0x582)
  FIELD(NMICLR, NMICLR, 0, 1)
  FIELD(NMICLR, OSTCLR, 2, 1)
REG8(NMICR, 0x583)
  FIELD(NMICR, NMIMD, 3, 1)

static void set_irq(RXICUState *icu, int n_IRQ, int req)
{
    if ((icu->fir & R_FIR_FIEN_MASK) &&
        (icu->fir & R_FIR_FVCT_MASK) == n_IRQ) {
        qemu_set_irq(icu->_fir, req);
    } else {
        qemu_set_irq(icu->_irq, req);
    }
}

static uint16_t rxicu_level(RXICUState *icu, unsigned n)
{
    return (icu->ipr[icu->map[n]] << 8) | n;
}

static void rxicu_request(RXICUState *icu, int n_IRQ)
{
    int enable;

    enable = icu->ier[n_IRQ / 8] & (1 << (n_IRQ & 7));
    if (n_IRQ > 0 && enable != 0 && qatomic_read(&icu->req_irq) < 0) {
        qatomic_set(&icu->req_irq, n_IRQ);
        set_irq(icu, n_IRQ, rxicu_level(icu, n_IRQ));
    }
}

static void rxicu_set_irq(void *opaque, int n_IRQ, int level)
{
    RXICUState *icu = opaque;
    struct IRQSource *src;
    int issue;

    if (n_IRQ >= NR_IRQS) {
        error_report("%s: IRQ %d out of range", __func__, n_IRQ);
        return;
    }

    src = &icu->src[n_IRQ];

    level = (level != 0);
    switch (src->sense) {
    case TRG_LEVEL:
        /* level-sensitive irq */
        issue = level;
        src->level = level;
        break;
    case TRG_NEDGE:
        issue = (level == 0 && src->level == 1);
        src->level = level;
        break;
    case TRG_PEDGE:
        issue = (level == 1 && src->level == 0);
        src->level = level;
        break;
    case TRG_BEDGE:
        issue = ((level ^ src->level) & 1);
        src->level = level;
        break;
    default:
        g_assert_not_reached();
    }
    if (issue == 0 && src->sense == TRG_LEVEL) {
        icu->ir[n_IRQ] = 0;
        if (qatomic_read(&icu->req_irq) == n_IRQ) {
            /* clear request */
            set_irq(icu, n_IRQ, 0);
            qatomic_set(&icu->req_irq, -1);
        }
        return;
    }
    if (issue) {
        icu->ir[n_IRQ] = 1;
        rxicu_request(icu, n_IRQ);
    }
}

static void rxicu_ack_irq(void *opaque, int no, int level)
{
    RXICUState *icu = opaque;
    int i;
    int n_IRQ;
    int max_pri;

    n_IRQ = qatomic_read(&icu->req_irq);
    if (n_IRQ < 0) {
        return;
    }
    qatomic_set(&icu->req_irq, -1);
    if (icu->src[n_IRQ].sense != TRG_LEVEL) {
        icu->ir[n_IRQ] = 0;
    }

    max_pri = 0;
    n_IRQ = -1;
    for (i = 0; i < NR_IRQS; i++) {
        if (icu->ir[i]) {
            if (max_pri < icu->ipr[icu->map[i]]) {
                n_IRQ = i;
                max_pri = icu->ipr[icu->map[i]];
            }
        }
    }

    if (n_IRQ >= 0) {
        rxicu_request(icu, n_IRQ);
    }
}

static uint64_t icu_read(void *opaque, hwaddr addr, unsigned size)
{
    RXICUState *icu = opaque;
    int reg = addr & 0xff;

    if ((addr != A_FIR && size != 1) ||
        (addr == A_FIR && size != 2)) {
        qemu_log_mask(LOG_GUEST_ERROR, "rx_icu: Invalid read size 0x%"
                                       HWADDR_PRIX "\n",
                      addr);
        return UINT64_MAX;
    }
    switch (addr) {
    case A_IR ... A_IR + 0xff:
        return icu->ir[reg] & R_IR_IR_MASK;
    case A_DTCER ... A_DTCER + 0xff:
        return icu->dtcer[reg] & R_DTCER_DTCE_MASK;
    case A_IER ... A_IER + 0x1f:
        return icu->ier[reg];
    case A_SWINTR:
        return 0;
    case A_FIR:
        return icu->fir & (R_FIR_FIEN_MASK | R_FIR_FVCT_MASK);
    case A_IPR ... A_IPR + 0x8f:
        return icu->ipr[reg] & R_IPR_IPR_MASK;
    case A_DMRSR:
    case A_DMRSR + 4:
    case A_DMRSR + 8:
    case A_DMRSR + 12:
        return icu->dmasr[reg >> 2];
    case A_IRQCR ... A_IRQCR + 0x1f:
        return icu->src[64 + reg].sense << R_IRQCR_IRQMD_SHIFT;
    case A_NMISR:
    case A_NMICLR:
        return 0;
    case A_NMIER:
        return icu->nmier;
    case A_NMICR:
        return icu->nmicr;
    default:
        qemu_log_mask(LOG_UNIMP, "rx_icu: Register 0x%" HWADDR_PRIX " "
                                 "not implemented.\n",
                      addr);
        break;
    }
    return UINT64_MAX;
}

static void icu_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    RXICUState *icu = opaque;
    int reg = addr & 0xff;

    if ((addr != A_FIR && size != 1) ||
        (addr == A_FIR && size != 2)) {
        qemu_log_mask(LOG_GUEST_ERROR, "rx_icu: Invalid write size at "
                                       "0x%" HWADDR_PRIX "\n",
                      addr);
        return;
    }
    switch (addr) {
    case A_IR ... A_IR + 0xff:
        if (icu->src[reg].sense != TRG_LEVEL && val == 0) {
            icu->ir[reg] = 0;
        }
        break;
    case A_DTCER ... A_DTCER + 0xff:
        icu->dtcer[reg] = val & R_DTCER_DTCE_MASK;
        qemu_log_mask(LOG_UNIMP, "rx_icu: DTC not implemented\n");
        break;
    case A_IER ... A_IER + 0x1f:
        icu->ier[reg] = val;
        break;
    case A_SWINTR:
        if (val & R_SWINTR_SWINT_MASK) {
            qemu_irq_pulse(icu->_swi);
        }
        break;
    case A_FIR:
        icu->fir = val & (R_FIR_FIEN_MASK | R_FIR_FVCT_MASK);
        break;
    case A_IPR ... A_IPR + 0x8f:
        icu->ipr[reg] = val & R_IPR_IPR_MASK;
        break;
    case A_DMRSR:
    case A_DMRSR + 4:
    case A_DMRSR + 8:
    case A_DMRSR + 12:
        icu->dmasr[reg >> 2] = val;
        qemu_log_mask(LOG_UNIMP, "rx_icu: DMAC not implemented\n");
        break;
    case A_IRQCR ... A_IRQCR + 0x1f:
        icu->src[64 + reg].sense = val >> R_IRQCR_IRQMD_SHIFT;
        break;
    case A_NMICLR:
        break;
    case A_NMIER:
        icu->nmier |= val & (R_NMIER_NMIEN_MASK |
                             R_NMIER_LVDEN_MASK |
                             R_NMIER_OSTEN_MASK);
        break;
    case A_NMICR:
        if ((icu->nmier & R_NMIER_NMIEN_MASK) == 0) {
            icu->nmicr = val & R_NMICR_NMIMD_MASK;
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "rx_icu: Register 0x%" HWADDR_PRIX " "
                                 "not implemented\n",
                      addr);
        break;
    }
}

static const MemoryRegionOps icu_ops = {
    .write = icu_write,
    .read  = icu_read,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 2,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 2,
    },
};

static void rxicu_realize(DeviceState *dev, Error **errp)
{
    RXICUState *icu = RX_ICU(dev);
    int i;

    if (icu->init_sense == NULL) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "rx_icu: trigger-level property must be set.");
        return;
    }

    for (i = 0; i < NR_IRQS; i++) {
        icu->src[i].sense = TRG_PEDGE;
    }
    for (i = 0; i < icu->nr_sense; i++) {
        uint8_t irqno = icu->init_sense[i];
        icu->src[irqno].sense = TRG_LEVEL;
    }
    icu->req_irq = -1;
}

static void rxicu_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    RXICUState *icu = RX_ICU(obj);

    memory_region_init_io(&icu->memory, OBJECT(icu), &icu_ops,
                          icu, "rx-icu", 0x600);
    sysbus_init_mmio(d, &icu->memory);

    qdev_init_gpio_in(DEVICE(d), rxicu_set_irq, NR_IRQS);
    qdev_init_gpio_in_named(DEVICE(d), rxicu_ack_irq, "ack", 1);
    sysbus_init_irq(d, &icu->_irq);
    sysbus_init_irq(d, &icu->_fir);
    sysbus_init_irq(d, &icu->_swi);
}

static void rxicu_fini(Object *obj)
{
    RXICUState *icu = RX_ICU(obj);
    g_free(icu->map);
    g_free(icu->init_sense);
}

static const VMStateDescription vmstate_rxicu = {
    .name = "rx-icu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(ir, RXICUState, NR_IRQS),
        VMSTATE_UINT8_ARRAY(dtcer, RXICUState, NR_IRQS),
        VMSTATE_UINT8_ARRAY(ier, RXICUState, NR_IRQS / 8),
        VMSTATE_UINT8_ARRAY(ipr, RXICUState, 142),
        VMSTATE_UINT8_ARRAY(dmasr, RXICUState, 4),
        VMSTATE_UINT16(fir, RXICUState),
        VMSTATE_UINT8(nmisr, RXICUState),
        VMSTATE_UINT8(nmier, RXICUState),
        VMSTATE_UINT8(nmiclr, RXICUState),
        VMSTATE_UINT8(nmicr, RXICUState),
        VMSTATE_INT16(req_irq, RXICUState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property rxicu_properties[] = {
    DEFINE_PROP_ARRAY("ipr-map", RXICUState, nr_irqs, map,
                      qdev_prop_uint8, uint8_t),
    DEFINE_PROP_ARRAY("trigger-level", RXICUState, nr_sense, init_sense,
                      qdev_prop_uint8, uint8_t),
};

static void rxicu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = rxicu_realize;
    dc->vmsd = &vmstate_rxicu;
    device_class_set_props(dc, rxicu_properties);
}

static const TypeInfo rxicu_info = {
    .name = TYPE_RX_ICU,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RXICUState),
    .instance_init = rxicu_init,
    .instance_finalize = rxicu_fini,
    .class_init = rxicu_class_init,
};

static void rxicu_register_types(void)
{
    type_register_static(&rxicu_info);
}

type_init(rxicu_register_types)
