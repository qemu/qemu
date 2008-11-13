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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include "hw.h"
#include "qemu-timer.h"
#include "i2c.h"
#include "sysemu.h"
#include "console.h"

#define VERBOSE 1

struct menelaus_s {
    i2c_slave i2c;
    qemu_irq irq;

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
    qemu_irq handler[3];
    qemu_irq *in;
    int pwrbtn_state;
    qemu_irq pwrbtn;
};

static inline void menelaus_update(struct menelaus_s *s)
{
    qemu_set_irq(s->irq, s->status & ~s->mask);
}

static inline void menelaus_rtc_start(struct menelaus_s *s)
{
    s->rtc.next =+ qemu_get_clock(rt_clock);
    qemu_mod_timer(s->rtc.hz_tm, s->rtc.next);
}

static inline void menelaus_rtc_stop(struct menelaus_s *s)
{
    qemu_del_timer(s->rtc.hz_tm);
    s->rtc.next =- qemu_get_clock(rt_clock);
    if (s->rtc.next < 1)
        s->rtc.next = 1;
}

static void menelaus_rtc_update(struct menelaus_s *s)
{
    qemu_get_timedate(&s->rtc.tm, s->rtc.sec_offset);
}

static void menelaus_alm_update(struct menelaus_s *s)
{
    if ((s->rtc.ctrl & 3) == 3)
        s->rtc.alm_sec = qemu_timedate_diff(&s->rtc.alm) - s->rtc.sec_offset;
}

static void menelaus_rtc_hz(void *opaque)
{
    struct menelaus_s *s = (struct menelaus_s *) opaque;

    s->rtc.next_comp --;
    s->rtc.alm_sec --;
    s->rtc.next += 1000;
    qemu_mod_timer(s->rtc.hz_tm, s->rtc.next);
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

static void menelaus_reset(i2c_slave *i2c)
{
    struct menelaus_s *s = (struct menelaus_s *) i2c;
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

static inline uint8_t to_bcd(int val)
{
    return ((val / 10) << 4) | (val % 10);
}

static inline int from_bcd(uint8_t val)
{
    return ((val >> 4) * 10) + (val & 0x0f);
}

static void menelaus_gpio_set(void *opaque, int line, int level)
{
    struct menelaus_s *s = (struct menelaus_s *) opaque;

    /* No interrupt generated */
    s->inputs &= ~(1 << line);
    s->inputs |= level << line;
}

static void menelaus_pwrbtn_set(void *opaque, int line, int level)
{
    struct menelaus_s *s = (struct menelaus_s *) opaque;

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
    struct menelaus_s *s = (struct menelaus_s *) opaque;
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
        printf("%s: unknown register %02x\n", __FUNCTION__, addr);
#endif
        break;
    }
    return 0;
}

static void menelaus_write(void *opaque, uint8_t addr, uint8_t value)
{
    struct menelaus_s *s = (struct menelaus_s *) opaque;
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
        if (value & 1)
            menelaus_reset(&s->i2c);
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
        for (line = 0; line < 3; line ++)
            if (((s->dir ^ value) >> line) & 1)
                if (s->handler[line])
                    qemu_set_irq(s->handler[line],
                                    ((s->outputs & ~s->dir) >> line) & 1);
        s->dir = value & 0x67;
        break;
    case MENELAUS_GPIO_OUT:
        for (line = 0; line < 3; line ++)
            if ((((s->outputs ^ value) & ~s->dir) >> line) & 1)
                if (s->handler[line])
                    qemu_set_irq(s->handler[line], (s->outputs >> line) & 1);
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
                            __FUNCTION__, value);
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
        printf("%s: unknown register %02x\n", __FUNCTION__, addr);
#endif
    }
}

static void menelaus_event(i2c_slave *i2c, enum i2c_event event)
{
    struct menelaus_s *s = (struct menelaus_s *) i2c;

    if (event == I2C_START_SEND)
        s->firstbyte = 1;
}

