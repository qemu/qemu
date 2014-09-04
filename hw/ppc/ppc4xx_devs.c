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
#include "hw/hw.h"
#include "hw/ppc/ppc.h"
#include "hw/ppc/ppc4xx.h"
#include "hw/boards.h"
#include "qemu/log.h"
#include "exec/address-spaces.h"

#define DEBUG_UIC


#ifdef DEBUG_UIC
#  define LOG_UIC(...) qemu_log_mask(CPU_LOG_INT, ## __VA_ARGS__)
#else
#  define LOG_UIC(...) do { } while (0)
#endif

static void ppc4xx_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    cpu_reset(CPU(cpu));
}

/*****************************************************************************/
/* Generic PowerPC 4xx processor instantiation */
PowerPCCPU *ppc4xx_init(const char *cpu_model,
                        clk_setup_t *cpu_clk, clk_setup_t *tb_clk,
                        uint32_t sysclk)
{
    PowerPCCPU *cpu;
    CPUPPCState *env;

    /* init CPUs */
    cpu = cpu_ppc_init(cpu_model);
    if (cpu == NULL) {
        fprintf(stderr, "Unable to find PowerPC %s CPU definition\n",
                cpu_model);
        exit(1);
    }
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
/* "Universal" Interrupt controller */
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

#define UIC_MAX_IRQ 32
typedef struct ppcuic_t ppcuic_t;
struct ppcuic_t {
    uint32_t dcr_base;
    int use_vectors;
    uint32_t level;  /* Remembers the state of level-triggered interrupts. */
    uint32_t uicsr;  /* Status register */
    uint32_t uicer;  /* Enable register */
    uint32_t uiccr;  /* Critical register */
    uint32_t uicpr;  /* Polarity register */
    uint32_t uictr;  /* Triggering register */
    uint32_t uicvcr; /* Vector configuration register */
    uint32_t uicvr;
    qemu_irq *irqs;
};

static void ppcuic_trigger_irq (ppcuic_t *uic)
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
        qemu_irq_raise(uic->irqs[PPCUIC_OUTPUT_INT]);
    } else {
        LOG_UIC("Lower UIC interrupt\n");
        qemu_irq_lower(uic->irqs[PPCUIC_OUTPUT_INT]);
    }
    /* Trigger critical interrupt if any is pending and update vector */
    if (cr != 0x0000000) {
        qemu_irq_raise(uic->irqs[PPCUIC_OUTPUT_CINT]);
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
        qemu_irq_lower(uic->irqs[PPCUIC_OUTPUT_CINT]);
        uic->uicvr = 0x00000000;
    }
}

static void ppcuic_set_irq (void *opaque, int irq_num, int level)
{
    ppcuic_t *uic;
    uint32_t mask, sr;

    uic = opaque;
    mask = 1U << (31-irq_num);
    LOG_UIC("%s: irq %d level %d uicsr %08" PRIx32
                " mask %08" PRIx32 " => %08" PRIx32 " %08" PRIx32 "\n",
                __func__, irq_num, level,
                uic->uicsr, mask, uic->uicsr & mask, level << irq_num);
    if (irq_num < 0 || irq_num > 31)
        return;
    sr = uic->uicsr;

    /* Update status register */
    if (uic->uictr & mask) {
        /* Edge sensitive interrupt */
        if (level == 1)
            uic->uicsr |= mask;
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
    if (sr != uic->uicsr)
        ppcuic_trigger_irq(uic);
}

static uint32_t dcr_read_uic (void *opaque, int dcrn)
{
    ppcuic_t *uic;
    uint32_t ret;

    uic = opaque;
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
        if (!uic->use_vectors)
            goto no_read;
        ret = uic->uicvr;
        break;
    case DCR_UICVCR:
        if (!uic->use_vectors)
            goto no_read;
        ret = uic->uicvcr;
        break;
    default:
    no_read:
        ret = 0x00000000;
        break;
    }

    return ret;
}

