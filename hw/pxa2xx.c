/*
 * Intel XScale PXA255/270 processor support.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This code is licenced under the GPL.
 */

#include "hw.h"
#include "pxa.h"
#include "sysemu.h"
#include "pc.h"
#include "i2c.h"
#include "qemu-timer.h"
#include "qemu-char.h"

static struct {
    target_phys_addr_t io_base;
    int irqn;
} pxa255_serial[] = {
    { 0x40100000, PXA2XX_PIC_FFUART },
    { 0x40200000, PXA2XX_PIC_BTUART },
    { 0x40700000, PXA2XX_PIC_STUART },
    { 0x41600000, PXA25X_PIC_HWUART },
    { 0, 0 }
}, pxa270_serial[] = {
    { 0x40100000, PXA2XX_PIC_FFUART },
    { 0x40200000, PXA2XX_PIC_BTUART },
    { 0x40700000, PXA2XX_PIC_STUART },
    { 0, 0 }
};

typedef struct PXASSPDef {
    target_phys_addr_t io_base;
    int irqn;
} PXASSPDef;

#if 0
static PXASSPDef pxa250_ssp[] = {
    { 0x41000000, PXA2XX_PIC_SSP },
    { 0, 0 }
};
#endif

static PXASSPDef pxa255_ssp[] = {
    { 0x41000000, PXA2XX_PIC_SSP },
    { 0x41400000, PXA25X_PIC_NSSP },
    { 0, 0 }
};

#if 0
static PXASSPDef pxa26x_ssp[] = {
    { 0x41000000, PXA2XX_PIC_SSP },
    { 0x41400000, PXA25X_PIC_NSSP },
    { 0x41500000, PXA26X_PIC_ASSP },
    { 0, 0 }
};
#endif

static PXASSPDef pxa27x_ssp[] = {
    { 0x41000000, PXA2XX_PIC_SSP },
    { 0x41700000, PXA27X_PIC_SSP2 },
    { 0x41900000, PXA2XX_PIC_SSP3 },
    { 0, 0 }
};

#define PMCR	0x00	/* Power Manager Control register */
#define PSSR	0x04	/* Power Manager Sleep Status register */
#define PSPR	0x08	/* Power Manager Scratch-Pad register */
#define PWER	0x0c	/* Power Manager Wake-Up Enable register */
#define PRER	0x10	/* Power Manager Rising-Edge Detect Enable register */
#define PFER	0x14	/* Power Manager Falling-Edge Detect Enable register */
#define PEDR	0x18	/* Power Manager Edge-Detect Status register */
#define PCFR	0x1c	/* Power Manager General Configuration register */
#define PGSR0	0x20	/* Power Manager GPIO Sleep-State register 0 */
#define PGSR1	0x24	/* Power Manager GPIO Sleep-State register 1 */
#define PGSR2	0x28	/* Power Manager GPIO Sleep-State register 2 */
#define PGSR3	0x2c	/* Power Manager GPIO Sleep-State register 3 */
#define RCSR	0x30	/* Reset Controller Status register */
#define PSLR	0x34	/* Power Manager Sleep Configuration register */
#define PTSR	0x38	/* Power Manager Standby Configuration register */
#define PVCR	0x40	/* Power Manager Voltage Change Control register */
#define PUCR	0x4c	/* Power Manager USIM Card Control/Status register */
#define PKWR	0x50	/* Power Manager Keyboard Wake-Up Enable register */
#define PKSR	0x54	/* Power Manager Keyboard Level-Detect Status */
#define PCMD0	0x80	/* Power Manager I2C Command register File 0 */
#define PCMD31	0xfc	/* Power Manager I2C Command register File 31 */

static uint32_t pxa2xx_pm_read(void *opaque, target_phys_addr_t addr)
{
    struct pxa2xx_state_s *s = (struct pxa2xx_state_s *) opaque;
    addr -= s->pm_base;

    switch (addr) {
    case PMCR ... PCMD31:
        if (addr & 3)
            goto fail;

        return s->pm_regs[addr >> 2];
    default:
    fail:
        printf("%s: Bad register " REG_FMT "\n", __FUNCTION__, addr);
        break;
    }
    return 0;
}

static void pxa2xx_pm_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct pxa2xx_state_s *s = (struct pxa2xx_state_s *) opaque;
    addr -= s->pm_base;

    switch (addr) {
    case PMCR:
        s->pm_regs[addr >> 2] &= 0x15 & ~(value & 0x2a);
        s->pm_regs[addr >> 2] |= value & 0x15;
        break;

    case PSSR:	/* Read-clean registers */
    case RCSR:
    case PKSR:
        s->pm_regs[addr >> 2] &= ~value;
        break;

    default:	/* Read-write registers */
        if (addr >= PMCR && addr <= PCMD31 && !(addr & 3)) {
            s->pm_regs[addr >> 2] = value;
            break;
        }

        printf("%s: Bad register " REG_FMT "\n", __FUNCTION__, addr);
        break;
    }
}

static CPUReadMemoryFunc *pxa2xx_pm_readfn[] = {
    pxa2xx_pm_read,
    pxa2xx_pm_read,
    pxa2xx_pm_read,
};

static CPUWriteMemoryFunc *pxa2xx_pm_writefn[] = {
    pxa2xx_pm_write,
    pxa2xx_pm_write,
    pxa2xx_pm_write,
};

static void pxa2xx_pm_save(QEMUFile *f, void *opaque)
{
    struct pxa2xx_state_s *s = (struct pxa2xx_state_s *) opaque;
    int i;

    for (i = 0; i < 0x40; i ++)
        qemu_put_be32s(f, &s->pm_regs[i]);
}

static int pxa2xx_pm_load(QEMUFile *f, void *opaque, int version_id)
{
    struct pxa2xx_state_s *s = (struct pxa2xx_state_s *) opaque;
    int i;

    for (i = 0; i < 0x40; i ++)
        qemu_get_be32s(f, &s->pm_regs[i]);

    return 0;
}

#define CCCR	0x00	/* Core Clock Configuration register */
#define CKEN	0x04	/* Clock Enable register */
#define OSCC	0x08	/* Oscillator Configuration register */
#define CCSR	0x0c	/* Core Clock Status register */

static uint32_t pxa2xx_cm_read(void *opaque, target_phys_addr_t addr)
{
    struct pxa2xx_state_s *s = (struct pxa2xx_state_s *) opaque;
    addr -= s->cm_base;

    switch (addr) {
    case CCCR:
    case CKEN:
    case OSCC:
        return s->cm_regs[addr >> 2];

    case CCSR:
        return s->cm_regs[CCCR >> 2] | (3 << 28);

    default:
        printf("%s: Bad register " REG_FMT "\n", __FUNCTION__, addr);
        break;
    }
    return 0;
}

static void pxa2xx_cm_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct pxa2xx_state_s *s = (struct pxa2xx_state_s *) opaque;
    addr -= s->cm_base;

    switch (addr) {
    case CCCR:
    case CKEN:
        s->cm_regs[addr >> 2] = value;
        break;

    case OSCC:
        s->cm_regs[addr >> 2] &= ~0x6c;
        s->cm_regs[addr >> 2] |= value & 0x6e;
        if ((value >> 1) & 1)			/* OON */
            s->cm_regs[addr >> 2] |= 1 << 0;	/* Oscillator is now stable */
        break;

    default:
        printf("%s: Bad register " REG_FMT "\n", __FUNCTION__, addr);
        break;
    }
}

static CPUReadMemoryFunc *pxa2xx_cm_readfn[] = {
    pxa2xx_cm_read,
    pxa2xx_cm_read,
    pxa2xx_cm_read,
};

static CPUWriteMemoryFunc *pxa2xx_cm_writefn[] = {
    pxa2xx_cm_write,
    pxa2xx_cm_write,
    pxa2xx_cm_write,
};

static void pxa2xx_cm_save(QEMUFile *f, void *opaque)
{
    struct pxa2xx_state_s *s = (struct pxa2xx_state_s *) opaque;
    int i;

    for (i = 0; i < 4; i ++)
        qemu_put_be32s(f, &s->cm_regs[i]);
    qemu_put_be32s(f, &s->clkcfg);
    qemu_put_be32s(f, &s->pmnc);
}

static int pxa2xx_cm_load(QEMUFile *f, void *opaque, int version_id)
{
    struct pxa2xx_state_s *s = (struct pxa2xx_state_s *) opaque;
    int i;

    for (i = 0; i < 4; i ++)
        qemu_get_be32s(f, &s->cm_regs[i]);
    qemu_get_be32s(f, &s->clkcfg);
    qemu_get_be32s(f, &s->pmnc);

    return 0;
}

static uint32_t pxa2xx_clkpwr_read(void *opaque, int op2, int reg, int crm)
{
    struct pxa2xx_state_s *s = (struct pxa2xx_state_s *) opaque;

    switch (reg) {
    case 6:	/* Clock Configuration register */
        return s->clkcfg;

    case 7:	/* Power Mode register */
        return 0;

    default:
        printf("%s: Bad register 0x%x\n", __FUNCTION__, reg);
        break;
    }
    return 0;
}

static void pxa2xx_clkpwr_write(void *opaque, int op2, int reg, int crm,
                uint32_t value)
{
    struct pxa2xx_state_s *s = (struct pxa2xx_state_s *) opaque;
    static const char *pwrmode[8] = {
        "Normal", "Idle", "Deep-idle", "Standby",
        "Sleep", "reserved (!)", "reserved (!)", "Deep-sleep",
    };

    switch (reg) {
    case 6:	/* Clock Configuration register */
        s->clkcfg = value & 0xf;
        if (value & 2)
            printf("%s: CPU frequency change attempt\n", __FUNCTION__);
        break;

    case 7:	/* Power Mode register */
        if (value & 8)
            printf("%s: CPU voltage change attempt\n", __FUNCTION__);
        switch (value & 7) {
        case 0:
            /* Do nothing */
            break;

        case 1:
            /* Idle */
            if (!(s->cm_regs[CCCR >> 2] & (1 << 31))) {	/* CPDIS */
                cpu_interrupt(s->env, CPU_INTERRUPT_HALT);
                break;
            }
            /* Fall through.  */

        case 2:
            /* Deep-Idle */
            cpu_interrupt(s->env, CPU_INTERRUPT_HALT);
            s->pm_regs[RCSR >> 2] |= 0x8;	/* Set GPR */
            goto message;

        case 3:
            s->env->uncached_cpsr =
                    ARM_CPU_MODE_SVC | CPSR_A | CPSR_F | CPSR_I;
            s->env->cp15.c1_sys = 0;
            s->env->cp15.c1_coproc = 0;
            s->env->cp15.c2_base0 = 0;
            s->env->cp15.c3 = 0;
            s->pm_regs[PSSR >> 2] |= 0x8;	/* Set STS */
            s->pm_regs[RCSR >> 2] |= 0x8;	/* Set GPR */

            /*
             * The scratch-pad register is almost universally used
             * for storing the return address on suspend.  For the
             * lack of a resuming bootloader, perform a jump
             * directly to that address.
             */
            memset(s->env->regs, 0, 4 * 15);
            s->env->regs[15] = s->pm_regs[PSPR >> 2];

#if 0
            buffer = 0xe59ff000;	/* ldr     pc, [pc, #0] */
            cpu_physical_memory_write(0, &buffer, 4);
            buffer = s->pm_regs[PSPR >> 2];
            cpu_physical_memory_write(8, &buffer, 4);
#endif

            /* Suspend */
            cpu_interrupt(cpu_single_env, CPU_INTERRUPT_HALT);

            goto message;

        default:
        message:
            printf("%s: machine entered %s mode\n", __FUNCTION__,
                            pwrmode[value & 7]);
        }
        break;

    default:
        printf("%s: Bad register 0x%x\n", __FUNCTION__, reg);
        break;
    }
}

