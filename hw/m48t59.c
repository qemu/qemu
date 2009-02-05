/*
 * QEMU M48T59 and M48T08 NVRAM emulation for PPC PREP and Sparc platforms
 *
 * Copyright (c) 2003-2005, 2007 Jocelyn Mayer
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "hw.h"
#include "nvram.h"
#include "isa.h"
#include "qemu-timer.h"
#include "sysemu.h"

//#define DEBUG_NVRAM

#if defined(DEBUG_NVRAM)
#define NVRAM_PRINTF(fmt, args...) do { printf(fmt , ##args); } while (0)
#else
#define NVRAM_PRINTF(fmt, args...) do { } while (0)
#endif

/*
 * The M48T02, M48T08 and M48T59 chips are very similar. The newer '59 has
 * alarm and a watchdog timer and related control registers. In the
 * PPC platform there is also a nvram lock function.
 */
struct m48t59_t {
    /* Model parameters */
    int type; // 2 = m48t02, 8 = m48t08, 59 = m48t59
    /* Hardware parameters */
    qemu_irq IRQ;
    int mem_index;
    uint32_t io_base;
    uint16_t size;
    /* RTC management */
    time_t   time_offset;
    time_t   stop_time;
    /* Alarm & watchdog */
    struct tm alarm;
    struct QEMUTimer *alrm_timer;
    struct QEMUTimer *wd_timer;
    /* NVRAM storage */
    uint8_t  lock;
    uint16_t addr;
    uint8_t *buffer;
};

/* Fake timer functions */
/* Generic helpers for BCD */
static inline uint8_t toBCD (uint8_t value)
{
    return (((value / 10) % 10) << 4) | (value % 10);
}

static inline uint8_t fromBCD (uint8_t BCD)
{
    return ((BCD >> 4) * 10) + (BCD & 0x0F);
}

/* Alarm management */
static void alarm_cb (void *opaque)
{
    struct tm tm;
    uint64_t next_time;
    m48t59_t *NVRAM = opaque;

    qemu_set_irq(NVRAM->IRQ, 1);
    if ((NVRAM->buffer[0x1FF5] & 0x80) == 0 &&
	(NVRAM->buffer[0x1FF4] & 0x80) == 0 &&
	(NVRAM->buffer[0x1FF3] & 0x80) == 0 &&
	(NVRAM->buffer[0x1FF2] & 0x80) == 0) {
        /* Repeat once a month */
        qemu_get_timedate(&tm, NVRAM->time_offset);
        tm.tm_mon++;
        if (tm.tm_mon == 13) {
            tm.tm_mon = 1;
            tm.tm_year++;
        }
        next_time = qemu_timedate_diff(&tm) - NVRAM->time_offset;
    } else if ((NVRAM->buffer[0x1FF5] & 0x80) != 0 &&
	       (NVRAM->buffer[0x1FF4] & 0x80) == 0 &&
	       (NVRAM->buffer[0x1FF3] & 0x80) == 0 &&
	       (NVRAM->buffer[0x1FF2] & 0x80) == 0) {
        /* Repeat once a day */
        next_time = 24 * 60 * 60;
    } else if ((NVRAM->buffer[0x1FF5] & 0x80) != 0 &&
	       (NVRAM->buffer[0x1FF4] & 0x80) != 0 &&
	       (NVRAM->buffer[0x1FF3] & 0x80) == 0 &&
	       (NVRAM->buffer[0x1FF2] & 0x80) == 0) {
        /* Repeat once an hour */
        next_time = 60 * 60;
    } else if ((NVRAM->buffer[0x1FF5] & 0x80) != 0 &&
	       (NVRAM->buffer[0x1FF4] & 0x80) != 0 &&
	       (NVRAM->buffer[0x1FF3] & 0x80) != 0 &&
	       (NVRAM->buffer[0x1FF2] & 0x80) == 0) {
        /* Repeat once a minute */
        next_time = 60;
    } else {
        /* Repeat once a second */
        next_time = 1;
    }
    qemu_mod_timer(NVRAM->alrm_timer, qemu_get_clock(vm_clock) +
                    next_time * 1000);
    qemu_set_irq(NVRAM->IRQ, 0);
}

