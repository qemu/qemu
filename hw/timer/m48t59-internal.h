/*
 * QEMU M48T59 and M48T08 NVRAM emulation (common header)
 *
 * Copyright (c) 2003-2005, 2007 Jocelyn Mayer
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
#ifndef HW_M48T59_INTERNAL_H
#define HW_M48T59_INTERNAL_H 1

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

typedef struct M48txxInfo {
    const char *bus_name;
    uint32_t model; /* 2 = m48t02, 8 = m48t08, 59 = m48t59 */
    uint32_t size;
} M48txxInfo;

typedef struct M48t59State {
    /* Hardware parameters */
    qemu_irq IRQ;
    MemoryRegion iomem;
    uint32_t size;
    int32_t base_year;
    /* RTC management */
    time_t   time_offset;
    time_t   stop_time;
    /* Alarm & watchdog */
    struct tm alarm;
    QEMUTimer *alrm_timer;
    QEMUTimer *wd_timer;
    /* NVRAM storage */
    uint8_t *buffer;
    /* Model parameters */
    uint32_t model; /* 2 = m48t02, 8 = m48t08, 59 = m48t59 */
    /* NVRAM storage */
    uint16_t addr;
    uint8_t  lock;
} M48t59State;

uint32_t m48t59_read(M48t59State *NVRAM, uint32_t addr);
void m48t59_write(M48t59State *NVRAM, uint32_t addr, uint32_t val);
void m48t59_reset_common(M48t59State *NVRAM);
void m48t59_realize_common(M48t59State *s, Error **errp);

static inline void m48t59_toggle_lock(M48t59State *NVRAM, int lock)
{
    NVRAM->lock ^= 1 << lock;
}

extern const MemoryRegionOps m48t59_io_ops;

#endif /* HW_M48T59_INTERNAL_H */