/* Performace Monitoring Registers */
#define CPPMNC		0	/* Performance Monitor Control register */
#define CPCCNT		1	/* Clock Counter register */
#define CPINTEN		4	/* Interrupt Enable register */
#define CPFLAG		5	/* Overflow Flag register */
#define CPEVTSEL	8	/* Event Selection register */

#define CPPMN0		0	/* Performance Count register 0 */
#define CPPMN1		1	/* Performance Count register 1 */
#define CPPMN2		2	/* Performance Count register 2 */
#define CPPMN3		3	/* Performance Count register 3 */

static uint32_t pxa2xx_perf_read(void *opaque, int op2, int reg, int crm)
{
    struct pxa2xx_state_s *s = (struct pxa2xx_state_s *) opaque;

    switch (reg) {
    case CPPMNC:
        return s->pmnc;
    case CPCCNT:
        if (s->pmnc & 1)
            return qemu_get_clock(vm_clock);
        else
            return 0;
    case CPINTEN:
    case CPFLAG:
    case CPEVTSEL:
        return 0;

    default:
        printf("%s: Bad register 0x%x\n", __FUNCTION__, reg);
        break;
    }
    return 0;
}

static void pxa2xx_perf_write(void *opaque, int op2, int reg, int crm,
                uint32_t value)
{
    struct pxa2xx_state_s *s = (struct pxa2xx_state_s *) opaque;

    switch (reg) {
    case CPPMNC:
        s->pmnc = value;
        break;

    case CPCCNT:
    case CPINTEN:
    case CPFLAG:
    case CPEVTSEL:
        break;

    default:
        printf("%s: Bad register 0x%x\n", __FUNCTION__, reg);
        break;
    }
}

static uint32_t pxa2xx_cp14_read(void *opaque, int op2, int reg, int crm)
{
    switch (crm) {
    case 0:
        return pxa2xx_clkpwr_read(opaque, op2, reg, crm);
    case 1:
        return pxa2xx_perf_read(opaque, op2, reg, crm);
    case 2:
        switch (reg) {
        case CPPMN0:
        case CPPMN1:
        case CPPMN2:
        case CPPMN3:
            return 0;
        }
        /* Fall through */
    default:
        printf("%s: Bad register 0x%x\n", __FUNCTION__, reg);
        break;
    }
    return 0;
}

static void pxa2xx_cp14_write(void *opaque, int op2, int reg, int crm,
                uint32_t value)
{
    switch (crm) {
    case 0:
        pxa2xx_clkpwr_write(opaque, op2, reg, crm, value);
        break;
    case 1:
        pxa2xx_perf_write(opaque, op2, reg, crm, value);
        break;
    case 2:
        switch (reg) {
        case CPPMN0:
        case CPPMN1:
        case CPPMN2:
        case CPPMN3:
            return;
        }
        /* Fall through */
    default:
        printf("%s: Bad register 0x%x\n", __FUNCTION__, reg);
        break;
    }
}

#define MDCNFG		0x00	/* SDRAM Configuration register */
#define MDREFR		0x04	/* SDRAM Refresh Control register */
#define MSC0		0x08	/* Static Memory Control register 0 */
#define MSC1		0x0c	/* Static Memory Control register 1 */
#define MSC2		0x10	/* Static Memory Control register 2 */
#define MECR		0x14	/* Expansion Memory Bus Config register */
#define SXCNFG		0x1c	/* Synchronous Static Memory Config register */
#define MCMEM0		0x28	/* PC Card Memory Socket 0 Timing register */
#define MCMEM1		0x2c	/* PC Card Memory Socket 1 Timing register */
#define MCATT0		0x30	/* PC Card Attribute Socket 0 register */
#define MCATT1		0x34	/* PC Card Attribute Socket 1 register */
#define MCIO0		0x38	/* PC Card I/O Socket 0 Timing register */
#define MCIO1		0x3c	/* PC Card I/O Socket 1 Timing register */
#define MDMRS		0x40	/* SDRAM Mode Register Set Config register */
#define BOOT_DEF	0x44	/* Boot-time Default Configuration register */
#define ARB_CNTL	0x48	/* Arbiter Control register */
#define BSCNTR0		0x4c	/* Memory Buffer Strength Control register 0 */
#define BSCNTR1		0x50	/* Memory Buffer Strength Control register 1 */
#define LCDBSCNTR	0x54	/* LCD Buffer Strength Control register */
#define MDMRSLP		0x58	/* Low Power SDRAM Mode Set Config register */
#define BSCNTR2		0x5c	/* Memory Buffer Strength Control register 2 */
#define BSCNTR3		0x60	/* Memory Buffer Strength Control register 3 */
#define SA1110		0x64	/* SA-1110 Memory Compatibility register */

static uint32_t pxa2xx_mm_read(void *opaque, target_phys_addr_t addr)
{
    struct pxa2xx_state_s *s = (struct pxa2xx_state_s *) opaque;
    addr -= s->mm_base;

    switch (addr) {
    case MDCNFG ... SA1110:
        if ((addr & 3) == 0)
            return s->mm_regs[addr >> 2];

    default:
        printf("%s: Bad register " REG_FMT "\n", __FUNCTION__, addr);
        break;
    }
    return 0;
}

static void pxa2xx_mm_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct pxa2xx_state_s *s = (struct pxa2xx_state_s *) opaque;
    addr -= s->mm_base;

    switch (addr) {
    case MDCNFG ... SA1110:
        if ((addr & 3) == 0) {
            s->mm_regs[addr >> 2] = value;
            break;
        }

    default:
        printf("%s: Bad register " REG_FMT "\n", __FUNCTION__, addr);
        break;
    }
}

static CPUReadMemoryFunc *pxa2xx_mm_readfn[] = {
    pxa2xx_mm_read,
    pxa2xx_mm_read,
    pxa2xx_mm_read,
};

static CPUWriteMemoryFunc *pxa2xx_mm_writefn[] = {
    pxa2xx_mm_write,
    pxa2xx_mm_write,
    pxa2xx_mm_write,
};

static void pxa2xx_mm_save(QEMUFile *f, void *opaque)
{
    struct pxa2xx_state_s *s = (struct pxa2xx_state_s *) opaque;
    int i;

    for (i = 0; i < 0x1a; i ++)
        qemu_put_be32s(f, &s->mm_regs[i]);
}

static int pxa2xx_mm_load(QEMUFile *f, void *opaque, int version_id)
{
    struct pxa2xx_state_s *s = (struct pxa2xx_state_s *) opaque;
    int i;

    for (i = 0; i < 0x1a; i ++)
        qemu_get_be32s(f, &s->mm_regs[i]);

    return 0;
}

/* Synchronous Serial Ports */
struct pxa2xx_ssp_s {
    target_phys_addr_t base;
    qemu_irq irq;
    int enable;

    uint32_t sscr[2];
    uint32_t sspsp;
    uint32_t ssto;
    uint32_t ssitr;
    uint32_t sssr;
    uint8_t sstsa;
    uint8_t ssrsa;
    uint8_t ssacd;

    uint32_t rx_fifo[16];
    int rx_level;
    int rx_start;

    uint32_t (*readfn)(void *opaque);
    void (*writefn)(void *opaque, uint32_t value);
    void *opaque;
};

#define SSCR0	0x00	/* SSP Control register 0 */
#define SSCR1	0x04	/* SSP Control register 1 */
#define SSSR	0x08	/* SSP Status register */
#define SSITR	0x0c	/* SSP Interrupt Test register */
#define SSDR	0x10	/* SSP Data register */
#define SSTO	0x28	/* SSP Time-Out register */
#define SSPSP	0x2c	/* SSP Programmable Serial Protocol register */
#define SSTSA	0x30	/* SSP TX Time Slot Active register */
#define SSRSA	0x34	/* SSP RX Time Slot Active register */
#define SSTSS	0x38	/* SSP Time Slot Status register */
#define SSACD	0x3c	/* SSP Audio Clock Divider register */

/* Bitfields for above registers */
#define SSCR0_SPI(x)	(((x) & 0x30) == 0x00)
#define SSCR0_SSP(x)	(((x) & 0x30) == 0x10)
#define SSCR0_UWIRE(x)	(((x) & 0x30) == 0x20)
#define SSCR0_PSP(x)	(((x) & 0x30) == 0x30)
#define SSCR0_SSE	(1 << 7)
#define SSCR0_RIM	(1 << 22)
#define SSCR0_TIM	(1 << 23)
#define SSCR0_MOD	(1 << 31)
#define SSCR0_DSS(x)	(((((x) >> 16) & 0x10) | ((x) & 0xf)) + 1)
#define SSCR1_RIE	(1 << 0)
#define SSCR1_TIE	(1 << 1)
#define SSCR1_LBM	(1 << 2)
#define SSCR1_MWDS	(1 << 5)
#define SSCR1_TFT(x)	((((x) >> 6) & 0xf) + 1)
#define SSCR1_RFT(x)	((((x) >> 10) & 0xf) + 1)
#define SSCR1_EFWR	(1 << 14)
#define SSCR1_PINTE	(1 << 18)
#define SSCR1_TINTE	(1 << 19)
#define SSCR1_RSRE	(1 << 20)
#define SSCR1_TSRE	(1 << 21)
#define SSCR1_EBCEI	(1 << 29)
#define SSITR_INT	(7 << 5)
#define SSSR_TNF	(1 << 2)
#define SSSR_RNE	(1 << 3)
#define SSSR_TFS	(1 << 5)
#define SSSR_RFS	(1 << 6)
#define SSSR_ROR	(1 << 7)
#define SSSR_PINT	(1 << 18)
#define SSSR_TINT	(1 << 19)
#define SSSR_EOC	(1 << 20)
#define SSSR_TUR	(1 << 21)
#define SSSR_BCE	(1 << 23)
#define SSSR_RW		0x00bc0080

