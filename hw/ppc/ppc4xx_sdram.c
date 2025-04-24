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
#include "qemu/error-report.h"
#include "system/address-spaces.h" /* get_system_memory() */
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
static bool ppc4xx_sdram_banks(MemoryRegion *ram, int nr_banks,
                               Ppc4xxSdramBank ram_banks[],
                               const ram_addr_t sdram_bank_sizes[],
                               Error **errp)
{
    ERRP_GUARD();
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
        error_setg(errp, "Invalid SDRAM banks");
        error_append_hint(errp, "at most %d bank%s of %s MiB each supported\n",
                          nr_banks, nr_banks == 1 ? "" : "s", s->str);
        error_append_hint(errp, "Possible valid RAM size: %" PRIi64 " MiB\n",
                  used_size ? used_size / MiB : sdram_bank_sizes[i - 1] / MiB);

        g_string_free(s, true);
        return false;
    }
    return true;
}

static void sdram_bank_map(Ppc4xxSdramBank *bank)
{
    trace_ppc4xx_sdram_map(bank->base, bank->size);
    memory_region_init(&bank->container, NULL, "sdram-container", bank->size);
    memory_region_add_subregion(&bank->container, 0, &bank->ram);
    memory_region_add_subregion(get_system_memory(), bank->base,
                                &bank->container);
}

static void sdram_bank_unmap(Ppc4xxSdramBank *bank)
{
    trace_ppc4xx_sdram_unmap(bank->base, bank->size);
    memory_region_del_subregion(get_system_memory(), &bank->container);
    memory_region_del_subregion(&bank->container, &bank->ram);
    object_unparent(OBJECT(&bank->container));
}

static void sdram_bank_set_bcr(Ppc4xxSdramBank *bank, uint32_t bcr,
                               hwaddr base, hwaddr size, int enabled)
{
    if (memory_region_is_mapped(&bank->container)) {
        sdram_bank_unmap(bank);
    }
    bank->bcr = bcr;
    bank->base = base;
    bank->size = size;
    if (enabled && (bcr & 1)) {
        sdram_bank_map(bank);
    }
}

enum {
    SDRAM0_CFGADDR = 0x010,
    SDRAM0_CFGDATA = 0x011,
};

/*****************************************************************************/
/* DDR SDRAM controller */
#define SDRAM_DDR_BCR_MASK 0xFFDEE001

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

static hwaddr sdram_ddr_size(uint32_t bcr)
{
    int sh = (bcr >> 17) & 0x7;

    if (sh == 7) {
        return -1;
    }

    return (4 * MiB) << sh;
}

