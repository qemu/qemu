/*
 * Intel XScale PXA255/270 processor support.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/sysbus.h"
#include "hw/arm/pxa.h"
#include "sysemu/sysemu.h"
#include "hw/char/serial.h"
#include "hw/i2c/i2c.h"
#include "hw/ssi/ssi.h"
#include "chardev/char-fe.h"
#include "sysemu/blockdev.h"
#include "sysemu/qtest.h"
#include "qemu/cutils.h"

static struct {
    hwaddr io_base;
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
    hwaddr io_base;
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

static uint64_t pxa2xx_pm_read(void *opaque, hwaddr addr,
                               unsigned size)
{
    PXA2xxState *s = (PXA2xxState *) opaque;

    switch (addr) {
    case PMCR ... PCMD31:
        if (addr & 3)
            goto fail;

        return s->pm_regs[addr >> 2];
    default:
    fail:
        printf("%s: Bad register " REG_FMT "\n", __func__, addr);
        break;
    }
    return 0;
}

static void pxa2xx_pm_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned size)
{
    PXA2xxState *s = (PXA2xxState *) opaque;

    switch (addr) {
    case PMCR:
        /* Clear the write-one-to-clear bits... */
        s->pm_regs[addr >> 2] &= ~(value & 0x2a);
        /* ...and set the plain r/w bits */
        s->pm_regs[addr >> 2] &= ~0x15;
        s->pm_regs[addr >> 2] |= value & 0x15;
        break;

    case PSSR:	/* Read-clean registers */
    case RCSR:
    case PKSR:
        s->pm_regs[addr >> 2] &= ~value;
        break;

    default:	/* Read-write registers */
        if (!(addr & 3)) {
            s->pm_regs[addr >> 2] = value;
            break;
        }

        printf("%s: Bad register " REG_FMT "\n", __func__, addr);
        break;
    }
}

static const MemoryRegionOps pxa2xx_pm_ops = {
    .read = pxa2xx_pm_read,
    .write = pxa2xx_pm_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_pxa2xx_pm = {
    .name = "pxa2xx_pm",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(pm_regs, PXA2xxState, 0x40),
        VMSTATE_END_OF_LIST()
    }
};

#define CCCR	0x00	/* Core Clock Configuration register */
#define CKEN	0x04	/* Clock Enable register */
#define OSCC	0x08	/* Oscillator Configuration register */
#define CCSR	0x0c	/* Core Clock Status register */

static uint64_t pxa2xx_cm_read(void *opaque, hwaddr addr,
                               unsigned size)
{
    PXA2xxState *s = (PXA2xxState *) opaque;

    switch (addr) {
    case CCCR:
    case CKEN:
    case OSCC:
        return s->cm_regs[addr >> 2];

    case CCSR:
        return s->cm_regs[CCCR >> 2] | (3 << 28);

    default:
        printf("%s: Bad register " REG_FMT "\n", __func__, addr);
        break;
    }
    return 0;
}

static void pxa2xx_cm_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned size)
{
    PXA2xxState *s = (PXA2xxState *) opaque;

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
        printf("%s: Bad register " REG_FMT "\n", __func__, addr);
        break;
    }
}

static const MemoryRegionOps pxa2xx_cm_ops = {
    .read = pxa2xx_cm_read,
    .write = pxa2xx_cm_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_pxa2xx_cm = {
    .name = "pxa2xx_cm",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(cm_regs, PXA2xxState, 4),
        VMSTATE_UINT32(clkcfg, PXA2xxState),
        VMSTATE_UINT32(pmnc, PXA2xxState),
        VMSTATE_END_OF_LIST()
    }
};

static uint64_t pxa2xx_clkcfg_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    PXA2xxState *s = (PXA2xxState *)ri->opaque;
    return s->clkcfg;
}

static void pxa2xx_clkcfg_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                uint64_t value)
{
    PXA2xxState *s = (PXA2xxState *)ri->opaque;
    s->clkcfg = value & 0xf;
    if (value & 2) {
        printf("%s: CPU frequency change attempt\n", __func__);
    }
}

static void pxa2xx_pwrmode_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                 uint64_t value)
{
    PXA2xxState *s = (PXA2xxState *)ri->opaque;
    static const char *pwrmode[8] = {
        "Normal", "Idle", "Deep-idle", "Standby",
        "Sleep", "reserved (!)", "reserved (!)", "Deep-sleep",
    };

    if (value & 8) {
        printf("%s: CPU voltage change attempt\n", __func__);
    }
    switch (value & 7) {
    case 0:
        /* Do nothing */
        break;

    case 1:
        /* Idle */
        if (!(s->cm_regs[CCCR >> 2] & (1U << 31))) { /* CPDIS */
            cpu_interrupt(CPU(s->cpu), CPU_INTERRUPT_HALT);
            break;
        }
        /* Fall through.  */

    case 2:
        /* Deep-Idle */
        cpu_interrupt(CPU(s->cpu), CPU_INTERRUPT_HALT);
        s->pm_regs[RCSR >> 2] |= 0x8; /* Set GPR */
        goto message;

    case 3:
        s->cpu->env.uncached_cpsr = ARM_CPU_MODE_SVC;
        s->cpu->env.daif = PSTATE_A | PSTATE_F | PSTATE_I;
        s->cpu->env.cp15.sctlr_ns = 0;
        s->cpu->env.cp15.cpacr_el1 = 0;
        s->cpu->env.cp15.ttbr0_el[1] = 0;
        s->cpu->env.cp15.dacr_ns = 0;
        s->pm_regs[PSSR >> 2] |= 0x8; /* Set STS */
        s->pm_regs[RCSR >> 2] |= 0x8; /* Set GPR */

        /*
         * The scratch-pad register is almost universally used
         * for storing the return address on suspend.  For the
         * lack of a resuming bootloader, perform a jump
         * directly to that address.
         */
        memset(s->cpu->env.regs, 0, 4 * 15);
        s->cpu->env.regs[15] = s->pm_regs[PSPR >> 2];

#if 0
        buffer = 0xe59ff000; /* ldr     pc, [pc, #0] */
        cpu_physical_memory_write(0, &buffer, 4);
        buffer = s->pm_regs[PSPR >> 2];
        cpu_physical_memory_write(8, &buffer, 4);
#endif

        /* Suspend */
        cpu_interrupt(current_cpu, CPU_INTERRUPT_HALT);

        goto message;

    default:
    message:
        printf("%s: machine entered %s mode\n", __func__,
               pwrmode[value & 7]);
    }
}

static uint64_t pxa2xx_cppmnc_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    PXA2xxState *s = (PXA2xxState *)ri->opaque;
    return s->pmnc;
}

static void pxa2xx_cppmnc_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                uint64_t value)
{
    PXA2xxState *s = (PXA2xxState *)ri->opaque;
    s->pmnc = value;
}

static uint64_t pxa2xx_cpccnt_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    PXA2xxState *s = (PXA2xxState *)ri->opaque;
    if (s->pmnc & 1) {
        return qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    } else {
        return 0;
    }
}

