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
#include "qemu-timer.h"
#include "sysemu.h"
#include "sysbus.h"
#include "isa.h"

//#define DEBUG_NVRAM

#if defined(DEBUG_NVRAM)
#define NVRAM_PRINTF(fmt, ...) do { printf(fmt , ## __VA_ARGS__); } while (0)
#else
#define NVRAM_PRINTF(fmt, ...) do { } while (0)
#endif

/*
 * The M48T02, M48T08 and M48T59 chips are very similar. The newer '59 has
 * alarm and a watchdog timer and related control registers. In the
 * PPC platform there is also a nvram lock function.
 */

/*
 * Chipset docs:
 * http://www.st.com/stonline/products/literature/ds/2410/m48t02.pdf
 * http://www.st.com/stonline/products/literature/ds/2411/m48t08.pdf
 * http://www.st.com/stonline/products/literature/od/7001/m48t59y.pdf
 */

struct M48t59State {
    /* Hardware parameters */
    qemu_irq IRQ;
    uint32_t io_base;
    uint32_t size;
    /* RTC management */
    time_t   time_offset;
    time_t   stop_time;
    /* Alarm & watchdog */
    struct tm alarm;
    struct QEMUTimer *alrm_timer;
    struct QEMUTimer *wd_timer;
    /* NVRAM storage */
    uint8_t *buffer;
    /* Model parameters */
    uint32_t type; /* 2 = m48t02, 8 = m48t08, 59 = m48t59 */
    /* NVRAM storage */
    uint16_t addr;
    uint8_t  lock;
};

typedef struct M48t59ISAState {
    ISADevice busdev;
    M48t59State state;
    MemoryRegion io;
} M48t59ISAState;

typedef struct M48t59SysBusState {
    SysBusDevice busdev;
    M48t59State state;
} M48t59SysBusState;

/* Fake timer functions */

/* Alarm management */
static void alarm_cb (void *opaque)
{
    struct tm tm;
    uint64_t next_time;
    M48t59State *NVRAM = opaque;

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
    qemu_mod_timer(NVRAM->alrm_timer, qemu_get_clock_ns(vm_clock) +
                    next_time * 1000);
    qemu_set_irq(NVRAM->IRQ, 0);
}

static void set_alarm(M48t59State *NVRAM)
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
static inline void get_time(M48t59State *NVRAM, struct tm *tm)
{
    qemu_get_timedate(tm, NVRAM->time_offset);
}

static void set_time(M48t59State *NVRAM, struct tm *tm)
{
    NVRAM->time_offset = qemu_timedate_diff(tm);
    set_alarm(NVRAM);
}

