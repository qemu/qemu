/*
 * National Semiconductor LM8322/8323 GPIO keyboard & PWM chips.
 *
 * Copyright (C) 2008 Nokia Corporation
 * Written by Andrzej Zaborowski <andrew@openedhand.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "hw.h"
#include "i2c.h"
#include "qemu-timer.h"
#include "console.h"

struct lm_kbd_s {
    i2c_slave i2c;
    int i2c_dir;
    int i2c_cycle;
    int reg;

    qemu_irq nirq;
    uint16_t model;

    struct {
        qemu_irq out[2];
        int in[2][2];
    } mux;

    uint8_t config;
    uint8_t status;
    uint8_t acttime;
    uint8_t error;
    uint8_t clock;

    struct {
        uint16_t pull;
        uint16_t mask;
        uint16_t dir;
        uint16_t level;
        qemu_irq out[16];
    } gpio;

    struct {
        uint8_t dbnctime;
        uint8_t size;
        int start;
        int len;
        uint8_t fifo[16];
    } kbd;

    struct {
        uint16_t file[256];
	uint8_t faddr;
        uint8_t addr[3];
        QEMUTimer *tm[3];
    } pwm;
};

#define INT_KEYPAD		(1 << 0)
#define INT_ERROR		(1 << 3)
#define INT_NOINIT		(1 << 4)
#define INT_PWMEND(n)		(1 << (5 + n))

#define ERR_BADPAR		(1 << 0)
#define ERR_CMDUNK		(1 << 1)
#define ERR_KEYOVR		(1 << 2)
#define ERR_FIFOOVR		(1 << 6)

static void lm_kbd_irq_update(struct lm_kbd_s *s)
{
    qemu_set_irq(s->nirq, !s->status);
}

static void lm_kbd_gpio_update(struct lm_kbd_s *s)
{
}

static void lm_kbd_reset(struct lm_kbd_s *s)
{
    s->config = 0x80;
    s->status = INT_NOINIT;
    s->acttime = 125;
    s->kbd.dbnctime = 3;
    s->kbd.size = 0x33;
    s->clock = 0x08;

    lm_kbd_irq_update(s);
    lm_kbd_gpio_update(s);
}

static void lm_kbd_error(struct lm_kbd_s *s, int err)
{
    s->error |= err;
    s->status |= INT_ERROR;
    lm_kbd_irq_update(s);
}

static void lm_kbd_pwm_tick(struct lm_kbd_s *s, int line)
{
}

static void lm_kbd_pwm_start(struct lm_kbd_s *s, int line)
{
    lm_kbd_pwm_tick(s, line);
}

static void lm_kbd_pwm0_tick(void *opaque)
{
    lm_kbd_pwm_tick(opaque, 0);
}
static void lm_kbd_pwm1_tick(void *opaque)
{
    lm_kbd_pwm_tick(opaque, 1);
}
static void lm_kbd_pwm2_tick(void *opaque)
{
    lm_kbd_pwm_tick(opaque, 2);
}

enum {
    LM832x_CMD_READ_ID		= 0x80, /* Read chip ID. */
    LM832x_CMD_WRITE_CFG	= 0x81, /* Set configuration item. */
    LM832x_CMD_READ_INT		= 0x82, /* Get interrupt status. */
    LM832x_CMD_RESET		= 0x83, /* Reset, same as external one */
    LM823x_CMD_WRITE_PULL_DOWN	= 0x84, /* Select GPIO pull-up/down. */
    LM832x_CMD_WRITE_PORT_SEL	= 0x85, /* Select GPIO in/out. */
    LM832x_CMD_WRITE_PORT_STATE	= 0x86, /* Set GPIO pull-up/down. */
    LM832x_CMD_READ_PORT_SEL	= 0x87, /* Get GPIO in/out. */
    LM832x_CMD_READ_PORT_STATE	= 0x88, /* Get GPIO pull-up/down. */
    LM832x_CMD_READ_FIFO	= 0x89, /* Read byte from FIFO. */
    LM832x_CMD_RPT_READ_FIFO	= 0x8a, /* Read FIFO (no increment). */
    LM832x_CMD_SET_ACTIVE	= 0x8b, /* Set active time. */
    LM832x_CMD_READ_ERROR	= 0x8c, /* Get error status. */
    LM832x_CMD_READ_ROTATOR	= 0x8e, /* Read rotator status. */
    LM832x_CMD_SET_DEBOUNCE	= 0x8f, /* Set debouncing time. */
    LM832x_CMD_SET_KEY_SIZE	= 0x90, /* Set keypad size. */
    LM832x_CMD_READ_KEY_SIZE	= 0x91, /* Get keypad size. */
    LM832x_CMD_READ_CFG		= 0x92, /* Get configuration item. */
    LM832x_CMD_WRITE_CLOCK	= 0x93, /* Set clock config. */
    LM832x_CMD_READ_CLOCK	= 0x94, /* Get clock config. */
    LM832x_CMD_PWM_WRITE	= 0x95, /* Write PWM script. */
    LM832x_CMD_PWM_START	= 0x96, /* Start PWM engine. */
    LM832x_CMD_PWM_STOP		= 0x97, /* Stop PWM engine. */
};

