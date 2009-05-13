/*
 * QEMU Sparc Sun4m ECC memory controller emulation
 *
 * Copyright (c) 2007 Robert Reif
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
#include "sun4m.h"
#include "sysemu.h"

//#define DEBUG_ECC

#ifdef DEBUG_ECC
#define DPRINTF(fmt, ...)                                       \
    do { printf("ECC: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...)
#endif

/* There are 3 versions of this chip used in SMP sun4m systems:
 * MCC (version 0, implementation 0) SS-600MP
 * EMC (version 0, implementation 1) SS-10
 * SMC (version 0, implementation 2) SS-10SX and SS-20
 */

#define ECC_MCC        0x00000000
#define ECC_EMC        0x10000000
#define ECC_SMC        0x20000000

/* Register indexes */
#define ECC_MER        0               /* Memory Enable Register */
#define ECC_MDR        1               /* Memory Delay Register */
#define ECC_MFSR       2               /* Memory Fault Status Register */
#define ECC_VCR        3               /* Video Configuration Register */
#define ECC_MFAR0      4               /* Memory Fault Address Register 0 */
#define ECC_MFAR1      5               /* Memory Fault Address Register 1 */
#define ECC_DR         6               /* Diagnostic Register */
#define ECC_ECR0       7               /* Event Count Register 0 */
#define ECC_ECR1       8               /* Event Count Register 1 */

/* ECC fault control register */
#define ECC_MER_EE     0x00000001      /* Enable ECC checking */
#define ECC_MER_EI     0x00000002      /* Enable Interrupts on
                                          correctable errors */
#define ECC_MER_MRR0   0x00000004      /* SIMM 0 */
#define ECC_MER_MRR1   0x00000008      /* SIMM 1 */
#define ECC_MER_MRR2   0x00000010      /* SIMM 2 */
#define ECC_MER_MRR3   0x00000020      /* SIMM 3 */
#define ECC_MER_MRR4   0x00000040      /* SIMM 4 */
#define ECC_MER_MRR5   0x00000080      /* SIMM 5 */
#define ECC_MER_MRR6   0x00000100      /* SIMM 6 */
#define ECC_MER_MRR7   0x00000200      /* SIMM 7 */
#define ECC_MER_REU    0x00000100      /* Memory Refresh Enable (600MP) */
#define ECC_MER_MRR    0x000003fc      /* MRR mask */
#define ECC_MER_A      0x00000400      /* Memory controller addr map select */
#define ECC_MER_DCI    0x00000800      /* Disables Coherent Invalidate ACK */
#define ECC_MER_VER    0x0f000000      /* Version */
#define ECC_MER_IMPL   0xf0000000      /* Implementation */
#define ECC_MER_MASK_0 0x00000103      /* Version 0 (MCC) mask */
#define ECC_MER_MASK_1 0x00000bff      /* Version 1 (EMC) mask */
#define ECC_MER_MASK_2 0x00000bff      /* Version 2 (SMC) mask */

/* ECC memory delay register */
#define ECC_MDR_RRI    0x000003ff      /* Refresh Request Interval */
#define ECC_MDR_MI     0x00001c00      /* MIH Delay */
#define ECC_MDR_CI     0x0000e000      /* Coherent Invalidate Delay */
#define ECC_MDR_MDL    0x001f0000      /* MBus Master arbitration delay */
#define ECC_MDR_MDH    0x03e00000      /* MBus Master arbitration delay */
#define ECC_MDR_GAD    0x7c000000      /* Graphics Arbitration Delay */
#define ECC_MDR_RSC    0x80000000      /* Refresh load control */
#define ECC_MDR_MASK   0x7fffffff

/* ECC fault status register */
#define ECC_MFSR_CE    0x00000001      /* Correctable error */
#define ECC_MFSR_BS    0x00000002      /* C2 graphics bad slot access */
#define ECC_MFSR_TO    0x00000004      /* Timeout on write */
#define ECC_MFSR_UE    0x00000008      /* Uncorrectable error */
#define ECC_MFSR_DW    0x000000f0      /* Index of double word in block */
#define ECC_MFSR_SYND  0x0000ff00      /* Syndrome for correctable error */
#define ECC_MFSR_ME    0x00010000      /* Multiple errors */
#define ECC_MFSR_C2ERR 0x00020000      /* C2 graphics error */

