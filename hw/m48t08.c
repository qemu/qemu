/*
 * QEMU M48T08 NVRAM emulation for Sparc platform
 * 
 * Copyright (c) 2003-2004 Jocelyn Mayer
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
#include "m48t08.h"

//#define DEBUG_NVRAM

#if defined(DEBUG_NVRAM)
#define NVRAM_PRINTF(fmt, args...) do { printf(fmt , ##args); } while (0)
#else
#define NVRAM_PRINTF(fmt, args...) do { } while (0)
#endif

#define NVRAM_MAX_MEM 0xfff0

struct m48t08_t {
    /* Hardware parameters */
    int mem_index;
    uint32_t mem_base;
    uint16_t size;
    /* RTC management */
    time_t   time_offset;
    time_t   stop_time;
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
static void get_time (m48t08_t *NVRAM, struct tm *tm)
{
    time_t t;

    t = time(NULL) + NVRAM->time_offset;
#ifdef _WIN32
    memcpy(tm,localtime(&t),sizeof(*tm));
#else
    localtime_r (&t, tm) ;
#endif
}

static void set_time (m48t08_t *NVRAM, struct tm *tm)
{
    time_t now, new_time;
    
    new_time = mktime(tm);
    now = time(NULL);
    NVRAM->time_offset = new_time - now;
}

/* Direct access to NVRAM */
void m48t08_write (m48t08_t *NVRAM, uint32_t val)
{
    struct tm tm;
    int tmp;

    if (NVRAM->addr > NVRAM_MAX_MEM && NVRAM->addr < 0x2000)
	NVRAM_PRINTF("%s: 0x%08x => 0x%08x\n", __func__, NVRAM->addr, val);
    switch (NVRAM->addr) {
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
	    tm.tm_year = fromBCD(val);
	    set_time(NVRAM, &tm);
	}
        break;
    default:
        /* Check lock registers state */
        if (NVRAM->addr >= 0x20 && NVRAM->addr <= 0x2F && (NVRAM->lock & 1))
            break;
        if (NVRAM->addr >= 0x30 && NVRAM->addr <= 0x3F && (NVRAM->lock & 2))
            break;
        if (NVRAM->addr < NVRAM_MAX_MEM ||
	    (NVRAM->addr > 0x1FFF && NVRAM->addr < NVRAM->size)) {
            NVRAM->buffer[NVRAM->addr] = val & 0xFF;
	}
        break;
    }
}

uint32_t m48t08_read (m48t08_t *NVRAM)
{
    struct tm tm;
    uint32_t retval = 0xFF;

    switch (NVRAM->addr) {
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
        retval = toBCD(tm.tm_year);
        break;
    default:
        /* Check lock registers state */
        if (NVRAM->addr >= 0x20 && NVRAM->addr <= 0x2F && (NVRAM->lock & 1))
            break;
        if (NVRAM->addr >= 0x30 && NVRAM->addr <= 0x3F && (NVRAM->lock & 2))
            break;
        if (NVRAM->addr < NVRAM_MAX_MEM ||
	    (NVRAM->addr > 0x1FFF && NVRAM->addr < NVRAM->size)) {
	do_read:
            retval = NVRAM->buffer[NVRAM->addr];
	}
        break;
    }
    if (NVRAM->addr > NVRAM_MAX_MEM + 1 && NVRAM->addr < 0x2000)
	NVRAM_PRINTF("0x%08x <= 0x%08x\n", NVRAM->addr, retval);

    return retval;
}

void m48t08_set_addr (m48t08_t *NVRAM, uint32_t addr)
{
    NVRAM->addr = addr;
}

void m48t08_toggle_lock (m48t08_t *NVRAM, int lock)
{
    NVRAM->lock ^= 1 << lock;
}

static void nvram_writeb (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    m48t08_t *NVRAM = opaque;
    
    addr -= NVRAM->mem_base;
    if (addr < NVRAM_MAX_MEM)
        NVRAM->buffer[addr] = value;
}

static void nvram_writew (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    m48t08_t *NVRAM = opaque;
    
    addr -= NVRAM->mem_base;
    if (addr < NVRAM_MAX_MEM) {
        NVRAM->buffer[addr] = value >> 8;
        NVRAM->buffer[addr + 1] = value;
    }
}

static void nvram_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
    m48t08_t *NVRAM = opaque;
    
    addr -= NVRAM->mem_base;
    if (addr < NVRAM_MAX_MEM) {
        NVRAM->buffer[addr] = value >> 24;
        NVRAM->buffer[addr + 1] = value >> 16;
        NVRAM->buffer[addr + 2] = value >> 8;
        NVRAM->buffer[addr + 3] = value;
    }
}

static uint32_t nvram_readb (void *opaque, target_phys_addr_t addr)
{
    m48t08_t *NVRAM = opaque;
    uint32_t retval = 0;
    
    addr -= NVRAM->mem_base;
    if (addr < NVRAM_MAX_MEM)
        retval = NVRAM->buffer[addr];

    return retval;
}

static uint32_t nvram_readw (void *opaque, target_phys_addr_t addr)
{
    m48t08_t *NVRAM = opaque;
    uint32_t retval = 0;
    
    addr -= NVRAM->mem_base;
    if (addr < NVRAM_MAX_MEM) {
        retval = NVRAM->buffer[addr] << 8;
        retval |= NVRAM->buffer[addr + 1];
    }

    return retval;
}

static uint32_t nvram_readl (void *opaque, target_phys_addr_t addr)
{
    m48t08_t *NVRAM = opaque;
    uint32_t retval = 0;
    
    addr -= NVRAM->mem_base;
    if (addr < NVRAM_MAX_MEM) {
        retval = NVRAM->buffer[addr] << 24;
        retval |= NVRAM->buffer[addr + 1] << 16;
        retval |= NVRAM->buffer[addr + 2] << 8;
        retval |= NVRAM->buffer[addr + 3];
    }

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
m48t08_t *m48t08_init(uint32_t mem_base, uint16_t size, uint8_t *macaddr)
{
    m48t08_t *s;
    int i;
    unsigned char tmp = 0;

    s = qemu_mallocz(sizeof(m48t08_t));
    if (!s)
	return NULL;
    s->buffer = qemu_mallocz(size);
    if (!s->buffer) {
        qemu_free(s);
        return NULL;
    }
    s->size = size;
    s->mem_base = mem_base;
    s->addr = 0;
    if (mem_base != 0) {
        s->mem_index = cpu_register_io_memory(0, nvram_read, nvram_write, s);
        cpu_register_physical_memory(mem_base, 0x4000, s->mem_index);
    }
    s->lock = 0;

    i = 0x1fd8;
    s->buffer[i++] = 0x01;
    s->buffer[i++] = 0x80; /* Sun4m OBP */
    memcpy(&s->buffer[i], macaddr, 6);

    /* Calculate checksum */
    for (i = 0x1fd8; i < 0x1fe7; i++) {
	tmp ^= s->buffer[i];
    }
    s->buffer[0x1fe7] = tmp;
    return s;
}

#if 0
struct idprom
{
        unsigned char   id_format;      /* Format identifier (always 0x01) */
        unsigned char   id_machtype;    /* Machine type */
        unsigned char   id_ethaddr[6];  /* Hardware ethernet address */
        long            id_date;        /* Date of manufacture */
        unsigned int    id_sernum:24;   /* Unique serial number */
        unsigned char   id_cksum;       /* Checksum - xor of the data bytes */
        unsigned char   reserved[16];
};
#endif
