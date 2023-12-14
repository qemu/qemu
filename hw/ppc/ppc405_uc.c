/*
 * QEMU PowerPC 405 embedded processors emulation
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
#include "qemu/units.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "cpu.h"
#include "hw/ppc/ppc.h"
#include "hw/i2c/ppc4xx_i2c.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "ppc405.h"
#include "hw/char/serial.h"
#include "qemu/timer.h"
#include "sysemu/reset.h"
#include "sysemu/sysemu.h"
#include "exec/address-spaces.h"
#include "hw/intc/ppc-uic.h"
#include "trace.h"

/*****************************************************************************/
/* Shared peripherals */

/*****************************************************************************/
/* PLB to OPB bridge */
enum {
    POB0_BESR0 = 0x0A0,
    POB0_BESR1 = 0x0A2,
    POB0_BEAR  = 0x0A4,
};

static uint32_t dcr_read_pob(void *opaque, int dcrn)
{
    Ppc405PobState *pob = opaque;
    uint32_t ret;

    switch (dcrn) {
    case POB0_BEAR:
        ret = pob->bear;
        break;
    case POB0_BESR0:
        ret = pob->besr0;
        break;
    case POB0_BESR1:
        ret = pob->besr1;
        break;
    default:
        /* Avoid gcc warning */
        ret = 0;
        break;
    }

    return ret;
}

static void dcr_write_pob(void *opaque, int dcrn, uint32_t val)
{
    Ppc405PobState *pob = opaque;

    switch (dcrn) {
    case POB0_BEAR:
        /* Read only */
        break;
    case POB0_BESR0:
        /* Write-clear */
        pob->besr0 &= ~val;
        break;
    case POB0_BESR1:
        /* Write-clear */
        pob->besr1 &= ~val;
        break;
    }
}

static void ppc405_pob_reset(DeviceState *dev)
{
    Ppc405PobState *pob = PPC405_POB(dev);

    /* No error */
    pob->bear = 0x00000000;
    pob->besr0 = 0x0000000;
    pob->besr1 = 0x0000000;
}

static void ppc405_pob_realize(DeviceState *dev, Error **errp)
{
    Ppc405PobState *pob = PPC405_POB(dev);
    Ppc4xxDcrDeviceState *dcr = PPC4xx_DCR_DEVICE(dev);

    ppc4xx_dcr_register(dcr, POB0_BEAR, pob, &dcr_read_pob, &dcr_write_pob);
    ppc4xx_dcr_register(dcr, POB0_BESR0, pob, &dcr_read_pob, &dcr_write_pob);
    ppc4xx_dcr_register(dcr, POB0_BESR1, pob, &dcr_read_pob, &dcr_write_pob);
}

static void ppc405_pob_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = ppc405_pob_realize;
    dc->reset = ppc405_pob_reset;
    /* Reason: only works as function of a ppc4xx SoC */
    dc->user_creatable = false;
}

/*****************************************************************************/
/* OPB arbitrer */
static uint64_t opba_readb(void *opaque, hwaddr addr, unsigned size)
{
    Ppc405OpbaState *opba = opaque;
    uint32_t ret;

    switch (addr) {
    case 0x00:
        ret = opba->cr;
        break;
    case 0x01:
        ret = opba->pr;
        break;
    default:
        ret = 0x00;
        break;
    }

    trace_opba_readb(addr, ret);
    return ret;
}

static void opba_writeb(void *opaque, hwaddr addr, uint64_t value,
                        unsigned size)
{
    Ppc405OpbaState *opba = opaque;

    trace_opba_writeb(addr, value);

    switch (addr) {
    case 0x00:
        opba->cr = value & 0xF8;
        break;
    case 0x01:
        opba->pr = value & 0xFF;
        break;
    default:
        break;
    }
}
static const MemoryRegionOps opba_ops = {
    .read = opba_readb,
    .write = opba_writeb,
    .impl.min_access_size = 1,
    .impl.max_access_size = 1,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void ppc405_opba_reset(DeviceState *dev)
{
    Ppc405OpbaState *opba = PPC405_OPBA(dev);

    opba->cr = 0x00; /* No dynamic priorities - park disabled */
    opba->pr = 0x11;
}

static void ppc405_opba_realize(DeviceState *dev, Error **errp)
{
    Ppc405OpbaState *s = PPC405_OPBA(dev);

    memory_region_init_io(&s->io, OBJECT(s), &opba_ops, s, "opba", 2);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->io);
}

static void ppc405_opba_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = ppc405_opba_realize;
    dc->reset = ppc405_opba_reset;
    /* Reason: only works as function of a ppc4xx SoC */
    dc->user_creatable = false;
}

/*****************************************************************************/
/* Code decompression controller */
/* XXX: TODO */

/*****************************************************************************/
/* DMA controller */
enum {
    DMA0_CR0 = 0x100,
    DMA0_CT0 = 0x101,
    DMA0_DA0 = 0x102,
    DMA0_SA0 = 0x103,
    DMA0_SG0 = 0x104,
    DMA0_CR1 = 0x108,
    DMA0_CT1 = 0x109,
    DMA0_DA1 = 0x10A,
    DMA0_SA1 = 0x10B,
    DMA0_SG1 = 0x10C,
    DMA0_CR2 = 0x110,
    DMA0_CT2 = 0x111,
    DMA0_DA2 = 0x112,
    DMA0_SA2 = 0x113,
    DMA0_SG2 = 0x114,
    DMA0_CR3 = 0x118,
    DMA0_CT3 = 0x119,
    DMA0_DA3 = 0x11A,
    DMA0_SA3 = 0x11B,
    DMA0_SG3 = 0x11C,
    DMA0_SR  = 0x120,
    DMA0_SGC = 0x123,
    DMA0_SLP = 0x125,
    DMA0_POL = 0x126,
};

