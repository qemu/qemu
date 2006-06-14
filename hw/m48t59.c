/*
 * QEMU M48T59 and M48T08 NVRAM emulation for PPC PREP and Sparc platforms
 * 
 * Copyright (c) 2003-2005 Jocelyn Mayer
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
#include "vl.h"
#include "m48t59.h"

//#define DEBUG_NVRAM

#if defined(DEBUG_NVRAM)
#define NVRAM_PRINTF(fmt, args...) do { printf(fmt , ##args); } while (0)
#else
#define NVRAM_PRINTF(fmt, args...) do { } while (0)
#endif

/*
 * The M48T08 and M48T59 chips are very similar. The newer '59 has
 * alarm and a watchdog timer and related control registers. In the
 * PPC platform there is also a nvram lock function.
 */
struct m48t59_t {
    /* Model parameters */
    int type; // 8 = m48t08, 59 = m48t59
    /* Hardware parameters */
    int      IRQ;
    int mem_index;
    uint32_t mem_base;
    uint32_t io_base;
    uint16_t size;
    /* RTC management */
    time_t   time_offset;
    time_t   stop_time;
    /* Alarm & watchdog */
    time_t   alarm;
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

/* RTC management helpers */
static void get_time (m48t59_t *NVRAM, struct tm *tm)
{
    time_t t;

    t = time(NULL) + NVRAM->time_offset;
#ifdef _WIN32
    memcpy(tm,localtime(&t),sizeof(*tm));
#else
    localtime_r (&t, tm) ;
#endif
}

static void set_time (m48t59_t *NVRAM, struct tm *tm)
{
    time_t now, new_time;
    
    new_time = mktime(tm);
    now = time(NULL);
    NVRAM->time_offset = new_time - now;
}

/* Alarm management */
static void alarm_cb (void *opaque)
{
    struct tm tm, tm_now;
    uint64_t next_time;
    m48t59_t *NVRAM = opaque;

    pic_set_irq(NVRAM->IRQ, 1);
    if ((NVRAM->buffer[0x1FF5] & 0x80) == 0 && 
	(NVRAM->buffer[0x1FF4] & 0x80) == 0 &&
	(NVRAM->buffer[0x1FF3] & 0x80) == 0 &&
	(NVRAM->buffer[0x1FF2] & 0x80) == 0) {
	/* Repeat once a month */
	get_time(NVRAM, &tm_now);
	memcpy(&tm, &tm_now, sizeof(struct tm));
	tm.tm_mon++;
	if (tm.tm_mon == 13) {
	    tm.tm_mon = 1;
	    tm.tm_year++;
	}
	next_time = mktime(&tm);
    } else if ((NVRAM->buffer[0x1FF5] & 0x80) != 0 &&
	       (NVRAM->buffer[0x1FF4] & 0x80) == 0 &&
	       (NVRAM->buffer[0x1FF3] & 0x80) == 0 &&
	       (NVRAM->buffer[0x1FF2] & 0x80) == 0) {
	/* Repeat once a day */
	next_time = 24 * 60 * 60 + mktime(&tm_now);
    } else if ((NVRAM->buffer[0x1FF5] & 0x80) != 0 &&
	       (NVRAM->buffer[0x1FF4] & 0x80) != 0 &&
	       (NVRAM->buffer[0x1FF3] & 0x80) == 0 &&
	       (NVRAM->buffer[0x1FF2] & 0x80) == 0) {
	/* Repeat once an hour */
	next_time = 60 * 60 + mktime(&tm_now);
    } else if ((NVRAM->buffer[0x1FF5] & 0x80) != 0 &&
	       (NVRAM->buffer[0x1FF4] & 0x80) != 0 &&
	       (NVRAM->buffer[0x1FF3] & 0x80) != 0 &&
	       (NVRAM->buffer[0x1FF2] & 0x80) == 0) {
	/* Repeat once a minute */
	next_time = 60 + mktime(&tm_now);
    } else {
	/* Repeat once a second */
	next_time = 1 + mktime(&tm_now);
    }
    qemu_mod_timer(NVRAM->alrm_timer, next_time * 1000);
    pic_set_irq(NVRAM->IRQ, 0);
}


static void get_alarm (m48t59_t *NVRAM, struct tm *tm)
{
#ifdef _WIN32
    memcpy(tm,localtime(&NVRAM->alarm),sizeof(*tm));
#else
    localtime_r (&NVRAM->alarm, tm);
#endif
}

static void set_alarm (m48t59_t *NVRAM, struct tm *tm)
{
    NVRAM->alarm = mktime(tm);
    if (NVRAM->alrm_timer != NULL) {
        qemu_del_timer(NVRAM->alrm_timer);
	NVRAM->alrm_timer = NULL;
    }
    if (NVRAM->alarm - time(NULL) > 0)
	qemu_mod_timer(NVRAM->alrm_timer, NVRAM->alarm * 1000);
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
	pic_set_irq(NVRAM->IRQ, 1);
	pic_set_irq(NVRAM->IRQ, 0);
    }
}

