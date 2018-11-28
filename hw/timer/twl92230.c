/*
 * TI TWL92230C energy-management companion device for the OMAP24xx.
 * Aka. Menelaus (N4200 MENELAUS1_V2.2)
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
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "qemu/timer.h"
#include "hw/i2c/i2c.h"
#include "sysemu/sysemu.h"
#include "ui/console.h"
#include "qemu/bcd.h"

#define VERBOSE 1

#define TYPE_TWL92230 "twl92230"
#define TWL92230(obj) OBJECT_CHECK(MenelausState, (obj), TYPE_TWL92230)

typedef struct MenelausState {
    I2CSlave parent_obj;

    int firstbyte;
    uint8_t reg;

    uint8_t vcore[5];
    uint8_t dcdc[3];
    uint8_t ldo[8];
    uint8_t sleep[2];
    uint8_t osc;
    uint8_t detect;
    uint16_t mask;
    uint16_t status;
    uint8_t dir;
    uint8_t inputs;
    uint8_t outputs;
    uint8_t bbsms;
    uint8_t pull[4];
    uint8_t mmc_ctrl[3];
    uint8_t mmc_debounce;
    struct {
        uint8_t ctrl;
        uint16_t comp;
        QEMUTimer *hz_tm;
        int64_t next;
        struct tm tm;
        struct tm new;
        struct tm alm;
        int sec_offset;
        int alm_sec;
        int next_comp;
    } rtc;
    uint16_t rtc_next_vmstate;
    qemu_irq out[4];
    uint8_t pwrbtn_state;
} MenelausState;

static inline void menelaus_update(MenelausState *s)
{
    qemu_set_irq(s->out[3], s->status & ~s->mask);
}

static inline void menelaus_rtc_start(MenelausState *s)
{
    s->rtc.next += qemu_clock_get_ms(rtc_clock);
    timer_mod(s->rtc.hz_tm, s->rtc.next);
}

static inline void menelaus_rtc_stop(MenelausState *s)
{
    timer_del(s->rtc.hz_tm);
    s->rtc.next -= qemu_clock_get_ms(rtc_clock);
    if (s->rtc.next < 1)
        s->rtc.next = 1;
}

static void menelaus_rtc_update(MenelausState *s)
{
    qemu_get_timedate(&s->rtc.tm, s->rtc.sec_offset);
}

static void menelaus_alm_update(MenelausState *s)
{
    if ((s->rtc.ctrl & 3) == 3)
        s->rtc.alm_sec = qemu_timedate_diff(&s->rtc.alm) - s->rtc.sec_offset;
}

static void menelaus_rtc_hz(void *opaque)
{
    MenelausState *s = (MenelausState *) opaque;

    s->rtc.next_comp --;
    s->rtc.alm_sec --;
    s->rtc.next += 1000;
    timer_mod(s->rtc.hz_tm, s->rtc.next);
    if ((s->rtc.ctrl >> 3) & 3) {				/* EVERY */
        menelaus_rtc_update(s);
        if (((s->rtc.ctrl >> 3) & 3) == 1 && !s->rtc.tm.tm_sec)
            s->status |= 1 << 8;				/* RTCTMR */
        else if (((s->rtc.ctrl >> 3) & 3) == 2 && !s->rtc.tm.tm_min)
            s->status |= 1 << 8;				/* RTCTMR */
        else if (!s->rtc.tm.tm_hour)
            s->status |= 1 << 8;				/* RTCTMR */
    } else
        s->status |= 1 << 8;					/* RTCTMR */
    if ((s->rtc.ctrl >> 1) & 1) {				/* RTC_AL_EN */
        if (s->rtc.alm_sec == 0)
            s->status |= 1 << 9;				/* RTCALM */
        /* TODO: wake-up */
    }
    if (s->rtc.next_comp <= 0) {
        s->rtc.next -= muldiv64((int16_t) s->rtc.comp, 1000, 0x8000);
        s->rtc.next_comp = 3600;
    }
    menelaus_update(s);
}