static void set_alarm (m48t59_t *NVRAM)
{
    int diff;
    if (NVRAM->alrm_timer != NULL) {
        qemu_del_timer(NVRAM->alrm_timer);
        diff = qemu_timedate_diff(&NVRAM->alarm) - NVRAM->time_offset;
        if (diff > 0)
            qemu_mod_timer(NVRAM->alrm_timer, diff * 1000);
    }
}

/* RTC management helpers */
static inline void get_time (m48t59_t *NVRAM, struct tm *tm)
{
    qemu_get_timedate(tm, NVRAM->time_offset);
}

static void set_time (m48t59_t *NVRAM, struct tm *tm)
{
    NVRAM->time_offset = qemu_timedate_diff(tm);
    set_alarm(NVRAM);
}

/* Watchdog management */
static void watchdog_cb (void *opaque)
{
    m48t59_t *NVRAM = opaque;

    NVRAM->buffer[0x1FF0] |= 0x80;
    if (NVRAM->buffer[0x1FF7] & 0x80) {
	NVRAM->buffer[0x1FF7] = 0x00;
	NVRAM->buffer[0x1FFC] &= ~0x40;
        /* May it be a hw CPU Reset instead ? */
        qemu_system_reset_request();
    } else {
	qemu_set_irq(NVRAM->IRQ, 1);
	qemu_set_irq(NVRAM->IRQ, 0);
    }
}

static void set_up_watchdog (m48t59_t *NVRAM, uint8_t value)
{
    uint64_t interval; /* in 1/16 seconds */

    NVRAM->buffer[0x1FF0] &= ~0x80;
    if (NVRAM->wd_timer != NULL) {
        qemu_del_timer(NVRAM->wd_timer);
        if (value != 0) {
            interval = (1 << (2 * (value & 0x03))) * ((value >> 2) & 0x1F);
            qemu_mod_timer(NVRAM->wd_timer, ((uint64_t)time(NULL) * 1000) +
                           ((interval * 1000) >> 4));
        }
    }
}

