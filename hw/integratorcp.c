/* 
 * ARM Integrator CP System emulation.
 *
 * Copyright (c) 2005 CodeSourcery, LLC.
 * Written by Paul Brook
 *
 * This code is licenced under the GPL
 */

#include <vl.h>

#define KERNEL_ARGS_ADDR 0x100
#define KERNEL_LOAD_ADDR 0x00010000
#define INITRD_LOAD_ADDR 0x00800000

/* Stub functions for hardware that doesn't exist.  */
void pic_set_irq(int irq, int level)
{
    cpu_abort (cpu_single_env, "pic_set_irq");
}

void pic_info(void)
{
}

void irq_info(void)
{
}

static void *lcd;

void vga_update_display(void)
{
    pl110_update_display(lcd);
}

void vga_screen_dump(const char *filename)
{
}

void vga_invalidate_display(void)
{
    pl110_invalidate_display(lcd);
}

void DMA_run (void)
{
}

typedef struct {
    uint32_t flash_offset;
    uint32_t cm_osc;
    uint32_t cm_ctrl;
    uint32_t cm_lock;
    uint32_t cm_auxosc;
    uint32_t cm_sdram;
    uint32_t cm_init;
    uint32_t cm_flags;
    uint32_t cm_nvflags;
    uint32_t int_level;
    uint32_t irq_enabled;
    uint32_t fiq_enabled;
} integratorcm_state;

static uint8_t integrator_spd[128] = {
   128, 8, 4, 11, 9, 1, 64, 0,  2, 0xa0, 0xa0, 0, 0, 8, 0, 1,
   0xe, 4, 0x1c, 1, 2, 0x20, 0xc0, 0, 0, 0, 0, 0x30, 0x28, 0x30, 0x28, 0x40
};

static uint32_t integratorcm_read(void *opaque, target_phys_addr_t offset)
{
    integratorcm_state *s = (integratorcm_state *)opaque;
    offset -= 0x10000000;
    if (offset >= 0x100 && offset < 0x200) {
        /* CM_SPD */
        if (offset >= 0x180)
            return 0;
        return integrator_spd[offset >> 2];
    }
    switch (offset >> 2) {
    case 0: /* CM_ID */
        return 0x411a3001;
    case 1: /* CM_PROC */
        return 0;
    case 2: /* CM_OSC */
        return s->cm_osc;
    case 3: /* CM_CTRL */
        return s->cm_ctrl;
    case 4: /* CM_STAT */
        return 0x00100000;
    case 5: /* CM_LOCK */
        if (s->cm_lock == 0xa05f) {
            return 0x1a05f;
        } else {
            return s->cm_lock;
        }
    case 6: /* CM_LMBUSCNT */
        /* ??? High frequency timer.  */
        cpu_abort(cpu_single_env, "integratorcm_read: CM_LMBUSCNT");
    case 7: /* CM_AUXOSC */
        return s->cm_auxosc;
    case 8: /* CM_SDRAM */
        return s->cm_sdram;
    case 9: /* CM_INIT */
        return s->cm_init;
    case 10: /* CM_REFCT */
        /* ??? High frequency timer.  */
        cpu_abort(cpu_single_env, "integratorcm_read: CM_REFCT");
    case 12: /* CM_FLAGS */
        return s->cm_flags;
    case 14: /* CM_NVFLAGS */
        return s->cm_nvflags;
    case 16: /* CM_IRQ_STAT */
        return s->int_level & s->irq_enabled;
    case 17: /* CM_IRQ_RSTAT */
        return s->int_level;
    case 18: /* CM_IRQ_ENSET */
        return s->irq_enabled;
    case 20: /* CM_SOFT_INTSET */
        return s->int_level & 1;
    case 24: /* CM_FIQ_STAT */
        return s->int_level & s->fiq_enabled;
    case 25: /* CM_FIQ_RSTAT */
        return s->int_level;
    case 26: /* CM_FIQ_ENSET */
        return s->fiq_enabled;
    case 32: /* CM_VOLTAGE_CTL0 */
    case 33: /* CM_VOLTAGE_CTL1 */
    case 34: /* CM_VOLTAGE_CTL2 */
    case 35: /* CM_VOLTAGE_CTL3 */
        /* ??? Voltage control unimplemented.  */
        return 0;
    default:
        cpu_abort (cpu_single_env,
            "integratorcm_read: Unimplemented offset 0x%x\n", offset);
        return 0;
    }
}

static void integratorcm_do_remap(integratorcm_state *s, int flash)
{
    if (flash) {
        cpu_register_physical_memory(0, 0x100000, IO_MEM_RAM);
    } else {
        cpu_register_physical_memory(0, 0x100000, s->flash_offset | IO_MEM_RAM);
    }
    //??? tlb_flush (cpu_single_env, 1);
}

