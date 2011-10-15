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
#include "hw.h"
#include "ppc.h"
#include "ppc405.h"
#include "pc.h"
#include "qemu-timer.h"
#include "sysemu.h"
#include "qemu-log.h"
#include "exec-memory.h"

#define DEBUG_OPBA
#define DEBUG_SDRAM
#define DEBUG_GPIO
#define DEBUG_SERIAL
#define DEBUG_OCM
//#define DEBUG_I2C
#define DEBUG_GPT
#define DEBUG_MAL
#define DEBUG_CLOCKS
//#define DEBUG_CLOCKS_LL

ram_addr_t ppc405_set_bootinfo (CPUState *env, ppc4xx_bd_info_t *bd,
                                uint32_t flags)
{
    ram_addr_t bdloc;
    int i, n;

    /* We put the bd structure at the top of memory */
    if (bd->bi_memsize >= 0x01000000UL)
        bdloc = 0x01000000UL - sizeof(struct ppc4xx_bd_info_t);
    else
        bdloc = bd->bi_memsize - sizeof(struct ppc4xx_bd_info_t);
    stl_be_phys(bdloc + 0x00, bd->bi_memstart);
    stl_be_phys(bdloc + 0x04, bd->bi_memsize);
    stl_be_phys(bdloc + 0x08, bd->bi_flashstart);
    stl_be_phys(bdloc + 0x0C, bd->bi_flashsize);
    stl_be_phys(bdloc + 0x10, bd->bi_flashoffset);
    stl_be_phys(bdloc + 0x14, bd->bi_sramstart);
    stl_be_phys(bdloc + 0x18, bd->bi_sramsize);
    stl_be_phys(bdloc + 0x1C, bd->bi_bootflags);
    stl_be_phys(bdloc + 0x20, bd->bi_ipaddr);
    for (i = 0; i < 6; i++) {
        stb_phys(bdloc + 0x24 + i, bd->bi_enetaddr[i]);
    }
    stw_be_phys(bdloc + 0x2A, bd->bi_ethspeed);
    stl_be_phys(bdloc + 0x2C, bd->bi_intfreq);
    stl_be_phys(bdloc + 0x30, bd->bi_busfreq);
    stl_be_phys(bdloc + 0x34, bd->bi_baudrate);
    for (i = 0; i < 4; i++) {
        stb_phys(bdloc + 0x38 + i, bd->bi_s_version[i]);
    }
    for (i = 0; i < 32; i++) {
        stb_phys(bdloc + 0x3C + i, bd->bi_r_version[i]);
    }
    stl_be_phys(bdloc + 0x5C, bd->bi_plb_busfreq);
    stl_be_phys(bdloc + 0x60, bd->bi_pci_busfreq);
    for (i = 0; i < 6; i++) {
        stb_phys(bdloc + 0x64 + i, bd->bi_pci_enetaddr[i]);
    }
    n = 0x6A;
    if (flags & 0x00000001) {
        for (i = 0; i < 6; i++)
            stb_phys(bdloc + n++, bd->bi_pci_enetaddr2[i]);
    }
    stl_be_phys(bdloc + n, bd->bi_opbfreq);
    n += 4;
    for (i = 0; i < 2; i++) {
        stl_be_phys(bdloc + n, bd->bi_iic_fast[i]);
        n += 4;
    }

    return bdloc;
}

/*****************************************************************************/
/* Shared peripherals */

/*****************************************************************************/
/* Peripheral local bus arbitrer */
enum {
    PLB0_BESR = 0x084,
    PLB0_BEAR = 0x086,
    PLB0_ACR  = 0x087,
};

typedef struct ppc4xx_plb_t ppc4xx_plb_t;
struct ppc4xx_plb_t {
    uint32_t acr;
    uint32_t bear;
    uint32_t besr;
};