static int menelaus_tx(i2c_slave *i2c, uint8_t data)
{
    struct menelaus_s *s = (struct menelaus_s *) i2c;
    /* Interpret register address byte */
    if (s->firstbyte) {
        s->reg = data;
        s->firstbyte = 0;
    } else
        menelaus_write(s, s->reg ++, data);

    return 0;
}

static int menelaus_rx(i2c_slave *i2c)
{
    struct menelaus_s *s = (struct menelaus_s *) i2c;

    return menelaus_read(s, s->reg ++);
}

static void tm_put(QEMUFile *f, struct tm *tm) {
    qemu_put_be16(f, tm->tm_sec);
    qemu_put_be16(f, tm->tm_min);
    qemu_put_be16(f, tm->tm_hour);
    qemu_put_be16(f, tm->tm_mday);
    qemu_put_be16(f, tm->tm_min);
    qemu_put_be16(f, tm->tm_year);
}

static void tm_get(QEMUFile *f, struct tm *tm) {
    tm->tm_sec = qemu_get_be16(f);
    tm->tm_min = qemu_get_be16(f);
    tm->tm_hour = qemu_get_be16(f);
    tm->tm_mday = qemu_get_be16(f);
    tm->tm_min = qemu_get_be16(f);
    tm->tm_year = qemu_get_be16(f);
}

static void menelaus_save(QEMUFile *f, void *opaque)
{
    struct menelaus_s *s = (struct menelaus_s *) opaque;

    qemu_put_be32(f, s->firstbyte);
    qemu_put_8s(f, &s->reg);

    qemu_put_8s(f, &s->vcore[0]);
    qemu_put_8s(f, &s->vcore[1]);
    qemu_put_8s(f, &s->vcore[2]);
    qemu_put_8s(f, &s->vcore[3]);
    qemu_put_8s(f, &s->vcore[4]);
    qemu_put_8s(f, &s->dcdc[3]);
    qemu_put_8s(f, &s->dcdc[3]);
    qemu_put_8s(f, &s->dcdc[3]);
    qemu_put_8s(f, &s->ldo[0]);
    qemu_put_8s(f, &s->ldo[1]);
    qemu_put_8s(f, &s->ldo[2]);
    qemu_put_8s(f, &s->ldo[3]);
    qemu_put_8s(f, &s->ldo[4]);
    qemu_put_8s(f, &s->ldo[5]);
    qemu_put_8s(f, &s->ldo[6]);
    qemu_put_8s(f, &s->ldo[7]);
    qemu_put_8s(f, &s->sleep[0]);
    qemu_put_8s(f, &s->sleep[1]);
    qemu_put_8s(f, &s->osc);
    qemu_put_8s(f, &s->detect);
    qemu_put_be16s(f, &s->mask);
    qemu_put_be16s(f, &s->status);
    qemu_put_8s(f, &s->dir);
    qemu_put_8s(f, &s->inputs);
    qemu_put_8s(f, &s->outputs);
    qemu_put_8s(f, &s->bbsms);
    qemu_put_8s(f, &s->pull[0]);
    qemu_put_8s(f, &s->pull[1]);
    qemu_put_8s(f, &s->pull[2]);
    qemu_put_8s(f, &s->pull[3]);
    qemu_put_8s(f, &s->mmc_ctrl[0]);
    qemu_put_8s(f, &s->mmc_ctrl[1]);
    qemu_put_8s(f, &s->mmc_ctrl[2]);
    qemu_put_8s(f, &s->mmc_debounce);
    qemu_put_8s(f, &s->rtc.ctrl);
    qemu_put_be16s(f, &s->rtc.comp);
    /* Should be <= 1000 */
    qemu_put_be16(f, s->rtc.next - qemu_get_clock(rt_clock));
    tm_put(f, &s->rtc.new);
    tm_put(f, &s->rtc.alm);
    qemu_put_byte(f, s->pwrbtn_state);

    i2c_slave_save(f, &s->i2c);
}

