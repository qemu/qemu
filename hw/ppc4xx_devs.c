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
#include "hw.h"
#include "ppc.h"
#include "ppc4xx.h"
#include "sysemu.h"
#include "qemu-log.h"

//#define DEBUG_MMIO
//#define DEBUG_UNASSIGNED
#define DEBUG_UIC

/*****************************************************************************/
/* Generic PowerPC 4xx processor instanciation */
CPUState *ppc4xx_init (const char *cpu_model,
                       clk_setup_t *cpu_clk, clk_setup_t *tb_clk,
                       uint32_t sysclk)
{
    CPUState *env;

    /* init CPUs */
    env = cpu_init(cpu_model);
    if (!env) {
        fprintf(stderr, "Unable to find PowerPC %s CPU definition\n",
                cpu_model);
        exit(1);
    }
    cpu_clk->cb = NULL; /* We don't care about CPU clock frequency changes */
    cpu_clk->opaque = env;
    /* Set time-base frequency to sysclk */
    tb_clk->cb = ppc_emb_timers_init(env, sysclk);
    tb_clk->opaque = env;
    ppc_dcr_init(env, NULL, NULL);
    /* Register qemu callbacks */
    qemu_register_reset(&cpu_ppc_reset, env);

    return env;
}

/*****************************************************************************/
/* Fake device used to map multiple devices in a single memory page */
#define MMIO_AREA_BITS 8
#define MMIO_AREA_LEN (1 << MMIO_AREA_BITS)
#define MMIO_AREA_NB (1 << (TARGET_PAGE_BITS - MMIO_AREA_BITS))
#define MMIO_IDX(addr) (((addr) >> MMIO_AREA_BITS) & (MMIO_AREA_NB - 1))
struct ppc4xx_mmio_t {
    target_phys_addr_t base;
    CPUReadMemoryFunc **mem_read[MMIO_AREA_NB];
    CPUWriteMemoryFunc **mem_write[MMIO_AREA_NB];
    void *opaque[MMIO_AREA_NB];
};

static uint32_t unassigned_mmio_readb (void *opaque, target_phys_addr_t addr)
{
#ifdef DEBUG_UNASSIGNED
    ppc4xx_mmio_t *mmio;

    mmio = opaque;
    printf("Unassigned mmio read 0x" PADDRX " base " PADDRX "\n",
           addr, mmio->base);
#endif

    return 0;
}

static void unassigned_mmio_writeb (void *opaque,
                                    target_phys_addr_t addr, uint32_t val)
{
#ifdef DEBUG_UNASSIGNED
    ppc4xx_mmio_t *mmio;

    mmio = opaque;
    printf("Unassigned mmio write 0x" PADDRX " = 0x%x base " PADDRX "\n",
           addr, val, mmio->base);
#endif
}

static CPUReadMemoryFunc *unassigned_mmio_read[3] = {
    unassigned_mmio_readb,
    unassigned_mmio_readb,
    unassigned_mmio_readb,
};

static CPUWriteMemoryFunc *unassigned_mmio_write[3] = {
    unassigned_mmio_writeb,
    unassigned_mmio_writeb,
    unassigned_mmio_writeb,
};

static uint32_t mmio_readlen (ppc4xx_mmio_t *mmio,
                              target_phys_addr_t addr, int len)
{
    CPUReadMemoryFunc **mem_read;
    uint32_t ret;
    int idx;

    idx = MMIO_IDX(addr);
#if defined(DEBUG_MMIO)
    printf("%s: mmio %p len %d addr " PADDRX " idx %d\n", __func__,
           mmio, len, addr, idx);
#endif
    mem_read = mmio->mem_read[idx];
    ret = (*mem_read[len])(mmio->opaque[idx], addr);

    return ret;
}

static void mmio_writelen (ppc4xx_mmio_t *mmio,
                           target_phys_addr_t addr, uint32_t value, int len)
{
    CPUWriteMemoryFunc **mem_write;
    int idx;

    idx = MMIO_IDX(addr);
#if defined(DEBUG_MMIO)
    printf("%s: mmio %p len %d addr " PADDRX " idx %d value %08" PRIx32 "\n",
           __func__, mmio, len, addr, idx, value);
#endif
    mem_write = mmio->mem_write[idx];
    (*mem_write[len])(mmio->opaque[idx], addr, value);
}

