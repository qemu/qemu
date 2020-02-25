/*
 * NeXT Cube System Driver
 *
 * Copyright (c) 2011 Bryce Lanham
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/hwaddr.h"
#include "exec/address-spaces.h"
#include "sysemu/sysemu.h"
#include "sysemu/qtest.h"
#include "hw/irq.h"
#include "hw/m68k/next-cube.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/scsi/esp.h"
#include "hw/sysbus.h"
#include "hw/char/escc.h" /* ZILOG 8530 Serial Emulation */
#include "hw/block/fdc.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "ui/console.h"
#include "target/m68k/cpu.h"

/* #define DEBUG_NEXT */
#ifdef DEBUG_NEXT
#define DPRINTF(fmt, ...) \
    do { printf("NeXT: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do { } while (0)
#endif

#define TYPE_NEXT_MACHINE MACHINE_TYPE_NAME("next-cube")
#define NEXT_MACHINE(obj) OBJECT_CHECK(NeXTState, (obj), TYPE_NEXT_MACHINE)

#define ENTRY       0x0100001e
#define RAM_SIZE    0x4000000
#define ROM_FILE    "Rev_2.5_v66.bin"

typedef struct next_dma {
    uint32_t csr;

    uint32_t saved_next;
    uint32_t saved_limit;
    uint32_t saved_start;
    uint32_t saved_stop;

    uint32_t next;
    uint32_t limit;
    uint32_t start;
    uint32_t stop;

    uint32_t next_initbuf;
    uint32_t size;
} next_dma;

typedef struct NextRtc {
    uint8_t ram[32];
    uint8_t command;
    uint8_t value;
    uint8_t status;
    uint8_t control;
    uint8_t retval;
} NextRtc;

typedef struct {
    MachineState parent;

    uint32_t int_mask;
    uint32_t int_status;

    uint8_t scsi_csr_1;
    uint8_t scsi_csr_2;
    next_dma dma[10];
    qemu_irq *scsi_irq;
    qemu_irq scsi_dma;
    qemu_irq scsi_reset;
    qemu_irq *fd_irq;

    uint32_t scr1;
    uint32_t scr2;

    NextRtc rtc;
} NeXTState;

/* Thanks to NeXT forums for this */
/*
static const uint8_t rtc_ram3[32] = {
    0x94, 0x0f, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xfb, 0x6d, 0x00, 0x00, 0x7B, 0x00,
    0x00, 0x00, 0x65, 0x6e, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x13
};
*/
static const uint8_t rtc_ram2[32] = {
    0x94, 0x0f, 0x40, 0x03, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xfb, 0x6d, 0x00, 0x00, 0x4b, 0x00,
    0x41, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x84, 0x7e,
};

#define SCR2_RTCLK 0x2
#define SCR2_RTDATA 0x4
#define SCR2_TOBCD(x) (((x / 10) << 4) + (x % 10))

static void nextscr2_write(NeXTState *s, uint32_t val, int size)
{
    static int led;
    static int phase;
    static uint8_t old_scr2;
    uint8_t scr2_2;
    NextRtc *rtc = &s->rtc;

    if (size == 4) {
        scr2_2 = (val >> 8) & 0xFF;
    } else {
        scr2_2 = val & 0xFF;
    }

    if (val & 0x1) {
        DPRINTF("fault!\n");
        led++;
        if (led == 10) {
            DPRINTF("LED flashing, possible fault!\n");
            led = 0;
        }
    }

    if (scr2_2 & 0x1) {
        /* DPRINTF("RTC %x phase %i\n", scr2_2, phase); */
        if (phase == -1) {
            phase = 0;
        }
        /* If we are in going down clock... do something */
        if (((old_scr2 & SCR2_RTCLK) != (scr2_2 & SCR2_RTCLK)) &&
                ((scr2_2 & SCR2_RTCLK) == 0)) {
            if (phase < 8) {
                rtc->command = (rtc->command << 1) |
                               ((scr2_2 & SCR2_RTDATA) ? 1 : 0);
            }
            if (phase >= 8 && phase < 16) {
                rtc->value = (rtc->value << 1) |
                             ((scr2_2 & SCR2_RTDATA) ? 1 : 0);

                /* if we read RAM register, output RT_DATA bit */
                if (rtc->command <= 0x1F) {
                    scr2_2 = scr2_2 & (~SCR2_RTDATA);
                    if (rtc->ram[rtc->command] & (0x80 >> (phase - 8))) {
                        scr2_2 |= SCR2_RTDATA;
                    }

                    rtc->retval = (rtc->retval << 1) |
                                  ((scr2_2 & SCR2_RTDATA) ? 1 : 0);
                }
                /* read the status 0x30 */
                if (rtc->command == 0x30) {
                    scr2_2 = scr2_2 & (~SCR2_RTDATA);
                    /* for now status = 0x98 (new rtc + FTU) */
                    if (rtc->status & (0x80 >> (phase - 8))) {
                        scr2_2 |= SCR2_RTDATA;
                    }

                    rtc->retval = (rtc->retval << 1) |
                                  ((scr2_2 & SCR2_RTDATA) ? 1 : 0);
                }
                /* read the status 0x31 */
                if (rtc->command == 0x31) {
                    scr2_2 = scr2_2 & (~SCR2_RTDATA);
                    if (rtc->control & (0x80 >> (phase - 8))) {
                        scr2_2 |= SCR2_RTDATA;
                    }
                    rtc->retval = (rtc->retval << 1) |
                                  ((scr2_2 & SCR2_RTDATA) ? 1 : 0);
                }

                if ((rtc->command >= 0x20) && (rtc->command <= 0x2F)) {
                    scr2_2 = scr2_2 & (~SCR2_RTDATA);
                    /* for now 0x00 */
                    time_t time_h = time(NULL);
                    struct tm *info = localtime(&time_h);
                    int ret = 0;

                    switch (rtc->command) {
                    case 0x20:
                        ret = SCR2_TOBCD(info->tm_sec);
                        break;
                    case 0x21:
                        ret = SCR2_TOBCD(info->tm_min);
                        break;
                    case 0x22:
                        ret = SCR2_TOBCD(info->tm_hour);
                        break;
                    case 0x24:
                        ret = SCR2_TOBCD(info->tm_mday);
                        break;
                    case 0x25:
                        ret = SCR2_TOBCD((info->tm_mon + 1));
                        break;
                    case 0x26:
                        ret = SCR2_TOBCD((info->tm_year - 100));
                        break;

                    }

                    if (ret & (0x80 >> (phase - 8))) {
                        scr2_2 |= SCR2_RTDATA;
                    }
                    rtc->retval = (rtc->retval << 1) |
                                  ((scr2_2 & SCR2_RTDATA) ? 1 : 0);
                }

            }

            phase++;
            if (phase == 16) {
                if (rtc->command >= 0x80 && rtc->command <= 0x9F) {
                    rtc->ram[rtc->command - 0x80] = rtc->value;
                }
                /* write to x30 register */
                if (rtc->command == 0xB1) {
                    /* clear FTU */
                    if (rtc->value & 0x04) {
                        rtc->status = rtc->status & (~0x18);
                        s->int_status = s->int_status & (~0x04);
                    }
                }
            }
        }
    } else {
        /* else end or abort */
        phase = -1;
        rtc->command = 0;
        rtc->value = 0;
    }
    s->scr2 = val & 0xFFFF00FF;
    s->scr2 |= scr2_2 << 8;
    old_scr2 = scr2_2;
}

static uint32_t mmio_readb(NeXTState *s, hwaddr addr)
{
    switch (addr) {
    case 0xc000:
        return (s->scr1 >> 24) & 0xFF;
    case 0xc001:
        return (s->scr1 >> 16) & 0xFF;
    case 0xc002:
        return (s->scr1 >> 8)  & 0xFF;
    case 0xc003:
        return (s->scr1 >> 0)  & 0xFF;

    case 0xd000:
        return (s->scr2 >> 24) & 0xFF;
    case 0xd001:
        return (s->scr2 >> 16) & 0xFF;
    case 0xd002:
        return (s->scr2 >> 8)  & 0xFF;
    case 0xd003:
        return (s->scr2 >> 0)  & 0xFF;
    case 0x14020:
        DPRINTF("MMIO Read 0x4020\n");
        return 0x7f;

    default:
        DPRINTF("MMIO Read B @ %"HWADDR_PRIx"\n", addr);
        return 0x0;
    }
}

static uint32_t mmio_readw(NeXTState *s, hwaddr addr)
{
    switch (addr) {
    default:
        DPRINTF("MMIO Read W @ %"HWADDR_PRIx"\n", addr);
        return 0x0;
    }
}

static uint32_t mmio_readl(NeXTState *s, hwaddr addr)
{
    switch (addr) {
    case 0x7000:
        /* DPRINTF("Read INT status: %x\n", s->int_status); */
        return s->int_status;

    case 0x7800:
        DPRINTF("MMIO Read INT mask: %x\n", s->int_mask);
        return s->int_mask;

    case 0xc000:
        return s->scr1;

    case 0xd000:
        return s->scr2;

    default:
        DPRINTF("MMIO Read L @ %"HWADDR_PRIx"\n", addr);
        return 0x0;
    }
}

static void mmio_writeb(NeXTState *s, hwaddr addr, uint32_t val)
{
    switch (addr) {
    case 0xd003:
        nextscr2_write(s, val, 1);
        break;
    default:
        DPRINTF("MMIO Write B @ %x with %x\n", (unsigned int)addr, val);
    }

}

static void mmio_writew(NeXTState *s, hwaddr addr, uint32_t val)
{
    DPRINTF("MMIO Write W\n");
}

static void mmio_writel(NeXTState *s, hwaddr addr, uint32_t val)
{
    switch (addr) {
    case 0x7000:
        DPRINTF("INT Status old: %x new: %x\n", s->int_status, val);
        s->int_status = val;
        break;
    case 0x7800:
        DPRINTF("INT Mask old: %x new: %x\n", s->int_mask, val);
        s->int_mask  = val;
        break;
    case 0xc000:
        DPRINTF("SCR1 Write: %x\n", val);
        break;
    case 0xd000:
        nextscr2_write(s, val, 4);
        break;

    default:
        DPRINTF("MMIO Write l @ %x with %x\n", (unsigned int)addr, val);
    }
}

static uint64_t mmio_readfn(void *opaque, hwaddr addr, unsigned size)
{
    NeXTState *ns = NEXT_MACHINE(opaque);

    switch (size) {
    case 1:
        return mmio_readb(ns, addr);
    case 2:
        return mmio_readw(ns, addr);
    case 4:
        return mmio_readl(ns, addr);
    default:
        g_assert_not_reached();
    }
}

static void mmio_writefn(void *opaque, hwaddr addr, uint64_t value,
                         unsigned size)
{
    NeXTState *ns = NEXT_MACHINE(opaque);

    switch (size) {
    case 1:
        mmio_writeb(ns, addr, value);
        break;
    case 2:
        mmio_writew(ns, addr, value);
        break;
    case 4:
        mmio_writel(ns, addr, value);
        break;
    default:
        g_assert_not_reached();
    }
}

static const MemoryRegionOps mmio_ops = {
    .read = mmio_readfn,
    .write = mmio_writefn,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static uint32_t scr_readb(NeXTState *s, hwaddr addr)
{
    switch (addr) {
    case 0x14108:
        DPRINTF("FD read @ %x\n", (unsigned int)addr);
        return 0x40 | 0x04 | 0x2 | 0x1;
    case 0x14020:
        DPRINTF("SCSI 4020  STATUS READ %X\n", s->scsi_csr_1);
        return s->scsi_csr_1;

    case 0x14021:
        DPRINTF("SCSI 4021 STATUS READ %X\n", s->scsi_csr_2);
        return 0x40;

    /*
     * These 4 registers are the hardware timer, not sure which register
     * is the latch instead of data, but no problems so far
     */
    case 0x1a000:
        return 0xff & (clock() >> 24);
    case 0x1a001:
        return 0xff & (clock() >> 16);
    case 0x1a002:
        return 0xff & (clock() >> 8);
    case 0x1a003:
        /* Hack: We need to have this change consistently to make it work */
        return 0xFF & clock();

    default:
        DPRINTF("BMAP Read B @ %x\n", (unsigned int)addr);
        return 0;
    }
}

static uint32_t scr_readw(NeXTState *s, hwaddr addr)
{
    DPRINTF("BMAP Read W @ %x\n", (unsigned int)addr);
    return 0;
}

static uint32_t scr_readl(NeXTState *s, hwaddr addr)
{
    DPRINTF("BMAP Read L @ %x\n", (unsigned int)addr);
    return 0;
}

#define SCSICSR_ENABLE  0x01
#define SCSICSR_RESET   0x02  /* reset scsi dma */
#define SCSICSR_FIFOFL  0x04
#define SCSICSR_DMADIR  0x08  /* if set, scsi to mem */
#define SCSICSR_CPUDMA  0x10  /* if set, dma enabled */
#define SCSICSR_INTMASK 0x20  /* if set, interrupt enabled */

static void scr_writeb(NeXTState *s, hwaddr addr, uint32_t value)
{
    switch (addr) {
    case 0x14108:
        DPRINTF("FDCSR Write: %x\n", value);

        if (value == 0x0) {
            /* qemu_irq_raise(s->fd_irq[0]); */
        }
        break;
    case 0x14020: /* SCSI Control Register */
        if (value & SCSICSR_FIFOFL) {
            DPRINTF("SCSICSR FIFO Flush\n");
            /* will have to add another irq to the esp if this is needed */
            /* esp_puflush_fifo(esp_g); */
            /* qemu_irq_pulse(s->scsi_dma); */
        }

        if (value & SCSICSR_ENABLE) {
            DPRINTF("SCSICSR Enable\n");
            /*
             * qemu_irq_raise(s->scsi_dma);
             * s->scsi_csr_1 = 0xc0;
             * s->scsi_csr_1 |= 0x1;
             * qemu_irq_pulse(s->scsi_dma);
             */
        }
        /*
         * else
         *     s->scsi_csr_1 &= ~SCSICSR_ENABLE;
         */

        if (value & SCSICSR_RESET) {
            DPRINTF("SCSICSR Reset\n");
            /* I think this should set DMADIR. CPUDMA and INTMASK to 0 */
            /* qemu_irq_raise(s->scsi_reset); */
            /* s->scsi_csr_1 &= ~(SCSICSR_INTMASK |0x80|0x1); */

        }
        if (value & SCSICSR_DMADIR) {
            DPRINTF("SCSICSR DMAdir\n");
        }
        if (value & SCSICSR_CPUDMA) {
            DPRINTF("SCSICSR CPUDMA\n");
            /* qemu_irq_raise(s->scsi_dma); */

            s->int_status |= 0x4000000;
        } else {
            s->int_status &= ~(0x4000000);
        }
        if (value & SCSICSR_INTMASK) {
            DPRINTF("SCSICSR INTMASK\n");
            /*
             * int_mask &= ~0x1000;
             * s->scsi_csr_1 |= value;
             * s->scsi_csr_1 &= ~SCSICSR_INTMASK;
             * if (s->scsi_queued) {
             *     s->scsi_queued = 0;
             *     next_irq(s, NEXT_SCSI_I, level);
             * }
             */
        } else {
            /* int_mask |= 0x1000; */
        }
        if (value & 0x80) {
            /* int_mask |= 0x1000; */
            /* s->scsi_csr_1 |= 0x80; */
        }
        DPRINTF("SCSICSR Write: %x\n", value);
        /* s->scsi_csr_1 = value; */
        return;
    /* Hardware timer latch - not implemented yet */
    case 0x1a000:
    default:
        DPRINTF("BMAP Write B @ %x with %x\n", (unsigned int)addr, value);
    }
}

static void scr_writew(NeXTState *s, hwaddr addr, uint32_t value)
{
    DPRINTF("BMAP Write W @ %x with %x\n", (unsigned int)addr, value);
}

static void scr_writel(NeXTState *s, hwaddr addr, uint32_t value)
{
    DPRINTF("BMAP Write L @ %x with %x\n", (unsigned int)addr, value);
}

static uint64_t scr_readfn(void *opaque, hwaddr addr, unsigned size)
{
    NeXTState *ns = NEXT_MACHINE(opaque);

    switch (size) {
    case 1:
        return scr_readb(ns, addr);
    case 2:
        return scr_readw(ns, addr);
    case 4:
        return scr_readl(ns, addr);
    default:
        g_assert_not_reached();
    }
}

static void scr_writefn(void *opaque, hwaddr addr, uint64_t value,
                        unsigned size)
{
    NeXTState *ns = NEXT_MACHINE(opaque);

    switch (size) {
    case 1:
        scr_writeb(ns, addr, value);
        break;
    case 2:
        scr_writew(ns, addr, value);
        break;
    case 4:
        scr_writel(ns, addr, value);
        break;
    default:
        g_assert_not_reached();
    }
}

static const MemoryRegionOps scr_ops = {
    .read = scr_readfn,
    .write = scr_writefn,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

#define NEXTDMA_SCSI(x)      (0x10 + x)
#define NEXTDMA_FD(x)        (0x10 + x)
#define NEXTDMA_ENTX(x)      (0x110 + x)
#define NEXTDMA_ENRX(x)      (0x150 + x)
#define NEXTDMA_CSR          0x0
#define NEXTDMA_NEXT         0x4000
#define NEXTDMA_LIMIT        0x4004
#define NEXTDMA_START        0x4008
#define NEXTDMA_STOP         0x400c
#define NEXTDMA_NEXT_INIT    0x4200
#define NEXTDMA_SIZE         0x4204

static void dma_writel(void *opaque, hwaddr addr, uint64_t value,
                       unsigned int size)
{
    NeXTState *next_state = NEXT_MACHINE(opaque);

    switch (addr) {
    case NEXTDMA_ENRX(NEXTDMA_CSR):
        if (value & DMA_DEV2M) {
            next_state->dma[NEXTDMA_ENRX].csr |= DMA_DEV2M;
        }

        if (value & DMA_SETENABLE) {
            /* DPRINTF("SCSI DMA ENABLE\n"); */
            next_state->dma[NEXTDMA_ENRX].csr |= DMA_ENABLE;
        }
        if (value & DMA_SETSUPDATE) {
            next_state->dma[NEXTDMA_ENRX].csr |= DMA_SUPDATE;
        }
        if (value & DMA_CLRCOMPLETE) {
            next_state->dma[NEXTDMA_ENRX].csr &= ~DMA_COMPLETE;
        }

        if (value & DMA_RESET) {
            next_state->dma[NEXTDMA_ENRX].csr &= ~(DMA_COMPLETE | DMA_SUPDATE |
                                                  DMA_ENABLE | DMA_DEV2M);
        }
        /* DPRINTF("RXCSR \tWrite: %x\n",value); */
        break;
    case NEXTDMA_ENRX(NEXTDMA_NEXT_INIT):
        next_state->dma[NEXTDMA_ENRX].next_initbuf = value;
        break;
    case NEXTDMA_ENRX(NEXTDMA_NEXT):
        next_state->dma[NEXTDMA_ENRX].next = value;
        break;
    case NEXTDMA_ENRX(NEXTDMA_LIMIT):
        next_state->dma[NEXTDMA_ENRX].limit = value;
        break;
    case NEXTDMA_SCSI(NEXTDMA_CSR):
        if (value & DMA_DEV2M) {
            next_state->dma[NEXTDMA_SCSI].csr |= DMA_DEV2M;
        }
        if (value & DMA_SETENABLE) {
            /* DPRINTF("SCSI DMA ENABLE\n"); */
            next_state->dma[NEXTDMA_SCSI].csr |= DMA_ENABLE;
        }
        if (value & DMA_SETSUPDATE) {
            next_state->dma[NEXTDMA_SCSI].csr |= DMA_SUPDATE;
        }
        if (value & DMA_CLRCOMPLETE) {
            next_state->dma[NEXTDMA_SCSI].csr &= ~DMA_COMPLETE;
        }

        if (value & DMA_RESET) {
            next_state->dma[NEXTDMA_SCSI].csr &= ~(DMA_COMPLETE | DMA_SUPDATE |
                                                  DMA_ENABLE | DMA_DEV2M);
            /* DPRINTF("SCSI DMA RESET\n"); */
        }
        /* DPRINTF("RXCSR \tWrite: %x\n",value); */
        break;

    case NEXTDMA_SCSI(NEXTDMA_NEXT):
        next_state->dma[NEXTDMA_SCSI].next = value;
        break;

    case NEXTDMA_SCSI(NEXTDMA_LIMIT):
        next_state->dma[NEXTDMA_SCSI].limit = value;
        break;

    case NEXTDMA_SCSI(NEXTDMA_START):
        next_state->dma[NEXTDMA_SCSI].start = value;
        break;

    case NEXTDMA_SCSI(NEXTDMA_STOP):
        next_state->dma[NEXTDMA_SCSI].stop = value;
        break;

    case NEXTDMA_SCSI(NEXTDMA_NEXT_INIT):
        next_state->dma[NEXTDMA_SCSI].next_initbuf = value;
        break;

    default:
        DPRINTF("DMA write @ %x w/ %x\n", (unsigned)addr, (unsigned)value);
    }
}

static uint64_t dma_readl(void *opaque, hwaddr addr, unsigned int size)
{
    NeXTState *next_state = NEXT_MACHINE(opaque);

    switch (addr) {
    case NEXTDMA_SCSI(NEXTDMA_CSR):
        DPRINTF("SCSI DMA CSR READ\n");
        return next_state->dma[NEXTDMA_SCSI].csr;
    case NEXTDMA_ENRX(NEXTDMA_CSR):
        return next_state->dma[NEXTDMA_ENRX].csr;
    case NEXTDMA_ENRX(NEXTDMA_NEXT_INIT):
        return next_state->dma[NEXTDMA_ENRX].next_initbuf;
    case NEXTDMA_ENRX(NEXTDMA_NEXT):
        return next_state->dma[NEXTDMA_ENRX].next;
    case NEXTDMA_ENRX(NEXTDMA_LIMIT):
        return next_state->dma[NEXTDMA_ENRX].limit;

    case NEXTDMA_SCSI(NEXTDMA_NEXT):
        return next_state->dma[NEXTDMA_SCSI].next;
    case NEXTDMA_SCSI(NEXTDMA_NEXT_INIT):
        return next_state->dma[NEXTDMA_SCSI].next_initbuf;
    case NEXTDMA_SCSI(NEXTDMA_LIMIT):
        return next_state->dma[NEXTDMA_SCSI].limit;
    case NEXTDMA_SCSI(NEXTDMA_START):
        return next_state->dma[NEXTDMA_SCSI].start;
    case NEXTDMA_SCSI(NEXTDMA_STOP):
        return next_state->dma[NEXTDMA_SCSI].stop;

    default:
        DPRINTF("DMA read @ %x\n", (unsigned int)addr);
        return 0;
    }

    /*
     * once the csr's are done, subtract 0x3FEC from the addr, and that will
     * normalize the upper registers
     */
}

static const MemoryRegionOps dma_ops = {
    .read = dma_readl,
    .write = dma_writel,
    .impl.min_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/*
 * TODO: set the shift numbers as values in the enum, so the first switch
 * will not be needed
 */
void next_irq(void *opaque, int number, int level)
{
    M68kCPU *cpu = opaque;
    int shift = 0;
    NeXTState *ns = NEXT_MACHINE(qdev_get_machine());

    /* first switch sets interupt status */
    /* DPRINTF("IRQ %i\n",number); */
    switch (number) {
    /* level 3 - floppy, kbd/mouse, power, ether rx/tx, scsi, clock */
    case NEXT_FD_I:
        shift = 7;
        break;
    case NEXT_KBD_I:
        shift = 3;
        break;
    case NEXT_PWR_I:
        shift = 2;
        break;
    case NEXT_ENRX_I:
        shift = 9;
        break;
    case NEXT_ENTX_I:
        shift = 10;
        break;
    case NEXT_SCSI_I:
        shift = 12;
        break;
    case NEXT_CLK_I:
        shift = 5;
        break;

    /* level 5 - scc (serial) */
    case NEXT_SCC_I:
        shift = 17;
        break;

    /* level 6 - audio etherrx/tx dma */
    case NEXT_ENTX_DMA_I:
        shift = 28;
        break;
    case NEXT_ENRX_DMA_I:
        shift = 27;
        break;
    case NEXT_SCSI_DMA_I:
        shift = 26;
        break;
    case NEXT_SND_I:
        shift = 23;
        break;
    case NEXT_SCC_DMA_I:
        shift = 21;
        break;

    }
    /*
     * this HAS to be wrong, the interrupt handlers in mach and together
     * int_status and int_mask and return if there is a hit
     */
    if (ns->int_mask & (1 << shift)) {
        DPRINTF("%x interrupt masked @ %x\n", 1 << shift, cpu->env.pc);
        /* return; */
    }

    /* second switch triggers the correct interrupt */
    if (level) {
        ns->int_status |= 1 << shift;

        switch (number) {
        /* level 3 - floppy, kbd/mouse, power, ether rx/tx, scsi, clock */
        case NEXT_FD_I:
        case NEXT_KBD_I:
        case NEXT_PWR_I:
        case NEXT_ENRX_I:
        case NEXT_ENTX_I:
        case NEXT_SCSI_I:
        case NEXT_CLK_I:
            m68k_set_irq_level(cpu, 3, 27);
            break;

        /* level 5 - scc (serial) */
        case NEXT_SCC_I:
            m68k_set_irq_level(cpu, 5, 29);
            break;

        /* level 6 - audio etherrx/tx dma */
        case NEXT_ENTX_DMA_I:
        case NEXT_ENRX_DMA_I:
        case NEXT_SCSI_DMA_I:
        case NEXT_SND_I:
        case NEXT_SCC_DMA_I:
            m68k_set_irq_level(cpu, 6, 30);
            break;
        }
    } else {
        ns->int_status &= ~(1 << shift);
        cpu_reset_interrupt(CPU(cpu), CPU_INTERRUPT_HARD);
    }
}

static void next_serial_irq(void *opaque, int n, int level)
{
    /* DPRINTF("SCC IRQ NUM %i\n",n); */
    if (n) {
        next_irq(opaque, NEXT_SCC_DMA_I, level);
    } else {
        next_irq(opaque, NEXT_SCC_I, level);
    }
}

static void next_escc_init(M68kCPU *cpu)
{
    qemu_irq *ser_irq = qemu_allocate_irqs(next_serial_irq, cpu, 2);
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_create(NULL, TYPE_ESCC);
    qdev_prop_set_uint32(dev, "disabled", 0);
    qdev_prop_set_uint32(dev, "frequency", 9600 * 384);
    qdev_prop_set_uint32(dev, "it_shift", 0);
    qdev_prop_set_bit(dev, "bit_swap", true);
    qdev_prop_set_chr(dev, "chrB", serial_hd(1));
    qdev_prop_set_chr(dev, "chrA", serial_hd(0));
    qdev_prop_set_uint32(dev, "chnBtype", escc_serial);
    qdev_prop_set_uint32(dev, "chnAtype", escc_serial);
    qdev_init_nofail(dev);

    s = SYS_BUS_DEVICE(dev);
    sysbus_connect_irq(s, 0, ser_irq[0]);
    sysbus_connect_irq(s, 1,  ser_irq[1]);
    sysbus_mmio_map(s, 0, 0x2118000);
}

static void next_cube_init(MachineState *machine)
{
    M68kCPU *cpu;
    CPUM68KState *env;
    MemoryRegion *rom = g_new(MemoryRegion, 1);
    MemoryRegion *mmiomem = g_new(MemoryRegion, 1);
    MemoryRegion *scrmem = g_new(MemoryRegion, 1);
    MemoryRegion *dmamem = g_new(MemoryRegion, 1);
    MemoryRegion *bmapm1 = g_new(MemoryRegion, 1);
    MemoryRegion *bmapm2 = g_new(MemoryRegion, 1);
    MemoryRegion *sysmem = get_system_memory();
    NeXTState *ns = NEXT_MACHINE(machine);
    DeviceState *dev;

    /* Initialize the cpu core */
    cpu = M68K_CPU(cpu_create(machine->cpu_type));
    if (!cpu) {
        error_report("Unable to find m68k CPU definition");
        exit(1);
    }
    env = &cpu->env;

    /* Initialize CPU registers.  */
    env->vbr = 0;
    env->sr  = 0x2700;

    /* Set internal registers to initial values */
    /*     0x0000XX00 << vital bits */
    ns->scr1 = 0x00011102;
    ns->scr2 = 0x00ff0c80;
    ns->rtc.status = 0x90;

    /* Load RTC RAM - TODO: provide possibility to load contents from file */
    memcpy(ns->rtc.ram, rtc_ram2, 32);

    /* 64MB RAM starting at 0x04000000  */
    memory_region_add_subregion(sysmem, 0x04000000, machine->ram);

    /* Framebuffer */
    dev = qdev_create(NULL, TYPE_NEXTFB);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, 0x0B000000);

    /* MMIO */
    memory_region_init_io(mmiomem, NULL, &mmio_ops, machine, "next.mmio",
                          0xD0000);
    memory_region_add_subregion(sysmem, 0x02000000, mmiomem);

    /* BMAP memory */
    memory_region_init_ram_shared_nomigrate(bmapm1, NULL, "next.bmapmem", 64,
                                            true, &error_fatal);
    memory_region_add_subregion(sysmem, 0x020c0000, bmapm1);
    /* The Rev_2.5_v66.bin firmware accesses it at 0x820c0020, too */
    memory_region_init_alias(bmapm2, NULL, "next.bmapmem2", bmapm1, 0x0, 64);
    memory_region_add_subregion(sysmem, 0x820c0000, bmapm2);

    /* BMAP IO - acts as a catch-all for now */
    memory_region_init_io(scrmem, NULL, &scr_ops, machine, "next.scr",
                          0x20000);
    memory_region_add_subregion(sysmem, 0x02100000, scrmem);

    /* KBD */
    dev = qdev_create(NULL, TYPE_NEXTKBD);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, 0x0200e000);

    /* Load ROM here */
    if (bios_name == NULL) {
        bios_name = ROM_FILE;
    }
    /* still not sure if the rom should also be mapped at 0x0*/
    memory_region_init_rom(rom, NULL, "next.rom", 0x20000, &error_fatal);
    memory_region_add_subregion(sysmem, 0x01000000, rom);
    if (load_image_targphys(bios_name, 0x01000000, 0x20000) < 8) {
        if (!qtest_enabled()) {
            error_report("Failed to load firmware '%s'.", bios_name);
        }
    } else {
        uint8_t *ptr;
        /* Initial PC is always at offset 4 in firmware binaries */
        ptr = rom_ptr(0x01000004, 4);
        g_assert(ptr != NULL);
        env->pc = ldl_p(ptr);
        if (env->pc >= 0x01020000) {
            error_report("'%s' does not seem to be a valid firmware image.",
                         bios_name);
            exit(1);
        }
    }

    /* Serial */
    next_escc_init(cpu);

    /* TODO: */
    /* Network */
    /* SCSI */

    /* DMA */
    memory_region_init_io(dmamem, NULL, &dma_ops, machine, "next.dma", 0x5000);
    memory_region_add_subregion(sysmem, 0x02000000, dmamem);
}

static void next_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "NeXT Cube";
    mc->init = next_cube_init;
    mc->default_ram_size = RAM_SIZE;
    mc->default_ram_id = "next.ram";
    mc->default_cpu_type = M68K_CPU_TYPE_NAME("m68040");
}

static const TypeInfo next_typeinfo = {
    .name = TYPE_NEXT_MACHINE,
    .parent = TYPE_MACHINE,
    .class_init = next_machine_class_init,
    .instance_size = sizeof(NeXTState),
};

static void next_register_type(void)
{
    type_register_static(&next_typeinfo);
}

type_init(next_register_type)
