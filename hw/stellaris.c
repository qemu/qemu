/*
 * Luminary Micro Stellaris peripherals
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the GPL.
 */

#include "sysbus.h"
#include "ssi.h"
#include "arm-misc.h"
#include "devices.h"
#include "qemu-timer.h"
#include "i2c.h"
#include "net.h"
#include "sysemu.h"
#include "boards.h"

#define GPIO_A 0
#define GPIO_B 1
#define GPIO_C 2
#define GPIO_D 3
#define GPIO_E 4
#define GPIO_F 5
#define GPIO_G 6

#define BP_OLED_I2C  0x01
#define BP_OLED_SSI  0x02
#define BP_GAMEPAD   0x04

typedef const struct {
    const char *name;
    uint32_t did0;
    uint32_t did1;
    uint32_t dc0;
    uint32_t dc1;
    uint32_t dc2;
    uint32_t dc3;
    uint32_t dc4;
    uint32_t peripherals;
} stellaris_board_info;

/* General purpose timer module.  */

typedef struct gptm_state {
    SysBusDevice busdev;
    uint32_t config;
    uint32_t mode[2];
    uint32_t control;
    uint32_t state;
    uint32_t mask;
    uint32_t load[2];
    uint32_t match[2];
    uint32_t prescale[2];
    uint32_t match_prescale[2];
    uint32_t rtc;
    int64_t tick[2];
    struct gptm_state *opaque[2];
    QEMUTimer *timer[2];
    /* The timers have an alternate output used to trigger the ADC.  */
    qemu_irq trigger;
    qemu_irq irq;
} gptm_state;

static void gptm_update_irq(gptm_state *s)
{
    int level;
    level = (s->state & s->mask) != 0;
    qemu_set_irq(s->irq, level);
}

static void gptm_stop(gptm_state *s, int n)
{
    qemu_del_timer(s->timer[n]);
}

static void gptm_reload(gptm_state *s, int n, int reset)
{
    int64_t tick;
    if (reset)
        tick = qemu_get_clock(vm_clock);
    else
        tick = s->tick[n];

    if (s->config == 0) {
        /* 32-bit CountDown.  */
        uint32_t count;
        count = s->load[0] | (s->load[1] << 16);
        tick += (int64_t)count * system_clock_scale;
    } else if (s->config == 1) {
        /* 32-bit RTC.  1Hz tick.  */
        tick += get_ticks_per_sec();
    } else if (s->mode[n] == 0xa) {
        /* PWM mode.  Not implemented.  */
    } else {
        hw_error("TODO: 16-bit timer mode 0x%x\n", s->mode[n]);
    }
    s->tick[n] = tick;
    qemu_mod_timer(s->timer[n], tick);
}

static void gptm_tick(void *opaque)
{
    gptm_state **p = (gptm_state **)opaque;
    gptm_state *s;
    int n;

    s = *p;
    n = p - s->opaque;
    if (s->config == 0) {
        s->state |= 1;
        if ((s->control & 0x20)) {
            /* Output trigger.  */
	    qemu_irq_pulse(s->trigger);
        }
        if (s->mode[0] & 1) {
            /* One-shot.  */
            s->control &= ~1;
        } else {
            /* Periodic.  */
            gptm_reload(s, 0, 0);
        }
    } else if (s->config == 1) {
        /* RTC.  */
        uint32_t match;
        s->rtc++;
        match = s->match[0] | (s->match[1] << 16);
        if (s->rtc > match)
            s->rtc = 0;
        if (s->rtc == 0) {
            s->state |= 8;
        }
        gptm_reload(s, 0, 0);
    } else if (s->mode[n] == 0xa) {
        /* PWM mode.  Not implemented.  */
    } else {
        hw_error("TODO: 16-bit timer mode 0x%x\n", s->mode[n]);
    }
    gptm_update_irq(s);
}

static uint32_t gptm_read(void *opaque, target_phys_addr_t offset)
{
    gptm_state *s = (gptm_state *)opaque;

    switch (offset) {
    case 0x00: /* CFG */
        return s->config;
    case 0x04: /* TAMR */
        return s->mode[0];
    case 0x08: /* TBMR */
        return s->mode[1];
    case 0x0c: /* CTL */
        return s->control;
    case 0x18: /* IMR */
        return s->mask;
    case 0x1c: /* RIS */
        return s->state;
    case 0x20: /* MIS */
        return s->state & s->mask;
    case 0x24: /* CR */
        return 0;
    case 0x28: /* TAILR */
        return s->load[0] | ((s->config < 4) ? (s->load[1] << 16) : 0);
    case 0x2c: /* TBILR */
        return s->load[1];
    case 0x30: /* TAMARCHR */
        return s->match[0] | ((s->config < 4) ? (s->match[1] << 16) : 0);
    case 0x34: /* TBMATCHR */
        return s->match[1];
    case 0x38: /* TAPR */
        return s->prescale[0];
    case 0x3c: /* TBPR */
        return s->prescale[1];
    case 0x40: /* TAPMR */
        return s->match_prescale[0];
    case 0x44: /* TBPMR */
        return s->match_prescale[1];
    case 0x48: /* TAR */
        if (s->control == 1)
            return s->rtc;
    case 0x4c: /* TBR */
        hw_error("TODO: Timer value read\n");
    default:
        hw_error("gptm_read: Bad offset 0x%x\n", (int)offset);
        return 0;
    }
}

