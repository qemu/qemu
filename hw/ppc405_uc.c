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
#include "vl.h"

extern int loglevel;
extern FILE *logfile;

#define DEBUG_MMIO
#define DEBUG_OPBA
#define DEBUG_SDRAM
#define DEBUG_GPIO
#define DEBUG_SERIAL
#define DEBUG_OCM
#define DEBUG_I2C
#define DEBUG_UIC
#define DEBUG_CLOCKS
#define DEBUG_UNASSIGNED

/*****************************************************************************/
/* Generic PowerPC 405 processor instanciation */
CPUState *ppc405_init (const unsigned char *cpu_model,
                       clk_setup_t *cpu_clk, clk_setup_t *tb_clk,
                       uint32_t sysclk)
{
    CPUState *env;
    ppc_def_t *def;

    /* init CPUs */
    env = cpu_init();
    qemu_register_reset(&cpu_ppc_reset, env);
    register_savevm("cpu", 0, 3, cpu_save, cpu_load, env);
    ppc_find_by_name(cpu_model, &def);
    if (def == NULL) {
        cpu_abort(env, "Unable to find PowerPC %s CPU definition\n",
                  cpu_model);
    }
    cpu_ppc_register(env, def);
    cpu_clk->cb = NULL; /* We don't care about CPU clock frequency changes */
    cpu_clk->opaque = env;
    /* Set time-base frequency to sysclk */
    tb_clk->cb = ppc_emb_timers_init(env, sysclk);
    tb_clk->opaque = env;
    ppc_dcr_init(env, NULL, NULL);

    return env;
}

/*****************************************************************************/
/* Shared peripherals */

/*****************************************************************************/
/* Fake device used to map multiple devices in a single memory page */
#define MMIO_AREA_BITS 8
#define MMIO_AREA_LEN (1 << MMIO_AREA_BITS)
#define MMIO_AREA_NB (1 << (TARGET_PAGE_BITS - MMIO_AREA_BITS))
#define MMIO_IDX(addr) (((addr) >> MMIO_AREA_BITS) & (MMIO_AREA_NB - 1))
struct ppc4xx_mmio_t {
    uint32_t base;
    CPUReadMemoryFunc **mem_read[MMIO_AREA_NB];
    CPUWriteMemoryFunc **mem_write[MMIO_AREA_NB];
    void *opaque[MMIO_AREA_NB];
};

static uint32_t unassigned_mem_readb (void *opaque, target_phys_addr_t addr)
{
#ifdef DEBUG_UNASSIGNED
    printf("Unassigned mem read 0x" PADDRX "\n", addr);
#endif

    return 0;
}

static void unassigned_mem_writeb (void *opaque,
                                   target_phys_addr_t addr, uint32_t val)
{
#ifdef DEBUG_UNASSIGNED
    printf("Unassigned mem write 0x" PADDRX " = 0x%x\n", addr, val);
#endif
}

static CPUReadMemoryFunc *unassigned_mem_read[3] = {
    unassigned_mem_readb,
    unassigned_mem_readb,
    unassigned_mem_readb,
};

static CPUWriteMemoryFunc *unassigned_mem_write[3] = {
    unassigned_mem_writeb,
    unassigned_mem_writeb,
    unassigned_mem_writeb,
};

static uint32_t mmio_readlen (ppc4xx_mmio_t *mmio,
                              target_phys_addr_t addr, int len)
{
    CPUReadMemoryFunc **mem_read;
    uint32_t ret;
    int idx;

    idx = MMIO_IDX(addr - mmio->base);
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

    idx = MMIO_IDX(addr - mmio->base);
#if defined(DEBUG_MMIO)
    printf("%s: mmio %p len %d addr " PADDRX " idx %d value %08x\n", __func__,
           mmio, len, addr, idx, value);
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
    printf("%s: addr " PADDRX " val %08x\n", __func__, addr, value);
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
    printf("%s: addr " PADDRX " val %08x\n", __func__, addr, value);
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
    printf("%s: addr " PADDRX " val %08x\n", __func__, addr, value);
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
                          uint32_t offset, uint32_t len,
                          CPUReadMemoryFunc **mem_read,
                          CPUWriteMemoryFunc **mem_write, void *opaque)
{
    uint32_t end;
    int idx, eidx;

    if ((offset + len) > TARGET_PAGE_SIZE)
        return -1;
    idx = MMIO_IDX(offset);
    end = offset + len - 1;
    eidx = MMIO_IDX(end);
#if defined(DEBUG_MMIO)
    printf("%s: offset %08x len %08x %08x %d %d\n", __func__, offset, len,
           end, idx, eidx);
#endif
    for (; idx <= eidx; idx++) {
        mmio->mem_read[idx] = mem_read;
        mmio->mem_write[idx] = mem_write;
        mmio->opaque[idx] = opaque;
    }

    return 0;
}