static uint32_t mmio_readb (void *opaque, target_phys_addr_t addr)
{
#if defined(DEBUG_MMIO)
    printf("%s: addr " PADDRX "\n", __func__, addr);
#endif

    return mmio_readlen(opaque, addr, 0);
}

static void mmio_writeb (void *opaque,
                         target_phys_addr_t addr, uint32_t value)
{
#if defined(DEBUG_MMIO)
    printf("%s: addr " PADDRX " val %08" PRIx32 "\n", __func__, addr, value);
#endif
    mmio_writelen(opaque, addr, value, 0);
}

static uint32_t mmio_readw (void *opaque, target_phys_addr_t addr)
{
#if defined(DEBUG_MMIO)
    printf("%s: addr " PADDRX "\n", __func__, addr);
#endif

    return mmio_readlen(opaque, addr, 1);
}

static void mmio_writew (void *opaque,
                         target_phys_addr_t addr, uint32_t value)
{
#if defined(DEBUG_MMIO)
    printf("%s: addr " PADDRX " val %08" PRIx32 "\n", __func__, addr, value);
#endif
    mmio_writelen(opaque, addr, value, 1);
}

static uint32_t mmio_readl (void *opaque, target_phys_addr_t addr)
{
#if defined(DEBUG_MMIO)
    printf("%s: addr " PADDRX "\n", __func__, addr);
#endif

    return mmio_readlen(opaque, addr, 2);
}

static void mmio_writel (void *opaque,
                         target_phys_addr_t addr, uint32_t value)
{
#if defined(DEBUG_MMIO)
    printf("%s: addr " PADDRX " val %08" PRIx32 "\n", __func__, addr, value);
#endif
    mmio_writelen(opaque, addr, value, 2);
}

static CPUReadMemoryFunc *mmio_read[] = {
    &mmio_readb,
    &mmio_readw,
    &mmio_readl,
};

static CPUWriteMemoryFunc *mmio_write[] = {
    &mmio_writeb,
    &mmio_writew,
    &mmio_writel,
};

int ppc4xx_mmio_register (CPUState *env, ppc4xx_mmio_t *mmio,
                          target_phys_addr_t offset, uint32_t len,
                          CPUReadMemoryFunc **mem_read,
                          CPUWriteMemoryFunc **mem_write, void *opaque)
{
    target_phys_addr_t end;
    int idx, eidx;

    if ((offset + len) > TARGET_PAGE_SIZE)
        return -1;
    idx = MMIO_IDX(offset);
    end = offset + len - 1;
    eidx = MMIO_IDX(end);
#if defined(DEBUG_MMIO)
    printf("%s: offset " PADDRX " len %08" PRIx32 " " PADDRX " %d %d\n",
           __func__, offset, len, end, idx, eidx);
#endif
    for (; idx <= eidx; idx++) {
        mmio->mem_read[idx] = mem_read;
        mmio->mem_write[idx] = mem_write;
        mmio->opaque[idx] = opaque;
    }

    return 0;
}