static void gptm_write(void *opaque, target_phys_addr_t offset, uint32_t value)
{
    gptm_state *s = (gptm_state *)opaque;
    uint32_t oldval;

    /* The timers should be disabled before changing the configuration.
       We take advantage of this and defer everything until the timer
       is enabled.  */
    switch (offset) {
    case 0x00: /* CFG */
        s->config = value;
        break;
    case 0x04: /* TAMR */
        s->mode[0] = value;
        break;
    case 0x08: /* TBMR */
        s->mode[1] = value;
        break;
    case 0x0c: /* CTL */
        oldval = s->control;
        s->control = value;
        /* TODO: Implement pause.  */
        if ((oldval ^ value) & 1) {
            if (value & 1) {
                gptm_reload(s, 0, 1);
            } else {
                gptm_stop(s, 0);
            }
        }
        if (((oldval ^ value) & 0x100) && s->config >= 4) {
            if (value & 0x100) {
                gptm_reload(s, 1, 1);
            } else {
                gptm_stop(s, 1);
            }
        }
        break;
    case 0x18: /* IMR */
        s->mask = value & 0x77;
        gptm_update_irq(s);
        break;
    case 0x24: /* CR */
        s->state &= ~value;
        break;
    case 0x28: /* TAILR */
        s->load[0] = value & 0xffff;
        if (s->config < 4) {
            s->load[1] = value >> 16;
        }
        break;
    case 0x2c: /* TBILR */
        s->load[1] = value & 0xffff;
        break;
    case 0x30: /* TAMARCHR */
        s->match[0] = value & 0xffff;
        if (s->config < 4) {
            s->match[1] = value >> 16;
        }
        break;
    case 0x34: /* TBMATCHR */
        s->match[1] = value >> 16;
        break;
    case 0x38: /* TAPR */
        s->prescale[0] = value;
        break;
    case 0x3c: /* TBPR */
        s->prescale[1] = value;
        break;
    case 0x40: /* TAPMR */
        s->match_prescale[0] = value;
        break;
    case 0x44: /* TBPMR */
        s->match_prescale[0] = value;
        break;
    default:
        hw_error("gptm_write: Bad offset 0x%x\n", (int)offset);
    }
    gptm_update_irq(s);
}

static CPUReadMemoryFunc * const gptm_readfn[] = {
   gptm_read,
   gptm_read,
   gptm_read
};

static CPUWriteMemoryFunc * const gptm_writefn[] = {
   gptm_write,
   gptm_write,
   gptm_write
};

static void gptm_save(QEMUFile *f, void *opaque)
{
    gptm_state *s = (gptm_state *)opaque;

    qemu_put_be32(f, s->config);
    qemu_put_be32(f, s->mode[0]);
    qemu_put_be32(f, s->mode[1]);
    qemu_put_be32(f, s->control);
    qemu_put_be32(f, s->state);
    qemu_put_be32(f, s->mask);
    qemu_put_be32(f, s->mode[0]);
    qemu_put_be32(f, s->mode[0]);
    qemu_put_be32(f, s->load[0]);
    qemu_put_be32(f, s->load[1]);
    qemu_put_be32(f, s->match[0]);
    qemu_put_be32(f, s->match[1]);
    qemu_put_be32(f, s->prescale[0]);
    qemu_put_be32(f, s->prescale[1]);
    qemu_put_be32(f, s->match_prescale[0]);
    qemu_put_be32(f, s->match_prescale[1]);
    qemu_put_be32(f, s->rtc);
    qemu_put_be64(f, s->tick[0]);
    qemu_put_be64(f, s->tick[1]);
    qemu_put_timer(f, s->timer[0]);
    qemu_put_timer(f, s->timer[1]);
}

static int gptm_load(QEMUFile *f, void *opaque, int version_id)
{
    gptm_state *s = (gptm_state *)opaque;

    if (version_id != 1)
        return -EINVAL;

    s->config = qemu_get_be32(f);
    s->mode[0] = qemu_get_be32(f);
    s->mode[1] = qemu_get_be32(f);
    s->control = qemu_get_be32(f);
    s->state = qemu_get_be32(f);
    s->mask = qemu_get_be32(f);
    s->mode[0] = qemu_get_be32(f);
    s->mode[0] = qemu_get_be32(f);
    s->load[0] = qemu_get_be32(f);
    s->load[1] = qemu_get_be32(f);
    s->match[0] = qemu_get_be32(f);
    s->match[1] = qemu_get_be32(f);
    s->prescale[0] = qemu_get_be32(f);
    s->prescale[1] = qemu_get_be32(f);
    s->match_prescale[0] = qemu_get_be32(f);
    s->match_prescale[1] = qemu_get_be32(f);
    s->rtc = qemu_get_be32(f);
    s->tick[0] = qemu_get_be64(f);
    s->tick[1] = qemu_get_be64(f);
    qemu_get_timer(f, s->timer[0]);
    qemu_get_timer(f, s->timer[1]);

    return 0;
}

static int stellaris_gptm_init(SysBusDevice *dev)
{
    int iomemtype;
    gptm_state *s = FROM_SYSBUS(gptm_state, dev);

    sysbus_init_irq(dev, &s->irq);
    qdev_init_gpio_out(&dev->qdev, &s->trigger, 1);

    iomemtype = cpu_register_io_memory(gptm_readfn,
                                       gptm_writefn, s,
                                       DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, 0x1000, iomemtype);

    s->opaque[0] = s->opaque[1] = s;
    s->timer[0] = qemu_new_timer(vm_clock, gptm_tick, &s->opaque[0]);
    s->timer[1] = qemu_new_timer(vm_clock, gptm_tick, &s->opaque[1]);
    register_savevm(&dev->qdev, "stellaris_gptm", -1, 1,
                    gptm_save, gptm_load, s);
    return 0;
}


/* System controller.  */

typedef struct {
    uint32_t pborctl;
    uint32_t ldopctl;
    uint32_t int_status;
    uint32_t int_mask;
    uint32_t resc;
    uint32_t rcc;
    uint32_t rcgc[3];
    uint32_t scgc[3];
    uint32_t dcgc[3];
    uint32_t clkvclr;
    uint32_t ldoarst;
    uint32_t user0;
    uint32_t user1;
    qemu_irq irq;
    stellaris_board_info *board;
} ssys_state;

static void ssys_update(ssys_state *s)
{
  qemu_set_irq(s->irq, (s->int_status & s->int_mask) != 0);
}

static uint32_t pllcfg_sandstorm[16] = {
    0x31c0, /* 1 Mhz */
    0x1ae0, /* 1.8432 Mhz */
    0x18c0, /* 2 Mhz */
    0xd573, /* 2.4576 Mhz */
    0x37a6, /* 3.57954 Mhz */
    0x1ae2, /* 3.6864 Mhz */
    0x0c40, /* 4 Mhz */
    0x98bc, /* 4.906 Mhz */
    0x935b, /* 4.9152 Mhz */
    0x09c0, /* 5 Mhz */
    0x4dee, /* 5.12 Mhz */
    0x0c41, /* 6 Mhz */
    0x75db, /* 6.144 Mhz */
    0x1ae6, /* 7.3728 Mhz */
    0x0600, /* 8 Mhz */
    0x585b /* 8.192 Mhz */
};

