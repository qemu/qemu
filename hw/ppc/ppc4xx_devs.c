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
#include "qemu/units.h"
#include "sysemu/reset.h"
#include "cpu.h"
#include "hw/irq.h"
#include "hw/ppc/ppc.h"
#include "hw/ppc/ppc4xx.h"
#include "hw/intc/ppc-uic.h"
#include "hw/qdev-properties.h"
#include "qemu/log.h"
#include "exec/address-spaces.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "trace.h"

static void ppc4xx_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    cpu_reset(CPU(cpu));
}

/*****************************************************************************/
/* Generic PowerPC 4xx processor instantiation */
PowerPCCPU *ppc4xx_init(const char *cpu_type,
                        clk_setup_t *cpu_clk, clk_setup_t *tb_clk,
                        uint32_t sysclk)
{
    PowerPCCPU *cpu;
    CPUPPCState *env;

    /* init CPUs */
    cpu = POWERPC_CPU(cpu_create(cpu_type));
    env = &cpu->env;

    cpu_clk->cb = NULL; /* We don't care about CPU clock frequency changes */
    cpu_clk->opaque = env;
    /* Set time-base frequency to sysclk */
    tb_clk->cb = ppc_40x_timers_init(env, sysclk, PPC_INTERRUPT_PIT);
    tb_clk->opaque = env;
    ppc_dcr_init(env, NULL, NULL);
    /* Register qemu callbacks */
    qemu_register_reset(ppc4xx_reset, cpu);

    return cpu;
}

/*****************************************************************************/
/* SDRAM controller */
typedef struct ppc4xx_sdram_t ppc4xx_sdram_t;
struct ppc4xx_sdram_t {
    uint32_t addr;
    int nbanks;
    MemoryRegion containers[4]; /* used for clipping */
    MemoryRegion *ram_memories;
    hwaddr ram_bases[4];
    hwaddr ram_sizes[4];
    uint32_t besr0;
    uint32_t besr1;
    uint32_t bear;
    uint32_t cfg;
    uint32_t status;
    uint32_t rtr;
    uint32_t pmit;
    uint32_t bcr[4];
    uint32_t tr;
    uint32_t ecccfg;
    uint32_t eccesr;
    qemu_irq irq;
};

enum {
    SDRAM0_CFGADDR = 0x010,
    SDRAM0_CFGDATA = 0x011,
};

/* XXX: TOFIX: some patches have made this code become inconsistent:
 *      there are type inconsistencies, mixing hwaddr, target_ulong
 *      and uint32_t
 */
static uint32_t sdram_bcr (hwaddr ram_base,
                           hwaddr ram_size)
{
    uint32_t bcr;

    switch (ram_size) {
    case 4 * MiB:
        bcr = 0x00000000;
        break;
    case 8 * MiB:
        bcr = 0x00020000;
        break;
    case 16 * MiB:
        bcr = 0x00040000;
        break;
    case 32 * MiB:
        bcr = 0x00060000;
        break;
    case 64 * MiB:
        bcr = 0x00080000;
        break;
    case 128 * MiB:
        bcr = 0x000A0000;
        break;
    case 256 * MiB:
        bcr = 0x000C0000;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid RAM size 0x%" HWADDR_PRIx "\n", __func__,
                      ram_size);
        return 0x00000000;
    }
    bcr |= ram_base & 0xFF800000;
    bcr |= 1;

    return bcr;
}

static inline hwaddr sdram_base(uint32_t bcr)
{
    return bcr & 0xFF800000;
}

static target_ulong sdram_size (uint32_t bcr)
{
    target_ulong size;
    int sh;

    sh = (bcr >> 17) & 0x7;
    if (sh == 7)
        size = -1;
    else
        size = (4 * MiB) << sh;

    return size;
}

static void sdram_set_bcr(ppc4xx_sdram_t *sdram, int i,
                          uint32_t bcr, int enabled)
{
    if (sdram->bcr[i] & 0x00000001) {
        /* Unmap RAM */
        trace_ppc4xx_sdram_unmap(sdram_base(sdram->bcr[i]),
                                 sdram_size(sdram->bcr[i]));
        memory_region_del_subregion(get_system_memory(),
                                    &sdram->containers[i]);
        memory_region_del_subregion(&sdram->containers[i],
                                    &sdram->ram_memories[i]);
        object_unparent(OBJECT(&sdram->containers[i]));
    }
    sdram->bcr[i] = bcr & 0xFFDEE001;
    if (enabled && (bcr & 0x00000001)) {
        trace_ppc4xx_sdram_unmap(sdram_base(bcr), sdram_size(bcr));
        memory_region_init(&sdram->containers[i], NULL, "sdram-containers",
                           sdram_size(bcr));
        memory_region_add_subregion(&sdram->containers[i], 0,
                                    &sdram->ram_memories[i]);
        memory_region_add_subregion(get_system_memory(),
                                    sdram_base(bcr),
                                    &sdram->containers[i]);
    }
}