static void pxa2xx_ssp_int_update(struct pxa2xx_ssp_s *s)
{
    int level = 0;

    level |= s->ssitr & SSITR_INT;
    level |= (s->sssr & SSSR_BCE)  &&  (s->sscr[1] & SSCR1_EBCEI);
    level |= (s->sssr & SSSR_TUR)  && !(s->sscr[0] & SSCR0_TIM);
    level |= (s->sssr & SSSR_EOC)  &&  (s->sssr & (SSSR_TINT | SSSR_PINT));
    level |= (s->sssr & SSSR_TINT) &&  (s->sscr[1] & SSCR1_TINTE);
    level |= (s->sssr & SSSR_PINT) &&  (s->sscr[1] & SSCR1_PINTE);
    level |= (s->sssr & SSSR_ROR)  && !(s->sscr[0] & SSCR0_RIM);
    level |= (s->sssr & SSSR_RFS)  &&  (s->sscr[1] & SSCR1_RIE);
    level |= (s->sssr & SSSR_TFS)  &&  (s->sscr[1] & SSCR1_TIE);
    qemu_set_irq(s->irq, !!level);
}

static void pxa2xx_ssp_fifo_update(struct pxa2xx_ssp_s *s)
{
    s->sssr &= ~(0xf << 12);	/* Clear RFL */
    s->sssr &= ~(0xf << 8);	/* Clear TFL */
    s->sssr &= ~SSSR_TNF;
    if (s->enable) {
        s->sssr |= ((s->rx_level - 1) & 0xf) << 12;
        if (s->rx_level >= SSCR1_RFT(s->sscr[1]))
            s->sssr |= SSSR_RFS;
        else
            s->sssr &= ~SSSR_RFS;
        if (0 <= SSCR1_TFT(s->sscr[1]))
            s->sssr |= SSSR_TFS;
        else
            s->sssr &= ~SSSR_TFS;
        if (s->rx_level)
            s->sssr |= SSSR_RNE;
        else
            s->sssr &= ~SSSR_RNE;
        s->sssr |= SSSR_TNF;
    }

    pxa2xx_ssp_int_update(s);
}

static uint32_t pxa2xx_ssp_read(void *opaque, target_phys_addr_t addr)
{
    struct pxa2xx_ssp_s *s = (struct pxa2xx_ssp_s *) opaque;
    uint32_t retval;
    addr -= s->base;

    switch (addr) {
    case SSCR0:
        return s->sscr[0];
    case SSCR1:
        return s->sscr[1];
    case SSPSP:
        return s->sspsp;
    case SSTO:
        return s->ssto;
    case SSITR:
        return s->ssitr;
    case SSSR:
        return s->sssr | s->ssitr;
    case SSDR:
        if (!s->enable)
            return 0xffffffff;
        if (s->rx_level < 1) {
            printf("%s: SSP Rx Underrun\n", __FUNCTION__);
            return 0xffffffff;
        }
        s->rx_level --;
        retval = s->rx_fifo[s->rx_start ++];
        s->rx_start &= 0xf;
        pxa2xx_ssp_fifo_update(s);
        return retval;
    case SSTSA:
        return s->sstsa;
    case SSRSA:
        return s->ssrsa;
    case SSTSS:
        return 0;
    case SSACD:
        return s->ssacd;
    default:
        printf("%s: Bad register " REG_FMT "\n", __FUNCTION__, addr);
        break;
    }
    return 0;
}

static void pxa2xx_ssp_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct pxa2xx_ssp_s *s = (struct pxa2xx_ssp_s *) opaque;
    addr -= s->base;

    switch (addr) {
    case SSCR0:
        s->sscr[0] = value & 0xc7ffffff;
        s->enable = value & SSCR0_SSE;
        if (value & SSCR0_MOD)
            printf("%s: Attempt to use network mode\n", __FUNCTION__);
        if (s->enable && SSCR0_DSS(value) < 4)
            printf("%s: Wrong data size: %i bits\n", __FUNCTION__,
                            SSCR0_DSS(value));
        if (!(value & SSCR0_SSE)) {
            s->sssr = 0;
            s->ssitr = 0;
            s->rx_level = 0;
        }
        pxa2xx_ssp_fifo_update(s);
        break;

    case SSCR1:
        s->sscr[1] = value;
        if (value & (SSCR1_LBM | SSCR1_EFWR))
            printf("%s: Attempt to use SSP test mode\n", __FUNCTION__);
        pxa2xx_ssp_fifo_update(s);
        break;

    case SSPSP:
        s->sspsp = value;
        break;

    case SSTO:
        s->ssto = value;
        break;

    case SSITR:
        s->ssitr = value & SSITR_INT;
        pxa2xx_ssp_int_update(s);
        break;

    case SSSR:
        s->sssr &= ~(value & SSSR_RW);
        pxa2xx_ssp_int_update(s);
        break;

    case SSDR:
        if (SSCR0_UWIRE(s->sscr[0])) {
            if (s->sscr[1] & SSCR1_MWDS)
                value &= 0xffff;
            else
                value &= 0xff;
        } else
            /* Note how 32bits overflow does no harm here */
            value &= (1 << SSCR0_DSS(s->sscr[0])) - 1;

        /* Data goes from here to the Tx FIFO and is shifted out from
         * there directly to the slave, no need to buffer it.
         */
        if (s->enable) {
            if (s->writefn)
                s->writefn(s->opaque, value);

            if (s->rx_level < 0x10) {
                if (s->readfn)
                    s->rx_fifo[(s->rx_start + s->rx_level ++) & 0xf] =
                            s->readfn(s->opaque);
                else
                    s->rx_fifo[(s->rx_start + s->rx_level ++) & 0xf] = 0x0;
            } else
                s->sssr |= SSSR_ROR;
        }
        pxa2xx_ssp_fifo_update(s);
        break;

    case SSTSA:
        s->sstsa = value;
        break;

    case SSRSA:
        s->ssrsa = value;
        break;

    case SSACD:
        s->ssacd = value;
        break;

    default:
        printf("%s: Bad register " REG_FMT "\n", __FUNCTION__, addr);
        break;
    }
}

void pxa2xx_ssp_attach(struct pxa2xx_ssp_s *port,
                uint32_t (*readfn)(void *opaque),
                void (*writefn)(void *opaque, uint32_t value), void *opaque)
{
    if (!port) {
        printf("%s: no such SSP\n", __FUNCTION__);
        exit(-1);
    }

    port->opaque = opaque;
    port->readfn = readfn;
    port->writefn = writefn;
}

static CPUReadMemoryFunc *pxa2xx_ssp_readfn[] = {
    pxa2xx_ssp_read,
    pxa2xx_ssp_read,
    pxa2xx_ssp_read,
};

static CPUWriteMemoryFunc *pxa2xx_ssp_writefn[] = {
    pxa2xx_ssp_write,
    pxa2xx_ssp_write,
    pxa2xx_ssp_write,
};

static void pxa2xx_ssp_save(QEMUFile *f, void *opaque)
{
    struct pxa2xx_ssp_s *s = (struct pxa2xx_ssp_s *) opaque;
    int i;

    qemu_put_be32(f, s->enable);

    qemu_put_be32s(f, &s->sscr[0]);
    qemu_put_be32s(f, &s->sscr[1]);
    qemu_put_be32s(f, &s->sspsp);
    qemu_put_be32s(f, &s->ssto);
    qemu_put_be32s(f, &s->ssitr);
    qemu_put_be32s(f, &s->sssr);
    qemu_put_8s(f, &s->sstsa);
    qemu_put_8s(f, &s->ssrsa);
    qemu_put_8s(f, &s->ssacd);

    qemu_put_byte(f, s->rx_level);
    for (i = 0; i < s->rx_level; i ++)
        qemu_put_byte(f, s->rx_fifo[(s->rx_start + i) & 0xf]);
}

static int pxa2xx_ssp_load(QEMUFile *f, void *opaque, int version_id)
{
    struct pxa2xx_ssp_s *s = (struct pxa2xx_ssp_s *) opaque;
    int i;

    s->enable = qemu_get_be32(f);

    qemu_get_be32s(f, &s->sscr[0]);
    qemu_get_be32s(f, &s->sscr[1]);
    qemu_get_be32s(f, &s->sspsp);
    qemu_get_be32s(f, &s->ssto);
    qemu_get_be32s(f, &s->ssitr);
    qemu_get_be32s(f, &s->sssr);
    qemu_get_8s(f, &s->sstsa);
    qemu_get_8s(f, &s->ssrsa);
    qemu_get_8s(f, &s->ssacd);

    s->rx_level = qemu_get_byte(f);
    s->rx_start = 0;
    for (i = 0; i < s->rx_level; i ++)
        s->rx_fifo[i] = qemu_get_byte(f);

    return 0;
}

/* Real-Time Clock */
#define RCNR		0x00	/* RTC Counter register */
#define RTAR		0x04	/* RTC Alarm register */
#define RTSR		0x08	/* RTC Status register */
#define RTTR		0x0c	/* RTC Timer Trim register */
#define RDCR		0x10	/* RTC Day Counter register */
#define RYCR		0x14	/* RTC Year Counter register */
#define RDAR1		0x18	/* RTC Wristwatch Day Alarm register 1 */
#define RYAR1		0x1c	/* RTC Wristwatch Year Alarm register 1 */
#define RDAR2		0x20	/* RTC Wristwatch Day Alarm register 2 */
#define RYAR2		0x24	/* RTC Wristwatch Year Alarm register 2 */
#define SWCR		0x28	/* RTC Stopwatch Counter register */
#define SWAR1		0x2c	/* RTC Stopwatch Alarm register 1 */
#define SWAR2		0x30	/* RTC Stopwatch Alarm register 2 */
#define RTCPICR		0x34	/* RTC Periodic Interrupt Counter register */
#define PIAR		0x38	/* RTC Periodic Interrupt Alarm register */

static inline void pxa2xx_rtc_int_update(struct pxa2xx_state_s *s)
{
    qemu_set_irq(s->pic[PXA2XX_PIC_RTCALARM], !!(s->rtsr & 0x2553));
}

static void pxa2xx_rtc_hzupdate(struct pxa2xx_state_s *s)
{
    int64_t rt = qemu_get_clock(rt_clock);
    s->last_rcnr += ((rt - s->last_hz) << 15) /
            (1000 * ((s->rttr & 0xffff) + 1));
    s->last_rdcr += ((rt - s->last_hz) << 15) /
            (1000 * ((s->rttr & 0xffff) + 1));
    s->last_hz = rt;
}

static void pxa2xx_rtc_swupdate(struct pxa2xx_state_s *s)
{
    int64_t rt = qemu_get_clock(rt_clock);
    if (s->rtsr & (1 << 12))
        s->last_swcr += (rt - s->last_sw) / 10;
    s->last_sw = rt;
}