static uint32_t pllcfg_fury[16] = {
    0x3200, /* 1 Mhz */
    0x1b20, /* 1.8432 Mhz */
    0x1900, /* 2 Mhz */
    0xf42b, /* 2.4576 Mhz */
    0x37e3, /* 3.57954 Mhz */
    0x1b21, /* 3.6864 Mhz */
    0x0c80, /* 4 Mhz */
    0x98ee, /* 4.906 Mhz */
    0xd5b4, /* 4.9152 Mhz */
    0x0a00, /* 5 Mhz */
    0x4e27, /* 5.12 Mhz */
    0x1902, /* 6 Mhz */
    0xec1c, /* 6.144 Mhz */
    0x1b23, /* 7.3728 Mhz */
    0x0640, /* 8 Mhz */
    0xb11c /* 8.192 Mhz */
};

static uint32_t ssys_read(void *opaque, target_phys_addr_t offset)
{
    ssys_state *s = (ssys_state *)opaque;

    switch (offset) {
    case 0x000: /* DID0 */
        return s->board->did0;
    case 0x004: /* DID1 */
        return s->board->did1;
    case 0x008: /* DC0 */
        return s->board->dc0;
    case 0x010: /* DC1 */
        return s->board->dc1;
    case 0x014: /* DC2 */
        return s->board->dc2;
    case 0x018: /* DC3 */
        return s->board->dc3;
    case 0x01c: /* DC4 */
        return s->board->dc4;
    case 0x030: /* PBORCTL */
        return s->pborctl;
    case 0x034: /* LDOPCTL */
        return s->ldopctl;
    case 0x040: /* SRCR0 */
        return 0;
    case 0x044: /* SRCR1 */
        return 0;
    case 0x048: /* SRCR2 */
        return 0;
    case 0x050: /* RIS */
        return s->int_status;
    case 0x054: /* IMC */
        return s->int_mask;
    case 0x058: /* MISC */
        return s->int_status & s->int_mask;
    case 0x05c: /* RESC */
        return s->resc;
    case 0x060: /* RCC */
        return s->rcc;
    case 0x064: /* PLLCFG */
        {
            int xtal;
            xtal = (s->rcc >> 6) & 0xf;
            if (s->board->did0 & (1 << 16)) {
                return pllcfg_fury[xtal];
            } else {
                return pllcfg_sandstorm[xtal];
            }
        }
    case 0x100: /* RCGC0 */
        return s->rcgc[0];
    case 0x104: /* RCGC1 */
        return s->rcgc[1];
    case 0x108: /* RCGC2 */
        return s->rcgc[2];
    case 0x110: /* SCGC0 */
        return s->scgc[0];
    case 0x114: /* SCGC1 */
        return s->scgc[1];
    case 0x118: /* SCGC2 */
        return s->scgc[2];
    case 0x120: /* DCGC0 */
        return s->dcgc[0];
    case 0x124: /* DCGC1 */
        return s->dcgc[1];
    case 0x128: /* DCGC2 */
        return s->dcgc[2];
    case 0x150: /* CLKVCLR */
        return s->clkvclr;
    case 0x160: /* LDOARST */
        return s->ldoarst;
    case 0x1e0: /* USER0 */
        return s->user0;
    case 0x1e4: /* USER1 */
        return s->user1;
    default:
        hw_error("ssys_read: Bad offset 0x%x\n", (int)offset);
        return 0;
    }
}

static void ssys_calculate_system_clock(ssys_state *s)
{
    system_clock_scale = 5 * (((s->rcc >> 23) & 0xf) + 1);
}

static void ssys_write(void *opaque, target_phys_addr_t offset, uint32_t value)
{
    ssys_state *s = (ssys_state *)opaque;

    switch (offset) {
    case 0x030: /* PBORCTL */
        s->pborctl = value & 0xffff;
        break;
    case 0x034: /* LDOPCTL */
        s->ldopctl = value & 0x1f;
        break;
    case 0x040: /* SRCR0 */
    case 0x044: /* SRCR1 */
    case 0x048: /* SRCR2 */
        fprintf(stderr, "Peripheral reset not implemented\n");
        break;
    case 0x054: /* IMC */
        s->int_mask = value & 0x7f;
        break;
    case 0x058: /* MISC */
        s->int_status &= ~value;
        break;
    case 0x05c: /* RESC */
        s->resc = value & 0x3f;
        break;
    case 0x060: /* RCC */
        if ((s->rcc & (1 << 13)) != 0 && (value & (1 << 13)) == 0) {
            /* PLL enable.  */
            s->int_status |= (1 << 6);
        }
        s->rcc = value;
        ssys_calculate_system_clock(s);
        break;
    case 0x100: /* RCGC0 */
        s->rcgc[0] = value;
        break;
    case 0x104: /* RCGC1 */
        s->rcgc[1] = value;
        break;
    case 0x108: /* RCGC2 */
        s->rcgc[2] = value;
        break;
    case 0x110: /* SCGC0 */
        s->scgc[0] = value;
        break;
    case 0x114: /* SCGC1 */
        s->scgc[1] = value;
        break;
    case 0x118: /* SCGC2 */
        s->scgc[2] = value;
        break;
    case 0x120: /* DCGC0 */
        s->dcgc[0] = value;
        break;
    case 0x124: /* DCGC1 */
        s->dcgc[1] = value;
        break;
    case 0x128: /* DCGC2 */
        s->dcgc[2] = value;
        break;
    case 0x150: /* CLKVCLR */
        s->clkvclr = value;
        break;
    case 0x160: /* LDOARST */
        s->ldoarst = value;
        break;
    default:
        hw_error("ssys_write: Bad offset 0x%x\n", (int)offset);
    }
    ssys_update(s);
}

static CPUReadMemoryFunc * const ssys_readfn[] = {
   ssys_read,
   ssys_read,
   ssys_read
};

static CPUWriteMemoryFunc * const ssys_writefn[] = {
   ssys_write,
   ssys_write,
   ssys_write
};