static void sdram_map_bcr (ppc4xx_sdram_t *sdram)
{
    int i;

    for (i = 0; i < sdram->nbanks; i++) {
        if (sdram->ram_sizes[i] != 0) {
            sdram_set_bcr(sdram, i, sdram_bcr(sdram->ram_bases[i],
                                              sdram->ram_sizes[i]), 1);
        } else {
            sdram_set_bcr(sdram, i, 0x00000000, 0);
        }
    }
}

static void sdram_unmap_bcr (ppc4xx_sdram_t *sdram)
{
    int i;

    for (i = 0; i < sdram->nbanks; i++) {
        trace_ppc4xx_sdram_unmap(sdram_base(sdram->bcr[i]),
                                 sdram_size(sdram->bcr[i]));
        memory_region_del_subregion(get_system_memory(),
                                    &sdram->ram_memories[i]);
    }
}

static uint32_t dcr_read_sdram (void *opaque, int dcrn)
{
    ppc4xx_sdram_t *sdram;
    uint32_t ret;

    sdram = opaque;
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
            ret = sdram->bcr[0];
            break;
        case 0x44: /* SDRAM_B1CR */
            ret = sdram->bcr[1];
            break;
        case 0x48: /* SDRAM_B2CR */
            ret = sdram->bcr[2];
            break;
        case 0x4C: /* SDRAM_B3CR */
            ret = sdram->bcr[3];
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
        ret = 0x00000000;
        break;
    }

    return ret;
}

static void dcr_write_sdram (void *opaque, int dcrn, uint32_t val)
{
    ppc4xx_sdram_t *sdram;

    sdram = opaque;
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
                sdram_map_bcr(sdram);
                sdram->status &= ~0x80000000;
            } else if ((sdram->cfg & 0x80000000) && !(val & 0x80000000)) {
                trace_ppc4xx_sdram_enable("disable");
                /* invalidate all RAM mappings */
                sdram_unmap_bcr(sdram);
                sdram->status |= 0x80000000;
            }
            if (!(sdram->cfg & 0x40000000) && (val & 0x40000000))
                sdram->status |= 0x40000000;
            else if ((sdram->cfg & 0x40000000) && !(val & 0x40000000))
                sdram->status &= ~0x40000000;
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
            sdram_set_bcr(sdram, 0, val, sdram->cfg & 0x80000000);
            break;
        case 0x44: /* SDRAM_B1CR */
            sdram_set_bcr(sdram, 1, val, sdram->cfg & 0x80000000);
            break;
        case 0x48: /* SDRAM_B2CR */
            sdram_set_bcr(sdram, 2, val, sdram->cfg & 0x80000000);
            break;
        case 0x4C: /* SDRAM_B3CR */
            sdram_set_bcr(sdram, 3, val, sdram->cfg & 0x80000000);
            break;
        case 0x80: /* SDRAM_TR */
            sdram->tr = val & 0x018FC01F;
            break;
        case 0x94: /* SDRAM_ECCCFG */
            sdram->ecccfg = val & 0x00F00000;
            break;
        case 0x98: /* SDRAM_ECCESR */
            val &= 0xFFF0F000;
            if (sdram->eccesr == 0 && val != 0)
                qemu_irq_raise(sdram->irq);
            else if (sdram->eccesr != 0 && val == 0)
                qemu_irq_lower(sdram->irq);
            sdram->eccesr = val;
            break;
        default: /* Error */
            break;
        }
        break;
    }
}

static void sdram_reset (void *opaque)
{
    ppc4xx_sdram_t *sdram;

    sdram = opaque;
    sdram->addr = 0x00000000;
    sdram->bear = 0x00000000;
    sdram->besr0 = 0x00000000; /* No error */
    sdram->besr1 = 0x00000000; /* No error */
    sdram->cfg = 0x00000000;
    sdram->ecccfg = 0x00000000; /* No ECC */
    sdram->eccesr = 0x00000000; /* No error */
    sdram->pmit = 0x07C00000;
    sdram->rtr = 0x05F00000;
    sdram->tr = 0x00854009;
    /* We pre-initialize RAM banks */
    sdram->status = 0x00000000;
    sdram->cfg = 0x00800000;
}

