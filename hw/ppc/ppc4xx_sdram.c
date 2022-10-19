/*
 * QEMU PowerPC 4xx embedded processors SDRAM controller emulation
 *
 * DDR SDRAM controller:
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
 *
 * DDR2 SDRAM controller:
 * Copyright (c) 2012 François Revol
 * Copyright (c) 2016-2019 BALATON Zoltan
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "exec/address-spaces.h" /* get_system_memory() */
#include "exec/cpu-defs.h" /* target_ulong */
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/ppc/ppc4xx.h"
#include "trace.h"

/*****************************************************************************/
/* Shared functions */

/*
 * Split RAM between SDRAM banks.
 *
 * sdram_bank_sizes[] must be in descending order, that is sizes[i] > sizes[i+1]
 * and must be 0-terminated.
 *
 * The 4xx SDRAM controller supports a small number of banks, and each bank
 * must be one of a small set of sizes. The number of banks and the supported
 * sizes varies by SoC.
 */
static void ppc4xx_sdram_banks(MemoryRegion *ram, int nr_banks,
                               Ppc4xxSdramBank ram_banks[],
                               const ram_addr_t sdram_bank_sizes[])
{
    ram_addr_t size_left = memory_region_size(ram);
    ram_addr_t base = 0;
    ram_addr_t bank_size;
    int i;
    int j;

    for (i = 0; i < nr_banks; i++) {
        for (j = 0; sdram_bank_sizes[j] != 0; j++) {
            bank_size = sdram_bank_sizes[j];
            if (bank_size <= size_left) {
                char name[32];

                ram_banks[i].base = base;
                ram_banks[i].size = bank_size;
                base += bank_size;
                size_left -= bank_size;
                snprintf(name, sizeof(name), "ppc4xx.sdram%d", i);
                memory_region_init_alias(&ram_banks[i].ram, NULL, name, ram,
                                         ram_banks[i].base, ram_banks[i].size);
                break;
            }
        }
        if (!size_left) {
            /* No need to use the remaining banks. */
            break;
        }
    }

    if (size_left) {
        ram_addr_t used_size = memory_region_size(ram) - size_left;
        GString *s = g_string_new(NULL);

        for (i = 0; sdram_bank_sizes[i]; i++) {
            g_string_append_printf(s, "%" PRIi64 "%s",
                                   sdram_bank_sizes[i] / MiB,
                                   sdram_bank_sizes[i + 1] ? ", " : "");
        }
        error_report("at most %d bank%s of %s MiB each supported",
                     nr_banks, nr_banks == 1 ? "" : "s", s->str);
        error_printf("Possible valid RAM size: %" PRIi64 " MiB\n",
            used_size ? used_size / MiB : sdram_bank_sizes[i - 1] / MiB);

        g_string_free(s, true);
        exit(EXIT_FAILURE);
    }
}

static void sdram_bank_map(Ppc4xxSdramBank *bank)
{
    memory_region_init(&bank->container, NULL, "sdram-container", bank->size);
    memory_region_add_subregion(&bank->container, 0, &bank->ram);
    memory_region_add_subregion(get_system_memory(), bank->base,
                                &bank->container);
}

static void sdram_bank_unmap(Ppc4xxSdramBank *bank)
{
    memory_region_del_subregion(get_system_memory(), &bank->container);
    memory_region_del_subregion(&bank->container, &bank->ram);
    object_unparent(OBJECT(&bank->container));
}

enum {
    SDRAM0_CFGADDR = 0x010,
    SDRAM0_CFGDATA = 0x011,
};

/*****************************************************************************/
/* DDR SDRAM controller */
/*
 * XXX: TOFIX: some patches have made this code become inconsistent:
 *      there are type inconsistencies, mixing hwaddr, target_ulong
 *      and uint32_t
 */
