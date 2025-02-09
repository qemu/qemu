/*
 * QEMU PowerPC 4xx embedded processors shared devices emulation
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
#include "cpu.h"
#include "hw/ppc/ppc4xx.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"

/*****************************************************************************/
/* MAL */

enum {
    MAL0_CFG      = 0x180,
    MAL0_ESR      = 0x181,
    MAL0_IER      = 0x182,
    MAL0_TXCASR   = 0x184,
    MAL0_TXCARR   = 0x185,
    MAL0_TXEOBISR = 0x186,
    MAL0_TXDEIR   = 0x187,
    MAL0_RXCASR   = 0x190,
    MAL0_RXCARR   = 0x191,
    MAL0_RXEOBISR = 0x192,
    MAL0_RXDEIR   = 0x193,
    MAL0_TXCTP0R  = 0x1A0,
    MAL0_RXCTP0R  = 0x1C0,
    MAL0_RCBS0    = 0x1E0,
    MAL0_RCBS1    = 0x1E1,
};

static void ppc4xx_mal_reset(DeviceState *dev)
{
    Ppc4xxMalState *mal = PPC4xx_MAL(dev);

    mal->cfg = 0x0007C000;
    mal->esr = 0x00000000;
    mal->ier = 0x00000000;
    mal->rxcasr = 0x00000000;
    mal->rxdeir = 0x00000000;
    mal->rxeobisr = 0x00000000;
    mal->txcasr = 0x00000000;
    mal->txdeir = 0x00000000;
    mal->txeobisr = 0x00000000;
}

static uint32_t dcr_read_mal(void *opaque, int dcrn)
{
    Ppc4xxMalState *mal = opaque;
    uint32_t ret;

    switch (dcrn) {
    case MAL0_CFG:
        ret = mal->cfg;
        break;
    case MAL0_ESR:
        ret = mal->esr;
        break;
    case MAL0_IER:
        ret = mal->ier;
        break;
    case MAL0_TXCASR:
        ret = mal->txcasr;
        break;
    case MAL0_TXCARR:
        ret = mal->txcarr;
        break;
    case MAL0_TXEOBISR:
        ret = mal->txeobisr;
        break;
    case MAL0_TXDEIR:
        ret = mal->txdeir;
        break;
    case MAL0_RXCASR:
        ret = mal->rxcasr;
        break;
    case MAL0_RXCARR:
        ret = mal->rxcarr;
        break;
    case MAL0_RXEOBISR:
        ret = mal->rxeobisr;
        break;
    case MAL0_RXDEIR:
        ret = mal->rxdeir;
        break;
    default:
        ret = 0;
        break;
    }
    if (dcrn >= MAL0_TXCTP0R && dcrn < MAL0_TXCTP0R + mal->txcnum) {
        ret = mal->txctpr[dcrn - MAL0_TXCTP0R];
    }
    if (dcrn >= MAL0_RXCTP0R && dcrn < MAL0_RXCTP0R + mal->rxcnum) {
        ret = mal->rxctpr[dcrn - MAL0_RXCTP0R];
    }
    if (dcrn >= MAL0_RCBS0 && dcrn < MAL0_RCBS0 + mal->rxcnum) {
        ret = mal->rcbs[dcrn - MAL0_RCBS0];
    }

    return ret;
}

static void dcr_write_mal(void *opaque, int dcrn, uint32_t val)
{
    Ppc4xxMalState *mal = opaque;

    switch (dcrn) {
    case MAL0_CFG:
        if (val & 0x80000000) {
            ppc4xx_mal_reset(DEVICE(mal));
        }
        mal->cfg = val & 0x00FFC087;
        break;
    case MAL0_ESR:
        /* Read/clear */
        mal->esr &= ~val;
        break;
    case MAL0_IER:
        mal->ier = val & 0x0000001F;
        break;
    case MAL0_TXCASR:
        mal->txcasr = val & 0xF0000000;
        break;
    case MAL0_TXCARR:
        mal->txcarr = val & 0xF0000000;
        break;
    case MAL0_TXEOBISR:
        /* Read/clear */
        mal->txeobisr &= ~val;
        break;
    case MAL0_TXDEIR:
        /* Read/clear */
        mal->txdeir &= ~val;
        break;
    case MAL0_RXCASR:
        mal->rxcasr = val & 0xC0000000;
        break;
    case MAL0_RXCARR:
        mal->rxcarr = val & 0xC0000000;
        break;
    case MAL0_RXEOBISR:
        /* Read/clear */
        mal->rxeobisr &= ~val;
        break;
    case MAL0_RXDEIR:
        /* Read/clear */
        mal->rxdeir &= ~val;
        break;
    }
    if (dcrn >= MAL0_TXCTP0R && dcrn < MAL0_TXCTP0R + mal->txcnum) {
        mal->txctpr[dcrn - MAL0_TXCTP0R] = val;
    }
    if (dcrn >= MAL0_RXCTP0R && dcrn < MAL0_RXCTP0R + mal->rxcnum) {
        mal->rxctpr[dcrn - MAL0_RXCTP0R] = val;
    }
    if (dcrn >= MAL0_RCBS0 && dcrn < MAL0_RCBS0 + mal->rxcnum) {
        mal->rcbs[dcrn - MAL0_RCBS0] = val & 0x000000FF;
    }
}