static void integratorcm_set_ctrl(integratorcm_state *s, uint32_t value)
{
    if (value & 8) {
        cpu_abort(cpu_single_env, "Board reset\n");
    }
    if ((s->cm_init ^ value) & 4) {
        integratorcm_do_remap(s, (value & 4) == 0);
    }
    if ((s->cm_init ^ value) & 1) {
        printf("Green LED %s\n", (value & 1) ? "on" : "off");
    }
    s->cm_init = (s->cm_init & ~ 5) | (value ^ 5);
}

static void integratorcm_update(integratorcm_state *s)
{
    /* ??? The CPU irq/fiq is raised when either the core module or base PIC
       are active.  */
    if (s->int_level & (s->irq_enabled | s->fiq_enabled))
        cpu_abort(cpu_single_env, "Core module interrupt\n");
}

static void integratorcm_write(void *opaque, target_phys_addr_t offset,
                               uint32_t value)
{
    integratorcm_state *s = (integratorcm_state *)opaque;
    offset -= 0x10000000;
    switch (offset >> 2) {
    case 2: /* CM_OSC */
        if (s->cm_lock == 0xa05f)
            s->cm_osc = value;
        break;
    case 3: /* CM_CTRL */
        integratorcm_set_ctrl(s, value);
        break;
    case 5: /* CM_LOCK */
        s->cm_lock = value & 0xffff;
        break;
    case 7: /* CM_AUXOSC */
        if (s->cm_lock == 0xa05f)
            s->cm_auxosc = value;
        break;
    case 8: /* CM_SDRAM */
        s->cm_sdram = value;
        break;
    case 9: /* CM_INIT */
        /* ??? This can change the memory bus frequency.  */
        s->cm_init = value;
        break;
    case 12: /* CM_FLAGSS */
        s->cm_flags |= value;
        break;
    case 13: /* CM_FLAGSC */
        s->cm_flags &= ~value;
        break;
    case 14: /* CM_NVFLAGSS */
        s->cm_nvflags |= value;
        break;
    case 15: /* CM_NVFLAGSS */
        s->cm_nvflags &= ~value;
        break;
    case 18: /* CM_IRQ_ENSET */
        s->irq_enabled |= value;
        integratorcm_update(s);
        break;
    case 19: /* CM_IRQ_ENCLR */
        s->irq_enabled &= ~value;
        integratorcm_update(s);
        break;
    case 20: /* CM_SOFT_INTSET */
        s->int_level |= (value & 1);
        integratorcm_update(s);
        break;
    case 21: /* CM_SOFT_INTCLR */
        s->int_level &= ~(value & 1);
        integratorcm_update(s);
        break;
    case 26: /* CM_FIQ_ENSET */
        s->fiq_enabled |= value;
        integratorcm_update(s);
        break;
    case 27: /* CM_FIQ_ENCLR */
        s->fiq_enabled &= ~value;
        integratorcm_update(s);
        break;
    case 32: /* CM_VOLTAGE_CTL0 */
    case 33: /* CM_VOLTAGE_CTL1 */
    case 34: /* CM_VOLTAGE_CTL2 */
    case 35: /* CM_VOLTAGE_CTL3 */
        /* ??? Voltage control unimplemented.  */
        break;
    default:
        cpu_abort (cpu_single_env,
            "integratorcm_write: Unimplemented offset 0x%x\n", offset);
        break;
    }
}

/* Integrator/CM control registers.  */

static CPUReadMemoryFunc *integratorcm_readfn[] = {
   integratorcm_read,
   integratorcm_read,
   integratorcm_read
};

static CPUWriteMemoryFunc *integratorcm_writefn[] = {
   integratorcm_write,
   integratorcm_write,
   integratorcm_write
};

static void integratorcm_init(int memsz, uint32_t flash_offset)
{
    int iomemtype;
    integratorcm_state *s;

    s = (integratorcm_state *)qemu_mallocz(sizeof(integratorcm_state));
    s->cm_osc = 0x01000048;
    /* ??? What should the high bits of this value be?  */
    s->cm_auxosc = 0x0007feff;
    s->cm_sdram = 0x00011122;
    if (memsz >= 256) {
        integrator_spd[31] = 64;
        s->cm_sdram |= 0x10;
    } else if (memsz >= 128) {
        integrator_spd[31] = 32;
        s->cm_sdram |= 0x0c;
    } else if (memsz >= 64) {
        integrator_spd[31] = 16;
        s->cm_sdram |= 0x08;
    } else if (memsz >= 32) {
        integrator_spd[31] = 4;
        s->cm_sdram |= 0x04;
    } else {
        integrator_spd[31] = 2;
    }
    memcpy(integrator_spd + 73, "QEMU-MEMORY", 11);
    s->cm_init = 0x00000112;
    s->flash_offset = flash_offset;

    iomemtype = cpu_register_io_memory(0, integratorcm_readfn,
                                       integratorcm_writefn, s);
    cpu_register_physical_memory(0x10000000, 0x007fffff, iomemtype);
    integratorcm_do_remap(s, 1);
    /* ??? Save/restore.  */
}

/* Integrator/CP hardware emulation.  */
/* Primary interrupt controller.  */

