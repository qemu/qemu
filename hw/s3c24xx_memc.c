/* hw/s3c24xx_memc.c
 *
 * Samsung S3C24XX memory controller emulation.
 *
 * The SDRAM controller on several S3C SOC is generic, the emulation needs to
 * be little more than backing the registers.
 *
 * Copyright 2006, 2007 Daniel Silverstone and Vincent Sanders
 *
 * This file is under the terms of the GNU General Public
 * License Version 2
 */

#include "hw.h"

#include "s3c24xx.h"

/* Memory controller state */
struct s3c24xx_memc_state_s {
    uint32_t memc_reg[13];
};

static void
s3c24xx_memc_write_f(void *opaque, target_phys_addr_t addr_, uint32_t value)
{
    struct s3c24xx_memc_state_s *s = (struct s3c24xx_memc_state_s *)opaque;
    int addr = (addr_ & 0x3f) >> 2;

    if (addr < 0 || addr > 12)
        addr = 12;

    s->memc_reg[addr] = value;
}

static uint32_t
s3c24xx_memc_read_f(void *opaque, target_phys_addr_t addr_)
{
    struct s3c24xx_memc_state_s *s = (struct s3c24xx_memc_state_s *)opaque;
    int addr = (addr_ & 0x3f) >> 2;

    if (addr < 0 || addr > 12)
        addr = 12;

    return s->memc_reg[addr];
}

static CPUReadMemoryFunc * const s3c24xx_memc_read[] = {
    s3c24xx_memc_read_f,
    s3c24xx_memc_read_f,
    s3c24xx_memc_read_f,
};

static CPUWriteMemoryFunc * const s3c24xx_memc_write[] = {
    s3c24xx_memc_write_f,
    s3c24xx_memc_write_f,
    s3c24xx_memc_write_f,
};

static void s3c24xx_memc_save(QEMUFile *f, void *opaque)
{
    struct s3c24xx_memc_state_s *s = (struct s3c24xx_memc_state_s *)opaque;
    int i;

    for (i = 0; i < 13; i ++)
        qemu_put_be32s(f, &s->memc_reg[i]);
}

static int s3c24xx_memc_load(QEMUFile *f, void *opaque, int version_id)
{
    struct s3c24xx_memc_state_s *s = (struct s3c24xx_memc_state_s *)opaque;
    int i;

    for (i = 0; i < 13; i ++)
        qemu_get_be32s(f, &s->memc_reg[i]);

    return 0;
}

struct s3c24xx_memc_state_s *
s3c24xx_memc_init(target_phys_addr_t base_addr)
{
    /* Memory controller is simple SDRAM control. As SDRAM is emulated and
     * requires no setup the emulation needs to be nothing more than memory
     * backing the registers.
     *
     * There are 13 registers, each 4 bytes.
     */
    struct s3c24xx_memc_state_s *s = g_malloc0(sizeof(struct s3c24xx_memc_state_s));

    int tag;
    tag = cpu_register_io_memory(s3c24xx_memc_read, s3c24xx_memc_write, s,
                                 DEVICE_NATIVE_ENDIAN);
    cpu_register_physical_memory(base_addr, 13 * 4, tag);
    register_savevm(NULL, "s3c24xx_memc", 0, 0, s3c24xx_memc_save, s3c24xx_memc_load, s);

    return s;
}