#define LM832x_MAX_KPX		8
#define LM832x_MAX_KPY		12

static uint8_t lm_kbd_read(struct lm_kbd_s *s, int reg, int byte)
{
    int ret;

    switch (reg) {
    case LM832x_CMD_READ_ID:
        ret = 0x0400;
        break;

    case LM832x_CMD_READ_INT:
        ret = s->status;
        if (!(s->status & INT_NOINIT)) {
            s->status = 0;
            lm_kbd_irq_update(s);
        }
        break;

    case LM832x_CMD_READ_PORT_SEL:
        ret = s->gpio.dir;
        break;
    case LM832x_CMD_READ_PORT_STATE:
        ret = s->gpio.mask;
        break;

    case LM832x_CMD_READ_FIFO:
        if (s->kbd.len <= 1)
            return 0x00;

        /* Example response from the two commands after a INT_KEYPAD
         * interrupt caused by the key 0x3c being pressed:
         * RPT_READ_FIFO: 55 bc 00 4e ff 0a 50 08 00 29 d9 08 01 c9 01
         *     READ_FIFO: bc 00 00 4e ff 0a 50 08 00 29 d9 08 01 c9 01
         * RPT_READ_FIFO: bc 00 00 4e ff 0a 50 08 00 29 d9 08 01 c9 01
         *
         * 55 is the code of the key release event serviced in the previous
         * interrupt handling.
         *
         * TODO: find out whether the FIFO is advanced a single character
         * before reading every byte or the whole size of the FIFO at the
         * last LM832x_CMD_READ_FIFO.  This affects LM832x_CMD_RPT_READ_FIFO
         * output in cases where there are more than one event in the FIFO.
         * Assume 0xbc and 0x3c events are in the FIFO:
         * RPT_READ_FIFO: 55 bc 3c 00 4e ff 0a 50 08 00 29 d9 08 01 c9
         *     READ_FIFO: bc 3c 00 00 4e ff 0a 50 08 00 29 d9 08 01 c9
         * Does RPT_READ_FIFO now return 0xbc and 0x3c or only 0x3c?
         */
        s->kbd.start ++;
        s->kbd.start &= sizeof(s->kbd.fifo) - 1;
        s->kbd.len --;

        return s->kbd.fifo[s->kbd.start];
    case LM832x_CMD_RPT_READ_FIFO:
        if (byte >= s->kbd.len)
            return 0x00;

        return s->kbd.fifo[(s->kbd.start + byte) & (sizeof(s->kbd.fifo) - 1)];

    case LM832x_CMD_READ_ERROR:
        return s->error;

    case LM832x_CMD_READ_ROTATOR:
        return 0;

    case LM832x_CMD_READ_KEY_SIZE:
        return s->kbd.size;

    case LM832x_CMD_READ_CFG:
        return s->config & 0xf;

    case LM832x_CMD_READ_CLOCK:
        return (s->clock & 0xfc) | 2;

    default:
        lm_kbd_error(s, ERR_CMDUNK);
        fprintf(stderr, "%s: unknown command %02x\n", __FUNCTION__, reg);
        return 0x00;
    }

    return ret >> (byte << 3);
}