typedef struct icp_pic_state
{
  uint32_t base;
  uint32_t level;
  uint32_t irq_enabled;
  uint32_t fiq_enabled;
  void *parent;
  /* -1 if parent is a cpu, otherwise IRQ number on parent PIC.  */
  int parent_irq;
} icp_pic_state;

static void icp_pic_update(icp_pic_state *s)
{
    CPUState *env;
    if (s->parent_irq != -1) {
        uint32_t flags;

        flags = (s->level & s->irq_enabled);
        pic_set_irq_new(s->parent, s->parent_irq,
                        flags != 0);
        return;
    }
    /* Raise CPU interrupt.  */
    env = (CPUState *)s->parent;
    if (s->level & s->fiq_enabled) {
        cpu_interrupt (env, CPU_INTERRUPT_FIQ);
    } else {
        cpu_reset_interrupt (env, CPU_INTERRUPT_FIQ);
    }
    if (s->level & s->irq_enabled) {
      cpu_interrupt (env, CPU_INTERRUPT_HARD);
    } else {
      cpu_reset_interrupt (env, CPU_INTERRUPT_HARD);
    }
}

void pic_set_irq_new(void *opaque, int irq, int level)
{
    icp_pic_state *s = (icp_pic_state *)opaque;
    if (level)
        s->level |= 1 << irq;
    else
        s->level &= ~(1 << irq);
    icp_pic_update(s);
}

static uint32_t icp_pic_read(void *opaque, target_phys_addr_t offset)
{
    icp_pic_state *s = (icp_pic_state *)opaque;

    offset -= s->base;
    switch (offset >> 2) {
    case 0: /* IRQ_STATUS */
        return s->level & s->irq_enabled;
    case 1: /* IRQ_RAWSTAT */
        return s->level;
    case 2: /* IRQ_ENABLESET */
        return s->irq_enabled;
    case 4: /* INT_SOFTSET */
        return s->level & 1;
    case 8: /* FRQ_STATUS */
        return s->level & s->fiq_enabled;
    case 9: /* FRQ_RAWSTAT */
        return s->level;
    case 10: /* FRQ_ENABLESET */
        return s->fiq_enabled;
    case 3: /* IRQ_ENABLECLR */
    case 5: /* INT_SOFTCLR */
    case 11: /* FRQ_ENABLECLR */
    default:
        printf ("icp_pic_read: Bad register offset 0x%x\n", offset);
        return 0;
    }
}

static void icp_pic_write(void *opaque, target_phys_addr_t offset,
                          uint32_t value)
{
    icp_pic_state *s = (icp_pic_state *)opaque;
    offset -= s->base;

    switch (offset >> 2) {
    case 2: /* IRQ_ENABLESET */
        s->irq_enabled |= value;
        break;
    case 3: /* IRQ_ENABLECLR */
        s->irq_enabled &= ~value;
        break;
    case 4: /* INT_SOFTSET */
        if (value & 1)
            pic_set_irq_new(s, 0, 1);
        break;
    case 5: /* INT_SOFTCLR */
        if (value & 1)
            pic_set_irq_new(s, 0, 0);
        break;
    case 10: /* FRQ_ENABLESET */
        s->fiq_enabled |= value;
        break;
    case 11: /* FRQ_ENABLECLR */
        s->fiq_enabled &= ~value;
        break;
    case 0: /* IRQ_STATUS */
    case 1: /* IRQ_RAWSTAT */
    case 8: /* FRQ_STATUS */
    case 9: /* FRQ_RAWSTAT */
    default:
        printf ("icp_pic_write: Bad register offset 0x%x\n", offset);
        return;
    }
    icp_pic_update(s);
}

static CPUReadMemoryFunc *icp_pic_readfn[] = {
   icp_pic_read,
   icp_pic_read,
   icp_pic_read
};

static CPUWriteMemoryFunc *icp_pic_writefn[] = {
   icp_pic_write,
   icp_pic_write,
   icp_pic_write
};

static icp_pic_state *icp_pic_init(uint32_t base, void *parent,
                                   int parent_irq)
{
    icp_pic_state *s;
    int iomemtype;

    s = (icp_pic_state *)qemu_mallocz(sizeof(icp_pic_state));
    if (!s)
        return NULL;

    s->base = base;
    s->parent = parent;
    s->parent_irq = parent_irq;
    iomemtype = cpu_register_io_memory(0, icp_pic_readfn,
                                       icp_pic_writefn, s);
    cpu_register_physical_memory(base, 0x007fffff, iomemtype);
    /* ??? Save/restore.  */
    return s;
}

/* Timers.  */

/* System bus clock speed (40MHz) for timer 0.  Not sure about this value.  */
#define ICP_BUS_FREQ 40000000

typedef struct {
    int64_t next_time;
    int64_t expires[3];
    int64_t loaded[3];
    QEMUTimer *timer;
    icp_pic_state *pic;
    uint32_t base;
    uint32_t control[3];
    uint32_t count[3];
    uint32_t limit[3];
    int freq[3];
    int int_level[3];
} icp_pit_state;

/* Calculate the new expiry time of the given timer.  */