static void ppc4xx_mal_realize(DeviceState *dev, Error **errp)
{
    Ppc4xxMalState *mal = PPC4xx_MAL(dev);
    Ppc4xxDcrDeviceState *dcr = PPC4xx_DCR_DEVICE(dev);
    int i;

    if (mal->txcnum > 32 || mal->rxcnum > 32) {
        error_setg(errp, "invalid TXC/RXC number");
        return;
    }

    mal->txctpr = g_new0(uint32_t, mal->txcnum);
    mal->rxctpr = g_new0(uint32_t, mal->rxcnum);
    mal->rcbs = g_new0(uint32_t, mal->rxcnum);

    for (i = 0; i < ARRAY_SIZE(mal->irqs); i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(dev), &mal->irqs[i]);
    }

    ppc4xx_dcr_register(dcr, MAL0_CFG, mal, &dcr_read_mal, &dcr_write_mal);
    ppc4xx_dcr_register(dcr, MAL0_ESR, mal, &dcr_read_mal, &dcr_write_mal);
    ppc4xx_dcr_register(dcr, MAL0_IER, mal, &dcr_read_mal, &dcr_write_mal);
    ppc4xx_dcr_register(dcr, MAL0_TXCASR, mal, &dcr_read_mal, &dcr_write_mal);
    ppc4xx_dcr_register(dcr, MAL0_TXCARR, mal, &dcr_read_mal, &dcr_write_mal);
    ppc4xx_dcr_register(dcr, MAL0_TXEOBISR, mal, &dcr_read_mal, &dcr_write_mal);
    ppc4xx_dcr_register(dcr, MAL0_TXDEIR, mal, &dcr_read_mal, &dcr_write_mal);
    ppc4xx_dcr_register(dcr, MAL0_RXCASR, mal, &dcr_read_mal, &dcr_write_mal);
    ppc4xx_dcr_register(dcr, MAL0_RXCARR, mal, &dcr_read_mal, &dcr_write_mal);
    ppc4xx_dcr_register(dcr, MAL0_RXEOBISR, mal, &dcr_read_mal, &dcr_write_mal);
    ppc4xx_dcr_register(dcr, MAL0_RXDEIR, mal, &dcr_read_mal, &dcr_write_mal);
    for (i = 0; i < mal->txcnum; i++) {
        ppc4xx_dcr_register(dcr, MAL0_TXCTP0R + i,
                            mal, &dcr_read_mal, &dcr_write_mal);
    }
    for (i = 0; i < mal->rxcnum; i++) {
        ppc4xx_dcr_register(dcr, MAL0_RXCTP0R + i,
                            mal, &dcr_read_mal, &dcr_write_mal);
    }
    for (i = 0; i < mal->rxcnum; i++) {
        ppc4xx_dcr_register(dcr, MAL0_RCBS0 + i,
                            mal, &dcr_read_mal, &dcr_write_mal);
    }
}

static void ppc4xx_mal_finalize(Object *obj)
{
    Ppc4xxMalState *mal = PPC4xx_MAL(obj);

    g_free(mal->rcbs);
    g_free(mal->rxctpr);
    g_free(mal->txctpr);
}

static const Property ppc4xx_mal_properties[] = {
    DEFINE_PROP_UINT8("txc-num", Ppc4xxMalState, txcnum, 0),
    DEFINE_PROP_UINT8("rxc-num", Ppc4xxMalState, rxcnum, 0),
};

static void ppc4xx_mal_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = ppc4xx_mal_realize;
    device_class_set_legacy_reset(dc, ppc4xx_mal_reset);
    /* Reason: only works as function of a ppc4xx SoC */
    dc->user_creatable = false;
    device_class_set_props(dc, ppc4xx_mal_properties);
}

/*****************************************************************************/
/* Peripheral local bus arbitrer */
enum {
    PLB3A0_ACR = 0x077,
    PLB4A0_ACR = 0x081,
    PLB0_BESR  = 0x084,
    PLB0_BEAR  = 0x086,
    PLB0_ACR   = 0x087,
    PLB4A1_ACR = 0x089,
};

