/*
 * QEMU M48T59 and M48T08 NVRAM emulation for PPC PREP and Sparc platforms
 *
 * Copyright (c) 2003-2005, 2007, 2017 Jocelyn Mayer
 * Copyright (c) 2013 Hervé Poussineau
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
#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/timer/m48t59.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "hw/sysbus.h"
#include "exec/address-spaces.h"
#include "qemu/bcd.h"

#include "m48t59-internal.h"

#define TYPE_M48TXX_SYS_BUS "sysbus-m48txx"
#define M48TXX_SYS_BUS_GET_CLASS(obj) \
    OBJECT_GET_CLASS(M48txxSysBusDeviceClass, (obj), TYPE_M48TXX_SYS_BUS)
#define M48TXX_SYS_BUS_CLASS(klass) \
    OBJECT_CLASS_CHECK(M48txxSysBusDeviceClass, (klass), TYPE_M48TXX_SYS_BUS)
#define M48TXX_SYS_BUS(obj) \
    OBJECT_CHECK(M48txxSysBusState, (obj), TYPE_M48TXX_SYS_BUS)

/*
 * Chipset docs:
 * http://www.st.com/stonline/products/literature/ds/2410/m48t02.pdf
 * http://www.st.com/stonline/products/literature/ds/2411/m48t08.pdf
 * http://www.st.com/stonline/products/literature/od/7001/m48t59y.pdf
 */

typedef struct M48txxSysBusState {
    SysBusDevice parent_obj;
    M48t59State state;
    MemoryRegion io;
} M48txxSysBusState;

typedef struct M48txxSysBusDeviceClass {
    SysBusDeviceClass parent_class;
    M48txxInfo info;
} M48txxSysBusDeviceClass;

static M48txxInfo m48txx_sysbus_info[] = {
    {
        .bus_name = "sysbus-m48t02",
        .model = 2,
        .size = 0x800,
    },{
        .bus_name = "sysbus-m48t08",
        .model = 8,
        .size = 0x2000,
    },{
        .bus_name = "sysbus-m48t59",
        .model = 59,
        .size = 0x2000,
    }
};


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
    timer_mod(NVRAM->alrm_timer, qemu_clock_get_ns(rtc_clock) +
                    next_time * 1000);
    qemu_set_irq(NVRAM->IRQ, 0);
}

static void set_alarm(M48t59State *NVRAM)
{
    int diff;
    if (NVRAM->alrm_timer != NULL) {
        timer_del(NVRAM->alrm_timer);
        diff = qemu_timedate_diff(&NVRAM->alarm) - NVRAM->time_offset;
        if (diff > 0)
            timer_mod(NVRAM->alrm_timer, diff * 1000);
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
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
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
        timer_del(NVRAM->wd_timer);
        if (value != 0) {
            interval = (1 << (2 * (value & 0x03))) * ((value >> 2) & 0x1F);
            timer_mod(NVRAM->wd_timer, ((uint64_t)time(NULL) * 1000) +
                           ((interval * 1000) >> 4));
        }
    }
}