static void icp_pit_reload(icp_pit_state *s, int n)
{
    int64_t delay;

    s->loaded[n] = s->expires[n];
    delay = muldiv64(s->count[n], ticks_per_sec, s->freq[n]);
    if (delay == 0)
        delay = 1;
    s->expires[n] += delay;
}

/* Check all active timers, and schedule the next timer interrupt.  */

static void icp_pit_update(icp_pit_state *s, int64_t now)
{
    int n;
    int64_t next;

    next = now;
    for (n = 0; n < 3; n++) {
        /* Ignore disabled timers.  */
        if ((s->control[n] & 0x80) == 0)
            continue;
        /* Ignore expired one-shot timers.  */
        if (s->count[n] == 0 && s->control[n] & 1)
            continue;
        if (s->expires[n] - now <= 0) {
            /* Timer has expired.  */
            s->int_level[n] = 1;
            if (s->control[n] & 1) {
                /* One-shot.  */
                s->count[n] = 0;
            } else {
                if ((s->control[n] & 0x40) == 0) {
                    /* Free running.  */
                    if (s->control[n] & 2)
                        s->count[n] = 0xffffffff;
                    else
                        s->count[n] = 0xffff;
                } else {
                      /* Periodic.  */
                      s->count[n] = s->limit[n];
                }
            }
        }
        while (s->expires[n] - now <= 0) {
            icp_pit_reload(s, n);
        }
    }
    /* Update interrupts.  */
    for (n = 0; n < 3; n++) {
        if (s->int_level[n] && (s->control[n] & 0x20)) {
            pic_set_irq_new(s->pic, 5 + n, 1);
        } else {
            pic_set_irq_new(s->pic, 5 + n, 0);
        }
        if (next - s->expires[n] < 0)
            next = s->expires[n];
    }
    /* Schedule the next timer interrupt.  */
    if (next == now) {
        qemu_del_timer(s->timer);
        s->next_time = 0;
    } else if (next != s->next_time) {
        qemu_mod_timer(s->timer, next);
        s->next_time = next;
    }
}

/* Return the current value of the timer.  */
static uint32_t icp_pit_getcount(icp_pit_state *s, int n, int64_t now)
{
    int64_t elapsed;
    int64_t period;

    if (s->count[n] == 0)
        return 0;
    if ((s->control[n] & 0x80) == 0)
        return s->count[n];
    elapsed = now - s->loaded[n];
    period = s->expires[n] - s->loaded[n];
    /* If the timer should have expired then return 0.  This can happen
       when the host timer signal doesnt occur immediately.  It's better to
       have a timer appear to sit at zero for a while than have it wrap
       around before the guest interrupt is raised.  */
    /* ??? Could we trigger the interrupt here?  */
    if (elapsed > period)
        return 0;
    /* We need to calculate count * elapsed / period without overfowing.
       Scale both elapsed and period so they fit in a 32-bit int.  */
    while (period != (int32_t)period) {
        period >>= 1;
        elapsed >>= 1;
    }
    return ((uint64_t)s->count[n] * (uint64_t)(int32_t)elapsed)
            / (int32_t)period;
}

static uint32_t icp_pit_read(void *opaque, target_phys_addr_t offset)
{
    int n;
    icp_pit_state *s = (icp_pit_state *)opaque;

    offset -= s->base;
    n = offset >> 8;
    if (n > 2)
        cpu_abort (cpu_single_env, "icp_pit_read: Bad timer %x\n", offset);
    switch ((offset & 0xff) >> 2) {
    case 0: /* TimerLoad */
    case 6: /* TimerBGLoad */
        return s->limit[n];
    case 1: /* TimerValue */
        return icp_pit_getcount(s, n, qemu_get_clock(vm_clock));
    case 2: /* TimerControl */
        return s->control[n];
    case 4: /* TimerRIS */
        return s->int_level[n];
    case 5: /* TimerMIS */
        if ((s->control[n] & 0x20) == 0)
            return 0;
        return s->int_level[n];
    default:
        cpu_abort (cpu_single_env, "icp_pit_read: Bad offset %x\n", offset);
        return 0;
    }
}

static void icp_pit_write(void *opaque, target_phys_addr_t offset,
                          uint32_t value)
{
    icp_pit_state *s = (icp_pit_state *)opaque;
    int n;
    int64_t now;

    now = qemu_get_clock(vm_clock);
    offset -= s->base;
    n = offset >> 8;
    if (n > 2)
        cpu_abort (cpu_single_env, "icp_pit_write: Bad offset %x\n", offset);

    switch ((offset & 0xff) >> 2) {
    case 0: /* TimerLoad */
        s->limit[n] = value;
        s->count[n] = value;
        s->expires[n] = now;
        icp_pit_reload(s, n);
        break;
    case 1: /* TimerValue */
        /* ??? Linux seems to want to write to this readonly register.
           Ignore it.  */
        break;
    case 2: /* TimerControl */
        if (s->control[n] & 0x80) {
            /* Pause the timer if it is running.  This may cause some
               inaccuracy dure to rounding, but avoids a whole lot of other
               messyness.  */
            s->count[n] = icp_pit_getcount(s, n, now);
        }
        s->control[n] = value;
        if (n == 0)
            s->freq[n] = ICP_BUS_FREQ;
        else
            s->freq[n] = 1000000;
        /* ??? Need to recalculate expiry time after changing divisor.  */
        switch ((value >> 2) & 3) {
        case 1: s->freq[n] >>= 4; break;
        case 2: s->freq[n] >>= 8; break;
        }
        if (s->control[n] & 0x80) {
            /* Restart the timer if still enabled.  */
            s->expires[n] = now;
            icp_pit_reload(s, n);
        }
        break;
    case 3: /* TimerIntClr */
        s->int_level[n] = 0;
        break;
    case 6: /* TimerBGLoad */
        s->limit[n] = value;
        break;
    default:
        cpu_abort (cpu_single_env, "icp_pit_write: Bad offset %x\n", offset);
    }
    icp_pit_update(s, now);
}