static void ssys_reset(void *opaque)
{
    ssys_state *s = (ssys_state *)opaque;

    s->pborctl = 0x7ffd;
    s->rcc = 0x078e3ac0;
    s->rcgc[0] = 1;
    s->scgc[0] = 1;
    s->dcgc[0] = 1;
}

static void ssys_save(QEMUFile *f, void *opaque)
{
    ssys_state *s = (ssys_state *)opaque;

    qemu_put_be32(f, s->pborctl);
    qemu_put_be32(f, s->ldopctl);
    qemu_put_be32(f, s->int_mask);
    qemu_put_be32(f, s->int_status);
    qemu_put_be32(f, s->resc);
    qemu_put_be32(f, s->rcc);
    qemu_put_be32(f, s->rcgc[0]);
    qemu_put_be32(f, s->rcgc[1]);
    qemu_put_be32(f, s->rcgc[2]);
    qemu_put_be32(f, s->scgc[0]);
    qemu_put_be32(f, s->scgc[1]);
    qemu_put_be32(f, s->scgc[2]);
    qemu_put_be32(f, s->dcgc[0]);
    qemu_put_be32(f, s->dcgc[1]);
    qemu_put_be32(f, s->dcgc[2]);
    qemu_put_be32(f, s->clkvclr);
    qemu_put_be32(f, s->ldoarst);
}

static int ssys_load(QEMUFile *f, void *opaque, int version_id)
{
    ssys_state *s = (ssys_state *)opaque;

    if (version_id != 1)
        return -EINVAL;

    s->pborctl = qemu_get_be32(f);
    s->ldopctl = qemu_get_be32(f);
    s->int_mask = qemu_get_be32(f);
    s->int_status = qemu_get_be32(f);
    s->resc = qemu_get_be32(f);
    s->rcc = qemu_get_be32(f);
    s->rcgc[0] = qemu_get_be32(f);
    s->rcgc[1] = qemu_get_be32(f);
    s->rcgc[2] = qemu_get_be32(f);
    s->scgc[0] = qemu_get_be32(f);
    s->scgc[1] = qemu_get_be32(f);
    s->scgc[2] = qemu_get_be32(f);
    s->dcgc[0] = qemu_get_be32(f);
    s->dcgc[1] = qemu_get_be32(f);
    s->dcgc[2] = qemu_get_be32(f);
    s->clkvclr = qemu_get_be32(f);
    s->ldoarst = qemu_get_be32(f);
    ssys_calculate_system_clock(s);

    return 0;
}

static int stellaris_sys_init(uint32_t base, qemu_irq irq,
                              stellaris_board_info * board,
                              uint8_t *macaddr)
{
    int iomemtype;
    ssys_state *s;

    s = (ssys_state *)qemu_mallocz(sizeof(ssys_state));
    s->irq = irq;
    s->board = board;
    /* Most devices come preprogrammed with a MAC address in the user data. */
    s->user0 = macaddr[0] | (macaddr[1] << 8) | (macaddr[2] << 16);
    s->user1 = macaddr[3] | (macaddr[4] << 8) | (macaddr[5] << 16);

    iomemtype = cpu_register_io_memory(ssys_readfn,
                                       ssys_writefn, s,
                                       DEVICE_NATIVE_ENDIAN);
    cpu_register_physical_memory(base, 0x00001000, iomemtype);
    ssys_reset(s);
    register_savevm(NULL, "stellaris_sys", -1, 1, ssys_save, ssys_load, s);
    return 0;
}


/* I2C controller.  */

typedef struct {
    SysBusDevice busdev;
    i2c_bus *bus;
    qemu_irq irq;
    uint32_t msa;
    uint32_t mcs;
    uint32_t mdr;
    uint32_t mtpr;
    uint32_t mimr;
    uint32_t mris;
    uint32_t mcr;
} stellaris_i2c_state;

#define STELLARIS_I2C_MCS_BUSY    0x01
#define STELLARIS_I2C_MCS_ERROR   0x02
#define STELLARIS_I2C_MCS_ADRACK  0x04
#define STELLARIS_I2C_MCS_DATACK  0x08
#define STELLARIS_I2C_MCS_ARBLST  0x10
#define STELLARIS_I2C_MCS_IDLE    0x20
#define STELLARIS_I2C_MCS_BUSBSY  0x40

static uint32_t stellaris_i2c_read(void *opaque, target_phys_addr_t offset)
{
    stellaris_i2c_state *s = (stellaris_i2c_state *)opaque;

    switch (offset) {
    case 0x00: /* MSA */
        return s->msa;
    case 0x04: /* MCS */
        /* We don't emulate timing, so the controller is never busy.  */
        return s->mcs | STELLARIS_I2C_MCS_IDLE;
    case 0x08: /* MDR */
        return s->mdr;
    case 0x0c: /* MTPR */
        return s->mtpr;
    case 0x10: /* MIMR */
        return s->mimr;
    case 0x14: /* MRIS */
        return s->mris;
    case 0x18: /* MMIS */
        return s->mris & s->mimr;
    case 0x20: /* MCR */
        return s->mcr;
    default:
        hw_error("strllaris_i2c_read: Bad offset 0x%x\n", (int)offset);
        return 0;
    }
}

static void stellaris_i2c_update(stellaris_i2c_state *s)
{
    int level;

    level = (s->mris & s->mimr) != 0;
    qemu_set_irq(s->irq, level);
}