static uint32_t dcr_read_plb(void *opaque, int dcrn)
{
    Ppc4xxPlbState *plb = opaque;
    uint32_t ret;

    switch (dcrn) {
    case PLB0_ACR:
        ret = plb->acr;
        break;
    case PLB0_BEAR:
        ret = plb->bear;
        break;
    case PLB0_BESR:
        ret = plb->besr;
        break;
    default:
        /* Avoid gcc warning */
        ret = 0;
        break;
    }

    return ret;
}

static void dcr_write_plb(void *opaque, int dcrn, uint32_t val)
{
    Ppc4xxPlbState *plb = opaque;

    switch (dcrn) {
    case PLB0_ACR:
        /*
         * We don't care about the actual parameters written as
         * we don't manage any priorities on the bus
         */
        plb->acr = val & 0xF8000000;
        break;
    case PLB0_BEAR:
        /* Read only */
        break;
    case PLB0_BESR:
        /* Write-clear */
        plb->besr &= ~val;
        break;
    }
}

static void ppc405_plb_reset(DeviceState *dev)
{
    Ppc4xxPlbState *plb = PPC4xx_PLB(dev);

    plb->acr = 0x00000000;
    plb->bear = 0x00000000;
    plb->besr = 0x00000000;
}

static void ppc405_plb_realize(DeviceState *dev, Error **errp)
{
    Ppc4xxPlbState *plb = PPC4xx_PLB(dev);
    Ppc4xxDcrDeviceState *dcr = PPC4xx_DCR_DEVICE(dev);

    ppc4xx_dcr_register(dcr, PLB3A0_ACR, plb, &dcr_read_plb, &dcr_write_plb);
    ppc4xx_dcr_register(dcr, PLB4A0_ACR, plb, &dcr_read_plb, &dcr_write_plb);
    ppc4xx_dcr_register(dcr, PLB0_ACR, plb, &dcr_read_plb, &dcr_write_plb);
    ppc4xx_dcr_register(dcr, PLB0_BEAR, plb, &dcr_read_plb, &dcr_write_plb);
    ppc4xx_dcr_register(dcr, PLB0_BESR, plb, &dcr_read_plb, &dcr_write_plb);
    ppc4xx_dcr_register(dcr, PLB4A1_ACR, plb, &dcr_read_plb, &dcr_write_plb);
}

static void ppc405_plb_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = ppc405_plb_realize;
    device_class_set_legacy_reset(dc, ppc405_plb_reset);
    /* Reason: only works as function of a ppc4xx SoC */
    dc->user_creatable = false;
}

/*****************************************************************************/
/* Peripheral controller */
enum {
    EBC0_CFGADDR = 0x012,
    EBC0_CFGDATA = 0x013,
};

static uint32_t dcr_read_ebc(void *opaque, int dcrn)
{
    Ppc4xxEbcState *ebc = opaque;
    uint32_t ret;

    switch (dcrn) {
    case EBC0_CFGADDR:
        ret = ebc->addr;
        break;
    case EBC0_CFGDATA:
        switch (ebc->addr) {
        case 0x00: /* B0CR */
            ret = ebc->bcr[0];
            break;
        case 0x01: /* B1CR */
            ret = ebc->bcr[1];
            break;
        case 0x02: /* B2CR */
            ret = ebc->bcr[2];
            break;
        case 0x03: /* B3CR */
            ret = ebc->bcr[3];
            break;
        case 0x04: /* B4CR */
            ret = ebc->bcr[4];
            break;
        case 0x05: /* B5CR */
            ret = ebc->bcr[5];
            break;
        case 0x06: /* B6CR */
            ret = ebc->bcr[6];
            break;
        case 0x07: /* B7CR */
            ret = ebc->bcr[7];
            break;
        case 0x10: /* B0AP */
            ret = ebc->bap[0];
            break;
        case 0x11: /* B1AP */
            ret = ebc->bap[1];
            break;
        case 0x12: /* B2AP */
            ret = ebc->bap[2];
            break;
        case 0x13: /* B3AP */
            ret = ebc->bap[3];
            break;
        case 0x14: /* B4AP */
            ret = ebc->bap[4];
            break;
        case 0x15: /* B5AP */
            ret = ebc->bap[5];
            break;
        case 0x16: /* B6AP */
            ret = ebc->bap[6];
            break;
        case 0x17: /* B7AP */
            ret = ebc->bap[7];
            break;
        case 0x20: /* BEAR */
            ret = ebc->bear;
            break;
        case 0x21: /* BESR0 */
            ret = ebc->besr0;
            break;
        case 0x22: /* BESR1 */
            ret = ebc->besr1;
            break;
        case 0x23: /* CFG */
            ret = ebc->cfg;
            break;
        default:
            ret = 0x00000000;
            break;
        }
        break;
    default:
        ret = 0x00000000;
        break;
    }

    return ret;
}