/* Watchdog management */
static void watchdog_cb (void *opaque)
{
    M48t59State *NVRAM = opaque;

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

static void set_up_watchdog(M48t59State *NVRAM, uint8_t value)
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
    M48t59State *NVRAM = opaque;
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
        tmp = from_bcd(val & 0x7F);
        if (tmp >= 0 && tmp <= 59) {
            NVRAM->alarm.tm_sec = tmp;
            NVRAM->buffer[0x1FF2] = val;
            set_alarm(NVRAM);
        }
        break;
    case 0x1FF3:
        /* alarm minutes */
        tmp = from_bcd(val & 0x7F);
        if (tmp >= 0 && tmp <= 59) {
            NVRAM->alarm.tm_min = tmp;
            NVRAM->buffer[0x1FF3] = val;
            set_alarm(NVRAM);
        }
        break;
    case 0x1FF4:
        /* alarm hours */
        tmp = from_bcd(val & 0x3F);
        if (tmp >= 0 && tmp <= 23) {
            NVRAM->alarm.tm_hour = tmp;
            NVRAM->buffer[0x1FF4] = val;
            set_alarm(NVRAM);
        }
        break;
    case 0x1FF5:
        /* alarm date */
        tmp = from_bcd(val & 0x1F);
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
	tmp = from_bcd(val & 0x7F);
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
	tmp = from_bcd(val & 0x7F);
	if (tmp >= 0 && tmp <= 59) {
	    get_time(NVRAM, &tm);
	    tm.tm_min = tmp;
	    set_time(NVRAM, &tm);
	}
        break;
    case 0x1FFB:
    case 0x07FB:
        /* hours (BCD) */
	tmp = from_bcd(val & 0x3F);
	if (tmp >= 0 && tmp <= 23) {
	    get_time(NVRAM, &tm);
	    tm.tm_hour = tmp;
	    set_time(NVRAM, &tm);
	}
        break;
    case 0x1FFC:
    case 0x07FC:
        /* day of the week / century */
	tmp = from_bcd(val & 0x07);
	get_time(NVRAM, &tm);
	tm.tm_wday = tmp;
	set_time(NVRAM, &tm);
        NVRAM->buffer[addr] = val & 0x40;
        break;
    case 0x1FFD:
    case 0x07FD:
        /* date */
	tmp = from_bcd(val & 0x1F);
	if (tmp != 0) {
	    get_time(NVRAM, &tm);
	    tm.tm_mday = tmp;
	    set_time(NVRAM, &tm);
	}
        break;
    case 0x1FFE:
    case 0x07FE:
        /* month */
	tmp = from_bcd(val & 0x1F);
	if (tmp >= 1 && tmp <= 12) {
	    get_time(NVRAM, &tm);
	    tm.tm_mon = tmp - 1;
	    set_time(NVRAM, &tm);
	}
        break;
    case 0x1FFF:
    case 0x07FF:
        /* year */
	tmp = from_bcd(val);
	if (tmp >= 0 && tmp <= 99) {
	    get_time(NVRAM, &tm);
            if (NVRAM->type == 8)
                tm.tm_year = from_bcd(val) + 68; // Base year is 1968
            else
                tm.tm_year = from_bcd(val);
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
    M48t59State *NVRAM = opaque;
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
        retval = (NVRAM->buffer[addr] & 0x80) | to_bcd(tm.tm_sec);
        break;
    case 0x1FFA:
    case 0x07FA:
        /* minutes (BCD) */
        get_time(NVRAM, &tm);
        retval = to_bcd(tm.tm_min);
        break;
    case 0x1FFB:
    case 0x07FB:
        /* hours (BCD) */
        get_time(NVRAM, &tm);
        retval = to_bcd(tm.tm_hour);
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
        retval = to_bcd(tm.tm_mday);
        break;
    case 0x1FFE:
    case 0x07FE:
        /* month */
        get_time(NVRAM, &tm);
        retval = to_bcd(tm.tm_mon + 1);
        break;
    case 0x1FFF:
    case 0x07FF:
        /* year */
        get_time(NVRAM, &tm);
        if (NVRAM->type == 8)
            retval = to_bcd(tm.tm_year - 68); // Base year is 1968
        else
            retval = to_bcd(tm.tm_year);
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
    M48t59State *NVRAM = opaque;

    NVRAM->addr = addr;
}

void m48t59_toggle_lock (void *opaque, int lock)
{
    M48t59State *NVRAM = opaque;

    NVRAM->lock ^= 1 << lock;
}

/* IO access to NVRAM */
static void NVRAM_writeb (void *opaque, uint32_t addr, uint32_t val)
{
    M48t59State *NVRAM = opaque;

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
        m48t59_write(NVRAM, NVRAM->addr, val);
        NVRAM->addr = 0x0000;
        break;
    default:
        break;
    }
}

static uint32_t NVRAM_readb (void *opaque, uint32_t addr)
{
    M48t59State *NVRAM = opaque;
    uint32_t retval;

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
    M48t59State *NVRAM = opaque;

    m48t59_write(NVRAM, addr, value & 0xff);
}