static void stellaris_i2c_write(void *opaque, target_phys_addr_t offset,
                                uint32_t value)
{
    stellaris_i2c_state *s = (stellaris_i2c_state *)opaque;

    switch (offset) {
    case 0x00: /* MSA */
        s->msa = value & 0xff;
        break;
    case 0x04: /* MCS */
        if ((s->mcr & 0x10) == 0) {
            /* Disabled.  Do nothing.  */
            break;
        }
        /* Grab the bus if this is starting a transfer.  */
        if ((value & 2) && (s->mcs & STELLARIS_I2C_MCS_BUSBSY) == 0) {
            if (i2c_start_transfer(s->bus, s->msa >> 1, s->msa & 1)) {
                s->mcs |= STELLARIS_I2C_MCS_ARBLST;
            } else {
                s->mcs &= ~STELLARIS_I2C_MCS_ARBLST;
                s->mcs |= STELLARIS_I2C_MCS_BUSBSY;
            }
        }
        /* If we don't have the bus then indicate an error.  */
        if (!i2c_bus_busy(s->bus)
                || (s->mcs & STELLARIS_I2C_MCS_BUSBSY) == 0) {
            s->mcs |= STELLARIS_I2C_MCS_ERROR;
            break;
        }
        s->mcs &= ~STELLARIS_I2C_MCS_ERROR;
        if (value & 1) {
            /* Transfer a byte.  */
            /* TODO: Handle errors.  */
            if (s->msa & 1) {
                /* Recv */
                s->mdr = i2c_recv(s->bus) & 0xff;
            } else {
                /* Send */
                i2c_send(s->bus, s->mdr);
            }
            /* Raise an interrupt.  */
            s->mris |= 1;
        }
        if (value & 4) {
            /* Finish transfer.  */
            i2c_end_transfer(s->bus);
            s->mcs &= ~STELLARIS_I2C_MCS_BUSBSY;
        }
        break;
    case 0x08: /* MDR */
        s->mdr = value & 0xff;
        break;
    case 0x0c: /* MTPR */
        s->mtpr = value & 0xff;
        break;
    case 0x10: /* MIMR */
        s->mimr = 1;
        break;
    case 0x1c: /* MICR */
        s->mris &= ~value;
        break;
    case 0x20: /* MCR */
        if (value & 1)
            hw_error(
                      "stellaris_i2c_write: Loopback not implemented\n");
        if (value & 0x20)
            hw_error(
                      "stellaris_i2c_write: Slave mode not implemented\n");
        s->mcr = value & 0x31;
        break;
    default:
        hw_error("stellaris_i2c_write: Bad offset 0x%x\n",
                  (int)offset);
    }
    stellaris_i2c_update(s);
}

static void stellaris_i2c_reset(stellaris_i2c_state *s)
{
    if (s->mcs & STELLARIS_I2C_MCS_BUSBSY)
        i2c_end_transfer(s->bus);

    s->msa = 0;
    s->mcs = 0;
    s->mdr = 0;
    s->mtpr = 1;
    s->mimr = 0;
    s->mris = 0;
    s->mcr = 0;
    stellaris_i2c_update(s);
}

static CPUReadMemoryFunc * const stellaris_i2c_readfn[] = {
   stellaris_i2c_read,
   stellaris_i2c_read,
   stellaris_i2c_read
};

static CPUWriteMemoryFunc * const stellaris_i2c_writefn[] = {
   stellaris_i2c_write,
   stellaris_i2c_write,
   stellaris_i2c_write
};

static void stellaris_i2c_save(QEMUFile *f, void *opaque)
{
    stellaris_i2c_state *s = (stellaris_i2c_state *)opaque;

    qemu_put_be32(f, s->msa);
    qemu_put_be32(f, s->mcs);
    qemu_put_be32(f, s->mdr);
    qemu_put_be32(f, s->mtpr);
    qemu_put_be32(f, s->mimr);
    qemu_put_be32(f, s->mris);
    qemu_put_be32(f, s->mcr);
}

static int stellaris_i2c_load(QEMUFile *f, void *opaque, int version_id)
{
    stellaris_i2c_state *s = (stellaris_i2c_state *)opaque;

    if (version_id != 1)
        return -EINVAL;

    s->msa = qemu_get_be32(f);
    s->mcs = qemu_get_be32(f);
    s->mdr = qemu_get_be32(f);
    s->mtpr = qemu_get_be32(f);
    s->mimr = qemu_get_be32(f);
    s->mris = qemu_get_be32(f);
    s->mcr = qemu_get_be32(f);

    return 0;
}

static int stellaris_i2c_init(SysBusDevice * dev)
{
    stellaris_i2c_state *s = FROM_SYSBUS(stellaris_i2c_state, dev);
    i2c_bus *bus;
    int iomemtype;

    sysbus_init_irq(dev, &s->irq);
    bus = i2c_init_bus(&dev->qdev, "i2c");
    s->bus = bus;

    iomemtype = cpu_register_io_memory(stellaris_i2c_readfn,
                                       stellaris_i2c_writefn, s,
                                       DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, 0x1000, iomemtype);
    /* ??? For now we only implement the master interface.  */
    stellaris_i2c_reset(s);
    register_savevm(&dev->qdev, "stellaris_i2c", -1, 1,
                    stellaris_i2c_save, stellaris_i2c_load, s);
    return 0;
}

/* Analogue to Digital Converter.  This is only partially implemented,
   enough for applications that use a combined ADC and timer tick.  */

#define STELLARIS_ADC_EM_CONTROLLER 0
#define STELLARIS_ADC_EM_COMP       1
#define STELLARIS_ADC_EM_EXTERNAL   4
#define STELLARIS_ADC_EM_TIMER      5
#define STELLARIS_ADC_EM_PWM0       6
#define STELLARIS_ADC_EM_PWM1       7
#define STELLARIS_ADC_EM_PWM2       8

#define STELLARIS_ADC_FIFO_EMPTY    0x0100
#define STELLARIS_ADC_FIFO_FULL     0x1000

typedef struct
{
    SysBusDevice busdev;
    uint32_t actss;
    uint32_t ris;
    uint32_t im;
    uint32_t emux;
    uint32_t ostat;
    uint32_t ustat;
    uint32_t sspri;
    uint32_t sac;
    struct {
        uint32_t state;
        uint32_t data[16];
    } fifo[4];
    uint32_t ssmux[4];
    uint32_t ssctl[4];
    uint32_t noise;
    qemu_irq irq[4];
} stellaris_adc_state;

static uint32_t stellaris_adc_fifo_read(stellaris_adc_state *s, int n)
{
    int tail;

    tail = s->fifo[n].state & 0xf;
    if (s->fifo[n].state & STELLARIS_ADC_FIFO_EMPTY) {
        s->ustat |= 1 << n;
    } else {
        s->fifo[n].state = (s->fifo[n].state & ~0xf) | ((tail + 1) & 0xf);
        s->fifo[n].state &= ~STELLARIS_ADC_FIFO_FULL;
        if (tail + 1 == ((s->fifo[n].state >> 4) & 0xf))
            s->fifo[n].state |= STELLARIS_ADC_FIFO_EMPTY;
    }
    return s->fifo[n].data[tail];
}