/* Direct access to NVRAM */
void m48t59_write (void *opaque, uint32_t addr, uint32_t val)
{
    m48t59_t *NVRAM = opaque;
    struct tm tm;
    int tmp;

    if (addr > 0x1FF8 && addr < 0x2000)
	NVRAM_PRINTF("%s: 0x%08x => 0x%08x\n", __func__, addr, val);

    /* check for NVRAM access */
    if ((NVRAM->type == 2 && addr < 0x7f8) ||
        (NVRAM->type == 8 && addr < 0x1ff8) ||
        (NVRAM->type == 59 && addr < 0x1ff0))
        goto do_write;

    /* TOD access */
    switch (addr) {
    case 0x1FF0:
        /* flags register : read-only */
        break;
    case 0x1FF1:
        /* unused */
        break;
    case 0x1FF2:
        /* alarm seconds */
        tmp = fromBCD(val & 0x7F);
        if (tmp >= 0 && tmp <= 59) {
            NVRAM->alarm.tm_sec = tmp;
            NVRAM->buffer[0x1FF2] = val;
            set_alarm(NVRAM);
        }
        break;
    case 0x1FF3:
        /* alarm minutes */
        tmp = fromBCD(val & 0x7F);
        if (tmp >= 0 && tmp <= 59) {
            NVRAM->alarm.tm_min = tmp;
            NVRAM->buffer[0x1FF3] = val;
            set_alarm(NVRAM);
        }
        break;
    case 0x1FF4:
        /* alarm hours */
        tmp = fromBCD(val & 0x3F);
        if (tmp >= 0 && tmp <= 23) {
            NVRAM->alarm.tm_hour = tmp;
            NVRAM->buffer[0x1FF4] = val;
            set_alarm(NVRAM);
        }
        break;
    case 0x1FF5:
        /* alarm date */
        tmp = fromBCD(val & 0x1F);
        if (tmp != 0) {
            NVRAM->alarm.tm_mday = tmp;
            NVRAM->buffer[0x1FF5] = val;
            set_alarm(NVRAM);
        }
        break;
    case 0x1FF6:
        /* interrupts */
        NVRAM->buffer[0x1FF6] = val;
        break;
    case 0x1FF7:
        /* watchdog */
        NVRAM->buffer[0x1FF7] = val;
        set_up_watchdog(NVRAM, val);
        break;
    case 0x1FF8:
    case 0x07F8:
        /* control */
       NVRAM->buffer[addr] = (val & ~0xA0) | 0x90;
        break;
    case 0x1FF9:
    case 0x07F9:
        /* seconds (BCD) */
	tmp = fromBCD(val & 0x7F);
	if (tmp >= 0 && tmp <= 59) {
	    get_time(NVRAM, &tm);
	    tm.tm_sec = tmp;
	    set_time(NVRAM, &tm);
	}
        if ((val & 0x80) ^ (NVRAM->buffer[addr] & 0x80)) {
	    if (val & 0x80) {
		NVRAM->stop_time = time(NULL);
	    } else {
		NVRAM->time_offset += NVRAM->stop_time - time(NULL);
		NVRAM->stop_time = 0;
	    }
	}
        NVRAM->buffer[addr] = val & 0x80;
        break;
    case 0x1FFA:
    case 0x07FA:
        /* minutes (BCD) */
	tmp = fromBCD(val & 0x7F);
	if (tmp >= 0 && tmp <= 59) {
	    get_time(NVRAM, &tm);
	    tm.tm_min = tmp;
	    set_time(NVRAM, &tm);
	}
        break;
    case 0x1FFB:
    case 0x07FB:
        /* hours (BCD) */
	tmp = fromBCD(val & 0x3F);
	if (tmp >= 0 && tmp <= 23) {
	    get_time(NVRAM, &tm);
	    tm.tm_hour = tmp;
	    set_time(NVRAM, &tm);
	}
        break;
    case 0x1FFC:
    case 0x07FC:
        /* day of the week / century */
	tmp = fromBCD(val & 0x07);
	get_time(NVRAM, &tm);
	tm.tm_wday = tmp;
	set_time(NVRAM, &tm);
        NVRAM->buffer[addr] = val & 0x40;
        break;
    case 0x1FFD:
    case 0x07FD:
        /* date */
	tmp = fromBCD(val & 0x1F);
	if (tmp != 0) {
	    get_time(NVRAM, &tm);
	    tm.tm_mday = tmp;
	    set_time(NVRAM, &tm);
	}
        break;
    case 0x1FFE:
    case 0x07FE:
        /* month */
	tmp = fromBCD(val & 0x1F);
	if (tmp >= 1 && tmp <= 12) {
	    get_time(NVRAM, &tm);
	    tm.tm_mon = tmp - 1;
	    set_time(NVRAM, &tm);
	}
        break;
    case 0x1FFF:
    case 0x07FF:
        /* year */
	tmp = fromBCD(val);
	if (tmp >= 0 && tmp <= 99) {
	    get_time(NVRAM, &tm);
            if (NVRAM->type == 8)
                tm.tm_year = fromBCD(val) + 68; // Base year is 1968
            else
                tm.tm_year = fromBCD(val);
	    set_time(NVRAM, &tm);
	}
        break;
    default:
        /* Check lock registers state */
        if (addr >= 0x20 && addr <= 0x2F && (NVRAM->lock & 1))
            break;
        if (addr >= 0x30 && addr <= 0x3F && (NVRAM->lock & 2))
            break;
    do_write:
        if (addr < NVRAM->size) {
            NVRAM->buffer[addr] = val & 0xFF;
	}
        break;
    }
}

