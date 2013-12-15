/* hw/s3c24xx_nand.c
 *
 * Samsung S3C24XX NAND emulation
 *
 * Copyright 2006, 2008 Ben Dooks, Daniel Silverstone and Vincent Sanders
 *
 * Copyright 2010, 2013 Stefan Weil
 *
 * This file is under the terms of the GNU General Public License Version 2.
 */

#include "hw/hw.h"
#include "exec/address-spaces.h" /* get_system_memory */

#include "s3c24xx.h"

#define NFCONF 0
#define NFCMD 1
#define NFADDR 2
#define NFDATA 3
#define NFSTAT 4
#define NFECC 5

#define NFCE ((s->nand_reg[NFCONF] & 1<<11) != 0)

/* NAND controller state */
typedef struct s3c24xx_nand_state_s {
    MemoryRegion mmio;
    uint32_t nand_reg[13];

    DeviceState *nand;
} S3C24xxNandState;

static void s3c24xx_nand_write(void *opaque, hwaddr addr,
                               uint64_t value, unsigned size)
{
    S3C24xxNandState *s = opaque;
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

static uint64_t s3c24xx_nand_read(void *opaque, hwaddr addr,
                                  unsigned size)
{
    S3C24xxNandState *s = opaque;
    int reg = (addr & 0x1f) >> 2;
    int value = 0;
    uint32_t ret = s->nand_reg[reg];

    switch (reg) {
    case NFDATA:
        if (s->nand != NULL) {
            nand_setpins(s->nand, 0, 0, NFCE, 1, 0);
            value = nand_getio(s->nand);
        }
        ret = s->nand_reg[ret] = value;
        break;

    case NFSTAT:
        if (s->nand != NULL) {
            nand_getpins(s->nand, &value);
        }
        ret = s->nand_reg[reg] = value;

    default:
        /* The rest read-back what was written to them */
        break;
    }

    return ret;
}

static const MemoryRegionOps s3c24xx_nand_ops = {
    .read = s3c24xx_nand_read,
    .write = s3c24xx_nand_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4
    }
};

struct s3c24xx_nand_state_s *s3c24xx_nand_init(hwaddr base_addr)
{
    S3C24xxNandState *s = g_new0(S3C24xxNandState, 1);

    memory_region_init_io(&s->mmio, OBJECT(s),
                          &s3c24xx_nand_ops, s, "s3c24xx.nand", 0x40);
    memory_region_add_subregion(get_system_memory(), base_addr, &s->mmio);

    return s;
}

void
s3c24xx_nand_attach(S3C24xxNandState *s, DeviceState *nand)
{
    if (s->nand != NULL) {
        /* Detach current nand device */
        /* no cmd, no addr, not enabled, write protected, no 'gnd' */
        nand_setpins(s->nand, 0, 0, 1, 0, 0);
    }
    s->nand = nand;
}