ppc4xx_mmio_t *ppc4xx_mmio_init (CPUState *env, target_phys_addr_t base)
{
    ppc4xx_mmio_t *mmio;
    int mmio_memory;

    mmio = qemu_mallocz(sizeof(ppc4xx_mmio_t));
    if (mmio != NULL) {
        mmio->base = base;
        mmio_memory = cpu_register_io_memory(0, mmio_read, mmio_write, mmio);
#if defined(DEBUG_MMIO)
        printf("%s: base " PADDRX " len %08x %d\n", __func__,
               base, TARGET_PAGE_SIZE, mmio_memory);
#endif
        cpu_register_physical_memory(base, TARGET_PAGE_SIZE, mmio_memory);
        ppc4xx_mmio_register(env, mmio, 0, TARGET_PAGE_SIZE,
                             unassigned_mmio_read, unassigned_mmio_write,
                             mmio);
    }

    return mmio;
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
#ifdef DEBUG_UIC
    if (loglevel & CPU_LOG_INT) {
        fprintf(logfile, "%s: uicsr %08" PRIx32 " uicer %08" PRIx32
                " uiccr %08" PRIx32 "\n"
                "   %08" PRIx32 " ir %08" PRIx32 " cr %08" PRIx32 "\n",
                __func__, uic->uicsr, uic->uicer, uic->uiccr,
                uic->uicsr & uic->uicer, ir, cr);
    }
#endif
    if (ir != 0x0000000) {
#ifdef DEBUG_UIC
        if (loglevel & CPU_LOG_INT) {
            fprintf(logfile, "Raise UIC interrupt\n");
        }
#endif
        qemu_irq_raise(uic->irqs[PPCUIC_OUTPUT_INT]);
    } else {
#ifdef DEBUG_UIC
        if (loglevel & CPU_LOG_INT) {
            fprintf(logfile, "Lower UIC interrupt\n");
        }
#endif
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
#ifdef DEBUG_UIC
        if (loglevel & CPU_LOG_INT) {
            fprintf(logfile, "Raise UIC critical interrupt - "
                    "vector %08" PRIx32 "\n", uic->uicvr);
        }
#endif
    } else {
#ifdef DEBUG_UIC
        if (loglevel & CPU_LOG_INT) {
            fprintf(logfile, "Lower UIC critical interrupt\n");
        }
#endif
        qemu_irq_lower(uic->irqs[PPCUIC_OUTPUT_CINT]);
        uic->uicvr = 0x00000000;
    }
}

static void ppcuic_set_irq (void *opaque, int irq_num, int level)
{
    ppcuic_t *uic;
    uint32_t mask, sr;

    uic = opaque;
    mask = 1 << (31-irq_num);
#ifdef DEBUG_UIC
    if (loglevel & CPU_LOG_INT) {
        fprintf(logfile, "%s: irq %d level %d uicsr %08" PRIx32
                " mask %08" PRIx32 " => %08" PRIx32 " %08" PRIx32 "\n",
                __func__, irq_num, level,
                uic->uicsr, mask, uic->uicsr & mask, level << irq_num);
    }
#endif
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
#ifdef DEBUG_UIC
    if (loglevel & CPU_LOG_INT) {
        fprintf(logfile, "%s: irq %d level %d sr %" PRIx32 " => "
                "%08" PRIx32 "\n", __func__, irq_num, level, uic->uicsr, sr);
    }
#endif
    if (sr != uic->uicsr)
        ppcuic_trigger_irq(uic);
}

static target_ulong dcr_read_uic (void *opaque, int dcrn)
{
    ppcuic_t *uic;
    target_ulong ret;

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

static void dcr_write_uic (void *opaque, int dcrn, target_ulong val)
{
    ppcuic_t *uic;

    uic = opaque;
    dcrn -= uic->dcr_base;
#ifdef DEBUG_UIC
    if (loglevel & CPU_LOG_INT) {
        fprintf(logfile, "%s: dcr %d val " ADDRX "\n", __func__, dcrn, val);
    }
#endif
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

qemu_irq *ppcuic_init (CPUState *env, qemu_irq *irqs,
                       uint32_t dcr_base, int has_ssr, int has_vr)
{
    ppcuic_t *uic;
    int i;

    uic = qemu_mallocz(sizeof(ppcuic_t));
    if (uic != NULL) {
        uic->dcr_base = dcr_base;
        uic->irqs = irqs;
        if (has_vr)
            uic->use_vectors = 1;
        for (i = 0; i < DCR_UICMAX; i++) {
            ppc_dcr_register(env, dcr_base + i, uic,
                             &dcr_read_uic, &dcr_write_uic);
        }
        qemu_register_reset(ppcuic_reset, uic);
        ppcuic_reset(uic);
    }

    return qemu_allocate_irqs(&ppcuic_set_irq, uic, UIC_MAX_IRQ);
}