static uint32_t dcr_read_dma(void *opaque, int dcrn)
{
    return 0;
}

static void dcr_write_dma(void *opaque, int dcrn, uint32_t val)
{
}

static void ppc405_dma_reset(DeviceState *dev)
{
    Ppc405DmaState *dma = PPC405_DMA(dev);
    int i;

    for (i = 0; i < 4; i++) {
        dma->cr[i] = 0x00000000;
        dma->ct[i] = 0x00000000;
        dma->da[i] = 0x00000000;
        dma->sa[i] = 0x00000000;
        dma->sg[i] = 0x00000000;
    }
    dma->sr = 0x00000000;
    dma->sgc = 0x00000000;
    dma->slp = 0x7C000000;
    dma->pol = 0x00000000;
}

static void ppc405_dma_realize(DeviceState *dev, Error **errp)
{
    Ppc405DmaState *dma = PPC405_DMA(dev);
    Ppc4xxDcrDeviceState *dcr = PPC4xx_DCR_DEVICE(dev);
    int i;

    for (i = 0; i < ARRAY_SIZE(dma->irqs); i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(dma), &dma->irqs[i]);
    }

    ppc4xx_dcr_register(dcr, DMA0_CR0, dma, &dcr_read_dma, &dcr_write_dma);
    ppc4xx_dcr_register(dcr, DMA0_CT0, dma, &dcr_read_dma, &dcr_write_dma);
    ppc4xx_dcr_register(dcr, DMA0_DA0, dma, &dcr_read_dma, &dcr_write_dma);
    ppc4xx_dcr_register(dcr, DMA0_SA0, dma, &dcr_read_dma, &dcr_write_dma);
    ppc4xx_dcr_register(dcr, DMA0_SG0, dma, &dcr_read_dma, &dcr_write_dma);
    ppc4xx_dcr_register(dcr, DMA0_CR1, dma, &dcr_read_dma, &dcr_write_dma);
    ppc4xx_dcr_register(dcr, DMA0_CT1, dma, &dcr_read_dma, &dcr_write_dma);
    ppc4xx_dcr_register(dcr, DMA0_DA1, dma, &dcr_read_dma, &dcr_write_dma);
    ppc4xx_dcr_register(dcr, DMA0_SA1, dma, &dcr_read_dma, &dcr_write_dma);
    ppc4xx_dcr_register(dcr, DMA0_SG1, dma, &dcr_read_dma, &dcr_write_dma);
    ppc4xx_dcr_register(dcr, DMA0_CR2, dma, &dcr_read_dma, &dcr_write_dma);
    ppc4xx_dcr_register(dcr, DMA0_CT2, dma, &dcr_read_dma, &dcr_write_dma);
    ppc4xx_dcr_register(dcr, DMA0_DA2, dma, &dcr_read_dma, &dcr_write_dma);
    ppc4xx_dcr_register(dcr, DMA0_SA2, dma, &dcr_read_dma, &dcr_write_dma);
    ppc4xx_dcr_register(dcr, DMA0_SG2, dma, &dcr_read_dma, &dcr_write_dma);
    ppc4xx_dcr_register(dcr, DMA0_CR3, dma, &dcr_read_dma, &dcr_write_dma);
    ppc4xx_dcr_register(dcr, DMA0_CT3, dma, &dcr_read_dma, &dcr_write_dma);
    ppc4xx_dcr_register(dcr, DMA0_DA3, dma, &dcr_read_dma, &dcr_write_dma);
    ppc4xx_dcr_register(dcr, DMA0_SA3, dma, &dcr_read_dma, &dcr_write_dma);
    ppc4xx_dcr_register(dcr, DMA0_SG3, dma, &dcr_read_dma, &dcr_write_dma);
    ppc4xx_dcr_register(dcr, DMA0_SR,  dma, &dcr_read_dma, &dcr_write_dma);
    ppc4xx_dcr_register(dcr, DMA0_SGC, dma, &dcr_read_dma, &dcr_write_dma);
    ppc4xx_dcr_register(dcr, DMA0_SLP, dma, &dcr_read_dma, &dcr_write_dma);
    ppc4xx_dcr_register(dcr, DMA0_POL, dma, &dcr_read_dma, &dcr_write_dma);
}

static void ppc405_dma_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = ppc405_dma_realize;
    dc->reset = ppc405_dma_reset;
    /* Reason: only works as function of a ppc4xx SoC */
    dc->user_creatable = false;
}

/*****************************************************************************/
/* GPIO */
static uint64_t ppc405_gpio_read(void *opaque, hwaddr addr, unsigned size)
{
    trace_ppc405_gpio_read(addr, size);
    return 0;
}

static void ppc405_gpio_write(void *opaque, hwaddr addr, uint64_t value,
                              unsigned size)
{
    trace_ppc405_gpio_write(addr, size, value);
}

static const MemoryRegionOps ppc405_gpio_ops = {
    .read = ppc405_gpio_read,
    .write = ppc405_gpio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ppc405_gpio_realize(DeviceState *dev, Error **errp)
{
    Ppc405GpioState *s = PPC405_GPIO(dev);

    memory_region_init_io(&s->io, OBJECT(s), &ppc405_gpio_ops, s, "gpio",
                          0x38);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->io);
}

static void ppc405_gpio_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = ppc405_gpio_realize;
    /* Reason: only works as function of a ppc4xx SoC */
    dc->user_creatable = false;
}