static void pxa2xx_rtc_piupdate(struct pxa2xx_state_s *s)
{
    int64_t rt = qemu_get_clock(rt_clock);
    if (s->rtsr & (1 << 15))
        s->last_swcr += rt - s->last_pi;
    s->last_pi = rt;
}

static inline void pxa2xx_rtc_alarm_update(struct pxa2xx_state_s *s,
                uint32_t rtsr)
{
    if ((rtsr & (1 << 2)) && !(rtsr & (1 << 0)))
        qemu_mod_timer(s->rtc_hz, s->last_hz +
                (((s->rtar - s->last_rcnr) * 1000 *
                  ((s->rttr & 0xffff) + 1)) >> 15));
    else
        qemu_del_timer(s->rtc_hz);

    if ((rtsr & (1 << 5)) && !(rtsr & (1 << 4)))
        qemu_mod_timer(s->rtc_rdal1, s->last_hz +
                (((s->rdar1 - s->last_rdcr) * 1000 *
                  ((s->rttr & 0xffff) + 1)) >> 15)); /* TODO: fixup */
    else
        qemu_del_timer(s->rtc_rdal1);

    if ((rtsr & (1 << 7)) && !(rtsr & (1 << 6)))
        qemu_mod_timer(s->rtc_rdal2, s->last_hz +
                (((s->rdar2 - s->last_rdcr) * 1000 *
                  ((s->rttr & 0xffff) + 1)) >> 15)); /* TODO: fixup */
    else
        qemu_del_timer(s->rtc_rdal2);

    if ((rtsr & 0x1200) == 0x1200 && !(rtsr & (1 << 8)))
        qemu_mod_timer(s->rtc_swal1, s->last_sw +
                        (s->swar1 - s->last_swcr) * 10); /* TODO: fixup */
    else
        qemu_del_timer(s->rtc_swal1);

    if ((rtsr & 0x1800) == 0x1800 && !(rtsr & (1 << 10)))
        qemu_mod_timer(s->rtc_swal2, s->last_sw +
                        (s->swar2 - s->last_swcr) * 10); /* TODO: fixup */
    else
        qemu_del_timer(s->rtc_swal2);

    if ((rtsr & 0xc000) == 0xc000 && !(rtsr & (1 << 13)))
        qemu_mod_timer(s->rtc_pi, s->last_pi +
                        (s->piar & 0xffff) - s->last_rtcpicr);
    else
        qemu_del_timer(s->rtc_pi);
}

static inline void pxa2xx_rtc_hz_tick(void *opaque)
{
    struct pxa2xx_state_s *s = (struct pxa2xx_state_s *) opaque;
    s->rtsr |= (1 << 0);
    pxa2xx_rtc_alarm_update(s, s->rtsr);
    pxa2xx_rtc_int_update(s);
}

static inline void pxa2xx_rtc_rdal1_tick(void *opaque)
{
    struct pxa2xx_state_s *s = (struct pxa2xx_state_s *) opaque;
    s->rtsr |= (1 << 4);
    pxa2xx_rtc_alarm_update(s, s->rtsr);
    pxa2xx_rtc_int_update(s);
}

static inline void pxa2xx_rtc_rdal2_tick(void *opaque)
{
    struct pxa2xx_state_s *s = (struct pxa2xx_state_s *) opaque;
    s->rtsr |= (1 << 6);
    pxa2xx_rtc_alarm_update(s, s->rtsr);
    pxa2xx_rtc_int_update(s);
}

static inline void pxa2xx_rtc_swal1_tick(void *opaque)
{
    struct pxa2xx_state_s *s = (struct pxa2xx_state_s *) opaque;
    s->rtsr |= (1 << 8);
    pxa2xx_rtc_alarm_update(s, s->rtsr);
    pxa2xx_rtc_int_update(s);
}

static inline void pxa2xx_rtc_swal2_tick(void *opaque)
{
    struct pxa2xx_state_s *s = (struct pxa2xx_state_s *) opaque;
    s->rtsr |= (1 << 10);
    pxa2xx_rtc_alarm_update(s, s->rtsr);
    pxa2xx_rtc_int_update(s);
}

static inline void pxa2xx_rtc_pi_tick(void *opaque)
{
    struct pxa2xx_state_s *s = (struct pxa2xx_state_s *) opaque;
    s->rtsr |= (1 << 13);
    pxa2xx_rtc_piupdate(s);
    s->last_rtcpicr = 0;
    pxa2xx_rtc_alarm_update(s, s->rtsr);
    pxa2xx_rtc_int_update(s);
}

static uint32_t pxa2xx_rtc_read(void *opaque, target_phys_addr_t addr)
{
    struct pxa2xx_state_s *s = (struct pxa2xx_state_s *) opaque;
    addr -= s->rtc_base;

    switch (addr) {
    case RTTR:
        return s->rttr;
    case RTSR:
        return s->rtsr;
    case RTAR:
        return s->rtar;
    case RDAR1:
        return s->rdar1;
    case RDAR2:
        return s->rdar2;
    case RYAR1:
        return s->ryar1;
    case RYAR2:
        return s->ryar2;
    case SWAR1:
        return s->swar1;
    case SWAR2:
        return s->swar2;
    case PIAR:
        return s->piar;
    case RCNR:
        return s->last_rcnr + ((qemu_get_clock(rt_clock) - s->last_hz) << 15) /
                (1000 * ((s->rttr & 0xffff) + 1));
    case RDCR:
        return s->last_rdcr + ((qemu_get_clock(rt_clock) - s->last_hz) << 15) /
                (1000 * ((s->rttr & 0xffff) + 1));
    case RYCR:
        return s->last_rycr;
    case SWCR:
        if (s->rtsr & (1 << 12))
            return s->last_swcr + (qemu_get_clock(rt_clock) - s->last_sw) / 10;
        else
            return s->last_swcr;
    default:
        printf("%s: Bad register " REG_FMT "\n", __FUNCTION__, addr);
        break;
    }
    return 0;
}

static void pxa2xx_rtc_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct pxa2xx_state_s *s = (struct pxa2xx_state_s *) opaque;
    addr -= s->rtc_base;

    switch (addr) {
    case RTTR:
        if (!(s->rttr & (1 << 31))) {
            pxa2xx_rtc_hzupdate(s);
            s->rttr = value;
            pxa2xx_rtc_alarm_update(s, s->rtsr);
        }
        break;

    case RTSR:
        if ((s->rtsr ^ value) & (1 << 15))
            pxa2xx_rtc_piupdate(s);

        if ((s->rtsr ^ value) & (1 << 12))
            pxa2xx_rtc_swupdate(s);

        if (((s->rtsr ^ value) & 0x4aac) | (value & ~0xdaac))
            pxa2xx_rtc_alarm_update(s, value);

        s->rtsr = (value & 0xdaac) | (s->rtsr & ~(value & ~0xdaac));
        pxa2xx_rtc_int_update(s);
        break;

    case RTAR:
        s->rtar = value;
        pxa2xx_rtc_alarm_update(s, s->rtsr);
        break;

    case RDAR1:
        s->rdar1 = value;
        pxa2xx_rtc_alarm_update(s, s->rtsr);
        break;

    case RDAR2:
        s->rdar2 = value;
        pxa2xx_rtc_alarm_update(s, s->rtsr);
        break;

    case RYAR1:
        s->ryar1 = value;
        pxa2xx_rtc_alarm_update(s, s->rtsr);
        break;

    case RYAR2:
        s->ryar2 = value;
        pxa2xx_rtc_alarm_update(s, s->rtsr);
        break;

    case SWAR1:
        pxa2xx_rtc_swupdate(s);
        s->swar1 = value;
        s->last_swcr = 0;
        pxa2xx_rtc_alarm_update(s, s->rtsr);
        break;

    case SWAR2:
        s->swar2 = value;
        pxa2xx_rtc_alarm_update(s, s->rtsr);
        break;

    case PIAR:
        s->piar = value;
        pxa2xx_rtc_alarm_update(s, s->rtsr);
        break;

    case RCNR:
        pxa2xx_rtc_hzupdate(s);
        s->last_rcnr = value;
        pxa2xx_rtc_alarm_update(s, s->rtsr);
        break;

    case RDCR:
        pxa2xx_rtc_hzupdate(s);
        s->last_rdcr = value;
        pxa2xx_rtc_alarm_update(s, s->rtsr);
        break;

    case RYCR:
        s->last_rycr = value;
        break;

    case SWCR:
        pxa2xx_rtc_swupdate(s);
        s->last_swcr = value;
        pxa2xx_rtc_alarm_update(s, s->rtsr);
        break;

    case RTCPICR:
        pxa2xx_rtc_piupdate(s);
        s->last_rtcpicr = value & 0xffff;
        pxa2xx_rtc_alarm_update(s, s->rtsr);
        break;

    default:
        printf("%s: Bad register " REG_FMT "\n", __FUNCTION__, addr);
    }
}

static CPUReadMemoryFunc *pxa2xx_rtc_readfn[] = {
    pxa2xx_rtc_read,
    pxa2xx_rtc_read,
    pxa2xx_rtc_read,
};

static CPUWriteMemoryFunc *pxa2xx_rtc_writefn[] = {
    pxa2xx_rtc_write,
    pxa2xx_rtc_write,
    pxa2xx_rtc_write,
};

static void pxa2xx_rtc_init(struct pxa2xx_state_s *s)
{
    struct tm *tm;
    time_t ti;
    int wom;

    s->rttr = 0x7fff;
    s->rtsr = 0;

    time(&ti);
    if (rtc_utc)
        tm = gmtime(&ti);
    else
        tm = localtime(&ti);
    wom = ((tm->tm_mday - 1) / 7) + 1;

    s->last_rcnr = (uint32_t) ti;
    s->last_rdcr = (wom << 20) | ((tm->tm_wday + 1) << 17) |
            (tm->tm_hour << 12) | (tm->tm_min << 6) | tm->tm_sec;
    s->last_rycr = ((tm->tm_year + 1900) << 9) |
            ((tm->tm_mon + 1) << 5) | tm->tm_mday;
    s->last_swcr = (tm->tm_hour << 19) |
            (tm->tm_min << 13) | (tm->tm_sec << 7);
    s->last_rtcpicr = 0;
    s->last_hz = s->last_sw = s->last_pi = qemu_get_clock(rt_clock);

    s->rtc_hz    = qemu_new_timer(rt_clock, pxa2xx_rtc_hz_tick,    s);
    s->rtc_rdal1 = qemu_new_timer(rt_clock, pxa2xx_rtc_rdal1_tick, s);
    s->rtc_rdal2 = qemu_new_timer(rt_clock, pxa2xx_rtc_rdal2_tick, s);
    s->rtc_swal1 = qemu_new_timer(rt_clock, pxa2xx_rtc_swal1_tick, s);
    s->rtc_swal2 = qemu_new_timer(rt_clock, pxa2xx_rtc_swal2_tick, s);
    s->rtc_pi    = qemu_new_timer(rt_clock, pxa2xx_rtc_pi_tick,    s);
}

