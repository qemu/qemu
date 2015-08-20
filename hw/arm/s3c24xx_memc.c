/* hw/s3c24xx_memc.c
 *
 * Samsung S3C24XX memory controller emulation.
 *
 * The SDRAM controller on several S3C SOC is generic, the emulation needs to
 * be little more than backing the registers.
 *
 * Copyright 2006, 2007 Daniel Silverstone and Vincent Sanders
 *
 * Copyright 2010, 2013 Stefan Weil
 *
 * This file is under the terms of the GNU General Public License Version 2.
 */

#include "hw/hw.h"
#include "exec/address-spaces.h" /* get_system_memory */

#include "s3c24xx.h"

/* Memory controller state */
struct s3c24xx_memc_state_s {
    MemoryRegion mmio;
    uint32_t memc_reg[13];
};

static void s3c24xx_memc_write(void *opaque, hwaddr addr_,
                               uint64_t value, unsigned size)
{
    struct s3c24xx_memc_state_s *s = opaque;
    int addr = (addr_ & 0x3f) >> 2;

    if (addr < 0 || addr > 12)
        addr = 12;

    s->memc_reg[addr] = value;
}

static uint64_t s3c24xx_memc_read(void *opaque, hwaddr addr_,
                                  unsigned size)
{
    struct s3c24xx_memc_state_s *s = opaque;
    int addr = (addr_ & 0x3f) >> 2;

    if (addr < 0 || addr > 12)
        addr = 12;

    return s->memc_reg[addr];
}

static const MemoryRegionOps s3c24xx_memc_ops = {
    .read = s3c24xx_memc_read,
    .write = s3c24xx_memc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4
    }
};

static void s3c24xx_memc_save(QEMUFile *f, void *opaque)
{
    struct s3c24xx_memc_state_s *s = opaque;
    int i;

    for (i = 0; i < 13; i ++)
        qemu_put_be32s(f, &s->memc_reg[i]);
}

static int s3c24xx_memc_load(QEMUFile *f, void *opaque, int version_id)
{
    struct s3c24xx_memc_state_s *s = opaque;
    int i;

    for (i = 0; i < 13; i ++)
        qemu_get_be32s(f, &s->memc_reg[i]);

    return 0;
}

struct s3c24xx_memc_state_s *
s3c24xx_memc_init(hwaddr base_addr)
{
    /* Memory controller is simple SDRAM control. As SDRAM is emulated and
     * requires no setup the emulation needs to be nothing more than memory
     * backing the registers.
     *
     * There are 13 registers, each 4 bytes.
     */
    struct s3c24xx_memc_state_s *s = g_new0(struct s3c24xx_memc_state_s, 1);

    memory_region_init_io(&s->mmio, OBJECT(s), &s3c24xx_memc_ops, s,
                          "s3c24xx.memc", 13 * 4);
    memory_region_add_subregion(get_system_memory(), base_addr, &s->mmio);
    register_savevm(NULL, "s3c24xx_memc", 0, 0, s3c24xx_memc_save, s3c24xx_memc_load, s);

    return s;
}
