/*
 * CBUS three-pin bus and the Retu / Betty / Tahvo / Vilma / Avilma /
 * Hinku / Vinku / Ahne / Pihi chips used in various Nokia platforms.
 * Based on reverse-engineering of a linux driver.
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

#include "qemu-common.h"
#include "irq.h"
#include "devices.h"
#include "sysemu.h"

//#define DEBUG

typedef struct {
    void *opaque;
    void (*io)(void *opaque, int rw, int reg, uint16_t *val);
    int addr;
} CBusSlave;

typedef struct {
    CBus cbus;

    int sel;
    int dat;
    int clk;
    int bit;
    int dir;
    uint16_t val;
    qemu_irq dat_out;

    int addr;
    int reg;
    int rw;
    enum {
        cbus_address,
        cbus_value,
    } cycle;

    CBusSlave *slave[8];
} CBusPriv;

static void cbus_io(CBusPriv *s)
{
    if (s->slave[s->addr])
        s->slave[s->addr]->io(s->slave[s->addr]->opaque,
                        s->rw, s->reg, &s->val);
    else
        hw_error("%s: bad slave address %i\n", __FUNCTION__, s->addr);
}

static void cbus_cycle(CBusPriv *s)
{
    switch (s->cycle) {
    case cbus_address:
        s->addr = (s->val >> 6) & 7;
        s->rw =   (s->val >> 5) & 1;
        s->reg =  (s->val >> 0) & 0x1f;

        s->cycle = cbus_value;
        s->bit = 15;
        s->dir = !s->rw;
        s->val = 0;

        if (s->rw)
            cbus_io(s);
        break;

    case cbus_value:
        if (!s->rw)
            cbus_io(s);

        s->cycle = cbus_address;
        s->bit = 8;
        s->dir = 1;
        s->val = 0;
        break;
    }
}

static void cbus_clk(void *opaque, int line, int level)
{
    CBusPriv *s = (CBusPriv *) opaque;

    if (!s->sel && level && !s->clk) {
        if (s->dir)
            s->val |= s->dat << (s->bit --);
        else
            qemu_set_irq(s->dat_out, (s->val >> (s->bit --)) & 1);

        if (s->bit < 0)
            cbus_cycle(s);
    }

    s->clk = level;
}

static void cbus_dat(void *opaque, int line, int level)
{
    CBusPriv *s = (CBusPriv *) opaque;

    s->dat = level;
}

static void cbus_sel(void *opaque, int line, int level)
{
    CBusPriv *s = (CBusPriv *) opaque;

    if (!level) {
        s->dir = 1;
        s->bit = 8;
        s->val = 0;
    }

    s->sel = level;
}

CBus *cbus_init(qemu_irq dat)
{
    CBusPriv *s = (CBusPriv *) qemu_mallocz(sizeof(*s));

    s->dat_out = dat;
    s->cbus.clk = qemu_allocate_irqs(cbus_clk, s, 1)[0];
    s->cbus.dat = qemu_allocate_irqs(cbus_dat, s, 1)[0];
    s->cbus.sel = qemu_allocate_irqs(cbus_sel, s, 1)[0];

    s->sel = 1;
    s->clk = 0;
    s->dat = 0;

    return &s->cbus;
}

void cbus_attach(CBus *bus, void *slave_opaque)
{
    CBusSlave *slave = (CBusSlave *) slave_opaque;
    CBusPriv *s = (CBusPriv *) bus;

    s->slave[slave->addr] = slave;
}

/* Retu/Vilma */
typedef struct {
    uint16_t irqst;
    uint16_t irqen;
    uint16_t cc[2];
    int channel;
    uint16_t result[16];
    uint16_t sample;
    uint16_t status;

    struct {
        uint16_t cal;
    } rtc;

    int is_vilma;
    qemu_irq irq;
    CBusSlave cbus;
} CBusRetu;