static uint32_t sdram_ddr_bcr(hwaddr ram_base, hwaddr ram_size)
{
    uint32_t bcr;

    switch (ram_size) {
    case 4 * MiB:
        bcr = 0;
        break;
    case 8 * MiB:
        bcr = 0x20000;
        break;
    case 16 * MiB:
        bcr = 0x40000;
        break;
    case 32 * MiB:
        bcr = 0x60000;
        break;
    case 64 * MiB:
        bcr = 0x80000;
        break;
    case 128 * MiB:
        bcr = 0xA0000;
        break;
    case 256 * MiB:
        bcr = 0xC0000;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid RAM size 0x%" HWADDR_PRIx "\n", __func__,
                      ram_size);
        return 0;
    }
    bcr |= ram_base & 0xFF800000;
    bcr |= 1;

    return bcr;
}

static inline hwaddr sdram_ddr_base(uint32_t bcr)
{
    return bcr & 0xFF800000;
}

static target_ulong sdram_ddr_size(uint32_t bcr)
{
    target_ulong size;
    int sh;

    sh = (bcr >> 17) & 0x7;
    if (sh == 7) {
        size = -1;
    } else {
        size = (4 * MiB) << sh;
    }

    return size;
}

static void sdram_ddr_set_bcr(Ppc4xxSdramDdrState *sdram, int i,
                              uint32_t bcr, int enabled)
{
    if (sdram->bank[i].bcr & 1) {
        /* Unmap RAM */
        trace_ppc4xx_sdram_unmap(sdram_ddr_base(sdram->bank[i].bcr),
                                 sdram_ddr_size(sdram->bank[i].bcr));
        memory_region_del_subregion(get_system_memory(),
                                    &sdram->bank[i].container);
        memory_region_del_subregion(&sdram->bank[i].container,
                                    &sdram->bank[i].ram);
        object_unparent(OBJECT(&sdram->bank[i].container));
    }
    sdram->bank[i].bcr = bcr & 0xFFDEE001;
    if (enabled && (bcr & 1)) {
        trace_ppc4xx_sdram_map(sdram_ddr_base(bcr), sdram_ddr_size(bcr));
        memory_region_init(&sdram->bank[i].container, NULL, "sdram-container",
                           sdram_ddr_size(bcr));
        memory_region_add_subregion(&sdram->bank[i].container, 0,
                                    &sdram->bank[i].ram);
        memory_region_add_subregion(get_system_memory(),
                                    sdram_ddr_base(bcr),
                                    &sdram->bank[i].container);
    }
}

static void sdram_ddr_map_bcr(Ppc4xxSdramDdrState *sdram)
{
    int i;

    for (i = 0; i < sdram->nbanks; i++) {
        if (sdram->bank[i].size != 0) {
            sdram_ddr_set_bcr(sdram, i, sdram_ddr_bcr(sdram->bank[i].base,
                                                      sdram->bank[i].size), 1);
        } else {
            sdram_ddr_set_bcr(sdram, i, 0, 0);
        }
    }
}

static void sdram_ddr_unmap_bcr(Ppc4xxSdramDdrState *sdram)
{
    int i;

    for (i = 0; i < sdram->nbanks; i++) {
        trace_ppc4xx_sdram_unmap(sdram_ddr_base(sdram->bank[i].bcr),
                                 sdram_ddr_size(sdram->bank[i].bcr));
        memory_region_del_subregion(get_system_memory(),
                                    &sdram->bank[i].ram);
    }
}

