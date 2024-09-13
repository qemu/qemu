/*
 * i.MX31 Vectored Interrupt Controller
 *
 * Note this is NOT the PL192 provided by ARM, but
 * a custom implementation by Freescale.
 *
 * Copyright (c) 2008 OKL
 * Copyright (c) 2011 NICTA Pty Ltd
 * Originally written by Hans Jiang
 * Updated by Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 *
 * TODO: implement vectors.
 */

#include "qemu/osdep.h"
#include "hw/intc/imx_avic.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"

#ifndef DEBUG_IMX_AVIC
#define DEBUG_IMX_AVIC 0
#endif

#define DPRINTF(fmt, args...) \
    do { \
        if (DEBUG_IMX_AVIC) { \
            fprintf(stderr, "[%s]%s: " fmt , TYPE_IMX_AVIC, \
                                             __func__, ##args); \
        } \
    } while (0)

static const VMStateDescription vmstate_imx_avic = {
    .name = TYPE_IMX_AVIC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(pending, IMXAVICState),
        VMSTATE_UINT64(enabled, IMXAVICState),
        VMSTATE_UINT64(is_fiq, IMXAVICState),
        VMSTATE_UINT32(intcntl, IMXAVICState),
        VMSTATE_UINT32(intmask, IMXAVICState),
        VMSTATE_UINT32_ARRAY(prio, IMXAVICState, PRIO_WORDS),
        VMSTATE_END_OF_LIST()
    },
};

static inline int imx_avic_prio(IMXAVICState *s, int irq)
{
    uint32_t word = irq / PRIO_PER_WORD;
    uint32_t part = 4 * (irq % PRIO_PER_WORD);
    return 0xf & (s->prio[word] >> part);
}

/* Update interrupts.  */
static void imx_avic_update(IMXAVICState *s)
{
    int i;
    uint64_t new = s->pending & s->enabled;
    uint64_t flags;

    flags = new & s->is_fiq;
    qemu_set_irq(s->fiq, !!flags);

    flags = new & ~s->is_fiq;
    if (!flags || (s->intmask == 0x1f)) {
        qemu_set_irq(s->irq, !!flags);
        return;
    }

    /*
     * Take interrupt if there's a pending interrupt with
     * priority higher than the value of intmask
     */
    for (i = 0; i < IMX_AVIC_NUM_IRQS; i++) {
        if (flags & (1UL << i)) {
            if (imx_avic_prio(s, i) > s->intmask) {
                qemu_set_irq(s->irq, 1);
                return;
            }
        }
    }
    qemu_set_irq(s->irq, 0);
}

static void imx_avic_set_irq(void *opaque, int irq, int level)
{
    IMXAVICState *s = (IMXAVICState *)opaque;

    if (level) {
        DPRINTF("Raising IRQ %d, prio %d\n",
                irq, imx_avic_prio(s, irq));
        s->pending |= (1ULL << irq);
    } else {
        DPRINTF("Clearing IRQ %d, prio %d\n",
                irq, imx_avic_prio(s, irq));
        s->pending &= ~(1ULL << irq);
    }

    imx_avic_update(s);
}


static uint64_t imx_avic_read(void *opaque,
                             hwaddr offset, unsigned size)
{
    IMXAVICState *s = (IMXAVICState *)opaque;

    DPRINTF("read(offset = 0x%" HWADDR_PRIx ")\n", offset);

    switch (offset >> 2) {
    case 0: /* INTCNTL */
        return s->intcntl;

    case 1: /* Normal Interrupt Mask Register, NIMASK */
        return s->intmask;

    case 2: /* Interrupt Enable Number Register, INTENNUM */
    case 3: /* Interrupt Disable Number Register, INTDISNUM */
        return 0;

    case 4: /* Interrupt Enabled Number Register High */
        return s->enabled >> 32;

    case 5: /* Interrupt Enabled Number Register Low */
        return s->enabled & 0xffffffffULL;

    case 6: /* Interrupt Type Register High */
        return s->is_fiq >> 32;

    case 7: /* Interrupt Type Register Low */
        return s->is_fiq & 0xffffffffULL;

    case 8: /* Normal Interrupt Priority Register 7 */
    case 9: /* Normal Interrupt Priority Register 6 */
    case 10:/* Normal Interrupt Priority Register 5 */
    case 11:/* Normal Interrupt Priority Register 4 */
    case 12:/* Normal Interrupt Priority Register 3 */
    case 13:/* Normal Interrupt Priority Register 2 */
    case 14:/* Normal Interrupt Priority Register 1 */
    case 15:/* Normal Interrupt Priority Register 0 */
        return s->prio[15-(offset>>2)];

    case 16: /* Normal interrupt vector and status register */
    {
        /*
         * This returns the highest priority
         * outstanding interrupt.  Where there is more than
         * one pending IRQ with the same priority,
         * take the highest numbered one.
         */
        uint64_t flags = s->pending & s->enabled & ~s->is_fiq;
        int i;
        int prio = -1;
        int irq = -1;
        for (i = 63; i >= 0; --i) {
            if (flags & (1ULL<<i)) {
                int irq_prio = imx_avic_prio(s, i);
                if (irq_prio > prio) {
                    irq = i;
                    prio = irq_prio;
                }
            }
        }
        if (irq >= 0) {
            imx_avic_set_irq(s, irq, 0);
            return irq << 16 | prio;
        }
        return 0xffffffffULL;
    }
    case 17:/* Fast Interrupt vector and status register */
    {
        uint64_t flags = s->pending & s->enabled & s->is_fiq;
        int i = ctz64(flags);
        if (i < 64) {
            imx_avic_set_irq(opaque, i, 0);
            return i;
        }
        return 0xffffffffULL;
    }
    case 18:/* Interrupt source register high */
        return s->pending >> 32;

    case 19:/* Interrupt source register low */
        return s->pending & 0xffffffffULL;

    case 20:/* Interrupt Force Register high */
    case 21:/* Interrupt Force Register low */
        return 0;

    case 22:/* Normal Interrupt Pending Register High */
        return (s->pending & s->enabled & ~s->is_fiq) >> 32;

    case 23:/* Normal Interrupt Pending Register Low */
        return (s->pending & s->enabled & ~s->is_fiq) & 0xffffffffULL;

    case 24: /* Fast Interrupt Pending Register High  */
        return (s->pending & s->enabled & s->is_fiq) >> 32;

    case 25: /* Fast Interrupt Pending Register Low  */
        return (s->pending & s->enabled & s->is_fiq) & 0xffffffffULL;

    case 0x40:            /* AVIC vector 0, use for WFI WAR */
        return 0x4;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX_AVIC, __func__, offset);
        return 0;
    }
}

