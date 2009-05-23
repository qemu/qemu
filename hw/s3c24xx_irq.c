/* hw/s3c24xx_irq.c
 *
 * Samsung S3C24XX IRQ controller emulation
 *
 * Copyright 2009 Daniel Silverstone and Vincent Sanders
 *
 * This file is under the terms of the GNU General Public
 * License Version 2
 */

#include "hw.h"

#include "s3c24xx.h"

/* IRQ request status              RW WORD */
#define S3C_IRQ_SRCPND 0
/* Interrupt mode control          WR WORD */
#define S3C_IRQ_INTMOD 1
/* Interrupt mask control          RW WORD */
#define S3C_IRQ_INTMSK 2
/* IRQ priority control            WR WORD */
#define S3C_IRQ_PRIORITY 3
/* Interrupt request status        RW WORD */
#define S3C_IRQ_INTPND 4
/* Interrupt request source offset RO WORD */
#define S3C_IRQ_OFFSET 5
/* Sub-source pending              RW WORD */
#define S3C_IRQ_SUBSRCPND 6
/* Interrupt sub-mask              RW WORD */
#define S3C_IRQ_INTSUBMSK 7

/* Interrupt controller state */
struct s3c24xx_irq_state_s {
    CPUState *cpu_env;

    qemu_irq *irqs;

    uint32_t irq_main_level, irq_subsrc_level;
    uint32_t irq_reg[8];
};


/* Take the status of the srcpnd register, percolate it through, raise to CPU
 * if necessary
 */
static void
s3c24xx_percolate_interrupt(struct s3c24xx_irq_state_s *s)
{
    uint32_t ints = (s->irq_reg[S3C_IRQ_SRCPND] & ~s->irq_reg[S3C_IRQ_INTMSK]);
    int fsb = ffs(ints);

    /* TODO: Priority encoder could go here */
    if (ints & s->irq_reg[S3C_IRQ_INTMOD]) {
        /* Detected a FIQ */
        cpu_interrupt(s->cpu_env, CPU_INTERRUPT_FIQ);
        return;
    } else {
        /* No FIQ here today */
        cpu_reset_interrupt(s->cpu_env, CPU_INTERRUPT_FIQ);
    }

    /* No FIQ, check for a normal IRQ */
    if (fsb) {
        if ((s->irq_reg[S3C_IRQ_INTPND] == 0) ||
            (s->irq_reg[S3C_IRQ_INTPND] > 1<<(fsb-1))) {
            /* Current INTPND is lower priority than fsb of ints (or empty) */
            s->irq_reg[S3C_IRQ_INTPND] = 1<<(fsb-1);
            s->irq_reg[S3C_IRQ_OFFSET] = fsb-1;
        }
    } else {
        /* No FSB, thus no IRQ, thus nothing to do yet */
    }

    if (s->irq_reg[S3C_IRQ_INTPND] != 0) {
        cpu_interrupt(s->cpu_env, CPU_INTERRUPT_HARD);
    } else {
        cpu_reset_interrupt(s->cpu_env, CPU_INTERRUPT_HARD);
    }
}

static void
s3c24xx_percolate_subsrc_interrupt(struct s3c24xx_irq_state_s *s)
{
    uint32_t ints;

    s->irq_reg[S3C_IRQ_SRCPND] |= s->irq_main_level;
    s->irq_reg[S3C_IRQ_SUBSRCPND] |= s->irq_subsrc_level;

    ints = (s->irq_reg[S3C_IRQ_SUBSRCPND] &
            ~s->irq_reg[S3C_IRQ_INTSUBMSK]);

    /* If UART0 has asserted, raise that */
    if (ints & 0x7) {
        s->irq_reg[S3C_IRQ_SRCPND] |= (1<<28);
    }

    /* Ditto UART1 */
    if (ints & 0x7<<3)
        s->irq_reg[S3C_IRQ_SRCPND] |= (1<<23);

    /* Ditto UART2 */
    if (ints & 0x7<<6)
        s->irq_reg[S3C_IRQ_SRCPND] |= (1<<15);

    /* And percolate it through */
    s3c24xx_percolate_interrupt(s);
}

static void
s3c24xx_irq_write_f(void *opaque, target_phys_addr_t addr_, uint32_t value)
{
    struct s3c24xx_irq_state_s *s = (struct s3c24xx_irq_state_s *)opaque;
    int addr = (addr_ >> 2) & 7;

    if (addr == S3C_IRQ_SRCPND ||
        addr == S3C_IRQ_INTPND ||
        addr == S3C_IRQ_SUBSRCPND) {
        s->irq_reg[addr] &= ~value;
    } else {
        s->irq_reg[addr] = value;
    }

    /* Start at the subsrc irqs and percolate from there */
    s3c24xx_percolate_subsrc_interrupt(s);
}