static void menelaus_reset(I2CSlave *i2c)
{
    MenelausState *s = TWL92230(i2c);

    s->reg = 0x00;

    s->vcore[0] = 0x0c;	/* XXX: X-loader needs 0x8c? check!  */
    s->vcore[1] = 0x05;
    s->vcore[2] = 0x02;
    s->vcore[3] = 0x0c;
    s->vcore[4] = 0x03;
    s->dcdc[0] = 0x33;	/* Depends on wiring */
    s->dcdc[1] = 0x03;
    s->dcdc[2] = 0x00;
    s->ldo[0] = 0x95;
    s->ldo[1] = 0x7e;
    s->ldo[2] = 0x00;
    s->ldo[3] = 0x00;	/* Depends on wiring */
    s->ldo[4] = 0x03;	/* Depends on wiring */
    s->ldo[5] = 0x00;
    s->ldo[6] = 0x00;
    s->ldo[7] = 0x00;
    s->sleep[0] = 0x00;
    s->sleep[1] = 0x00;
    s->osc = 0x01;
    s->detect = 0x09;
    s->mask = 0x0fff;
    s->status = 0;
    s->dir = 0x07;
    s->outputs = 0x00;
    s->bbsms = 0x00;
    s->pull[0] = 0x00;
    s->pull[1] = 0x00;
    s->pull[2] = 0x00;
    s->pull[3] = 0x00;
    s->mmc_ctrl[0] = 0x03;
    s->mmc_ctrl[1] = 0xc0;
    s->mmc_ctrl[2] = 0x00;
    s->mmc_debounce = 0x05;

    if (s->rtc.ctrl & 1)
        menelaus_rtc_stop(s);
    s->rtc.ctrl = 0x00;
    s->rtc.comp = 0x0000;
    s->rtc.next = 1000;
    s->rtc.sec_offset = 0;
    s->rtc.next_comp = 1800;
    s->rtc.alm_sec = 1800;
    s->rtc.alm.tm_sec = 0x00;
    s->rtc.alm.tm_min = 0x00;
    s->rtc.alm.tm_hour = 0x00;
    s->rtc.alm.tm_mday = 0x01;
    s->rtc.alm.tm_mon = 0x00;
    s->rtc.alm.tm_year = 2004;
    menelaus_update(s);
}

static void menelaus_gpio_set(void *opaque, int line, int level)
{
    MenelausState *s = (MenelausState *) opaque;

    if (line < 3) {
        /* No interrupt generated */
        s->inputs &= ~(1 << line);
        s->inputs |= level << line;
        return;
    }

    if (!s->pwrbtn_state && level) {
        s->status |= 1 << 11;					/* PSHBTN */
        menelaus_update(s);
    }
    s->pwrbtn_state = level;
}