static void pxa2xx_rtc_save(QEMUFile *f, void *opaque)
{
    struct pxa2xx_state_s *s = (struct pxa2xx_state_s *) opaque;

    pxa2xx_rtc_hzupdate(s);
    pxa2xx_rtc_piupdate(s);
    pxa2xx_rtc_swupdate(s);

    qemu_put_be32s(f, &s->rttr);
    qemu_put_be32s(f, &s->rtsr);
    qemu_put_be32s(f, &s->rtar);
    qemu_put_be32s(f, &s->rdar1);
    qemu_put_be32s(f, &s->rdar2);
    qemu_put_be32s(f, &s->ryar1);
    qemu_put_be32s(f, &s->ryar2);
    qemu_put_be32s(f, &s->swar1);
    qemu_put_be32s(f, &s->swar2);
    qemu_put_be32s(f, &s->piar);
    qemu_put_be32s(f, &s->last_rcnr);
    qemu_put_be32s(f, &s->last_rdcr);
    qemu_put_be32s(f, &s->last_rycr);
    qemu_put_be32s(f, &s->last_swcr);
    qemu_put_be32s(f, &s->last_rtcpicr);
    qemu_put_be64s(f, &s->last_hz);
    qemu_put_be64s(f, &s->last_sw);
    qemu_put_be64s(f, &s->last_pi);
}

static int pxa2xx_rtc_load(QEMUFile *f, void *opaque, int version_id)
{
    struct pxa2xx_state_s *s = (struct pxa2xx_state_s *) opaque;

    qemu_get_be32s(f, &s->rttr);
    qemu_get_be32s(f, &s->rtsr);
    qemu_get_be32s(f, &s->rtar);
    qemu_get_be32s(f, &s->rdar1);
    qemu_get_be32s(f, &s->rdar2);
    qemu_get_be32s(f, &s->ryar1);
    qemu_get_be32s(f, &s->ryar2);
    qemu_get_be32s(f, &s->swar1);
    qemu_get_be32s(f, &s->swar2);
    qemu_get_be32s(f, &s->piar);
    qemu_get_be32s(f, &s->last_rcnr);
    qemu_get_be32s(f, &s->last_rdcr);
    qemu_get_be32s(f, &s->last_rycr);
    qemu_get_be32s(f, &s->last_swcr);
    qemu_get_be32s(f, &s->last_rtcpicr);
    qemu_get_be64s(f, &s->last_hz);
    qemu_get_be64s(f, &s->last_sw);
    qemu_get_be64s(f, &s->last_pi);

    pxa2xx_rtc_alarm_update(s, s->rtsr);

    return 0;
}

/* I2C Interface */
struct pxa2xx_i2c_s {
    i2c_slave slave;
    i2c_bus *bus;
    target_phys_addr_t base;
    qemu_irq irq;

    uint16_t control;
    uint16_t status;
    uint8_t ibmr;
    uint8_t data;
};

#define IBMR	0x80	/* I2C Bus Monitor register */
#define IDBR	0x88	/* I2C Data Buffer register */
#define ICR	0x90	/* I2C Control register */
#define ISR	0x98	/* I2C Status register */
#define ISAR	0xa0	/* I2C Slave Address register */

static void pxa2xx_i2c_update(struct pxa2xx_i2c_s *s)
{
    uint16_t level = 0;
    level |= s->status & s->control & (1 << 10);		/* BED */
    level |= (s->status & (1 << 7)) && (s->control & (1 << 9));	/* IRF */
    level |= (s->status & (1 << 6)) && (s->control & (1 << 8));	/* ITE */
    level |= s->status & (1 << 9);				/* SAD */
    qemu_set_irq(s->irq, !!level);
}

/* These are only stubs now.  */
static void pxa2xx_i2c_event(i2c_slave *i2c, enum i2c_event event)
{
    struct pxa2xx_i2c_s *s = (struct pxa2xx_i2c_s *) i2c;

    switch (event) {
    case I2C_START_SEND:
        s->status |= (1 << 9);				/* set SAD */
        s->status &= ~(1 << 0);				/* clear RWM */
        break;
    case I2C_START_RECV:
        s->status |= (1 << 9);				/* set SAD */
        s->status |= 1 << 0;				/* set RWM */
        break;
    case I2C_FINISH:
        s->status |= (1 << 4);				/* set SSD */
        break;
    case I2C_NACK:
        s->status |= 1 << 1;				/* set ACKNAK */
        break;
    }
    pxa2xx_i2c_update(s);
}

static int pxa2xx_i2c_rx(i2c_slave *i2c)
{
    struct pxa2xx_i2c_s *s = (struct pxa2xx_i2c_s *) i2c;
    if ((s->control & (1 << 14)) || !(s->control & (1 << 6)))
        return 0;

    if (s->status & (1 << 0)) {			/* RWM */
        s->status |= 1 << 6;			/* set ITE */
    }
    pxa2xx_i2c_update(s);

    return s->data;
}

static int pxa2xx_i2c_tx(i2c_slave *i2c, uint8_t data)
{
    struct pxa2xx_i2c_s *s = (struct pxa2xx_i2c_s *) i2c;
    if ((s->control & (1 << 14)) || !(s->control & (1 << 6)))
        return 1;

    if (!(s->status & (1 << 0))) {		/* RWM */
        s->status |= 1 << 7;			/* set IRF */
        s->data = data;
    }
    pxa2xx_i2c_update(s);

    return 1;
}

static uint32_t pxa2xx_i2c_read(void *opaque, target_phys_addr_t addr)
{
    struct pxa2xx_i2c_s *s = (struct pxa2xx_i2c_s *) opaque;
    addr -= s->base;

    switch (addr) {
    case ICR:
        return s->control;
    case ISR:
        return s->status | (i2c_bus_busy(s->bus) << 2);
    case ISAR:
        return s->slave.address;
    case IDBR:
        return s->data;
    case IBMR:
        if (s->status & (1 << 2))
            s->ibmr ^= 3;	/* Fake SCL and SDA pin changes */
        else
            s->ibmr = 0;
        return s->ibmr;
    default:
        printf("%s: Bad register " REG_FMT "\n", __FUNCTION__, addr);
        break;
    }
    return 0;
}

static void pxa2xx_i2c_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct pxa2xx_i2c_s *s = (struct pxa2xx_i2c_s *) opaque;
    int ack;
    addr -= s->base;

    switch (addr) {
    case ICR:
        s->control = value & 0xfff7;
        if ((value & (1 << 3)) && (value & (1 << 6))) {	/* TB and IUE */
            /* TODO: slave mode */
            if (value & (1 << 0)) {			/* START condition */
                if (s->data & 1)
                    s->status |= 1 << 0;		/* set RWM */
                else
                    s->status &= ~(1 << 0);		/* clear RWM */
                ack = !i2c_start_transfer(s->bus, s->data >> 1, s->data & 1);
            } else {
                if (s->status & (1 << 0)) {		/* RWM */
                    s->data = i2c_recv(s->bus);
                    if (value & (1 << 2))		/* ACKNAK */
                        i2c_nack(s->bus);
                    ack = 1;
                } else
                    ack = !i2c_send(s->bus, s->data);
            }

            if (value & (1 << 1))			/* STOP condition */
                i2c_end_transfer(s->bus);

            if (ack) {
                if (value & (1 << 0))			/* START condition */
                    s->status |= 1 << 6;		/* set ITE */
                else
                    if (s->status & (1 << 0))		/* RWM */
                        s->status |= 1 << 7;		/* set IRF */
                    else
                        s->status |= 1 << 6;		/* set ITE */
                s->status &= ~(1 << 1);			/* clear ACKNAK */
            } else {
                s->status |= 1 << 6;			/* set ITE */
                s->status |= 1 << 10;			/* set BED */
                s->status |= 1 << 1;			/* set ACKNAK */
            }
        }
        if (!(value & (1 << 3)) && (value & (1 << 6)))	/* !TB and IUE */
            if (value & (1 << 4))			/* MA */
                i2c_end_transfer(s->bus);
        pxa2xx_i2c_update(s);
        break;

    case ISR:
        s->status &= ~(value & 0x07f0);
        pxa2xx_i2c_update(s);
        break;

    case ISAR:
        i2c_set_slave_address(&s->slave, value & 0x7f);
        break;

    case IDBR:
        s->data = value & 0xff;
        break;

    default:
        printf("%s: Bad register " REG_FMT "\n", __FUNCTION__, addr);
    }
}

static CPUReadMemoryFunc *pxa2xx_i2c_readfn[] = {
    pxa2xx_i2c_read,
    pxa2xx_i2c_read,
    pxa2xx_i2c_read,
};

static CPUWriteMemoryFunc *pxa2xx_i2c_writefn[] = {
    pxa2xx_i2c_write,
    pxa2xx_i2c_write,
    pxa2xx_i2c_write,
};

static void pxa2xx_i2c_save(QEMUFile *f, void *opaque)
{
    struct pxa2xx_i2c_s *s = (struct pxa2xx_i2c_s *) opaque;

    qemu_put_be16s(f, &s->control);
    qemu_put_be16s(f, &s->status);
    qemu_put_8s(f, &s->ibmr);
    qemu_put_8s(f, &s->data);

    i2c_bus_save(f, s->bus);
    i2c_slave_save(f, &s->slave);
}

static int pxa2xx_i2c_load(QEMUFile *f, void *opaque, int version_id)
{
    struct pxa2xx_i2c_s *s = (struct pxa2xx_i2c_s *) opaque;

    qemu_get_be16s(f, &s->control);
    qemu_get_be16s(f, &s->status);
    qemu_get_8s(f, &s->ibmr);
    qemu_get_8s(f, &s->data);

    i2c_bus_load(f, s->bus);
    i2c_slave_load(f, &s->slave);
    return 0;
}

struct pxa2xx_i2c_s *pxa2xx_i2c_init(target_phys_addr_t base,
                qemu_irq irq, uint32_t page_size)
{
    int iomemtype;
    struct pxa2xx_i2c_s *s = (struct pxa2xx_i2c_s *)
            i2c_slave_init(i2c_init_bus(), 0, sizeof(struct pxa2xx_i2c_s));

    s->base = base;
    s->irq = irq;
    s->slave.event = pxa2xx_i2c_event;
    s->slave.recv = pxa2xx_i2c_rx;
    s->slave.send = pxa2xx_i2c_tx;
    s->bus = i2c_init_bus();

    iomemtype = cpu_register_io_memory(0, pxa2xx_i2c_readfn,
                    pxa2xx_i2c_writefn, s);
    cpu_register_physical_memory(s->base & ~page_size, page_size, iomemtype);

    register_savevm("pxa2xx_i2c", base, 0,
                    pxa2xx_i2c_save, pxa2xx_i2c_load, s);

    return s;
}