static void icp_pit_tick(void *opaque)
{
    int64_t now;

    now = qemu_get_clock(vm_clock);
    icp_pit_update((icp_pit_state *)opaque, now);
}

static CPUReadMemoryFunc *icp_pit_readfn[] = {
   icp_pit_read,
   icp_pit_read,
   icp_pit_read
};

static CPUWriteMemoryFunc *icp_pit_writefn[] = {
   icp_pit_write,
   icp_pit_write,
   icp_pit_write
};

static void icp_pit_init(uint32_t base, icp_pic_state *pic)
{
    int iomemtype;
    icp_pit_state *s;
    int n;

    s = (icp_pit_state *)qemu_mallocz(sizeof(icp_pit_state));
    s->base = base;
    s->pic = pic;
    s->freq[0] = ICP_BUS_FREQ;
    s->freq[1] = 1000000;
    s->freq[2] = 1000000;
    for (n = 0; n < 3; n++) {
        s->control[n] = 0x20;
        s->count[n] = 0xffffffff;
    }

    iomemtype = cpu_register_io_memory(0, icp_pit_readfn,
                                       icp_pit_writefn, s);
    cpu_register_physical_memory(base, 0x007fffff, iomemtype);
    s->timer = qemu_new_timer(vm_clock, icp_pit_tick, s);
    /* ??? Save/restore.  */
}

/* ARM PrimeCell PL011 UART */

typedef struct {
    uint32_t base;
    uint32_t readbuff;
    uint32_t flags;
    uint32_t lcr;
    uint32_t cr;
    uint32_t dmacr;
    uint32_t int_enabled;
    uint32_t int_level;
    uint32_t read_fifo[16];
    uint32_t ilpr;
    uint32_t ibrd;
    uint32_t fbrd;
    uint32_t ifl;
    int read_pos;
    int read_count;
    int read_trigger;
    CharDriverState *chr;
    icp_pic_state *pic;
    int irq;
} pl011_state;

#define PL011_INT_TX 0x20
#define PL011_INT_RX 0x10

#define PL011_FLAG_TXFE 0x80
#define PL011_FLAG_RXFF 0x40
#define PL011_FLAG_TXFF 0x20
#define PL011_FLAG_RXFE 0x10

static const unsigned char pl011_id[] =
{ 0x11, 0x10, 0x14, 0x00, 0x0d, 0xf0, 0x05, 0xb1 };

static void pl011_update(pl011_state *s)
{
    uint32_t flags;
    
    flags = s->int_level & s->int_enabled;
    pic_set_irq_new(s->pic, s->irq, flags != 0);
}

static uint32_t pl011_read(void *opaque, target_phys_addr_t offset)
{
    pl011_state *s = (pl011_state *)opaque;
    uint32_t c;

    offset -= s->base;
    if (offset >= 0xfe0 && offset < 0x1000) {
        return pl011_id[(offset - 0xfe0) >> 2];
    }
    switch (offset >> 2) {
    case 0: /* UARTDR */
        s->flags &= ~PL011_FLAG_RXFF;
        c = s->read_fifo[s->read_pos];
        if (s->read_count > 0) {
            s->read_count--;
            if (++s->read_pos == 16)
                s->read_pos = 0;
        }
        if (s->read_count == 0) {
            s->flags |= PL011_FLAG_RXFE;
        }
        if (s->read_count == s->read_trigger - 1)
            s->int_level &= ~ PL011_INT_RX;
        pl011_update(s);
        return c;
    case 1: /* UARTCR */
        return 0;
    case 6: /* UARTFR */
        return s->flags;
    case 8: /* UARTILPR */
        return s->ilpr;
    case 9: /* UARTIBRD */
        return s->ibrd;
    case 10: /* UARTFBRD */
        return s->fbrd;
    case 11: /* UARTLCR_H */
        return s->lcr;
    case 12: /* UARTCR */
        return s->cr;
    case 13: /* UARTIFLS */
        return s->ifl;
    case 14: /* UARTIMSC */
        return s->int_enabled;
    case 15: /* UARTRIS */
        return s->int_level;
    case 16: /* UARTMIS */
        return s->int_level & s->int_enabled;
    case 18: /* UARTDMACR */
        return s->dmacr;
    default:
        cpu_abort (cpu_single_env, "pl011_read: Bad offset %x\n", offset);
        return 0;
    }
}