#define MENELAUS_REV		0x01
#define MENELAUS_VCORE_CTRL1	0x02
#define MENELAUS_VCORE_CTRL2	0x03
#define MENELAUS_VCORE_CTRL3	0x04
#define MENELAUS_VCORE_CTRL4	0x05
#define MENELAUS_VCORE_CTRL5	0x06
#define MENELAUS_DCDC_CTRL1	0x07
#define MENELAUS_DCDC_CTRL2	0x08
#define MENELAUS_DCDC_CTRL3	0x09
#define MENELAUS_LDO_CTRL1	0x0a
#define MENELAUS_LDO_CTRL2	0x0b
#define MENELAUS_LDO_CTRL3	0x0c
#define MENELAUS_LDO_CTRL4	0x0d
#define MENELAUS_LDO_CTRL5	0x0e
#define MENELAUS_LDO_CTRL6	0x0f
#define MENELAUS_LDO_CTRL7	0x10
#define MENELAUS_LDO_CTRL8	0x11
#define MENELAUS_SLEEP_CTRL1	0x12
#define MENELAUS_SLEEP_CTRL2	0x13
#define MENELAUS_DEVICE_OFF	0x14
#define MENELAUS_OSC_CTRL	0x15
#define MENELAUS_DETECT_CTRL	0x16
#define MENELAUS_INT_MASK1	0x17
#define MENELAUS_INT_MASK2	0x18
#define MENELAUS_INT_STATUS1	0x19
#define MENELAUS_INT_STATUS2	0x1a
#define MENELAUS_INT_ACK1	0x1b
#define MENELAUS_INT_ACK2	0x1c
#define MENELAUS_GPIO_CTRL	0x1d
#define MENELAUS_GPIO_IN	0x1e
#define MENELAUS_GPIO_OUT	0x1f
#define MENELAUS_BBSMS		0x20
#define MENELAUS_RTC_CTRL	0x21
#define MENELAUS_RTC_UPDATE	0x22
#define MENELAUS_RTC_SEC	0x23
#define MENELAUS_RTC_MIN	0x24
#define MENELAUS_RTC_HR		0x25
#define MENELAUS_RTC_DAY	0x26
#define MENELAUS_RTC_MON	0x27
#define MENELAUS_RTC_YR		0x28
#define MENELAUS_RTC_WKDAY	0x29
#define MENELAUS_RTC_AL_SEC	0x2a
#define MENELAUS_RTC_AL_MIN	0x2b
#define MENELAUS_RTC_AL_HR	0x2c
#define MENELAUS_RTC_AL_DAY	0x2d
#define MENELAUS_RTC_AL_MON	0x2e
#define MENELAUS_RTC_AL_YR	0x2f
#define MENELAUS_RTC_COMP_MSB	0x30
#define MENELAUS_RTC_COMP_LSB	0x31
#define MENELAUS_S1_PULL_EN	0x32
#define MENELAUS_S1_PULL_DIR	0x33
#define MENELAUS_S2_PULL_EN	0x34
#define MENELAUS_S2_PULL_DIR	0x35
#define MENELAUS_MCT_CTRL1	0x36
#define MENELAUS_MCT_CTRL2	0x37
#define MENELAUS_MCT_CTRL3	0x38
#define MENELAUS_MCT_PIN_ST	0x39
#define MENELAUS_DEBOUNCE1	0x3a