static void lm_kbd_write(struct lm_kbd_s *s, int reg, int byte, uint8_t value)
{
    switch (reg) {
    case LM832x_CMD_WRITE_CFG:
        s->config = value;
        /* This must be done whenever s->mux.in is updated (never).  */
        if ((s->config >> 1) & 1)			/* MUX1EN */
            qemu_set_irq(s->mux.out[0], s->mux.in[0][(s->config >> 0) & 1]);
        if ((s->config >> 3) & 1)			/* MUX2EN */
            qemu_set_irq(s->mux.out[0], s->mux.in[0][(s->config >> 2) & 1]);
        /* TODO: check that this is issued only following the chip reset
         * and not in the middle of operation and that it is followed by
         * the GPIO ports re-resablishing through WRITE_PORT_SEL and
         * WRITE_PORT_STATE (using a timer perhaps) and otherwise output
         * warnings.  */
        s->status = 0;
        lm_kbd_irq_update(s);
        s->kbd.len = 0;
        s->kbd.start = 0;
        s->reg = -1;
        break;

    case LM832x_CMD_RESET:
        if (value == 0xaa)
            lm_kbd_reset(s);
        else
            lm_kbd_error(s, ERR_BADPAR);
        s->reg = -1;
        break;

    case LM823x_CMD_WRITE_PULL_DOWN:
        if (!byte)
            s->gpio.pull = value;
        else {
            s->gpio.pull |= value << 8;
            lm_kbd_gpio_update(s);
            s->reg = -1;
        }
        break;
    case LM832x_CMD_WRITE_PORT_SEL:
        if (!byte)
            s->gpio.dir = value;
        else {
            s->gpio.dir |= value << 8;
            lm_kbd_gpio_update(s);
            s->reg = -1;
        }
        break;
    case LM832x_CMD_WRITE_PORT_STATE:
        if (!byte)
            s->gpio.mask = value;
        else {
            s->gpio.mask |= value << 8;
            lm_kbd_gpio_update(s);
            s->reg = -1;
        }
        break;

    case LM832x_CMD_SET_ACTIVE:
        s->acttime = value;
        s->reg = -1;
        break;

    case LM832x_CMD_SET_DEBOUNCE:
        s->kbd.dbnctime = value;
        s->reg = -1;
        if (!value)
            lm_kbd_error(s, ERR_BADPAR);
        break;

    case LM832x_CMD_SET_KEY_SIZE:
        s->kbd.size = value;
        s->reg = -1;
        if (
                        (value & 0xf) < 3 || (value & 0xf) > LM832x_MAX_KPY ||
                        (value >> 4) < 3 || (value >> 4) > LM832x_MAX_KPX)
            lm_kbd_error(s, ERR_BADPAR);
        break;

    case LM832x_CMD_WRITE_CLOCK:
        s->clock = value;
        s->reg = -1;
        if ((value & 3) && (value & 3) != 3) {
            lm_kbd_error(s, ERR_BADPAR);
            fprintf(stderr, "%s: invalid clock setting in RCPWM\n",
                            __FUNCTION__);
        }
        /* TODO: Validate that the command is only issued once */
        break;

    case LM832x_CMD_PWM_WRITE:
        if (byte == 0) {
            if (!(value & 3) || (value >> 2) > 59) {
                lm_kbd_error(s, ERR_BADPAR);
                s->reg = -1;
                break;
            }

            s->pwm.faddr = value;
            s->pwm.file[s->pwm.faddr] = 0;
        } else if (byte == 1) {
            s->pwm.file[s->pwm.faddr] |= value << 8;
        } else if (byte == 2) {
            s->pwm.file[s->pwm.faddr] |= value << 0;
            s->reg = -1;
        }
        break;
    case LM832x_CMD_PWM_START:
        s->reg = -1;
        if (!(value & 3) || (value >> 2) > 59) {
            lm_kbd_error(s, ERR_BADPAR);
            break;
        }

        s->pwm.addr[(value & 3) - 1] = value >> 2;
        lm_kbd_pwm_start(s, (value & 3) - 1);
        break;
    case LM832x_CMD_PWM_STOP:
        s->reg = -1;
        if (!(value & 3)) {
            lm_kbd_error(s, ERR_BADPAR);
            break;
        }

        qemu_del_timer(s->pwm.tm[(value & 3) - 1]);
        break;

    case -1:
        lm_kbd_error(s, ERR_BADPAR);
        break;
    default:
        lm_kbd_error(s, ERR_CMDUNK);
        fprintf(stderr, "%s: unknown command %02x\n", __FUNCTION__, reg);
        break;
    }
}

static void lm_i2c_event(i2c_slave *i2c, enum i2c_event event)
{
    struct lm_kbd_s *s = (struct lm_kbd_s *) i2c;

    switch (event) {
    case I2C_START_RECV:
    case I2C_START_SEND:
        s->i2c_cycle = 0;
        s->i2c_dir = (event == I2C_START_SEND);
        break;

    default:
        break;
    }
}

static int lm_i2c_rx(i2c_slave *i2c)
{
    struct lm_kbd_s *s = (struct lm_kbd_s *) i2c;

    return lm_kbd_read(s, s->reg, s->i2c_cycle ++);
}

static int lm_i2c_tx(i2c_slave *i2c, uint8_t data)
{
    struct lm_kbd_s *s = (struct lm_kbd_s *) i2c;

    if (!s->i2c_cycle)
        s->reg = data;
    else
        lm_kbd_write(s, s->reg, s->i2c_cycle - 1, data);
    s->i2c_cycle ++;

    return 0;
}