i2c_bus *pxa2xx_i2c_bus(struct pxa2xx_i2c_s *s)
{
    return s->bus;
}

/* PXA Inter-IC Sound Controller */
static void pxa2xx_i2s_reset(struct pxa2xx_i2s_s *i2s)
{
    i2s->rx_len = 0;
    i2s->tx_len = 0;
    i2s->fifo_len = 0;
    i2s->clk = 0x1a;
    i2s->control[0] = 0x00;
    i2s->control[1] = 0x00;
    i2s->status = 0x00;
    i2s->mask = 0x00;
}

#define SACR_TFTH(val)	((val >> 8) & 0xf)
#define SACR_RFTH(val)	((val >> 12) & 0xf)
#define SACR_DREC(val)	(val & (1 << 3))
#define SACR_DPRL(val)	(val & (1 << 4))

static inline void pxa2xx_i2s_update(struct pxa2xx_i2s_s *i2s)
{
    int rfs, tfs;
    rfs = SACR_RFTH(i2s->control[0]) < i2s->rx_len &&
            !SACR_DREC(i2s->control[1]);
    tfs = (i2s->tx_len || i2s->fifo_len < SACR_TFTH(i2s->control[0])) &&
            i2s->enable && !SACR_DPRL(i2s->control[1]);

    pxa2xx_dma_request(i2s->dma, PXA2XX_RX_RQ_I2S, rfs);
    pxa2xx_dma_request(i2s->dma, PXA2XX_TX_RQ_I2S, tfs);

    i2s->status &= 0xe0;
    if (i2s->fifo_len < 16 || !i2s->enable)
        i2s->status |= 1 << 0;			/* TNF */
    if (i2s->rx_len)
        i2s->status |= 1 << 1;			/* RNE */
    if (i2s->enable)
        i2s->status |= 1 << 2;			/* BSY */
    if (tfs)
        i2s->status |= 1 << 3;			/* TFS */
    if (rfs)
        i2s->status |= 1 << 4;			/* RFS */
    if (!(i2s->tx_len && i2s->enable))
        i2s->status |= i2s->fifo_len << 8;	/* TFL */
    i2s->status |= MAX(i2s->rx_len, 0xf) << 12;	/* RFL */

    qemu_set_irq(i2s->irq, i2s->status & i2s->mask);
}

#define SACR0	0x00	/* Serial Audio Global Control register */
#define SACR1	0x04	/* Serial Audio I2S/MSB-Justified Control register */
#define SASR0	0x0c	/* Serial Audio Interface and FIFO Status register */
#define SAIMR	0x14	/* Serial Audio Interrupt Mask register */
#define SAICR	0x18	/* Serial Audio Interrupt Clear register */
#define SADIV	0x60	/* Serial Audio Clock Divider register */
#define SADR	0x80	/* Serial Audio Data register */

static uint32_t pxa2xx_i2s_read(void *opaque, target_phys_addr_t addr)
{
    struct pxa2xx_i2s_s *s = (struct pxa2xx_i2s_s *) opaque;
    addr -= s->base;

    switch (addr) {
    case SACR0:
        return s->control[0];
    case SACR1:
        return s->control[1];
    case SASR0:
        return s->status;
    case SAIMR:
        return s->mask;
    case SAICR:
        return 0;
    case SADIV:
        return s->clk;
    case SADR:
        if (s->rx_len > 0) {
            s->rx_len --;
            pxa2xx_i2s_update(s);
            return s->codec_in(s->opaque);
        }
        return 0;
    default:
        printf("%s: Bad register " REG_FMT "\n", __FUNCTION__, addr);
        break;
    }
    return 0;
}

static void pxa2xx_i2s_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct pxa2xx_i2s_s *s = (struct pxa2xx_i2s_s *) opaque;
    uint32_t *sample;
    addr -= s->base;

    switch (addr) {
    case SACR0:
        if (value & (1 << 3))				/* RST */
            pxa2xx_i2s_reset(s);
        s->control[0] = value & 0xff3d;
        if (!s->enable && (value & 1) && s->tx_len) {	/* ENB */
            for (sample = s->fifo; s->fifo_len > 0; s->fifo_len --, sample ++)
                s->codec_out(s->opaque, *sample);
            s->status &= ~(1 << 7);			/* I2SOFF */
        }
        if (value & (1 << 4))				/* EFWR */
            printf("%s: Attempt to use special function\n", __FUNCTION__);
        s->enable = ((value ^ 4) & 5) == 5;		/* ENB && !RST*/
        pxa2xx_i2s_update(s);
        break;
    case SACR1:
        s->control[1] = value & 0x0039;
        if (value & (1 << 5))				/* ENLBF */
            printf("%s: Attempt to use loopback function\n", __FUNCTION__);
        if (value & (1 << 4))				/* DPRL */
            s->fifo_len = 0;
        pxa2xx_i2s_update(s);
        break;
    case SAIMR:
        s->mask = value & 0x0078;
        pxa2xx_i2s_update(s);
        break;
    case SAICR:
        s->status &= ~(value & (3 << 5));
        pxa2xx_i2s_update(s);
        break;
    case SADIV:
        s->clk = value & 0x007f;
        break;
    case SADR:
        if (s->tx_len && s->enable) {
            s->tx_len --;
            pxa2xx_i2s_update(s);
            s->codec_out(s->opaque, value);
        } else if (s->fifo_len < 16) {
            s->fifo[s->fifo_len ++] = value;
            pxa2xx_i2s_update(s);
        }
        break;
    default:
        printf("%s: Bad register " REG_FMT "\n", __FUNCTION__, addr);
    }
}

static CPUReadMemoryFunc *pxa2xx_i2s_readfn[] = {
    pxa2xx_i2s_read,
    pxa2xx_i2s_read,
    pxa2xx_i2s_read,
};

static CPUWriteMemoryFunc *pxa2xx_i2s_writefn[] = {
    pxa2xx_i2s_write,
    pxa2xx_i2s_write,
    pxa2xx_i2s_write,
};

static void pxa2xx_i2s_save(QEMUFile *f, void *opaque)
{
    struct pxa2xx_i2s_s *s = (struct pxa2xx_i2s_s *) opaque;

    qemu_put_be32s(f, &s->control[0]);
    qemu_put_be32s(f, &s->control[1]);
    qemu_put_be32s(f, &s->status);
    qemu_put_be32s(f, &s->mask);
    qemu_put_be32s(f, &s->clk);

    qemu_put_be32(f, s->enable);
    qemu_put_be32(f, s->rx_len);
    qemu_put_be32(f, s->tx_len);
    qemu_put_be32(f, s->fifo_len);
}

static int pxa2xx_i2s_load(QEMUFile *f, void *opaque, int version_id)
{
    struct pxa2xx_i2s_s *s = (struct pxa2xx_i2s_s *) opaque;

    qemu_get_be32s(f, &s->control[0]);
    qemu_get_be32s(f, &s->control[1]);
    qemu_get_be32s(f, &s->status);
    qemu_get_be32s(f, &s->mask);
    qemu_get_be32s(f, &s->clk);

    s->enable = qemu_get_be32(f);
    s->rx_len = qemu_get_be32(f);
    s->tx_len = qemu_get_be32(f);
    s->fifo_len = qemu_get_be32(f);

    return 0;
}

static void pxa2xx_i2s_data_req(void *opaque, int tx, int rx)
{
    struct pxa2xx_i2s_s *s = (struct pxa2xx_i2s_s *) opaque;
    uint32_t *sample;

    /* Signal FIFO errors */
    if (s->enable && s->tx_len)
        s->status |= 1 << 5;		/* TUR */
    if (s->enable && s->rx_len)
        s->status |= 1 << 6;		/* ROR */

    /* Should be tx - MIN(tx, s->fifo_len) but we don't really need to
     * handle the cases where it makes a difference.  */
    s->tx_len = tx - s->fifo_len;
    s->rx_len = rx;
    /* Note that is s->codec_out wasn't set, we wouldn't get called.  */
    if (s->enable)
        for (sample = s->fifo; s->fifo_len; s->fifo_len --, sample ++)
            s->codec_out(s->opaque, *sample);
    pxa2xx_i2s_update(s);
}

static struct pxa2xx_i2s_s *pxa2xx_i2s_init(target_phys_addr_t base,
                qemu_irq irq, struct pxa2xx_dma_state_s *dma)
{
    int iomemtype;
    struct pxa2xx_i2s_s *s = (struct pxa2xx_i2s_s *)
            qemu_mallocz(sizeof(struct pxa2xx_i2s_s));

    s->base = base;
    s->irq = irq;
    s->dma = dma;
    s->data_req = pxa2xx_i2s_data_req;

    pxa2xx_i2s_reset(s);

    iomemtype = cpu_register_io_memory(0, pxa2xx_i2s_readfn,
                    pxa2xx_i2s_writefn, s);
    cpu_register_physical_memory(s->base & 0xfff00000, 0x100000, iomemtype);

    register_savevm("pxa2xx_i2s", base, 0,
                    pxa2xx_i2s_save, pxa2xx_i2s_load, s);

    return s;
}

/* PXA Fast Infra-red Communications Port */
struct pxa2xx_fir_s {
    target_phys_addr_t base;
    qemu_irq irq;
    struct pxa2xx_dma_state_s *dma;
    int enable;
    CharDriverState *chr;

    uint8_t control[3];
    uint8_t status[2];

    int rx_len;
    int rx_start;
    uint8_t rx_fifo[64];
};

static void pxa2xx_fir_reset(struct pxa2xx_fir_s *s)
{
    s->control[0] = 0x00;
    s->control[1] = 0x00;
    s->control[2] = 0x00;
    s->status[0] = 0x00;
    s->status[1] = 0x00;
    s->enable = 0;
}