static void imx_avic_write(void *opaque, hwaddr offset,
                          uint64_t val, unsigned size)
{
    IMXAVICState *s = (IMXAVICState *)opaque;

    /* Vector Registers not yet supported */
    if (offset >= 0x100 && offset <= 0x2fc) {
        qemu_log_mask(LOG_UNIMP, "[%s]%s: vector %d ignored\n",
                      TYPE_IMX_AVIC, __func__, (int)((offset - 0x100) >> 2));
        return;
    }

    DPRINTF("(0x%" HWADDR_PRIx ") = 0x%x\n", offset, (unsigned int)val);

    switch (offset >> 2) {
    case 0: /* Interrupt Control Register, INTCNTL */
        s->intcntl = val & (ABFEN | NIDIS | FIDIS | NIAD | FIAD | NM);
        if (s->intcntl & ABFEN) {
            s->intcntl &= ~(val & ABFLAG);
        }
        break;

    case 1: /* Normal Interrupt Mask Register, NIMASK */
        s->intmask = val & 0x1f;
        break;

    case 2: /* Interrupt Enable Number Register, INTENNUM */
        DPRINTF("enable(%d)\n", (int)val);
        val &= 0x3f;
        s->enabled |= (1ULL << val);
        break;

    case 3: /* Interrupt Disable Number Register, INTDISNUM */
        DPRINTF("disable(%d)\n", (int)val);
        val &= 0x3f;
        s->enabled &= ~(1ULL << val);
        break;

    case 4: /* Interrupt Enable Number Register High */
        s->enabled = (s->enabled & 0xffffffffULL) | (val << 32);
        break;

    case 5: /* Interrupt Enable Number Register Low */
        s->enabled = (s->enabled & 0xffffffff00000000ULL) | val;
        break;

    case 6: /* Interrupt Type Register High */
        s->is_fiq = (s->is_fiq & 0xffffffffULL) | (val << 32);
        break;

    case 7: /* Interrupt Type Register Low */
        s->is_fiq = (s->is_fiq & 0xffffffff00000000ULL) | val;
        break;

    case 8: /* Normal Interrupt Priority Register 7 */
    case 9: /* Normal Interrupt Priority Register 6 */
    case 10:/* Normal Interrupt Priority Register 5 */
    case 11:/* Normal Interrupt Priority Register 4 */
    case 12:/* Normal Interrupt Priority Register 3 */
    case 13:/* Normal Interrupt Priority Register 2 */
    case 14:/* Normal Interrupt Priority Register 1 */
    case 15:/* Normal Interrupt Priority Register 0 */
        s->prio[15-(offset>>2)] = val;
        break;

        /* Read-only registers, writes ignored */
    case 16:/* Normal Interrupt Vector and Status register */
    case 17:/* Fast Interrupt vector and status register */
    case 18:/* Interrupt source register high */
    case 19:/* Interrupt source register low */
        return;

    case 20:/* Interrupt Force Register high */
        s->pending = (s->pending & 0xffffffffULL) | (val << 32);
        break;

    case 21:/* Interrupt Force Register low */
        s->pending = (s->pending & 0xffffffff00000000ULL) | val;
        break;

    case 22:/* Normal Interrupt Pending Register High */
    case 23:/* Normal Interrupt Pending Register Low */
    case 24: /* Fast Interrupt Pending Register High  */
    case 25: /* Fast Interrupt Pending Register Low  */
        return;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_IMX_AVIC, __func__, offset);
    }
    imx_avic_update(s);
}

static const MemoryRegionOps imx_avic_ops = {
    .read = imx_avic_read,
    .write = imx_avic_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void imx_avic_reset(DeviceState *dev)
{
    IMXAVICState *s = IMX_AVIC(dev);

    s->pending = 0;
    s->enabled = 0;
    s->is_fiq = 0;
    s->intmask = 0x1f;
    s->intcntl = 0;
    memset(s->prio, 0, sizeof s->prio);
}

static void imx_avic_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    IMXAVICState *s = IMX_AVIC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &imx_avic_ops, s,
                          TYPE_IMX_AVIC, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);

    qdev_init_gpio_in(dev, imx_avic_set_irq, IMX_AVIC_NUM_IRQS);
    sysbus_init_irq(sbd, &s->irq);
    sysbus_init_irq(sbd, &s->fiq);
}


static void imx_avic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_imx_avic;
    device_class_set_legacy_reset(dc, imx_avic_reset);
    dc->desc = "i.MX Advanced Vector Interrupt Controller";
}

static const TypeInfo imx_avic_info = {
    .name = TYPE_IMX_AVIC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMXAVICState),
    .instance_init = imx_avic_init,
    .class_init = imx_avic_class_init,
};

static void imx_avic_register_types(void)
{
    type_register_static(&imx_avic_info);
}

type_init(imx_avic_register_types)