static uint32_t dcr_read_plb (void *opaque, int dcrn)
{
    ppc4xx_plb_t *plb;
    uint32_t ret;

    plb = opaque;
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

static void dcr_write_plb (void *opaque, int dcrn, uint32_t val)
{
    ppc4xx_plb_t *plb;

    plb = opaque;
    switch (dcrn) {
    case PLB0_ACR:
        /* We don't care about the actual parameters written as
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

static void ppc4xx_plb_reset (void *opaque)
{
    ppc4xx_plb_t *plb;

    plb = opaque;
    plb->acr = 0x00000000;
    plb->bear = 0x00000000;
    plb->besr = 0x00000000;
}

static void ppc4xx_plb_init(CPUState *env)
{
    ppc4xx_plb_t *plb;

    plb = g_malloc0(sizeof(ppc4xx_plb_t));
    ppc_dcr_register(env, PLB0_ACR, plb, &dcr_read_plb, &dcr_write_plb);
    ppc_dcr_register(env, PLB0_BEAR, plb, &dcr_read_plb, &dcr_write_plb);
    ppc_dcr_register(env, PLB0_BESR, plb, &dcr_read_plb, &dcr_write_plb);
    qemu_register_reset(ppc4xx_plb_reset, plb);
}

/*****************************************************************************/
/* PLB to OPB bridge */
enum {
    POB0_BESR0 = 0x0A0,
    POB0_BESR1 = 0x0A2,
    POB0_BEAR  = 0x0A4,
};

typedef struct ppc4xx_pob_t ppc4xx_pob_t;
struct ppc4xx_pob_t {
    uint32_t bear;
    uint32_t besr[2];
};

static uint32_t dcr_read_pob (void *opaque, int dcrn)
{
    ppc4xx_pob_t *pob;
    uint32_t ret;

    pob = opaque;
    switch (dcrn) {
    case POB0_BEAR:
        ret = pob->bear;
        break;
    case POB0_BESR0:
    case POB0_BESR1:
        ret = pob->besr[dcrn - POB0_BESR0];
        break;
    default:
        /* Avoid gcc warning */
        ret = 0;
        break;
    }

    return ret;
}

static void dcr_write_pob (void *opaque, int dcrn, uint32_t val)
{
    ppc4xx_pob_t *pob;

    pob = opaque;
    switch (dcrn) {
    case POB0_BEAR:
        /* Read only */
        break;
    case POB0_BESR0:
    case POB0_BESR1:
        /* Write-clear */
        pob->besr[dcrn - POB0_BESR0] &= ~val;
        break;
    }
}

static void ppc4xx_pob_reset (void *opaque)
{
    ppc4xx_pob_t *pob;

    pob = opaque;
    /* No error */
    pob->bear = 0x00000000;
    pob->besr[0] = 0x0000000;
    pob->besr[1] = 0x0000000;
}

static void ppc4xx_pob_init(CPUState *env)
{
    ppc4xx_pob_t *pob;

    pob = g_malloc0(sizeof(ppc4xx_pob_t));
    ppc_dcr_register(env, POB0_BEAR, pob, &dcr_read_pob, &dcr_write_pob);
    ppc_dcr_register(env, POB0_BESR0, pob, &dcr_read_pob, &dcr_write_pob);
    ppc_dcr_register(env, POB0_BESR1, pob, &dcr_read_pob, &dcr_write_pob);
    qemu_register_reset(ppc4xx_pob_reset, pob);
}

/*****************************************************************************/
/* OPB arbitrer */
typedef struct ppc4xx_opba_t ppc4xx_opba_t;
struct ppc4xx_opba_t {
    MemoryRegion io;
    uint8_t cr;
    uint8_t pr;
};

static uint32_t opba_readb (void *opaque, target_phys_addr_t addr)
{
    ppc4xx_opba_t *opba;
    uint32_t ret;

#ifdef DEBUG_OPBA
    printf("%s: addr " TARGET_FMT_plx "\n", __func__, addr);
#endif
    opba = opaque;
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

    return ret;
}

static void opba_writeb (void *opaque,
                         target_phys_addr_t addr, uint32_t value)
{
    ppc4xx_opba_t *opba;

#ifdef DEBUG_OPBA
    printf("%s: addr " TARGET_FMT_plx " val %08" PRIx32 "\n", __func__, addr,
           value);
#endif
    opba = opaque;
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

static uint32_t opba_readw (void *opaque, target_phys_addr_t addr)
{
    uint32_t ret;

#ifdef DEBUG_OPBA
    printf("%s: addr " TARGET_FMT_plx "\n", __func__, addr);
#endif
    ret = opba_readb(opaque, addr) << 8;
    ret |= opba_readb(opaque, addr + 1);

    return ret;
}

static void opba_writew (void *opaque,
                         target_phys_addr_t addr, uint32_t value)
{
#ifdef DEBUG_OPBA
    printf("%s: addr " TARGET_FMT_plx " val %08" PRIx32 "\n", __func__, addr,
           value);
#endif
    opba_writeb(opaque, addr, value >> 8);
    opba_writeb(opaque, addr + 1, value);
}

static uint32_t opba_readl (void *opaque, target_phys_addr_t addr)
{
    uint32_t ret;

#ifdef DEBUG_OPBA
    printf("%s: addr " TARGET_FMT_plx "\n", __func__, addr);
#endif
    ret = opba_readb(opaque, addr) << 24;
    ret |= opba_readb(opaque, addr + 1) << 16;

    return ret;
}

static void opba_writel (void *opaque,
                         target_phys_addr_t addr, uint32_t value)
{
#ifdef DEBUG_OPBA
    printf("%s: addr " TARGET_FMT_plx " val %08" PRIx32 "\n", __func__, addr,
           value);
#endif
    opba_writeb(opaque, addr, value >> 24);
    opba_writeb(opaque, addr + 1, value >> 16);
}

static const MemoryRegionOps opba_ops = {
    .old_mmio = {
        .read = { opba_readb, opba_readw, opba_readl, },
        .write = { opba_writeb, opba_writew, opba_writel, },
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ppc4xx_opba_reset (void *opaque)
{
    ppc4xx_opba_t *opba;

    opba = opaque;
    opba->cr = 0x00; /* No dynamic priorities - park disabled */
    opba->pr = 0x11;
}

static void ppc4xx_opba_init(target_phys_addr_t base)
{
    ppc4xx_opba_t *opba;

    opba = g_malloc0(sizeof(ppc4xx_opba_t));
#ifdef DEBUG_OPBA
    printf("%s: offset " TARGET_FMT_plx "\n", __func__, base);
#endif
    memory_region_init_io(&opba->io, &opba_ops, opba, "opba", 0x002);
    memory_region_add_subregion(get_system_memory(), base, &opba->io);
    qemu_register_reset(ppc4xx_opba_reset, opba);
}

/*****************************************************************************/
/* Code decompression controller */
/* XXX: TODO */

/*****************************************************************************/
/* Peripheral controller */
typedef struct ppc4xx_ebc_t ppc4xx_ebc_t;
struct ppc4xx_ebc_t {
    uint32_t addr;
    uint32_t bcr[8];
    uint32_t bap[8];
    uint32_t bear;
    uint32_t besr0;
    uint32_t besr1;
    uint32_t cfg;
};

enum {
    EBC0_CFGADDR = 0x012,
    EBC0_CFGDATA = 0x013,
};

static uint32_t dcr_read_ebc (void *opaque, int dcrn)
{
    ppc4xx_ebc_t *ebc;
    uint32_t ret;

    ebc = opaque;
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

static void dcr_write_ebc (void *opaque, int dcrn, uint32_t val)
{
    ppc4xx_ebc_t *ebc;

    ebc = opaque;
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

static void ebc_reset (void *opaque)
{
    ppc4xx_ebc_t *ebc;
    int i;

    ebc = opaque;
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

static void ppc405_ebc_init(CPUState *env)
{
    ppc4xx_ebc_t *ebc;

    ebc = g_malloc0(sizeof(ppc4xx_ebc_t));
    qemu_register_reset(&ebc_reset, ebc);
    ppc_dcr_register(env, EBC0_CFGADDR,
                     ebc, &dcr_read_ebc, &dcr_write_ebc);
    ppc_dcr_register(env, EBC0_CFGDATA,
                     ebc, &dcr_read_ebc, &dcr_write_ebc);
}

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

typedef struct ppc405_dma_t ppc405_dma_t;
struct ppc405_dma_t {
    qemu_irq irqs[4];
    uint32_t cr[4];
    uint32_t ct[4];
    uint32_t da[4];
    uint32_t sa[4];
    uint32_t sg[4];
    uint32_t sr;
    uint32_t sgc;
    uint32_t slp;
    uint32_t pol;
};

static uint32_t dcr_read_dma (void *opaque, int dcrn)
{
    return 0;
}

static void dcr_write_dma (void *opaque, int dcrn, uint32_t val)
{
}

static void ppc405_dma_reset (void *opaque)
{
    ppc405_dma_t *dma;
    int i;

    dma = opaque;
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

static void ppc405_dma_init(CPUState *env, qemu_irq irqs[4])
{
    ppc405_dma_t *dma;

    dma = g_malloc0(sizeof(ppc405_dma_t));
    memcpy(dma->irqs, irqs, 4 * sizeof(qemu_irq));
    qemu_register_reset(&ppc405_dma_reset, dma);
    ppc_dcr_register(env, DMA0_CR0,
                     dma, &dcr_read_dma, &dcr_write_dma);
    ppc_dcr_register(env, DMA0_CT0,
                     dma, &dcr_read_dma, &dcr_write_dma);
    ppc_dcr_register(env, DMA0_DA0,
                     dma, &dcr_read_dma, &dcr_write_dma);
    ppc_dcr_register(env, DMA0_SA0,
                     dma, &dcr_read_dma, &dcr_write_dma);
    ppc_dcr_register(env, DMA0_SG0,
                     dma, &dcr_read_dma, &dcr_write_dma);
    ppc_dcr_register(env, DMA0_CR1,
                     dma, &dcr_read_dma, &dcr_write_dma);
    ppc_dcr_register(env, DMA0_CT1,
                     dma, &dcr_read_dma, &dcr_write_dma);
    ppc_dcr_register(env, DMA0_DA1,
                     dma, &dcr_read_dma, &dcr_write_dma);
    ppc_dcr_register(env, DMA0_SA1,
                     dma, &dcr_read_dma, &dcr_write_dma);
    ppc_dcr_register(env, DMA0_SG1,
                     dma, &dcr_read_dma, &dcr_write_dma);
    ppc_dcr_register(env, DMA0_CR2,
                     dma, &dcr_read_dma, &dcr_write_dma);
    ppc_dcr_register(env, DMA0_CT2,
                     dma, &dcr_read_dma, &dcr_write_dma);
    ppc_dcr_register(env, DMA0_DA2,
                     dma, &dcr_read_dma, &dcr_write_dma);
    ppc_dcr_register(env, DMA0_SA2,
                     dma, &dcr_read_dma, &dcr_write_dma);
    ppc_dcr_register(env, DMA0_SG2,
                     dma, &dcr_read_dma, &dcr_write_dma);
    ppc_dcr_register(env, DMA0_CR3,
                     dma, &dcr_read_dma, &dcr_write_dma);
    ppc_dcr_register(env, DMA0_CT3,
                     dma, &dcr_read_dma, &dcr_write_dma);
    ppc_dcr_register(env, DMA0_DA3,
                     dma, &dcr_read_dma, &dcr_write_dma);
    ppc_dcr_register(env, DMA0_SA3,
                     dma, &dcr_read_dma, &dcr_write_dma);
    ppc_dcr_register(env, DMA0_SG3,
                     dma, &dcr_read_dma, &dcr_write_dma);
    ppc_dcr_register(env, DMA0_SR,
                     dma, &dcr_read_dma, &dcr_write_dma);
    ppc_dcr_register(env, DMA0_SGC,
                     dma, &dcr_read_dma, &dcr_write_dma);
    ppc_dcr_register(env, DMA0_SLP,
                     dma, &dcr_read_dma, &dcr_write_dma);
    ppc_dcr_register(env, DMA0_POL,
                     dma, &dcr_read_dma, &dcr_write_dma);
}

/*****************************************************************************/
/* GPIO */
typedef struct ppc405_gpio_t ppc405_gpio_t;
struct ppc405_gpio_t {
    MemoryRegion io;
    uint32_t or;
    uint32_t tcr;
    uint32_t osrh;
    uint32_t osrl;
    uint32_t tsrh;
    uint32_t tsrl;
    uint32_t odr;
    uint32_t ir;
    uint32_t rr1;
    uint32_t isr1h;
    uint32_t isr1l;
};

static uint32_t ppc405_gpio_readb (void *opaque, target_phys_addr_t addr)
{
#ifdef DEBUG_GPIO
    printf("%s: addr " TARGET_FMT_plx "\n", __func__, addr);
#endif

    return 0;
}

static void ppc405_gpio_writeb (void *opaque,
                                target_phys_addr_t addr, uint32_t value)
{
#ifdef DEBUG_GPIO
    printf("%s: addr " TARGET_FMT_plx " val %08" PRIx32 "\n", __func__, addr,
           value);
#endif
}

static uint32_t ppc405_gpio_readw (void *opaque, target_phys_addr_t addr)
{
#ifdef DEBUG_GPIO
    printf("%s: addr " TARGET_FMT_plx "\n", __func__, addr);
#endif

    return 0;
}

static void ppc405_gpio_writew (void *opaque,
                                target_phys_addr_t addr, uint32_t value)
{
#ifdef DEBUG_GPIO
    printf("%s: addr " TARGET_FMT_plx " val %08" PRIx32 "\n", __func__, addr,
           value);
#endif
}

static uint32_t ppc405_gpio_readl (void *opaque, target_phys_addr_t addr)
{
#ifdef DEBUG_GPIO
    printf("%s: addr " TARGET_FMT_plx "\n", __func__, addr);
#endif

    return 0;
}

static void ppc405_gpio_writel (void *opaque,
                                target_phys_addr_t addr, uint32_t value)
{
#ifdef DEBUG_GPIO
    printf("%s: addr " TARGET_FMT_plx " val %08" PRIx32 "\n", __func__, addr,
           value);
#endif
}

static const MemoryRegionOps ppc405_gpio_ops = {
    .old_mmio = {
        .read = { ppc405_gpio_readb, ppc405_gpio_readw, ppc405_gpio_readl, },
        .write = { ppc405_gpio_writeb, ppc405_gpio_writew, ppc405_gpio_writel, },
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ppc405_gpio_reset (void *opaque)
{
}

static void ppc405_gpio_init(target_phys_addr_t base)
{
    ppc405_gpio_t *gpio;

    gpio = g_malloc0(sizeof(ppc405_gpio_t));
#ifdef DEBUG_GPIO
    printf("%s: offset " TARGET_FMT_plx "\n", __func__, base);
#endif
    memory_region_init_io(&gpio->io, &ppc405_gpio_ops, gpio, "pgio", 0x038);
    memory_region_add_subregion(get_system_memory(), base, &gpio->io);
    qemu_register_reset(&ppc405_gpio_reset, gpio);
}

/*****************************************************************************/
/* On Chip Memory */
enum {
    OCM0_ISARC   = 0x018,
    OCM0_ISACNTL = 0x019,
    OCM0_DSARC   = 0x01A,
    OCM0_DSACNTL = 0x01B,
};

typedef struct ppc405_ocm_t ppc405_ocm_t;
struct ppc405_ocm_t {
    MemoryRegion ram;
    MemoryRegion isarc_ram;
    MemoryRegion dsarc_ram;
    uint32_t isarc;
    uint32_t isacntl;
    uint32_t dsarc;
    uint32_t dsacntl;
};

static void ocm_update_mappings (ppc405_ocm_t *ocm,
                                 uint32_t isarc, uint32_t isacntl,
                                 uint32_t dsarc, uint32_t dsacntl)
{
#ifdef DEBUG_OCM
    printf("OCM update ISA %08" PRIx32 " %08" PRIx32 " (%08" PRIx32
           " %08" PRIx32 ") DSA %08" PRIx32 " %08" PRIx32
           " (%08" PRIx32 " %08" PRIx32 ")\n",
           isarc, isacntl, dsarc, dsacntl,
           ocm->isarc, ocm->isacntl, ocm->dsarc, ocm->dsacntl);
#endif
    if (ocm->isarc != isarc ||
        (ocm->isacntl & 0x80000000) != (isacntl & 0x80000000)) {
        if (ocm->isacntl & 0x80000000) {
            /* Unmap previously assigned memory region */
            printf("OCM unmap ISA %08" PRIx32 "\n", ocm->isarc);
            memory_region_del_subregion(get_system_memory(), &ocm->isarc_ram);
        }
        if (isacntl & 0x80000000) {
            /* Map new instruction memory region */
#ifdef DEBUG_OCM
            printf("OCM map ISA %08" PRIx32 "\n", isarc);
#endif
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
#ifdef DEBUG_OCM
                printf("OCM unmap DSA %08" PRIx32 "\n", ocm->dsarc);
#endif
                memory_region_del_subregion(get_system_memory(),
                                            &ocm->dsarc_ram);
            }
        }
        if (dsacntl & 0x80000000) {
            /* Beware not to remap the region we just mapped */
            if (!(isacntl & 0x80000000) || dsarc != isarc) {
                /* Map new data memory region */
#ifdef DEBUG_OCM
                printf("OCM map DSA %08" PRIx32 "\n", dsarc);
#endif
                memory_region_add_subregion(get_system_memory(), dsarc,
                                            &ocm->dsarc_ram);
            }
        }
    }
}

static uint32_t dcr_read_ocm (void *opaque, int dcrn)
{
    ppc405_ocm_t *ocm;
    uint32_t ret;

    ocm = opaque;
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

static void dcr_write_ocm (void *opaque, int dcrn, uint32_t val)
{
    ppc405_ocm_t *ocm;
    uint32_t isarc, dsarc, isacntl, dsacntl;

    ocm = opaque;
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

static void ocm_reset (void *opaque)
{
    ppc405_ocm_t *ocm;
    uint32_t isarc, dsarc, isacntl, dsacntl;

    ocm = opaque;
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

static void ppc405_ocm_init(CPUState *env)
{
    ppc405_ocm_t *ocm;

    ocm = g_malloc0(sizeof(ppc405_ocm_t));
    /* XXX: Size is 4096 or 0x04000000 */
    memory_region_init_ram(&ocm->isarc_ram, NULL, "ppc405.ocm", 4096);
    memory_region_init_alias(&ocm->dsarc_ram, "ppc405.dsarc", &ocm->isarc_ram,
                             0, 4096);
    qemu_register_reset(&ocm_reset, ocm);
    ppc_dcr_register(env, OCM0_ISARC,
                     ocm, &dcr_read_ocm, &dcr_write_ocm);
    ppc_dcr_register(env, OCM0_ISACNTL,
                     ocm, &dcr_read_ocm, &dcr_write_ocm);
    ppc_dcr_register(env, OCM0_DSARC,
                     ocm, &dcr_read_ocm, &dcr_write_ocm);
    ppc_dcr_register(env, OCM0_DSACNTL,
                     ocm, &dcr_read_ocm, &dcr_write_ocm);
}

/*****************************************************************************/
/* I2C controller */
typedef struct ppc4xx_i2c_t ppc4xx_i2c_t;
struct ppc4xx_i2c_t {
    qemu_irq irq;
    MemoryRegion iomem;
    uint8_t mdata;
    uint8_t lmadr;
    uint8_t hmadr;
    uint8_t cntl;
    uint8_t mdcntl;
    uint8_t sts;
    uint8_t extsts;
    uint8_t sdata;
    uint8_t lsadr;
    uint8_t hsadr;
    uint8_t clkdiv;
    uint8_t intrmsk;
    uint8_t xfrcnt;
    uint8_t xtcntlss;
    uint8_t directcntl;
};

static uint32_t ppc4xx_i2c_readb (void *opaque, target_phys_addr_t addr)
{
    ppc4xx_i2c_t *i2c;
    uint32_t ret;

#ifdef DEBUG_I2C
    printf("%s: addr " TARGET_FMT_plx "\n", __func__, addr);
#endif
    i2c = opaque;
    switch (addr) {
    case 0x00:
        //        i2c_readbyte(&i2c->mdata);
        ret = i2c->mdata;
        break;
    case 0x02:
        ret = i2c->sdata;
        break;
    case 0x04:
        ret = i2c->lmadr;
        break;
    case 0x05:
        ret = i2c->hmadr;
        break;
    case 0x06:
        ret = i2c->cntl;
        break;
    case 0x07:
        ret = i2c->mdcntl;
        break;
    case 0x08:
        ret = i2c->sts;
        break;
    case 0x09:
        ret = i2c->extsts;
        break;
    case 0x0A:
        ret = i2c->lsadr;
        break;
    case 0x0B:
        ret = i2c->hsadr;
        break;
    case 0x0C:
        ret = i2c->clkdiv;
        break;
    case 0x0D:
        ret = i2c->intrmsk;
        break;
    case 0x0E:
        ret = i2c->xfrcnt;
        break;
    case 0x0F:
        ret = i2c->xtcntlss;
        break;
    case 0x10:
        ret = i2c->directcntl;
        break;
    default:
        ret = 0x00;
        break;
    }
#ifdef DEBUG_I2C
    printf("%s: addr " TARGET_FMT_plx " %02" PRIx32 "\n", __func__, addr, ret);
#endif

    return ret;
}

static void ppc4xx_i2c_writeb (void *opaque,
                               target_phys_addr_t addr, uint32_t value)
{
    ppc4xx_i2c_t *i2c;

#ifdef DEBUG_I2C
    printf("%s: addr " TARGET_FMT_plx " val %08" PRIx32 "\n", __func__, addr,
           value);
#endif
    i2c = opaque;
    switch (addr) {
    case 0x00:
        i2c->mdata = value;
        //        i2c_sendbyte(&i2c->mdata);
        break;
    case 0x02:
        i2c->sdata = value;
        break;
    case 0x04:
        i2c->lmadr = value;
        break;
    case 0x05:
        i2c->hmadr = value;
        break;
    case 0x06:
        i2c->cntl = value;
        break;
    case 0x07:
        i2c->mdcntl = value & 0xDF;
        break;
    case 0x08:
        i2c->sts &= ~(value & 0x0A);
        break;
    case 0x09:
        i2c->extsts &= ~(value & 0x8F);
        break;
    case 0x0A:
        i2c->lsadr = value;
        break;
    case 0x0B:
        i2c->hsadr = value;
        break;
    case 0x0C:
        i2c->clkdiv = value;
        break;
    case 0x0D:
        i2c->intrmsk = value;
        break;
    case 0x0E:
        i2c->xfrcnt = value & 0x77;
        break;
    case 0x0F:
        i2c->xtcntlss = value;
        break;
    case 0x10:
        i2c->directcntl = value & 0x7;
        break;
    }
}

static uint32_t ppc4xx_i2c_readw (void *opaque, target_phys_addr_t addr)
{
    uint32_t ret;

#ifdef DEBUG_I2C
    printf("%s: addr " TARGET_FMT_plx "\n", __func__, addr);
#endif
    ret = ppc4xx_i2c_readb(opaque, addr) << 8;
    ret |= ppc4xx_i2c_readb(opaque, addr + 1);

    return ret;
}

static void ppc4xx_i2c_writew (void *opaque,
                               target_phys_addr_t addr, uint32_t value)
{
#ifdef DEBUG_I2C
    printf("%s: addr " TARGET_FMT_plx " val %08" PRIx32 "\n", __func__, addr,
           value);
#endif
    ppc4xx_i2c_writeb(opaque, addr, value >> 8);
    ppc4xx_i2c_writeb(opaque, addr + 1, value);
}

static uint32_t ppc4xx_i2c_readl (void *opaque, target_phys_addr_t addr)
{
    uint32_t ret;

#ifdef DEBUG_I2C
    printf("%s: addr " TARGET_FMT_plx "\n", __func__, addr);
#endif
    ret = ppc4xx_i2c_readb(opaque, addr) << 24;
    ret |= ppc4xx_i2c_readb(opaque, addr + 1) << 16;
    ret |= ppc4xx_i2c_readb(opaque, addr + 2) << 8;
    ret |= ppc4xx_i2c_readb(opaque, addr + 3);

    return ret;
}

static void ppc4xx_i2c_writel (void *opaque,
                               target_phys_addr_t addr, uint32_t value)
{
#ifdef DEBUG_I2C
    printf("%s: addr " TARGET_FMT_plx " val %08" PRIx32 "\n", __func__, addr,
           value);
#endif
    ppc4xx_i2c_writeb(opaque, addr, value >> 24);
    ppc4xx_i2c_writeb(opaque, addr + 1, value >> 16);
    ppc4xx_i2c_writeb(opaque, addr + 2, value >> 8);
    ppc4xx_i2c_writeb(opaque, addr + 3, value);
}

static const MemoryRegionOps i2c_ops = {
    .old_mmio = {
        .read = { ppc4xx_i2c_readb, ppc4xx_i2c_readw, ppc4xx_i2c_readl, },
        .write = { ppc4xx_i2c_writeb, ppc4xx_i2c_writew, ppc4xx_i2c_writel, },
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ppc4xx_i2c_reset (void *opaque)
{
    ppc4xx_i2c_t *i2c;

    i2c = opaque;
    i2c->mdata = 0x00;
    i2c->sdata = 0x00;
    i2c->cntl = 0x00;
    i2c->mdcntl = 0x00;
    i2c->sts = 0x00;
    i2c->extsts = 0x00;
    i2c->clkdiv = 0x00;
    i2c->xfrcnt = 0x00;
    i2c->directcntl = 0x0F;
}

static void ppc405_i2c_init(target_phys_addr_t base, qemu_irq irq)
{
    ppc4xx_i2c_t *i2c;

    i2c = g_malloc0(sizeof(ppc4xx_i2c_t));
    i2c->irq = irq;
#ifdef DEBUG_I2C
    printf("%s: offset " TARGET_FMT_plx "\n", __func__, base);
#endif
    memory_region_init_io(&i2c->iomem, &i2c_ops, i2c, "i2c", 0x011);
    memory_region_add_subregion(get_system_memory(), base, &i2c->iomem);
    qemu_register_reset(ppc4xx_i2c_reset, i2c);
}

/*****************************************************************************/
/* General purpose timers */
typedef struct ppc4xx_gpt_t ppc4xx_gpt_t;
struct ppc4xx_gpt_t {
    MemoryRegion iomem;
    int64_t tb_offset;
    uint32_t tb_freq;
    struct QEMUTimer *timer;
    qemu_irq irqs[5];
    uint32_t oe;
    uint32_t ol;
    uint32_t im;
    uint32_t is;
    uint32_t ie;
    uint32_t comp[5];
    uint32_t mask[5];
};

static uint32_t ppc4xx_gpt_readb (void *opaque, target_phys_addr_t addr)
{
#ifdef DEBUG_GPT
    printf("%s: addr " TARGET_FMT_plx "\n", __func__, addr);
#endif
    /* XXX: generate a bus fault */
    return -1;
}

static void ppc4xx_gpt_writeb (void *opaque,
                               target_phys_addr_t addr, uint32_t value)
{
#ifdef DEBUG_I2C
    printf("%s: addr " TARGET_FMT_plx " val %08" PRIx32 "\n", __func__, addr,
           value);
#endif
    /* XXX: generate a bus fault */
}

static uint32_t ppc4xx_gpt_readw (void *opaque, target_phys_addr_t addr)
{
#ifdef DEBUG_GPT
    printf("%s: addr " TARGET_FMT_plx "\n", __func__, addr);
#endif
    /* XXX: generate a bus fault */
    return -1;
}

static void ppc4xx_gpt_writew (void *opaque,
                               target_phys_addr_t addr, uint32_t value)
{
#ifdef DEBUG_I2C
    printf("%s: addr " TARGET_FMT_plx " val %08" PRIx32 "\n", __func__, addr,
           value);
#endif
    /* XXX: generate a bus fault */
}

static int ppc4xx_gpt_compare (ppc4xx_gpt_t *gpt, int n)
{
    /* XXX: TODO */
    return 0;
}

static void ppc4xx_gpt_set_output (ppc4xx_gpt_t *gpt, int n, int level)
{
    /* XXX: TODO */
}

static void ppc4xx_gpt_set_outputs (ppc4xx_gpt_t *gpt)
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

static void ppc4xx_gpt_set_irqs (ppc4xx_gpt_t *gpt)
{
    uint32_t mask;
    int i;

    mask = 0x00008000;
    for (i = 0; i < 5; i++) {
        if (gpt->is & gpt->im & mask)
            qemu_irq_raise(gpt->irqs[i]);
        else
            qemu_irq_lower(gpt->irqs[i]);
        mask = mask >> 1;
    }
}

static void ppc4xx_gpt_compute_timer (ppc4xx_gpt_t *gpt)
{
    /* XXX: TODO */
}

static uint32_t ppc4xx_gpt_readl (void *opaque, target_phys_addr_t addr)
{
    ppc4xx_gpt_t *gpt;
    uint32_t ret;
    int idx;

#ifdef DEBUG_GPT
    printf("%s: addr " TARGET_FMT_plx "\n", __func__, addr);
#endif
    gpt = opaque;
    switch (addr) {
    case 0x00:
        /* Time base counter */
        ret = muldiv64(qemu_get_clock_ns(vm_clock) + gpt->tb_offset,
                       gpt->tb_freq, get_ticks_per_sec());
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

static void ppc4xx_gpt_writel (void *opaque,
                               target_phys_addr_t addr, uint32_t value)
{
    ppc4xx_gpt_t *gpt;
    int idx;

#ifdef DEBUG_I2C
    printf("%s: addr " TARGET_FMT_plx " val %08" PRIx32 "\n", __func__, addr,
           value);
#endif
    gpt = opaque;
    switch (addr) {
    case 0x00:
        /* Time base counter */
        gpt->tb_offset = muldiv64(value, get_ticks_per_sec(), gpt->tb_freq)
            - qemu_get_clock_ns(vm_clock);
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
    .old_mmio = {
        .read = { ppc4xx_gpt_readb, ppc4xx_gpt_readw, ppc4xx_gpt_readl, },
        .write = { ppc4xx_gpt_writeb, ppc4xx_gpt_writew, ppc4xx_gpt_writel, },
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ppc4xx_gpt_cb (void *opaque)
{
    ppc4xx_gpt_t *gpt;

    gpt = opaque;
    ppc4xx_gpt_set_irqs(gpt);
    ppc4xx_gpt_set_outputs(gpt);
    ppc4xx_gpt_compute_timer(gpt);
}

static void ppc4xx_gpt_reset (void *opaque)
{
    ppc4xx_gpt_t *gpt;
    int i;

    gpt = opaque;
    qemu_del_timer(gpt->timer);
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

static void ppc4xx_gpt_init(target_phys_addr_t base, qemu_irq irqs[5])
{
    ppc4xx_gpt_t *gpt;
    int i;

    gpt = g_malloc0(sizeof(ppc4xx_gpt_t));
    for (i = 0; i < 5; i++) {
        gpt->irqs[i] = irqs[i];
    }
    gpt->timer = qemu_new_timer_ns(vm_clock, &ppc4xx_gpt_cb, gpt);
#ifdef DEBUG_GPT
    printf("%s: offset " TARGET_FMT_plx "\n", __func__, base);
#endif
    memory_region_init_io(&gpt->iomem, &gpt_ops, gpt, "gpt", 0x0d4);
    memory_region_add_subregion(get_system_memory(), base, &gpt->iomem);
    qemu_register_reset(ppc4xx_gpt_reset, gpt);
}

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
    MAL0_TXCTP1R  = 0x1A1,
    MAL0_TXCTP2R  = 0x1A2,
    MAL0_TXCTP3R  = 0x1A3,
    MAL0_RXCTP0R  = 0x1C0,
    MAL0_RXCTP1R  = 0x1C1,
    MAL0_RCBS0    = 0x1E0,
    MAL0_RCBS1    = 0x1E1,
};

typedef struct ppc40x_mal_t ppc40x_mal_t;
struct ppc40x_mal_t {
    qemu_irq irqs[4];
    uint32_t cfg;
    uint32_t esr;
    uint32_t ier;
    uint32_t txcasr;
    uint32_t txcarr;
    uint32_t txeobisr;
    uint32_t txdeir;
    uint32_t rxcasr;
    uint32_t rxcarr;
    uint32_t rxeobisr;
    uint32_t rxdeir;
    uint32_t txctpr[4];
    uint32_t rxctpr[2];
    uint32_t rcbs[2];
};

static void ppc40x_mal_reset (void *opaque);

static uint32_t dcr_read_mal (void *opaque, int dcrn)
{
    ppc40x_mal_t *mal;
    uint32_t ret;

    mal = opaque;
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
    case MAL0_TXCTP0R:
        ret = mal->txctpr[0];
        break;
    case MAL0_TXCTP1R:
        ret = mal->txctpr[1];
        break;
    case MAL0_TXCTP2R:
        ret = mal->txctpr[2];
        break;
    case MAL0_TXCTP3R:
        ret = mal->txctpr[3];
        break;
    case MAL0_RXCTP0R:
        ret = mal->rxctpr[0];
        break;
    case MAL0_RXCTP1R:
        ret = mal->rxctpr[1];
        break;
    case MAL0_RCBS0:
        ret = mal->rcbs[0];
        break;
    case MAL0_RCBS1:
        ret = mal->rcbs[1];
        break;
    default:
        ret = 0;
        break;
    }

    return ret;
}

static void dcr_write_mal (void *opaque, int dcrn, uint32_t val)
{
    ppc40x_mal_t *mal;
    int idx;

    mal = opaque;
    switch (dcrn) {
    case MAL0_CFG:
        if (val & 0x80000000)
            ppc40x_mal_reset(mal);
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
    case MAL0_TXCTP0R:
        idx = 0;
        goto update_tx_ptr;
    case MAL0_TXCTP1R:
        idx = 1;
        goto update_tx_ptr;
    case MAL0_TXCTP2R:
        idx = 2;
        goto update_tx_ptr;
    case MAL0_TXCTP3R:
        idx = 3;
    update_tx_ptr:
        mal->txctpr[idx] = val;
        break;
    case MAL0_RXCTP0R:
        idx = 0;
        goto update_rx_ptr;
    case MAL0_RXCTP1R:
        idx = 1;
    update_rx_ptr:
        mal->rxctpr[idx] = val;
        break;
    case MAL0_RCBS0:
        idx = 0;
        goto update_rx_size;
    case MAL0_RCBS1:
        idx = 1;
    update_rx_size:
        mal->rcbs[idx] = val & 0x000000FF;
        break;
    }
}

static void ppc40x_mal_reset (void *opaque)
{
    ppc40x_mal_t *mal;

    mal = opaque;
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

static void ppc405_mal_init(CPUState *env, qemu_irq irqs[4])
{
    ppc40x_mal_t *mal;
    int i;

    mal = g_malloc0(sizeof(ppc40x_mal_t));
    for (i = 0; i < 4; i++)
        mal->irqs[i] = irqs[i];
    qemu_register_reset(&ppc40x_mal_reset, mal);
    ppc_dcr_register(env, MAL0_CFG,
                     mal, &dcr_read_mal, &dcr_write_mal);
    ppc_dcr_register(env, MAL0_ESR,
                     mal, &dcr_read_mal, &dcr_write_mal);
    ppc_dcr_register(env, MAL0_IER,
                     mal, &dcr_read_mal, &dcr_write_mal);
    ppc_dcr_register(env, MAL0_TXCASR,
                     mal, &dcr_read_mal, &dcr_write_mal);
    ppc_dcr_register(env, MAL0_TXCARR,
                     mal, &dcr_read_mal, &dcr_write_mal);
    ppc_dcr_register(env, MAL0_TXEOBISR,
                     mal, &dcr_read_mal, &dcr_write_mal);
    ppc_dcr_register(env, MAL0_TXDEIR,
                     mal, &dcr_read_mal, &dcr_write_mal);
    ppc_dcr_register(env, MAL0_RXCASR,
                     mal, &dcr_read_mal, &dcr_write_mal);
    ppc_dcr_register(env, MAL0_RXCARR,
                     mal, &dcr_read_mal, &dcr_write_mal);
    ppc_dcr_register(env, MAL0_RXEOBISR,
                     mal, &dcr_read_mal, &dcr_write_mal);
    ppc_dcr_register(env, MAL0_RXDEIR,
                     mal, &dcr_read_mal, &dcr_write_mal);
    ppc_dcr_register(env, MAL0_TXCTP0R,
                     mal, &dcr_read_mal, &dcr_write_mal);
    ppc_dcr_register(env, MAL0_TXCTP1R,
                     mal, &dcr_read_mal, &dcr_write_mal);
    ppc_dcr_register(env, MAL0_TXCTP2R,
                     mal, &dcr_read_mal, &dcr_write_mal);
    ppc_dcr_register(env, MAL0_TXCTP3R,
                     mal, &dcr_read_mal, &dcr_write_mal);
    ppc_dcr_register(env, MAL0_RXCTP0R,
                     mal, &dcr_read_mal, &dcr_write_mal);
    ppc_dcr_register(env, MAL0_RXCTP1R,
                     mal, &dcr_read_mal, &dcr_write_mal);
    ppc_dcr_register(env, MAL0_RCBS0,
                     mal, &dcr_read_mal, &dcr_write_mal);
    ppc_dcr_register(env, MAL0_RCBS1,
                     mal, &dcr_read_mal, &dcr_write_mal);
}

/*****************************************************************************/
/* SPR */
void ppc40x_core_reset (CPUState *env)
{
    target_ulong dbsr;

    printf("Reset PowerPC core\n");
    env->interrupt_request |= CPU_INTERRUPT_EXITTB;
    /* XXX: TOFIX */
#if 0
    cpu_reset(env);
#else
    qemu_system_reset_request();
#endif
    dbsr = env->spr[SPR_40x_DBSR];
    dbsr &= ~0x00000300;
    dbsr |= 0x00000100;
    env->spr[SPR_40x_DBSR] = dbsr;
}

void ppc40x_chip_reset (CPUState *env)
{
    target_ulong dbsr;

    printf("Reset PowerPC chip\n");
    env->interrupt_request |= CPU_INTERRUPT_EXITTB;
    /* XXX: TOFIX */
#if 0
    cpu_reset(env);
#else
    qemu_system_reset_request();
#endif
    /* XXX: TODO reset all internal peripherals */
    dbsr = env->spr[SPR_40x_DBSR];
    dbsr &= ~0x00000300;
    dbsr |= 0x00000200;
    env->spr[SPR_40x_DBSR] = dbsr;
}

void ppc40x_system_reset (CPUState *env)
{
    printf("Reset PowerPC system\n");
    qemu_system_reset_request();
}

void store_40x_dbcr0 (CPUState *env, uint32_t val)
{
    switch ((val >> 28) & 0x3) {
    case 0x0:
        /* No action */
        break;
    case 0x1:
        /* Core reset */
        ppc40x_core_reset(env);
        break;
    case 0x2:
        /* Chip reset */
        ppc40x_chip_reset(env);
        break;
    case 0x3:
        /* System reset */
        ppc40x_system_reset(env);
        break;
    }
}

/*****************************************************************************/
/* PowerPC 405CR */
enum {
    PPC405CR_CPC0_PLLMR  = 0x0B0,
    PPC405CR_CPC0_CR0    = 0x0B1,
    PPC405CR_CPC0_CR1    = 0x0B2,
    PPC405CR_CPC0_PSR    = 0x0B4,
    PPC405CR_CPC0_JTAGID = 0x0B5,
    PPC405CR_CPC0_ER     = 0x0B9,
    PPC405CR_CPC0_FR     = 0x0BA,
    PPC405CR_CPC0_SR     = 0x0BB,
};

enum {
    PPC405CR_CPU_CLK   = 0,
    PPC405CR_TMR_CLK   = 1,
    PPC405CR_PLB_CLK   = 2,
    PPC405CR_SDRAM_CLK = 3,
    PPC405CR_OPB_CLK   = 4,
    PPC405CR_EXT_CLK   = 5,
    PPC405CR_UART_CLK  = 6,
    PPC405CR_CLK_NB    = 7,
};

typedef struct ppc405cr_cpc_t ppc405cr_cpc_t;
struct ppc405cr_cpc_t {
    clk_setup_t clk_setup[PPC405CR_CLK_NB];
    uint32_t sysclk;
    uint32_t psr;
    uint32_t cr0;
    uint32_t cr1;
    uint32_t jtagid;
    uint32_t pllmr;
    uint32_t er;
    uint32_t fr;
};

static void ppc405cr_clk_setup (ppc405cr_cpc_t *cpc)
{
    uint64_t VCO_out, PLL_out;
    uint32_t CPU_clk, TMR_clk, SDRAM_clk, PLB_clk, OPB_clk, EXT_clk, UART_clk;
    int M, D0, D1, D2;

    D0 = ((cpc->pllmr >> 26) & 0x3) + 1; /* CBDV */
    if (cpc->pllmr & 0x80000000) {
        D1 = (((cpc->pllmr >> 20) - 1) & 0xF) + 1; /* FBDV */
        D2 = 8 - ((cpc->pllmr >> 16) & 0x7); /* FWDVA */
        M = D0 * D1 * D2;
        VCO_out = cpc->sysclk * M;
        if (VCO_out < 400000000 || VCO_out > 800000000) {
            /* PLL cannot lock */
            cpc->pllmr &= ~0x80000000;
            goto bypass_pll;
        }
        PLL_out = VCO_out / D2;
    } else {
        /* Bypass PLL */
    bypass_pll:
        M = D0;
        PLL_out = cpc->sysclk * M;
    }
    CPU_clk = PLL_out;
    if (cpc->cr1 & 0x00800000)
        TMR_clk = cpc->sysclk; /* Should have a separate clock */
    else
        TMR_clk = CPU_clk;
    PLB_clk = CPU_clk / D0;
    SDRAM_clk = PLB_clk;
    D0 = ((cpc->pllmr >> 10) & 0x3) + 1;
    OPB_clk = PLB_clk / D0;
    D0 = ((cpc->pllmr >> 24) & 0x3) + 2;
    EXT_clk = PLB_clk / D0;
    D0 = ((cpc->cr0 >> 1) & 0x1F) + 1;
    UART_clk = CPU_clk / D0;
    /* Setup CPU clocks */
    clk_setup(&cpc->clk_setup[PPC405CR_CPU_CLK], CPU_clk);
    /* Setup time-base clock */
    clk_setup(&cpc->clk_setup[PPC405CR_TMR_CLK], TMR_clk);
    /* Setup PLB clock */
    clk_setup(&cpc->clk_setup[PPC405CR_PLB_CLK], PLB_clk);
    /* Setup SDRAM clock */
    clk_setup(&cpc->clk_setup[PPC405CR_SDRAM_CLK], SDRAM_clk);
    /* Setup OPB clock */
    clk_setup(&cpc->clk_setup[PPC405CR_OPB_CLK], OPB_clk);
    /* Setup external clock */
    clk_setup(&cpc->clk_setup[PPC405CR_EXT_CLK], EXT_clk);
    /* Setup UART clock */
    clk_setup(&cpc->clk_setup[PPC405CR_UART_CLK], UART_clk);
}

static uint32_t dcr_read_crcpc (void *opaque, int dcrn)
{
    ppc405cr_cpc_t *cpc;
    uint32_t ret;

    cpc = opaque;
    switch (dcrn) {
    case PPC405CR_CPC0_PLLMR:
        ret = cpc->pllmr;
        break;
    case PPC405CR_CPC0_CR0:
        ret = cpc->cr0;
        break;
    case PPC405CR_CPC0_CR1:
        ret = cpc->cr1;
        break;
    case PPC405CR_CPC0_PSR:
        ret = cpc->psr;
        break;
    case PPC405CR_CPC0_JTAGID:
        ret = cpc->jtagid;
        break;
    case PPC405CR_CPC0_ER:
        ret = cpc->er;
        break;
    case PPC405CR_CPC0_FR:
        ret = cpc->fr;
        break;
    case PPC405CR_CPC0_SR:
        ret = ~(cpc->er | cpc->fr) & 0xFFFF0000;
        break;
    default:
        /* Avoid gcc warning */
        ret = 0;
        break;
    }

    return ret;
}

static void dcr_write_crcpc (void *opaque, int dcrn, uint32_t val)
{
    ppc405cr_cpc_t *cpc;

    cpc = opaque;
    switch (dcrn) {
    case PPC405CR_CPC0_PLLMR:
        cpc->pllmr = val & 0xFFF77C3F;
        break;
    case PPC405CR_CPC0_CR0:
        cpc->cr0 = val & 0x0FFFFFFE;
        break;
    case PPC405CR_CPC0_CR1:
        cpc->cr1 = val & 0x00800000;
        break;
    case PPC405CR_CPC0_PSR:
        /* Read-only */
        break;
    case PPC405CR_CPC0_JTAGID:
        /* Read-only */
        break;
    case PPC405CR_CPC0_ER:
        cpc->er = val & 0xBFFC0000;
        break;
    case PPC405CR_CPC0_FR:
        cpc->fr = val & 0xBFFC0000;
        break;
    case PPC405CR_CPC0_SR:
        /* Read-only */
        break;
    }
}

static void ppc405cr_cpc_reset (void *opaque)
{
    ppc405cr_cpc_t *cpc;
    int D;

    cpc = opaque;
    /* Compute PLLMR value from PSR settings */
    cpc->pllmr = 0x80000000;
    /* PFWD */
    switch ((cpc->psr >> 30) & 3) {
    case 0:
        /* Bypass */
        cpc->pllmr &= ~0x80000000;
        break;
    case 1:
        /* Divide by 3 */
        cpc->pllmr |= 5 << 16;
        break;
    case 2:
        /* Divide by 4 */
        cpc->pllmr |= 4 << 16;
        break;
    case 3:
        /* Divide by 6 */
        cpc->pllmr |= 2 << 16;
        break;
    }
    /* PFBD */
    D = (cpc->psr >> 28) & 3;
    cpc->pllmr |= (D + 1) << 20;
    /* PT   */
    D = (cpc->psr >> 25) & 7;
    switch (D) {
    case 0x2:
        cpc->pllmr |= 0x13;
        break;
    case 0x4:
        cpc->pllmr |= 0x15;
        break;
    case 0x5:
        cpc->pllmr |= 0x16;
        break;
    default:
        break;
    }
    /* PDC  */
    D = (cpc->psr >> 23) & 3;
    cpc->pllmr |= D << 26;
    /* ODP  */
    D = (cpc->psr >> 21) & 3;
    cpc->pllmr |= D << 10;
    /* EBPD */
    D = (cpc->psr >> 17) & 3;
    cpc->pllmr |= D << 24;
    cpc->cr0 = 0x0000003C;
    cpc->cr1 = 0x2B0D8800;
    cpc->er = 0x00000000;
    cpc->fr = 0x00000000;
    ppc405cr_clk_setup(cpc);
}

static void ppc405cr_clk_init (ppc405cr_cpc_t *cpc)
{
    int D;

    /* XXX: this should be read from IO pins */
    cpc->psr = 0x00000000; /* 8 bits ROM */
    /* PFWD */
    D = 0x2; /* Divide by 4 */
    cpc->psr |= D << 30;
    /* PFBD */
    D = 0x1; /* Divide by 2 */
    cpc->psr |= D << 28;
    /* PDC */
    D = 0x1; /* Divide by 2 */
    cpc->psr |= D << 23;
    /* PT */
    D = 0x5; /* M = 16 */
    cpc->psr |= D << 25;
    /* ODP */
    D = 0x1; /* Divide by 2 */
    cpc->psr |= D << 21;
    /* EBDP */
    D = 0x2; /* Divide by 4 */
    cpc->psr |= D << 17;
}

static void ppc405cr_cpc_init (CPUState *env, clk_setup_t clk_setup[7],
                               uint32_t sysclk)
{
    ppc405cr_cpc_t *cpc;

    cpc = g_malloc0(sizeof(ppc405cr_cpc_t));
    memcpy(cpc->clk_setup, clk_setup,
           PPC405CR_CLK_NB * sizeof(clk_setup_t));
    cpc->sysclk = sysclk;
    cpc->jtagid = 0x42051049;
    ppc_dcr_register(env, PPC405CR_CPC0_PSR, cpc,
                     &dcr_read_crcpc, &dcr_write_crcpc);
    ppc_dcr_register(env, PPC405CR_CPC0_CR0, cpc,
                     &dcr_read_crcpc, &dcr_write_crcpc);
    ppc_dcr_register(env, PPC405CR_CPC0_CR1, cpc,
                     &dcr_read_crcpc, &dcr_write_crcpc);
    ppc_dcr_register(env, PPC405CR_CPC0_JTAGID, cpc,
                     &dcr_read_crcpc, &dcr_write_crcpc);
    ppc_dcr_register(env, PPC405CR_CPC0_PLLMR, cpc,
                     &dcr_read_crcpc, &dcr_write_crcpc);
    ppc_dcr_register(env, PPC405CR_CPC0_ER, cpc,
                     &dcr_read_crcpc, &dcr_write_crcpc);
    ppc_dcr_register(env, PPC405CR_CPC0_FR, cpc,
                     &dcr_read_crcpc, &dcr_write_crcpc);
    ppc_dcr_register(env, PPC405CR_CPC0_SR, cpc,
                     &dcr_read_crcpc, &dcr_write_crcpc);
    ppc405cr_clk_init(cpc);
    qemu_register_reset(ppc405cr_cpc_reset, cpc);
}

CPUState *ppc405cr_init(MemoryRegion *address_space_mem,
                        MemoryRegion ram_memories[4],
                        target_phys_addr_t ram_bases[4],
                        target_phys_addr_t ram_sizes[4],
                        uint32_t sysclk, qemu_irq **picp,
                        int do_init)
{
    clk_setup_t clk_setup[PPC405CR_CLK_NB];
    qemu_irq dma_irqs[4];
    CPUState *env;
    qemu_irq *pic, *irqs;

    memset(clk_setup, 0, sizeof(clk_setup));
    env = ppc4xx_init("405cr", &clk_setup[PPC405CR_CPU_CLK],
                      &clk_setup[PPC405CR_TMR_CLK], sysclk);
    /* Memory mapped devices registers */
    /* PLB arbitrer */
    ppc4xx_plb_init(env);
    /* PLB to OPB bridge */
    ppc4xx_pob_init(env);
    /* OBP arbitrer */
    ppc4xx_opba_init(0xef600600);
    /* Universal interrupt controller */
    irqs = g_malloc0(sizeof(qemu_irq) * PPCUIC_OUTPUT_NB);
    irqs[PPCUIC_OUTPUT_INT] =
        ((qemu_irq *)env->irq_inputs)[PPC40x_INPUT_INT];
    irqs[PPCUIC_OUTPUT_CINT] =
        ((qemu_irq *)env->irq_inputs)[PPC40x_INPUT_CINT];
    pic = ppcuic_init(env, irqs, 0x0C0, 0, 1);
    *picp = pic;
    /* SDRAM controller */
    ppc4xx_sdram_init(env, pic[14], 1, ram_memories,
                      ram_bases, ram_sizes, do_init);
    /* External bus controller */
    ppc405_ebc_init(env);
    /* DMA controller */
    dma_irqs[0] = pic[26];
    dma_irqs[1] = pic[25];
    dma_irqs[2] = pic[24];
    dma_irqs[3] = pic[23];
    ppc405_dma_init(env, dma_irqs);
    /* Serial ports */
    if (serial_hds[0] != NULL) {
        serial_mm_init(address_space_mem, 0xef600300, 0, pic[0],
                       PPC_SERIAL_MM_BAUDBASE, serial_hds[0],
                       DEVICE_BIG_ENDIAN);
    }
    if (serial_hds[1] != NULL) {
        serial_mm_init(address_space_mem, 0xef600400, 0, pic[1],
                       PPC_SERIAL_MM_BAUDBASE, serial_hds[1],
                       DEVICE_BIG_ENDIAN);
    }
    /* IIC controller */
    ppc405_i2c_init(0xef600500, pic[2]);
    /* GPIO */
    ppc405_gpio_init(0xef600700);
    /* CPU control */
    ppc405cr_cpc_init(env, clk_setup, sysclk);

    return env;
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

enum {
    PPC405EP_CPU_CLK   = 0,
    PPC405EP_PLB_CLK   = 1,
    PPC405EP_OPB_CLK   = 2,
    PPC405EP_EBC_CLK   = 3,
    PPC405EP_MAL_CLK   = 4,
    PPC405EP_PCI_CLK   = 5,
    PPC405EP_UART0_CLK = 6,
    PPC405EP_UART1_CLK = 7,
    PPC405EP_CLK_NB    = 8,
};

typedef struct ppc405ep_cpc_t ppc405ep_cpc_t;
struct ppc405ep_cpc_t {
    uint32_t sysclk;
    clk_setup_t clk_setup[PPC405EP_CLK_NB];
    uint32_t boot;
    uint32_t epctl;
    uint32_t pllmr[2];
    uint32_t ucr;
    uint32_t srr;
    uint32_t jtagid;
    uint32_t pci;
    /* Clock and power management */
    uint32_t er;
    uint32_t fr;
    uint32_t sr;
};

static void ppc405ep_compute_clocks (ppc405ep_cpc_t *cpc)
{
    uint32_t CPU_clk, PLB_clk, OPB_clk, EBC_clk, MAL_clk, PCI_clk;
    uint32_t UART0_clk, UART1_clk;
    uint64_t VCO_out, PLL_out;
    int M, D;

    VCO_out = 0;
    if ((cpc->pllmr[1] & 0x80000000) && !(cpc->pllmr[1] & 0x40000000)) {
        M = (((cpc->pllmr[1] >> 20) - 1) & 0xF) + 1; /* FBMUL */
#ifdef DEBUG_CLOCKS_LL
        printf("FBMUL %01" PRIx32 " %d\n", (cpc->pllmr[1] >> 20) & 0xF, M);
#endif
        D = 8 - ((cpc->pllmr[1] >> 16) & 0x7); /* FWDA */
#ifdef DEBUG_CLOCKS_LL
        printf("FWDA %01" PRIx32 " %d\n", (cpc->pllmr[1] >> 16) & 0x7, D);
#endif
        VCO_out = cpc->sysclk * M * D;
        if (VCO_out < 500000000UL || VCO_out > 1000000000UL) {
            /* Error - unlock the PLL */
            printf("VCO out of range %" PRIu64 "\n", VCO_out);
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
#ifdef DEBUG_CLOCKS_LL
    printf("CCDV %01" PRIx32 " %d\n", (cpc->pllmr[0] >> 20) & 0x3, D);
#endif
    CPU_clk = PLL_out / D;
    D = ((cpc->pllmr[0] >> 16) & 0x3) + 1; /* CBDV */
#ifdef DEBUG_CLOCKS_LL
    printf("CBDV %01" PRIx32 " %d\n", (cpc->pllmr[0] >> 16) & 0x3, D);
#endif
    PLB_clk = CPU_clk / D;
    D = ((cpc->pllmr[0] >> 12) & 0x3) + 1; /* OPDV */
#ifdef DEBUG_CLOCKS_LL
    printf("OPDV %01" PRIx32 " %d\n", (cpc->pllmr[0] >> 12) & 0x3, D);
#endif
    OPB_clk = PLB_clk / D;
    D = ((cpc->pllmr[0] >> 8) & 0x3) + 2; /* EPDV */
#ifdef DEBUG_CLOCKS_LL
    printf("EPDV %01" PRIx32 " %d\n", (cpc->pllmr[0] >> 8) & 0x3, D);
#endif
    EBC_clk = PLB_clk / D;
    D = ((cpc->pllmr[0] >> 4) & 0x3) + 1; /* MPDV */
#ifdef DEBUG_CLOCKS_LL
    printf("MPDV %01" PRIx32 " %d\n", (cpc->pllmr[0] >> 4) & 0x3, D);
#endif
    MAL_clk = PLB_clk / D;
    D = (cpc->pllmr[0] & 0x3) + 1; /* PPDV */
#ifdef DEBUG_CLOCKS_LL
    printf("PPDV %01" PRIx32 " %d\n", cpc->pllmr[0] & 0x3, D);
#endif
    PCI_clk = PLB_clk / D;
    D = ((cpc->ucr - 1) & 0x7F) + 1; /* U0DIV */
#ifdef DEBUG_CLOCKS_LL
    printf("U0DIV %01" PRIx32 " %d\n", cpc->ucr & 0x7F, D);
#endif
    UART0_clk = PLL_out / D;
    D = (((cpc->ucr >> 8) - 1) & 0x7F) + 1; /* U1DIV */
#ifdef DEBUG_CLOCKS_LL
    printf("U1DIV %01" PRIx32 " %d\n", (cpc->ucr >> 8) & 0x7F, D);
#endif
    UART1_clk = PLL_out / D;
#ifdef DEBUG_CLOCKS
    printf("Setup PPC405EP clocks - sysclk %" PRIu32 " VCO %" PRIu64
           " PLL out %" PRIu64 " Hz\n", cpc->sysclk, VCO_out, PLL_out);
    printf("CPU %" PRIu32 " PLB %" PRIu32 " OPB %" PRIu32 " EBC %" PRIu32
           " MAL %" PRIu32 " PCI %" PRIu32 " UART0 %" PRIu32
           " UART1 %" PRIu32 "\n",
           CPU_clk, PLB_clk, OPB_clk, EBC_clk, MAL_clk, PCI_clk,
           UART0_clk, UART1_clk);
#endif
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

static uint32_t dcr_read_epcpc (void *opaque, int dcrn)
{
    ppc405ep_cpc_t *cpc;
    uint32_t ret;

    cpc = opaque;
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

static void dcr_write_epcpc (void *opaque, int dcrn, uint32_t val)
{
    ppc405ep_cpc_t *cpc;

    cpc = opaque;
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

static void ppc405ep_cpc_reset (void *opaque)
{
    ppc405ep_cpc_t *cpc = opaque;

    cpc->boot = 0x00000010;     /* Boot from PCI - IIC EEPROM disabled */
    cpc->epctl = 0x00000000;
    cpc->pllmr[0] = 0x00011010;
    cpc->pllmr[1] = 0x40000000;
    cpc->ucr = 0x00000000;
    cpc->srr = 0x00040000;
    cpc->pci = 0x00000000;
    cpc->er = 0x00000000;
    cpc->fr = 0x00000000;
    cpc->sr = 0x00000000;
    ppc405ep_compute_clocks(cpc);
}

/* XXX: sysclk should be between 25 and 100 MHz */
static void ppc405ep_cpc_init (CPUState *env, clk_setup_t clk_setup[8],
                               uint32_t sysclk)
{
    ppc405ep_cpc_t *cpc;

    cpc = g_malloc0(sizeof(ppc405ep_cpc_t));
    memcpy(cpc->clk_setup, clk_setup,
           PPC405EP_CLK_NB * sizeof(clk_setup_t));
    cpc->jtagid = 0x20267049;
    cpc->sysclk = sysclk;
    qemu_register_reset(&ppc405ep_cpc_reset, cpc);
    ppc_dcr_register(env, PPC405EP_CPC0_BOOT, cpc,
                     &dcr_read_epcpc, &dcr_write_epcpc);
    ppc_dcr_register(env, PPC405EP_CPC0_EPCTL, cpc,
                     &dcr_read_epcpc, &dcr_write_epcpc);
    ppc_dcr_register(env, PPC405EP_CPC0_PLLMR0, cpc,
                     &dcr_read_epcpc, &dcr_write_epcpc);
    ppc_dcr_register(env, PPC405EP_CPC0_PLLMR1, cpc,
                     &dcr_read_epcpc, &dcr_write_epcpc);
    ppc_dcr_register(env, PPC405EP_CPC0_UCR, cpc,
                     &dcr_read_epcpc, &dcr_write_epcpc);
    ppc_dcr_register(env, PPC405EP_CPC0_SRR, cpc,
                     &dcr_read_epcpc, &dcr_write_epcpc);
    ppc_dcr_register(env, PPC405EP_CPC0_JTAGID, cpc,
                     &dcr_read_epcpc, &dcr_write_epcpc);
    ppc_dcr_register(env, PPC405EP_CPC0_PCI, cpc,
                     &dcr_read_epcpc, &dcr_write_epcpc);
#if 0
    ppc_dcr_register(env, PPC405EP_CPC0_ER, cpc,
                     &dcr_read_epcpc, &dcr_write_epcpc);
    ppc_dcr_register(env, PPC405EP_CPC0_FR, cpc,
                     &dcr_read_epcpc, &dcr_write_epcpc);
    ppc_dcr_register(env, PPC405EP_CPC0_SR, cpc,
                     &dcr_read_epcpc, &dcr_write_epcpc);
#endif
}

CPUState *ppc405ep_init(MemoryRegion *address_space_mem,
                        MemoryRegion ram_memories[2],
                        target_phys_addr_t ram_bases[2],
                        target_phys_addr_t ram_sizes[2],
                        uint32_t sysclk, qemu_irq **picp,
                        int do_init)
{
    clk_setup_t clk_setup[PPC405EP_CLK_NB], tlb_clk_setup;
    qemu_irq dma_irqs[4], gpt_irqs[5], mal_irqs[4];
    CPUState *env;
    qemu_irq *pic, *irqs;

    memset(clk_setup, 0, sizeof(clk_setup));
    /* init CPUs */
    env = ppc4xx_init("405ep", &clk_setup[PPC405EP_CPU_CLK],
                      &tlb_clk_setup, sysclk);
    clk_setup[PPC405EP_CPU_CLK].cb = tlb_clk_setup.cb;
    clk_setup[PPC405EP_CPU_CLK].opaque = tlb_clk_setup.opaque;
    /* Internal devices init */
    /* Memory mapped devices registers */
    /* PLB arbitrer */
    ppc4xx_plb_init(env);
    /* PLB to OPB bridge */
    ppc4xx_pob_init(env);
    /* OBP arbitrer */
    ppc4xx_opba_init(0xef600600);
    /* Universal interrupt controller */
    irqs = g_malloc0(sizeof(qemu_irq) * PPCUIC_OUTPUT_NB);
    irqs[PPCUIC_OUTPUT_INT] =
        ((qemu_irq *)env->irq_inputs)[PPC40x_INPUT_INT];
    irqs[PPCUIC_OUTPUT_CINT] =
        ((qemu_irq *)env->irq_inputs)[PPC40x_INPUT_CINT];
    pic = ppcuic_init(env, irqs, 0x0C0, 0, 1);
    *picp = pic;
    /* SDRAM controller */
	/* XXX 405EP has no ECC interrupt */
    ppc4xx_sdram_init(env, pic[17], 2, ram_memories,
                      ram_bases, ram_sizes, do_init);
    /* External bus controller */
    ppc405_ebc_init(env);
    /* DMA controller */
    dma_irqs[0] = pic[5];
    dma_irqs[1] = pic[6];
    dma_irqs[2] = pic[7];
    dma_irqs[3] = pic[8];
    ppc405_dma_init(env, dma_irqs);
    /* IIC controller */
    ppc405_i2c_init(0xef600500, pic[2]);
    /* GPIO */
    ppc405_gpio_init(0xef600700);
    /* Serial ports */
    if (serial_hds[0] != NULL) {
        serial_mm_init(address_space_mem, 0xef600300, 0, pic[0],
                       PPC_SERIAL_MM_BAUDBASE, serial_hds[0],
                       DEVICE_BIG_ENDIAN);
    }
    if (serial_hds[1] != NULL) {
        serial_mm_init(address_space_mem, 0xef600400, 0, pic[1],
                       PPC_SERIAL_MM_BAUDBASE, serial_hds[1],
                       DEVICE_BIG_ENDIAN);
    }
    /* OCM */
    ppc405_ocm_init(env);
    /* GPT */
    gpt_irqs[0] = pic[19];
    gpt_irqs[1] = pic[20];
    gpt_irqs[2] = pic[21];
    gpt_irqs[3] = pic[22];
    gpt_irqs[4] = pic[23];
    ppc4xx_gpt_init(0xef600000, gpt_irqs);
    /* PCI */
    /* Uses pic[3], pic[16], pic[18] */
    /* MAL */
    mal_irqs[0] = pic[11];
    mal_irqs[1] = pic[12];
    mal_irqs[2] = pic[13];
    mal_irqs[3] = pic[14];
    ppc405_mal_init(env, mal_irqs);
    /* Ethernet */
    /* Uses pic[9], pic[15], pic[17] */
    /* CPU control */
    ppc405ep_cpc_init(env, clk_setup, sysclk);

    return env;
}