/* ECC fault address register 0 */
#define ECC_MFAR0_PADDR 0x0000000f     /* PA[32-35] */
#define ECC_MFAR0_TYPE  0x000000f0     /* Transaction type */
#define ECC_MFAR0_SIZE  0x00000700     /* Transaction size */
#define ECC_MFAR0_CACHE 0x00000800     /* Mapped cacheable */
#define ECC_MFAR0_LOCK  0x00001000     /* Error occurred in atomic cycle */
#define ECC_MFAR0_BMODE 0x00002000     /* Boot mode */
#define ECC_MFAR0_VADDR 0x003fc000     /* VA[12-19] (superset bits) */
#define ECC_MFAR0_S     0x08000000     /* Supervisor mode */
#define ECC_MFARO_MID   0xf0000000     /* Module ID */

/* ECC diagnostic register */
#define ECC_DR_CBX     0x00000001
#define ECC_DR_CB0     0x00000002
#define ECC_DR_CB1     0x00000004
#define ECC_DR_CB2     0x00000008
#define ECC_DR_CB4     0x00000010
#define ECC_DR_CB8     0x00000020
#define ECC_DR_CB16    0x00000040
#define ECC_DR_CB32    0x00000080
#define ECC_DR_DMODE   0x00000c00

#define ECC_NREGS      9
#define ECC_SIZE       (ECC_NREGS * sizeof(uint32_t))

#define ECC_DIAG_SIZE  4
#define ECC_DIAG_MASK  (ECC_DIAG_SIZE - 1)

typedef struct ECCState {
    qemu_irq irq;
    uint32_t regs[ECC_NREGS];
    uint8_t diag[ECC_DIAG_SIZE];
    uint32_t version;
} ECCState;

static void ecc_mem_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    ECCState *s = opaque;

    switch (addr >> 2) {
    case ECC_MER:
        if (s->version == ECC_MCC)
            s->regs[ECC_MER] = (val & ECC_MER_MASK_0);
        else if (s->version == ECC_EMC)
            s->regs[ECC_MER] = s->version | (val & ECC_MER_MASK_1);
        else if (s->version == ECC_SMC)
            s->regs[ECC_MER] = s->version | (val & ECC_MER_MASK_2);
        DPRINTF("Write memory enable %08x\n", val);
        break;
    case ECC_MDR:
        s->regs[ECC_MDR] =  val & ECC_MDR_MASK;
        DPRINTF("Write memory delay %08x\n", val);
        break;
    case ECC_MFSR:
        s->regs[ECC_MFSR] =  val;
        qemu_irq_lower(s->irq);
        DPRINTF("Write memory fault status %08x\n", val);
        break;
    case ECC_VCR:
        s->regs[ECC_VCR] =  val;
        DPRINTF("Write slot configuration %08x\n", val);
        break;
    case ECC_DR:
        s->regs[ECC_DR] =  val;
        DPRINTF("Write diagnostic %08x\n", val);
        break;
    case ECC_ECR0:
        s->regs[ECC_ECR0] =  val;
        DPRINTF("Write event count 1 %08x\n", val);
        break;
    case ECC_ECR1:
        s->regs[ECC_ECR0] =  val;
        DPRINTF("Write event count 2 %08x\n", val);
        break;
    }
}

static uint32_t ecc_mem_readl(void *opaque, target_phys_addr_t addr)
{
    ECCState *s = opaque;
    uint32_t ret = 0;

    switch (addr >> 2) {
    case ECC_MER:
        ret = s->regs[ECC_MER];
        DPRINTF("Read memory enable %08x\n", ret);
        break;
    case ECC_MDR:
        ret = s->regs[ECC_MDR];
        DPRINTF("Read memory delay %08x\n", ret);
        break;
    case ECC_MFSR:
        ret = s->regs[ECC_MFSR];
        DPRINTF("Read memory fault status %08x\n", ret);
        break;
    case ECC_VCR:
        ret = s->regs[ECC_VCR];
        DPRINTF("Read slot configuration %08x\n", ret);
        break;
    case ECC_MFAR0:
        ret = s->regs[ECC_MFAR0];
        DPRINTF("Read memory fault address 0 %08x\n", ret);
        break;
    case ECC_MFAR1:
        ret = s->regs[ECC_MFAR1];
        DPRINTF("Read memory fault address 1 %08x\n", ret);
        break;
    case ECC_DR:
        ret = s->regs[ECC_DR];
        DPRINTF("Read diagnostic %08x\n", ret);
        break;
    case ECC_ECR0:
        ret = s->regs[ECC_ECR0];
        DPRINTF("Read event count 1 %08x\n", ret);
        break;
    case ECC_ECR1:
        ret = s->regs[ECC_ECR0];
        DPRINTF("Read event count 2 %08x\n", ret);
        break;
    }
    return ret;
}