static void set_up_watchdog (m48t59_t *NVRAM, uint8_t value)
{
    uint64_t interval; /* in 1/16 seconds */

    if (NVRAM->wd_timer != NULL) {
        qemu_del_timer(NVRAM->wd_timer);
	NVRAM->wd_timer = NULL;
    }
    NVRAM->buffer[0x1FF0] &= ~0x80;
    if (value != 0) {
	interval = (1 << (2 * (value & 0x03))) * ((value >> 2) & 0x1F);
	qemu_mod_timer(NVRAM->wd_timer, ((uint64_t)time(NULL) * 1000) +
		       ((interval * 1000) >> 4));
    }
}

/* Direct access to NVRAM */
void m48t59_write (m48t59_t *NVRAM, uint32_t addr, uint32_t val)
{
    struct tm tm;
    int tmp;

    if (addr > 0x1FF8 && addr < 0x2000)
	NVRAM_PRINTF("%s: 0x%08x => 0x%08x\n", __func__, addr, val);
    if (NVRAM->type == 8 && 
        (addr >= 0x1ff0 && addr <= 0x1ff7))
        goto do_write;
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
            get_alarm(NVRAM, &tm);
            tm.tm_sec = tmp;
            NVRAM->buffer[0x1FF2] = val;
            set_alarm(NVRAM, &tm);
        }
        break;
    case 0x1FF3:
        /* alarm minutes */
        tmp = fromBCD(val & 0x7F);
        if (tmp >= 0 && tmp <= 59) {
            get_alarm(NVRAM, &tm);
            tm.tm_min = tmp;
            NVRAM->buffer[0x1FF3] = val;
            set_alarm(NVRAM, &tm);
        }
        break;
    case 0x1FF4:
        /* alarm hours */
        tmp = fromBCD(val & 0x3F);
        if (tmp >= 0 && tmp <= 23) {
            get_alarm(NVRAM, &tm);
            tm.tm_hour = tmp;
            NVRAM->buffer[0x1FF4] = val;
            set_alarm(NVRAM, &tm);
        }
        break;
    case 0x1FF5:
        /* alarm date */
        tmp = fromBCD(val & 0x1F);
        if (tmp != 0) {
            get_alarm(NVRAM, &tm);
            tm.tm_mday = tmp;
            NVRAM->buffer[0x1FF5] = val;
            set_alarm(NVRAM, &tm);
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
        /* control */
	NVRAM->buffer[0x1FF8] = (val & ~0xA0) | 0x90;
        break;
    case 0x1FF9:
        /* seconds (BCD) */
	tmp = fromBCD(val & 0x7F);
	if (tmp >= 0 && tmp <= 59) {
	    get_time(NVRAM, &tm);
	    tm.tm_sec = tmp;
	    set_time(NVRAM, &tm);
	}
	if ((val & 0x80) ^ (NVRAM->buffer[0x1FF9] & 0x80)) {
	    if (val & 0x80) {
		NVRAM->stop_time = time(NULL);
	    } else {
		NVRAM->time_offset += NVRAM->stop_time - time(NULL);
		NVRAM->stop_time = 0;
	    }
	}
	NVRAM->buffer[0x1FF9] = val & 0x80;
        break;
    case 0x1FFA:
        /* minutes (BCD) */
	tmp = fromBCD(val & 0x7F);
	if (tmp >= 0 && tmp <= 59) {
	    get_time(NVRAM, &tm);
	    tm.tm_min = tmp;
	    set_time(NVRAM, &tm);
	}
        break;
    case 0x1FFB:
        /* hours (BCD) */
	tmp = fromBCD(val & 0x3F);
	if (tmp >= 0 && tmp <= 23) {
	    get_time(NVRAM, &tm);
	    tm.tm_hour = tmp;
	    set_time(NVRAM, &tm);
	}
        break;
    case 0x1FFC:
        /* day of the week / century */
	tmp = fromBCD(val & 0x07);
	get_time(NVRAM, &tm);
	tm.tm_wday = tmp;
	set_time(NVRAM, &tm);
        NVRAM->buffer[0x1FFC] = val & 0x40;
        break;
    case 0x1FFD:
        /* date */
	tmp = fromBCD(val & 0x1F);
	if (tmp != 0) {
	    get_time(NVRAM, &tm);
	    tm.tm_mday = tmp;
	    set_time(NVRAM, &tm);
	}
        break;
    case 0x1FFE:
        /* month */
	tmp = fromBCD(val & 0x1F);
	if (tmp >= 1 && tmp <= 12) {
	    get_time(NVRAM, &tm);
	    tm.tm_mon = tmp - 1;
	    set_time(NVRAM, &tm);
	}
        break;
    case 0x1FFF:
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

uint32_t m48t59_read (m48t59_t *NVRAM, uint32_t addr)
{
    struct tm tm;
    uint32_t retval = 0xFF;

    if (NVRAM->type == 8 && 
        (addr >= 0x1ff0 && addr <= 0x1ff7))
        goto do_read;
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
        /* control */
	goto do_read;
    case 0x1FF9:
        /* seconds (BCD) */
        get_time(NVRAM, &tm);
        retval = (NVRAM->buffer[0x1FF9] & 0x80) | toBCD(tm.tm_sec);
        break;
    case 0x1FFA:
        /* minutes (BCD) */
        get_time(NVRAM, &tm);
        retval = toBCD(tm.tm_min);
        break;
    case 0x1FFB:
        /* hours (BCD) */
        get_time(NVRAM, &tm);
        retval = toBCD(tm.tm_hour);
        break;
    case 0x1FFC:
        /* day of the week / century */
        get_time(NVRAM, &tm);
        retval = NVRAM->buffer[0x1FFC] | tm.tm_wday;
        break;
    case 0x1FFD:
        /* date */
        get_time(NVRAM, &tm);
        retval = toBCD(tm.tm_mday);
        break;
    case 0x1FFE:
        /* month */
        get_time(NVRAM, &tm);
        retval = toBCD(tm.tm_mon + 1);
        break;
    case 0x1FFF:
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
	NVRAM_PRINTF("0x%08x <= 0x%08x\n", addr, retval);

    return retval;
}

void m48t59_set_addr (m48t59_t *NVRAM, uint32_t addr)
{
    NVRAM->addr = addr;
}

void m48t59_toggle_lock (m48t59_t *NVRAM, int lock)
{
    NVRAM->lock ^= 1 << lock;
}

/* IO access to NVRAM */
static void NVRAM_writeb (void *opaque, uint32_t addr, uint32_t val)
{
    m48t59_t *NVRAM = opaque;

    addr -= NVRAM->io_base;
    NVRAM_PRINTF("0x%08x => 0x%08x\n", addr, val);
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
    NVRAM_PRINTF("0x%08x <= 0x%08x\n", addr, retval);

    return retval;
}

static void nvram_writeb (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    m48t59_t *NVRAM = opaque;
    
    addr -= NVRAM->mem_base;
    m48t59_write(NVRAM, addr, value & 0xff);
}

static void nvram_writew (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    m48t59_t *NVRAM = opaque;
    
    addr -= NVRAM->mem_base;
    m48t59_write(NVRAM, addr, (value >> 8) & 0xff);
    m48t59_write(NVRAM, addr + 1, value & 0xff);
}

static void nvram_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    m48t59_t *NVRAM = opaque;
    
    addr -= NVRAM->mem_base;
    m48t59_write(NVRAM, addr, (value >> 24) & 0xff);
    m48t59_write(NVRAM, addr + 1, (value >> 16) & 0xff);
    m48t59_write(NVRAM, addr + 2, (value >> 8) & 0xff);
    m48t59_write(NVRAM, addr + 3, value & 0xff);
}