static void dcr_write_ebc(void *opaque, int dcrn, uint32_t val)
{
    Ppc4xxEbcState *ebc = opaque;

    switch (dcrn) {
    case EBC0_CFGADDR:
        ebc->addr = val;
        break;
    case EBC0_CFGDATA:
        switch (ebc->addr) {
        case 0x00: /* B0CR */
            break;
        case 0x01: /* B1CR */
            break;
        case 0x02: /* B2CR */
            break;
        case 0x03: /* B3CR */
            break;
        case 0x04: /* B4CR */
            break;
        case 0x05: /* B5CR */
            break;
        case 0x06: /* B6CR */
            break;
        case 0x07: /* B7CR */
            break;
        case 0x10: /* B0AP */
            break;
        case 0x11: /* B1AP */
            break;
        case 0x12: /* B2AP */
            break;
        case 0x13: /* B3AP */
            break;
        case 0x14: /* B4AP */
            break;
        case 0x15: /* B5AP */
            break;
        case 0x16: /* B6AP */
            break;
        case 0x17: /* B7AP */
            break;
        case 0x20: /* BEAR */
            break;
        case 0x21: /* BESR0 */
            break;
        case 0x22: /* BESR1 */
            break;
        case 0x23: /* CFG */
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
}

static void ppc405_ebc_reset(DeviceState *dev)
{
    Ppc4xxEbcState *ebc = PPC4xx_EBC(dev);
    int i;

    ebc->addr = 0x00000000;
    ebc->bap[0] = 0x7F8FFE80;
    ebc->bcr[0] = 0xFFE28000;
    for (i = 0; i < 8; i++) {
        ebc->bap[i] = 0x00000000;
        ebc->bcr[i] = 0x00000000;
    }
    ebc->besr0 = 0x00000000;
    ebc->besr1 = 0x00000000;
    ebc->cfg = 0x80400000;
}

static void ppc405_ebc_realize(DeviceState *dev, Error **errp)
{
    Ppc4xxEbcState *ebc = PPC4xx_EBC(dev);
    Ppc4xxDcrDeviceState *dcr = PPC4xx_DCR_DEVICE(dev);

    ppc4xx_dcr_register(dcr, EBC0_CFGADDR, ebc, &dcr_read_ebc, &dcr_write_ebc);
    ppc4xx_dcr_register(dcr, EBC0_CFGDATA, ebc, &dcr_read_ebc, &dcr_write_ebc);
}

static void ppc405_ebc_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = ppc405_ebc_realize;
    device_class_set_legacy_reset(dc, ppc405_ebc_reset);
    /* Reason: only works as function of a ppc4xx SoC */
    dc->user_creatable = false;
}

/* PPC4xx_DCR_DEVICE */

void ppc4xx_dcr_register(Ppc4xxDcrDeviceState *dev, int dcrn, void *opaque,
                         dcr_read_cb dcr_read, dcr_write_cb dcr_write)
{
    assert(dev->cpu);
    ppc_dcr_register(&dev->cpu->env, dcrn, opaque, dcr_read, dcr_write);
}

bool ppc4xx_dcr_realize(Ppc4xxDcrDeviceState *dev, PowerPCCPU *cpu,
                        Error **errp)
{
    object_property_set_link(OBJECT(dev), "cpu", OBJECT(cpu), &error_abort);
    return sysbus_realize(SYS_BUS_DEVICE(dev), errp);
}

static const Property ppc4xx_dcr_properties[] = {
    DEFINE_PROP_LINK("cpu", Ppc4xxDcrDeviceState, cpu, TYPE_POWERPC_CPU,
                     PowerPCCPU *),
};

static void ppc4xx_dcr_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_props(dc, ppc4xx_dcr_properties);
}

static const TypeInfo ppc4xx_types[] = {
    {
        .name           = TYPE_PPC4xx_MAL,
        .parent         = TYPE_PPC4xx_DCR_DEVICE,
        .instance_size  = sizeof(Ppc4xxMalState),
        .instance_finalize = ppc4xx_mal_finalize,
        .class_init     = ppc4xx_mal_class_init,
    }, {
        .name           = TYPE_PPC4xx_PLB,
        .parent         = TYPE_PPC4xx_DCR_DEVICE,
        .instance_size  = sizeof(Ppc4xxPlbState),
        .class_init     = ppc405_plb_class_init,
    }, {
        .name           = TYPE_PPC4xx_EBC,
        .parent         = TYPE_PPC4xx_DCR_DEVICE,
        .instance_size  = sizeof(Ppc4xxEbcState),
        .class_init     = ppc405_ebc_class_init,
    }, {
        .name           = TYPE_PPC4xx_DCR_DEVICE,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(Ppc4xxDcrDeviceState),
        .class_init     = ppc4xx_dcr_class_init,
        .abstract       = true,
    }
};

DEFINE_TYPES(ppc4xx_types)