/*****************************************************************************/
/* On Chip Memory */
enum {
    OCM0_ISARC   = 0x018,
    OCM0_ISACNTL = 0x019,
    OCM0_DSARC   = 0x01A,
    OCM0_DSACNTL = 0x01B,
};

static void ocm_update_mappings(Ppc405OcmState *ocm,
                                uint32_t isarc, uint32_t isacntl,
                                uint32_t dsarc, uint32_t dsacntl)
{
    trace_ocm_update_mappings(isarc, isacntl, dsarc, dsacntl, ocm->isarc,
                              ocm->isacntl, ocm->dsarc, ocm->dsacntl);

    if (ocm->isarc != isarc ||
        (ocm->isacntl & 0x80000000) != (isacntl & 0x80000000)) {
        if (ocm->isacntl & 0x80000000) {
            /* Unmap previously assigned memory region */
            trace_ocm_unmap("ISA", ocm->isarc);
            memory_region_del_subregion(get_system_memory(), &ocm->isarc_ram);
        }
        if (isacntl & 0x80000000) {
            /* Map new instruction memory region */
            trace_ocm_map("ISA", isarc);
            memory_region_add_subregion(get_system_memory(), isarc,
                                        &ocm->isarc_ram);
        }
    }
    if (ocm->dsarc != dsarc ||
        (ocm->dsacntl & 0x80000000) != (dsacntl & 0x80000000)) {
        if (ocm->dsacntl & 0x80000000) {
            /* Beware not to unmap the region we just mapped */
            if (!(isacntl & 0x80000000) || ocm->dsarc != isarc) {
                /* Unmap previously assigned memory region */
                trace_ocm_unmap("DSA", ocm->dsarc);
                memory_region_del_subregion(get_system_memory(),
                                            &ocm->dsarc_ram);
            }
        }
        if (dsacntl & 0x80000000) {
            /* Beware not to remap the region we just mapped */
            if (!(isacntl & 0x80000000) || dsarc != isarc) {
                /* Map new data memory region */
                trace_ocm_map("DSA", dsarc);
                memory_region_add_subregion(get_system_memory(), dsarc,
                                            &ocm->dsarc_ram);
            }
        }
    }
}

static uint32_t dcr_read_ocm(void *opaque, int dcrn)
{
    Ppc405OcmState *ocm = opaque;
    uint32_t ret;

    switch (dcrn) {
    case OCM0_ISARC:
        ret = ocm->isarc;
        break;
    case OCM0_ISACNTL:
        ret = ocm->isacntl;
        break;
    case OCM0_DSARC:
        ret = ocm->dsarc;
        break;
    case OCM0_DSACNTL:
        ret = ocm->dsacntl;
        break;
    default:
        ret = 0;
        break;
    }

    return ret;
}

static void dcr_write_ocm(void *opaque, int dcrn, uint32_t val)
{
    Ppc405OcmState *ocm = opaque;
    uint32_t isarc, dsarc, isacntl, dsacntl;

    isarc = ocm->isarc;
    dsarc = ocm->dsarc;
    isacntl = ocm->isacntl;
    dsacntl = ocm->dsacntl;
    switch (dcrn) {
    case OCM0_ISARC:
        isarc = val & 0xFC000000;
        break;
    case OCM0_ISACNTL:
        isacntl = val & 0xC0000000;
        break;
    case OCM0_DSARC:
        isarc = val & 0xFC000000;
        break;
    case OCM0_DSACNTL:
        isacntl = val & 0xC0000000;
        break;
    }
    ocm_update_mappings(ocm, isarc, isacntl, dsarc, dsacntl);
    ocm->isarc = isarc;
    ocm->dsarc = dsarc;
    ocm->isacntl = isacntl;
    ocm->dsacntl = dsacntl;
}

static void ppc405_ocm_reset(DeviceState *dev)
{
    Ppc405OcmState *ocm = PPC405_OCM(dev);
    uint32_t isarc, dsarc, isacntl, dsacntl;

    isarc = 0x00000000;
    isacntl = 0x00000000;
    dsarc = 0x00000000;
    dsacntl = 0x00000000;
    ocm_update_mappings(ocm, isarc, isacntl, dsarc, dsacntl);
    ocm->isarc = isarc;
    ocm->dsarc = dsarc;
    ocm->isacntl = isacntl;
    ocm->dsacntl = dsacntl;
}

static void ppc405_ocm_realize(DeviceState *dev, Error **errp)
{
    Ppc405OcmState *ocm = PPC405_OCM(dev);
    Ppc4xxDcrDeviceState *dcr = PPC4xx_DCR_DEVICE(dev);

    /* XXX: Size is 4096 or 0x04000000 */
    memory_region_init_ram(&ocm->isarc_ram, OBJECT(ocm), "ppc405.ocm", 4 * KiB,
                           &error_fatal);
    memory_region_init_alias(&ocm->dsarc_ram, OBJECT(ocm), "ppc405.dsarc",
                             &ocm->isarc_ram, 0, 4 * KiB);

    ppc4xx_dcr_register(dcr, OCM0_ISARC, ocm, &dcr_read_ocm, &dcr_write_ocm);
    ppc4xx_dcr_register(dcr, OCM0_ISACNTL, ocm, &dcr_read_ocm, &dcr_write_ocm);
    ppc4xx_dcr_register(dcr, OCM0_DSARC, ocm, &dcr_read_ocm, &dcr_write_ocm);
    ppc4xx_dcr_register(dcr, OCM0_DSACNTL, ocm, &dcr_read_ocm, &dcr_write_ocm);
}

static void ppc405_ocm_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = ppc405_ocm_realize;
    dc->reset = ppc405_ocm_reset;
    /* Reason: only works as function of a ppc4xx SoC */
    dc->user_creatable = false;
}