static uint8_t menelaus_read(void *opaque, uint8_t addr)
{
    MenelausState *s = (MenelausState *) opaque;
    int reg = 0;

    switch (addr) {
    case MENELAUS_REV:
        return 0x22;

    case MENELAUS_VCORE_CTRL5: reg ++;
    case MENELAUS_VCORE_CTRL4: reg ++;
    case MENELAUS_VCORE_CTRL3: reg ++;
    case MENELAUS_VCORE_CTRL2: reg ++;
    case MENELAUS_VCORE_CTRL1:
        return s->vcore[reg];

    case MENELAUS_DCDC_CTRL3: reg ++;
    case MENELAUS_DCDC_CTRL2: reg ++;
    case MENELAUS_DCDC_CTRL1:
        return s->dcdc[reg];

    case MENELAUS_LDO_CTRL8: reg ++;
    case MENELAUS_LDO_CTRL7: reg ++;
    case MENELAUS_LDO_CTRL6: reg ++;
    case MENELAUS_LDO_CTRL5: reg ++;
    case MENELAUS_LDO_CTRL4: reg ++;
    case MENELAUS_LDO_CTRL3: reg ++;
    case MENELAUS_LDO_CTRL2: reg ++;
    case MENELAUS_LDO_CTRL1:
        return s->ldo[reg];

    case MENELAUS_SLEEP_CTRL2: reg ++;
    case MENELAUS_SLEEP_CTRL1:
        return s->sleep[reg];

    case MENELAUS_DEVICE_OFF:
        return 0;

    case MENELAUS_OSC_CTRL:
        return s->osc | (1 << 7);			/* CLK32K_GOOD */

    case MENELAUS_DETECT_CTRL:
        return s->detect;

    case MENELAUS_INT_MASK1:
        return (s->mask >> 0) & 0xff;
    case MENELAUS_INT_MASK2:
        return (s->mask >> 8) & 0xff;

    case MENELAUS_INT_STATUS1:
        return (s->status >> 0) & 0xff;
    case MENELAUS_INT_STATUS2:
        return (s->status >> 8) & 0xff;

    case MENELAUS_INT_ACK1:
    case MENELAUS_INT_ACK2:
        return 0;

    case MENELAUS_GPIO_CTRL:
        return s->dir;
    case MENELAUS_GPIO_IN:
        return s->inputs | (~s->dir & s->outputs);
    case MENELAUS_GPIO_OUT:
        return s->outputs;

    case MENELAUS_BBSMS:
        return s->bbsms;

    case MENELAUS_RTC_CTRL:
        return s->rtc.ctrl;
    case MENELAUS_RTC_UPDATE:
        return 0x00;
    case MENELAUS_RTC_SEC:
        menelaus_rtc_update(s);
        return to_bcd(s->rtc.tm.tm_sec);
    case MENELAUS_RTC_MIN:
        menelaus_rtc_update(s);
        return to_bcd(s->rtc.tm.tm_min);
    case MENELAUS_RTC_HR:
        menelaus_rtc_update(s);
        if ((s->rtc.ctrl >> 2) & 1)			/* MODE12_n24 */
            return to_bcd((s->rtc.tm.tm_hour % 12) + 1) |
                    (!!(s->rtc.tm.tm_hour >= 12) << 7);	/* PM_nAM */
        else
            return to_bcd(s->rtc.tm.tm_hour);
    case MENELAUS_RTC_DAY:
        menelaus_rtc_update(s);
        return to_bcd(s->rtc.tm.tm_mday);
    case MENELAUS_RTC_MON:
        menelaus_rtc_update(s);
        return to_bcd(s->rtc.tm.tm_mon + 1);
    case MENELAUS_RTC_YR:
        menelaus_rtc_update(s);
        return to_bcd(s->rtc.tm.tm_year - 2000);
    case MENELAUS_RTC_WKDAY:
        menelaus_rtc_update(s);
        return to_bcd(s->rtc.tm.tm_wday);
    case MENELAUS_RTC_AL_SEC:
        return to_bcd(s->rtc.alm.tm_sec);
    case MENELAUS_RTC_AL_MIN:
        return to_bcd(s->rtc.alm.tm_min);
    case MENELAUS_RTC_AL_HR:
        if ((s->rtc.ctrl >> 2) & 1)			/* MODE12_n24 */
            return to_bcd((s->rtc.alm.tm_hour % 12) + 1) |
                    (!!(s->rtc.alm.tm_hour >= 12) << 7);/* AL_PM_nAM */
        else
            return to_bcd(s->rtc.alm.tm_hour);
    case MENELAUS_RTC_AL_DAY:
        return to_bcd(s->rtc.alm.tm_mday);
    case MENELAUS_RTC_AL_MON:
        return to_bcd(s->rtc.alm.tm_mon + 1);
    case MENELAUS_RTC_AL_YR:
        return to_bcd(s->rtc.alm.tm_year - 2000);
    case MENELAUS_RTC_COMP_MSB:
        return (s->rtc.comp >> 8) & 0xff;
    case MENELAUS_RTC_COMP_LSB:
        return (s->rtc.comp >> 0) & 0xff;

    case MENELAUS_S1_PULL_EN:
        return s->pull[0];
    case MENELAUS_S1_PULL_DIR:
        return s->pull[1];
    case MENELAUS_S2_PULL_EN:
        return s->pull[2];
    case MENELAUS_S2_PULL_DIR:
        return s->pull[3];

    case MENELAUS_MCT_CTRL3: reg ++;
    case MENELAUS_MCT_CTRL2: reg ++;
    case MENELAUS_MCT_CTRL1:
        return s->mmc_ctrl[reg];
    case MENELAUS_MCT_PIN_ST:
        /* TODO: return the real Card Detect */
        return 0;
    case MENELAUS_DEBOUNCE1:
        return s->mmc_debounce;

    default:
#ifdef VERBOSE
        printf("%s: unknown register %02x\n", __func__, addr);
#endif
        break;
    }
    return 0;
}