static inline void pxa2xx_fir_update(struct pxa2xx_fir_s *s)
{
    static const int tresh[4] = { 8, 16, 32, 0 };
    int intr = 0;
    if ((s->control[0] & (1 << 4)) &&			/* RXE */
                    s->rx_len >= tresh[s->control[2] & 3])	/* TRIG */
        s->status[0] |= 1 << 4;				/* RFS */
    else
        s->status[0] &= ~(1 << 4);			/* RFS */
    if (s->control[0] & (1 << 3))			/* TXE */
        s->status[0] |= 1 << 3;				/* TFS */
    else
        s->status[0] &= ~(1 << 3);			/* TFS */
    if (s->rx_len)
        s->status[1] |= 1 << 2;				/* RNE */
    else
        s->status[1] &= ~(1 << 2);			/* RNE */
    if (s->control[0] & (1 << 4))			/* RXE */
        s->status[1] |= 1 << 0;				/* RSY */
    else
        s->status[1] &= ~(1 << 0);			/* RSY */

    intr |= (s->control[0] & (1 << 5)) &&		/* RIE */
            (s->status[0] & (1 << 4));			/* RFS */
    intr |= (s->control[0] & (1 << 6)) &&		/* TIE */
            (s->status[0] & (1 << 3));			/* TFS */
    intr |= (s->control[2] & (1 << 4)) &&		/* TRAIL */
            (s->status[0] & (1 << 6));			/* EOC */
    intr |= (s->control[0] & (1 << 2)) &&		/* TUS */
            (s->status[0] & (1 << 1));			/* TUR */
    intr |= s->status[0] & 0x25;			/* FRE, RAB, EIF */

    pxa2xx_dma_request(s->dma, PXA2XX_RX_RQ_ICP, (s->status[0] >> 4) & 1);
    pxa2xx_dma_request(s->dma, PXA2XX_TX_RQ_ICP, (s->status[0] >> 3) & 1);

    qemu_set_irq(s->irq, intr && s->enable);
}

#define ICCR0	0x00	/* FICP Control register 0 */
#define ICCR1	0x04	/* FICP Control register 1 */
#define ICCR2	0x08	/* FICP Control register 2 */
#define ICDR	0x0c	/* FICP Data register */
#define ICSR0	0x14	/* FICP Status register 0 */
#define ICSR1	0x18	/* FICP Status register 1 */
#define ICFOR	0x1c	/* FICP FIFO Occupancy Status register */

static uint32_t pxa2xx_fir_read(void *opaque, target_phys_addr_t addr)
{
    struct pxa2xx_fir_s *s = (struct pxa2xx_fir_s *) opaque;
    uint8_t ret;
    addr -= s->base;

    switch (addr) {
    case ICCR0:
        return s->control[0];
    case ICCR1:
        return s->control[1];
    case ICCR2:
        return s->control[2];
    case ICDR:
        s->status[0] &= ~0x01;
        s->status[1] &= ~0x72;
        if (s->rx_len) {
            s->rx_len --;
            ret = s->rx_fifo[s->rx_start ++];
            s->rx_start &= 63;
            pxa2xx_fir_update(s);
            return ret;
        }
        printf("%s: Rx FIFO underrun.\n", __FUNCTION__);
        break;
    case ICSR0:
        return s->status[0];
    case ICSR1:
        return s->status[1] | (1 << 3);			/* TNF */
    case ICFOR:
        return s->rx_len;
    default:
        printf("%s: Bad register " REG_FMT "\n", __FUNCTION__, addr);
        break;
    }
    return 0;
}

static void pxa2xx_fir_write(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    struct pxa2xx_fir_s *s = (struct pxa2xx_fir_s *) opaque;
    uint8_t ch;
    addr -= s->base;

    switch (addr) {
    case ICCR0:
        s->control[0] = value;
        if (!(value & (1 << 4)))			/* RXE */
            s->rx_len = s->rx_start = 0;
        if (!(value & (1 << 3)))			/* TXE */
            /* Nop */;
        s->enable = value & 1;				/* ITR */
        if (!s->enable)
            s->status[0] = 0;
        pxa2xx_fir_update(s);
        break;
    case ICCR1:
        s->control[1] = value;
        break;
    case ICCR2:
        s->control[2] = value & 0x3f;
        pxa2xx_fir_update(s);
        break;
    case ICDR:
        if (s->control[2] & (1 << 2))			/* TXP */
            ch = value;
        else
            ch = ~value;
        if (s->chr && s->enable && (s->control[0] & (1 << 3)))	/* TXE */
            qemu_chr_write(s->chr, &ch, 1);
        break;
    case ICSR0:
        s->status[0] &= ~(value & 0x66);
        pxa2xx_fir_update(s);
        break;
    case ICFOR:
        break;
    default:
        printf("%s: Bad register " REG_FMT "\n", __FUNCTION__, addr);
    }
}

static CPUReadMemoryFunc *pxa2xx_fir_readfn[] = {
    pxa2xx_fir_read,
    pxa2xx_fir_read,
    pxa2xx_fir_read,
};

static CPUWriteMemoryFunc *pxa2xx_fir_writefn[] = {
    pxa2xx_fir_write,
    pxa2xx_fir_write,
    pxa2xx_fir_write,
};

static int pxa2xx_fir_is_empty(void *opaque)
{
    struct pxa2xx_fir_s *s = (struct pxa2xx_fir_s *) opaque;
    return (s->rx_len < 64);
}

static void pxa2xx_fir_rx(void *opaque, const uint8_t *buf, int size)
{
    struct pxa2xx_fir_s *s = (struct pxa2xx_fir_s *) opaque;
    if (!(s->control[0] & (1 << 4)))			/* RXE */
        return;

    while (size --) {
        s->status[1] |= 1 << 4;				/* EOF */
        if (s->rx_len >= 64) {
            s->status[1] |= 1 << 6;			/* ROR */
            break;
        }

        if (s->control[2] & (1 << 3))			/* RXP */
            s->rx_fifo[(s->rx_start + s->rx_len ++) & 63] = *(buf ++);
        else
            s->rx_fifo[(s->rx_start + s->rx_len ++) & 63] = ~*(buf ++);
    }

    pxa2xx_fir_update(s);
}

static void pxa2xx_fir_event(void *opaque, int event)
{
}

static void pxa2xx_fir_save(QEMUFile *f, void *opaque)
{
    struct pxa2xx_fir_s *s = (struct pxa2xx_fir_s *) opaque;
    int i;

    qemu_put_be32(f, s->enable);

    qemu_put_8s(f, &s->control[0]);
    qemu_put_8s(f, &s->control[1]);
    qemu_put_8s(f, &s->control[2]);
    qemu_put_8s(f, &s->status[0]);
    qemu_put_8s(f, &s->status[1]);

    qemu_put_byte(f, s->rx_len);
    for (i = 0; i < s->rx_len; i ++)
        qemu_put_byte(f, s->rx_fifo[(s->rx_start + i) & 63]);
}

static int pxa2xx_fir_load(QEMUFile *f, void *opaque, int version_id)
{
    struct pxa2xx_fir_s *s = (struct pxa2xx_fir_s *) opaque;
    int i;

    s->enable = qemu_get_be32(f);

    qemu_get_8s(f, &s->control[0]);
    qemu_get_8s(f, &s->control[1]);
    qemu_get_8s(f, &s->control[2]);
    qemu_get_8s(f, &s->status[0]);
    qemu_get_8s(f, &s->status[1]);

    s->rx_len = qemu_get_byte(f);
    s->rx_start = 0;
    for (i = 0; i < s->rx_len; i ++)
        s->rx_fifo[i] = qemu_get_byte(f);

    return 0;
}

static struct pxa2xx_fir_s *pxa2xx_fir_init(target_phys_addr_t base,
                qemu_irq irq, struct pxa2xx_dma_state_s *dma,
                CharDriverState *chr)
{
    int iomemtype;
    struct pxa2xx_fir_s *s = (struct pxa2xx_fir_s *)
            qemu_mallocz(sizeof(struct pxa2xx_fir_s));

    s->base = base;
    s->irq = irq;
    s->dma = dma;
    s->chr = chr;

    pxa2xx_fir_reset(s);

    iomemtype = cpu_register_io_memory(0, pxa2xx_fir_readfn,
                    pxa2xx_fir_writefn, s);
    cpu_register_physical_memory(s->base, 0x1000, iomemtype);

    if (chr)
        qemu_chr_add_handlers(chr, pxa2xx_fir_is_empty,
                        pxa2xx_fir_rx, pxa2xx_fir_event, s);

    register_savevm("pxa2xx_fir", 0, 0, pxa2xx_fir_save, pxa2xx_fir_load, s);

    return s;
}

static void pxa2xx_reset(void *opaque, int line, int level)
{
    struct pxa2xx_state_s *s = (struct pxa2xx_state_s *) opaque;

    if (level && (s->pm_regs[PCFR >> 2] & 0x10)) {	/* GPR_EN */
        cpu_reset(s->env);
        /* TODO: reset peripherals */
    }
}