static void stellaris_adc_fifo_write(stellaris_adc_state *s, int n,
                                     uint32_t value)
{
    int head;

    /* TODO: Real hardware has limited size FIFOs.  We have a full 16 entry 
       FIFO fir each sequencer.  */
    head = (s->fifo[n].state >> 4) & 0xf;
    if (s->fifo[n].state & STELLARIS_ADC_FIFO_FULL) {
        s->ostat |= 1 << n;
        return;
    }
    s->fifo[n].data[head] = value;
    head = (head + 1) & 0xf;
    s->fifo[n].state &= ~STELLARIS_ADC_FIFO_EMPTY;
    s->fifo[n].state = (s->fifo[n].state & ~0xf0) | (head << 4);
    if ((s->fifo[n].state & 0xf) == head)
        s->fifo[n].state |= STELLARIS_ADC_FIFO_FULL;
}

static void stellaris_adc_update(stellaris_adc_state *s)
{
    int level;
    int n;

    for (n = 0; n < 4; n++) {
        level = (s->ris & s->im & (1 << n)) != 0;
        qemu_set_irq(s->irq[n], level);
    }
}

static void stellaris_adc_trigger(void *opaque, int irq, int level)
{
    stellaris_adc_state *s = (stellaris_adc_state *)opaque;
    int n;

    for (n = 0; n < 4; n++) {
        if ((s->actss & (1 << n)) == 0) {
            continue;
        }

        if (((s->emux >> (n * 4)) & 0xff) != 5) {
            continue;
        }

        /* Some applications use the ADC as a random number source, so introduce
           some variation into the signal.  */
        s->noise = s->noise * 314159 + 1;
        /* ??? actual inputs not implemented.  Return an arbitrary value.  */
        stellaris_adc_fifo_write(s, n, 0x200 + ((s->noise >> 16) & 7));
        s->ris |= (1 << n);
        stellaris_adc_update(s);
    }
}

static void stellaris_adc_reset(stellaris_adc_state *s)
{
    int n;

    for (n = 0; n < 4; n++) {
        s->ssmux[n] = 0;
        s->ssctl[n] = 0;
        s->fifo[n].state = STELLARIS_ADC_FIFO_EMPTY;
    }
}

static uint32_t stellaris_adc_read(void *opaque, target_phys_addr_t offset)
{
    stellaris_adc_state *s = (stellaris_adc_state *)opaque;

    /* TODO: Implement this.  */
    if (offset >= 0x40 && offset < 0xc0) {
        int n;
        n = (offset - 0x40) >> 5;
        switch (offset & 0x1f) {
        case 0x00: /* SSMUX */
            return s->ssmux[n];
        case 0x04: /* SSCTL */
            return s->ssctl[n];
        case 0x08: /* SSFIFO */
            return stellaris_adc_fifo_read(s, n);
        case 0x0c: /* SSFSTAT */
            return s->fifo[n].state;
        default:
            break;
        }
    }
    switch (offset) {
    case 0x00: /* ACTSS */
        return s->actss;
    case 0x04: /* RIS */
        return s->ris;
    case 0x08: /* IM */
        return s->im;
    case 0x0c: /* ISC */
        return s->ris & s->im;
    case 0x10: /* OSTAT */
        return s->ostat;
    case 0x14: /* EMUX */
        return s->emux;
    case 0x18: /* USTAT */
        return s->ustat;
    case 0x20: /* SSPRI */
        return s->sspri;
    case 0x30: /* SAC */
        return s->sac;
    default:
        hw_error("strllaris_adc_read: Bad offset 0x%x\n",
                  (int)offset);
        return 0;
    }
}

static void stellaris_adc_write(void *opaque, target_phys_addr_t offset,
                                uint32_t value)
{
    stellaris_adc_state *s = (stellaris_adc_state *)opaque;

    /* TODO: Implement this.  */
    if (offset >= 0x40 && offset < 0xc0) {
        int n;
        n = (offset - 0x40) >> 5;
        switch (offset & 0x1f) {
        case 0x00: /* SSMUX */
            s->ssmux[n] = value & 0x33333333;
            return;
        case 0x04: /* SSCTL */
            if (value != 6) {
                hw_error("ADC: Unimplemented sequence %x\n",
                          value);
            }
            s->ssctl[n] = value;
            return;
        default:
            break;
        }
    }
    switch (offset) {
    case 0x00: /* ACTSS */
        s->actss = value & 0xf;
        break;
    case 0x08: /* IM */
        s->im = value;
        break;
    case 0x0c: /* ISC */
        s->ris &= ~value;
        break;
    case 0x10: /* OSTAT */
        s->ostat &= ~value;
        break;
    case 0x14: /* EMUX */
        s->emux = value;
        break;
    case 0x18: /* USTAT */
        s->ustat &= ~value;
        break;
    case 0x20: /* SSPRI */
        s->sspri = value;
        break;
    case 0x28: /* PSSI */
        hw_error("Not implemented:  ADC sample initiate\n");
        break;
    case 0x30: /* SAC */
        s->sac = value;
        break;
    default:
        hw_error("stellaris_adc_write: Bad offset 0x%x\n", (int)offset);
    }
    stellaris_adc_update(s);
}

static CPUReadMemoryFunc * const stellaris_adc_readfn[] = {
   stellaris_adc_read,
   stellaris_adc_read,
   stellaris_adc_read
};

static CPUWriteMemoryFunc * const stellaris_adc_writefn[] = {
   stellaris_adc_write,
   stellaris_adc_write,
   stellaris_adc_write
};

static void stellaris_adc_save(QEMUFile *f, void *opaque)
{
    stellaris_adc_state *s = (stellaris_adc_state *)opaque;
    int i;
    int j;

    qemu_put_be32(f, s->actss);
    qemu_put_be32(f, s->ris);
    qemu_put_be32(f, s->im);
    qemu_put_be32(f, s->emux);
    qemu_put_be32(f, s->ostat);
    qemu_put_be32(f, s->ustat);
    qemu_put_be32(f, s->sspri);
    qemu_put_be32(f, s->sac);
    for (i = 0; i < 4; i++) {
        qemu_put_be32(f, s->fifo[i].state);
        for (j = 0; j < 16; j++) {
            qemu_put_be32(f, s->fifo[i].data[j]);
        }
        qemu_put_be32(f, s->ssmux[i]);
        qemu_put_be32(f, s->ssctl[i]);
    }
    qemu_put_be32(f, s->noise);
}