static void retu_interrupt_update(CBusRetu *s)
{
    qemu_set_irq(s->irq, s->irqst & ~s->irqen);
}

#define RETU_REG_ASICR		0x00	/* (RO) ASIC ID & revision */
#define RETU_REG_IDR		0x01	/* (T)  Interrupt ID */
#define RETU_REG_IMR		0x02	/* (RW) Interrupt mask */
#define RETU_REG_RTCDSR		0x03	/* (RW) RTC seconds register */
#define RETU_REG_RTCHMR		0x04	/* (RO) RTC hours and minutes reg */
#define RETU_REG_RTCHMAR	0x05	/* (RW) RTC hours and minutes set reg */
#define RETU_REG_RTCCALR	0x06	/* (RW) RTC calibration register */
#define RETU_REG_ADCR		0x08	/* (RW) ADC result register */
#define RETU_REG_ADCSCR		0x09	/* (RW) ADC sample control register */
#define RETU_REG_AFCR		0x0a	/* (RW) AFC register */
#define RETU_REG_ANTIFR		0x0b	/* (RW) AntiF register */
#define RETU_REG_CALIBR		0x0c	/* (RW) CalibR register*/
#define RETU_REG_CCR1		0x0d	/* (RW) Common control register 1 */
#define RETU_REG_CCR2		0x0e	/* (RW) Common control register 2 */
#define RETU_REG_RCTRL_CLR	0x0f	/* (T)  Regulator clear register */
#define RETU_REG_RCTRL_SET	0x10	/* (T)  Regulator set register */
#define RETU_REG_TXCR		0x11	/* (RW) TxC register */
#define RETU_REG_STATUS		0x16	/* (RO) Status register */
#define RETU_REG_WATCHDOG	0x17	/* (RW) Watchdog register */
#define RETU_REG_AUDTXR		0x18	/* (RW) Audio Codec Tx register */
#define RETU_REG_AUDPAR		0x19	/* (RW) AudioPA register */
#define RETU_REG_AUDRXR1	0x1a	/* (RW) Audio receive register 1 */
#define RETU_REG_AUDRXR2	0x1b	/* (RW) Audio receive register 2 */
#define RETU_REG_SGR1		0x1c	/* (RW) */
#define RETU_REG_SCR1		0x1d	/* (RW) */
#define RETU_REG_SGR2		0x1e	/* (RW) */
#define RETU_REG_SCR2		0x1f	/* (RW) */

/* Retu Interrupt sources */
enum {
    retu_int_pwr	= 0,	/* Power button */
    retu_int_char	= 1,	/* Charger */
    retu_int_rtcs	= 2,	/* Seconds */
    retu_int_rtcm	= 3,	/* Minutes */
    retu_int_rtcd	= 4,	/* Days */
    retu_int_rtca	= 5,	/* Alarm */
    retu_int_hook	= 6,	/* Hook */
    retu_int_head	= 7,	/* Headset */
    retu_int_adcs	= 8,	/* ADC sample */
};

/* Retu ADC channel wiring */
enum {
    retu_adc_bsi	= 1,	/* BSI */
    retu_adc_batt_temp	= 2,	/* Battery temperature */
    retu_adc_chg_volt	= 3,	/* Charger voltage */
    retu_adc_head_det	= 4,	/* Headset detection */
    retu_adc_hook_det	= 5,	/* Hook detection */
    retu_adc_rf_gp	= 6,	/* RF GP */
    retu_adc_tx_det	= 7,	/* Wideband Tx detection */
    retu_adc_batt_volt	= 8,	/* Battery voltage */
    retu_adc_sens	= 10,	/* Light sensor */
    retu_adc_sens_temp	= 11,	/* Light sensor temperature */
    retu_adc_bbatt_volt	= 12,	/* Backup battery voltage */
    retu_adc_self_temp	= 13,	/* RETU temperature */
};