static void lm_kbd_save(QEMUFile *f, void *opaque)
{
    struct lm_kbd_s *s = (struct lm_kbd_s *) opaque;
    int i;

    i2c_slave_save(f, &s->i2c);
    qemu_put_byte(f, s->i2c_dir);
    qemu_put_byte(f, s->i2c_cycle);
    qemu_put_byte(f, (uint8_t) s->reg);

    qemu_put_8s(f, &s->config);
    qemu_put_8s(f, &s->status);
    qemu_put_8s(f, &s->acttime);
    qemu_put_8s(f, &s->error);
    qemu_put_8s(f, &s->clock);

    qemu_put_be16s(f, &s->gpio.pull);
    qemu_put_be16s(f, &s->gpio.mask);
    qemu_put_be16s(f, &s->gpio.dir);
    qemu_put_be16s(f, &s->gpio.level);

    qemu_put_byte(f, s->kbd.dbnctime);
    qemu_put_byte(f, s->kbd.size);
    qemu_put_byte(f, s->kbd.start);
    qemu_put_byte(f, s->kbd.len);
    qemu_put_buffer(f, s->kbd.fifo, sizeof(s->kbd.fifo));

    for (i = 0; i < sizeof(s->pwm.file); i ++)
        qemu_put_be16s(f, &s->pwm.file[i]);
    qemu_put_8s(f, &s->pwm.faddr);
    qemu_put_buffer(f, s->pwm.addr, sizeof(s->pwm.addr));
    qemu_put_timer(f, s->pwm.tm[0]);
    qemu_put_timer(f, s->pwm.tm[1]);
    qemu_put_timer(f, s->pwm.tm[2]);
}

static int lm_kbd_load(QEMUFile *f, void *opaque, int version_id)
{
    struct lm_kbd_s *s = (struct lm_kbd_s *) opaque;
    int i;

    i2c_slave_load(f, &s->i2c);
    s->i2c_dir = qemu_get_byte(f);
    s->i2c_cycle = qemu_get_byte(f);
    s->reg = (int8_t) qemu_get_byte(f);

    qemu_get_8s(f, &s->config);
    qemu_get_8s(f, &s->status);
    qemu_get_8s(f, &s->acttime);
    qemu_get_8s(f, &s->error);
    qemu_get_8s(f, &s->clock);

    qemu_get_be16s(f, &s->gpio.pull);
    qemu_get_be16s(f, &s->gpio.mask);
    qemu_get_be16s(f, &s->gpio.dir);
    qemu_get_be16s(f, &s->gpio.level);

    s->kbd.dbnctime = qemu_get_byte(f);
    s->kbd.size = qemu_get_byte(f);
    s->kbd.start = qemu_get_byte(f);
    s->kbd.len = qemu_get_byte(f);
    qemu_get_buffer(f, s->kbd.fifo, sizeof(s->kbd.fifo));

    for (i = 0; i < sizeof(s->pwm.file); i ++)
        qemu_get_be16s(f, &s->pwm.file[i]);
    qemu_get_8s(f, &s->pwm.faddr);
    qemu_get_buffer(f, s->pwm.addr, sizeof(s->pwm.addr));
    qemu_get_timer(f, s->pwm.tm[0]);
    qemu_get_timer(f, s->pwm.tm[1]);
    qemu_get_timer(f, s->pwm.tm[2]);

    lm_kbd_irq_update(s);
    lm_kbd_gpio_update(s);

    return 0;
}

struct i2c_slave *lm8323_init(i2c_bus *bus, qemu_irq nirq)
{
    struct lm_kbd_s *s;

    s = (struct lm_kbd_s *) i2c_slave_init(bus, 0, sizeof(struct lm_kbd_s));
    s->model = 0x8323;
    s->pwm.tm[0] = qemu_new_timer(vm_clock, lm_kbd_pwm0_tick, s);
    s->pwm.tm[1] = qemu_new_timer(vm_clock, lm_kbd_pwm1_tick, s);
    s->pwm.tm[2] = qemu_new_timer(vm_clock, lm_kbd_pwm2_tick, s);
    s->nirq = nirq;

    s->i2c.event = lm_i2c_event;
    s->i2c.recv = lm_i2c_rx;
    s->i2c.send = lm_i2c_tx;

    lm_kbd_reset(s);

    qemu_register_reset((void *) lm_kbd_reset, s);
    register_savevm("LM8323", -1, 0, lm_kbd_save, lm_kbd_load, s);

    return &s->i2c;
}

void lm832x_key_event(struct i2c_slave *i2c, int key, int state)
{
    struct lm_kbd_s *s = (struct lm_kbd_s *) i2c;

    if ((s->status & INT_ERROR) && (s->error & ERR_FIFOOVR))
        return;

    if (s->kbd.len >= sizeof(s->kbd.fifo)) {
        lm_kbd_error(s, ERR_FIFOOVR);
        return;
    }

    s->kbd.fifo[(s->kbd.start + s->kbd.len ++) & (sizeof(s->kbd.fifo) - 1)] =
            key | (state << 7);

    /* We never set ERR_KEYOVR because we support multiple keys fine.  */
    s->status |= INT_KEYPAD;
    lm_kbd_irq_update(s);
}