/*****************************************************************************/
/* General purpose timers */
static int ppc4xx_gpt_compare(Ppc405GptState *gpt, int n)
{
    /* XXX: TODO */
    return 0;
}

static void ppc4xx_gpt_set_output(Ppc405GptState *gpt, int n, int level)
{
    /* XXX: TODO */
}

static void ppc4xx_gpt_set_outputs(Ppc405GptState *gpt)
{
    uint32_t mask;
    int i;

    mask = 0x80000000;
    for (i = 0; i < 5; i++) {
        if (gpt->oe & mask) {
            /* Output is enabled */
            if (ppc4xx_gpt_compare(gpt, i)) {
                /* Comparison is OK */
                ppc4xx_gpt_set_output(gpt, i, gpt->ol & mask);
            } else {
                /* Comparison is KO */
                ppc4xx_gpt_set_output(gpt, i, gpt->ol & mask ? 0 : 1);
            }
        }
        mask = mask >> 1;
    }
}

static void ppc4xx_gpt_set_irqs(Ppc405GptState *gpt)
{
    uint32_t mask;
    int i;

    mask = 0x00008000;
    for (i = 0; i < 5; i++) {
        if (gpt->is & gpt->im & mask) {
            qemu_irq_raise(gpt->irqs[i]);
        } else {
            qemu_irq_lower(gpt->irqs[i]);
        }
        mask = mask >> 1;
    }
}

static void ppc4xx_gpt_compute_timer(Ppc405GptState *gpt)
{
    /* XXX: TODO */
}

static uint64_t ppc4xx_gpt_read(void *opaque, hwaddr addr, unsigned size)
{
    Ppc405GptState *gpt = opaque;
    uint32_t ret;
    int idx;

    trace_ppc4xx_gpt_read(addr, size);

    switch (addr) {
    case 0x00:
        /* Time base counter */
        ret = muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + gpt->tb_offset,
                       gpt->tb_freq, NANOSECONDS_PER_SECOND);
        break;
    case 0x10:
        /* Output enable */
        ret = gpt->oe;
        break;
    case 0x14:
        /* Output level */
        ret = gpt->ol;
        break;
    case 0x18:
        /* Interrupt mask */
        ret = gpt->im;
        break;
    case 0x1C:
    case 0x20:
        /* Interrupt status */
        ret = gpt->is;
        break;
    case 0x24:
        /* Interrupt enable */
        ret = gpt->ie;
        break;
    case 0x80 ... 0x90:
        /* Compare timer */
        idx = (addr - 0x80) >> 2;
        ret = gpt->comp[idx];
        break;
    case 0xC0 ... 0xD0:
        /* Compare mask */
        idx = (addr - 0xC0) >> 2;
        ret = gpt->mask[idx];
        break;
    default:
        ret = -1;
        break;
    }

    return ret;
}

static void ppc4xx_gpt_write(void *opaque, hwaddr addr, uint64_t value,
                             unsigned size)
{
    Ppc405GptState *gpt = opaque;
    int idx;

    trace_ppc4xx_gpt_write(addr, size, value);

    switch (addr) {
    case 0x00:
        /* Time base counter */
        gpt->tb_offset = muldiv64(value, NANOSECONDS_PER_SECOND, gpt->tb_freq)
            - qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        ppc4xx_gpt_compute_timer(gpt);
        break;
    case 0x10:
        /* Output enable */
        gpt->oe = value & 0xF8000000;
        ppc4xx_gpt_set_outputs(gpt);
        break;
    case 0x14:
        /* Output level */
        gpt->ol = value & 0xF8000000;
        ppc4xx_gpt_set_outputs(gpt);
        break;
    case 0x18:
        /* Interrupt mask */
        gpt->im = value & 0x0000F800;
        break;
    case 0x1C:
        /* Interrupt status set */
        gpt->is |= value & 0x0000F800;
        ppc4xx_gpt_set_irqs(gpt);
        break;
    case 0x20:
        /* Interrupt status clear */
        gpt->is &= ~(value & 0x0000F800);
        ppc4xx_gpt_set_irqs(gpt);
        break;
    case 0x24:
        /* Interrupt enable */
        gpt->ie = value & 0x0000F800;
        ppc4xx_gpt_set_irqs(gpt);
        break;
    case 0x80 ... 0x90:
        /* Compare timer */
        idx = (addr - 0x80) >> 2;
        gpt->comp[idx] = value & 0xF8000000;
        ppc4xx_gpt_compute_timer(gpt);
        break;
    case 0xC0 ... 0xD0:
        /* Compare mask */
        idx = (addr - 0xC0) >> 2;
        gpt->mask[idx] = value & 0xF8000000;
        ppc4xx_gpt_compute_timer(gpt);
        break;
    }
}

