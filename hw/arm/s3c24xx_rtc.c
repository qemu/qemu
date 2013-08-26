/* hw/s3c24xx_rtc.c
 *
 * Samsung S3C24XX RTC emulation
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


/* RTC Control RW Byte */
#define S3C_REG_RTCCON 0
/* Tick time count RW Byte */
#define S3C_REG_TICNT 1
/* RTC Alarm Control RW Byte */
#define S3C_REG_RTCALM 4
/* Alarm second */
#define S3C_REG_ALMSEC 5
/* Alarm minute */
#define S3C_REG_ALMMIN 6
/* Alarm hour */
#define S3C_REG_ALMHOUR 7
/* Alarm day */
#define S3C_REG_ALMDATE 8
/* Alarm month */
#define S3C_REG_ALMMON 9
/* Alarm year */
#define S3C_REG_ALMYEAR 10
/* RTC Round Reset */
#define S3C_REG_RTCRST 11
/* BCD Second */
#define S3C_REG_BCDSEC 12
/* BCD Minute */
#define S3C_REG_BCDMIN 13
/* BCD Hour */
#define S3C_REG_BCDHOUR 14
/* BCD Day */
#define S3C_REG_BCDDATE 15
/* BCD Day of week */
#define S3C_REG_BCDDAY 16
/* BCD Month */
#define S3C_REG_BCDMON 17
/* BCD Year */
#define S3C_REG_BCDYEAR 18

/* Real Time clock state */
struct s3c24xx_rtc_state_s {
    MemoryRegion mmio;
    uint32_t rtc_reg[19];
};


static void update_time(struct s3c24xx_rtc_state_s *s)
{
    time_t ti;
    struct tm *tm;
    /* update the RTC registers from system time */
    time(&ti);
    tm = gmtime(&ti);
    s->rtc_reg[S3C_REG_BCDSEC] = to_bcd(tm->tm_sec);
    s->rtc_reg[S3C_REG_BCDMIN] = to_bcd(tm->tm_min);
    s->rtc_reg[S3C_REG_BCDHOUR] = to_bcd(tm->tm_hour);
    s->rtc_reg[S3C_REG_BCDDATE] = to_bcd(tm->tm_mday);
    s->rtc_reg[S3C_REG_BCDDAY] = to_bcd(tm->tm_wday + 1);
    s->rtc_reg[S3C_REG_BCDMON] = to_bcd(tm->tm_mon + 1);
    s->rtc_reg[S3C_REG_BCDYEAR] =  to_bcd(tm->tm_year - 100);
}

static void s3c24xx_rtc_write(void *opaque, hwaddr addr_,
                              uint64_t value, unsigned size)
{
    struct s3c24xx_rtc_state_s *s = opaque;
    int addr = (addr_ - 0x40) >> 2;
    if (addr < 0 || addr > 18)
        addr = 18;

    s->rtc_reg[addr] = value;
}

static uint64_t s3c24xx_rtc_read(void *opaque, hwaddr addr_,
                                 unsigned size)
{
    struct s3c24xx_rtc_state_s *s = opaque;
    int addr = (addr_ - 0x40) >> 2;

    if (addr < 0 || addr > 18)
        addr = 18;

    update_time(s);

    return s->rtc_reg[addr];
}

static const MemoryRegionOps s3c24xx_rtc_ops = {
    .read = s3c24xx_rtc_read,
    .write = s3c24xx_rtc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4
    }
};

struct s3c24xx_rtc_state_s *s3c24xx_rtc_init(hwaddr base_addr)
{
    struct s3c24xx_rtc_state_s *s = g_new0(struct s3c24xx_rtc_state_s, 1);

    /* there are only 19 real registers but they start at offset 0x40 into the
     * range so we have 35 registers mapped
     */
    memory_region_init_io(&s->mmio, OBJECT(s), &s3c24xx_rtc_ops, s, "s3c24xx.rtc", 35 * 4);
    memory_region_add_subregion(get_system_memory(), base_addr, &s->mmio);

    /* set the RTC so it appears active */
    s->rtc_reg[S3C_REG_RTCCON] = 1;

    return s;
}