static uint32_t sdram_ddr_dcr_read(void *opaque, int dcrn)
{
    Ppc4xxSdramDdrState *sdram = opaque;
    uint32_t ret;

    switch (dcrn) {
    case SDRAM0_CFGADDR:
        ret = sdram->addr;
        break;
    case SDRAM0_CFGDATA:
        switch (sdram->addr) {
        case 0x00: /* SDRAM_BESR0 */
            ret = sdram->besr0;
            break;
        case 0x08: /* SDRAM_BESR1 */
            ret = sdram->besr1;
            break;
        case 0x10: /* SDRAM_BEAR */
            ret = sdram->bear;
            break;
        case 0x20: /* SDRAM_CFG */
            ret = sdram->cfg;
            break;
        case 0x24: /* SDRAM_STATUS */
            ret = sdram->status;
            break;
        case 0x30: /* SDRAM_RTR */
            ret = sdram->rtr;
            break;
        case 0x34: /* SDRAM_PMIT */
            ret = sdram->pmit;
            break;
        case 0x40: /* SDRAM_B0CR */
            ret = sdram->bank[0].bcr;
            break;
        case 0x44: /* SDRAM_B1CR */
            ret = sdram->bank[1].bcr;
            break;
        case 0x48: /* SDRAM_B2CR */
            ret = sdram->bank[2].bcr;
            break;
        case 0x4C: /* SDRAM_B3CR */
            ret = sdram->bank[3].bcr;
            break;
        case 0x80: /* SDRAM_TR */
            ret = -1; /* ? */
            break;
        case 0x94: /* SDRAM_ECCCFG */
            ret = sdram->ecccfg;
            break;
        case 0x98: /* SDRAM_ECCESR */
            ret = sdram->eccesr;
            break;
        default: /* Error */
            ret = -1;
            break;
        }
        break;
    default:
        /* Avoid gcc warning */
        ret = 0;
        break;
    }

    return ret;
}

static void sdram_ddr_dcr_write(void *opaque, int dcrn, uint32_t val)
{
    Ppc4xxSdramDdrState *sdram = opaque;

    switch (dcrn) {
    case SDRAM0_CFGADDR:
        sdram->addr = val;
        break;
    case SDRAM0_CFGDATA:
        switch (sdram->addr) {
        case 0x00: /* SDRAM_BESR0 */
            sdram->besr0 &= ~val;
            break;
        case 0x08: /* SDRAM_BESR1 */
            sdram->besr1 &= ~val;
            break;
        case 0x10: /* SDRAM_BEAR */
            sdram->bear = val;
            break;
        case 0x20: /* SDRAM_CFG */
            val &= 0xFFE00000;
            if (!(sdram->cfg & 0x80000000) && (val & 0x80000000)) {
                trace_ppc4xx_sdram_enable("enable");
                /* validate all RAM mappings */
                sdram_ddr_map_bcr(sdram);
                sdram->status &= ~0x80000000;
            } else if ((sdram->cfg & 0x80000000) && !(val & 0x80000000)) {
                trace_ppc4xx_sdram_enable("disable");
                /* invalidate all RAM mappings */
                sdram_ddr_unmap_bcr(sdram);
                sdram->status |= 0x80000000;
            }
            if (!(sdram->cfg & 0x40000000) && (val & 0x40000000)) {
                sdram->status |= 0x40000000;
            } else if ((sdram->cfg & 0x40000000) && !(val & 0x40000000)) {
                sdram->status &= ~0x40000000;
            }
            sdram->cfg = val;
            break;
        case 0x24: /* SDRAM_STATUS */
            /* Read-only register */
            break;
        case 0x30: /* SDRAM_RTR */
            sdram->rtr = val & 0x3FF80000;
            break;
        case 0x34: /* SDRAM_PMIT */
            sdram->pmit = (val & 0xF8000000) | 0x07C00000;
            break;
        case 0x40: /* SDRAM_B0CR */
            sdram_ddr_set_bcr(sdram, 0, val, sdram->cfg & 0x80000000);
            break;
        case 0x44: /* SDRAM_B1CR */
            sdram_ddr_set_bcr(sdram, 1, val, sdram->cfg & 0x80000000);
            break;
        case 0x48: /* SDRAM_B2CR */
            sdram_ddr_set_bcr(sdram, 2, val, sdram->cfg & 0x80000000);
            break;
        case 0x4C: /* SDRAM_B3CR */
            sdram_ddr_set_bcr(sdram, 3, val, sdram->cfg & 0x80000000);
            break;
        case 0x80: /* SDRAM_TR */
            sdram->tr = val & 0x018FC01F;
            break;
        case 0x94: /* SDRAM_ECCCFG */
            sdram->ecccfg = val & 0x00F00000;
            break;
        case 0x98: /* SDRAM_ECCESR */
            val &= 0xFFF0F000;
            if (sdram->eccesr == 0 && val != 0) {
                qemu_irq_raise(sdram->irq);
            } else if (sdram->eccesr != 0 && val == 0) {
                qemu_irq_lower(sdram->irq);
            }
            sdram->eccesr = val;
            break;
        default: /* Error */
            break;
        }
        break;
    }
}