uint32_t m48t59_read (void *opaque, uint32_t addr)
{
    m48t59_t *NVRAM = opaque;
    struct tm tm;
    uint32_t retval = 0xFF;

    /* check for NVRAM access */
    if ((NVRAM->type == 2 && addr < 0x078f) ||
        (NVRAM->type == 8 && addr < 0x1ff8) ||
        (NVRAM->type == 59 && addr < 0x1ff0))
        goto do_read;

    /* TOD access */
    switch (addr) {
    case 0x1FF0:
        /* flags register */
	goto do_read;
    case 0x1FF1:
        /* unused */
	retval = 0;
        break;
    case 0x1FF2:
        /* alarm seconds */
	goto do_read;
    case 0x1FF3:
        /* alarm minutes */
	goto do_read;
    case 0x1FF4:
        /* alarm hours */
	goto do_read;
    case 0x1FF5:
        /* alarm date */
	goto do_read;
    case 0x1FF6:
        /* interrupts */
	goto do_read;
    case 0x1FF7:
	/* A read resets the watchdog */
	set_up_watchdog(NVRAM, NVRAM->buffer[0x1FF7]);
	goto do_read;
    case 0x1FF8:
    case 0x07F8:
        /* control */
	goto do_read;
    case 0x1FF9:
    case 0x07F9:
        /* seconds (BCD) */
        get_time(NVRAM, &tm);
        retval = (NVRAM->buffer[addr] & 0x80) | toBCD(tm.tm_sec);
        break;
    case 0x1FFA:
    case 0x07FA:
        /* minutes (BCD) */
        get_time(NVRAM, &tm);
        retval = toBCD(tm.tm_min);
        break;
    case 0x1FFB:
    case 0x07FB:
        /* hours (BCD) */
        get_time(NVRAM, &tm);
        retval = toBCD(tm.tm_hour);
        break;
    case 0x1FFC:
    case 0x07FC:
        /* day of the week / century */
        get_time(NVRAM, &tm);
        retval = NVRAM->buffer[addr] | tm.tm_wday;
        break;
    case 0x1FFD:
    case 0x07FD:
        /* date */
        get_time(NVRAM, &tm);
        retval = toBCD(tm.tm_mday);
        break;
    case 0x1FFE:
    case 0x07FE:
        /* month */
        get_time(NVRAM, &tm);
        retval = toBCD(tm.tm_mon + 1);
        break;
    case 0x1FFF:
    case 0x07FF:
        /* year */
        get_time(NVRAM, &tm);
        if (NVRAM->type == 8)
            retval = toBCD(tm.tm_year - 68); // Base year is 1968
        else
            retval = toBCD(tm.tm_year);
        break;
    default:
        /* Check lock registers state */
        if (addr >= 0x20 && addr <= 0x2F && (NVRAM->lock & 1))
            break;
        if (addr >= 0x30 && addr <= 0x3F && (NVRAM->lock & 2))
            break;
    do_read:
        if (addr < NVRAM->size) {
            retval = NVRAM->buffer[addr];
	}
        break;
    }
    if (addr > 0x1FF9 && addr < 0x2000)
       NVRAM_PRINTF("%s: 0x%08x <= 0x%08x\n", __func__, addr, retval);

    return retval;
}

void m48t59_set_addr (void *opaque, uint32_t addr)
{
    m48t59_t *NVRAM = opaque;

    NVRAM->addr = addr;
}

void m48t59_toggle_lock (void *opaque, int lock)
{
    m48t59_t *NVRAM = opaque;

    NVRAM->lock ^= 1 << lock;
}

/* IO access to NVRAM */
static void NVRAM_writeb (void *opaque, uint32_t addr, uint32_t val)
{
    m48t59_t *NVRAM = opaque;

    addr -= NVRAM->io_base;
    NVRAM_PRINTF("%s: 0x%08x => 0x%08x\n", __func__, addr, val);
    switch (addr) {
    case 0:
        NVRAM->addr &= ~0x00FF;
        NVRAM->addr |= val;
        break;
    case 1:
        NVRAM->addr &= ~0xFF00;
        NVRAM->addr |= val << 8;
        break;
    case 3:
        m48t59_write(NVRAM, val, NVRAM->addr);
        NVRAM->addr = 0x0000;
        break;
    default:
        break;
    }
}

static uint32_t NVRAM_readb (void *opaque, uint32_t addr)
{
    m48t59_t *NVRAM = opaque;
    uint32_t retval;

    addr -= NVRAM->io_base;
    switch (addr) {
    case 3:
        retval = m48t59_read(NVRAM, NVRAM->addr);
        break;
    default:
        retval = -1;
        break;
    }
    NVRAM_PRINTF("%s: 0x%08x <= 0x%08x\n", __func__, addr, retval);

    return retval;
}

static void nvram_writeb (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    m48t59_t *NVRAM = opaque;

    m48t59_write(NVRAM, addr, value & 0xff);
}