static int stellaris_adc_load(QEMUFile *f, void *opaque, int version_id)
{
    stellaris_adc_state *s = (stellaris_adc_state *)opaque;
    int i;
    int j;

    if (version_id != 1)
        return -EINVAL;

    s->actss = qemu_get_be32(f);
    s->ris = qemu_get_be32(f);
    s->im = qemu_get_be32(f);
    s->emux = qemu_get_be32(f);
    s->ostat = qemu_get_be32(f);
    s->ustat = qemu_get_be32(f);
    s->sspri = qemu_get_be32(f);
    s->sac = qemu_get_be32(f);
    for (i = 0; i < 4; i++) {
        s->fifo[i].state = qemu_get_be32(f);
        for (j = 0; j < 16; j++) {
            s->fifo[i].data[j] = qemu_get_be32(f);
        }
        s->ssmux[i] = qemu_get_be32(f);
        s->ssctl[i] = qemu_get_be32(f);
    }
    s->noise = qemu_get_be32(f);

    return 0;
}

static int stellaris_adc_init(SysBusDevice *dev)
{
    stellaris_adc_state *s = FROM_SYSBUS(stellaris_adc_state, dev);
    int iomemtype;
    int n;

    for (n = 0; n < 4; n++) {
        sysbus_init_irq(dev, &s->irq[n]);
    }

    iomemtype = cpu_register_io_memory(stellaris_adc_readfn,
                                       stellaris_adc_writefn, s,
                                       DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, 0x1000, iomemtype);
    stellaris_adc_reset(s);
    qdev_init_gpio_in(&dev->qdev, stellaris_adc_trigger, 1);
    register_savevm(&dev->qdev, "stellaris_adc", -1, 1,
                    stellaris_adc_save, stellaris_adc_load, s);
    return 0;
}

/* Some boards have both an OLED controller and SD card connected to
   the same SSI port, with the SD card chip select connected to a
   GPIO pin.  Technically the OLED chip select is connected to the SSI
   Fss pin.  We do not bother emulating that as both devices should
   never be selected simultaneously, and our OLED controller ignores stray
   0xff commands that occur when deselecting the SD card.  */

typedef struct {
    SSISlave ssidev;
    qemu_irq irq;
    int current_dev;
    SSIBus *bus[2];
} stellaris_ssi_bus_state;

static void stellaris_ssi_bus_select(void *opaque, int irq, int level)
{
    stellaris_ssi_bus_state *s = (stellaris_ssi_bus_state *)opaque;

    s->current_dev = level;
}

static uint32_t stellaris_ssi_bus_transfer(SSISlave *dev, uint32_t val)
{
    stellaris_ssi_bus_state *s = FROM_SSI_SLAVE(stellaris_ssi_bus_state, dev);

    return ssi_transfer(s->bus[s->current_dev], val);
}

static void stellaris_ssi_bus_save(QEMUFile *f, void *opaque)
{
    stellaris_ssi_bus_state *s = (stellaris_ssi_bus_state *)opaque;

    qemu_put_be32(f, s->current_dev);
}

static int stellaris_ssi_bus_load(QEMUFile *f, void *opaque, int version_id)
{
    stellaris_ssi_bus_state *s = (stellaris_ssi_bus_state *)opaque;

    if (version_id != 1)
        return -EINVAL;

    s->current_dev = qemu_get_be32(f);

    return 0;
}

static int stellaris_ssi_bus_init(SSISlave *dev)
{
    stellaris_ssi_bus_state *s = FROM_SSI_SLAVE(stellaris_ssi_bus_state, dev);

    s->bus[0] = ssi_create_bus(&dev->qdev, "ssi0");
    s->bus[1] = ssi_create_bus(&dev->qdev, "ssi1");
    qdev_init_gpio_in(&dev->qdev, stellaris_ssi_bus_select, 1);

    register_savevm(&dev->qdev, "stellaris_ssi_bus", -1, 1,
                    stellaris_ssi_bus_save, stellaris_ssi_bus_load, s);
    return 0;
}

/* Board init.  */
static stellaris_board_info stellaris_boards[] = {
  { "LM3S811EVB",
    0,
    0x0032000e,
    0x001f001f, /* dc0 */
    0x001132bf,
    0x01071013,
    0x3f0f01ff,
    0x0000001f,
    BP_OLED_I2C
  },
  { "LM3S6965EVB",
    0x10010002,
    0x1073402e,
    0x00ff007f, /* dc0 */
    0x001133ff,
    0x030f5317,
    0x0f0f87ff,
    0x5000007f,
    BP_OLED_SSI | BP_GAMEPAD
  }
};