static void ppc4xx_sdram_ddr_reset(DeviceState *dev)
{
    Ppc4xxSdramDdrState *sdram = PPC4xx_SDRAM_DDR(dev);

    sdram->addr = 0;
    sdram->bear = 0;
    sdram->besr0 = 0; /* No error */
    sdram->besr1 = 0; /* No error */
    sdram->cfg = 0;
    sdram->ecccfg = 0; /* No ECC */
    sdram->eccesr = 0; /* No error */
    sdram->pmit = 0x07C00000;
    sdram->rtr = 0x05F00000;
    sdram->tr = 0x00854009;
    /* We pre-initialize RAM banks */
    sdram->status = 0;
    sdram->cfg = 0x00800000;
}

static void ppc4xx_sdram_ddr_realize(DeviceState *dev, Error **errp)
{
    Ppc4xxSdramDdrState *s = PPC4xx_SDRAM_DDR(dev);
    Ppc4xxDcrDeviceState *dcr = PPC4xx_DCR_DEVICE(dev);
    const ram_addr_t valid_bank_sizes[] = {
        256 * MiB, 128 * MiB, 64 * MiB, 32 * MiB, 16 * MiB, 8 * MiB, 4 * MiB, 0
    };

    if (s->nbanks < 1 || s->nbanks > 4) {
        error_setg(errp, "Invalid number of RAM banks");
        return;
    }
    if (!s->dram_mr) {
        error_setg(errp, "Missing dram memory region");
        return;
    }
    ppc4xx_sdram_banks(s->dram_mr, s->nbanks, s->bank, valid_bank_sizes);

    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);

    ppc4xx_dcr_register(dcr, SDRAM0_CFGADDR,
                        s, &sdram_ddr_dcr_read, &sdram_ddr_dcr_write);
    ppc4xx_dcr_register(dcr, SDRAM0_CFGDATA,
                        s, &sdram_ddr_dcr_read, &sdram_ddr_dcr_write);
}

static Property ppc4xx_sdram_ddr_props[] = {
    DEFINE_PROP_LINK("dram", Ppc4xxSdramDdrState, dram_mr, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_UINT32("nbanks", Ppc4xxSdramDdrState, nbanks, 4),
    DEFINE_PROP_END_OF_LIST(),
};

static void ppc4xx_sdram_ddr_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = ppc4xx_sdram_ddr_realize;
    dc->reset = ppc4xx_sdram_ddr_reset;
    /* Reason: only works as function of a ppc4xx SoC */
    dc->user_creatable = false;
    device_class_set_props(dc, ppc4xx_sdram_ddr_props);
}

void ppc4xx_sdram_ddr_enable(Ppc4xxSdramDdrState *s)
{
    sdram_ddr_dcr_write(s, SDRAM0_CFGADDR, 0x20);
    sdram_ddr_dcr_write(s, SDRAM0_CFGDATA, 0x80000000);
}

/*****************************************************************************/
/* DDR2 SDRAM controller */
enum {
    SDRAM_R0BAS = 0x40,
    SDRAM_R1BAS,
    SDRAM_R2BAS,
    SDRAM_R3BAS,
    SDRAM_CONF1HB = 0x45,
    SDRAM_PLBADDULL = 0x4a,
    SDRAM_CONF1LL = 0x4b,
    SDRAM_CONFPATHB = 0x4f,
    SDRAM_PLBADDUHB = 0x50,
};