static const MemoryRegionOps gpt_ops = {
    .read = ppc4xx_gpt_read,
    .write = ppc4xx_gpt_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ppc4xx_gpt_cb(void *opaque)
{
    Ppc405GptState *gpt = opaque;

    ppc4xx_gpt_set_irqs(gpt);
    ppc4xx_gpt_set_outputs(gpt);
    ppc4xx_gpt_compute_timer(gpt);
}

static void ppc405_gpt_reset(DeviceState *dev)
{
    Ppc405GptState *gpt = PPC405_GPT(dev);
    int i;

    timer_del(gpt->timer);
    gpt->oe = 0x00000000;
    gpt->ol = 0x00000000;
    gpt->im = 0x00000000;
    gpt->is = 0x00000000;
    gpt->ie = 0x00000000;
    for (i = 0; i < 5; i++) {
        gpt->comp[i] = 0x00000000;
        gpt->mask[i] = 0x00000000;
    }
}

static void ppc405_gpt_realize(DeviceState *dev, Error **errp)
{
    Ppc405GptState *s = PPC405_GPT(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    int i;

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, &ppc4xx_gpt_cb, s);
    memory_region_init_io(&s->iomem, OBJECT(s), &gpt_ops, s, "gpt", 0xd4);
    sysbus_init_mmio(sbd, &s->iomem);

    for (i = 0; i < ARRAY_SIZE(s->irqs); i++) {
        sysbus_init_irq(sbd, &s->irqs[i]);
    }
}

static void ppc405_gpt_finalize(Object *obj)
{
    /* timer will be NULL if the GPT wasn't realized */
    if (PPC405_GPT(obj)->timer) {
        timer_del(PPC405_GPT(obj)->timer);
    }
}

static void ppc405_gpt_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = ppc405_gpt_realize;
    dc->reset = ppc405_gpt_reset;
    /* Reason: only works as function of a ppc4xx SoC */
    dc->user_creatable = false;
}

/*****************************************************************************/
/* PowerPC 405EP */
/* CPU control */
enum {
    PPC405EP_CPC0_PLLMR0 = 0x0F0,
    PPC405EP_CPC0_BOOT   = 0x0F1,
    PPC405EP_CPC0_EPCTL  = 0x0F3,
    PPC405EP_CPC0_PLLMR1 = 0x0F4,
    PPC405EP_CPC0_UCR    = 0x0F5,
    PPC405EP_CPC0_SRR    = 0x0F6,
    PPC405EP_CPC0_JTAGID = 0x0F7,
    PPC405EP_CPC0_PCI    = 0x0F9,
#if 0
    PPC405EP_CPC0_ER     = xxx,
    PPC405EP_CPC0_FR     = xxx,
    PPC405EP_CPC0_SR     = xxx,
#endif
};

static void ppc405ep_compute_clocks(Ppc405CpcState *cpc)
{
    uint32_t CPU_clk, PLB_clk, OPB_clk, EBC_clk, MAL_clk, PCI_clk;
    uint32_t UART0_clk, UART1_clk;
    uint64_t VCO_out, PLL_out;
    int M, D;

    VCO_out = 0;
    if ((cpc->pllmr[1] & 0x80000000) && !(cpc->pllmr[1] & 0x40000000)) {
        M = (((cpc->pllmr[1] >> 20) - 1) & 0xF) + 1; /* FBMUL */
        trace_ppc405ep_clocks_compute("FBMUL", (cpc->pllmr[1] >> 20) & 0xF, M);
        D = 8 - ((cpc->pllmr[1] >> 16) & 0x7); /* FWDA */
        trace_ppc405ep_clocks_compute("FWDA", (cpc->pllmr[1] >> 16) & 0x7, D);
        VCO_out = (uint64_t)cpc->sysclk * M * D;
        if (VCO_out < 500000000UL || VCO_out > 1000000000UL) {
            /* Error - unlock the PLL */
            qemu_log_mask(LOG_GUEST_ERROR, "VCO out of range %" PRIu64 "\n",
                          VCO_out);
#if 0
            cpc->pllmr[1] &= ~0x80000000;
            goto pll_bypass;
#endif
        }
        PLL_out = VCO_out / D;
        /* Pretend the PLL is locked */
        cpc->boot |= 0x00000001;
    } else {
#if 0
    pll_bypass:
#endif
        PLL_out = cpc->sysclk;
        if (cpc->pllmr[1] & 0x40000000) {
            /* Pretend the PLL is not locked */
            cpc->boot &= ~0x00000001;
        }
    }
    /* Now, compute all other clocks */
    D = ((cpc->pllmr[0] >> 20) & 0x3) + 1; /* CCDV */
     trace_ppc405ep_clocks_compute("CCDV", (cpc->pllmr[0] >> 20) & 0x3, D);
    CPU_clk = PLL_out / D;
    D = ((cpc->pllmr[0] >> 16) & 0x3) + 1; /* CBDV */
    trace_ppc405ep_clocks_compute("CBDV", (cpc->pllmr[0] >> 16) & 0x3, D);
    PLB_clk = CPU_clk / D;
    D = ((cpc->pllmr[0] >> 12) & 0x3) + 1; /* OPDV */
    trace_ppc405ep_clocks_compute("OPDV", (cpc->pllmr[0] >> 12) & 0x3, D);
    OPB_clk = PLB_clk / D;
    D = ((cpc->pllmr[0] >> 8) & 0x3) + 2; /* EPDV */
    trace_ppc405ep_clocks_compute("EPDV", (cpc->pllmr[0] >> 8) & 0x3, D);
    EBC_clk = PLB_clk / D;
    D = ((cpc->pllmr[0] >> 4) & 0x3) + 1; /* MPDV */
    trace_ppc405ep_clocks_compute("MPDV", (cpc->pllmr[0] >> 4) & 0x3, D);
    MAL_clk = PLB_clk / D;
    D = (cpc->pllmr[0] & 0x3) + 1; /* PPDV */
    trace_ppc405ep_clocks_compute("PPDV", cpc->pllmr[0] & 0x3, D);
    PCI_clk = PLB_clk / D;
    D = ((cpc->ucr - 1) & 0x7F) + 1; /* U0DIV */
    trace_ppc405ep_clocks_compute("U0DIV", cpc->ucr & 0x7F, D);
    UART0_clk = PLL_out / D;
    D = (((cpc->ucr >> 8) - 1) & 0x7F) + 1; /* U1DIV */
    trace_ppc405ep_clocks_compute("U1DIV", (cpc->ucr >> 8) & 0x7F, D);
    UART1_clk = PLL_out / D;

    if (trace_event_get_state_backends(TRACE_PPC405EP_CLOCKS_SETUP)) {
        g_autofree char *trace = g_strdup_printf(
            "Setup PPC405EP clocks - sysclk %" PRIu32 " VCO %" PRIu64
            " PLL out %" PRIu64 " Hz\n"
            "CPU %" PRIu32 " PLB %" PRIu32 " OPB %" PRIu32 " EBC %" PRIu32
            " MAL %" PRIu32 " PCI %" PRIu32 " UART0 %" PRIu32
            " UART1 %" PRIu32 "\n",
            cpc->sysclk, VCO_out, PLL_out,
            CPU_clk, PLB_clk, OPB_clk, EBC_clk, MAL_clk, PCI_clk,
            UART0_clk, UART1_clk);
        trace_ppc405ep_clocks_setup(trace);
    }

    /* Setup CPU clocks */
    clk_setup(&cpc->clk_setup[PPC405EP_CPU_CLK], CPU_clk);
    /* Setup PLB clock */
    clk_setup(&cpc->clk_setup[PPC405EP_PLB_CLK], PLB_clk);
    /* Setup OPB clock */
    clk_setup(&cpc->clk_setup[PPC405EP_OPB_CLK], OPB_clk);
    /* Setup external clock */
    clk_setup(&cpc->clk_setup[PPC405EP_EBC_CLK], EBC_clk);
    /* Setup MAL clock */
    clk_setup(&cpc->clk_setup[PPC405EP_MAL_CLK], MAL_clk);
    /* Setup PCI clock */
    clk_setup(&cpc->clk_setup[PPC405EP_PCI_CLK], PCI_clk);
    /* Setup UART0 clock */
    clk_setup(&cpc->clk_setup[PPC405EP_UART0_CLK], UART0_clk);
    /* Setup UART1 clock */
    clk_setup(&cpc->clk_setup[PPC405EP_UART1_CLK], UART1_clk);
}