static void menelaus_write(void *opaque, uint8_t addr, uint8_t value)
{
    MenelausState *s = (MenelausState *) opaque;
    int line;
    int reg = 0;
    struct tm tm;

    switch (addr) {
    case MENELAUS_VCORE_CTRL1:
        s->vcore[0] = (value & 0xe) | MIN(value & 0x1f, 0x12);
        break;
    case MENELAUS_VCORE_CTRL2:
        s->vcore[1] = value;
        break;
    case MENELAUS_VCORE_CTRL3:
        s->vcore[2] = MIN(value & 0x1f, 0x12);
        break;
    case MENELAUS_VCORE_CTRL4:
        s->vcore[3] = MIN(value & 0x1f, 0x12);
        break;
    case MENELAUS_VCORE_CTRL5:
        s->vcore[4] = value & 3;
        /* XXX
         * auto set to 3 on M_Active, nRESWARM
         * auto set to 0 on M_WaitOn, M_Backup
         */
        break;

    case MENELAUS_DCDC_CTRL1:
        s->dcdc[0] = value & 0x3f;
        break;
    case MENELAUS_DCDC_CTRL2:
        s->dcdc[1] = value & 0x07;
        /* XXX
         * auto set to 3 on M_Active, nRESWARM
         * auto set to 0 on M_WaitOn, M_Backup
         */
        break;
    case MENELAUS_DCDC_CTRL3:
        s->dcdc[2] = value & 0x07;
        break;

    case MENELAUS_LDO_CTRL1:
        s->ldo[0] = value;
        break;
    case MENELAUS_LDO_CTRL2:
        s->ldo[1] = value & 0x7f;
        /* XXX
         * auto set to 0x7e on M_WaitOn, M_Backup
         */
        break;
    case MENELAUS_LDO_CTRL3:
        s->ldo[2] = value & 3;
        /* XXX
         * auto set to 3 on M_Active, nRESWARM
         * auto set to 0 on M_WaitOn, M_Backup
         */
        break;
    case MENELAUS_LDO_CTRL4:
        s->ldo[3] = value & 3;
        /* XXX
         * auto set to 3 on M_Active, nRESWARM
         * auto set to 0 on M_WaitOn, M_Backup
         */
        break;
    case MENELAUS_LDO_CTRL5:
        s->ldo[4] = value & 3;
        /* XXX
         * auto set to 3 on M_Active, nRESWARM
         * auto set to 0 on M_WaitOn, M_Backup
         */
        break;
    case MENELAUS_LDO_CTRL6:
        s->ldo[5] = value & 3;
        break;
    case MENELAUS_LDO_CTRL7:
        s->ldo[6] = value & 3;
        break;
    case MENELAUS_LDO_CTRL8:
        s->ldo[7] = value & 3;
        break;

    case MENELAUS_SLEEP_CTRL2: reg ++;
    case MENELAUS_SLEEP_CTRL1:
        s->sleep[reg] = value;
        break;

    case MENELAUS_DEVICE_OFF:
        if (value & 1) {
            menelaus_reset(I2C_SLAVE(s));
        }
        break;

    case MENELAUS_OSC_CTRL:
        s->osc = value & 7;
        break;

    case MENELAUS_DETECT_CTRL:
        s->detect = value & 0x7f;
        break;

    case MENELAUS_INT_MASK1:
        s->mask &= 0xf00;
        s->mask |= value << 0;
        menelaus_update(s);
        break;
    case MENELAUS_INT_MASK2:
        s->mask &= 0x0ff;
        s->mask |= value << 8;
        menelaus_update(s);
        break;

    case MENELAUS_INT_ACK1:
        s->status &= ~(((uint16_t) value) << 0);
        menelaus_update(s);
        break;
    case MENELAUS_INT_ACK2:
        s->status &= ~(((uint16_t) value) << 8);
        menelaus_update(s);
        break;

    case MENELAUS_GPIO_CTRL:
        for (line = 0; line < 3; line ++) {
            if (((s->dir ^ value) >> line) & 1) {
                qemu_set_irq(s->out[line],
                             ((s->outputs & ~s->dir) >> line) & 1);
            }
        }
        s->dir = value & 0x67;
        break;
    case MENELAUS_GPIO_OUT:
        for (line = 0; line < 3; line ++) {
            if ((((s->outputs ^ value) & ~s->dir) >> line) & 1) {
                qemu_set_irq(s->out[line], (s->outputs >> line) & 1);
            }
        }
        s->outputs = value & 0x07;
        break;

    case MENELAUS_BBSMS:
        s->bbsms = 0x0d;
        break;

    case MENELAUS_RTC_CTRL:
        if ((s->rtc.ctrl ^ value) & 1) {			/* RTC_EN */
            if (value & 1)
                menelaus_rtc_start(s);
            else
                menelaus_rtc_stop(s);
        }
        s->rtc.ctrl = value & 0x1f;
        menelaus_alm_update(s);
        break;
    case MENELAUS_RTC_UPDATE:
        menelaus_rtc_update(s);
        memcpy(&tm, &s->rtc.tm, sizeof(tm));
        switch (value & 0xf) {
        case 0:
            break;
        case 1:
            tm.tm_sec = s->rtc.new.tm_sec;
            break;
        case 2:
            tm.tm_min = s->rtc.new.tm_min;
            break;
        case 3:
            if (s->rtc.new.tm_hour > 23)
                goto rtc_badness;
            tm.tm_hour = s->rtc.new.tm_hour;
            break;
        case 4:
            if (s->rtc.new.tm_mday < 1)
                goto rtc_badness;
            /* TODO check range */
            tm.tm_mday = s->rtc.new.tm_mday;
            break;
        case 5:
            if (s->rtc.new.tm_mon < 0 || s->rtc.new.tm_mon > 11)
                goto rtc_badness;
            tm.tm_mon = s->rtc.new.tm_mon;
            break;
        case 6:
            tm.tm_year = s->rtc.new.tm_year;
            break;
        case 7:
            /* TODO set .tm_mday instead */
            tm.tm_wday = s->rtc.new.tm_wday;
            break;
        case 8:
            if (s->rtc.new.tm_hour > 23)
                goto rtc_badness;
            if (s->rtc.new.tm_mday < 1)
                goto rtc_badness;
            if (s->rtc.new.tm_mon < 0 || s->rtc.new.tm_mon > 11)
                goto rtc_badness;
            tm.tm_sec = s->rtc.new.tm_sec;
            tm.tm_min = s->rtc.new.tm_min;
            tm.tm_hour = s->rtc.new.tm_hour;
            tm.tm_mday = s->rtc.new.tm_mday;
            tm.tm_mon = s->rtc.new.tm_mon;
            tm.tm_year = s->rtc.new.tm_year;
            break;
        rtc_badness:
        default:
            fprintf(stderr, "%s: bad RTC_UPDATE value %02x\n",
                            __func__, value);
            s->status |= 1 << 10;				/* RTCERR */
            menelaus_update(s);
        }
        s->rtc.sec_offset = qemu_timedate_diff(&tm);
        break;
    case MENELAUS_RTC_SEC:
        s->rtc.tm.tm_sec = from_bcd(value & 0x7f);
        break;
    case MENELAUS_RTC_MIN:
        s->rtc.tm.tm_min = from_bcd(value & 0x7f);
        break;
    case MENELAUS_RTC_HR:
        s->rtc.tm.tm_hour = (s->rtc.ctrl & (1 << 2)) ?	/* MODE12_n24 */
                MIN(from_bcd(value & 0x3f), 12) + ((value >> 7) ? 11 : -1) :
                from_bcd(value & 0x3f);
        break;
    case MENELAUS_RTC_DAY:
        s->rtc.tm.tm_mday = from_bcd(value);
        break;
    case MENELAUS_RTC_MON:
        s->rtc.tm.tm_mon = MAX(1, from_bcd(value)) - 1;
        break;
    case MENELAUS_RTC_YR:
        s->rtc.tm.tm_year = 2000 + from_bcd(value);
        break;
    case MENELAUS_RTC_WKDAY:
        s->rtc.tm.tm_mday = from_bcd(value);
        break;
    case MENELAUS_RTC_AL_SEC:
        s->rtc.alm.tm_sec = from_bcd(value & 0x7f);
        menelaus_alm_update(s);
        break;
    case MENELAUS_RTC_AL_MIN:
        s->rtc.alm.tm_min = from_bcd(value & 0x7f);
        menelaus_alm_update(s);
        break;
    case MENELAUS_RTC_AL_HR:
        s->rtc.alm.tm_hour = (s->rtc.ctrl & (1 << 2)) ?	/* MODE12_n24 */
                MIN(from_bcd(value & 0x3f), 12) + ((value >> 7) ? 11 : -1) :
                from_bcd(value & 0x3f);
        menelaus_alm_update(s);
        break;
    case MENELAUS_RTC_AL_DAY:
        s->rtc.alm.tm_mday = from_bcd(value);
        menelaus_alm_update(s);
        break;
    case MENELAUS_RTC_AL_MON:
        s->rtc.alm.tm_mon = MAX(1, from_bcd(value)) - 1;
        menelaus_alm_update(s);
        break;
    case MENELAUS_RTC_AL_YR:
        s->rtc.alm.tm_year = 2000 + from_bcd(value);
        menelaus_alm_update(s);
        break;
    case MENELAUS_RTC_COMP_MSB:
        s->rtc.comp &= 0xff;
        s->rtc.comp |= value << 8;
        break;
    case MENELAUS_RTC_COMP_LSB:
        s->rtc.comp &= 0xff << 8;
        s->rtc.comp |= value;
        break;

    case MENELAUS_S1_PULL_EN:
        s->pull[0] = value;
        break;
    case MENELAUS_S1_PULL_DIR:
        s->pull[1] = value & 0x1f;
        break;
    case MENELAUS_S2_PULL_EN:
        s->pull[2] = value;
        break;
    case MENELAUS_S2_PULL_DIR:
        s->pull[3] = value & 0x1f;
        break;

    case MENELAUS_MCT_CTRL1:
        s->mmc_ctrl[0] = value & 0x7f;
        break;
    case MENELAUS_MCT_CTRL2:
        s->mmc_ctrl[1] = value;
        /* TODO update Card Detect interrupts */
        break;
    case MENELAUS_MCT_CTRL3:
        s->mmc_ctrl[2] = value & 0xf;
        break;
    case MENELAUS_DEBOUNCE1:
        s->mmc_debounce = value & 0x3f;
        break;

    default:
#ifdef VERBOSE
        printf("%s: unknown register %02x\n", __func__, addr);
#endif
    }
}