static void dcr_write_uic (void *opaque, int dcrn, uint32_t val)
{
    ppcuic_t *uic;

    uic = opaque;
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

static void ppcuic_reset (void *opaque)
{
    ppcuic_t *uic;

    uic = opaque;
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

qemu_irq *ppcuic_init (CPUPPCState *env, qemu_irq *irqs,
                       uint32_t dcr_base, int has_ssr, int has_vr)
{
    ppcuic_t *uic;
    int i;

    uic = g_malloc0(sizeof(ppcuic_t));
    uic->dcr_base = dcr_base;
    uic->irqs = irqs;
    if (has_vr)
        uic->use_vectors = 1;
    for (i = 0; i < DCR_UICMAX; i++) {
        ppc_dcr_register(env, dcr_base + i, uic,
                         &dcr_read_uic, &dcr_write_uic);
    }
    qemu_register_reset(ppcuic_reset, uic);

    return qemu_allocate_irqs(&ppcuic_set_irq, uic, UIC_MAX_IRQ);
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
    case (4 * 1024 * 1024):
        bcr = 0x00000000;
        break;
    case (8 * 1024 * 1024):
        bcr = 0x00020000;
        break;
    case (16 * 1024 * 1024):
        bcr = 0x00040000;
        break;
    case (32 * 1024 * 1024):
        bcr = 0x00060000;
        break;
    case (64 * 1024 * 1024):
        bcr = 0x00080000;
        break;
    case (128 * 1024 * 1024):
        bcr = 0x000A0000;
        break;
    case (256 * 1024 * 1024):
        bcr = 0x000C0000;
        break;
    default:
        printf("%s: invalid RAM size " TARGET_FMT_plx "\n", __func__,
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
        size = (4 * 1024 * 1024) << sh;

    return size;
}

static void sdram_set_bcr(ppc4xx_sdram_t *sdram,
                          uint32_t *bcrp, uint32_t bcr, int enabled)
{
    unsigned n = bcrp - sdram->bcr;

    if (*bcrp & 0x00000001) {
        /* Unmap RAM */
#ifdef DEBUG_SDRAM
        printf("%s: unmap RAM area " TARGET_FMT_plx " " TARGET_FMT_lx "\n",
               __func__, sdram_base(*bcrp), sdram_size(*bcrp));
#endif
        memory_region_del_subregion(get_system_memory(),
                                    &sdram->containers[n]);
        memory_region_del_subregion(&sdram->containers[n],
                                    &sdram->ram_memories[n]);
        memory_region_destroy(&sdram->containers[n]);
    }
    *bcrp = bcr & 0xFFDEE001;
    if (enabled && (bcr & 0x00000001)) {
#ifdef DEBUG_SDRAM
        printf("%s: Map RAM area " TARGET_FMT_plx " " TARGET_FMT_lx "\n",
               __func__, sdram_base(bcr), sdram_size(bcr));
#endif
        memory_region_init(&sdram->containers[n], NULL, "sdram-containers",
                           sdram_size(bcr));
        memory_region_add_subregion(&sdram->containers[n], 0,
                                    &sdram->ram_memories[n]);
        memory_region_add_subregion(get_system_memory(),
                                    sdram_base(bcr),
                                    &sdram->containers[n]);
    }
}

static void sdram_map_bcr (ppc4xx_sdram_t *sdram)
{
    int i;

    for (i = 0; i < sdram->nbanks; i++) {
        if (sdram->ram_sizes[i] != 0) {
            sdram_set_bcr(sdram,
                          &sdram->bcr[i],
                          sdram_bcr(sdram->ram_bases[i], sdram->ram_sizes[i]),
                          1);
        } else {
            sdram_set_bcr(sdram, &sdram->bcr[i], 0x00000000, 0);
        }
    }
}

static void sdram_unmap_bcr (ppc4xx_sdram_t *sdram)
{
    int i;

    for (i = 0; i < sdram->nbanks; i++) {
#ifdef DEBUG_SDRAM
        printf("%s: Unmap RAM area " TARGET_FMT_plx " " TARGET_FMT_lx "\n",
               __func__, sdram_base(sdram->bcr[i]), sdram_size(sdram->bcr[i]));
#endif
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
#ifdef DEBUG_SDRAM
                printf("%s: enable SDRAM controller\n", __func__);
#endif
                /* validate all RAM mappings */
                sdram_map_bcr(sdram);
                sdram->status &= ~0x80000000;
            } else if ((sdram->cfg & 0x80000000) && !(val & 0x80000000)) {
#ifdef DEBUG_SDRAM
                printf("%s: disable SDRAM controller\n", __func__);
#endif
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
            sdram_set_bcr(sdram, &sdram->bcr[0], val, sdram->cfg & 0x80000000);
            break;
        case 0x44: /* SDRAM_B1CR */
            sdram_set_bcr(sdram, &sdram->bcr[1], val, sdram->cfg & 0x80000000);
            break;
        case 0x48: /* SDRAM_B2CR */
            sdram_set_bcr(sdram, &sdram->bcr[2], val, sdram->cfg & 0x80000000);
            break;
        case 0x4C: /* SDRAM_B3CR */
            sdram_set_bcr(sdram, &sdram->bcr[3], val, sdram->cfg & 0x80000000);
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

    sdram = g_malloc0(sizeof(ppc4xx_sdram_t));
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

/* Fill in consecutive SDRAM banks with 'ram_size' bytes of memory.
 *
 * sdram_bank_sizes[] must be 0-terminated.
 *
 * The 4xx SDRAM controller supports a small number of banks, and each bank
 * must be one of a small set of sizes. The number of banks and the supported
 * sizes varies by SoC. */
ram_addr_t ppc4xx_sdram_adjust(ram_addr_t ram_size, int nr_banks,
                               MemoryRegion ram_memories[],
                               hwaddr ram_bases[],
                               hwaddr ram_sizes[],
                               const unsigned int sdram_bank_sizes[])
{
    MemoryRegion *ram = g_malloc0(sizeof(*ram));
    ram_addr_t size_left = ram_size;
    ram_addr_t base = 0;
    unsigned int bank_size;
    int i;
    int j;

    for (i = 0; i < nr_banks; i++) {
        for (j = 0; sdram_bank_sizes[j] != 0; j++) {
            bank_size = sdram_bank_sizes[j];
            if (bank_size <= size_left) {
                size_left -= bank_size;
            }
        }
        if (!size_left) {
            /* No need to use the remaining banks. */
            break;
        }
    }

    ram_size -= size_left;
    if (size_left) {
        printf("Truncating memory to %d MiB to fit SDRAM controller limits.\n",
               (int)(ram_size >> 20));
    }

    memory_region_allocate_system_memory(ram, NULL, "ppc4xx.sdram", ram_size);

    size_left = ram_size;
    for (i = 0; i < nr_banks && size_left; i++) {
        for (j = 0; sdram_bank_sizes[j] != 0; j++) {
            bank_size = sdram_bank_sizes[j];

            if (bank_size <= size_left) {
                char name[32];
                snprintf(name, sizeof(name), "ppc4xx.sdram%d", i);
                memory_region_init_alias(&ram_memories[i], NULL, name, ram,
                                         base, bank_size);
                ram_bases[i] = base;
                ram_sizes[i] = bank_size;
                base += bank_size;
                size_left -= bank_size;
                break;
            }
        }
    }

    return ram_size;
}
