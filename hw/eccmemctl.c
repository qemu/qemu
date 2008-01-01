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
#define DPRINTF(fmt, args...)                           \
    do { printf("ECC: " fmt , ##args); } while (0)
#else
#define DPRINTF(fmt, args...)
#endif

/* There are 3 versions of this chip used in SMP sun4m systems:
 * MCC (version 0, implementation 0) SS-600MP
 * EMC (version 0, implementation 1) SS-10
 * SMC (version 0, implementation 2) SS-10SX and SS-20
 */

/* Register offsets */
#define ECC_FCR_REG    0
#define ECC_FSR_REG    8
#define ECC_FAR0_REG   16
#define ECC_FAR1_REG   20
#define ECC_DIAG_REG   24

/* ECC fault control register */
#define ECC_FCR_EE     0x00000001      /* Enable ECC checking */
#define ECC_FCR_EI     0x00000010      /* Enable Interrupts on correctable errors */
#define ECC_FCR_VER    0x0f000000      /* Version */
#define ECC_FCR_IMPL   0xf0000000      /* Implementation */

/* ECC fault status register */
#define ECC_FSR_CE     0x00000001      /* Correctable error */
#define ECC_FSR_BS     0x00000002      /* C2 graphics bad slot access */
#define ECC_FSR_TO     0x00000004      /* Timeout on write */
#define ECC_FSR_UE     0x00000008      /* Uncorrectable error */
#define ECC_FSR_DW     0x000000f0      /* Index of double word in block */
#define ECC_FSR_SYND   0x0000ff00      /* Syndrome for correctable error */
#define ECC_FSR_ME     0x00010000      /* Multiple errors */
#define ECC_FSR_C2ERR  0x00020000      /* C2 graphics error */

/* ECC fault address register 0 */
#define ECC_FAR0_PADDR 0x0000000f      /* PA[32-35] */
#define ECC_FAR0_TYPE  0x000000f0      /* Transaction type */
#define ECC_FAR0_SIZE  0x00000700      /* Transaction size */
#define ECC_FAR0_CACHE 0x00000800      /* Mapped cacheable */
#define ECC_FAR0_LOCK  0x00001000      /* Error occurred in attomic cycle */
#define ECC_FAR0_BMODE 0x00002000      /* Boot mode */
#define ECC_FAR0_VADDR 0x003fc000      /* VA[12-19] (superset bits) */
#define ECC_FAR0_S     0x08000000      /* Supervisor mode */
#define ECC_FARO_MID   0xf0000000      /* Module ID */

/* ECC diagnostic register */
#define ECC_DIAG_CBX   0x00000001
#define ECC_DIAG_CB0   0x00000002
#define ECC_DIAG_CB1   0x00000004
#define ECC_DIAG_CB2   0x00000008
#define ECC_DIAG_CB4   0x00000010
#define ECC_DIAG_CB8   0x00000020
#define ECC_DIAG_CB16  0x00000040
#define ECC_DIAG_CB32  0x00000080
#define ECC_DIAG_DMODE 0x00000c00

#define ECC_NREGS      8
#define ECC_SIZE       (ECC_NREGS * sizeof(uint32_t))
#define ECC_ADDR_MASK  (ECC_SIZE - 1)

typedef struct ECCState {
    uint32_t regs[ECC_NREGS];
} ECCState;

static void ecc_mem_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    ECCState *s = opaque;

    switch (addr & ECC_ADDR_MASK) {
    case ECC_FCR_REG:
        s->regs[0] = (s->regs[0] & (ECC_FCR_VER | ECC_FCR_IMPL)) |
                     (val & ~(ECC_FCR_VER | ECC_FCR_IMPL));
        DPRINTF("Write fault control %08x\n", val);
        break;
    case 4:
        s->regs[1] =  val;
        DPRINTF("Write reg[1] %08x\n", val);
        break;
    case ECC_FSR_REG:
        s->regs[2] =  val;
        DPRINTF("Write fault status %08x\n", val);
        break;
    case 12:
        s->regs[3] =  val;
        DPRINTF("Write reg[3] %08x\n", val);
        break;
    case ECC_FAR0_REG:
        s->regs[4] =  val;
        DPRINTF("Write fault address 0 %08x\n", val);
        break;
    case ECC_FAR1_REG:
        s->regs[5] =  val;
        DPRINTF("Write fault address 1 %08x\n", val);
        break;
    case ECC_DIAG_REG:
        s->regs[6] =  val;
        DPRINTF("Write diag %08x\n", val);
        break;
    case 28:
        s->regs[7] =  val;
        DPRINTF("Write reg[7] %08x\n", val);
        break;
    }
}

static uint32_t ecc_mem_readl(void *opaque, target_phys_addr_t addr)
{
    ECCState *s = opaque;
    uint32_t ret = 0;

    switch (addr & ECC_ADDR_MASK) {
    case ECC_FCR_REG:
        ret = s->regs[0];
        DPRINTF("Read enable %08x\n", ret);
        break;
    case 4:
        ret = s->regs[1];
        DPRINTF("Read register[1] %08x\n", ret);
        break;
    case ECC_FSR_REG:
        ret = s->regs[2];
        DPRINTF("Read fault status %08x\n", ret);
        break;
    case 12:
        ret = s->regs[3];
        DPRINTF("Read reg[3] %08x\n", ret);
        break;
    case ECC_FAR0_REG:
        ret = s->regs[4];
        DPRINTF("Read fault address 0 %08x\n", ret);
        break;
    case ECC_FAR1_REG:
        ret = s->regs[5];
        DPRINTF("Read fault address 1 %08x\n", ret);
        break;
    case ECC_DIAG_REG:
        ret = s->regs[6];
        DPRINTF("Read diag %08x\n", ret);
        break;
    case 28:
        ret = s->regs[7];
        DPRINTF("Read reg[7] %08x\n", ret);
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

static int ecc_load(QEMUFile *f, void *opaque, int version_id)
{
    ECCState *s = opaque;
    int i;

    if (version_id != 1)
        return -EINVAL;

    for (i = 0; i < ECC_NREGS; i++)
        qemu_get_be32s(f, &s->regs[i]);

    return 0;
}

static void ecc_save(QEMUFile *f, void *opaque)
{
    ECCState *s = opaque;
    int i;

    for (i = 0; i < ECC_NREGS; i++)
        qemu_put_be32s(f, &s->regs[i]);
}

static void ecc_reset(void *opaque)
{
    ECCState *s = opaque;
    int i;

    s->regs[ECC_FCR_REG] &= (ECC_FCR_VER | ECC_FCR_IMPL);

    for (i = 1; i < ECC_NREGS; i++)
        s->regs[i] = 0;
}

void * ecc_init(target_phys_addr_t base, uint32_t version)
{
    int ecc_io_memory;
    ECCState *s;

    s = qemu_mallocz(sizeof(ECCState));
    if (!s)
        return NULL;

    s->regs[0] = version;

    ecc_io_memory = cpu_register_io_memory(0, ecc_mem_read, ecc_mem_write, s);
    cpu_register_physical_memory(base, ECC_SIZE, ecc_io_memory);
    register_savevm("ECC", base, 1, ecc_save, ecc_load, s);
    qemu_register_reset(ecc_reset, s);
    ecc_reset(s);
    return s;
}