static uint32_t sdram_ddr2_bcr(hwaddr ram_base, hwaddr ram_size)
{
    uint32_t bcr;

    switch (ram_size) {
    case 8 * MiB:
        bcr = 0xffc0;
        break;
    case 16 * MiB:
        bcr = 0xff80;
        break;
    case 32 * MiB:
        bcr = 0xff00;
        break;
    case 64 * MiB:
        bcr = 0xfe00;
        break;
    case 128 * MiB:
        bcr = 0xfc00;
        break;
    case 256 * MiB:
        bcr = 0xf800;
        break;
    case 512 * MiB:
        bcr = 0xf000;
        break;
    case 1 * GiB:
        bcr = 0xe000;
        break;
    case 2 * GiB:
        bcr = 0xc000;
        break;
    case 4 * GiB:
        bcr = 0x8000;
        break;
    default:
        error_report("invalid RAM size " TARGET_FMT_plx, ram_size);
        return 0;
    }
    bcr |= ram_base >> 2 & 0xffe00000;
    bcr |= 1;

    return bcr;
}

static inline hwaddr sdram_ddr2_base(uint32_t bcr)
{
    return (bcr & 0xffe00000) << 2;
}

static uint64_t sdram_ddr2_size(uint32_t bcr)
{
    uint64_t size;
    int sh;

    sh = 1024 - ((bcr >> 6) & 0x3ff);
    size = 8 * MiB * sh;

    return size;
}

static void sdram_ddr2_set_bcr(Ppc4xxSdramDdr2State *sdram, int i,
                               uint32_t bcr, int enabled)
{
    if (sdram->bank[i].bcr & 1) {
        /* First unmap RAM if enabled */
        trace_ppc4xx_sdram_unmap(sdram_ddr2_base(sdram->bank[i].bcr),
                                 sdram_ddr2_size(sdram->bank[i].bcr));
        sdram_bank_unmap(&sdram->bank[i]);
    }
    sdram->bank[i].bcr = bcr & 0xffe0ffc1;
    if (enabled && (bcr & 1)) {
        trace_ppc4xx_sdram_map(sdram_ddr2_base(bcr), sdram_ddr2_size(bcr));
        sdram_bank_map(&sdram->bank[i]);
    }
}

static void sdram_ddr2_map_bcr(Ppc4xxSdramDdr2State *sdram)
{
    int i;

    for (i = 0; i < sdram->nbanks; i++) {
        if (sdram->bank[i].size) {
            sdram_ddr2_set_bcr(sdram, i,
                               sdram_ddr2_bcr(sdram->bank[i].base,
                                              sdram->bank[i].size), 1);
        } else {
            sdram_ddr2_set_bcr(sdram, i, 0, 0);
        }
    }
}

static void sdram_ddr2_unmap_bcr(Ppc4xxSdramDdr2State *sdram)
{
    int i;

    for (i = 0; i < sdram->nbanks; i++) {
        if (sdram->bank[i].size) {
            sdram_ddr2_set_bcr(sdram, i, sdram->bank[i].bcr & ~1, 0);
        }
    }
}