static void stellaris_init(const char *kernel_filename, const char *cpu_model,
                           stellaris_board_info *board)
{
    static const int uart_irq[] = {5, 6, 33, 34};
    static const int timer_irq[] = {19, 21, 23, 35};
    static const uint32_t gpio_addr[7] =
      { 0x40004000, 0x40005000, 0x40006000, 0x40007000,
        0x40024000, 0x40025000, 0x40026000};
    static const int gpio_irq[7] = {0, 1, 2, 3, 4, 30, 31};

    qemu_irq *pic;
    DeviceState *gpio_dev[7];
    qemu_irq gpio_in[7][8];
    qemu_irq gpio_out[7][8];
    qemu_irq adc;
    int sram_size;
    int flash_size;
    i2c_bus *i2c;
    DeviceState *dev;
    int i;
    int j;

    flash_size = ((board->dc0 & 0xffff) + 1) << 1;
    sram_size = (board->dc0 >> 18) + 1;
    pic = armv7m_init(flash_size, sram_size, kernel_filename, cpu_model);

    if (board->dc1 & (1 << 16)) {
        dev = sysbus_create_varargs("stellaris-adc", 0x40038000,
                                    pic[14], pic[15], pic[16], pic[17], NULL);
        adc = qdev_get_gpio_in(dev, 0);
    } else {
        adc = NULL;
    }
    for (i = 0; i < 4; i++) {
        if (board->dc2 & (0x10000 << i)) {
            dev = sysbus_create_simple("stellaris-gptm",
                                       0x40030000 + i * 0x1000,
                                       pic[timer_irq[i]]);
            /* TODO: This is incorrect, but we get away with it because
               the ADC output is only ever pulsed.  */
            qdev_connect_gpio_out(dev, 0, adc);
        }
    }

    stellaris_sys_init(0x400fe000, pic[28], board, nd_table[0].macaddr);

    for (i = 0; i < 7; i++) {
        if (board->dc4 & (1 << i)) {
            gpio_dev[i] = sysbus_create_simple("pl061", gpio_addr[i],
                                               pic[gpio_irq[i]]);
            for (j = 0; j < 8; j++) {
                gpio_in[i][j] = qdev_get_gpio_in(gpio_dev[i], j);
                gpio_out[i][j] = NULL;
            }
        }
    }

    if (board->dc2 & (1 << 12)) {
        dev = sysbus_create_simple("stellaris-i2c", 0x40020000, pic[8]);
        i2c = (i2c_bus *)qdev_get_child_bus(dev, "i2c");
        if (board->peripherals & BP_OLED_I2C) {
            i2c_create_slave(i2c, "ssd0303", 0x3d);
        }
    }

    for (i = 0; i < 4; i++) {
        if (board->dc2 & (1 << i)) {
            sysbus_create_simple("pl011_luminary", 0x4000c000 + i * 0x1000,
                                 pic[uart_irq[i]]);
        }
    }
    if (board->dc2 & (1 << 4)) {
        dev = sysbus_create_simple("pl022", 0x40008000, pic[7]);
        if (board->peripherals & BP_OLED_SSI) {
            DeviceState *mux;
            void *bus;

            bus = qdev_get_child_bus(dev, "ssi");
            mux = ssi_create_slave(bus, "evb6965-ssi");
            gpio_out[GPIO_D][0] = qdev_get_gpio_in(mux, 0);

            bus = qdev_get_child_bus(mux, "ssi0");
            ssi_create_slave(bus, "ssi-sd");

            bus = qdev_get_child_bus(mux, "ssi1");
            dev = ssi_create_slave(bus, "ssd0323");
            gpio_out[GPIO_C][7] = qdev_get_gpio_in(dev, 0);

            /* Make sure the select pin is high.  */
            qemu_irq_raise(gpio_out[GPIO_D][0]);
        }
    }
    if (board->dc4 & (1 << 28)) {
        DeviceState *enet;

        qemu_check_nic_model(&nd_table[0], "stellaris");

        enet = qdev_create(NULL, "stellaris_enet");
        qdev_set_nic_properties(enet, &nd_table[0]);
        qdev_init_nofail(enet);
        sysbus_mmio_map(sysbus_from_qdev(enet), 0, 0x40048000);
        sysbus_connect_irq(sysbus_from_qdev(enet), 0, pic[42]);
    }
    if (board->peripherals & BP_GAMEPAD) {
        qemu_irq gpad_irq[5];
        static const int gpad_keycode[5] = { 0xc8, 0xd0, 0xcb, 0xcd, 0x1d };

        gpad_irq[0] = qemu_irq_invert(gpio_in[GPIO_E][0]); /* up */
        gpad_irq[1] = qemu_irq_invert(gpio_in[GPIO_E][1]); /* down */
        gpad_irq[2] = qemu_irq_invert(gpio_in[GPIO_E][2]); /* left */
        gpad_irq[3] = qemu_irq_invert(gpio_in[GPIO_E][3]); /* right */
        gpad_irq[4] = qemu_irq_invert(gpio_in[GPIO_F][1]); /* select */

        stellaris_gamepad_init(5, gpad_irq, gpad_keycode);
    }
    for (i = 0; i < 7; i++) {
        if (board->dc4 & (1 << i)) {
            for (j = 0; j < 8; j++) {
                if (gpio_out[i][j]) {
                    qdev_connect_gpio_out(gpio_dev[i], j, gpio_out[i][j]);
                }
            }
        }
    }
}

/* FIXME: Figure out how to generate these from stellaris_boards.  */
static void lm3s811evb_init(ram_addr_t ram_size,
                     const char *boot_device,
                     const char *kernel_filename, const char *kernel_cmdline,
                     const char *initrd_filename, const char *cpu_model)
{
    stellaris_init(kernel_filename, cpu_model, &stellaris_boards[0]);
}

static void lm3s6965evb_init(ram_addr_t ram_size,
                     const char *boot_device,
                     const char *kernel_filename, const char *kernel_cmdline,
                     const char *initrd_filename, const char *cpu_model)
{
    stellaris_init(kernel_filename, cpu_model, &stellaris_boards[1]);
}

static QEMUMachine lm3s811evb_machine = {
    .name = "lm3s811evb",
    .desc = "Stellaris LM3S811EVB",
    .init = lm3s811evb_init,
};

static QEMUMachine lm3s6965evb_machine = {
    .name = "lm3s6965evb",
    .desc = "Stellaris LM3S6965EVB",
    .init = lm3s6965evb_init,
};

static void stellaris_machine_init(void)
{
    qemu_register_machine(&lm3s811evb_machine);
    qemu_register_machine(&lm3s6965evb_machine);
}

machine_init(stellaris_machine_init);

static SSISlaveInfo stellaris_ssi_bus_info = {
    .qdev.name = "evb6965-ssi",
    .qdev.size = sizeof(stellaris_ssi_bus_state),
    .init = stellaris_ssi_bus_init,
    .transfer = stellaris_ssi_bus_transfer
};

static void stellaris_register_devices(void)
{
    sysbus_register_dev("stellaris-i2c", sizeof(stellaris_i2c_state),
                        stellaris_i2c_init);
    sysbus_register_dev("stellaris-gptm", sizeof(gptm_state),
                        stellaris_gptm_init);
    sysbus_register_dev("stellaris-adc", sizeof(stellaris_adc_state),
                        stellaris_adc_init);
    ssi_register_slave(&stellaris_ssi_bus_info);
}

device_init(stellaris_register_devices)