static uint32_t sdram_ddr_dcr_read(void *opaque, int dcrn)
{
    Ppc4xxSdramDdrState *s = opaque;
    uint32_t ret;

    switch (dcrn) {
    case SDRAM0_CFGADDR:
        ret = s->addr;
        break;
    case SDRAM0_CFGDATA:
        switch (s->addr) {
        case 0x00: /* SDRAM_BESR0 */
            ret = s->besr0;
            break;
        case 0x08: /* SDRAM_BESR1 */
            ret = s->besr1;
            break;
        case 0x10: /* SDRAM_BEAR */
            ret = s->bear;
            break;
        case 0x20: /* SDRAM_CFG */
            ret = s->cfg;
            break;
        case 0x24: /* SDRAM_STATUS */
            ret = s->status;
            break;
        case 0x30: /* SDRAM_RTR */
            ret = s->rtr;
            break;
        case 0x34: /* SDRAM_PMIT */
            ret = s->pmit;
            break;
        case 0x40: /* SDRAM_B0CR */
            ret = s->bank[0].bcr;
            break;
        case 0x44: /* SDRAM_B1CR */
            ret = s->bank[1].bcr;
            break;
        case 0x48: /* SDRAM_B2CR */
            ret = s->bank[2].bcr;
            break;
        case 0x4C: /* SDRAM_B3CR */
            ret = s->bank[3].bcr;
            break;
        case 0x80: /* SDRAM_TR */
            ret = -1; /* ? */
            break;
        case 0x94: /* SDRAM_ECCCFG */
            ret = s->ecccfg;
            break;
        case 0x98: /* SDRAM_ECCESR */
            ret = s->eccesr;
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
    Ppc4xxSdramDdrState *s = opaque;
    int i;

    switch (dcrn) {
    case SDRAM0_CFGADDR:
        s->addr = val;
        break;
    case SDRAM0_CFGDATA:
        switch (s->addr) {
        case 0x00: /* SDRAM_BESR0 */
            s->besr0 &= ~val;
            break;
        case 0x08: /* SDRAM_BESR1 */
            s->besr1 &= ~val;
            break;
        case 0x10: /* SDRAM_BEAR */
            s->bear = val;
            break;
        case 0x20: /* SDRAM_CFG */
            val &= 0xFFE00000;
            if (!(s->cfg & 0x80000000) && (val & 0x80000000)) {
                trace_ppc4xx_sdram_enable("enable");
                /* validate all RAM mappings */
                for (i = 0; i < s->nbanks; i++) {
                    if (s->bank[i].size) {
                        sdram_bank_set_bcr(&s->bank[i], s->bank[i].bcr,
                                           s->bank[i].base, s->bank[i].size,
                                           1);
                    }
                }
                s->status &= ~0x80000000;
            } else if ((s->cfg & 0x80000000) && !(val & 0x80000000)) {
                trace_ppc4xx_sdram_enable("disable");
                /* invalidate all RAM mappings */
                for (i = 0; i < s->nbanks; i++) {
                    if (s->bank[i].size) {
                        sdram_bank_set_bcr(&s->bank[i], s->bank[i].bcr,
                                           s->bank[i].base, s->bank[i].size,
                                           0);
                    }
                }
                s->status |= 0x80000000;
            }
            if (!(s->cfg & 0x40000000) && (val & 0x40000000)) {
                s->status |= 0x40000000;
            } else if ((s->cfg & 0x40000000) && !(val & 0x40000000)) {
                s->status &= ~0x40000000;
            }
            s->cfg = val;
            break;
        case 0x24: /* SDRAM_STATUS */
            /* Read-only register */
            break;
        case 0x30: /* SDRAM_RTR */
            s->rtr = val & 0x3FF80000;
            break;
        case 0x34: /* SDRAM_PMIT */
            s->pmit = (val & 0xF8000000) | 0x07C00000;
            break;
        case 0x40: /* SDRAM_B0CR */
        case 0x44: /* SDRAM_B1CR */
        case 0x48: /* SDRAM_B2CR */
        case 0x4C: /* SDRAM_B3CR */
            i = (s->addr - 0x40) / 4;
            val &= SDRAM_DDR_BCR_MASK;
            if (s->bank[i].size) {
                sdram_bank_set_bcr(&s->bank[i], val,
                                   sdram_ddr_base(val), sdram_ddr_size(val),
                                   s->cfg & 0x80000000);
            }
            break;
        case 0x80: /* SDRAM_TR */
            s->tr = val & 0x018FC01F;
            break;
        case 0x94: /* SDRAM_ECCCFG */
            s->ecccfg = val & 0x00F00000;
            break;
        case 0x98: /* SDRAM_ECCESR */
            val &= 0xFFF0F000;
            if (s->eccesr == 0 && val != 0) {
                qemu_irq_raise(s->irq);
            } else if (s->eccesr != 0 && val == 0) {
                qemu_irq_lower(s->irq);
            }
            s->eccesr = val;
            break;
        default: /* Error */
            break;
        }
        break;
    }
}

static void ppc4xx_sdram_ddr_reset(DeviceState *dev)
{
    Ppc4xxSdramDdrState *s = PPC4xx_SDRAM_DDR(dev);

    s->addr = 0;
    s->bear = 0;
    s->besr0 = 0; /* No error */
    s->besr1 = 0; /* No error */
    s->cfg = 0;
    s->ecccfg = 0; /* No ECC */
    s->eccesr = 0; /* No error */
    s->pmit = 0x07C00000;
    s->rtr = 0x05F00000;
    s->tr = 0x00854009;
    /* We pre-initialize RAM banks */
    s->status = 0;
    s->cfg = 0x00800000;
}

static void ppc4xx_sdram_ddr_realize(DeviceState *dev, Error **errp)
{
    Ppc4xxSdramDdrState *s = PPC4xx_SDRAM_DDR(dev);
    Ppc4xxDcrDeviceState *dcr = PPC4xx_DCR_DEVICE(dev);
    const ram_addr_t valid_bank_sizes[] = {
        256 * MiB, 128 * MiB, 64 * MiB, 32 * MiB, 16 * MiB, 8 * MiB, 4 * MiB, 0
    };
    int i;

    if (s->nbanks < 1 || s->nbanks > 4) {
        error_setg(errp, "Invalid number of RAM banks");
        return;
    }
    if (!s->dram_mr) {
        error_setg(errp, "Missing dram memory region");
        return;
    }
    if (!ppc4xx_sdram_banks(s->dram_mr, s->nbanks, s->bank,
                            valid_bank_sizes, errp)) {
        return;
    }
    for (i = 0; i < s->nbanks; i++) {
        if (s->bank[i].size) {
            s->bank[i].bcr = sdram_ddr_bcr(s->bank[i].base, s->bank[i].size);
            sdram_bank_set_bcr(&s->bank[i], s->bank[i].bcr,
                               s->bank[i].base, s->bank[i].size, 0);
        } else {
            sdram_bank_set_bcr(&s->bank[i], 0, 0, 0, 0);
        }
        trace_ppc4xx_sdram_init(sdram_ddr_base(s->bank[i].bcr),
                                sdram_ddr_size(s->bank[i].bcr),
                                s->bank[i].bcr);
    }

    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);

    ppc4xx_dcr_register(dcr, SDRAM0_CFGADDR,
                        s, &sdram_ddr_dcr_read, &sdram_ddr_dcr_write);
    ppc4xx_dcr_register(dcr, SDRAM0_CFGDATA,
                        s, &sdram_ddr_dcr_read, &sdram_ddr_dcr_write);
}

static const Property ppc4xx_sdram_ddr_props[] = {
    DEFINE_PROP_LINK("dram", Ppc4xxSdramDdrState, dram_mr, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_UINT32("nbanks", Ppc4xxSdramDdrState, nbanks, 4),
};

static void ppc4xx_sdram_ddr_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = ppc4xx_sdram_ddr_realize;
    device_class_set_legacy_reset(dc, ppc4xx_sdram_ddr_reset);
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
#define SDRAM_DDR2_BCR_MASK 0xffe0ffc1

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
        error_report("invalid RAM size " HWADDR_FMT_plx, ram_size);
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

static hwaddr sdram_ddr2_size(uint32_t bcr)
{
    int sh;

    sh = 1024 - ((bcr >> 6) & 0x3ff);
    return 8 * MiB * sh;
}

static uint32_t sdram_ddr2_dcr_read(void *opaque, int dcrn)
{
    Ppc4xxSdramDdr2State *s = opaque;
    uint32_t ret = 0;

    switch (dcrn) {
    case SDRAM_R0BAS:
    case SDRAM_R1BAS:
    case SDRAM_R2BAS:
    case SDRAM_R3BAS:
        if (s->bank[dcrn - SDRAM_R0BAS].size) {
            ret = sdram_ddr2_bcr(s->bank[dcrn - SDRAM_R0BAS].base,
                                 s->bank[dcrn - SDRAM_R0BAS].size);
        }
        break;
    case SDRAM_CONF1HB:
    case SDRAM_CONF1LL:
    case SDRAM_CONFPATHB:
    case SDRAM_PLBADDULL:
    case SDRAM_PLBADDUHB:
        break;
    case SDRAM0_CFGADDR:
        ret = s->addr;
        break;
    case SDRAM0_CFGDATA:
        switch (s->addr) {
        case 0x14: /* SDRAM_MCSTAT (405EX) */
        case 0x1F:
            ret = 0x80000000;
            break;
        case 0x21: /* SDRAM_MCOPT2 */
            ret = s->mcopt2;
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
    Ppc4xxSdramDdr2State *s = opaque;
    int i;

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
        s->addr = val;
        break;
    case SDRAM0_CFGDATA:
        switch (s->addr) {
        case 0x00: /* B0CR */
            break;
        case 0x21: /* SDRAM_MCOPT2 */
            if (!(s->mcopt2 & SDRAM_DDR2_MCOPT2_DCEN) &&
                (val & SDRAM_DDR2_MCOPT2_DCEN)) {
                trace_ppc4xx_sdram_enable("enable");
                /* validate all RAM mappings */
                for (i = 0; i < s->nbanks; i++) {
                    if (s->bank[i].size) {
                        sdram_bank_set_bcr(&s->bank[i], s->bank[i].bcr,
                                           s->bank[i].base, s->bank[i].size,
                                           1);
                    }
                }
                s->mcopt2 |= SDRAM_DDR2_MCOPT2_DCEN;
            } else if ((s->mcopt2 & SDRAM_DDR2_MCOPT2_DCEN) &&
                       !(val & SDRAM_DDR2_MCOPT2_DCEN)) {
                trace_ppc4xx_sdram_enable("disable");
                /* invalidate all RAM mappings */
                for (i = 0; i < s->nbanks; i++) {
                    if (s->bank[i].size) {
                        sdram_bank_set_bcr(&s->bank[i], s->bank[i].bcr,
                                           s->bank[i].base, s->bank[i].size,
                                           0);
                    }
                }
                s->mcopt2 &= ~SDRAM_DDR2_MCOPT2_DCEN;
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
    Ppc4xxSdramDdr2State *s = PPC4xx_SDRAM_DDR2(dev);

    s->addr = 0;
    s->mcopt2 = 0;
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
    int i;

    if (s->nbanks < 1 || s->nbanks > 4) {
        error_setg(errp, "Invalid number of RAM banks");
        return;
    }
    if (!s->dram_mr) {
        error_setg(errp, "Missing dram memory region");
        return;
    }
    if (!ppc4xx_sdram_banks(s->dram_mr, s->nbanks, s->bank,
                            valid_bank_sizes, errp)) {
        return;
    }
    for (i = 0; i < s->nbanks; i++) {
        if (s->bank[i].size) {
            s->bank[i].bcr = sdram_ddr2_bcr(s->bank[i].base, s->bank[i].size);
            s->bank[i].bcr &= SDRAM_DDR2_BCR_MASK;
            sdram_bank_set_bcr(&s->bank[i], s->bank[i].bcr,
                               s->bank[i].base, s->bank[i].size, 0);
        } else {
            sdram_bank_set_bcr(&s->bank[i], 0, 0, 0, 0);
        }
        trace_ppc4xx_sdram_init(sdram_ddr2_base(s->bank[i].bcr),
                                sdram_ddr2_size(s->bank[i].bcr),
                                s->bank[i].bcr);
    }

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

static const Property ppc4xx_sdram_ddr2_props[] = {
    DEFINE_PROP_LINK("dram", Ppc4xxSdramDdr2State, dram_mr, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_UINT32("nbanks", Ppc4xxSdramDdr2State, nbanks, 4),
};

static void ppc4xx_sdram_ddr2_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = ppc4xx_sdram_ddr2_realize;
    device_class_set_legacy_reset(dc, ppc4xx_sdram_ddr2_reset);
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