static uint32_t sdram_ddr2_dcr_read(void *opaque, int dcrn)
{
    Ppc4xxSdramDdr2State *sdram = opaque;
    uint32_t ret = 0;

    switch (dcrn) {
    case SDRAM_R0BAS:
    case SDRAM_R1BAS:
    case SDRAM_R2BAS:
    case SDRAM_R3BAS:
        if (sdram->bank[dcrn - SDRAM_R0BAS].size) {
            ret = sdram_ddr2_bcr(sdram->bank[dcrn - SDRAM_R0BAS].base,
                                 sdram->bank[dcrn - SDRAM_R0BAS].size);
        }
        break;
    case SDRAM_CONF1HB:
    case SDRAM_CONF1LL:
    case SDRAM_CONFPATHB:
    case SDRAM_PLBADDULL:
    case SDRAM_PLBADDUHB:
        break;
    case SDRAM0_CFGADDR:
        ret = sdram->addr;
        break;
    case SDRAM0_CFGDATA:
        switch (sdram->addr) {
        case 0x14: /* SDRAM_MCSTAT (405EX) */
        case 0x1F:
            ret = 0x80000000;
            break;
        case 0x21: /* SDRAM_MCOPT2 */
            ret = sdram->mcopt2;
            break;
        case 0x40: /* SDRAM_MB0CF */
            ret = 0x00008001;
            break;
        case 0x7A: /* SDRAM_DLCR */
            ret = 0x02000000;
            break;
        case 0xE1: /* SDR0_DDR0 */
            ret = SDR0_DDR0_DDRM_ENCODE(1) | SDR0_DDR0_DDRM_DDR1;
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }

    return ret;
}

#define SDRAM_DDR2_MCOPT2_DCEN BIT(27)

static void sdram_ddr2_dcr_write(void *opaque, int dcrn, uint32_t val)
{
    Ppc4xxSdramDdr2State *sdram = opaque;

    switch (dcrn) {
    case SDRAM_R0BAS:
    case SDRAM_R1BAS:
    case SDRAM_R2BAS:
    case SDRAM_R3BAS:
    case SDRAM_CONF1HB:
    case SDRAM_CONF1LL:
    case SDRAM_CONFPATHB:
    case SDRAM_PLBADDULL:
    case SDRAM_PLBADDUHB:
        break;
    case SDRAM0_CFGADDR:
        sdram->addr = val;
        break;
    case SDRAM0_CFGDATA:
        switch (sdram->addr) {
        case 0x00: /* B0CR */
            break;
        case 0x21: /* SDRAM_MCOPT2 */
            if (!(sdram->mcopt2 & SDRAM_DDR2_MCOPT2_DCEN) &&
                (val & SDRAM_DDR2_MCOPT2_DCEN)) {
                trace_ppc4xx_sdram_enable("enable");
                /* validate all RAM mappings */
                sdram_ddr2_map_bcr(sdram);
                sdram->mcopt2 |= SDRAM_DDR2_MCOPT2_DCEN;
            } else if ((sdram->mcopt2 & SDRAM_DDR2_MCOPT2_DCEN) &&
                       !(val & SDRAM_DDR2_MCOPT2_DCEN)) {
                trace_ppc4xx_sdram_enable("disable");
                /* invalidate all RAM mappings */
                sdram_ddr2_unmap_bcr(sdram);
                sdram->mcopt2 &= ~SDRAM_DDR2_MCOPT2_DCEN;
            }
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
}

static void ppc4xx_sdram_ddr2_reset(DeviceState *dev)
{
    Ppc4xxSdramDdr2State *sdram = PPC4xx_SDRAM_DDR2(dev);

    sdram->addr = 0;
    sdram->mcopt2 = 0;
}

static void ppc4xx_sdram_ddr2_realize(DeviceState *dev, Error **errp)
{
    Ppc4xxSdramDdr2State *s = PPC4xx_SDRAM_DDR2(dev);
    Ppc4xxDcrDeviceState *dcr = PPC4xx_DCR_DEVICE(dev);
    /*
     * SoC also has 4 GiB but that causes problem with 32 bit
     * builds (4*GiB overflows the 32 bit ram_addr_t).
     */
    const ram_addr_t valid_bank_sizes[] = {
        2 * GiB, 1 * GiB, 512 * MiB, 256 * MiB, 128 * MiB,
        64 * MiB, 32 * MiB, 16 * MiB, 8 * MiB, 0
    };

    if (s->nbanks < 1 || s->nbanks > 4) {
        error_setg(errp, "Invalid number of RAM banks");
        return;
    }
    if (!s->dram_mr) {
        error_setg(errp, "Missing dram memory region");
        return;
    }
    ppc4xx_sdram_banks(s->dram_mr, s->nbanks, s->bank, valid_bank_sizes);

    ppc4xx_dcr_register(dcr, SDRAM0_CFGADDR,
                        s, &sdram_ddr2_dcr_read, &sdram_ddr2_dcr_write);
    ppc4xx_dcr_register(dcr, SDRAM0_CFGDATA,
                        s, &sdram_ddr2_dcr_read, &sdram_ddr2_dcr_write);

    ppc4xx_dcr_register(dcr, SDRAM_R0BAS,
                        s, &sdram_ddr2_dcr_read, &sdram_ddr2_dcr_write);
    ppc4xx_dcr_register(dcr, SDRAM_R1BAS,
                        s, &sdram_ddr2_dcr_read, &sdram_ddr2_dcr_write);
    ppc4xx_dcr_register(dcr, SDRAM_R2BAS,
                        s, &sdram_ddr2_dcr_read, &sdram_ddr2_dcr_write);
    ppc4xx_dcr_register(dcr, SDRAM_R3BAS,
                        s, &sdram_ddr2_dcr_read, &sdram_ddr2_dcr_write);
    ppc4xx_dcr_register(dcr, SDRAM_CONF1HB,
                        s, &sdram_ddr2_dcr_read, &sdram_ddr2_dcr_write);
    ppc4xx_dcr_register(dcr, SDRAM_PLBADDULL,
                        s, &sdram_ddr2_dcr_read, &sdram_ddr2_dcr_write);
    ppc4xx_dcr_register(dcr, SDRAM_CONF1LL,
                        s, &sdram_ddr2_dcr_read, &sdram_ddr2_dcr_write);
    ppc4xx_dcr_register(dcr, SDRAM_CONFPATHB,
                        s, &sdram_ddr2_dcr_read, &sdram_ddr2_dcr_write);
    ppc4xx_dcr_register(dcr, SDRAM_PLBADDUHB,
                        s, &sdram_ddr2_dcr_read, &sdram_ddr2_dcr_write);
}

static Property ppc4xx_sdram_ddr2_props[] = {
    DEFINE_PROP_LINK("dram", Ppc4xxSdramDdr2State, dram_mr, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_UINT32("nbanks", Ppc4xxSdramDdr2State, nbanks, 4),
    DEFINE_PROP_END_OF_LIST(),
};

static void ppc4xx_sdram_ddr2_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = ppc4xx_sdram_ddr2_realize;
    dc->reset = ppc4xx_sdram_ddr2_reset;
    /* Reason: only works as function of a ppc4xx SoC */
    dc->user_creatable = false;
    device_class_set_props(dc, ppc4xx_sdram_ddr2_props);
}

void ppc4xx_sdram_ddr2_enable(Ppc4xxSdramDdr2State *s)
{
    sdram_ddr2_dcr_write(s, SDRAM0_CFGADDR, 0x21);
    sdram_ddr2_dcr_write(s, SDRAM0_CFGDATA, 0x08000000);
}

static const TypeInfo ppc4xx_sdram_types[] = {
    {
        .name           = TYPE_PPC4xx_SDRAM_DDR,
        .parent         = TYPE_PPC4xx_DCR_DEVICE,
        .instance_size  = sizeof(Ppc4xxSdramDdrState),
        .class_init     = ppc4xx_sdram_ddr_class_init,
    }, {
        .name           = TYPE_PPC4xx_SDRAM_DDR2,
        .parent         = TYPE_PPC4xx_DCR_DEVICE,
        .instance_size  = sizeof(Ppc4xxSdramDdr2State),
        .class_init     = ppc4xx_sdram_ddr2_class_init,
    }
};

DEFINE_TYPES(ppc4xx_sdram_types)
