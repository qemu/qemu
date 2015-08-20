/* hw/s3c24xx_clkcon.c
 *
 * Samsung S3C24XX Clock control emulation
 *
 * Copyright 2006, 2007, 2008 Daniel Silverstone and Vincent Sanders
 *
 * Copyright 2010, 2013 Stefan Weil
 *
 * This file is under the terms of the GNU General Public License Version 2.
 */

#include "hw/hw.h"
#include "exec/address-spaces.h" /* get_system_memory */

#include "s3c24xx.h"

/* Lock time RW */
#define S3C_REG_LOCKTIME 0

/* MPLL Control RW */
#define S3C_REG_MPLLCON 1

/* UPLL Control RW */
#define S3C_REG_UPLLCON 2

/* Clock Generator Control RW */
#define S3C_REG_CLKCON 3

/* CLKCON IDLE */
#define S3C_REG_CLKCON_IDLE (1<<2)

/* Slow Clock Control RW */
#define S3C_REG_CLKSLOW 4

/* Clock divider control RW */
#define S3C_REG_CLKDIVN 5

/* Clock controller state */
struct s3c24xx_clkcon_state_s {
    MemoryRegion mmio;
    CPUARMState *cpu_env;
    uint32_t ref_freq; /* frequency of reference xtal or extclock */
    uint32_t clkcon_reg[7];
};

static void s3c24xx_clkcon_write(void *opaque, hwaddr addr_,
                                 uint64_t value, unsigned size)
{
    struct s3c24xx_clkcon_state_s *s = opaque;
    unsigned addr = (addr_ & 0x1F) >> 2;
    int idle_rising_edge = 0;

    assert(addr < ARRAY_SIZE(s->clkcon_reg));

    if (addr == S3C_REG_CLKCON) {
        if (!(s->clkcon_reg[addr] & S3C_REG_CLKCON_IDLE) &&
            (value & S3C_REG_CLKCON_IDLE))
            idle_rising_edge = 1;
    }

    s->clkcon_reg[addr] = value;

    if (idle_rising_edge) {
        cpu_interrupt(CPU(s), CPU_INTERRUPT_HALT);
    }
}

static uint64_t s3c24xx_clkcon_read(void *opaque, hwaddr addr_,
                                    unsigned size)
{
    struct s3c24xx_clkcon_state_s *s = opaque;
    unsigned addr = (addr_ & 0x1F) >> 2;

    assert(addr < ARRAY_SIZE(s->clkcon_reg));

    return s->clkcon_reg[addr];
}

static const MemoryRegionOps s3c24xx_clkcon_ops = {
    .read = s3c24xx_clkcon_read,
    .write = s3c24xx_clkcon_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4
    }
};

static void s3c24xx_clkcon_save(QEMUFile *f, void *opaque)
{
    struct s3c24xx_clkcon_state_s *s = (struct s3c24xx_clkcon_state_s *)opaque;
    int i;

    for (i = 0; i < ARRAY_SIZE(s->clkcon_reg); i ++) {
        qemu_put_be32s(f, &s->clkcon_reg[i]);
    }
}

static int s3c24xx_clkcon_load(QEMUFile *f, void *opaque, int version_id)
{
    struct s3c24xx_clkcon_state_s *s = opaque;
    int i;

    for (i = 0; i < ARRAY_SIZE(s->clkcon_reg); i ++) {
        qemu_get_be32s(f, &s->clkcon_reg[i]);
    }

    return 0;
}

struct s3c24xx_clkcon_state_s *
s3c24xx_clkcon_init(S3CState *soc, hwaddr base_addr, uint32_t ref_freq)
{
    struct s3c24xx_clkcon_state_s *s = g_new0(struct s3c24xx_clkcon_state_s, 1);

    memory_region_init_io(&s->mmio, OBJECT(s), &s3c24xx_clkcon_ops, s,
                          "s3c24xx.clkcon", ARRAY_SIZE(s->clkcon_reg) * 4);
    memory_region_add_subregion(get_system_memory(), base_addr, &s->mmio);
    register_savevm(NULL, "s3c24xx_clkcon", 0, 0, s3c24xx_clkcon_save, s3c24xx_clkcon_load, s);

    s->cpu_env = &soc->cpu->env;
    s->ref_freq = ref_freq;

    /* initialise register values to power on defaults */
    s->clkcon_reg[S3C_REG_LOCKTIME] = 0x00FFFFFF;
    s->clkcon_reg[S3C_REG_MPLLCON] = 0x0005C080;
    s->clkcon_reg[S3C_REG_UPLLCON] = 0x00028080;
    s->clkcon_reg[S3C_REG_CLKCON] = 0x0007FFF0;
    s->clkcon_reg[S3C_REG_CLKSLOW] = 0x00000004;
    s->clkcon_reg[S3C_REG_CLKDIVN] = 0x00000000;

    return s;
}
