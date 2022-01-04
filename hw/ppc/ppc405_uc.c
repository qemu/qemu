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
#include "ppc405.h"
#include "hw/char/serial.h"
#include "qemu/timer.h"
#include "sysemu/reset.h"
#include "sysemu/sysemu.h"
#include "exec/address-spaces.h"
#include "hw/intc/ppc-uic.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "trace.h"

static void ppc405_set_default_bootinfo(ppc4xx_bd_info_t *bd,
                                        ram_addr_t ram_size)
{
        memset(bd, 0, sizeof(*bd));

        bd->bi_memstart = PPC405EP_SDRAM_BASE;
        bd->bi_memsize = ram_size;
        bd->bi_sramstart = PPC405EP_SRAM_BASE;
        bd->bi_sramsize = PPC405EP_SRAM_SIZE;
        bd->bi_bootflags = 0;
        bd->bi_intfreq = 133333333;
        bd->bi_busfreq = 33333333;
        bd->bi_baudrate = 115200;
        bd->bi_s_version[0] = 'Q';
        bd->bi_s_version[1] = 'M';
        bd->bi_s_version[2] = 'U';
        bd->bi_s_version[3] = '\0';
        bd->bi_r_version[0] = 'Q';
        bd->bi_r_version[1] = 'E';
        bd->bi_r_version[2] = 'M';
        bd->bi_r_version[3] = 'U';
        bd->bi_r_version[4] = '\0';
        bd->bi_procfreq = 133333333;
        bd->bi_plb_busfreq = 33333333;
        bd->bi_pci_busfreq = 33333333;
        bd->bi_opbfreq = 33333333;
}

static ram_addr_t __ppc405_set_bootinfo(CPUPPCState *env, ppc4xx_bd_info_t *bd)
{
    CPUState *cs = env_cpu(env);
    ram_addr_t bdloc;
    int i, n;

    /* We put the bd structure at the top of memory */
    if (bd->bi_memsize >= 0x01000000UL)
        bdloc = 0x01000000UL - sizeof(struct ppc4xx_bd_info_t);
    else
        bdloc = bd->bi_memsize - sizeof(struct ppc4xx_bd_info_t);
    stl_be_phys(cs->as, bdloc + 0x00, bd->bi_memstart);
    stl_be_phys(cs->as, bdloc + 0x04, bd->bi_memsize);
    stl_be_phys(cs->as, bdloc + 0x08, bd->bi_flashstart);
    stl_be_phys(cs->as, bdloc + 0x0C, bd->bi_flashsize);
    stl_be_phys(cs->as, bdloc + 0x10, bd->bi_flashoffset);
    stl_be_phys(cs->as, bdloc + 0x14, bd->bi_sramstart);
    stl_be_phys(cs->as, bdloc + 0x18, bd->bi_sramsize);
    stl_be_phys(cs->as, bdloc + 0x1C, bd->bi_bootflags);
    stl_be_phys(cs->as, bdloc + 0x20, bd->bi_ipaddr);
    for (i = 0; i < 6; i++) {
        stb_phys(cs->as, bdloc + 0x24 + i, bd->bi_enetaddr[i]);
    }
    stw_be_phys(cs->as, bdloc + 0x2A, bd->bi_ethspeed);
    stl_be_phys(cs->as, bdloc + 0x2C, bd->bi_intfreq);
    stl_be_phys(cs->as, bdloc + 0x30, bd->bi_busfreq);
    stl_be_phys(cs->as, bdloc + 0x34, bd->bi_baudrate);
    for (i = 0; i < 4; i++) {
        stb_phys(cs->as, bdloc + 0x38 + i, bd->bi_s_version[i]);
    }
    for (i = 0; i < 32; i++) {
        stb_phys(cs->as, bdloc + 0x3C + i, bd->bi_r_version[i]);
    }
    stl_be_phys(cs->as, bdloc + 0x5C, bd->bi_procfreq);
    stl_be_phys(cs->as, bdloc + 0x60, bd->bi_plb_busfreq);
    stl_be_phys(cs->as, bdloc + 0x64, bd->bi_pci_busfreq);
    for (i = 0; i < 6; i++) {
        stb_phys(cs->as, bdloc + 0x68 + i, bd->bi_pci_enetaddr[i]);
    }
    n = 0x70; /* includes 2 bytes hole */
    for (i = 0; i < 6; i++) {
        stb_phys(cs->as, bdloc + n++, bd->bi_pci_enetaddr2[i]);
    }
    stl_be_phys(cs->as, bdloc + n, bd->bi_opbfreq);
    n += 4;
    for (i = 0; i < 2; i++) {
        stl_be_phys(cs->as, bdloc + n, bd->bi_iic_fast[i]);
        n += 4;
    }

    return bdloc;
}

ram_addr_t ppc405_set_bootinfo(CPUPPCState *env, ram_addr_t ram_size)
{
    ppc4xx_bd_info_t bd;

    memset(&bd, 0, sizeof(bd));

    ppc405_set_default_bootinfo(&bd, ram_size);

    return __ppc405_set_bootinfo(env, &bd);
}