static uint32_t dcr_read_epcpc(void *opaque, int dcrn)
{
    Ppc405CpcState *cpc = opaque;
    uint32_t ret;

    switch (dcrn) {
    case PPC405EP_CPC0_BOOT:
        ret = cpc->boot;
        break;
    case PPC405EP_CPC0_EPCTL:
        ret = cpc->epctl;
        break;
    case PPC405EP_CPC0_PLLMR0:
        ret = cpc->pllmr[0];
        break;
    case PPC405EP_CPC0_PLLMR1:
        ret = cpc->pllmr[1];
        break;
    case PPC405EP_CPC0_UCR:
        ret = cpc->ucr;
        break;
    case PPC405EP_CPC0_SRR:
        ret = cpc->srr;
        break;
    case PPC405EP_CPC0_JTAGID:
        ret = cpc->jtagid;
        break;
    case PPC405EP_CPC0_PCI:
        ret = cpc->pci;
        break;
    default:
        /* Avoid gcc warning */
        ret = 0;
        break;
    }

    return ret;
}

static void dcr_write_epcpc(void *opaque, int dcrn, uint32_t val)
{
    Ppc405CpcState *cpc = opaque;

    switch (dcrn) {
    case PPC405EP_CPC0_BOOT:
        /* Read-only register */
        break;
    case PPC405EP_CPC0_EPCTL:
        /* Don't care for now */
        cpc->epctl = val & 0xC00000F3;
        break;
    case PPC405EP_CPC0_PLLMR0:
        cpc->pllmr[0] = val & 0x00633333;
        ppc405ep_compute_clocks(cpc);
        break;
    case PPC405EP_CPC0_PLLMR1:
        cpc->pllmr[1] = val & 0xC0F73FFF;
        ppc405ep_compute_clocks(cpc);
        break;
    case PPC405EP_CPC0_UCR:
        /* UART control - don't care for now */
        cpc->ucr = val & 0x003F7F7F;
        break;
    case PPC405EP_CPC0_SRR:
        cpc->srr = val;
        break;
    case PPC405EP_CPC0_JTAGID:
        /* Read-only */
        break;
    case PPC405EP_CPC0_PCI:
        cpc->pci = val;
        break;
    }
}

static void ppc405_cpc_reset(DeviceState *dev)
{
    Ppc405CpcState *cpc = PPC405_CPC(dev);

    cpc->boot = 0x00000010;     /* Boot from PCI - IIC EEPROM disabled */
    cpc->epctl = 0x00000000;
    cpc->pllmr[0] = 0x00021002;
    cpc->pllmr[1] = 0x80a552be;
    cpc->ucr = 0x00004646;
    cpc->srr = 0x00040000;
    cpc->pci = 0x00000000;
    cpc->er = 0x00000000;
    cpc->fr = 0x00000000;
    cpc->sr = 0x00000000;
    cpc->jtagid = 0x20267049;
    ppc405ep_compute_clocks(cpc);
}