/* Initialise a PXA270 integrated chip (ARM based core).  */
struct pxa2xx_state_s *pxa270_init(unsigned int sdram_size,
                DisplayState *ds, const char *revision)
{
    struct pxa2xx_state_s *s;
    struct pxa2xx_ssp_s *ssp;
    int iomemtype, i;
    int index;
    s = (struct pxa2xx_state_s *) qemu_mallocz(sizeof(struct pxa2xx_state_s));

    if (revision && strncmp(revision, "pxa27", 5)) {
        fprintf(stderr, "Machine requires a PXA27x processor.\n");
        exit(1);
    }
    if (!revision)
        revision = "pxa270";
    
    s->env = cpu_init(revision);
    if (!s->env) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }
    register_savevm("cpu", 0, ARM_CPU_SAVE_VERSION, cpu_save, cpu_load,
                    s->env);

    s->reset = qemu_allocate_irqs(pxa2xx_reset, s, 1)[0];

    /* SDRAM & Internal Memory Storage */
    cpu_register_physical_memory(PXA2XX_SDRAM_BASE,
                    sdram_size, qemu_ram_alloc(sdram_size) | IO_MEM_RAM);
    cpu_register_physical_memory(PXA2XX_INTERNAL_BASE,
                    0x40000, qemu_ram_alloc(0x40000) | IO_MEM_RAM);

    s->pic = pxa2xx_pic_init(0x40d00000, s->env);

    s->dma = pxa27x_dma_init(0x40000000, s->pic[PXA2XX_PIC_DMA]);

    pxa27x_timer_init(0x40a00000, &s->pic[PXA2XX_PIC_OST_0],
                    s->pic[PXA27X_PIC_OST_4_11]);

    s->gpio = pxa2xx_gpio_init(0x40e00000, s->env, s->pic, 121);

    index = drive_get_index(IF_SD, 0, 0);
    if (index == -1) {
        fprintf(stderr, "qemu: missing SecureDigital device\n");
        exit(1);
    }
    s->mmc = pxa2xx_mmci_init(0x41100000, drives_table[index].bdrv,
                              s->pic[PXA2XX_PIC_MMC], s->dma);

    for (i = 0; pxa270_serial[i].io_base; i ++)
        if (serial_hds[i])
            serial_mm_init(pxa270_serial[i].io_base, 2,
                            s->pic[pxa270_serial[i].irqn], serial_hds[i], 1);
        else
            break;
    if (serial_hds[i])
        s->fir = pxa2xx_fir_init(0x40800000, s->pic[PXA2XX_PIC_ICP],
                        s->dma, serial_hds[i]);

    if (ds)
        s->lcd = pxa2xx_lcdc_init(0x44000000, s->pic[PXA2XX_PIC_LCD], ds);

    s->cm_base = 0x41300000;
    s->cm_regs[CCCR >> 2] = 0x02000210;	/* 416.0 MHz */
    s->clkcfg = 0x00000009;		/* Turbo mode active */
    iomemtype = cpu_register_io_memory(0, pxa2xx_cm_readfn,
                    pxa2xx_cm_writefn, s);
    cpu_register_physical_memory(s->cm_base, 0x1000, iomemtype);
    register_savevm("pxa2xx_cm", 0, 0, pxa2xx_cm_save, pxa2xx_cm_load, s);

    cpu_arm_set_cp_io(s->env, 14, pxa2xx_cp14_read, pxa2xx_cp14_write, s);

    s->mm_base = 0x48000000;
    s->mm_regs[MDMRS >> 2] = 0x00020002;
    s->mm_regs[MDREFR >> 2] = 0x03ca4000;
    s->mm_regs[MECR >> 2] = 0x00000001;	/* Two PC Card sockets */
    iomemtype = cpu_register_io_memory(0, pxa2xx_mm_readfn,
                    pxa2xx_mm_writefn, s);
    cpu_register_physical_memory(s->mm_base, 0x1000, iomemtype);
    register_savevm("pxa2xx_mm", 0, 0, pxa2xx_mm_save, pxa2xx_mm_load, s);

    s->pm_base = 0x40f00000;
    iomemtype = cpu_register_io_memory(0, pxa2xx_pm_readfn,
                    pxa2xx_pm_writefn, s);
    cpu_register_physical_memory(s->pm_base, 0x100, iomemtype);
    register_savevm("pxa2xx_pm", 0, 0, pxa2xx_pm_save, pxa2xx_pm_load, s);

    for (i = 0; pxa27x_ssp[i].io_base; i ++);
    s->ssp = (struct pxa2xx_ssp_s **)
            qemu_mallocz(sizeof(struct pxa2xx_ssp_s *) * i);
    ssp = (struct pxa2xx_ssp_s *)
            qemu_mallocz(sizeof(struct pxa2xx_ssp_s) * i);
    for (i = 0; pxa27x_ssp[i].io_base; i ++) {
        s->ssp[i] = &ssp[i];
        ssp[i].base = pxa27x_ssp[i].io_base;
        ssp[i].irq = s->pic[pxa27x_ssp[i].irqn];

        iomemtype = cpu_register_io_memory(0, pxa2xx_ssp_readfn,
                        pxa2xx_ssp_writefn, &ssp[i]);
        cpu_register_physical_memory(ssp[i].base, 0x1000, iomemtype);
        register_savevm("pxa2xx_ssp", i, 0,
                        pxa2xx_ssp_save, pxa2xx_ssp_load, s);
    }

    if (usb_enabled) {
        usb_ohci_init_pxa(0x4c000000, 3, -1, s->pic[PXA2XX_PIC_USBH1]);
    }

    s->pcmcia[0] = pxa2xx_pcmcia_init(0x20000000);
    s->pcmcia[1] = pxa2xx_pcmcia_init(0x30000000);

    s->rtc_base = 0x40900000;
    iomemtype = cpu_register_io_memory(0, pxa2xx_rtc_readfn,
                    pxa2xx_rtc_writefn, s);
    cpu_register_physical_memory(s->rtc_base, 0x1000, iomemtype);
    pxa2xx_rtc_init(s);
    register_savevm("pxa2xx_rtc", 0, 0, pxa2xx_rtc_save, pxa2xx_rtc_load, s);

    s->i2c[0] = pxa2xx_i2c_init(0x40301600, s->pic[PXA2XX_PIC_I2C], 0xffff);
    s->i2c[1] = pxa2xx_i2c_init(0x40f00100, s->pic[PXA2XX_PIC_PWRI2C], 0xff);

    s->i2s = pxa2xx_i2s_init(0x40400000, s->pic[PXA2XX_PIC_I2S], s->dma);

    s->kp = pxa27x_keypad_init(0x41500000, s->pic[PXA2XX_PIC_KEYPAD]);

    /* GPIO1 resets the processor */
    /* The handler can be overridden by board-specific code */
    pxa2xx_gpio_out_set(s->gpio, 1, s->reset);
    return s;
}

/* Initialise a PXA255 integrated chip (ARM based core).  */
struct pxa2xx_state_s *pxa255_init(unsigned int sdram_size,
                DisplayState *ds)
{
    struct pxa2xx_state_s *s;
    struct pxa2xx_ssp_s *ssp;
    int iomemtype, i;
    int index;

    s = (struct pxa2xx_state_s *) qemu_mallocz(sizeof(struct pxa2xx_state_s));

    s->env = cpu_init("pxa255");
    if (!s->env) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }
    register_savevm("cpu", 0, ARM_CPU_SAVE_VERSION, cpu_save, cpu_load,
                    s->env);

    s->reset = qemu_allocate_irqs(pxa2xx_reset, s, 1)[0];

    /* SDRAM & Internal Memory Storage */
    cpu_register_physical_memory(PXA2XX_SDRAM_BASE, sdram_size,
                    qemu_ram_alloc(sdram_size) | IO_MEM_RAM);
    cpu_register_physical_memory(PXA2XX_INTERNAL_BASE, PXA2XX_INTERNAL_SIZE,
                    qemu_ram_alloc(PXA2XX_INTERNAL_SIZE) | IO_MEM_RAM);

    s->pic = pxa2xx_pic_init(0x40d00000, s->env);

    s->dma = pxa255_dma_init(0x40000000, s->pic[PXA2XX_PIC_DMA]);

    pxa25x_timer_init(0x40a00000, &s->pic[PXA2XX_PIC_OST_0]);

    s->gpio = pxa2xx_gpio_init(0x40e00000, s->env, s->pic, 85);

    index = drive_get_index(IF_SD, 0, 0);
    if (index == -1) {
        fprintf(stderr, "qemu: missing SecureDigital device\n");
        exit(1);
    }
    s->mmc = pxa2xx_mmci_init(0x41100000, drives_table[index].bdrv,
                              s->pic[PXA2XX_PIC_MMC], s->dma);

    for (i = 0; pxa255_serial[i].io_base; i ++)
        if (serial_hds[i])
            serial_mm_init(pxa255_serial[i].io_base, 2,
                            s->pic[pxa255_serial[i].irqn], serial_hds[i], 1);
        else
            break;
    if (serial_hds[i])
        s->fir = pxa2xx_fir_init(0x40800000, s->pic[PXA2XX_PIC_ICP],
                        s->dma, serial_hds[i]);

    if (ds)
        s->lcd = pxa2xx_lcdc_init(0x44000000, s->pic[PXA2XX_PIC_LCD], ds);

    s->cm_base = 0x41300000;
    s->cm_regs[CCCR >> 2] = 0x02000210;	/* 416.0 MHz */
    s->clkcfg = 0x00000009;		/* Turbo mode active */
    iomemtype = cpu_register_io_memory(0, pxa2xx_cm_readfn,
                    pxa2xx_cm_writefn, s);
    cpu_register_physical_memory(s->cm_base, 0x1000, iomemtype);
    register_savevm("pxa2xx_cm", 0, 0, pxa2xx_cm_save, pxa2xx_cm_load, s);

    cpu_arm_set_cp_io(s->env, 14, pxa2xx_cp14_read, pxa2xx_cp14_write, s);

    s->mm_base = 0x48000000;
    s->mm_regs[MDMRS >> 2] = 0x00020002;
    s->mm_regs[MDREFR >> 2] = 0x03ca4000;
    s->mm_regs[MECR >> 2] = 0x00000001;	/* Two PC Card sockets */
    iomemtype = cpu_register_io_memory(0, pxa2xx_mm_readfn,
                    pxa2xx_mm_writefn, s);
    cpu_register_physical_memory(s->mm_base, 0x1000, iomemtype);
    register_savevm("pxa2xx_mm", 0, 0, pxa2xx_mm_save, pxa2xx_mm_load, s);

    s->pm_base = 0x40f00000;
    iomemtype = cpu_register_io_memory(0, pxa2xx_pm_readfn,
                    pxa2xx_pm_writefn, s);
    cpu_register_physical_memory(s->pm_base, 0x100, iomemtype);
    register_savevm("pxa2xx_pm", 0, 0, pxa2xx_pm_save, pxa2xx_pm_load, s);

    for (i = 0; pxa255_ssp[i].io_base; i ++);
    s->ssp = (struct pxa2xx_ssp_s **)
            qemu_mallocz(sizeof(struct pxa2xx_ssp_s *) * i);
    ssp = (struct pxa2xx_ssp_s *)
            qemu_mallocz(sizeof(struct pxa2xx_ssp_s) * i);
    for (i = 0; pxa255_ssp[i].io_base; i ++) {
        s->ssp[i] = &ssp[i];
        ssp[i].base = pxa255_ssp[i].io_base;
        ssp[i].irq = s->pic[pxa255_ssp[i].irqn];

        iomemtype = cpu_register_io_memory(0, pxa2xx_ssp_readfn,
                        pxa2xx_ssp_writefn, &ssp[i]);
        cpu_register_physical_memory(ssp[i].base, 0x1000, iomemtype);
        register_savevm("pxa2xx_ssp", i, 0,
                        pxa2xx_ssp_save, pxa2xx_ssp_load, s);
    }

    if (usb_enabled) {
        usb_ohci_init_pxa(0x4c000000, 3, -1, s->pic[PXA2XX_PIC_USBH1]);
    }

    s->pcmcia[0] = pxa2xx_pcmcia_init(0x20000000);
    s->pcmcia[1] = pxa2xx_pcmcia_init(0x30000000);

    s->rtc_base = 0x40900000;
    iomemtype = cpu_register_io_memory(0, pxa2xx_rtc_readfn,
                    pxa2xx_rtc_writefn, s);
    cpu_register_physical_memory(s->rtc_base, 0x1000, iomemtype);
    pxa2xx_rtc_init(s);
    register_savevm("pxa2xx_rtc", 0, 0, pxa2xx_rtc_save, pxa2xx_rtc_load, s);

    s->i2c[0] = pxa2xx_i2c_init(0x40301600, s->pic[PXA2XX_PIC_I2C], 0xffff);
    s->i2c[1] = pxa2xx_i2c_init(0x40f00100, s->pic[PXA2XX_PIC_PWRI2C], 0xff);

    s->i2s = pxa2xx_i2s_init(0x40400000, s->pic[PXA2XX_PIC_I2S], s->dma);

    /* GPIO1 resets the processor */
    /* The handler can be overridden by board-specific code */
    pxa2xx_gpio_out_set(s->gpio, 1, s->reset);
    return s;
}