static const ARMCPRegInfo pxa_cp_reginfo[] = {
    /* cp14 crm==1: perf registers */
    { .name = "CPPMNC", .cp = 14, .crn = 0, .crm = 1, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_IO,
      .readfn = pxa2xx_cppmnc_read, .writefn = pxa2xx_cppmnc_write },
    { .name = "CPCCNT", .cp = 14, .crn = 1, .crm = 1, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_IO,
      .readfn = pxa2xx_cpccnt_read, .writefn = arm_cp_write_ignore },
    { .name = "CPINTEN", .cp = 14, .crn = 4, .crm = 1, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPFLAG", .cp = 14, .crn = 5, .crm = 1, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPEVTSEL", .cp = 14, .crn = 8, .crm = 1, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    /* cp14 crm==2: performance count registers */
    { .name = "CPPMN0", .cp = 14, .crn = 0, .crm = 2, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPPMN1", .cp = 14, .crn = 1, .crm = 2, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPPMN2", .cp = 14, .crn = 2, .crm = 2, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPPMN3", .cp = 14, .crn = 2, .crm = 3, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    /* cp14 crn==6: CLKCFG */
    { .name = "CLKCFG", .cp = 14, .crn = 6, .crm = 0, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_IO,
      .readfn = pxa2xx_clkcfg_read, .writefn = pxa2xx_clkcfg_write },
    /* cp14 crn==7: PWRMODE */
    { .name = "PWRMODE", .cp = 14, .crn = 7, .crm = 0, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_IO,
      .readfn = arm_cp_read_zero, .writefn = pxa2xx_pwrmode_write },
    REGINFO_SENTINEL
};

static void pxa2xx_setup_cp14(PXA2xxState *s)
{
    define_arm_cp_regs_with_opaque(s->cpu, pxa_cp_reginfo, s);
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

static uint64_t pxa2xx_mm_read(void *opaque, hwaddr addr,
                               unsigned size)
{
    PXA2xxState *s = (PXA2xxState *) opaque;

    switch (addr) {
    case MDCNFG ... SA1110:
        if ((addr & 3) == 0)
            return s->mm_regs[addr >> 2];
        /* fall through */
    default:
        printf("%s: Bad register " REG_FMT "\n", __func__, addr);
        break;
    }
    return 0;
}

static void pxa2xx_mm_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned size)
{
    PXA2xxState *s = (PXA2xxState *) opaque;

    switch (addr) {
    case MDCNFG ... SA1110:
        if ((addr & 3) == 0) {
            s->mm_regs[addr >> 2] = value;
            break;
        }

    default:
        printf("%s: Bad register " REG_FMT "\n", __func__, addr);
        break;
    }
}

static const MemoryRegionOps pxa2xx_mm_ops = {
    .read = pxa2xx_mm_read,
    .write = pxa2xx_mm_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_pxa2xx_mm = {
    .name = "pxa2xx_mm",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(mm_regs, PXA2xxState, 0x1a),
        VMSTATE_END_OF_LIST()
    }
};

#define TYPE_PXA2XX_SSP "pxa2xx-ssp"
#define PXA2XX_SSP(obj) \
    OBJECT_CHECK(PXA2xxSSPState, (obj), TYPE_PXA2XX_SSP)

/* Synchronous Serial Ports */
typedef struct {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    qemu_irq irq;
    uint32_t enable;
    SSIBus *bus;

    uint32_t sscr[2];
    uint32_t sspsp;
    uint32_t ssto;
    uint32_t ssitr;
    uint32_t sssr;
    uint8_t sstsa;
    uint8_t ssrsa;
    uint8_t ssacd;

    uint32_t rx_fifo[16];
    uint32_t rx_level;
    uint32_t rx_start;
} PXA2xxSSPState;

static bool pxa2xx_ssp_vmstate_validate(void *opaque, int version_id)
{
    PXA2xxSSPState *s = opaque;

    return s->rx_start < sizeof(s->rx_fifo);
}

static const VMStateDescription vmstate_pxa2xx_ssp = {
    .name = "pxa2xx-ssp",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(enable, PXA2xxSSPState),
        VMSTATE_UINT32_ARRAY(sscr, PXA2xxSSPState, 2),
        VMSTATE_UINT32(sspsp, PXA2xxSSPState),
        VMSTATE_UINT32(ssto, PXA2xxSSPState),
        VMSTATE_UINT32(ssitr, PXA2xxSSPState),
        VMSTATE_UINT32(sssr, PXA2xxSSPState),
        VMSTATE_UINT8(sstsa, PXA2xxSSPState),
        VMSTATE_UINT8(ssrsa, PXA2xxSSPState),
        VMSTATE_UINT8(ssacd, PXA2xxSSPState),
        VMSTATE_UINT32(rx_level, PXA2xxSSPState),
        VMSTATE_UINT32(rx_start, PXA2xxSSPState),
        VMSTATE_VALIDATE("fifo is 16 bytes", pxa2xx_ssp_vmstate_validate),
        VMSTATE_UINT32_ARRAY(rx_fifo, PXA2xxSSPState, 16),
        VMSTATE_END_OF_LIST()
    }
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
#define SSCR0_MOD       (1U << 31)
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

static void pxa2xx_ssp_int_update(PXA2xxSSPState *s)
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

static void pxa2xx_ssp_fifo_update(PXA2xxSSPState *s)
{
    s->sssr &= ~(0xf << 12);	/* Clear RFL */
    s->sssr &= ~(0xf << 8);	/* Clear TFL */
    s->sssr &= ~SSSR_TFS;
    s->sssr &= ~SSSR_TNF;
    if (s->enable) {
        s->sssr |= ((s->rx_level - 1) & 0xf) << 12;
        if (s->rx_level >= SSCR1_RFT(s->sscr[1]))
            s->sssr |= SSSR_RFS;
        else
            s->sssr &= ~SSSR_RFS;
        if (s->rx_level)
            s->sssr |= SSSR_RNE;
        else
            s->sssr &= ~SSSR_RNE;
        /* TX FIFO is never filled, so it is always in underrun
           condition if SSP is enabled */
        s->sssr |= SSSR_TFS;
        s->sssr |= SSSR_TNF;
    }

    pxa2xx_ssp_int_update(s);
}

static uint64_t pxa2xx_ssp_read(void *opaque, hwaddr addr,
                                unsigned size)
{
    PXA2xxSSPState *s = (PXA2xxSSPState *) opaque;
    uint32_t retval;

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
            printf("%s: SSP Rx Underrun\n", __func__);
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
        printf("%s: Bad register " REG_FMT "\n", __func__, addr);
        break;
    }
    return 0;
}

static void pxa2xx_ssp_write(void *opaque, hwaddr addr,
                             uint64_t value64, unsigned size)
{
    PXA2xxSSPState *s = (PXA2xxSSPState *) opaque;
    uint32_t value = value64;

    switch (addr) {
    case SSCR0:
        s->sscr[0] = value & 0xc7ffffff;
        s->enable = value & SSCR0_SSE;
        if (value & SSCR0_MOD)
            printf("%s: Attempt to use network mode\n", __func__);
        if (s->enable && SSCR0_DSS(value) < 4)
            printf("%s: Wrong data size: %i bits\n", __func__,
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
            printf("%s: Attempt to use SSP test mode\n", __func__);
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
            uint32_t readval;
            readval = ssi_transfer(s->bus, value);
            if (s->rx_level < 0x10) {
                s->rx_fifo[(s->rx_start + s->rx_level ++) & 0xf] = readval;
            } else {
                s->sssr |= SSSR_ROR;
            }
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
        printf("%s: Bad register " REG_FMT "\n", __func__, addr);
        break;
    }
}

static const MemoryRegionOps pxa2xx_ssp_ops = {
    .read = pxa2xx_ssp_read,
    .write = pxa2xx_ssp_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void pxa2xx_ssp_reset(DeviceState *d)
{
    PXA2xxSSPState *s = PXA2XX_SSP(d);

    s->enable = 0;
    s->sscr[0] = s->sscr[1] = 0;
    s->sspsp = 0;
    s->ssto = 0;
    s->ssitr = 0;
    s->sssr = 0;
    s->sstsa = 0;
    s->ssrsa = 0;
    s->ssacd = 0;
    s->rx_start = s->rx_level = 0;
}

static void pxa2xx_ssp_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    PXA2xxSSPState *s = PXA2XX_SSP(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    sysbus_init_irq(sbd, &s->irq);

    memory_region_init_io(&s->iomem, obj, &pxa2xx_ssp_ops, s,
                          "pxa2xx-ssp", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);

    s->bus = ssi_create_bus(dev, "ssi");
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

#define TYPE_PXA2XX_RTC "pxa2xx_rtc"
#define PXA2XX_RTC(obj) \
    OBJECT_CHECK(PXA2xxRTCState, (obj), TYPE_PXA2XX_RTC)

typedef struct {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    uint32_t rttr;
    uint32_t rtsr;
    uint32_t rtar;
    uint32_t rdar1;
    uint32_t rdar2;
    uint32_t ryar1;
    uint32_t ryar2;
    uint32_t swar1;
    uint32_t swar2;
    uint32_t piar;
    uint32_t last_rcnr;
    uint32_t last_rdcr;
    uint32_t last_rycr;
    uint32_t last_swcr;
    uint32_t last_rtcpicr;
    int64_t last_hz;
    int64_t last_sw;
    int64_t last_pi;
    QEMUTimer *rtc_hz;
    QEMUTimer *rtc_rdal1;
    QEMUTimer *rtc_rdal2;
    QEMUTimer *rtc_swal1;
    QEMUTimer *rtc_swal2;
    QEMUTimer *rtc_pi;
    qemu_irq rtc_irq;
} PXA2xxRTCState;

static inline void pxa2xx_rtc_int_update(PXA2xxRTCState *s)
{
    qemu_set_irq(s->rtc_irq, !!(s->rtsr & 0x2553));
}

static void pxa2xx_rtc_hzupdate(PXA2xxRTCState *s)
{
    int64_t rt = qemu_clock_get_ms(rtc_clock);
    s->last_rcnr += ((rt - s->last_hz) << 15) /
            (1000 * ((s->rttr & 0xffff) + 1));
    s->last_rdcr += ((rt - s->last_hz) << 15) /
            (1000 * ((s->rttr & 0xffff) + 1));
    s->last_hz = rt;
}

static void pxa2xx_rtc_swupdate(PXA2xxRTCState *s)
{
    int64_t rt = qemu_clock_get_ms(rtc_clock);
    if (s->rtsr & (1 << 12))
        s->last_swcr += (rt - s->last_sw) / 10;
    s->last_sw = rt;
}

static void pxa2xx_rtc_piupdate(PXA2xxRTCState *s)
{
    int64_t rt = qemu_clock_get_ms(rtc_clock);
    if (s->rtsr & (1 << 15))
        s->last_swcr += rt - s->last_pi;
    s->last_pi = rt;
}

static inline void pxa2xx_rtc_alarm_update(PXA2xxRTCState *s,
                uint32_t rtsr)
{
    if ((rtsr & (1 << 2)) && !(rtsr & (1 << 0)))
        timer_mod(s->rtc_hz, s->last_hz +
                (((s->rtar - s->last_rcnr) * 1000 *
                  ((s->rttr & 0xffff) + 1)) >> 15));
    else
        timer_del(s->rtc_hz);

    if ((rtsr & (1 << 5)) && !(rtsr & (1 << 4)))
        timer_mod(s->rtc_rdal1, s->last_hz +
                (((s->rdar1 - s->last_rdcr) * 1000 *
                  ((s->rttr & 0xffff) + 1)) >> 15)); /* TODO: fixup */
    else
        timer_del(s->rtc_rdal1);

    if ((rtsr & (1 << 7)) && !(rtsr & (1 << 6)))
        timer_mod(s->rtc_rdal2, s->last_hz +
                (((s->rdar2 - s->last_rdcr) * 1000 *
                  ((s->rttr & 0xffff) + 1)) >> 15)); /* TODO: fixup */
    else
        timer_del(s->rtc_rdal2);

    if ((rtsr & 0x1200) == 0x1200 && !(rtsr & (1 << 8)))
        timer_mod(s->rtc_swal1, s->last_sw +
                        (s->swar1 - s->last_swcr) * 10); /* TODO: fixup */
    else
        timer_del(s->rtc_swal1);

    if ((rtsr & 0x1800) == 0x1800 && !(rtsr & (1 << 10)))
        timer_mod(s->rtc_swal2, s->last_sw +
                        (s->swar2 - s->last_swcr) * 10); /* TODO: fixup */
    else
        timer_del(s->rtc_swal2);

    if ((rtsr & 0xc000) == 0xc000 && !(rtsr & (1 << 13)))
        timer_mod(s->rtc_pi, s->last_pi +
                        (s->piar & 0xffff) - s->last_rtcpicr);
    else
        timer_del(s->rtc_pi);
}

static inline void pxa2xx_rtc_hz_tick(void *opaque)
{
    PXA2xxRTCState *s = (PXA2xxRTCState *) opaque;
    s->rtsr |= (1 << 0);
    pxa2xx_rtc_alarm_update(s, s->rtsr);
    pxa2xx_rtc_int_update(s);
}

static inline void pxa2xx_rtc_rdal1_tick(void *opaque)
{
    PXA2xxRTCState *s = (PXA2xxRTCState *) opaque;
    s->rtsr |= (1 << 4);
    pxa2xx_rtc_alarm_update(s, s->rtsr);
    pxa2xx_rtc_int_update(s);
}

static inline void pxa2xx_rtc_rdal2_tick(void *opaque)
{
    PXA2xxRTCState *s = (PXA2xxRTCState *) opaque;
    s->rtsr |= (1 << 6);
    pxa2xx_rtc_alarm_update(s, s->rtsr);
    pxa2xx_rtc_int_update(s);
}

static inline void pxa2xx_rtc_swal1_tick(void *opaque)
{
    PXA2xxRTCState *s = (PXA2xxRTCState *) opaque;
    s->rtsr |= (1 << 8);
    pxa2xx_rtc_alarm_update(s, s->rtsr);
    pxa2xx_rtc_int_update(s);
}

static inline void pxa2xx_rtc_swal2_tick(void *opaque)
{
    PXA2xxRTCState *s = (PXA2xxRTCState *) opaque;
    s->rtsr |= (1 << 10);
    pxa2xx_rtc_alarm_update(s, s->rtsr);
    pxa2xx_rtc_int_update(s);
}

static inline void pxa2xx_rtc_pi_tick(void *opaque)
{
    PXA2xxRTCState *s = (PXA2xxRTCState *) opaque;
    s->rtsr |= (1 << 13);
    pxa2xx_rtc_piupdate(s);
    s->last_rtcpicr = 0;
    pxa2xx_rtc_alarm_update(s, s->rtsr);
    pxa2xx_rtc_int_update(s);
}

static uint64_t pxa2xx_rtc_read(void *opaque, hwaddr addr,
                                unsigned size)
{
    PXA2xxRTCState *s = (PXA2xxRTCState *) opaque;

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
        return s->last_rcnr +
            ((qemu_clock_get_ms(rtc_clock) - s->last_hz) << 15) /
            (1000 * ((s->rttr & 0xffff) + 1));
    case RDCR:
        return s->last_rdcr +
            ((qemu_clock_get_ms(rtc_clock) - s->last_hz) << 15) /
            (1000 * ((s->rttr & 0xffff) + 1));
    case RYCR:
        return s->last_rycr;
    case SWCR:
        if (s->rtsr & (1 << 12))
            return s->last_swcr +
                (qemu_clock_get_ms(rtc_clock) - s->last_sw) / 10;
        else
            return s->last_swcr;
    default:
        printf("%s: Bad register " REG_FMT "\n", __func__, addr);
        break;
    }
    return 0;
}

static void pxa2xx_rtc_write(void *opaque, hwaddr addr,
                             uint64_t value64, unsigned size)
{
    PXA2xxRTCState *s = (PXA2xxRTCState *) opaque;
    uint32_t value = value64;

    switch (addr) {
    case RTTR:
        if (!(s->rttr & (1U << 31))) {
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
        printf("%s: Bad register " REG_FMT "\n", __func__, addr);
    }
}

static const MemoryRegionOps pxa2xx_rtc_ops = {
    .read = pxa2xx_rtc_read,
    .write = pxa2xx_rtc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void pxa2xx_rtc_init(Object *obj)
{
    PXA2xxRTCState *s = PXA2XX_RTC(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);
    struct tm tm;
    int wom;

    s->rttr = 0x7fff;
    s->rtsr = 0;

    qemu_get_timedate(&tm, 0);
    wom = ((tm.tm_mday - 1) / 7) + 1;

    s->last_rcnr = (uint32_t) mktimegm(&tm);
    s->last_rdcr = (wom << 20) | ((tm.tm_wday + 1) << 17) |
            (tm.tm_hour << 12) | (tm.tm_min << 6) | tm.tm_sec;
    s->last_rycr = ((tm.tm_year + 1900) << 9) |
            ((tm.tm_mon + 1) << 5) | tm.tm_mday;
    s->last_swcr = (tm.tm_hour << 19) |
            (tm.tm_min << 13) | (tm.tm_sec << 7);
    s->last_rtcpicr = 0;
    s->last_hz = s->last_sw = s->last_pi = qemu_clock_get_ms(rtc_clock);

    s->rtc_hz    = timer_new_ms(rtc_clock, pxa2xx_rtc_hz_tick,    s);
    s->rtc_rdal1 = timer_new_ms(rtc_clock, pxa2xx_rtc_rdal1_tick, s);
    s->rtc_rdal2 = timer_new_ms(rtc_clock, pxa2xx_rtc_rdal2_tick, s);
    s->rtc_swal1 = timer_new_ms(rtc_clock, pxa2xx_rtc_swal1_tick, s);
    s->rtc_swal2 = timer_new_ms(rtc_clock, pxa2xx_rtc_swal2_tick, s);
    s->rtc_pi    = timer_new_ms(rtc_clock, pxa2xx_rtc_pi_tick,    s);

    sysbus_init_irq(dev, &s->rtc_irq);

    memory_region_init_io(&s->iomem, obj, &pxa2xx_rtc_ops, s,
                          "pxa2xx-rtc", 0x10000);
    sysbus_init_mmio(dev, &s->iomem);
}

static int pxa2xx_rtc_pre_save(void *opaque)
{
    PXA2xxRTCState *s = (PXA2xxRTCState *) opaque;

    pxa2xx_rtc_hzupdate(s);
    pxa2xx_rtc_piupdate(s);
    pxa2xx_rtc_swupdate(s);

    return 0;
}

static int pxa2xx_rtc_post_load(void *opaque, int version_id)
{
    PXA2xxRTCState *s = (PXA2xxRTCState *) opaque;

    pxa2xx_rtc_alarm_update(s, s->rtsr);

    return 0;
}

static const VMStateDescription vmstate_pxa2xx_rtc_regs = {
    .name = "pxa2xx_rtc",
    .version_id = 0,
    .minimum_version_id = 0,
    .pre_save = pxa2xx_rtc_pre_save,
    .post_load = pxa2xx_rtc_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(rttr, PXA2xxRTCState),
        VMSTATE_UINT32(rtsr, PXA2xxRTCState),
        VMSTATE_UINT32(rtar, PXA2xxRTCState),
        VMSTATE_UINT32(rdar1, PXA2xxRTCState),
        VMSTATE_UINT32(rdar2, PXA2xxRTCState),
        VMSTATE_UINT32(ryar1, PXA2xxRTCState),
        VMSTATE_UINT32(ryar2, PXA2xxRTCState),
        VMSTATE_UINT32(swar1, PXA2xxRTCState),
        VMSTATE_UINT32(swar2, PXA2xxRTCState),
        VMSTATE_UINT32(piar, PXA2xxRTCState),
        VMSTATE_UINT32(last_rcnr, PXA2xxRTCState),
        VMSTATE_UINT32(last_rdcr, PXA2xxRTCState),
        VMSTATE_UINT32(last_rycr, PXA2xxRTCState),
        VMSTATE_UINT32(last_swcr, PXA2xxRTCState),
        VMSTATE_UINT32(last_rtcpicr, PXA2xxRTCState),
        VMSTATE_INT64(last_hz, PXA2xxRTCState),
        VMSTATE_INT64(last_sw, PXA2xxRTCState),
        VMSTATE_INT64(last_pi, PXA2xxRTCState),
        VMSTATE_END_OF_LIST(),
    },
};

static void pxa2xx_rtc_sysbus_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "PXA2xx RTC Controller";
    dc->vmsd = &vmstate_pxa2xx_rtc_regs;
}

static const TypeInfo pxa2xx_rtc_sysbus_info = {
    .name          = TYPE_PXA2XX_RTC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PXA2xxRTCState),
    .instance_init = pxa2xx_rtc_init,
    .class_init    = pxa2xx_rtc_sysbus_class_init,
};

/* I2C Interface */

#define TYPE_PXA2XX_I2C_SLAVE "pxa2xx-i2c-slave"
#define PXA2XX_I2C_SLAVE(obj) \
    OBJECT_CHECK(PXA2xxI2CSlaveState, (obj), TYPE_PXA2XX_I2C_SLAVE)

typedef struct PXA2xxI2CSlaveState {
    I2CSlave parent_obj;

    PXA2xxI2CState *host;
} PXA2xxI2CSlaveState;

#define TYPE_PXA2XX_I2C "pxa2xx_i2c"
#define PXA2XX_I2C(obj) \
    OBJECT_CHECK(PXA2xxI2CState, (obj), TYPE_PXA2XX_I2C)

struct PXA2xxI2CState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    PXA2xxI2CSlaveState *slave;
    I2CBus *bus;
    qemu_irq irq;
    uint32_t offset;
    uint32_t region_size;

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

static void pxa2xx_i2c_update(PXA2xxI2CState *s)
{
    uint16_t level = 0;
    level |= s->status & s->control & (1 << 10);		/* BED */
    level |= (s->status & (1 << 7)) && (s->control & (1 << 9));	/* IRF */
    level |= (s->status & (1 << 6)) && (s->control & (1 << 8));	/* ITE */
    level |= s->status & (1 << 9);				/* SAD */
    qemu_set_irq(s->irq, !!level);
}

/* These are only stubs now.  */
static int pxa2xx_i2c_event(I2CSlave *i2c, enum i2c_event event)
{
    PXA2xxI2CSlaveState *slave = PXA2XX_I2C_SLAVE(i2c);
    PXA2xxI2CState *s = slave->host;

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

    return 0;
}

static int pxa2xx_i2c_rx(I2CSlave *i2c)
{
    PXA2xxI2CSlaveState *slave = PXA2XX_I2C_SLAVE(i2c);
    PXA2xxI2CState *s = slave->host;

    if ((s->control & (1 << 14)) || !(s->control & (1 << 6))) {
        return 0;
    }

    if (s->status & (1 << 0)) {			/* RWM */
        s->status |= 1 << 6;			/* set ITE */
    }
    pxa2xx_i2c_update(s);

    return s->data;
}

static int pxa2xx_i2c_tx(I2CSlave *i2c, uint8_t data)
{
    PXA2xxI2CSlaveState *slave = PXA2XX_I2C_SLAVE(i2c);
    PXA2xxI2CState *s = slave->host;

    if ((s->control & (1 << 14)) || !(s->control & (1 << 6))) {
        return 1;
    }

    if (!(s->status & (1 << 0))) {		/* RWM */
        s->status |= 1 << 7;			/* set IRF */
        s->data = data;
    }
    pxa2xx_i2c_update(s);

    return 1;
}

static uint64_t pxa2xx_i2c_read(void *opaque, hwaddr addr,
                                unsigned size)
{
    PXA2xxI2CState *s = (PXA2xxI2CState *) opaque;
    I2CSlave *slave;

    addr -= s->offset;
    switch (addr) {
    case ICR:
        return s->control;
    case ISR:
        return s->status | (i2c_bus_busy(s->bus) << 2);
    case ISAR:
        slave = I2C_SLAVE(s->slave);
        return slave->address;
    case IDBR:
        return s->data;
    case IBMR:
        if (s->status & (1 << 2))
            s->ibmr ^= 3;	/* Fake SCL and SDA pin changes */
        else
            s->ibmr = 0;
        return s->ibmr;
    default:
        printf("%s: Bad register " REG_FMT "\n", __func__, addr);
        break;
    }
    return 0;
}

static void pxa2xx_i2c_write(void *opaque, hwaddr addr,
                             uint64_t value64, unsigned size)
{
    PXA2xxI2CState *s = (PXA2xxI2CState *) opaque;
    uint32_t value = value64;
    int ack;

    addr -= s->offset;
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
        i2c_set_slave_address(I2C_SLAVE(s->slave), value & 0x7f);
        break;

    case IDBR:
        s->data = value & 0xff;
        break;

    default:
        printf("%s: Bad register " REG_FMT "\n", __func__, addr);
    }
}

static const MemoryRegionOps pxa2xx_i2c_ops = {
    .read = pxa2xx_i2c_read,
    .write = pxa2xx_i2c_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_pxa2xx_i2c_slave = {
    .name = "pxa2xx_i2c_slave",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, PXA2xxI2CSlaveState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_pxa2xx_i2c = {
    .name = "pxa2xx_i2c",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(control, PXA2xxI2CState),
        VMSTATE_UINT16(status, PXA2xxI2CState),
        VMSTATE_UINT8(ibmr, PXA2xxI2CState),
        VMSTATE_UINT8(data, PXA2xxI2CState),
        VMSTATE_STRUCT_POINTER(slave, PXA2xxI2CState,
                               vmstate_pxa2xx_i2c_slave, PXA2xxI2CSlaveState),
        VMSTATE_END_OF_LIST()
    }
};

static void pxa2xx_i2c_slave_class_init(ObjectClass *klass, void *data)
{
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->event = pxa2xx_i2c_event;
    k->recv = pxa2xx_i2c_rx;
    k->send = pxa2xx_i2c_tx;
}

static const TypeInfo pxa2xx_i2c_slave_info = {
    .name          = TYPE_PXA2XX_I2C_SLAVE,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(PXA2xxI2CSlaveState),
    .class_init    = pxa2xx_i2c_slave_class_init,
};

PXA2xxI2CState *pxa2xx_i2c_init(hwaddr base,
                qemu_irq irq, uint32_t region_size)
{
    DeviceState *dev;
    SysBusDevice *i2c_dev;
    PXA2xxI2CState *s;
    I2CBus *i2cbus;

    dev = qdev_create(NULL, TYPE_PXA2XX_I2C);
    qdev_prop_set_uint32(dev, "size", region_size + 1);
    qdev_prop_set_uint32(dev, "offset", base & region_size);
    qdev_init_nofail(dev);

    i2c_dev = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(i2c_dev, 0, base & ~region_size);
    sysbus_connect_irq(i2c_dev, 0, irq);

    s = PXA2XX_I2C(i2c_dev);
    /* FIXME: Should the slave device really be on a separate bus?  */
    i2cbus = i2c_init_bus(dev, "dummy");
    dev = i2c_create_slave(i2cbus, TYPE_PXA2XX_I2C_SLAVE, 0);
    s->slave = PXA2XX_I2C_SLAVE(dev);
    s->slave->host = s;

    return s;
}

static void pxa2xx_i2c_initfn(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    PXA2xxI2CState *s = PXA2XX_I2C(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    s->bus = i2c_init_bus(dev, NULL);

    memory_region_init_io(&s->iomem, obj, &pxa2xx_i2c_ops, s,
                          "pxa2xx-i2c", s->region_size);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

I2CBus *pxa2xx_i2c_bus(PXA2xxI2CState *s)
{
    return s->bus;
}

static Property pxa2xx_i2c_properties[] = {
    DEFINE_PROP_UINT32("size", PXA2xxI2CState, region_size, 0x10000),
    DEFINE_PROP_UINT32("offset", PXA2xxI2CState, offset, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void pxa2xx_i2c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "PXA2xx I2C Bus Controller";
    dc->vmsd = &vmstate_pxa2xx_i2c;
    dc->props = pxa2xx_i2c_properties;
}

static const TypeInfo pxa2xx_i2c_info = {
    .name          = TYPE_PXA2XX_I2C,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PXA2xxI2CState),
    .instance_init = pxa2xx_i2c_initfn,
    .class_init    = pxa2xx_i2c_class_init,
};

/* PXA Inter-IC Sound Controller */
static void pxa2xx_i2s_reset(PXA2xxI2SState *i2s)
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

static inline void pxa2xx_i2s_update(PXA2xxI2SState *i2s)
{
    int rfs, tfs;
    rfs = SACR_RFTH(i2s->control[0]) < i2s->rx_len &&
            !SACR_DREC(i2s->control[1]);
    tfs = (i2s->tx_len || i2s->fifo_len < SACR_TFTH(i2s->control[0])) &&
            i2s->enable && !SACR_DPRL(i2s->control[1]);

    qemu_set_irq(i2s->rx_dma, rfs);
    qemu_set_irq(i2s->tx_dma, tfs);

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

static uint64_t pxa2xx_i2s_read(void *opaque, hwaddr addr,
                                unsigned size)
{
    PXA2xxI2SState *s = (PXA2xxI2SState *) opaque;

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
        printf("%s: Bad register " REG_FMT "\n", __func__, addr);
        break;
    }
    return 0;
}

static void pxa2xx_i2s_write(void *opaque, hwaddr addr,
                             uint64_t value, unsigned size)
{
    PXA2xxI2SState *s = (PXA2xxI2SState *) opaque;
    uint32_t *sample;

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
            printf("%s: Attempt to use special function\n", __func__);
        s->enable = (value & 9) == 1;			/* ENB && !RST*/
        pxa2xx_i2s_update(s);
        break;
    case SACR1:
        s->control[1] = value & 0x0039;
        if (value & (1 << 5))				/* ENLBF */
            printf("%s: Attempt to use loopback function\n", __func__);
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
        printf("%s: Bad register " REG_FMT "\n", __func__, addr);
    }
}

static const MemoryRegionOps pxa2xx_i2s_ops = {
    .read = pxa2xx_i2s_read,
    .write = pxa2xx_i2s_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_pxa2xx_i2s = {
    .name = "pxa2xx_i2s",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(control, PXA2xxI2SState, 2),
        VMSTATE_UINT32(status, PXA2xxI2SState),
        VMSTATE_UINT32(mask, PXA2xxI2SState),
        VMSTATE_UINT32(clk, PXA2xxI2SState),
        VMSTATE_INT32(enable, PXA2xxI2SState),
        VMSTATE_INT32(rx_len, PXA2xxI2SState),
        VMSTATE_INT32(tx_len, PXA2xxI2SState),
        VMSTATE_INT32(fifo_len, PXA2xxI2SState),
        VMSTATE_END_OF_LIST()
    }
};

static void pxa2xx_i2s_data_req(void *opaque, int tx, int rx)
{
    PXA2xxI2SState *s = (PXA2xxI2SState *) opaque;
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

static PXA2xxI2SState *pxa2xx_i2s_init(MemoryRegion *sysmem,
                hwaddr base,
                qemu_irq irq, qemu_irq rx_dma, qemu_irq tx_dma)
{
    PXA2xxI2SState *s = g_new0(PXA2xxI2SState, 1);

    s->irq = irq;
    s->rx_dma = rx_dma;
    s->tx_dma = tx_dma;
    s->data_req = pxa2xx_i2s_data_req;

    pxa2xx_i2s_reset(s);

    memory_region_init_io(&s->iomem, NULL, &pxa2xx_i2s_ops, s,
                          "pxa2xx-i2s", 0x100000);
    memory_region_add_subregion(sysmem, base, &s->iomem);

    vmstate_register(NULL, base, &vmstate_pxa2xx_i2s, s);

    return s;
}

/* PXA Fast Infra-red Communications Port */
#define TYPE_PXA2XX_FIR "pxa2xx-fir"
#define PXA2XX_FIR(obj) OBJECT_CHECK(PXA2xxFIrState, (obj), TYPE_PXA2XX_FIR)

struct PXA2xxFIrState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    qemu_irq irq;
    qemu_irq rx_dma;
    qemu_irq tx_dma;
    uint32_t enable;
    CharBackend chr;

    uint8_t control[3];
    uint8_t status[2];

    uint32_t rx_len;
    uint32_t rx_start;
    uint8_t rx_fifo[64];
};

static void pxa2xx_fir_reset(DeviceState *d)
{
    PXA2xxFIrState *s = PXA2XX_FIR(d);

    s->control[0] = 0x00;
    s->control[1] = 0x00;
    s->control[2] = 0x00;
    s->status[0] = 0x00;
    s->status[1] = 0x00;
    s->enable = 0;
}

static inline void pxa2xx_fir_update(PXA2xxFIrState *s)
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

    qemu_set_irq(s->rx_dma, (s->status[0] >> 4) & 1);
    qemu_set_irq(s->tx_dma, (s->status[0] >> 3) & 1);

    qemu_set_irq(s->irq, intr && s->enable);
}

#define ICCR0	0x00	/* FICP Control register 0 */
#define ICCR1	0x04	/* FICP Control register 1 */
#define ICCR2	0x08	/* FICP Control register 2 */
#define ICDR	0x0c	/* FICP Data register */
#define ICSR0	0x14	/* FICP Status register 0 */
#define ICSR1	0x18	/* FICP Status register 1 */
#define ICFOR	0x1c	/* FICP FIFO Occupancy Status register */

static uint64_t pxa2xx_fir_read(void *opaque, hwaddr addr,
                                unsigned size)
{
    PXA2xxFIrState *s = (PXA2xxFIrState *) opaque;
    uint8_t ret;

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
        printf("%s: Rx FIFO underrun.\n", __func__);
        break;
    case ICSR0:
        return s->status[0];
    case ICSR1:
        return s->status[1] | (1 << 3);			/* TNF */
    case ICFOR:
        return s->rx_len;
    default:
        printf("%s: Bad register " REG_FMT "\n", __func__, addr);
        break;
    }
    return 0;
}

static void pxa2xx_fir_write(void *opaque, hwaddr addr,
                             uint64_t value64, unsigned size)
{
    PXA2xxFIrState *s = (PXA2xxFIrState *) opaque;
    uint32_t value = value64;
    uint8_t ch;

    switch (addr) {
    case ICCR0:
        s->control[0] = value;
        if (!(value & (1 << 4)))			/* RXE */
            s->rx_len = s->rx_start = 0;
        if (!(value & (1 << 3))) {                      /* TXE */
            /* Nop */
        }
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
        if (s->control[2] & (1 << 2)) { /* TXP */
            ch = value;
        } else {
            ch = ~value;
        }
        if (s->enable && (s->control[0] & (1 << 3))) { /* TXE */
            /* XXX this blocks entire thread. Rewrite to use
             * qemu_chr_fe_write and background I/O callbacks */
            qemu_chr_fe_write_all(&s->chr, &ch, 1);
        }
        break;
    case ICSR0:
        s->status[0] &= ~(value & 0x66);
        pxa2xx_fir_update(s);
        break;
    case ICFOR:
        break;
    default:
        printf("%s: Bad register " REG_FMT "\n", __func__, addr);
    }
}

static const MemoryRegionOps pxa2xx_fir_ops = {
    .read = pxa2xx_fir_read,
    .write = pxa2xx_fir_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int pxa2xx_fir_is_empty(void *opaque)
{
    PXA2xxFIrState *s = (PXA2xxFIrState *) opaque;
    return (s->rx_len < 64);
}

static void pxa2xx_fir_rx(void *opaque, const uint8_t *buf, int size)
{
    PXA2xxFIrState *s = (PXA2xxFIrState *) opaque;
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

static void pxa2xx_fir_instance_init(Object *obj)
{
    PXA2xxFIrState *s = PXA2XX_FIR(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &pxa2xx_fir_ops, s,
                          "pxa2xx-fir", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    sysbus_init_irq(sbd, &s->rx_dma);
    sysbus_init_irq(sbd, &s->tx_dma);
}

static void pxa2xx_fir_realize(DeviceState *dev, Error **errp)
{
    PXA2xxFIrState *s = PXA2XX_FIR(dev);

    qemu_chr_fe_set_handlers(&s->chr, pxa2xx_fir_is_empty,
                             pxa2xx_fir_rx, pxa2xx_fir_event, NULL, s, NULL,
                             true);
}

static bool pxa2xx_fir_vmstate_validate(void *opaque, int version_id)
{
    PXA2xxFIrState *s = opaque;

    return s->rx_start < ARRAY_SIZE(s->rx_fifo);
}

static const VMStateDescription pxa2xx_fir_vmsd = {
    .name = "pxa2xx-fir",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(enable, PXA2xxFIrState),
        VMSTATE_UINT8_ARRAY(control, PXA2xxFIrState, 3),
        VMSTATE_UINT8_ARRAY(status, PXA2xxFIrState, 2),
        VMSTATE_UINT32(rx_len, PXA2xxFIrState),
        VMSTATE_UINT32(rx_start, PXA2xxFIrState),
        VMSTATE_VALIDATE("fifo is 64 bytes", pxa2xx_fir_vmstate_validate),
        VMSTATE_UINT8_ARRAY(rx_fifo, PXA2xxFIrState, 64),
        VMSTATE_END_OF_LIST()
    }
};

static Property pxa2xx_fir_properties[] = {
    DEFINE_PROP_CHR("chardev", PXA2xxFIrState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void pxa2xx_fir_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pxa2xx_fir_realize;
    dc->vmsd = &pxa2xx_fir_vmsd;
    dc->props = pxa2xx_fir_properties;
    dc->reset = pxa2xx_fir_reset;
}

static const TypeInfo pxa2xx_fir_info = {
    .name = TYPE_PXA2XX_FIR,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PXA2xxFIrState),
    .class_init = pxa2xx_fir_class_init,
    .instance_init = pxa2xx_fir_instance_init,
};

static PXA2xxFIrState *pxa2xx_fir_init(MemoryRegion *sysmem,
                                       hwaddr base,
                                       qemu_irq irq, qemu_irq rx_dma,
                                       qemu_irq tx_dma,
                                       Chardev *chr)
{
    DeviceState *dev;
    SysBusDevice *sbd;

    dev = qdev_create(NULL, TYPE_PXA2XX_FIR);
    qdev_prop_set_chr(dev, "chardev", chr);
    qdev_init_nofail(dev);
    sbd = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(sbd, 0, base);
    sysbus_connect_irq(sbd, 0, irq);
    sysbus_connect_irq(sbd, 1, rx_dma);
    sysbus_connect_irq(sbd, 2, tx_dma);
    return PXA2XX_FIR(dev);
}

static void pxa2xx_reset(void *opaque, int line, int level)
{
    PXA2xxState *s = (PXA2xxState *) opaque;

    if (level && (s->pm_regs[PCFR >> 2] & 0x10)) {	/* GPR_EN */
        cpu_reset(CPU(s->cpu));
        /* TODO: reset peripherals */
    }
}

/* Initialise a PXA270 integrated chip (ARM based core).  */
PXA2xxState *pxa270_init(MemoryRegion *address_space,
                         unsigned int sdram_size, const char *cpu_type)
{
    PXA2xxState *s;
    int i;
    DriveInfo *dinfo;
    s = g_new0(PXA2xxState, 1);

    if (strncmp(cpu_type, "pxa27", 5)) {
        error_report("Machine requires a PXA27x processor");
        exit(1);
    }

    s->cpu = ARM_CPU(cpu_create(cpu_type));
    s->reset = qemu_allocate_irq(pxa2xx_reset, s, 0);

    /* SDRAM & Internal Memory Storage */
    memory_region_init_ram(&s->sdram, NULL, "pxa270.sdram", sdram_size,
                           &error_fatal);
    memory_region_add_subregion(address_space, PXA2XX_SDRAM_BASE, &s->sdram);
    memory_region_init_ram(&s->internal, NULL, "pxa270.internal", 0x40000,
                           &error_fatal);
    memory_region_add_subregion(address_space, PXA2XX_INTERNAL_BASE,
                                &s->internal);

    s->pic = pxa2xx_pic_init(0x40d00000, s->cpu);

    s->dma = pxa27x_dma_init(0x40000000,
                    qdev_get_gpio_in(s->pic, PXA2XX_PIC_DMA));

    sysbus_create_varargs("pxa27x-timer", 0x40a00000,
                    qdev_get_gpio_in(s->pic, PXA2XX_PIC_OST_0 + 0),
                    qdev_get_gpio_in(s->pic, PXA2XX_PIC_OST_0 + 1),
                    qdev_get_gpio_in(s->pic, PXA2XX_PIC_OST_0 + 2),
                    qdev_get_gpio_in(s->pic, PXA2XX_PIC_OST_0 + 3),
                    qdev_get_gpio_in(s->pic, PXA27X_PIC_OST_4_11),
                    NULL);

    s->gpio = pxa2xx_gpio_init(0x40e00000, s->cpu, s->pic, 121);

    dinfo = drive_get(IF_SD, 0, 0);
    if (!dinfo && !qtest_enabled()) {
        warn_report("missing SecureDigital device");
    }
    s->mmc = pxa2xx_mmci_init(address_space, 0x41100000,
                    dinfo ? blk_by_legacy_dinfo(dinfo) : NULL,
                    qdev_get_gpio_in(s->pic, PXA2XX_PIC_MMC),
                    qdev_get_gpio_in(s->dma, PXA2XX_RX_RQ_MMCI),
                    qdev_get_gpio_in(s->dma, PXA2XX_TX_RQ_MMCI));

    for (i = 0; pxa270_serial[i].io_base; i++) {
        if (serial_hd(i)) {
            serial_mm_init(address_space, pxa270_serial[i].io_base, 2,
                           qdev_get_gpio_in(s->pic, pxa270_serial[i].irqn),
                           14857000 / 16, serial_hd(i),
                           DEVICE_NATIVE_ENDIAN);
        } else {
            break;
        }
    }
    if (serial_hd(i))
        s->fir = pxa2xx_fir_init(address_space, 0x40800000,
                        qdev_get_gpio_in(s->pic, PXA2XX_PIC_ICP),
                        qdev_get_gpio_in(s->dma, PXA2XX_RX_RQ_ICP),
                        qdev_get_gpio_in(s->dma, PXA2XX_TX_RQ_ICP),
                        serial_hd(i));

    s->lcd = pxa2xx_lcdc_init(address_space, 0x44000000,
                    qdev_get_gpio_in(s->pic, PXA2XX_PIC_LCD));

    s->cm_base = 0x41300000;
    s->cm_regs[CCCR >> 2] = 0x02000210;	/* 416.0 MHz */
    s->clkcfg = 0x00000009;		/* Turbo mode active */
    memory_region_init_io(&s->cm_iomem, NULL, &pxa2xx_cm_ops, s, "pxa2xx-cm", 0x1000);
    memory_region_add_subregion(address_space, s->cm_base, &s->cm_iomem);
    vmstate_register(NULL, 0, &vmstate_pxa2xx_cm, s);

    pxa2xx_setup_cp14(s);

    s->mm_base = 0x48000000;
    s->mm_regs[MDMRS >> 2] = 0x00020002;
    s->mm_regs[MDREFR >> 2] = 0x03ca4000;
    s->mm_regs[MECR >> 2] = 0x00000001;	/* Two PC Card sockets */
    memory_region_init_io(&s->mm_iomem, NULL, &pxa2xx_mm_ops, s, "pxa2xx-mm", 0x1000);
    memory_region_add_subregion(address_space, s->mm_base, &s->mm_iomem);
    vmstate_register(NULL, 0, &vmstate_pxa2xx_mm, s);

    s->pm_base = 0x40f00000;
    memory_region_init_io(&s->pm_iomem, NULL, &pxa2xx_pm_ops, s, "pxa2xx-pm", 0x100);
    memory_region_add_subregion(address_space, s->pm_base, &s->pm_iomem);
    vmstate_register(NULL, 0, &vmstate_pxa2xx_pm, s);

    for (i = 0; pxa27x_ssp[i].io_base; i ++);
    s->ssp = g_new0(SSIBus *, i);
    for (i = 0; pxa27x_ssp[i].io_base; i ++) {
        DeviceState *dev;
        dev = sysbus_create_simple(TYPE_PXA2XX_SSP, pxa27x_ssp[i].io_base,
                        qdev_get_gpio_in(s->pic, pxa27x_ssp[i].irqn));
        s->ssp[i] = (SSIBus *)qdev_get_child_bus(dev, "ssi");
    }

    sysbus_create_simple("sysbus-ohci", 0x4c000000,
                         qdev_get_gpio_in(s->pic, PXA2XX_PIC_USBH1));

    s->pcmcia[0] = pxa2xx_pcmcia_init(address_space, 0x20000000);
    s->pcmcia[1] = pxa2xx_pcmcia_init(address_space, 0x30000000);

    sysbus_create_simple(TYPE_PXA2XX_RTC, 0x40900000,
                    qdev_get_gpio_in(s->pic, PXA2XX_PIC_RTCALARM));

    s->i2c[0] = pxa2xx_i2c_init(0x40301600,
                    qdev_get_gpio_in(s->pic, PXA2XX_PIC_I2C), 0xffff);
    s->i2c[1] = pxa2xx_i2c_init(0x40f00100,
                    qdev_get_gpio_in(s->pic, PXA2XX_PIC_PWRI2C), 0xff);

    s->i2s = pxa2xx_i2s_init(address_space, 0x40400000,
                    qdev_get_gpio_in(s->pic, PXA2XX_PIC_I2S),
                    qdev_get_gpio_in(s->dma, PXA2XX_RX_RQ_I2S),
                    qdev_get_gpio_in(s->dma, PXA2XX_TX_RQ_I2S));

    s->kp = pxa27x_keypad_init(address_space, 0x41500000,
                    qdev_get_gpio_in(s->pic, PXA2XX_PIC_KEYPAD));

    /* GPIO1 resets the processor */
    /* The handler can be overridden by board-specific code */
    qdev_connect_gpio_out(s->gpio, 1, s->reset);
    return s;
}

/* Initialise a PXA255 integrated chip (ARM based core).  */
PXA2xxState *pxa255_init(MemoryRegion *address_space, unsigned int sdram_size)
{
    PXA2xxState *s;
    int i;
    DriveInfo *dinfo;

    s = g_new0(PXA2xxState, 1);

    s->cpu = ARM_CPU(cpu_create(ARM_CPU_TYPE_NAME("pxa255")));
    s->reset = qemu_allocate_irq(pxa2xx_reset, s, 0);

    /* SDRAM & Internal Memory Storage */
    memory_region_init_ram(&s->sdram, NULL, "pxa255.sdram", sdram_size,
                           &error_fatal);
    memory_region_add_subregion(address_space, PXA2XX_SDRAM_BASE, &s->sdram);
    memory_region_init_ram(&s->internal, NULL, "pxa255.internal",
                           PXA2XX_INTERNAL_SIZE, &error_fatal);
    memory_region_add_subregion(address_space, PXA2XX_INTERNAL_BASE,
                                &s->internal);

    s->pic = pxa2xx_pic_init(0x40d00000, s->cpu);

    s->dma = pxa255_dma_init(0x40000000,
                    qdev_get_gpio_in(s->pic, PXA2XX_PIC_DMA));

    sysbus_create_varargs("pxa25x-timer", 0x40a00000,
                    qdev_get_gpio_in(s->pic, PXA2XX_PIC_OST_0 + 0),
                    qdev_get_gpio_in(s->pic, PXA2XX_PIC_OST_0 + 1),
                    qdev_get_gpio_in(s->pic, PXA2XX_PIC_OST_0 + 2),
                    qdev_get_gpio_in(s->pic, PXA2XX_PIC_OST_0 + 3),
                    NULL);

    s->gpio = pxa2xx_gpio_init(0x40e00000, s->cpu, s->pic, 85);

    dinfo = drive_get(IF_SD, 0, 0);
    if (!dinfo && !qtest_enabled()) {
        warn_report("missing SecureDigital device");
    }
    s->mmc = pxa2xx_mmci_init(address_space, 0x41100000,
                    dinfo ? blk_by_legacy_dinfo(dinfo) : NULL,
                    qdev_get_gpio_in(s->pic, PXA2XX_PIC_MMC),
                    qdev_get_gpio_in(s->dma, PXA2XX_RX_RQ_MMCI),
                    qdev_get_gpio_in(s->dma, PXA2XX_TX_RQ_MMCI));

    for (i = 0; pxa255_serial[i].io_base; i++) {
        if (serial_hd(i)) {
            serial_mm_init(address_space, pxa255_serial[i].io_base, 2,
                           qdev_get_gpio_in(s->pic, pxa255_serial[i].irqn),
                           14745600 / 16, serial_hd(i),
                           DEVICE_NATIVE_ENDIAN);
        } else {
            break;
        }
    }
    if (serial_hd(i))
        s->fir = pxa2xx_fir_init(address_space, 0x40800000,
                        qdev_get_gpio_in(s->pic, PXA2XX_PIC_ICP),
                        qdev_get_gpio_in(s->dma, PXA2XX_RX_RQ_ICP),
                        qdev_get_gpio_in(s->dma, PXA2XX_TX_RQ_ICP),
                        serial_hd(i));

    s->lcd = pxa2xx_lcdc_init(address_space, 0x44000000,
                    qdev_get_gpio_in(s->pic, PXA2XX_PIC_LCD));

    s->cm_base = 0x41300000;
    s->cm_regs[CCCR >> 2] = 0x00000121;         /* from datasheet */
    s->cm_regs[CKEN >> 2] = 0x00017def;         /* from datasheet */

    s->clkcfg = 0x00000009;		/* Turbo mode active */
    memory_region_init_io(&s->cm_iomem, NULL, &pxa2xx_cm_ops, s, "pxa2xx-cm", 0x1000);
    memory_region_add_subregion(address_space, s->cm_base, &s->cm_iomem);
    vmstate_register(NULL, 0, &vmstate_pxa2xx_cm, s);

    pxa2xx_setup_cp14(s);

    s->mm_base = 0x48000000;
    s->mm_regs[MDMRS >> 2] = 0x00020002;
    s->mm_regs[MDREFR >> 2] = 0x03ca4000;
    s->mm_regs[MECR >> 2] = 0x00000001;	/* Two PC Card sockets */
    memory_region_init_io(&s->mm_iomem, NULL, &pxa2xx_mm_ops, s, "pxa2xx-mm", 0x1000);
    memory_region_add_subregion(address_space, s->mm_base, &s->mm_iomem);
    vmstate_register(NULL, 0, &vmstate_pxa2xx_mm, s);

    s->pm_base = 0x40f00000;
    memory_region_init_io(&s->pm_iomem, NULL, &pxa2xx_pm_ops, s, "pxa2xx-pm", 0x100);
    memory_region_add_subregion(address_space, s->pm_base, &s->pm_iomem);
    vmstate_register(NULL, 0, &vmstate_pxa2xx_pm, s);

    for (i = 0; pxa255_ssp[i].io_base; i ++);
    s->ssp = g_new0(SSIBus *, i);
    for (i = 0; pxa255_ssp[i].io_base; i ++) {
        DeviceState *dev;
        dev = sysbus_create_simple(TYPE_PXA2XX_SSP, pxa255_ssp[i].io_base,
                        qdev_get_gpio_in(s->pic, pxa255_ssp[i].irqn));
        s->ssp[i] = (SSIBus *)qdev_get_child_bus(dev, "ssi");
    }

    sysbus_create_simple("sysbus-ohci", 0x4c000000,
                         qdev_get_gpio_in(s->pic, PXA2XX_PIC_USBH1));

    s->pcmcia[0] = pxa2xx_pcmcia_init(address_space, 0x20000000);
    s->pcmcia[1] = pxa2xx_pcmcia_init(address_space, 0x30000000);

    sysbus_create_simple(TYPE_PXA2XX_RTC, 0x40900000,
                    qdev_get_gpio_in(s->pic, PXA2XX_PIC_RTCALARM));

    s->i2c[0] = pxa2xx_i2c_init(0x40301600,
                    qdev_get_gpio_in(s->pic, PXA2XX_PIC_I2C), 0xffff);
    s->i2c[1] = pxa2xx_i2c_init(0x40f00100,
                    qdev_get_gpio_in(s->pic, PXA2XX_PIC_PWRI2C), 0xff);

    s->i2s = pxa2xx_i2s_init(address_space, 0x40400000,
                    qdev_get_gpio_in(s->pic, PXA2XX_PIC_I2S),
                    qdev_get_gpio_in(s->dma, PXA2XX_RX_RQ_I2S),
                    qdev_get_gpio_in(s->dma, PXA2XX_TX_RQ_I2S));

    /* GPIO1 resets the processor */
    /* The handler can be overridden by board-specific code */
    qdev_connect_gpio_out(s->gpio, 1, s->reset);
    return s;
}

static void pxa2xx_ssp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = pxa2xx_ssp_reset;
    dc->vmsd = &vmstate_pxa2xx_ssp;
}

static const TypeInfo pxa2xx_ssp_info = {
    .name          = TYPE_PXA2XX_SSP,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PXA2xxSSPState),
    .instance_init = pxa2xx_ssp_init,
    .class_init    = pxa2xx_ssp_class_init,
};

static void pxa2xx_register_types(void)
{
    type_register_static(&pxa2xx_i2c_slave_info);
    type_register_static(&pxa2xx_ssp_info);
    type_register_static(&pxa2xx_i2c_info);
    type_register_static(&pxa2xx_rtc_sysbus_info);
    type_register_static(&pxa2xx_fir_info);
}

type_init(pxa2xx_register_types)