static void pl011_set_read_trigger(pl011_state *s)
{
#if 0
    /* The docs say the RX interrupt is triggered when the FIFO exceeds
       the threshold.  However linux only reads the FIFO in response to an
       interrupt.  Triggering the interrupt when the FIFO is non-empty seems
       to make things work.  */
    if (s->lcr & 0x10)
        s->read_trigger = (s->ifl >> 1) & 0x1c;
    else
#endif
        s->read_trigger = 1;
}

static void pl011_write(void *opaque, target_phys_addr_t offset,
                          uint32_t value)
{
    pl011_state *s = (pl011_state *)opaque;
    unsigned char ch;

    offset -= s->base;
    switch (offset >> 2) {
    case 0: /* UARTDR */
        /* ??? Check if transmitter is enabled.  */
        ch = value;
        if (s->chr)
            qemu_chr_write(s->chr, &ch, 1);
        s->int_level |= PL011_INT_TX;
        pl011_update(s);
        break;
    case 1: /* UARTCR */
        s->cr = value;
        break;
    case 8: /* UARTUARTILPR */
        s->ilpr = value;
        break;
    case 9: /* UARTIBRD */
        s->ibrd = value;
        break;
    case 10: /* UARTFBRD */
        s->fbrd = value;
        break;
    case 11: /* UARTLCR_H */
        s->lcr = value;
        pl011_set_read_trigger(s);
        break;
    case 12: /* UARTCR */
        /* ??? Need to implement the enable and loopback bits.  */
        s->cr = value;
        break;
    case 13: /* UARTIFS */
        s->ifl = value;
        pl011_set_read_trigger(s);
        break;
    case 14: /* UARTIMSC */
        s->int_enabled = value;
        pl011_update(s);
        break;
    case 17: /* UARTICR */
        s->int_level &= ~value;
        pl011_update(s);
        break;
    case 18: /* UARTDMACR */
        s->dmacr = value;
        if (value & 3)
            cpu_abort(cpu_single_env, "PL011: DMA not implemented\n");
        break;
    default:
        cpu_abort (cpu_single_env, "pl011_write: Bad offset %x\n", offset);
    }
}

static int pl011_can_recieve(void *opaque)
{
    pl011_state *s = (pl011_state *)opaque;

    if (s->lcr & 0x10)
        return s->read_count < 16;
    else
        return s->read_count < 1;
}

static void pl011_recieve(void *opaque, const uint8_t *buf, int size)
{
    pl011_state *s = (pl011_state *)opaque;
    int slot;

    slot = s->read_pos + s->read_count;
    if (slot >= 16)
        slot -= 16;
    s->read_fifo[slot] = *buf;
    s->read_count++;
    s->flags &= ~PL011_FLAG_RXFE;
    if (s->cr & 0x10 || s->read_count == 16) {
        s->flags |= PL011_FLAG_RXFF;
    }
    if (s->read_count == s->read_trigger) {
        s->int_level |= PL011_INT_RX;
        pl011_update(s);
    }
}

static void pl011_event(void *opaque, int event)
{
    /* ??? Should probably implement break.  */
}

static CPUReadMemoryFunc *pl011_readfn[] = {
   pl011_read,
   pl011_read,
   pl011_read
};

static CPUWriteMemoryFunc *pl011_writefn[] = {
   pl011_write,
   pl011_write,
   pl011_write
};

static void pl011_init(uint32_t base, icp_pic_state *pic, int irq,
                       CharDriverState *chr)
{
    int iomemtype;
    pl011_state *s;

    s = (pl011_state *)qemu_mallocz(sizeof(pl011_state));
    iomemtype = cpu_register_io_memory(0, pl011_readfn,
                                       pl011_writefn, s);
    cpu_register_physical_memory(base, 0x007fffff, iomemtype);
    s->base = base;
    s->pic = pic;
    s->irq = irq;
    s->chr = chr;
    s->read_trigger = 1;
    s->ifl = 0x12;
    s->cr = 0x300;
    s->flags = 0x90;
    if (chr){ 
        qemu_chr_add_read_handler(chr, pl011_can_recieve, pl011_recieve, s);
        qemu_chr_add_event_handler(chr, pl011_event);
    }
    /* ??? Save/restore.  */
}

/* CP control registers.  */
typedef struct {
    uint32_t base;
} icp_control_state;

static uint32_t icp_control_read(void *opaque, target_phys_addr_t offset)
{
    icp_control_state *s = (icp_control_state *)opaque;
    offset -= s->base;
    switch (offset >> 2) {
    case 0: /* CP_IDFIELD */
        return 0x41034003;
    case 1: /* CP_FLASHPROG */
        return 0;
    case 2: /* CP_INTREG */
        return 0;
    case 3: /* CP_DECODE */
        return 0x11;
    default:
        cpu_abort (cpu_single_env, "icp_control_read: Bad offset %x\n", offset);
        return 0;
    }
}