static int menelaus_load(QEMUFile *f, void *opaque, int version_id)
{
    struct menelaus_s *s = (struct menelaus_s *) opaque;

    s->firstbyte = qemu_get_be32(f);
    qemu_get_8s(f, &s->reg);

    if (s->rtc.ctrl & 1)					/* RTC_EN */
        menelaus_rtc_stop(s);
    qemu_get_8s(f, &s->vcore[0]);
    qemu_get_8s(f, &s->vcore[1]);
    qemu_get_8s(f, &s->vcore[2]);
    qemu_get_8s(f, &s->vcore[3]);
    qemu_get_8s(f, &s->vcore[4]);
    qemu_get_8s(f, &s->dcdc[3]);
    qemu_get_8s(f, &s->dcdc[3]);
    qemu_get_8s(f, &s->dcdc[3]);
    qemu_get_8s(f, &s->ldo[0]);
    qemu_get_8s(f, &s->ldo[1]);
    qemu_get_8s(f, &s->ldo[2]);
    qemu_get_8s(f, &s->ldo[3]);
    qemu_get_8s(f, &s->ldo[4]);
    qemu_get_8s(f, &s->ldo[5]);
    qemu_get_8s(f, &s->ldo[6]);
    qemu_get_8s(f, &s->ldo[7]);
    qemu_get_8s(f, &s->sleep[0]);
    qemu_get_8s(f, &s->sleep[1]);
    qemu_get_8s(f, &s->osc);
    qemu_get_8s(f, &s->detect);
    qemu_get_be16s(f, &s->mask);
    qemu_get_be16s(f, &s->status);
    qemu_get_8s(f, &s->dir);
    qemu_get_8s(f, &s->inputs);
    qemu_get_8s(f, &s->outputs);
    qemu_get_8s(f, &s->bbsms);
    qemu_get_8s(f, &s->pull[0]);
    qemu_get_8s(f, &s->pull[1]);
    qemu_get_8s(f, &s->pull[2]);
    qemu_get_8s(f, &s->pull[3]);
    qemu_get_8s(f, &s->mmc_ctrl[0]);
    qemu_get_8s(f, &s->mmc_ctrl[1]);
    qemu_get_8s(f, &s->mmc_ctrl[2]);
    qemu_get_8s(f, &s->mmc_debounce);
    qemu_get_8s(f, &s->rtc.ctrl);
    qemu_get_be16s(f, &s->rtc.comp);
    s->rtc.next = qemu_get_be16(f);
    tm_get(f, &s->rtc.new);
    tm_get(f, &s->rtc.alm);
    s->pwrbtn_state = qemu_get_byte(f);
    menelaus_alm_update(s);
    menelaus_update(s);
    if (s->rtc.ctrl & 1)					/* RTC_EN */
        menelaus_rtc_start(s);

    i2c_slave_load(f, &s->i2c);
    return 0;
}

i2c_slave *twl92230_init(i2c_bus *bus, qemu_irq irq)
{
    struct menelaus_s *s = (struct menelaus_s *)
            i2c_slave_init(bus, 0, sizeof(struct menelaus_s));

    s->i2c.event = menelaus_event;
    s->i2c.recv = menelaus_rx;
    s->i2c.send = menelaus_tx;

    s->irq = irq;
    s->rtc.hz_tm = qemu_new_timer(rt_clock, menelaus_rtc_hz, s);
    s->in = qemu_allocate_irqs(menelaus_gpio_set, s, 3);
    s->pwrbtn = qemu_allocate_irqs(menelaus_pwrbtn_set, s, 1)[0];

    menelaus_reset(&s->i2c);

    register_savevm("menelaus", -1, 0, menelaus_save, menelaus_load, s);

    return &s->i2c;
}

qemu_irq *twl92230_gpio_in_get(i2c_slave *i2c)
{
    struct menelaus_s *s = (struct menelaus_s *) i2c;

    return s->in;
}

void twl92230_gpio_out_set(i2c_slave *i2c, int line, qemu_irq handler)
{
    struct menelaus_s *s = (struct menelaus_s *) i2c;

    if (line >= 3 || line < 0) {
        fprintf(stderr, "%s: No GPO line %i\n", __FUNCTION__, line);
        exit(-1);
    }
    s->handler[line] = handler;
}