static void nvram_writew (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    M48t59State *NVRAM = opaque;

    m48t59_write(NVRAM, addr, (value >> 8) & 0xff);
    m48t59_write(NVRAM, addr + 1, value & 0xff);
}

static void nvram_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    M48t59State *NVRAM = opaque;

    m48t59_write(NVRAM, addr, (value >> 24) & 0xff);
    m48t59_write(NVRAM, addr + 1, (value >> 16) & 0xff);
    m48t59_write(NVRAM, addr + 2, (value >> 8) & 0xff);
    m48t59_write(NVRAM, addr + 3, value & 0xff);
}

static uint32_t nvram_readb (void *opaque, target_phys_addr_t addr)
{
    M48t59State *NVRAM = opaque;
    uint32_t retval;

    retval = m48t59_read(NVRAM, addr);
    return retval;
}

static uint32_t nvram_readw (void *opaque, target_phys_addr_t addr)
{
    M48t59State *NVRAM = opaque;
    uint32_t retval;

    retval = m48t59_read(NVRAM, addr) << 8;
    retval |= m48t59_read(NVRAM, addr + 1);
    return retval;
}

static uint32_t nvram_readl (void *opaque, target_phys_addr_t addr)
{
    M48t59State *NVRAM = opaque;
    uint32_t retval;

    retval = m48t59_read(NVRAM, addr) << 24;
    retval |= m48t59_read(NVRAM, addr + 1) << 16;
    retval |= m48t59_read(NVRAM, addr + 2) << 8;
    retval |= m48t59_read(NVRAM, addr + 3);
    return retval;
}

static CPUWriteMemoryFunc * const nvram_write[] = {
    &nvram_writeb,
    &nvram_writew,
    &nvram_writel,
};

static CPUReadMemoryFunc * const nvram_read[] = {
    &nvram_readb,
    &nvram_readw,
    &nvram_readl,
};

static const VMStateDescription vmstate_m48t59 = {
    .name = "m48t59",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT8(lock, M48t59State),
        VMSTATE_UINT16(addr, M48t59State),
        VMSTATE_VBUFFER_UINT32(buffer, M48t59State, 0, NULL, 0, size),
        VMSTATE_END_OF_LIST()
    }
};

static void m48t59_reset_common(M48t59State *NVRAM)
{
    NVRAM->addr = 0;
    NVRAM->lock = 0;
    if (NVRAM->alrm_timer != NULL)
        qemu_del_timer(NVRAM->alrm_timer);

    if (NVRAM->wd_timer != NULL)
        qemu_del_timer(NVRAM->wd_timer);
}

static void m48t59_reset_isa(DeviceState *d)
{
    M48t59ISAState *isa = container_of(d, M48t59ISAState, busdev.qdev);
    M48t59State *NVRAM = &isa->state;

    m48t59_reset_common(NVRAM);
}

static void m48t59_reset_sysbus(DeviceState *d)
{
    M48t59SysBusState *sys = container_of(d, M48t59SysBusState, busdev.qdev);
    M48t59State *NVRAM = &sys->state;

    m48t59_reset_common(NVRAM);
}

static const MemoryRegionPortio m48t59_portio[] = {
    {0, 4, 1, .read = NVRAM_readb, .write = NVRAM_writeb },
    PORTIO_END_OF_LIST(),
};

static const MemoryRegionOps m48t59_io_ops = {
    .old_portio = m48t59_portio,
};