static uint32_t nvram_readb (void *opaque, target_phys_addr_t addr)
{
    m48t59_t *NVRAM = opaque;
    uint32_t retval;
    
    addr -= NVRAM->mem_base;
    retval = m48t59_read(NVRAM, addr);
    return retval;
}

static uint32_t nvram_readw (void *opaque, target_phys_addr_t addr)
{
    m48t59_t *NVRAM = opaque;
    uint32_t retval;
    
    addr -= NVRAM->mem_base;
    retval = m48t59_read(NVRAM, addr) << 8;
    retval |= m48t59_read(NVRAM, addr + 1);
    return retval;
}

static uint32_t nvram_readl (void *opaque, target_phys_addr_t addr)
{
    m48t59_t *NVRAM = opaque;
    uint32_t retval;

    addr -= NVRAM->mem_base;
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

/* Initialisation routine */
m48t59_t *m48t59_init (int IRQ, target_ulong mem_base,
                       uint32_t io_base, uint16_t size,
                       int type)
{
    m48t59_t *s;

    s = qemu_mallocz(sizeof(m48t59_t));
    if (!s)
	return NULL;
    s->buffer = qemu_mallocz(size);
    if (!s->buffer) {
        qemu_free(s);
        return NULL;
    }
    s->IRQ = IRQ;
    s->size = size;
    s->mem_base = mem_base;
    s->io_base = io_base;
    s->addr = 0;
    s->type = type;
    if (io_base != 0) {
        register_ioport_read(io_base, 0x04, 1, NVRAM_readb, s);
        register_ioport_write(io_base, 0x04, 1, NVRAM_writeb, s);
    }
    if (mem_base != 0) {
        s->mem_index = cpu_register_io_memory(0, nvram_read, nvram_write, s);
        cpu_register_physical_memory(mem_base, 0x4000, s->mem_index);
    }
    if (type == 59) {
        s->alrm_timer = qemu_new_timer(vm_clock, &alarm_cb, s);
        s->wd_timer = qemu_new_timer(vm_clock, &watchdog_cb, s);
    }
    s->lock = 0;

    return s;
}