ppc4xx_mmio_t *ppc4xx_mmio_init (CPUState *env, uint32_t base)
{
    ppc4xx_mmio_t *mmio;
    int mmio_memory;

    mmio = qemu_mallocz(sizeof(ppc4xx_mmio_t));
    if (mmio != NULL) {
        mmio->base = base;
        mmio_memory = cpu_register_io_memory(0, mmio_read, mmio_write, mmio);
#if defined(DEBUG_MMIO)
        printf("%s: %p base %08x len %08x %d\n", __func__,
               mmio, base, TARGET_PAGE_SIZE, mmio_memory);
#endif
        cpu_register_physical_memory(base, TARGET_PAGE_SIZE, mmio_memory);
        ppc4xx_mmio_register(env, mmio, 0, TARGET_PAGE_SIZE,
                             unassigned_mem_read, unassigned_mem_write, NULL);
    }

    return mmio;
}

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

static target_ulong dcr_read_plb (void *opaque, int dcrn)
{
    ppc4xx_plb_t *plb;
    target_ulong ret;

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

static void dcr_write_plb (void *opaque, int dcrn, target_ulong val)
{
    ppc4xx_plb_t *plb;

    plb = opaque;
    switch (dcrn) {
    case PLB0_ACR:
        plb->acr = val & 0xFC000000;
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

void ppc4xx_plb_init (CPUState *env)
{
    ppc4xx_plb_t *plb;

    plb = qemu_mallocz(sizeof(ppc4xx_plb_t));
    if (plb != NULL) {
        ppc_dcr_register(env, PLB0_ACR, plb, &dcr_read_plb, &dcr_write_plb);
        ppc_dcr_register(env, PLB0_BEAR, plb, &dcr_read_plb, &dcr_write_plb);
        ppc_dcr_register(env, PLB0_BESR, plb, &dcr_read_plb, &dcr_write_plb);
        ppc4xx_plb_reset(plb);
        qemu_register_reset(ppc4xx_plb_reset, plb);
    }
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

static target_ulong dcr_read_pob (void *opaque, int dcrn)
{
    ppc4xx_pob_t *pob;
    target_ulong ret;

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

static void dcr_write_pob (void *opaque, int dcrn, target_ulong val)
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

void ppc4xx_pob_init (CPUState *env)
{
    ppc4xx_pob_t *pob;

    pob = qemu_mallocz(sizeof(ppc4xx_pob_t));
    if (pob != NULL) {
        ppc_dcr_register(env, POB0_BEAR, pob, &dcr_read_pob, &dcr_write_pob);
        ppc_dcr_register(env, POB0_BESR0, pob, &dcr_read_pob, &dcr_write_pob);
        ppc_dcr_register(env, POB0_BESR1, pob, &dcr_read_pob, &dcr_write_pob);
        qemu_register_reset(ppc4xx_pob_reset, pob);
        ppc4xx_pob_reset(env);
    }
}

/*****************************************************************************/
/* OPB arbitrer */
typedef struct ppc4xx_opba_t ppc4xx_opba_t;
struct ppc4xx_opba_t {
    target_ulong base;
    uint8_t cr;
    uint8_t pr;
};

static uint32_t opba_readb (void *opaque, target_phys_addr_t addr)
{
    ppc4xx_opba_t *opba;
    uint32_t ret;

#ifdef DEBUG_OPBA
    printf("%s: addr " PADDRX "\n", __func__, addr);
#endif
    opba = opaque;
    switch (addr - opba->base) {
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
    printf("%s: addr " PADDRX " val %08x\n", __func__, addr, value);
#endif
    opba = opaque;
    switch (addr - opba->base) {
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
    printf("%s: addr " PADDRX "\n", __func__, addr);
#endif
    ret = opba_readb(opaque, addr) << 8;
    ret |= opba_readb(opaque, addr + 1);

    return ret;
}

static void opba_writew (void *opaque,
                         target_phys_addr_t addr, uint32_t value)
{
#ifdef DEBUG_OPBA
    printf("%s: addr " PADDRX " val %08x\n", __func__, addr, value);
#endif
    opba_writeb(opaque, addr, value >> 8);
    opba_writeb(opaque, addr + 1, value);
}

static uint32_t opba_readl (void *opaque, target_phys_addr_t addr)
{
    uint32_t ret;

#ifdef DEBUG_OPBA
    printf("%s: addr " PADDRX "\n", __func__, addr);
#endif
    ret = opba_readb(opaque, addr) << 24;
    ret |= opba_readb(opaque, addr + 1) << 16;

    return ret;
}

static void opba_writel (void *opaque,
                         target_phys_addr_t addr, uint32_t value)
{
#ifdef DEBUG_OPBA
    printf("%s: addr " PADDRX " val %08x\n", __func__, addr, value);
#endif
    opba_writeb(opaque, addr, value >> 24);
    opba_writeb(opaque, addr + 1, value >> 16);
}

static CPUReadMemoryFunc *opba_read[] = {
    &opba_readb,
    &opba_readw,
    &opba_readl,
};

static CPUWriteMemoryFunc *opba_write[] = {
    &opba_writeb,
    &opba_writew,
    &opba_writel,
};

static void ppc4xx_opba_reset (void *opaque)
{
    ppc4xx_opba_t *opba;

    opba = opaque;
    opba->cr = 0x00; /* No dynamic priorities - park disabled */
    opba->pr = 0x11;
}

void ppc4xx_opba_init (CPUState *env, ppc4xx_mmio_t *mmio, uint32_t offset)
{
    ppc4xx_opba_t *opba;

    opba = qemu_mallocz(sizeof(ppc4xx_opba_t));
    if (opba != NULL) {
        opba->base = mmio->base + offset;
#ifdef DEBUG_OPBA
        printf("%s: offset=%08x\n", __func__, offset);
#endif
        ppc4xx_mmio_register(env, mmio, offset, 0x002,
                             opba_read, opba_write, opba);
        qemu_register_reset(ppc4xx_opba_reset, opba);
        ppc4xx_opba_reset(opba);
    }
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
        fprintf(logfile, "%s: uicsr %08x uicer %08x uiccr %08x\n"
                "   %08x ir %08x cr %08x\n", __func__,
                uic->uicsr, uic->uicer, uic->uiccr,
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
            fprintf(logfile, "Raise UIC critical interrupt - vector %08x\n",
                    uic->uicvr);
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
    mask = 1 << irq_num;
#ifdef DEBUG_UIC
    if (loglevel & CPU_LOG_INT) {
        fprintf(logfile, "%s: irq %d level %d uicsr %08x mask %08x => %08x "
                "%08x\n", __func__, irq_num, level,
                uic->uicsr, mask, uic->uicsr & mask, level << irq_num);
    }
#endif
    if (irq_num < 0 || irq_num > 31)
        return;
    sr = uic->uicsr;
    if (!(uic->uicpr & mask)) {
        /* Negatively asserted IRQ */
        level = level == 0 ? 1 : 0;
    }
    /* Update status register */
    if (uic->uictr & mask) {
        /* Edge sensitive interrupt */
        if (level == 1)
            uic->uicsr |= mask;
    } else {
        /* Level sensitive interrupt */
        if (level == 1)
            uic->uicsr |= mask;
        else
            uic->uicsr &= ~mask;
    }
#ifdef DEBUG_UIC
    if (loglevel & CPU_LOG_INT) {
        fprintf(logfile, "%s: irq %d level %d sr %08x => %08x\n", __func__,
                irq_num, level, uic->uicsr, sr);
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
        ppcuic_trigger_irq(uic);
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

/*****************************************************************************/
/* Code decompression controller */
/* XXX: TODO */

/*****************************************************************************/
/* SDRAM controller */
typedef struct ppc4xx_sdram_t ppc4xx_sdram_t;
struct ppc4xx_sdram_t {
    uint32_t addr;
    int nbanks;
    target_ulong ram_bases[4];
    target_ulong ram_sizes[4];
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

static uint32_t sdram_bcr (target_ulong ram_base, target_ulong ram_size)
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
        printf("%s: invalid RAM size " TARGET_FMT_ld "\n", __func__, ram_size);
        return 0x00000000;
    }
    bcr |= ram_base & 0xFF800000;
    bcr |= 1;

    return bcr;
}

static inline target_ulong sdram_base (uint32_t bcr)
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

static void sdram_set_bcr (uint32_t *bcrp, uint32_t bcr, int enabled)
{
    if (*bcrp & 0x00000001) {
        /* Unmap RAM */
#ifdef DEBUG_SDRAM
        printf("%s: unmap RAM area " ADDRX " " ADDRX "\n", __func__,
               sdram_base(*bcrp), sdram_size(*bcrp));
#endif
        cpu_register_physical_memory(sdram_base(*bcrp), sdram_size(*bcrp),
                                     IO_MEM_UNASSIGNED);
    }
    *bcrp = bcr & 0xFFDEE001;
    if (enabled && (bcr & 0x00000001)) {
#ifdef DEBUG_SDRAM
        printf("%s: Map RAM area " ADDRX " " ADDRX "\n", __func__,
               sdram_base(bcr), sdram_size(bcr));
#endif
        cpu_register_physical_memory(sdram_base(bcr), sdram_size(bcr),
                                     sdram_base(bcr) | IO_MEM_RAM);
    }
}

static void sdram_map_bcr (ppc4xx_sdram_t *sdram)
{
    int i;

    for (i = 0; i < sdram->nbanks; i++) {
        if (sdram->ram_sizes[i] != 0) {
            sdram_set_bcr(&sdram->bcr[i],
                          sdram_bcr(sdram->ram_bases[i], sdram->ram_sizes[i]),
                          1);
        } else {
            sdram_set_bcr(&sdram->bcr[i], 0x00000000, 0);
        }
    }
}

static void sdram_unmap_bcr (ppc4xx_sdram_t *sdram)
{
    int i;

    for (i = 0; i < sdram->nbanks; i++) {
        cpu_register_physical_memory(sdram_base(sdram->bcr[i]),
                                     sdram_size(sdram->bcr[i]),
                                     IO_MEM_UNASSIGNED);
    }
}

static target_ulong dcr_read_sdram (void *opaque, int dcrn)
{
    ppc4xx_sdram_t *sdram;
    target_ulong ret;

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

static void dcr_write_sdram (void *opaque, int dcrn, target_ulong val)
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
            sdram_set_bcr(&sdram->bcr[0], val, sdram->cfg & 0x80000000);
            break;
        case 0x44: /* SDRAM_B1CR */
            sdram_set_bcr(&sdram->bcr[1], val, sdram->cfg & 0x80000000);
            break;
        case 0x48: /* SDRAM_B2CR */
            sdram_set_bcr(&sdram->bcr[2], val, sdram->cfg & 0x80000000);
            break;
        case 0x4C: /* SDRAM_B3CR */
            sdram_set_bcr(&sdram->bcr[3], val, sdram->cfg & 0x80000000);
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
    sdram_unmap_bcr(sdram);
}

void ppc405_sdram_init (CPUState *env, qemu_irq irq, int nbanks,
                        target_ulong *ram_bases, target_ulong *ram_sizes)
{
    ppc4xx_sdram_t *sdram;

    sdram = qemu_mallocz(sizeof(ppc4xx_sdram_t));
    if (sdram != NULL) {
        sdram->irq = irq;
        sdram->nbanks = nbanks;
        memset(sdram->ram_bases, 0, 4 * sizeof(target_ulong));
        memcpy(sdram->ram_bases, ram_bases, nbanks * sizeof(target_ulong));
        memset(sdram->ram_sizes, 0, 4 * sizeof(target_ulong));
        memcpy(sdram->ram_sizes, ram_sizes, nbanks * sizeof(target_ulong));
        sdram_reset(sdram);
        qemu_register_reset(&sdram_reset, sdram);
        ppc_dcr_register(env, SDRAM0_CFGADDR,
                         sdram, &dcr_read_sdram, &dcr_write_sdram);
        ppc_dcr_register(env, SDRAM0_CFGDATA,
                         sdram, &dcr_read_sdram, &dcr_write_sdram);
    }
}

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

static target_ulong dcr_read_ebc (void *opaque, int dcrn)
{
    ppc4xx_ebc_t *ebc;
    target_ulong ret;

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
    default:
        ret = 0x00000000;
        break;
    }

    return ret;
}

static void dcr_write_ebc (void *opaque, int dcrn, target_ulong val)
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
    ebc->cfg = 0x07C00000;
}

void ppc405_ebc_init (CPUState *env)
{
    ppc4xx_ebc_t *ebc;

    ebc = qemu_mallocz(sizeof(ppc4xx_ebc_t));
    if (ebc != NULL) {
        ebc_reset(ebc);
        qemu_register_reset(&ebc_reset, ebc);
        ppc_dcr_register(env, EBC0_CFGADDR,
                         ebc, &dcr_read_ebc, &dcr_write_ebc);
        ppc_dcr_register(env, EBC0_CFGDATA,
                         ebc, &dcr_read_ebc, &dcr_write_ebc);
    }
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

static target_ulong dcr_read_dma (void *opaque, int dcrn)
{
    ppc405_dma_t *dma;

    dma = opaque;

    return 0;
}

static void dcr_write_dma (void *opaque, int dcrn, target_ulong val)
{
    ppc405_dma_t *dma;

    dma = opaque;
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

void ppc405_dma_init (CPUState *env, qemu_irq irqs[4])
{
    ppc405_dma_t *dma;

    dma = qemu_mallocz(sizeof(ppc405_dma_t));
    if (dma != NULL) {
        memcpy(dma->irqs, irqs, 4 * sizeof(qemu_irq));
        ppc405_dma_reset(dma);
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
}

/*****************************************************************************/
/* GPIO */
typedef struct ppc405_gpio_t ppc405_gpio_t;
struct ppc405_gpio_t {
    uint32_t base;
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
    ppc405_gpio_t *gpio;

    gpio = opaque;
#ifdef DEBUG_GPIO
    printf("%s: addr " PADDRX "\n", __func__, addr);
#endif

    return 0;
}

static void ppc405_gpio_writeb (void *opaque,
                                target_phys_addr_t addr, uint32_t value)
{
    ppc405_gpio_t *gpio;

    gpio = opaque;
#ifdef DEBUG_GPIO
    printf("%s: addr " PADDRX " val %08x\n", __func__, addr, value);
#endif
}

static uint32_t ppc405_gpio_readw (void *opaque, target_phys_addr_t addr)
{
    ppc405_gpio_t *gpio;

    gpio = opaque;
#ifdef DEBUG_GPIO
    printf("%s: addr " PADDRX "\n", __func__, addr);
#endif

    return 0;
}

static void ppc405_gpio_writew (void *opaque,
                                target_phys_addr_t addr, uint32_t value)
{
    ppc405_gpio_t *gpio;

    gpio = opaque;
#ifdef DEBUG_GPIO
    printf("%s: addr " PADDRX " val %08x\n", __func__, addr, value);
#endif
}

static uint32_t ppc405_gpio_readl (void *opaque, target_phys_addr_t addr)
{
    ppc405_gpio_t *gpio;

    gpio = opaque;
#ifdef DEBUG_GPIO
    printf("%s: addr " PADDRX "\n", __func__, addr);
#endif

    return 0;
}

static void ppc405_gpio_writel (void *opaque,
                                target_phys_addr_t addr, uint32_t value)
{
    ppc405_gpio_t *gpio;

    gpio = opaque;
#ifdef DEBUG_GPIO
    printf("%s: addr " PADDRX " val %08x\n", __func__, addr, value);
#endif
}

static CPUReadMemoryFunc *ppc405_gpio_read[] = {
    &ppc405_gpio_readb,
    &ppc405_gpio_readw,
    &ppc405_gpio_readl,
};

static CPUWriteMemoryFunc *ppc405_gpio_write[] = {
    &ppc405_gpio_writeb,
    &ppc405_gpio_writew,
    &ppc405_gpio_writel,
};

static void ppc405_gpio_reset (void *opaque)
{
    ppc405_gpio_t *gpio;

    gpio = opaque;
}

void ppc405_gpio_init (CPUState *env, ppc4xx_mmio_t *mmio, uint32_t offset)
{
    ppc405_gpio_t *gpio;

    gpio = qemu_mallocz(sizeof(ppc405_gpio_t));
    if (gpio != NULL) {
        gpio->base = mmio->base + offset;
        ppc405_gpio_reset(gpio);
        qemu_register_reset(&ppc405_gpio_reset, gpio);
#ifdef DEBUG_GPIO
        printf("%s: offset=%08x\n", __func__, offset);
#endif
        ppc4xx_mmio_register(env, mmio, offset, 0x038,
                             ppc405_gpio_read, ppc405_gpio_write, gpio);
    }
}

/*****************************************************************************/
/* Serial ports */
static CPUReadMemoryFunc *serial_mm_read[] = {
    &serial_mm_readb,
    &serial_mm_readw,
    &serial_mm_readl,
};

static CPUWriteMemoryFunc *serial_mm_write[] = {
    &serial_mm_writeb,
    &serial_mm_writew,
    &serial_mm_writel,
};

void ppc405_serial_init (CPUState *env, ppc4xx_mmio_t *mmio,
                         uint32_t offset, qemu_irq irq,
                         CharDriverState *chr)
{
    void *serial;

#ifdef DEBUG_SERIAL
    printf("%s: offset=%08x\n", __func__, offset);
#endif
    serial = serial_mm_init(mmio->base + offset, 0, irq, chr, 0);
    ppc4xx_mmio_register(env, mmio, offset, 0x008,
                         serial_mm_read, serial_mm_write, serial);
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
    target_ulong offset;
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
    printf("OCM update ISA %08x %08x (%08x %08x) DSA %08x %08x (%08x %08x)\n",
           isarc, isacntl, dsarc, dsacntl,
           ocm->isarc, ocm->isacntl, ocm->dsarc, ocm->dsacntl);
#endif
    if (ocm->isarc != isarc ||
        (ocm->isacntl & 0x80000000) != (isacntl & 0x80000000)) {
        if (ocm->isacntl & 0x80000000) {
            /* Unmap previously assigned memory region */
            printf("OCM unmap ISA %08x\n", ocm->isarc);
            cpu_register_physical_memory(ocm->isarc, 0x04000000,
                                         IO_MEM_UNASSIGNED);
        }
        if (isacntl & 0x80000000) {
            /* Map new instruction memory region */
#ifdef DEBUG_OCM
            printf("OCM map ISA %08x\n", isarc);
#endif
            cpu_register_physical_memory(isarc, 0x04000000,
                                         ocm->offset | IO_MEM_RAM);
        }
    }
    if (ocm->dsarc != dsarc ||
        (ocm->dsacntl & 0x80000000) != (dsacntl & 0x80000000)) {
        if (ocm->dsacntl & 0x80000000) {
            /* Beware not to unmap the region we just mapped */
            if (!(isacntl & 0x80000000) || ocm->dsarc != isarc) {
                /* Unmap previously assigned memory region */
#ifdef DEBUG_OCM
                printf("OCM unmap DSA %08x\n", ocm->dsarc);
#endif
                cpu_register_physical_memory(ocm->dsarc, 0x04000000,
                                             IO_MEM_UNASSIGNED);
            }
        }
        if (dsacntl & 0x80000000) {
            /* Beware not to remap the region we just mapped */
            if (!(isacntl & 0x80000000) || dsarc != isarc) {
                /* Map new data memory region */
#ifdef DEBUG_OCM
                printf("OCM map DSA %08x\n", dsarc);
#endif
                cpu_register_physical_memory(dsarc, 0x04000000,
                                             ocm->offset | IO_MEM_RAM);
            }
        }
    }
}

static target_ulong dcr_read_ocm (void *opaque, int dcrn)
{
    ppc405_ocm_t *ocm;
    target_ulong ret;

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

static void dcr_write_ocm (void *opaque, int dcrn, target_ulong val)
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

void ppc405_ocm_init (CPUState *env, unsigned long offset)
{
    ppc405_ocm_t *ocm;

    ocm = qemu_mallocz(sizeof(ppc405_ocm_t));
    if (ocm != NULL) {
        ocm->offset = offset;
        ocm_reset(ocm);
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
}

/*****************************************************************************/
/* I2C controller */
typedef struct ppc4xx_i2c_t ppc4xx_i2c_t;
struct ppc4xx_i2c_t {
    uint32_t base;
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
    printf("%s: addr " PADDRX "\n", __func__, addr);
#endif
    i2c = opaque;
    switch (addr - i2c->base) {
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
    printf("%s: addr " PADDRX " %02x\n", __func__, addr, ret);
#endif

    return ret;
}

static void ppc4xx_i2c_writeb (void *opaque,
                               target_phys_addr_t addr, uint32_t value)
{
    ppc4xx_i2c_t *i2c;

#ifdef DEBUG_I2C
    printf("%s: addr " PADDRX " val %08x\n", __func__, addr, value);
#endif
    i2c = opaque;
    switch (addr - i2c->base) {
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
    printf("%s: addr " PADDRX "\n", __func__, addr);
#endif
    ret = ppc4xx_i2c_readb(opaque, addr) << 8;
    ret |= ppc4xx_i2c_readb(opaque, addr + 1);

    return ret;
}

static void ppc4xx_i2c_writew (void *opaque,
                               target_phys_addr_t addr, uint32_t value)
{
#ifdef DEBUG_I2C
    printf("%s: addr " PADDRX " val %08x\n", __func__, addr, value);
#endif
    ppc4xx_i2c_writeb(opaque, addr, value >> 8);
    ppc4xx_i2c_writeb(opaque, addr + 1, value);
}

static uint32_t ppc4xx_i2c_readl (void *opaque, target_phys_addr_t addr)
{
    uint32_t ret;

#ifdef DEBUG_I2C
    printf("%s: addr " PADDRX "\n", __func__, addr);
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
    printf("%s: addr " PADDRX " val %08x\n", __func__, addr, value);
#endif
    ppc4xx_i2c_writeb(opaque, addr, value >> 24);
    ppc4xx_i2c_writeb(opaque, addr + 1, value >> 16);
    ppc4xx_i2c_writeb(opaque, addr + 2, value >> 8);
    ppc4xx_i2c_writeb(opaque, addr + 3, value);
}

static CPUReadMemoryFunc *i2c_read[] = {
    &ppc4xx_i2c_readb,
    &ppc4xx_i2c_readw,
    &ppc4xx_i2c_readl,
};

static CPUWriteMemoryFunc *i2c_write[] = {
    &ppc4xx_i2c_writeb,
    &ppc4xx_i2c_writew,
    &ppc4xx_i2c_writel,
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

void ppc405_i2c_init (CPUState *env, ppc4xx_mmio_t *mmio, uint32_t offset)
{
    ppc4xx_i2c_t *i2c;

    i2c = qemu_mallocz(sizeof(ppc4xx_i2c_t));
    if (i2c != NULL) {
        i2c->base = mmio->base + offset;
        ppc4xx_i2c_reset(i2c);
#ifdef DEBUG_I2C
        printf("%s: offset=%08x\n", __func__, offset);
#endif
        ppc4xx_mmio_register(env, mmio, offset, 0x011,
                             i2c_read, i2c_write, i2c);
        qemu_register_reset(ppc4xx_i2c_reset, i2c);
    }
}

/*****************************************************************************/
/* SPR */
void ppc40x_core_reset (CPUState *env)
{
    target_ulong dbsr;

    printf("Reset PowerPC core\n");
    cpu_ppc_reset(env);
    dbsr = env->spr[SPR_40x_DBSR];
    dbsr &= ~0x00000300;
    dbsr |= 0x00000100;
    env->spr[SPR_40x_DBSR] = dbsr;
    cpu_loop_exit();
}

void ppc40x_chip_reset (CPUState *env)
{
    target_ulong dbsr;

    printf("Reset PowerPC chip\n");
    cpu_ppc_reset(env);
    /* XXX: TODO reset all internal peripherals */
    dbsr = env->spr[SPR_40x_DBSR];
    dbsr &= ~0x00000300;
    dbsr |= 0x00000100;
    env->spr[SPR_40x_DBSR] = dbsr;
    cpu_loop_exit();
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

typedef struct ppc405cr_cpc_t ppc405cr_cpc_t;
struct ppc405cr_cpc_t {
    clk_setup_t clk_setup[7];
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
    clk_setup(&cpc->clk_setup[0], CPU_clk);
    /* Setup time-base clock */
    clk_setup(&cpc->clk_setup[1], TMR_clk);
    /* Setup PLB clock */
    clk_setup(&cpc->clk_setup[2], PLB_clk);
    /* Setup SDRAM clock */
    clk_setup(&cpc->clk_setup[3], SDRAM_clk);
    /* Setup OPB clock */
    clk_setup(&cpc->clk_setup[4], OPB_clk);
    /* Setup external clock */
    clk_setup(&cpc->clk_setup[5], EXT_clk);
    /* Setup UART clock */
    clk_setup(&cpc->clk_setup[6], UART_clk);
}

static target_ulong dcr_read_crcpc (void *opaque, int dcrn)
{
    ppc405cr_cpc_t *cpc;
    target_ulong ret;

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

static void dcr_write_crcpc (void *opaque, int dcrn, target_ulong val)
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

    cpc = qemu_mallocz(sizeof(ppc405cr_cpc_t));
    if (cpc != NULL) {
        memcpy(cpc->clk_setup, clk_setup, 7 * sizeof(clk_setup_t));
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
        ppc405cr_cpc_reset(cpc);
    }
}

CPUState *ppc405cr_init (target_ulong ram_bases[4], target_ulong ram_sizes[4],
                         uint32_t sysclk, qemu_irq **picp,
                         ram_addr_t *offsetp)
{
    clk_setup_t clk_setup[7];
    qemu_irq dma_irqs[4];
    CPUState *env;
    ppc4xx_mmio_t *mmio;
    qemu_irq *pic, *irqs;
    ram_addr_t offset;
    int i;

    memset(clk_setup, 0, sizeof(clk_setup));
    env = ppc405_init("405cr", &clk_setup[0], &clk_setup[1], sysclk);
    /* Memory mapped devices registers */
    mmio = ppc4xx_mmio_init(env, 0xEF600000);
    /* PLB arbitrer */
    ppc4xx_plb_init(env);
    /* PLB to OPB bridge */
    ppc4xx_pob_init(env);
    /* OBP arbitrer */
    ppc4xx_opba_init(env, mmio, 0x600);
    /* Universal interrupt controller */
    irqs = qemu_mallocz(sizeof(qemu_irq) * PPCUIC_OUTPUT_NB);
    irqs[PPCUIC_OUTPUT_INT] =
        ((qemu_irq *)env->irq_inputs)[PPC405_INPUT_INT];
    irqs[PPCUIC_OUTPUT_CINT] =
        ((qemu_irq *)env->irq_inputs)[PPC405_INPUT_CINT];
    pic = ppcuic_init(env, irqs, 0x0C0, 0, 1);
    *picp = pic;
    /* SDRAM controller */
    ppc405_sdram_init(env, pic[17], 1, ram_bases, ram_sizes);
    offset = 0;
    for (i = 0; i < 4; i++)
        offset += ram_sizes[i];
    /* External bus controller */
    ppc405_ebc_init(env);
    /* DMA controller */
    dma_irqs[0] = pic[5];
    dma_irqs[1] = pic[6];
    dma_irqs[2] = pic[7];
    dma_irqs[3] = pic[8];
    ppc405_dma_init(env, dma_irqs);
    /* Serial ports */
    if (serial_hds[0] != NULL) {
        ppc405_serial_init(env, mmio, 0x400, pic[0], serial_hds[0]);
    }
    if (serial_hds[1] != NULL) {
        ppc405_serial_init(env, mmio, 0x300, pic[1], serial_hds[1]);
    }
    /* IIC controller */
    ppc405_i2c_init(env, mmio, 0x500);
    /* GPIO */
    ppc405_gpio_init(env, mmio, 0x700);
    /* CPU control */
    ppc405cr_cpc_init(env, clk_setup, sysclk);
    *offsetp = offset;

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
};

typedef struct ppc405ep_cpc_t ppc405ep_cpc_t;
struct ppc405ep_cpc_t {
    uint32_t sysclk;
    clk_setup_t clk_setup[8];
    uint32_t boot;
    uint32_t epctl;
    uint32_t pllmr[2];
    uint32_t ucr;
    uint32_t srr;
    uint32_t jtagid;
    uint32_t pci;
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
        //        printf("FBMUL %01x %d\n", (cpc->pllmr[1] >> 20) & 0xF, M);
        D = 8 - ((cpc->pllmr[1] >> 16) & 0x7); /* FWDA */
        //        printf("FWDA %01x %d\n", (cpc->pllmr[1] >> 16) & 0x7, D);
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
    } else {
#if 0
    pll_bypass:
#endif
        PLL_out = cpc->sysclk;
    }
    /* Now, compute all other clocks */
    D = ((cpc->pllmr[0] >> 20) & 0x3) + 1; /* CCDV */
#ifdef DEBUG_CLOCKS
    //    printf("CCDV %01x %d\n", (cpc->pllmr[0] >> 20) & 0x3, D);
#endif
    CPU_clk = PLL_out / D;
    D = ((cpc->pllmr[0] >> 16) & 0x3) + 1; /* CBDV */
#ifdef DEBUG_CLOCKS
    //    printf("CBDV %01x %d\n", (cpc->pllmr[0] >> 16) & 0x3, D);
#endif
    PLB_clk = CPU_clk / D;
    D = ((cpc->pllmr[0] >> 12) & 0x3) + 1; /* OPDV */
#ifdef DEBUG_CLOCKS
    //    printf("OPDV %01x %d\n", (cpc->pllmr[0] >> 12) & 0x3, D);
#endif
    OPB_clk = PLB_clk / D;
    D = ((cpc->pllmr[0] >> 8) & 0x3) + 2; /* EPDV */
#ifdef DEBUG_CLOCKS
    //    printf("EPDV %01x %d\n", (cpc->pllmr[0] >> 8) & 0x3, D);
#endif
    EBC_clk = PLB_clk / D;
    D = ((cpc->pllmr[0] >> 4) & 0x3) + 1; /* MPDV */
#ifdef DEBUG_CLOCKS
    //    printf("MPDV %01x %d\n", (cpc->pllmr[0] >> 4) & 0x3, D);
#endif
    MAL_clk = PLB_clk / D;
    D = (cpc->pllmr[0] & 0x3) + 1; /* PPDV */
#ifdef DEBUG_CLOCKS
    //    printf("PPDV %01x %d\n", cpc->pllmr[0] & 0x3, D);
#endif
    PCI_clk = PLB_clk / D;
    D = ((cpc->ucr - 1) & 0x7F) + 1; /* U0DIV */
#ifdef DEBUG_CLOCKS
    //    printf("U0DIV %01x %d\n", cpc->ucr & 0x7F, D);
#endif
    UART0_clk = PLL_out / D;
    D = (((cpc->ucr >> 8) - 1) & 0x7F) + 1; /* U1DIV */
#ifdef DEBUG_CLOCKS
    //    printf("U1DIV %01x %d\n", (cpc->ucr >> 8) & 0x7F, D);
#endif
    UART1_clk = PLL_out / D;
#ifdef DEBUG_CLOCKS
    printf("Setup PPC405EP clocks - sysclk %d VCO %" PRIu64
           " PLL out %" PRIu64 " Hz\n", cpc->sysclk, VCO_out, PLL_out);
    printf("CPU %d PLB %d OPB %d EBC %d MAL %d PCI %d UART0 %d UART1 %d\n",
           CPU_clk, PLB_clk, OPB_clk, EBC_clk, MAL_clk, PCI_clk,
           UART0_clk, UART1_clk);
#endif
    /* Setup CPU clocks */
    clk_setup(&cpc->clk_setup[0], CPU_clk);
    /* Setup PLB clock */
    clk_setup(&cpc->clk_setup[1], PLB_clk);
    /* Setup OPB clock */
    clk_setup(&cpc->clk_setup[2], OPB_clk);
    /* Setup external clock */
    clk_setup(&cpc->clk_setup[3], EBC_clk);
    /* Setup MAL clock */
    clk_setup(&cpc->clk_setup[4], MAL_clk);
    /* Setup PCI clock */
    clk_setup(&cpc->clk_setup[5], PCI_clk);
    /* Setup UART0 clock */
    clk_setup(&cpc->clk_setup[6], UART0_clk);
    /* Setup UART1 clock */
    clk_setup(&cpc->clk_setup[7], UART0_clk);
}

static target_ulong dcr_read_epcpc (void *opaque, int dcrn)
{
    ppc405ep_cpc_t *cpc;
    target_ulong ret;

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

static void dcr_write_epcpc (void *opaque, int dcrn, target_ulong val)
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
    ppc405ep_compute_clocks(cpc);
}

/* XXX: sysclk should be between 25 and 100 MHz */
static void ppc405ep_cpc_init (CPUState *env, clk_setup_t clk_setup[8],
                               uint32_t sysclk)
{
    ppc405ep_cpc_t *cpc;

    cpc = qemu_mallocz(sizeof(ppc405ep_cpc_t));
    if (cpc != NULL) {
        memcpy(cpc->clk_setup, clk_setup, 7 * sizeof(clk_setup_t));
        cpc->jtagid = 0x20267049;
        cpc->sysclk = sysclk;
        ppc405ep_cpc_reset(cpc);
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
    }
}

CPUState *ppc405ep_init (target_ulong ram_bases[2], target_ulong ram_sizes[2],
                         uint32_t sysclk, qemu_irq **picp,
                         ram_addr_t *offsetp)
{
    clk_setup_t clk_setup[8];
    qemu_irq dma_irqs[4];
    CPUState *env;
    ppc4xx_mmio_t *mmio;
    qemu_irq *pic, *irqs;
    ram_addr_t offset;
    int i;

    memset(clk_setup, 0, sizeof(clk_setup));
    /* init CPUs */
    env = ppc405_init("405ep", &clk_setup[0], &clk_setup[1], sysclk);
    /* Internal devices init */
    /* Memory mapped devices registers */
    mmio = ppc4xx_mmio_init(env, 0xEF600000);
    /* PLB arbitrer */
    ppc4xx_plb_init(env);
    /* PLB to OPB bridge */
    ppc4xx_pob_init(env);
    /* OBP arbitrer */
    ppc4xx_opba_init(env, mmio, 0x600);
    /* Universal interrupt controller */
    irqs = qemu_mallocz(sizeof(qemu_irq) * PPCUIC_OUTPUT_NB);
    irqs[PPCUIC_OUTPUT_INT] =
        ((qemu_irq *)env->irq_inputs)[PPC405_INPUT_INT];
    irqs[PPCUIC_OUTPUT_CINT] =
        ((qemu_irq *)env->irq_inputs)[PPC405_INPUT_CINT];
    pic = ppcuic_init(env, irqs, 0x0C0, 0, 1);
    *picp = pic;
    /* SDRAM controller */
    ppc405_sdram_init(env, pic[17], 2, ram_bases, ram_sizes);
    offset = 0;
    for (i = 0; i < 2; i++)
        offset += ram_sizes[i];
    /* External bus controller */
    ppc405_ebc_init(env);
    /* DMA controller */
    dma_irqs[0] = pic[5];
    dma_irqs[1] = pic[6];
    dma_irqs[2] = pic[7];
    dma_irqs[3] = pic[8];
    ppc405_dma_init(env, dma_irqs);
    /* IIC controller */
    ppc405_i2c_init(env, mmio, 0x500);
    /* GPIO */
    ppc405_gpio_init(env, mmio, 0x700);
    /* Serial ports */
    if (serial_hds[0] != NULL) {
        ppc405_serial_init(env, mmio, 0x300, pic[0], serial_hds[0]);
    }
    if (serial_hds[1] != NULL) {
        ppc405_serial_init(env, mmio, 0x400, pic[1], serial_hds[1]);
    }
    /* OCM */
    ppc405_ocm_init(env, ram_sizes[0] + ram_sizes[1]);
    offset += 4096;
    /* PCI */
    /* CPU control */
    ppc405ep_cpc_init(env, clk_setup, sysclk);
    *offsetp = offset;

    return env;
}