static inline uint16_t retu_read(CBusRetu *s, int reg)
{
#ifdef DEBUG
    printf("RETU read at %02x\n", reg);
#endif

    switch (reg) {
    case RETU_REG_ASICR:
        return 0x0215 | (s->is_vilma << 7);

    case RETU_REG_IDR:	/* TODO: Or is this ffs(s->irqst)?  */
        return s->irqst;

    case RETU_REG_IMR:
        return s->irqen;

    case RETU_REG_RTCDSR:
    case RETU_REG_RTCHMR:
    case RETU_REG_RTCHMAR:
        /* TODO */
        return 0x0000;

    case RETU_REG_RTCCALR:
        return s->rtc.cal;

    case RETU_REG_ADCR:
        return (s->channel << 10) | s->result[s->channel];
    case RETU_REG_ADCSCR:
        return s->sample;

    case RETU_REG_AFCR:
    case RETU_REG_ANTIFR:
    case RETU_REG_CALIBR:
        /* TODO */
        return 0x0000;

    case RETU_REG_CCR1:
        return s->cc[0];
    case RETU_REG_CCR2:
        return s->cc[1];

    case RETU_REG_RCTRL_CLR:
    case RETU_REG_RCTRL_SET:
    case RETU_REG_TXCR:
        /* TODO */
        return 0x0000;

    case RETU_REG_STATUS:
        return s->status;

    case RETU_REG_WATCHDOG:
    case RETU_REG_AUDTXR:
    case RETU_REG_AUDPAR:
    case RETU_REG_AUDRXR1:
    case RETU_REG_AUDRXR2:
    case RETU_REG_SGR1:
    case RETU_REG_SCR1:
    case RETU_REG_SGR2:
    case RETU_REG_SCR2:
        /* TODO */
        return 0x0000;

    default:
        hw_error("%s: bad register %02x\n", __FUNCTION__, reg);
    }
}

static inline void retu_write(CBusRetu *s, int reg, uint16_t val)
{
#ifdef DEBUG
    printf("RETU write of %04x at %02x\n", val, reg);
#endif

    switch (reg) {
    case RETU_REG_IDR:
        s->irqst ^= val;
        retu_interrupt_update(s);
        break;

    case RETU_REG_IMR:
        s->irqen = val;
        retu_interrupt_update(s);
        break;

    case RETU_REG_RTCDSR:
    case RETU_REG_RTCHMAR:
        /* TODO */
        break;

    case RETU_REG_RTCCALR:
        s->rtc.cal = val;
        break;

    case RETU_REG_ADCR:
        s->channel = (val >> 10) & 0xf;
        s->irqst |= 1 << retu_int_adcs;
        retu_interrupt_update(s);
        break;
    case RETU_REG_ADCSCR:
        s->sample &= ~val;
        break;

    case RETU_REG_AFCR:
    case RETU_REG_ANTIFR:
    case RETU_REG_CALIBR:

    case RETU_REG_CCR1:
        s->cc[0] = val;
        break;
    case RETU_REG_CCR2:
        s->cc[1] = val;
        break;

    case RETU_REG_RCTRL_CLR:
    case RETU_REG_RCTRL_SET:
        /* TODO */
        break;

    case RETU_REG_WATCHDOG:
        if (val == 0 && (s->cc[0] & 2))
            qemu_system_shutdown_request();
        break;

    case RETU_REG_TXCR:
    case RETU_REG_AUDTXR:
    case RETU_REG_AUDPAR:
    case RETU_REG_AUDRXR1:
    case RETU_REG_AUDRXR2:
    case RETU_REG_SGR1:
    case RETU_REG_SCR1:
    case RETU_REG_SGR2:
    case RETU_REG_SCR2:
        /* TODO */
        break;

    default:
        hw_error("%s: bad register %02x\n", __FUNCTION__, reg);
    }
}