/* XXX: sysclk should be between 25 and 100 MHz */
static void ppc405_cpc_realize(DeviceState *dev, Error **errp)
{
    Ppc405CpcState *cpc = PPC405_CPC(dev);
    Ppc4xxDcrDeviceState *dcr = PPC4xx_DCR_DEVICE(dev);

    assert(dcr->cpu);
    cpc->clk_setup[PPC405EP_CPU_CLK].cb =
        ppc_40x_timers_init(&dcr->cpu->env, cpc->sysclk, PPC_INTERRUPT_PIT);
    cpc->clk_setup[PPC405EP_CPU_CLK].opaque = &dcr->cpu->env;

    ppc4xx_dcr_register(dcr, PPC405EP_CPC0_BOOT, cpc,
                        &dcr_read_epcpc, &dcr_write_epcpc);
    ppc4xx_dcr_register(dcr, PPC405EP_CPC0_EPCTL, cpc,
                        &dcr_read_epcpc, &dcr_write_epcpc);
    ppc4xx_dcr_register(dcr, PPC405EP_CPC0_PLLMR0, cpc,
                        &dcr_read_epcpc, &dcr_write_epcpc);
    ppc4xx_dcr_register(dcr, PPC405EP_CPC0_PLLMR1, cpc,
                        &dcr_read_epcpc, &dcr_write_epcpc);
    ppc4xx_dcr_register(dcr, PPC405EP_CPC0_UCR, cpc,
                        &dcr_read_epcpc, &dcr_write_epcpc);
    ppc4xx_dcr_register(dcr, PPC405EP_CPC0_SRR, cpc,
                        &dcr_read_epcpc, &dcr_write_epcpc);
    ppc4xx_dcr_register(dcr, PPC405EP_CPC0_JTAGID, cpc,
                        &dcr_read_epcpc, &dcr_write_epcpc);
    ppc4xx_dcr_register(dcr, PPC405EP_CPC0_PCI, cpc,
                        &dcr_read_epcpc, &dcr_write_epcpc);
}

static Property ppc405_cpc_properties[] = {
    DEFINE_PROP_UINT32("sys-clk", Ppc405CpcState, sysclk, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void ppc405_cpc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = ppc405_cpc_realize;
    dc->reset = ppc405_cpc_reset;
    /* Reason: only works as function of a ppc4xx SoC */
    dc->user_creatable = false;
    device_class_set_props(dc, ppc405_cpc_properties);
}

/* PPC405_SOC */

static void ppc405_soc_instance_init(Object *obj)
{
    Ppc405SoCState *s = PPC405_SOC(obj);

    object_initialize_child(obj, "cpu", &s->cpu,
                            POWERPC_CPU_TYPE_NAME("405ep"));

    object_initialize_child(obj, "uic", &s->uic, TYPE_PPC_UIC);

    object_initialize_child(obj, "cpc", &s->cpc, TYPE_PPC405_CPC);
    object_property_add_alias(obj, "sys-clk", OBJECT(&s->cpc), "sys-clk");

    object_initialize_child(obj, "gpt", &s->gpt, TYPE_PPC405_GPT);

    object_initialize_child(obj, "ocm", &s->ocm, TYPE_PPC405_OCM);

    object_initialize_child(obj, "gpio", &s->gpio, TYPE_PPC405_GPIO);

    object_initialize_child(obj, "dma", &s->dma, TYPE_PPC405_DMA);

    object_initialize_child(obj, "i2c", &s->i2c, TYPE_PPC4xx_I2C);

    object_initialize_child(obj, "ebc", &s->ebc, TYPE_PPC4xx_EBC);

    object_initialize_child(obj, "opba", &s->opba, TYPE_PPC405_OPBA);

    object_initialize_child(obj, "pob", &s->pob, TYPE_PPC405_POB);

    object_initialize_child(obj, "plb", &s->plb, TYPE_PPC4xx_PLB);

    object_initialize_child(obj, "mal", &s->mal, TYPE_PPC4xx_MAL);

    object_initialize_child(obj, "sdram", &s->sdram, TYPE_PPC4xx_SDRAM_DDR);
    object_property_add_alias(obj, "dram", OBJECT(&s->sdram), "dram");
}

static void ppc405_reset(void *opaque)
{
    cpu_reset(CPU(opaque));
}

static void ppc405_soc_realize(DeviceState *dev, Error **errp)
{
    Ppc405SoCState *s = PPC405_SOC(dev);
    CPUPPCState *env;
    SysBusDevice *sbd;
    int i;

    /* init CPUs */
    if (!qdev_realize(DEVICE(&s->cpu), NULL, errp)) {
        return;
    }
    qemu_register_reset(ppc405_reset, &s->cpu);

    env = &s->cpu.env;

    ppc_dcr_init(env, NULL, NULL);

    /* CPU control */
    if (!ppc4xx_dcr_realize(PPC4xx_DCR_DEVICE(&s->cpc), &s->cpu, errp)) {
        return;
    }

    /* PLB arbitrer */
    if (!ppc4xx_dcr_realize(PPC4xx_DCR_DEVICE(&s->plb), &s->cpu, errp)) {
        return;
    }

    /* PLB to OPB bridge */
    if (!ppc4xx_dcr_realize(PPC4xx_DCR_DEVICE(&s->pob), &s->cpu, errp)) {
        return;
    }

    /* OBP arbitrer */
    sbd = SYS_BUS_DEVICE(&s->opba);
    if (!sysbus_realize(sbd, errp)) {
        return;
    }
    sysbus_mmio_map(sbd, 0, 0xef600600);

    /* Universal interrupt controller */
    if (!ppc4xx_dcr_realize(PPC4xx_DCR_DEVICE(&s->uic), &s->cpu, errp)) {
        return;
    }
    sbd = SYS_BUS_DEVICE(&s->uic);
    sysbus_connect_irq(sbd, PPCUIC_OUTPUT_INT,
                       qdev_get_gpio_in(DEVICE(&s->cpu), PPC40x_INPUT_INT));
    sysbus_connect_irq(sbd, PPCUIC_OUTPUT_CINT,
                       qdev_get_gpio_in(DEVICE(&s->cpu), PPC40x_INPUT_CINT));

    /* SDRAM controller */
    /*
     * We use the 440 DDR SDRAM controller which has more regs and features
     * but it's compatible enough for now
     */
    object_property_set_int(OBJECT(&s->sdram), "nbanks", 2, &error_abort);
    if (!ppc4xx_dcr_realize(PPC4xx_DCR_DEVICE(&s->sdram), &s->cpu, errp)) {
        return;
    }
    /* XXX 405EP has no ECC interrupt */
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->sdram), 0,
                       qdev_get_gpio_in(DEVICE(&s->uic), 17));

    /* External bus controller */
    if (!ppc4xx_dcr_realize(PPC4xx_DCR_DEVICE(&s->ebc), &s->cpu, errp)) {
        return;
    }

    /* DMA controller */
    if (!ppc4xx_dcr_realize(PPC4xx_DCR_DEVICE(&s->dma), &s->cpu, errp)) {
        return;
    }
    sbd = SYS_BUS_DEVICE(&s->dma);
    for (i = 0; i < ARRAY_SIZE(s->dma.irqs); i++) {
        sysbus_connect_irq(sbd, i, qdev_get_gpio_in(DEVICE(&s->uic), 5 + i));
    }

    /* I2C controller */
    sbd = SYS_BUS_DEVICE(&s->i2c);
    if (!sysbus_realize(sbd, errp)) {
        return;
    }
    sysbus_mmio_map(sbd, 0, 0xef600500);
    sysbus_connect_irq(sbd, 0, qdev_get_gpio_in(DEVICE(&s->uic), 2));

    /* GPIO */
    sbd = SYS_BUS_DEVICE(&s->gpio);
    if (!sysbus_realize(sbd, errp)) {
        return;
    }
    sysbus_mmio_map(sbd, 0, 0xef600700);

    /* Serial ports */
    if (serial_hd(0) != NULL) {
        serial_mm_init(get_system_memory(), 0xef600300, 0,
                       qdev_get_gpio_in(DEVICE(&s->uic), 0),
                       PPC_SERIAL_MM_BAUDBASE, serial_hd(0),
                       DEVICE_BIG_ENDIAN);
    }
    if (serial_hd(1) != NULL) {
        serial_mm_init(get_system_memory(), 0xef600400, 0,
                       qdev_get_gpio_in(DEVICE(&s->uic), 1),
                       PPC_SERIAL_MM_BAUDBASE, serial_hd(1),
                       DEVICE_BIG_ENDIAN);
    }

    /* OCM */
    if (!ppc4xx_dcr_realize(PPC4xx_DCR_DEVICE(&s->ocm), &s->cpu, errp)) {
        return;
    }

    /* GPT */
    sbd = SYS_BUS_DEVICE(&s->gpt);
    if (!sysbus_realize(sbd, errp)) {
        return;
    }
    sysbus_mmio_map(sbd, 0, 0xef600000);
    for (i = 0; i < ARRAY_SIZE(s->gpt.irqs); i++) {
        sysbus_connect_irq(sbd, i, qdev_get_gpio_in(DEVICE(&s->uic), 19 + i));
    }

    /* MAL */
    object_property_set_int(OBJECT(&s->mal), "txc-num", 4, &error_abort);
    object_property_set_int(OBJECT(&s->mal), "rxc-num", 2, &error_abort);
    if (!ppc4xx_dcr_realize(PPC4xx_DCR_DEVICE(&s->mal), &s->cpu, errp)) {
        return;
    }
    sbd = SYS_BUS_DEVICE(&s->mal);
    for (i = 0; i < ARRAY_SIZE(s->mal.irqs); i++) {
        sysbus_connect_irq(sbd, i, qdev_get_gpio_in(DEVICE(&s->uic), 11 + i));
    }

    /* Ethernet */
    /* Uses UIC IRQs 9, 15, 17 */
}

