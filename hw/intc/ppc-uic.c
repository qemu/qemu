/*
 * "Universal" Interrupt Controller for PowerPPC 4xx embedded processors
 *
 * Copyright (c) 2007 Jocelyn Mayer
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

#include "qemu/osdep.h"
#include "hw/intc/ppc-uic.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

enum {
    DCR_UICSR  = 0x000,
    DCR_UICSRS = 0x001,
    DCR_UICER  = 0x002,
    DCR_UICCR  = 0x003,
    DCR_UICPR  = 0x004,
    DCR_UICTR  = 0x005,
    DCR_UICMSR = 0x006,
    DCR_UICVR  = 0x007,
    DCR_UICVCR = 0x008,
    DCR_UICMAX = 0x009,
};

/*#define DEBUG_UIC*/

#ifdef DEBUG_UIC
#  define LOG_UIC(...) qemu_log_mask(CPU_LOG_INT, ## __VA_ARGS__)
#else
#  define LOG_UIC(...) do { } while (0)
#endif

static void ppcuic_trigger_irq(PPCUIC *uic)
{
    uint32_t ir, cr;
    int start, end, inc, i;

    /* Trigger interrupt if any is pending */
    ir = uic->uicsr & uic->uicer & (~uic->uiccr);
    cr = uic->uicsr & uic->uicer & uic->uiccr;
    LOG_UIC("%s: uicsr %08" PRIx32 " uicer %08" PRIx32
                " uiccr %08" PRIx32 "\n"
                "   %08" PRIx32 " ir %08" PRIx32 " cr %08" PRIx32 "\n",
                __func__, uic->uicsr, uic->uicer, uic->uiccr,
                uic->uicsr & uic->uicer, ir, cr);
    if (ir != 0x0000000) {
        LOG_UIC("Raise UIC interrupt\n");
        qemu_irq_raise(uic->output_int);
    } else {
        LOG_UIC("Lower UIC interrupt\n");
        qemu_irq_lower(uic->output_int);
    }
    /* Trigger critical interrupt if any is pending and update vector */
    if (cr != 0x0000000) {
        qemu_irq_raise(uic->output_cint);
        if (uic->use_vectors) {
            /* Compute critical IRQ vector */
            if (uic->uicvcr & 1) {
                start = 31;
                end = 0;
                inc = -1;
            } else {
                start = 0;
                end = 31;
                inc = 1;
            }
            uic->uicvr = uic->uicvcr & 0xFFFFFFFC;
            for (i = start; i <= end; i += inc) {
                if (cr & (1 << i)) {
                    uic->uicvr += (i - start) * 512 * inc;
                    break;
                }
            }
        }
        LOG_UIC("Raise UIC critical interrupt - "
                    "vector %08" PRIx32 "\n", uic->uicvr);
    } else {
        LOG_UIC("Lower UIC critical interrupt\n");
        qemu_irq_lower(uic->output_cint);
        uic->uicvr = 0x00000000;
    }
}

static void ppcuic_set_irq(void *opaque, int irq_num, int level)
{
    PPCUIC *uic = opaque;
    uint32_t mask, sr;

    mask = 1U << (31 - irq_num);
    LOG_UIC("%s: irq %d level %d uicsr %08" PRIx32
                " mask %08" PRIx32 " => %08" PRIx32 " %08" PRIx32 "\n",
                __func__, irq_num, level,
                uic->uicsr, mask, uic->uicsr & mask, level << irq_num);
    if (irq_num < 0 || irq_num > 31) {
        return;
    }
    sr = uic->uicsr;

    /* Update status register */
    if (uic->uictr & mask) {
        /* Edge sensitive interrupt */
        if (level == 1) {
            uic->uicsr |= mask;
        }
    } else {
        /* Level sensitive interrupt */
        if (level == 1) {
            uic->uicsr |= mask;
            uic->level |= mask;
        } else {
            uic->uicsr &= ~mask;
            uic->level &= ~mask;
        }
    }
    LOG_UIC("%s: irq %d level %d sr %" PRIx32 " => "
                "%08" PRIx32 "\n", __func__, irq_num, level, uic->uicsr, sr);
    if (sr != uic->uicsr) {
        ppcuic_trigger_irq(uic);
    }
}