static void icp_control_write(void *opaque, target_phys_addr_t offset,
                          uint32_t value)
{
    icp_control_state *s = (icp_control_state *)opaque;
    offset -= s->base;
    switch (offset >> 2) {
    case 1: /* CP_FLASHPROG */
    case 2: /* CP_INTREG */
    case 3: /* CP_DECODE */
        /* Nothing interesting implemented yet.  */
        break;
    default:
        cpu_abort (cpu_single_env, "icp_control_write: Bad offset %x\n", offset);
    }
}
static CPUReadMemoryFunc *icp_control_readfn[] = {
   icp_control_read,
   icp_control_read,
   icp_control_read
};

static CPUWriteMemoryFunc *icp_control_writefn[] = {
   icp_control_write,
   icp_control_write,
   icp_control_write
};

static void icp_control_init(uint32_t base)
{
    int iomemtype;
    icp_control_state *s;

    s = (icp_control_state *)qemu_mallocz(sizeof(icp_control_state));
    iomemtype = cpu_register_io_memory(0, icp_control_readfn,
                                       icp_control_writefn, s);
    cpu_register_physical_memory(base, 0x007fffff, iomemtype);
    s->base = base;
    /* ??? Save/restore.  */
}


/* Keyboard/Mouse Interface.  */

typedef struct {
    void *dev;
    uint32_t base;
    uint32_t cr;
    uint32_t clk;
    uint32_t last;
    icp_pic_state *pic;
    int pending;
    int irq;
    int is_mouse;
} icp_kmi_state;

static void icp_kmi_update(void *opaque, int level)
{
    icp_kmi_state *s = (icp_kmi_state *)opaque;
    int raise;

    s->pending = level;
    raise = (s->pending && (s->cr & 0x10) != 0)
            || (s->cr & 0x08) != 0;
    pic_set_irq_new(s->pic, s->irq, raise);
}

static uint32_t icp_kmi_read(void *opaque, target_phys_addr_t offset)
{
    icp_kmi_state *s = (icp_kmi_state *)opaque;
    offset -= s->base;
    if (offset >= 0xfe0 && offset < 0x1000)
        return 0;

    switch (offset >> 2) {
    case 0: /* KMICR */
        return s->cr;
    case 1: /* KMISTAT */
        /* KMIC and KMID bits not implemented.  */
        if (s->pending) {
            return 0x10;
        } else {
            return 0;
        }
    case 2: /* KMIDATA */
        if (s->pending)
            s->last = ps2_read_data(s->dev);
        return s->last;
    case 3: /* KMICLKDIV */
        return s->clk;
    case 4: /* KMIIR */
        return s->pending | 2;
    default:
        cpu_abort (cpu_single_env, "icp_kmi_read: Bad offset %x\n", offset);
        return 0;
    }
}

static void icp_kmi_write(void *opaque, target_phys_addr_t offset,
                          uint32_t value)
{
    icp_kmi_state *s = (icp_kmi_state *)opaque;
    offset -= s->base;
    switch (offset >> 2) {
    case 0: /* KMICR */
        s->cr = value;
        icp_kmi_update(s, s->pending);
        /* ??? Need to implement the enable/disable bit.  */
        break;
    case 2: /* KMIDATA */
        /* ??? This should toggle the TX interrupt line.  */
        /* ??? This means kbd/mouse can block each other.  */
        if (s->is_mouse) {
            ps2_write_mouse(s->dev, value);
        } else {
            ps2_write_keyboard(s->dev, value);
        }
        break;
    case 3: /* KMICLKDIV */
        s->clk = value;
        return;
    default:
        cpu_abort (cpu_single_env, "icp_kmi_write: Bad offset %x\n", offset);
    }
}
static CPUReadMemoryFunc *icp_kmi_readfn[] = {
   icp_kmi_read,
   icp_kmi_read,
   icp_kmi_read
};

static CPUWriteMemoryFunc *icp_kmi_writefn[] = {
   icp_kmi_write,
   icp_kmi_write,
   icp_kmi_write
};

static void icp_kmi_init(uint32_t base, icp_pic_state * pic, int irq,
                         int is_mouse)
{
    int iomemtype;
    icp_kmi_state *s;

    s = (icp_kmi_state *)qemu_mallocz(sizeof(icp_kmi_state));
    iomemtype = cpu_register_io_memory(0, icp_kmi_readfn,
                                       icp_kmi_writefn, s);
    cpu_register_physical_memory(base, 0x007fffff, iomemtype);
    s->base = base;
    s->pic = pic;
    s->irq = irq;
    s->is_mouse = is_mouse;
    if (is_mouse)
        s->dev = ps2_mouse_init(icp_kmi_update, s);
    else
        s->dev = ps2_kbd_init(icp_kmi_update, s);
    /* ??? Save/restore.  */
}