void ppc4xx_sdram_init (CPUPPCState *env, qemu_irq irq, int nbanks,
                        MemoryRegion *ram_memories,
                        hwaddr *ram_bases,
                        hwaddr *ram_sizes,
                        int do_init)
{
    ppc4xx_sdram_t *sdram;

    sdram = g_new0(ppc4xx_sdram_t, 1);
    sdram->irq = irq;
    sdram->nbanks = nbanks;
    sdram->ram_memories = ram_memories;
    memset(sdram->ram_bases, 0, 4 * sizeof(hwaddr));
    memcpy(sdram->ram_bases, ram_bases,
           nbanks * sizeof(hwaddr));
    memset(sdram->ram_sizes, 0, 4 * sizeof(hwaddr));
    memcpy(sdram->ram_sizes, ram_sizes,
           nbanks * sizeof(hwaddr));
    qemu_register_reset(&sdram_reset, sdram);
    ppc_dcr_register(env, SDRAM0_CFGADDR,
                     sdram, &dcr_read_sdram, &dcr_write_sdram);
    ppc_dcr_register(env, SDRAM0_CFGDATA,
                     sdram, &dcr_read_sdram, &dcr_write_sdram);
    if (do_init)
        sdram_map_bcr(sdram);
}

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
void ppc4xx_sdram_banks(MemoryRegion *ram, int nr_banks,
                        MemoryRegion ram_memories[],
                        hwaddr ram_bases[], hwaddr ram_sizes[],
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

                ram_bases[i] = base;
                ram_sizes[i] = bank_size;
                base += bank_size;
                size_left -= bank_size;
                snprintf(name, sizeof(name), "ppc4xx.sdram%d", i);
                memory_region_init_alias(&ram_memories[i], NULL, name, ram,
                                         ram_bases[i], ram_sizes[i]);
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
        error_printf("Possible valid RAM size: %" PRIi64 " MiB \n",
            used_size ? used_size / MiB : sdram_bank_sizes[i - 1] / MiB);

        g_string_free(s, true);
        exit(EXIT_FAILURE);
    }
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
    MAL0_RXCTP0R  = 0x1C0,
    MAL0_RCBS0    = 0x1E0,
    MAL0_RCBS1    = 0x1E1,
};

typedef struct ppc4xx_mal_t ppc4xx_mal_t;
struct ppc4xx_mal_t {
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
    uint32_t *txctpr;
    uint32_t *rxctpr;
    uint32_t *rcbs;
    uint8_t  txcnum;
    uint8_t  rxcnum;
};

static void ppc4xx_mal_reset(void *opaque)
{
    ppc4xx_mal_t *mal;

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

static uint32_t dcr_read_mal(void *opaque, int dcrn)
{
    ppc4xx_mal_t *mal;
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
    ppc4xx_mal_t *mal;

    mal = opaque;
    switch (dcrn) {
    case MAL0_CFG:
        if (val & 0x80000000) {
            ppc4xx_mal_reset(mal);
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

void ppc4xx_mal_init(CPUPPCState *env, uint8_t txcnum, uint8_t rxcnum,
                     qemu_irq irqs[4])
{
    ppc4xx_mal_t *mal;
    int i;

    assert(txcnum <= 32 && rxcnum <= 32);
    mal = g_malloc0(sizeof(*mal));
    mal->txcnum = txcnum;
    mal->rxcnum = rxcnum;
    mal->txctpr = g_new0(uint32_t, txcnum);
    mal->rxctpr = g_new0(uint32_t, rxcnum);
    mal->rcbs = g_new0(uint32_t, rxcnum);
    for (i = 0; i < 4; i++) {
        mal->irqs[i] = irqs[i];
    }
    qemu_register_reset(&ppc4xx_mal_reset, mal);
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
    for (i = 0; i < txcnum; i++) {
        ppc_dcr_register(env, MAL0_TXCTP0R + i,
                         mal, &dcr_read_mal, &dcr_write_mal);
    }
    for (i = 0; i < rxcnum; i++) {
        ppc_dcr_register(env, MAL0_RXCTP0R + i,
                         mal, &dcr_read_mal, &dcr_write_mal);
    }
    for (i = 0; i < rxcnum; i++) {
        ppc_dcr_register(env, MAL0_RCBS0 + i,
                         mal, &dcr_read_mal, &dcr_write_mal);
    }
}