/* Direct access to NVRAM */
void m48t59_write(M48t59State *NVRAM, uint32_t addr, uint32_t val)
{
    struct tm tm;
    int tmp;

    if (addr > 0x1FF8 && addr < 0x2000)
	NVRAM_PRINTF("%s: 0x%08x => 0x%08x\n", __func__, addr, val);

    /* check for NVRAM access */
    if ((NVRAM->model == 2 && addr < 0x7f8) ||
        (NVRAM->model == 8 && addr < 0x1ff8) ||
        (NVRAM->model == 59 && addr < 0x1ff0)) {
        goto do_write;
    }

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
        tmp = from_bcd(val & 0x3F);
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
        /* date (BCD) */
       tmp = from_bcd(val & 0x3F);
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
            tm.tm_year = from_bcd(val) + NVRAM->base_year - 1900;
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

uint32_t m48t59_read(M48t59State *NVRAM, uint32_t addr)
{
    struct tm tm;
    uint32_t retval = 0xFF;

    /* check for NVRAM access */
    if ((NVRAM->model == 2 && addr < 0x078f) ||
        (NVRAM->model == 8 && addr < 0x1ff8) ||
        (NVRAM->model == 59 && addr < 0x1ff0)) {
        goto do_read;
    }

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
        retval = to_bcd((tm.tm_year + 1900 - NVRAM->base_year) % 100);
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

/* IO access to NVRAM */
static void NVRAM_writeb(void *opaque, hwaddr addr, uint64_t val,
                         unsigned size)
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

static uint64_t NVRAM_readb(void *opaque, hwaddr addr, unsigned size)
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

static void nvram_writeb (void *opaque, hwaddr addr, uint32_t value)
{
    M48t59State *NVRAM = opaque;

    m48t59_write(NVRAM, addr, value & 0xff);
}

static void nvram_writew (void *opaque, hwaddr addr, uint32_t value)
{
    M48t59State *NVRAM = opaque;

    m48t59_write(NVRAM, addr, (value >> 8) & 0xff);
    m48t59_write(NVRAM, addr + 1, value & 0xff);
}

static void nvram_writel (void *opaque, hwaddr addr, uint32_t value)
{
    M48t59State *NVRAM = opaque;

    m48t59_write(NVRAM, addr, (value >> 24) & 0xff);
    m48t59_write(NVRAM, addr + 1, (value >> 16) & 0xff);
    m48t59_write(NVRAM, addr + 2, (value >> 8) & 0xff);
    m48t59_write(NVRAM, addr + 3, value & 0xff);
}

static uint32_t nvram_readb (void *opaque, hwaddr addr)
{
    M48t59State *NVRAM = opaque;

    return m48t59_read(NVRAM, addr);
}

static uint32_t nvram_readw (void *opaque, hwaddr addr)
{
    M48t59State *NVRAM = opaque;
    uint32_t retval;

    retval = m48t59_read(NVRAM, addr) << 8;
    retval |= m48t59_read(NVRAM, addr + 1);
    return retval;
}

static uint32_t nvram_readl (void *opaque, hwaddr addr)
{
    M48t59State *NVRAM = opaque;
    uint32_t retval;

    retval = m48t59_read(NVRAM, addr) << 24;
    retval |= m48t59_read(NVRAM, addr + 1) << 16;
    retval |= m48t59_read(NVRAM, addr + 2) << 8;
    retval |= m48t59_read(NVRAM, addr + 3);
    return retval;
}

static const MemoryRegionOps nvram_ops = {
    .old_mmio = {
        .read = { nvram_readb, nvram_readw, nvram_readl, },
        .write = { nvram_writeb, nvram_writew, nvram_writel, },
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_m48t59 = {
    .name = "m48t59",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(lock, M48t59State),
        VMSTATE_UINT16(addr, M48t59State),
        VMSTATE_VBUFFER_UINT32(buffer, M48t59State, 0, NULL, size),
        VMSTATE_END_OF_LIST()
    }
};

void m48t59_reset_common(M48t59State *NVRAM)
{
    NVRAM->addr = 0;
    NVRAM->lock = 0;
    if (NVRAM->alrm_timer != NULL)
        timer_del(NVRAM->alrm_timer);

    if (NVRAM->wd_timer != NULL)
        timer_del(NVRAM->wd_timer);
}

static void m48t59_reset_sysbus(DeviceState *d)
{
    M48txxSysBusState *sys = M48TXX_SYS_BUS(d);
    M48t59State *NVRAM = &sys->state;

    m48t59_reset_common(NVRAM);
}

const MemoryRegionOps m48t59_io_ops = {
    .read = NVRAM_readb,
    .write = NVRAM_writeb,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/* Initialisation routine */
Nvram *m48t59_init(qemu_irq IRQ, hwaddr mem_base,
                   uint32_t io_base, uint16_t size, int base_year,
                   int model)
{
    DeviceState *dev;
    SysBusDevice *s;
    int i;

    for (i = 0; i < ARRAY_SIZE(m48txx_sysbus_info); i++) {
        if (m48txx_sysbus_info[i].size != size ||
            m48txx_sysbus_info[i].model != model) {
            continue;
        }

        dev = qdev_create(NULL, m48txx_sysbus_info[i].bus_name);
        qdev_prop_set_int32(dev, "base-year", base_year);
        qdev_init_nofail(dev);
        s = SYS_BUS_DEVICE(dev);
        sysbus_connect_irq(s, 0, IRQ);
        if (io_base != 0) {
            memory_region_add_subregion(get_system_io(), io_base,
                                        sysbus_mmio_get_region(s, 1));
        }
        if (mem_base != 0) {
            sysbus_mmio_map(s, 0, mem_base);
        }

        return NVRAM(s);
    }

    assert(false);
    return NULL;
}

void m48t59_realize_common(M48t59State *s, Error **errp)
{
    s->buffer = g_malloc0(s->size);
    if (s->model == 59) {
        s->alrm_timer = timer_new_ns(rtc_clock, &alarm_cb, s);
        s->wd_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, &watchdog_cb, s);
    }
    qemu_get_timedate(&s->alarm, 0);
}

static void m48t59_init1(Object *obj)
{
    M48txxSysBusDeviceClass *u = M48TXX_SYS_BUS_GET_CLASS(obj);
    M48txxSysBusState *d = M48TXX_SYS_BUS(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);
    M48t59State *s = &d->state;

    s->model = u->info.model;
    s->size = u->info.size;
    sysbus_init_irq(dev, &s->IRQ);

    memory_region_init_io(&s->iomem, obj, &nvram_ops, s, "m48t59.nvram",
                          s->size);
    memory_region_init_io(&d->io, obj, &m48t59_io_ops, s, "m48t59", 4);
}

static void m48t59_realize(DeviceState *dev, Error **errp)
{
    M48txxSysBusState *d = M48TXX_SYS_BUS(dev);
    M48t59State *s = &d->state;
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_mmio(sbd, &d->io);
    m48t59_realize_common(s, errp);
}

static uint32_t m48txx_sysbus_read(Nvram *obj, uint32_t addr)
{
    M48txxSysBusState *d = M48TXX_SYS_BUS(obj);
    return m48t59_read(&d->state, addr);
}

static void m48txx_sysbus_write(Nvram *obj, uint32_t addr, uint32_t val)
{
    M48txxSysBusState *d = M48TXX_SYS_BUS(obj);
    m48t59_write(&d->state, addr, val);
}

static void m48txx_sysbus_toggle_lock(Nvram *obj, int lock)
{
    M48txxSysBusState *d = M48TXX_SYS_BUS(obj);
    m48t59_toggle_lock(&d->state, lock);
}

static Property m48t59_sysbus_properties[] = {
    DEFINE_PROP_INT32("base-year", M48txxSysBusState, state.base_year, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void m48txx_sysbus_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    NvramClass *nc = NVRAM_CLASS(klass);

    dc->realize = m48t59_realize;
    dc->reset = m48t59_reset_sysbus;
    dc->props = m48t59_sysbus_properties;
    dc->vmsd = &vmstate_m48t59;
    nc->read = m48txx_sysbus_read;
    nc->write = m48txx_sysbus_write;
    nc->toggle_lock = m48txx_sysbus_toggle_lock;
}

static void m48txx_sysbus_concrete_class_init(ObjectClass *klass, void *data)
{
    M48txxSysBusDeviceClass *u = M48TXX_SYS_BUS_CLASS(klass);
    M48txxInfo *info = data;

    u->info = *info;
}

static const TypeInfo nvram_info = {
    .name = TYPE_NVRAM,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(NvramClass),
};

static const TypeInfo m48txx_sysbus_type_info = {
    .name = TYPE_M48TXX_SYS_BUS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(M48txxSysBusState),
    .instance_init = m48t59_init1,
    .abstract = true,
    .class_init = m48txx_sysbus_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_NVRAM },
        { }
    }
};

static void m48t59_register_types(void)
{
    TypeInfo sysbus_type_info = {
        .parent = TYPE_M48TXX_SYS_BUS,
        .class_size = sizeof(M48txxSysBusDeviceClass),
        .class_init = m48txx_sysbus_concrete_class_init,
    };
    int i;

    type_register_static(&nvram_info);
    type_register_static(&m48txx_sysbus_type_info);

    for (i = 0; i < ARRAY_SIZE(m48txx_sysbus_info); i++) {
        sysbus_type_info.name = m48txx_sysbus_info[i].bus_name;
        sysbus_type_info.class_data = &m48txx_sysbus_info[i];
        type_register(&sysbus_type_info);
    }
}

type_init(m48t59_register_types)