static CPUReadMemoryFunc *ecc_mem_read[3] = {
    NULL,
    NULL,
    ecc_mem_readl,
};

static CPUWriteMemoryFunc *ecc_mem_write[3] = {
    NULL,
    NULL,
    ecc_mem_writel,
};

static void ecc_diag_mem_writeb(void *opaque, target_phys_addr_t addr,
                                uint32_t val)
{
    ECCState *s = opaque;

    DPRINTF("Write diagnostic[%d] = %02x\n", (int)addr, val);
    s->diag[addr & ECC_DIAG_MASK] = val;
}

static uint32_t ecc_diag_mem_readb(void *opaque, target_phys_addr_t addr)
{
    ECCState *s = opaque;
    uint32_t ret = s->diag[(int)addr];

    DPRINTF("Read diagnostic[%d] = %02x\n", (int)addr, ret);
    return ret;
}

static CPUReadMemoryFunc *ecc_diag_mem_read[3] = {
    ecc_diag_mem_readb,
    NULL,
    NULL,
};

static CPUWriteMemoryFunc *ecc_diag_mem_write[3] = {
    ecc_diag_mem_writeb,
    NULL,
    NULL,
};

static int ecc_load(QEMUFile *f, void *opaque, int version_id)
{
    ECCState *s = opaque;
    int i;

    if (version_id != 3)
        return -EINVAL;

    for (i = 0; i < ECC_NREGS; i++)
        qemu_get_be32s(f, &s->regs[i]);

    for (i = 0; i < ECC_DIAG_SIZE; i++)
        qemu_get_8s(f, &s->diag[i]);

    qemu_get_be32s(f, &s->version);

    return 0;
}

static void ecc_save(QEMUFile *f, void *opaque)
{
    ECCState *s = opaque;
    int i;

    for (i = 0; i < ECC_NREGS; i++)
        qemu_put_be32s(f, &s->regs[i]);

    for (i = 0; i < ECC_DIAG_SIZE; i++)
        qemu_put_8s(f, &s->diag[i]);

    qemu_put_be32s(f, &s->version);
}

static void ecc_reset(void *opaque)
{
    ECCState *s = opaque;

    if (s->version == ECC_MCC)
        s->regs[ECC_MER] &= ECC_MER_REU;
    else
        s->regs[ECC_MER] &= (ECC_MER_VER | ECC_MER_IMPL | ECC_MER_MRR |
                             ECC_MER_DCI);
    s->regs[ECC_MDR] = 0x20;
    s->regs[ECC_MFSR] = 0;
    s->regs[ECC_VCR] = 0;
    s->regs[ECC_MFAR0] = 0x07c00000;
    s->regs[ECC_MFAR1] = 0;
    s->regs[ECC_DR] = 0;
    s->regs[ECC_ECR0] = 0;
    s->regs[ECC_ECR1] = 0;
}

void * ecc_init(target_phys_addr_t base, qemu_irq irq, uint32_t version)
{
    int ecc_io_memory;
    ECCState *s;

    s = qemu_mallocz(sizeof(ECCState));

    s->version = version;
    s->regs[0] = version;
    s->irq = irq;

    ecc_io_memory = cpu_register_io_memory(0, ecc_mem_read, ecc_mem_write, s);
    cpu_register_physical_memory(base, ECC_SIZE, ecc_io_memory);
    if (version == ECC_MCC) { // SS-600MP only
        ecc_io_memory = cpu_register_io_memory(0, ecc_diag_mem_read,
                                               ecc_diag_mem_write, s);
        cpu_register_physical_memory(base + 0x1000, ECC_DIAG_SIZE,
                                     ecc_io_memory);
    }
    register_savevm("ECC", base, 3, ecc_save, ecc_load, s);
    qemu_register_reset(ecc_reset, s);
    ecc_reset(s);
    return s;
}