static int menelaus_event(I2CSlave *i2c, enum i2c_event event)
{
    MenelausState *s = TWL92230(i2c);

    if (event == I2C_START_SEND)
        s->firstbyte = 1;

    return 0;
}

static int menelaus_tx(I2CSlave *i2c, uint8_t data)
{
    MenelausState *s = TWL92230(i2c);

    /* Interpret register address byte */
    if (s->firstbyte) {
        s->reg = data;
        s->firstbyte = 0;
    } else
        menelaus_write(s, s->reg ++, data);

    return 0;
}

static int menelaus_rx(I2CSlave *i2c)
{
    MenelausState *s = TWL92230(i2c);

    return menelaus_read(s, s->reg ++);
}

/* Save restore 32 bit int as uint16_t
   This is a Big hack, but it is how the old state did it.
   Or we broke compatibility in the state, or we can't use struct tm
 */

static int get_int32_as_uint16(QEMUFile *f, void *pv, size_t size,
                               const VMStateField *field)
{
    int *v = pv;
    *v = qemu_get_be16(f);
    return 0;
}

static int put_int32_as_uint16(QEMUFile *f, void *pv, size_t size,
                               const VMStateField *field, QJSON *vmdesc)
{
    int *v = pv;
    qemu_put_be16(f, *v);

    return 0;
}