static uint32_t dcr_read_uic(void *opaque, int dcrn)
{
    PPCUIC *uic = opaque;
    uint32_t ret;

    dcrn -= uic->dcr_base;
    switch (dcrn) {
    case DCR_UICSR:
    case DCR_UICSRS:
        ret = uic->uicsr;
        break;
    case DCR_UICER:
        ret = uic->uicer;
        break;
    case DCR_UICCR:
        ret = uic->uiccr;
        break;
    case DCR_UICPR:
        ret = uic->uicpr;
        break;
    case DCR_UICTR:
        ret = uic->uictr;
        break;
    case DCR_UICMSR:
        ret = uic->uicsr & uic->uicer;
        break;
    case DCR_UICVR:
        if (!uic->use_vectors) {
            goto no_read;
        }
        ret = uic->uicvr;
        break;
    case DCR_UICVCR:
        if (!uic->use_vectors) {
            goto no_read;
        }
        ret = uic->uicvcr;
        break;
    default:
    no_read:
        ret = 0x00000000;
        break;
    }

    return ret;
}

static void dcr_write_uic(void *opaque, int dcrn, uint32_t val)
{
    PPCUIC *uic = opaque;

    dcrn -= uic->dcr_base;
    LOG_UIC("%s: dcr %d val 0x%x\n", __func__, dcrn, val);
    switch (dcrn) {
    case DCR_UICSR:
        uic->uicsr &= ~val;
        uic->uicsr |= uic->level;
        ppcuic_trigger_irq(uic);
        break;
    case DCR_UICSRS:
        uic->uicsr |= val;
        ppcuic_trigger_irq(uic);
        break;
    case DCR_UICER:
        uic->uicer = val;
        ppcuic_trigger_irq(uic);
        break;
    case DCR_UICCR:
        uic->uiccr = val;
        ppcuic_trigger_irq(uic);
        break;
    case DCR_UICPR:
        uic->uicpr = val;
        break;
    case DCR_UICTR:
        uic->uictr = val;
        ppcuic_trigger_irq(uic);
        break;
    case DCR_UICMSR:
        break;
    case DCR_UICVR:
        break;
    case DCR_UICVCR:
        uic->uicvcr = val & 0xFFFFFFFD;
        ppcuic_trigger_irq(uic);
        break;
    }
}

static void ppc_uic_reset(DeviceState *dev)
{
    PPCUIC *uic = PPC_UIC(dev);

    uic->uiccr = 0x00000000;
    uic->uicer = 0x00000000;
    uic->uicpr = 0x00000000;
    uic->uicsr = 0x00000000;
    uic->uictr = 0x00000000;
    if (uic->use_vectors) {
        uic->uicvcr = 0x00000000;
        uic->uicvr = 0x0000000;
    }
}

static void ppc_uic_realize(DeviceState *dev, Error **errp)
{
    PPCUIC *uic = PPC_UIC(dev);
    Ppc4xxDcrDeviceState *dcr = PPC4xx_DCR_DEVICE(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    int i;

    for (i = 0; i < DCR_UICMAX; i++) {
        ppc4xx_dcr_register(dcr, uic->dcr_base + i, uic,
                         &dcr_read_uic, &dcr_write_uic);
    }

    sysbus_init_irq(sbd, &uic->output_int);
    sysbus_init_irq(sbd, &uic->output_cint);
    qdev_init_gpio_in(dev, ppcuic_set_irq, UIC_MAX_IRQ);
}

static const Property ppc_uic_properties[] = {
    DEFINE_PROP_UINT32("dcr-base", PPCUIC, dcr_base, 0xc0),
    DEFINE_PROP_BOOL("use-vectors", PPCUIC, use_vectors, true),
};

static const VMStateDescription ppc_uic_vmstate = {
    .name = "ppc-uic",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(level, PPCUIC),
        VMSTATE_UINT32(uicsr, PPCUIC),
        VMSTATE_UINT32(uicer, PPCUIC),
        VMSTATE_UINT32(uiccr, PPCUIC),
        VMSTATE_UINT32(uicpr, PPCUIC),
        VMSTATE_UINT32(uictr, PPCUIC),
        VMSTATE_UINT32(uicvcr, PPCUIC),
        VMSTATE_UINT32(uicvr, PPCUIC),
        VMSTATE_END_OF_LIST()
    },
};

static void ppc_uic_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, ppc_uic_reset);
    dc->realize = ppc_uic_realize;
    dc->vmsd = &ppc_uic_vmstate;
    device_class_set_props(dc, ppc_uic_properties);
}

static const TypeInfo ppc_uic_info = {
    .name = TYPE_PPC_UIC,
    .parent = TYPE_PPC4xx_DCR_DEVICE,
    .instance_size = sizeof(PPCUIC),
    .class_init = ppc_uic_class_init,
};

static void ppc_uic_register_types(void)
{
    type_register_static(&ppc_uic_info);
}

type_init(ppc_uic_register_types);