static void ppc405_soc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = ppc405_soc_realize;
    /* Reason: only works as part of a ppc405 board/machine */
    dc->user_creatable = false;
}

static const TypeInfo ppc405_types[] = {
    {
        .name           = TYPE_PPC405_POB,
        .parent         = TYPE_PPC4xx_DCR_DEVICE,
        .instance_size  = sizeof(Ppc405PobState),
        .class_init     = ppc405_pob_class_init,
    }, {
        .name           = TYPE_PPC405_OPBA,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(Ppc405OpbaState),
        .class_init     = ppc405_opba_class_init,
    }, {
        .name           = TYPE_PPC405_DMA,
        .parent         = TYPE_PPC4xx_DCR_DEVICE,
        .instance_size  = sizeof(Ppc405DmaState),
        .class_init     = ppc405_dma_class_init,
    }, {
        .name           = TYPE_PPC405_GPIO,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(Ppc405GpioState),
        .class_init     = ppc405_gpio_class_init,
    }, {
        .name           = TYPE_PPC405_OCM,
        .parent         = TYPE_PPC4xx_DCR_DEVICE,
        .instance_size  = sizeof(Ppc405OcmState),
        .class_init     = ppc405_ocm_class_init,
    }, {
        .name           = TYPE_PPC405_GPT,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(Ppc405GptState),
        .instance_finalize = ppc405_gpt_finalize,
        .class_init     = ppc405_gpt_class_init,
    }, {
        .name           = TYPE_PPC405_CPC,
        .parent         = TYPE_PPC4xx_DCR_DEVICE,
        .instance_size  = sizeof(Ppc405CpcState),
        .class_init     = ppc405_cpc_class_init,
    }, {
        .name           = TYPE_PPC405_SOC,
        .parent         = TYPE_DEVICE,
        .instance_size  = sizeof(Ppc405SoCState),
        .instance_init  = ppc405_soc_instance_init,
        .class_init     = ppc405_soc_class_init,
    }
};

DEFINE_TYPES(ppc405_types)
