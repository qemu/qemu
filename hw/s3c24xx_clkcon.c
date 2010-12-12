/* hw/s3c24xx_clkcon.c
 *
 * Samsung S3C24XX Clock control emulation
 *
 * Copyright 2006, 2007, 2008 Daniel Silverstone and Vincent Sanders
 *
 * This file is under the terms of the GNU General Public
 * License Version 2
 */

#include "hw.h"

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
    CPUState *cpu_env;
    uint32_t ref_freq; /* frequency of reference xtal or extclock */
    uint32_t clkcon_reg[7];
};

static void
s3c24xx_clkcon_write_f(void *opaque, target_phys_addr_t addr_, uint32_t value)
{
    struct s3c24xx_clkcon_state_s *s = (struct s3c24xx_clkcon_state_s *)opaque;
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
        cpu_interrupt(s->cpu_env, CPU_INTERRUPT_HALT);
    }
}

static uint32_t
s3c24xx_clkcon_read_f(void *opaque, target_phys_addr_t addr_)
{
    struct s3c24xx_clkcon_state_s *s = (struct s3c24xx_clkcon_state_s *)opaque;
    unsigned addr = (addr_ & 0x1F) >> 2;

    assert(addr < ARRAY_SIZE(s->clkcon_reg));

    return s->clkcon_reg[addr];
}

static CPUReadMemoryFunc * const s3c24xx_clkcon_read[] = {
    s3c24xx_clkcon_read_f,
    s3c24xx_clkcon_read_f,
    s3c24xx_clkcon_read_f,
};

static CPUWriteMemoryFunc * const s3c24xx_clkcon_write[] = {
    s3c24xx_clkcon_write_f,
    s3c24xx_clkcon_write_f,
    s3c24xx_clkcon_write_f,
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
    struct s3c24xx_clkcon_state_s *s = (struct s3c24xx_clkcon_state_s *)opaque;
    int i;

    for (i = 0; i < ARRAY_SIZE(s->clkcon_reg); i ++) {
        qemu_get_be32s(f, &s->clkcon_reg[i]);
    }

    return 0;
}

struct s3c24xx_clkcon_state_s *
s3c24xx_clkcon_init(S3CState *soc, target_phys_addr_t base_addr, uint32_t ref_freq)
{
    int tag;
    struct s3c24xx_clkcon_state_s *s;

    s = qemu_mallocz(sizeof(struct s3c24xx_clkcon_state_s));

    tag = cpu_register_io_memory(s3c24xx_clkcon_read, s3c24xx_clkcon_write, s,
                                 DEVICE_NATIVE_ENDIAN);
    cpu_register_physical_memory(base_addr, ARRAY_SIZE(s->clkcon_reg) * 4, tag);
    register_savevm(NULL, "s3c24xx_clkcon", 0, 0, s3c24xx_clkcon_save, s3c24xx_clkcon_load, s);

    s->cpu_env = soc->cpu_env;
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