static const VMStateInfo vmstate_hack_int32_as_uint16 = {
    .name = "int32_as_uint16",
    .get  = get_int32_as_uint16,
    .put  = put_int32_as_uint16,
};

#define VMSTATE_UINT16_HACK(_f, _s)                                  \
    VMSTATE_SINGLE(_f, _s, 0, vmstate_hack_int32_as_uint16, int32_t)


static const VMStateDescription vmstate_menelaus_tm = {
    .name = "menelaus_tm",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16_HACK(tm_sec, struct tm),
        VMSTATE_UINT16_HACK(tm_min, struct tm),
        VMSTATE_UINT16_HACK(tm_hour, struct tm),
        VMSTATE_UINT16_HACK(tm_mday, struct tm),
        VMSTATE_UINT16_HACK(tm_min, struct tm),
        VMSTATE_UINT16_HACK(tm_year, struct tm),
        VMSTATE_END_OF_LIST()
    }
};

static int menelaus_pre_save(void *opaque)
{
    MenelausState *s = opaque;
    /* Should be <= 1000 */
    s->rtc_next_vmstate =  s->rtc.next - qemu_clock_get_ms(rtc_clock);

    return 0;
}

static int menelaus_post_load(void *opaque, int version_id)
{
    MenelausState *s = opaque;

    if (s->rtc.ctrl & 1)					/* RTC_EN */
        menelaus_rtc_stop(s);

    s->rtc.next = s->rtc_next_vmstate;

    menelaus_alm_update(s);
    menelaus_update(s);
    if (s->rtc.ctrl & 1)					/* RTC_EN */
        menelaus_rtc_start(s);
    return 0;
}