static void retu_io(void *opaque, int rw, int reg, uint16_t *val)
{
    CBusRetu *s = (CBusRetu *) opaque;

    if (rw)
        *val = retu_read(s, reg);
    else
        retu_write(s, reg, *val);
}

void *retu_init(qemu_irq irq, int vilma)
{
    CBusRetu *s = (CBusRetu *) qemu_mallocz(sizeof(*s));

    s->irq = irq;
    s->irqen = 0xffff;
    s->irqst = 0x0000;
    s->status = 0x0020;
    s->is_vilma = !!vilma;
    s->rtc.cal = 0x01;
    s->result[retu_adc_bsi] = 0x3c2;
    s->result[retu_adc_batt_temp] = 0x0fc;
    s->result[retu_adc_chg_volt] = 0x165;
    s->result[retu_adc_head_det] = 123;
    s->result[retu_adc_hook_det] = 1023;
    s->result[retu_adc_rf_gp] = 0x11;
    s->result[retu_adc_tx_det] = 0x11;
    s->result[retu_adc_batt_volt] = 0x250;
    s->result[retu_adc_sens] = 2;
    s->result[retu_adc_sens_temp] = 0x11;
    s->result[retu_adc_bbatt_volt] = 0x3d0;
    s->result[retu_adc_self_temp] = 0x330;

    s->cbus.opaque = s;
    s->cbus.io = retu_io;
    s->cbus.addr = 1;

    return &s->cbus;
}

void retu_key_event(void *retu, int state)
{
    CBusSlave *slave = (CBusSlave *) retu;
    CBusRetu *s = (CBusRetu *) slave->opaque;

    s->irqst |= 1 << retu_int_pwr;
    retu_interrupt_update(s);

    if (state)
        s->status &= ~(1 << 5);
    else
        s->status |= 1 << 5;
}

#if 0
static void retu_head_event(void *retu, int state)
{
    CBusSlave *slave = (CBusSlave *) retu;
    CBusRetu *s = (CBusRetu *) slave->opaque;

    if ((s->cc[0] & 0x500) == 0x500) {	/* TODO: Which bits? */
        /* TODO: reissue the interrupt every 100ms or so.  */
        s->irqst |= 1 << retu_int_head;
        retu_interrupt_update(s);
    }

    if (state)
        s->result[retu_adc_head_det] = 50;
    else
        s->result[retu_adc_head_det] = 123;
}

static void retu_hook_event(void *retu, int state)
{
    CBusSlave *slave = (CBusSlave *) retu;
    CBusRetu *s = (CBusRetu *) slave->opaque;

    if ((s->cc[0] & 0x500) == 0x500) {
        /* TODO: reissue the interrupt every 100ms or so.  */
        s->irqst |= 1 << retu_int_hook;
        retu_interrupt_update(s);
    }

    if (state)
        s->result[retu_adc_hook_det] = 50;
    else
        s->result[retu_adc_hook_det] = 123;
}
#endif

/* Tahvo/Betty */
typedef struct {
    uint16_t irqst;
    uint16_t irqen;
    uint8_t charger;
    uint8_t backlight;
    uint16_t usbr;
    uint16_t power;

    int is_betty;
    qemu_irq irq;
    CBusSlave cbus;
} CBusTahvo;

static void tahvo_interrupt_update(CBusTahvo *s)
{
    qemu_set_irq(s->irq, s->irqst & ~s->irqen);
}