/*****************************************************************************/
/* Shared peripherals */

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

void ppc4xx_plb_init(CPUPPCState *env)
{
    ppc4xx_plb_t *plb;

    plb = g_malloc0(sizeof(ppc4xx_plb_t));
    ppc_dcr_register(env, PLB3A0_ACR, plb, &dcr_read_plb, &dcr_write_plb);
    ppc_dcr_register(env, PLB4A0_ACR, plb, &dcr_read_plb, &dcr_write_plb);
    ppc_dcr_register(env, PLB0_ACR, plb, &dcr_read_plb, &dcr_write_plb);
    ppc_dcr_register(env, PLB0_BEAR, plb, &dcr_read_plb, &dcr_write_plb);
    ppc_dcr_register(env, PLB0_BESR, plb, &dcr_read_plb, &dcr_write_plb);
    ppc_dcr_register(env, PLB4A1_ACR, plb, &dcr_read_plb, &dcr_write_plb);
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
    uint32_t besr0;
    uint32_t besr1;
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

static void dcr_write_pob (void *opaque, int dcrn, uint32_t val)
{
    ppc4xx_pob_t *pob;

    pob = opaque;
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

static void ppc4xx_pob_reset (void *opaque)
{
    ppc4xx_pob_t *pob;

    pob = opaque;
    /* No error */
    pob->bear = 0x00000000;
    pob->besr0 = 0x0000000;
    pob->besr1 = 0x0000000;
}

static void ppc4xx_pob_init(CPUPPCState *env)
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

static uint64_t opba_readb(void *opaque, hwaddr addr, unsigned size)
{
    ppc4xx_opba_t *opba = opaque;
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
    ppc4xx_opba_t *opba = opaque;

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

static void ppc4xx_opba_reset (void *opaque)
{
    ppc4xx_opba_t *opba;

    opba = opaque;
    opba->cr = 0x00; /* No dynamic priorities - park disabled */
    opba->pr = 0x11;
}

static void ppc4xx_opba_init(hwaddr base)
{
    ppc4xx_opba_t *opba;

    trace_opba_init(base);

    opba = g_malloc0(sizeof(ppc4xx_opba_t));
    memory_region_init_io(&opba->io, NULL, &opba_ops, opba, "opba", 0x002);
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

void ppc405_ebc_init(CPUPPCState *env)
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

static void ppc405_dma_init(CPUPPCState *env, qemu_irq irqs[4])
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

static void ppc405_gpio_reset (void *opaque)
{
}

static void ppc405_gpio_init(hwaddr base)
{
    ppc405_gpio_t *gpio;

    trace_ppc405_gpio_init(base);

    gpio = g_malloc0(sizeof(ppc405_gpio_t));
    memory_region_init_io(&gpio->io, NULL, &ppc405_gpio_ops, gpio, "pgio", 0x038);
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

static void ppc405_ocm_init(CPUPPCState *env)
{
    ppc405_ocm_t *ocm;

    ocm = g_malloc0(sizeof(ppc405_ocm_t));
    /* XXX: Size is 4096 or 0x04000000 */
    memory_region_init_ram(&ocm->isarc_ram, NULL, "ppc405.ocm", 4 * KiB,
                           &error_fatal);
    memory_region_init_alias(&ocm->dsarc_ram, NULL, "ppc405.dsarc",
                             &ocm->isarc_ram, 0, 4 * KiB);
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
/* General purpose timers */
typedef struct ppc4xx_gpt_t ppc4xx_gpt_t;
struct ppc4xx_gpt_t {
    MemoryRegion iomem;
    int64_t tb_offset;
    uint32_t tb_freq;
    QEMUTimer *timer;
    qemu_irq irqs[5];
    uint32_t oe;
    uint32_t ol;
    uint32_t im;
    uint32_t is;
    uint32_t ie;
    uint32_t comp[5];
    uint32_t mask[5];
};

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

static uint64_t ppc4xx_gpt_read(void *opaque, hwaddr addr, unsigned size)
{
    ppc4xx_gpt_t *gpt = opaque;
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
    ppc4xx_gpt_t *gpt = opaque;
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

static void ppc4xx_gpt_init(hwaddr base, qemu_irq irqs[5])
{
    ppc4xx_gpt_t *gpt;
    int i;

    trace_ppc4xx_gpt_init(base);

    gpt = g_malloc0(sizeof(ppc4xx_gpt_t));
    for (i = 0; i < 5; i++) {
        gpt->irqs[i] = irqs[i];
    }
    gpt->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, &ppc4xx_gpt_cb, gpt);
    memory_region_init_io(&gpt->iomem, NULL, &gpt_ops, gpt, "gpt", 0x0d4);
    memory_region_add_subregion(get_system_memory(), base, &gpt->iomem);
    qemu_register_reset(ppc4xx_gpt_reset, gpt);
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
    cpc->pllmr[0] = 0x00021002;
    cpc->pllmr[1] = 0x80a552be;
    cpc->ucr = 0x00004646;
    cpc->srr = 0x00040000;
    cpc->pci = 0x00000000;
    cpc->er = 0x00000000;
    cpc->fr = 0x00000000;
    cpc->sr = 0x00000000;
    ppc405ep_compute_clocks(cpc);
}

/* XXX: sysclk should be between 25 and 100 MHz */
static void ppc405ep_cpc_init (CPUPPCState *env, clk_setup_t clk_setup[8],
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

PowerPCCPU *ppc405ep_init(MemoryRegion *address_space_mem,
                        MemoryRegion ram_memories[2],
                        hwaddr ram_bases[2],
                        hwaddr ram_sizes[2],
                        uint32_t sysclk, DeviceState **uicdevp,
                        int do_init)
{
    clk_setup_t clk_setup[PPC405EP_CLK_NB], tlb_clk_setup;
    qemu_irq dma_irqs[4], gpt_irqs[5], mal_irqs[4];
    PowerPCCPU *cpu;
    CPUPPCState *env;
    DeviceState *uicdev;
    SysBusDevice *uicsbd;

    memset(clk_setup, 0, sizeof(clk_setup));
    /* init CPUs */
    cpu = ppc4xx_init(POWERPC_CPU_TYPE_NAME("405ep"),
                      &clk_setup[PPC405EP_CPU_CLK],
                      &tlb_clk_setup, sysclk);
    env = &cpu->env;
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
    uicdev = qdev_new(TYPE_PPC_UIC);
    uicsbd = SYS_BUS_DEVICE(uicdev);

    object_property_set_link(OBJECT(uicdev), "cpu", OBJECT(cpu),
                             &error_fatal);
    sysbus_realize_and_unref(uicsbd, &error_fatal);

    sysbus_connect_irq(uicsbd, PPCUIC_OUTPUT_INT,
                       ((qemu_irq *)env->irq_inputs)[PPC40x_INPUT_INT]);
    sysbus_connect_irq(uicsbd, PPCUIC_OUTPUT_CINT,
                       ((qemu_irq *)env->irq_inputs)[PPC40x_INPUT_CINT]);

    *uicdevp = uicdev;

    /* SDRAM controller */
        /* XXX 405EP has no ECC interrupt */
    ppc4xx_sdram_init(env, qdev_get_gpio_in(uicdev, 17), 2, ram_memories,
                      ram_bases, ram_sizes, do_init);
    /* External bus controller */
    ppc405_ebc_init(env);
    /* DMA controller */
    dma_irqs[0] = qdev_get_gpio_in(uicdev, 5);
    dma_irqs[1] = qdev_get_gpio_in(uicdev, 6);
    dma_irqs[2] = qdev_get_gpio_in(uicdev, 7);
    dma_irqs[3] = qdev_get_gpio_in(uicdev, 8);
    ppc405_dma_init(env, dma_irqs);
    /* IIC controller */
    sysbus_create_simple(TYPE_PPC4xx_I2C, 0xef600500,
                         qdev_get_gpio_in(uicdev, 2));
    /* GPIO */
    ppc405_gpio_init(0xef600700);
    /* Serial ports */
    if (serial_hd(0) != NULL) {
        serial_mm_init(address_space_mem, 0xef600300, 0,
                       qdev_get_gpio_in(uicdev, 0),
                       PPC_SERIAL_MM_BAUDBASE, serial_hd(0),
                       DEVICE_BIG_ENDIAN);
    }
    if (serial_hd(1) != NULL) {
        serial_mm_init(address_space_mem, 0xef600400, 0,
                       qdev_get_gpio_in(uicdev, 1),
                       PPC_SERIAL_MM_BAUDBASE, serial_hd(1),
                       DEVICE_BIG_ENDIAN);
    }
    /* OCM */
    ppc405_ocm_init(env);
    /* GPT */
    gpt_irqs[0] = qdev_get_gpio_in(uicdev, 19);
    gpt_irqs[1] = qdev_get_gpio_in(uicdev, 20);
    gpt_irqs[2] = qdev_get_gpio_in(uicdev, 21);
    gpt_irqs[3] = qdev_get_gpio_in(uicdev, 22);
    gpt_irqs[4] = qdev_get_gpio_in(uicdev, 23);
    ppc4xx_gpt_init(0xef600000, gpt_irqs);
    /* PCI */
    /* Uses UIC IRQs 3, 16, 18 */
    /* MAL */
    mal_irqs[0] = qdev_get_gpio_in(uicdev, 11);
    mal_irqs[1] = qdev_get_gpio_in(uicdev, 12);
    mal_irqs[2] = qdev_get_gpio_in(uicdev, 13);
    mal_irqs[3] = qdev_get_gpio_in(uicdev, 14);
    ppc4xx_mal_init(env, 4, 2, mal_irqs);
    /* Ethernet */
    /* Uses UIC IRQs 9, 15, 17 */
    /* CPU control */
    ppc405ep_cpc_init(env, clk_setup, sysclk);

    return cpu;
}