/* Initialisation routine */
M48t59State *m48t59_init(qemu_irq IRQ, target_phys_addr_t mem_base,
                         uint32_t io_base, uint16_t size, int type)
{
    DeviceState *dev;
    SysBusDevice *s;
    M48t59SysBusState *d;
    M48t59State *state;

    dev = qdev_create(NULL, "m48t59");
    qdev_prop_set_uint32(dev, "type", type);
    qdev_prop_set_uint32(dev, "size", size);
    qdev_prop_set_uint32(dev, "io_base", io_base);
    qdev_init_nofail(dev);
    s = sysbus_from_qdev(dev);
    d = FROM_SYSBUS(M48t59SysBusState, s);
    state = &d->state;
    sysbus_connect_irq(s, 0, IRQ);
    if (io_base != 0) {
        register_ioport_read(io_base, 0x04, 1, NVRAM_readb, state);
        register_ioport_write(io_base, 0x04, 1, NVRAM_writeb, state);
    }
    if (mem_base != 0) {
        sysbus_mmio_map(s, 0, mem_base);
    }

    return state;
}

M48t59State *m48t59_init_isa(uint32_t io_base, uint16_t size, int type)
{
    M48t59ISAState *d;
    ISADevice *dev;
    M48t59State *s;

    dev = isa_create("m48t59_isa");
    qdev_prop_set_uint32(&dev->qdev, "type", type);
    qdev_prop_set_uint32(&dev->qdev, "size", size);
    qdev_prop_set_uint32(&dev->qdev, "io_base", io_base);
    qdev_init_nofail(&dev->qdev);
    d = DO_UPCAST(M48t59ISAState, busdev, dev);
    s = &d->state;

    memory_region_init_io(&d->io, &m48t59_io_ops, s, "m48t59", 4);
    if (io_base != 0) {
        isa_register_ioport(dev, &d->io, io_base);
    }

    return s;
}

static void m48t59_init_common(M48t59State *s)
{
    s->buffer = g_malloc0(s->size);
    if (s->type == 59) {
        s->alrm_timer = qemu_new_timer_ns(vm_clock, &alarm_cb, s);
        s->wd_timer = qemu_new_timer_ns(vm_clock, &watchdog_cb, s);
    }
    qemu_get_timedate(&s->alarm, 0);

    vmstate_register(NULL, -1, &vmstate_m48t59, s);
}

static int m48t59_init_isa1(ISADevice *dev)
{
    M48t59ISAState *d = DO_UPCAST(M48t59ISAState, busdev, dev);
    M48t59State *s = &d->state;

    isa_init_irq(dev, &s->IRQ, 8);
    m48t59_init_common(s);

    return 0;
}

static int m48t59_init1(SysBusDevice *dev)
{
    M48t59SysBusState *d = FROM_SYSBUS(M48t59SysBusState, dev);
    M48t59State *s = &d->state;
    int mem_index;

    sysbus_init_irq(dev, &s->IRQ);

    mem_index = cpu_register_io_memory(nvram_read, nvram_write, s,
                                       DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, s->size, mem_index);
    m48t59_init_common(s);

    return 0;
}

static ISADeviceInfo m48t59_isa_info = {
    .init = m48t59_init_isa1,
    .qdev.name = "m48t59_isa",
    .qdev.size = sizeof(M48t59ISAState),
    .qdev.reset = m48t59_reset_isa,
    .qdev.no_user = 1,
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("size",    M48t59ISAState, state.size,    -1),
        DEFINE_PROP_UINT32("type",    M48t59ISAState, state.type,    -1),
        DEFINE_PROP_HEX32( "io_base", M48t59ISAState, state.io_base,  0),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static SysBusDeviceInfo m48t59_info = {
    .init = m48t59_init1,
    .qdev.name  = "m48t59",
    .qdev.size = sizeof(M48t59SysBusState),
    .qdev.reset = m48t59_reset_sysbus,
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("size",    M48t59SysBusState, state.size,    -1),
        DEFINE_PROP_UINT32("type",    M48t59SysBusState, state.type,    -1),
        DEFINE_PROP_HEX32( "io_base", M48t59SysBusState, state.io_base,  0),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void m48t59_register_devices(void)
{
    sysbus_register_withprop(&m48t59_info);
    isa_qdev_register(&m48t59_isa_info);
}

device_init(m48t59_register_devices)