static void nvram_writew (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    m48t59_t *NVRAM = opaque;

    m48t59_write(NVRAM, addr, (value >> 8) & 0xff);
    m48t59_write(NVRAM, addr + 1, value & 0xff);
}

static void nvram_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    m48t59_t *NVRAM = opaque;

    m48t59_write(NVRAM, addr, (value >> 24) & 0xff);
    m48t59_write(NVRAM, addr + 1, (value >> 16) & 0xff);
    m48t59_write(NVRAM, addr + 2, (value >> 8) & 0xff);
    m48t59_write(NVRAM, addr + 3, value & 0xff);
}

static uint32_t nvram_readb (void *opaque, target_phys_addr_t addr)
{
    m48t59_t *NVRAM = opaque;
    uint32_t retval;

    retval = m48t59_read(NVRAM, addr);
    return retval;
}

static uint32_t nvram_readw (void *opaque, target_phys_addr_t addr)
{
    m48t59_t *NVRAM = opaque;
    uint32_t retval;

    retval = m48t59_read(NVRAM, addr) << 8;
    retval |= m48t59_read(NVRAM, addr + 1);
    return retval;
}

static uint32_t nvram_readl (void *opaque, target_phys_addr_t addr)
{
    m48t59_t *NVRAM = opaque;
    uint32_t retval;

    retval = m48t59_read(NVRAM, addr) << 24;
    retval |= m48t59_read(NVRAM, addr + 1) << 16;
    retval |= m48t59_read(NVRAM, addr + 2) << 8;
    retval |= m48t59_read(NVRAM, addr + 3);
    return retval;
}

static CPUWriteMemoryFunc *nvram_write[] = {
    &nvram_writeb,
    &nvram_writew,
    &nvram_writel,
};

static CPUReadMemoryFunc *nvram_read[] = {
    &nvram_readb,
    &nvram_readw,
    &nvram_readl,
};

static void m48t59_save(QEMUFile *f, void *opaque)
{
    m48t59_t *s = opaque;

    qemu_put_8s(f, &s->lock);
    qemu_put_be16s(f, &s->addr);
    qemu_put_buffer(f, s->buffer, s->size);
}

static int m48t59_load(QEMUFile *f, void *opaque, int version_id)
{
    m48t59_t *s = opaque;

    if (version_id != 1)
        return -EINVAL;

    qemu_get_8s(f, &s->lock);
    qemu_get_be16s(f, &s->addr);
    qemu_get_buffer(f, s->buffer, s->size);

    return 0;
}

static void m48t59_reset(void *opaque)
{
    m48t59_t *NVRAM = opaque;

    NVRAM->addr = 0;
    NVRAM->lock = 0;
    if (NVRAM->alrm_timer != NULL)
        qemu_del_timer(NVRAM->alrm_timer);

    if (NVRAM->wd_timer != NULL)
        qemu_del_timer(NVRAM->wd_timer);
}

/* Initialisation routine */
m48t59_t *m48t59_init (qemu_irq IRQ, target_phys_addr_t mem_base,
                       uint32_t io_base, uint16_t size,
                       int type)
{
    m48t59_t *s;
    target_phys_addr_t save_base;

    s = qemu_mallocz(sizeof(m48t59_t));
    s->buffer = qemu_mallocz(size);
    s->IRQ = IRQ;
    s->size = size;
    s->io_base = io_base;
    s->type = type;
    if (io_base != 0) {
        register_ioport_read(io_base, 0x04, 1, NVRAM_readb, s);
        register_ioport_write(io_base, 0x04, 1, NVRAM_writeb, s);
    }
    if (mem_base != 0) {
        s->mem_index = cpu_register_io_memory(0, nvram_read, nvram_write, s);
        cpu_register_physical_memory(mem_base, size, s->mem_index);
    }
    if (type == 59) {
        s->alrm_timer = qemu_new_timer(vm_clock, &alarm_cb, s);
        s->wd_timer = qemu_new_timer(vm_clock, &watchdog_cb, s);
    }
    qemu_get_timedate(&s->alarm, 0);

    qemu_register_reset(m48t59_reset, s);
    save_base = mem_base ? mem_base : io_base;
    register_savevm("m48t59", save_base, 1, m48t59_save, m48t59_load, s);

    return s;
}