/* The worlds second smallest bootloader.  Set r0-r2, then jump to kernel.  */
static uint32_t bootloader[] = {
  0xe3a00000, /* mov     r0, #0 */
  0xe3a01013, /* mov     r1, #0x13 */
  0xe3811c01, /* orr     r1, r1, #0x100 */
  0xe59f2000, /* ldr     r2, [pc, #0] */
  0xe59ff000, /* ldr     pc, [pc, #0] */
  0, /* Address of kernel args.  Set by integratorcp_init.  */
  0  /* Kernel entry point.  Set by integratorcp_init.  */
};

static void set_kernel_args(uint32_t ram_size, int initrd_size,
                            const char *kernel_cmdline)
{
    uint32_t *p;

    p = (uint32_t *)(phys_ram_base + KERNEL_ARGS_ADDR);
    /* ATAG_CORE */
    stl_raw(p++, 5);
    stl_raw(p++, 0x54410001);
    stl_raw(p++, 1);
    stl_raw(p++, 0x1000);
    stl_raw(p++, 0);
    /* ATAG_MEM */
    stl_raw(p++, 4);
    stl_raw(p++, 0x54410002);
    stl_raw(p++, ram_size);
    stl_raw(p++, 0);
    if (initrd_size) {
        /* ATAG_INITRD2 */
        stl_raw(p++, 4);
        stl_raw(p++, 0x54420005);
        stl_raw(p++, INITRD_LOAD_ADDR);
        stl_raw(p++, initrd_size);
    }
    if (kernel_cmdline && *kernel_cmdline) {
        /* ATAG_CMDLINE */
        int cmdline_size;

        cmdline_size = strlen(kernel_cmdline);
        memcpy (p + 2, kernel_cmdline, cmdline_size + 1);
        cmdline_size = (cmdline_size >> 2) + 1;
        stl_raw(p++, cmdline_size + 2);
        stl_raw(p++, 0x54410009);
        p += cmdline_size;
    }
    /* ATAG_END */
    stl_raw(p++, 0);
    stl_raw(p++, 0);
}

/* Board init.  */

static void integratorcp_init(int ram_size, int vga_ram_size, int boot_device,
                     DisplayState *ds, const char **fd_filename, int snapshot,
                     const char *kernel_filename, const char *kernel_cmdline,
                     const char *initrd_filename)
{
    CPUState *env;
    uint32_t bios_offset;
    icp_pic_state *pic;
    int kernel_size;
    int initrd_size;
    int n;

    env = cpu_init();
    bios_offset = ram_size + vga_ram_size;
    /* ??? On a real system the first 1Mb is mapped as SSRAM or boot flash.  */
    /* ??? RAM shoud repeat to fill physical memory space.  */
    /* SDRAM at address zero*/
    cpu_register_physical_memory(0, ram_size, IO_MEM_RAM);
    /* And again at address 0x80000000 */
    cpu_register_physical_memory(0x80000000, ram_size, IO_MEM_RAM);

    integratorcm_init(ram_size >> 20, bios_offset);
    pic = icp_pic_init(0x14000000, env, -1);
    icp_pic_init(0xca000000, pic, 26);
    icp_pit_init(0x13000000, pic);
    pl011_init(0x16000000, pic, 1, serial_hds[0]);
    pl011_init(0x17000000, pic, 2, serial_hds[1]);
    icp_control_init(0xcb000000);
    icp_kmi_init(0x18000000, pic, 3, 0);
    icp_kmi_init(0x19000000, pic, 4, 1);
    if (nd_table[0].vlan) {
        if (nd_table[0].model == NULL
            || strcmp(nd_table[0].model, "smc91c111") == 0) {
            smc91c111_init(&nd_table[0], 0xc8000000, pic, 27);
        } else {
            fprintf(stderr, "qemu: Unsupported NIC: %s\n", nd_table[0].model);
            exit (1);
        }
    }
    lcd = pl110_init(ds, 0xc0000000, pic, 22);

    /* Load the kernel.  */
    if (!kernel_filename) {
        fprintf(stderr, "Kernel image must be specified\n");
        exit(1);
    }
    kernel_size = load_image(kernel_filename,
                             phys_ram_base + KERNEL_LOAD_ADDR);
    if (kernel_size < 0) {
        fprintf(stderr, "qemu: could not load kernel '%s'\n", kernel_filename);
        exit(1);
    }
    if (initrd_filename) {
        initrd_size = load_image(initrd_filename,
                                 phys_ram_base + INITRD_LOAD_ADDR);
        if (initrd_size < 0) {
            fprintf(stderr, "qemu: could not load initrd '%s'\n",
                    initrd_filename);
            exit(1);
        }
    } else {
        initrd_size = 0;
    }
    bootloader[5] = KERNEL_ARGS_ADDR;
    bootloader[6] = KERNEL_LOAD_ADDR;
    for (n = 0; n < sizeof(bootloader) / 4; n++)
        stl_raw(phys_ram_base + (n * 4), bootloader[n]);
    set_kernel_args(ram_size, initrd_size, kernel_cmdline);
}

QEMUMachine integratorcp_machine = {
    "integratorcp",
    "ARM Integrator/CP",
    integratorcp_init,
};