static const VMStateDescription vmstate_menelaus = {
    .name = "menelaus",
    .version_id = 0,
    .minimum_version_id = 0,
    .pre_save = menelaus_pre_save,
    .post_load = menelaus_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(firstbyte, MenelausState),
        VMSTATE_UINT8(reg, MenelausState),
        VMSTATE_UINT8_ARRAY(vcore, MenelausState, 5),
        VMSTATE_UINT8_ARRAY(dcdc, MenelausState, 3),
        VMSTATE_UINT8_ARRAY(ldo, MenelausState, 8),
        VMSTATE_UINT8_ARRAY(sleep, MenelausState, 2),
        VMSTATE_UINT8(osc, MenelausState),
        VMSTATE_UINT8(detect, MenelausState),
        VMSTATE_UINT16(mask, MenelausState),
        VMSTATE_UINT16(status, MenelausState),
        VMSTATE_UINT8(dir, MenelausState),
        VMSTATE_UINT8(inputs, MenelausState),
        VMSTATE_UINT8(outputs, MenelausState),
        VMSTATE_UINT8(bbsms, MenelausState),
        VMSTATE_UINT8_ARRAY(pull, MenelausState, 4),
        VMSTATE_UINT8_ARRAY(mmc_ctrl, MenelausState, 3),
        VMSTATE_UINT8(mmc_debounce, MenelausState),
        VMSTATE_UINT8(rtc.ctrl, MenelausState),
        VMSTATE_UINT16(rtc.comp, MenelausState),
        VMSTATE_UINT16(rtc_next_vmstate, MenelausState),
        VMSTATE_STRUCT(rtc.new, MenelausState, 0, vmstate_menelaus_tm,
                       struct tm),
        VMSTATE_STRUCT(rtc.alm, MenelausState, 0, vmstate_menelaus_tm,
                       struct tm),
        VMSTATE_UINT8(pwrbtn_state, MenelausState),
        VMSTATE_I2C_SLAVE(parent_obj, MenelausState),
        VMSTATE_END_OF_LIST()
    }
};

static void twl92230_realize(DeviceState *dev, Error **errp)
{
    MenelausState *s = TWL92230(dev);

    s->rtc.hz_tm = timer_new_ms(rtc_clock, menelaus_rtc_hz, s);
    /* Three output pins plus one interrupt pin.  */
    qdev_init_gpio_out(dev, s->out, 4);

    /* Three input pins plus one power-button pin.  */
    qdev_init_gpio_in(dev, menelaus_gpio_set, 4);

    menelaus_reset(I2C_SLAVE(dev));
}

static void twl92230_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *sc = I2C_SLAVE_CLASS(klass);

    dc->realize = twl92230_realize;
    sc->event = menelaus_event;
    sc->recv = menelaus_rx;
    sc->send = menelaus_tx;
    dc->vmsd = &vmstate_menelaus;
}

static const TypeInfo twl92230_info = {
    .name          = TYPE_TWL92230,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(MenelausState),
    .class_init    = twl92230_class_init,
};

static void twl92230_register_types(void)
{
    type_register_static(&twl92230_info);
}

type_init(twl92230_register_types)