#define TAHVO_REG_ASICR		0x00	/* (RO) ASIC ID & revision */
#define TAHVO_REG_IDR		0x01	/* (T)  Interrupt ID */
#define TAHVO_REG_IDSR		0x02	/* (RO) Interrupt status */
#define TAHVO_REG_IMR		0x03	/* (RW) Interrupt mask */
#define TAHVO_REG_CHAPWMR	0x04	/* (RW) Charger PWM */
#define TAHVO_REG_LEDPWMR	0x05	/* (RW) LED PWM */
#define TAHVO_REG_USBR		0x06	/* (RW) USB control */
#define TAHVO_REG_RCR		0x07	/* (RW) Some kind of power management */
#define TAHVO_REG_CCR1		0x08	/* (RW) Common control register 1 */
#define TAHVO_REG_CCR2		0x09	/* (RW) Common control register 2 */
#define TAHVO_REG_TESTR1	0x0a	/* (RW) Test register 1 */
#define TAHVO_REG_TESTR2	0x0b	/* (RW) Test register 2 */
#define TAHVO_REG_NOPR		0x0c	/* (RW) Number of periods */
#define TAHVO_REG_FRR		0x0d	/* (RO) FR */

static inline uint16_t tahvo_read(CBusTahvo *s, int reg)
{
#ifdef DEBUG
    printf("TAHVO read at %02x\n", reg);
#endif

    switch (reg) {
    case TAHVO_REG_ASICR:
        return 0x0021 | (s->is_betty ? 0x0b00 : 0x0300);	/* 22 in N810 */

    case TAHVO_REG_IDR:
    case TAHVO_REG_IDSR:	/* XXX: what does this do?  */
        return s->irqst;

    case TAHVO_REG_IMR:
        return s->irqen;

    case TAHVO_REG_CHAPWMR:
        return s->charger;

    case TAHVO_REG_LEDPWMR:
        return s->backlight;

    case TAHVO_REG_USBR:
        return s->usbr;

    case TAHVO_REG_RCR:
        return s->power;

    case TAHVO_REG_CCR1:
    case TAHVO_REG_CCR2:
    case TAHVO_REG_TESTR1:
    case TAHVO_REG_TESTR2:
    case TAHVO_REG_NOPR:
    case TAHVO_REG_FRR:
        return 0x0000;

    default:
        hw_error("%s: bad register %02x\n", __FUNCTION__, reg);
    }
}

static inline void tahvo_write(CBusTahvo *s, int reg, uint16_t val)
{
#ifdef DEBUG
    printf("TAHVO write of %04x at %02x\n", val, reg);
#endif

    switch (reg) {
    case TAHVO_REG_IDR:
        s->irqst ^= val;
        tahvo_interrupt_update(s);
        break;

    case TAHVO_REG_IMR:
        s->irqen = val;
        tahvo_interrupt_update(s);
        break;

    case TAHVO_REG_CHAPWMR:
        s->charger = val;
        break;

    case TAHVO_REG_LEDPWMR:
        if (s->backlight != (val & 0x7f)) {
            s->backlight = val & 0x7f;
            printf("%s: LCD backlight now at %i / 127\n",
                            __FUNCTION__, s->backlight);
        }
        break;

    case TAHVO_REG_USBR:
        s->usbr = val;
        break;

    case TAHVO_REG_RCR:
        s->power = val;
        break;

    case TAHVO_REG_CCR1:
    case TAHVO_REG_CCR2:
    case TAHVO_REG_TESTR1:
    case TAHVO_REG_TESTR2:
    case TAHVO_REG_NOPR:
    case TAHVO_REG_FRR:
        break;

    default:
        hw_error("%s: bad register %02x\n", __FUNCTION__, reg);
    }
}

static void tahvo_io(void *opaque, int rw, int reg, uint16_t *val)
{
    CBusTahvo *s = (CBusTahvo *) opaque;

    if (rw)
        *val = tahvo_read(s, reg);
    else
        tahvo_write(s, reg, *val);
}

void *tahvo_init(qemu_irq irq, int betty)
{
    CBusTahvo *s = (CBusTahvo *) qemu_mallocz(sizeof(*s));

    s->irq = irq;
    s->irqen = 0xffff;
    s->irqst = 0x0000;
    s->is_betty = !!betty;

    s->cbus.opaque = s;
    s->cbus.io = tahvo_io;
    s->cbus.addr = 2;

    return &s->cbus;
}