static uint32_t
s3c24xx_irq_read_f(void *opaque, target_phys_addr_t addr_)
{
    struct s3c24xx_irq_state_s *s = (struct s3c24xx_irq_state_s *)opaque;
    int addr = (addr_ >> 2) & 0x7;

    return s->irq_reg[addr];
}


static CPUReadMemoryFunc * const s3c24xx_irq_read[] = {
    s3c24xx_irq_read_f,
    s3c24xx_irq_read_f,
    s3c24xx_irq_read_f,
};

static CPUWriteMemoryFunc * const s3c24xx_irq_write[] = {
    s3c24xx_irq_write_f,
    s3c24xx_irq_write_f,
    s3c24xx_irq_write_f,
};

static void
s3c24xx_irq_set_interrupt_level(struct s3c24xx_irq_state_s *s, int irq_num, int level, int set_level)
{
    if (level) {
        if (set_level)
            s->irq_main_level |= 1<<irq_num;
        s->irq_reg[S3C_IRQ_SRCPND] |= 1<<irq_num;
    } else {
        s->irq_main_level &= ~(1<<irq_num);
        s->irq_reg[S3C_IRQ_SRCPND] &= ~(1<<irq_num);
    }
    s3c24xx_percolate_subsrc_interrupt(s);
}

static void
s3c24xx_irq_set_subsrc_interrupt_level(struct s3c24xx_irq_state_s *s, int irq_num, int level, int set_level)
{
    if (level) {
        if (set_level)
            s->irq_subsrc_level |= 1<<irq_num;
        s->irq_reg[S3C_IRQ_SUBSRCPND] |= 1<<irq_num;
    } else {
        s->irq_subsrc_level &= ~(1<<irq_num);
        s->irq_reg[S3C_IRQ_SUBSRCPND] &= ~(1<<irq_num);
    }
    s3c24xx_percolate_subsrc_interrupt(s);
}

static void
s3c24xx_irq_handler(void *opaque, int _n, int level)
{
    struct s3c24xx_irq_state_s *s = (struct s3c24xx_irq_state_s *)opaque;
    int irq_num = _n % 32;
    int is_subsrc = (_n & 32)?1:0;
    int is_level = (_n & 64)?1:0;

    if (is_subsrc == 0)
        s3c24xx_irq_set_interrupt_level(s, irq_num, level, is_level);
    else
        s3c24xx_irq_set_subsrc_interrupt_level(s, irq_num, level, is_level);
}

static void s3c24xx_irq_save(QEMUFile *f, void *opaque)
{
    struct s3c24xx_irq_state_s *s = (struct s3c24xx_irq_state_s *)opaque;
    int i;

    for (i = 0; i < 8; i ++)
        qemu_put_be32s(f, &s->irq_reg[i]);
}

static int s3c24xx_irq_load(QEMUFile *f, void *opaque, int version_id)
{
    struct s3c24xx_irq_state_s *s = (struct s3c24xx_irq_state_s *)opaque;
    int i;

    for (i = 0; i < 8; i ++)
        qemu_get_be32s(f, &s->irq_reg[i]);

    return 0;
}

struct s3c24xx_irq_state_s *
s3c24xx_irq_init(S3CState *soc, target_phys_addr_t base_addr)
{
    struct s3c24xx_irq_state_s * s;
    int tag;

    s = qemu_mallocz(sizeof(struct s3c24xx_irq_state_s));

    /* Samsung S3C24XX IRQ registration. */
    tag = cpu_register_io_memory(s3c24xx_irq_read, s3c24xx_irq_write, s);
    cpu_register_physical_memory(base_addr, 8 * 4, tag);
    register_savevm(NULL, "s3c24xx_irq", 0, 0, s3c24xx_irq_save, s3c24xx_irq_load, s);

    s->cpu_env = soc->cpu_env;

    /* Set up registers to power on values */
    s->irq_reg[S3C_IRQ_SRCPND] = 0x00;
    s->irq_reg[S3C_IRQ_INTMOD] = 0x00;
    s->irq_reg[S3C_IRQ_INTMSK] = 0xFFFFFFFF;
    s->irq_reg[S3C_IRQ_PRIORITY] = 0x7F;
    s->irq_reg[S3C_IRQ_INTPND] = 0x00;
    s->irq_reg[S3C_IRQ_OFFSET] = 0x00;
    s->irq_reg[S3C_IRQ_SUBSRCPND] = 0x00;
    s->irq_reg[S3C_IRQ_INTSUBMSK] = 0x7FF;

    /* Allocate the interrupts and return them. All 64 potential ones.
     * We return them doubled up because the latter half are level where
     * the former half are edge.
     */
    s->irqs =  qemu_allocate_irqs(s3c24xx_irq_handler, s, 128);

    return s;
}

/* get the qemu interrupt from an irq number */
qemu_irq
s3c24xx_get_irq(struct s3c24xx_irq_state_s *s, int inum)
{
    return s->irqs[inum];
}
