/* hw/s3c24xx_nand.c
 *
 * Samsung S3C24XX NAND emulation
 *
 * Copyright 2006, 2008 Ben Dooks, Daniel Silverstone and Vincent Sanders
 *
 * This file is under the terms of the GNU General Public
 * License Version 2
 */

#include "hw.h"

#include "s3c24xx.h"

#define NFCONF 0
#define NFCMD 1
#define NFADDR 2
#define NFDATA 3
#define NFSTAT 4
#define NFECC 5

#define NFCE ((s->nand_reg[NFCONF] & 1<<11) != 0)

/* NAND controller state */
struct s3c24xx_nand_state_s {
    uint32_t nand_reg[13];

    DeviceState *nand;
};

static void
s3c24xx_nand_write_f(void *opaque, target_phys_addr_t addr,
                     uint32_t value)
{
    struct s3c24xx_nand_state_s *s = (struct s3c24xx_nand_state_s *)opaque;
    int reg = (addr & 0x1f) >> 2;

    if ((reg != NFCONF) && ((s->nand_reg[NFCONF] & 1<<15) == 0)) {
        return; /* Ignore the write, the nand is not enabled */
    }

    switch (reg) {
    case NFCONF:
        s->nand_reg[reg] = value;
        if (s->nand != NULL)
            nand_setpins(s->nand, 0, 0, NFCE, 1, 0);
        break;

    case NFCMD:
        s->nand_reg[reg] = value;
        if (s->nand != NULL) {
            nand_setpins(s->nand, 1, 0, NFCE, 1, 0);
            nand_setio(s->nand, value);
        }
        break;

    case NFADDR:
        s->nand_reg[reg] = value;
        if (s->nand != NULL) {
            nand_setpins(s->nand, 0, 1, NFCE, 1, 0);
            nand_setio(s->nand, value);
        }
        break;

    case NFDATA:
        s->nand_reg[reg] = value;
        if (s->nand != NULL) {
            nand_setpins(s->nand, 0, 0, NFCE, 1, 0);
            nand_setio(s->nand, value);
        }
        break;

    default:
        /* Do nothing because the other registers are read only */
        break;
    }
}

static uint32_t
s3c24xx_nand_read_f(void *opaque, target_phys_addr_t addr)
{
    struct s3c24xx_nand_state_s *s = (struct s3c24xx_nand_state_s *)opaque;
    int reg = (addr & 0x1f) >> 2;
    uint32_t ret = s->nand_reg[reg];

    switch (reg) {
    case NFDATA:
        if (s->nand != NULL) {
            nand_setpins(s->nand, 0, 0, NFCE, 1, 0);
            ret = s->nand_reg[reg] = nand_getio(s->nand);
        } else {
            ret = s->nand_reg[ret] = 0;
        }
        break;

    case NFSTAT:
        if (s->nand != NULL) {
            nand_getpins(s->nand, (int *)&ret);
            s->nand_reg[reg] = ret;
        } else {
            ret = s->nand_reg[ret] = 0;
        }

    default:
        /* The rest read-back what was written to them */
        break;
    }

    return ret;
}

static CPUReadMemoryFunc * const s3c24xx_nand_read[] = {
    s3c24xx_nand_read_f,
    s3c24xx_nand_read_f,
    s3c24xx_nand_read_f
};

static CPUWriteMemoryFunc * const s3c24xx_nand_write[] = {
    s3c24xx_nand_write_f,
    s3c24xx_nand_write_f,
    s3c24xx_nand_write_f
};

struct s3c24xx_nand_state_s *
s3c24xx_nand_init(target_phys_addr_t base_addr)
{
    struct s3c24xx_nand_state_s *s;
    int tag;

    s = g_malloc0(sizeof(struct s3c24xx_nand_state_s));

    tag = cpu_register_io_memory(s3c24xx_nand_read, s3c24xx_nand_write,
                                 s, DEVICE_NATIVE_ENDIAN);
    cpu_register_physical_memory(base_addr, 0x40, tag);

    return s;
}

void
s3c24xx_nand_attach(struct s3c24xx_nand_state_s *s, DeviceState *nand)
{
    if (s->nand != NULL) {
        /* Detach current nand device */
        /* no cmd, no addr, not enabled, write protected, no 'gnd' */
        nand_setpins(s->nand, 0, 0, 1, 0, 0);
    }
    s->nand = nand;
}
