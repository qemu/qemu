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
#include "exec/hwaddr.h"
#include "sysemu/sysemu.h"
#include "sysemu/qtest.h"
#include "hw/irq.h"
#include "hw/m68k/next-cube.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/scsi/esp.h"
#include "hw/sysbus.h"
#include "qom/object.h"
#include "hw/char/escc.h" /* ZILOG 8530 Serial Emulation */
#include "hw/block/fdc.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "ui/console.h"
#include "target/m68k/cpu.h"
#include "migration/vmstate.h"

/* #define DEBUG_NEXT */
#ifdef DEBUG_NEXT
#define DPRINTF(fmt, ...) \
    do { printf("NeXT: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do { } while (0)
#endif

#define TYPE_NEXT_MACHINE MACHINE_TYPE_NAME("next-cube")
OBJECT_DECLARE_SIMPLE_TYPE(NeXTState, NEXT_MACHINE)

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
    int8_t phase;
    uint8_t ram[32];
    uint8_t command;
    uint8_t value;
    uint8_t status;
    uint8_t control;
    uint8_t retval;
} NextRtc;

struct NeXTState {
    MachineState parent;

    MemoryRegion rom;
    MemoryRegion rom2;
    MemoryRegion dmamem;
    MemoryRegion bmapm1;
    MemoryRegion bmapm2;

    next_dma dma[10];
};

#define TYPE_NEXT_PC "next-pc"
OBJECT_DECLARE_SIMPLE_TYPE(NeXTPC, NEXT_PC)

/* NeXT Peripheral Controller */
struct NeXTPC {
    SysBusDevice parent_obj;

    M68kCPU *cpu;

    MemoryRegion mmiomem;
    MemoryRegion scrmem;

    uint32_t scr1;
    uint32_t scr2;
    uint32_t old_scr2;
    uint32_t int_mask;
    uint32_t int_status;
    uint32_t led;
    uint8_t scsi_csr_1;
    uint8_t scsi_csr_2;

    qemu_irq scsi_reset;
    qemu_irq scsi_dma;

    NextRtc rtc;
};

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

static void next_scr2_led_update(NeXTPC *s)
{
    if (s->scr2 & 0x1) {
        DPRINTF("fault!\n");
        s->led++;
        if (s->led == 10) {
            DPRINTF("LED flashing, possible fault!\n");
            s->led = 0;
        }
    }
}

static void next_scr2_rtc_update(NeXTPC *s)
{
    uint8_t old_scr2, scr2_2;
    NextRtc *rtc = &s->rtc;

    old_scr2 = extract32(s->old_scr2, 8, 8);
    scr2_2 = extract32(s->scr2, 8, 8);

    if (scr2_2 & 0x1) {
        /* DPRINTF("RTC %x phase %i\n", scr2_2, rtc->phase); */
        if (rtc->phase == -1) {
            rtc->phase = 0;
        }
        /* If we are in going down clock... do something */
        if (((old_scr2 & SCR2_RTCLK) != (scr2_2 & SCR2_RTCLK)) &&
                ((scr2_2 & SCR2_RTCLK) == 0)) {
            if (rtc->phase < 8) {
                rtc->command = (rtc->command << 1) |
                               ((scr2_2 & SCR2_RTDATA) ? 1 : 0);
            }
            if (rtc->phase >= 8 && rtc->phase < 16) {
                rtc->value = (rtc->value << 1) |
                             ((scr2_2 & SCR2_RTDATA) ? 1 : 0);

                /* if we read RAM register, output RT_DATA bit */
                if (rtc->command <= 0x1F) {
                    scr2_2 = scr2_2 & (~SCR2_RTDATA);
                    if (rtc->ram[rtc->command] & (0x80 >> (rtc->phase - 8))) {
                        scr2_2 |= SCR2_RTDATA;
                    }

                    rtc->retval = (rtc->retval << 1) |
                                  ((scr2_2 & SCR2_RTDATA) ? 1 : 0);
                }
                /* read the status 0x30 */
                if (rtc->command == 0x30) {
                    scr2_2 = scr2_2 & (~SCR2_RTDATA);
                    /* for now status = 0x98 (new rtc + FTU) */
                    if (rtc->status & (0x80 >> (rtc->phase - 8))) {
                        scr2_2 |= SCR2_RTDATA;
                    }

                    rtc->retval = (rtc->retval << 1) |
                                  ((scr2_2 & SCR2_RTDATA) ? 1 : 0);
                }
                /* read the status 0x31 */
                if (rtc->command == 0x31) {
                    scr2_2 = scr2_2 & (~SCR2_RTDATA);
                    if (rtc->control & (0x80 >> (rtc->phase - 8))) {
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

                    if (ret & (0x80 >> (rtc->phase - 8))) {
                        scr2_2 |= SCR2_RTDATA;
                    }
                    rtc->retval = (rtc->retval << 1) |
                                  ((scr2_2 & SCR2_RTDATA) ? 1 : 0);
                }

            }

            rtc->phase++;
            if (rtc->phase == 16) {
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
        rtc->phase = -1;
        rtc->command = 0;
        rtc->value = 0;
    }

    s->scr2 = deposit32(s->scr2, 8, 8, scr2_2);
}

static uint64_t next_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    NeXTPC *s = NEXT_PC(opaque);
    uint64_t val;

    switch (addr) {
    case 0x7000:
        /* DPRINTF("Read INT status: %x\n", s->int_status); */
        val = s->int_status;
        break;

    case 0x7800:
        DPRINTF("MMIO Read INT mask: %x\n", s->int_mask);
        val = s->int_mask;
        break;

    case 0xc000 ... 0xc003:
        val = extract32(s->scr1, (4 - (addr - 0xc000) - size) << 3,
                        size << 3);
        break;

    case 0xd000 ... 0xd003:
        val = extract32(s->scr2, (4 - (addr - 0xd000) - size) << 3,
                        size << 3);
        break;

    case 0x14020:
        val = 0x7f;
        break;

    default:
        val = 0;
        DPRINTF("MMIO Read @ 0x%"HWADDR_PRIx" size %d\n", addr, size);
        break;
    }

    return val;
}

static void next_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                            unsigned size)
{
    NeXTPC *s = NEXT_PC(opaque);

    switch (addr) {
    case 0x7000:
        DPRINTF("INT Status old: %x new: %x\n", s->int_status,
                (unsigned int)val);
        s->int_status = val;
        break;

    case 0x7800:
        DPRINTF("INT Mask old: %x new: %x\n", s->int_mask, (unsigned int)val);
        s->int_mask  = val;
        break;

    case 0xc000 ... 0xc003:
        DPRINTF("SCR1 Write: %x\n", (unsigned int)val);
        s->scr1 = deposit32(s->scr1, (4 - (addr - 0xc000) - size) << 3,
                            size << 3, val);
        break;

    case 0xd000 ... 0xd003:
        s->scr2 = deposit32(s->scr2, (4 - (addr - 0xd000) - size) << 3,
                            size << 3, val);
        next_scr2_led_update(s);
        next_scr2_rtc_update(s);
        s->old_scr2 = s->scr2;
        break;

    default:
        DPRINTF("MMIO Write @ 0x%"HWADDR_PRIx " with 0x%x size %u\n", addr,
                (unsigned int)val, size);
    }
}

static const MemoryRegionOps next_mmio_ops = {
    .read = next_mmio_read,
    .write = next_mmio_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .endianness = DEVICE_BIG_ENDIAN,
};

#define SCSICSR_ENABLE  0x01
#define SCSICSR_RESET   0x02  /* reset scsi dma */
#define SCSICSR_FIFOFL  0x04
#define SCSICSR_DMADIR  0x08  /* if set, scsi to mem */
#define SCSICSR_CPUDMA  0x10  /* if set, dma enabled */
#define SCSICSR_INTMASK 0x20  /* if set, interrupt enabled */

static uint64_t next_scr_readfn(void *opaque, hwaddr addr, unsigned size)
{
    NeXTPC *s = NEXT_PC(opaque);
    uint64_t val;

    switch (addr) {
    case 0x14108:
        DPRINTF("FD read @ %x\n", (unsigned int)addr);
        val = 0x40 | 0x04 | 0x2 | 0x1;
        break;

    case 0x14020:
        DPRINTF("SCSI 4020  STATUS READ %X\n", s->scsi_csr_1);
        val = s->scsi_csr_1;
        break;

    case 0x14021:
        DPRINTF("SCSI 4021 STATUS READ %X\n", s->scsi_csr_2);
        val = 0x40;
        break;

    /*
     * These 4 registers are the hardware timer, not sure which register
     * is the latch instead of data, but no problems so far.
     *
     * Hack: We need to have the LSB change consistently to make it work
     */
    case 0x1a000 ... 0x1a003:
        val = extract32(clock(), (4 - (addr - 0x1a000) - size) << 3,
                        size << 3);
        break;

    /* For now return dummy byte to allow the Ethernet test to timeout */
    case 0x6000:
        val = 0xff;
        break;

    default:
        DPRINTF("BMAP Read @ 0x%x size %u\n", (unsigned int)addr, size);
        val = 0;
        break;
    }

    return val;
}

static void next_scr_writefn(void *opaque, hwaddr addr, uint64_t val,
                             unsigned size)
{
    NeXTPC *s = NEXT_PC(opaque);

    switch (addr) {
    case 0x14108:
        DPRINTF("FDCSR Write: %x\n", value);
        if (val == 0x0) {
            /* qemu_irq_raise(s->fd_irq[0]); */
        }
        break;

    case 0x14020: /* SCSI Control Register */
        if (val & SCSICSR_FIFOFL) {
            DPRINTF("SCSICSR FIFO Flush\n");
            /* will have to add another irq to the esp if this is needed */
            /* esp_puflush_fifo(esp_g); */
        }

        if (val & SCSICSR_ENABLE) {
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

        if (val & SCSICSR_RESET) {
            DPRINTF("SCSICSR Reset\n");
            /* I think this should set DMADIR. CPUDMA and INTMASK to 0 */
            qemu_irq_raise(s->scsi_reset);
            s->scsi_csr_1 &= ~(SCSICSR_INTMASK | 0x80 | 0x1);
            qemu_irq_lower(s->scsi_reset);
        }
        if (val & SCSICSR_DMADIR) {
            DPRINTF("SCSICSR DMAdir\n");
        }
        if (val & SCSICSR_CPUDMA) {
            DPRINTF("SCSICSR CPUDMA\n");
            /* qemu_irq_raise(s->scsi_dma); */
            s->int_status |= 0x4000000;
        } else {
            /* fprintf(stderr,"SCSICSR CPUDMA disabled\n"); */
            s->int_status &= ~(0x4000000);
            /* qemu_irq_lower(s->scsi_dma); */
        }
        if (val & SCSICSR_INTMASK) {
            DPRINTF("SCSICSR INTMASK\n");
            /*
             * int_mask &= ~0x1000;
             * s->scsi_csr_1 |= val;
             * s->scsi_csr_1 &= ~SCSICSR_INTMASK;
             * if (s->scsi_queued) {
             *     s->scsi_queued = 0;
             *     next_irq(s, NEXT_SCSI_I, level);
             * }
             */
        } else {
            /* int_mask |= 0x1000; */
        }
        if (val & 0x80) {
            /* int_mask |= 0x1000; */
            /* s->scsi_csr_1 |= 0x80; */
        }
        DPRINTF("SCSICSR Write: %x\n", val);
        /* s->scsi_csr_1 = val; */
        break;

    /* Hardware timer latch - not implemented yet */
    case 0x1a000:
    default:
        DPRINTF("BMAP Write @ 0x%x with 0x%x size %u\n", (unsigned int)addr,
                val, size);
    }
}

static const MemoryRegionOps next_scr_ops = {
    .read = next_scr_readfn,
    .write = next_scr_writefn,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .endianness = DEVICE_BIG_ENDIAN,
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

static void next_dma_write(void *opaque, hwaddr addr, uint64_t val,
                           unsigned int size)
{
    NeXTState *next_state = NEXT_MACHINE(opaque);

    switch (addr) {
    case NEXTDMA_ENRX(NEXTDMA_CSR):
        if (val & DMA_DEV2M) {
            next_state->dma[NEXTDMA_ENRX].csr |= DMA_DEV2M;
        }

        if (val & DMA_SETENABLE) {
            /* DPRINTF("SCSI DMA ENABLE\n"); */
            next_state->dma[NEXTDMA_ENRX].csr |= DMA_ENABLE;
        }
        if (val & DMA_SETSUPDATE) {
            next_state->dma[NEXTDMA_ENRX].csr |= DMA_SUPDATE;
        }
        if (val & DMA_CLRCOMPLETE) {
            next_state->dma[NEXTDMA_ENRX].csr &= ~DMA_COMPLETE;
        }

        if (val & DMA_RESET) {
            next_state->dma[NEXTDMA_ENRX].csr &= ~(DMA_COMPLETE | DMA_SUPDATE |
                                                  DMA_ENABLE | DMA_DEV2M);
        }
        /* DPRINTF("RXCSR \tWrite: %x\n",value); */
        break;

    case NEXTDMA_ENRX(NEXTDMA_NEXT_INIT):
        next_state->dma[NEXTDMA_ENRX].next_initbuf = val;
        break;

    case NEXTDMA_ENRX(NEXTDMA_NEXT):
        next_state->dma[NEXTDMA_ENRX].next = val;
        break;

    case NEXTDMA_ENRX(NEXTDMA_LIMIT):
        next_state->dma[NEXTDMA_ENRX].limit = val;
        break;

    case NEXTDMA_SCSI(NEXTDMA_CSR):
        if (val & DMA_DEV2M) {
            next_state->dma[NEXTDMA_SCSI].csr |= DMA_DEV2M;
        }
        if (val & DMA_SETENABLE) {
            /* DPRINTF("SCSI DMA ENABLE\n"); */
            next_state->dma[NEXTDMA_SCSI].csr |= DMA_ENABLE;
        }
        if (val & DMA_SETSUPDATE) {
            next_state->dma[NEXTDMA_SCSI].csr |= DMA_SUPDATE;
        }
        if (val & DMA_CLRCOMPLETE) {
            next_state->dma[NEXTDMA_SCSI].csr &= ~DMA_COMPLETE;
        }

        if (val & DMA_RESET) {
            next_state->dma[NEXTDMA_SCSI].csr &= ~(DMA_COMPLETE | DMA_SUPDATE |
                                                  DMA_ENABLE | DMA_DEV2M);
            /* DPRINTF("SCSI DMA RESET\n"); */
        }
        /* DPRINTF("RXCSR \tWrite: %x\n",value); */
        break;

    case NEXTDMA_SCSI(NEXTDMA_NEXT):
        next_state->dma[NEXTDMA_SCSI].next = val;
        break;

    case NEXTDMA_SCSI(NEXTDMA_LIMIT):
        next_state->dma[NEXTDMA_SCSI].limit = val;
        break;

    case NEXTDMA_SCSI(NEXTDMA_START):
        next_state->dma[NEXTDMA_SCSI].start = val;
        break;

    case NEXTDMA_SCSI(NEXTDMA_STOP):
        next_state->dma[NEXTDMA_SCSI].stop = val;
        break;

    case NEXTDMA_SCSI(NEXTDMA_NEXT_INIT):
        next_state->dma[NEXTDMA_SCSI].next_initbuf = val;
        break;

    default:
        DPRINTF("DMA write @ %x w/ %x\n", (unsigned)addr, (unsigned)value);
    }
}

static uint64_t next_dma_read(void *opaque, hwaddr addr, unsigned int size)
{
    NeXTState *next_state = NEXT_MACHINE(opaque);
    uint64_t val;

    switch (addr) {
    case NEXTDMA_SCSI(NEXTDMA_CSR):
        DPRINTF("SCSI DMA CSR READ\n");
        val = next_state->dma[NEXTDMA_SCSI].csr;
        break;

    case NEXTDMA_ENRX(NEXTDMA_CSR):
        val = next_state->dma[NEXTDMA_ENRX].csr;
        break;

    case NEXTDMA_ENRX(NEXTDMA_NEXT_INIT):
        val = next_state->dma[NEXTDMA_ENRX].next_initbuf;
        break;

    case NEXTDMA_ENRX(NEXTDMA_NEXT):
        val = next_state->dma[NEXTDMA_ENRX].next;
        break;

    case NEXTDMA_ENRX(NEXTDMA_LIMIT):
        val = next_state->dma[NEXTDMA_ENRX].limit;
        break;

    case NEXTDMA_SCSI(NEXTDMA_NEXT):
        val = next_state->dma[NEXTDMA_SCSI].next;
        break;

    case NEXTDMA_SCSI(NEXTDMA_NEXT_INIT):
        val = next_state->dma[NEXTDMA_SCSI].next_initbuf;
        break;

    case NEXTDMA_SCSI(NEXTDMA_LIMIT):
        val = next_state->dma[NEXTDMA_SCSI].limit;
        break;

    case NEXTDMA_SCSI(NEXTDMA_START):
        val = next_state->dma[NEXTDMA_SCSI].start;
        break;

    case NEXTDMA_SCSI(NEXTDMA_STOP):
        val = next_state->dma[NEXTDMA_SCSI].stop;
        break;

    default:
        DPRINTF("DMA read @ %x\n", (unsigned int)addr);
        val = 0;
    }

    /*
     * once the csr's are done, subtract 0x3FEC from the addr, and that will
     * normalize the upper registers
     */

    return val;
}

static const MemoryRegionOps next_dma_ops = {
    .read = next_dma_read,
    .write = next_dma_write,
    .impl.min_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void next_irq(void *opaque, int number, int level)
{
    NeXTPC *s = NEXT_PC(opaque);
    M68kCPU *cpu = s->cpu;
    int shift = 0;

    /* first switch sets interrupt status */
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
    if (s->int_mask & (1 << shift)) {
        DPRINTF("%x interrupt masked @ %x\n", 1 << shift, cpu->env.pc);
        /* return; */
    }

    /* second switch triggers the correct interrupt */
    if (level) {
        s->int_status |= 1 << shift;

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
        s->int_status &= ~(1 << shift);
        cpu_reset_interrupt(CPU(cpu), CPU_INTERRUPT_HARD);
    }
}

static void nextdma_write(void *opaque, uint8_t *buf, int size, int type)
{
    uint32_t base_addr;
    int irq = 0;
    uint8_t align = 16;
    NeXTState *next_state = NEXT_MACHINE(qdev_get_machine());

    if (type == NEXTDMA_ENRX || type == NEXTDMA_ENTX) {
        align = 32;
    }
    /* Most DMA is supposedly 16 byte aligned */
    if ((size % align) != 0) {
        size -= size % align;
        size += align;
    }

    /*
     * prom sets the dma start using initbuf while the bootloader uses next
     * so we check to see if initbuf is 0
     */
    if (next_state->dma[type].next_initbuf == 0) {
        base_addr = next_state->dma[type].next;
    } else {
        base_addr = next_state->dma[type].next_initbuf;
    }

    cpu_physical_memory_write(base_addr, buf, size);

    next_state->dma[type].next_initbuf = 0;

    /* saved limit is checked to calculate packet size by both, rom and netbsd */
    next_state->dma[type].saved_limit = (next_state->dma[type].next + size);
    next_state->dma[type].saved_next  = (next_state->dma[type].next);

    /*
     * 32 bytes under savedbase seems to be some kind of register
     * of which the purpose is unknown as of yet
     */
    /* stl_phys(s->rx_dma.base-32,0xFFFFFFFF); */

    if (!(next_state->dma[type].csr & DMA_SUPDATE)) {
        next_state->dma[type].next  = next_state->dma[type].start;
        next_state->dma[type].limit = next_state->dma[type].stop;
    }

    /* Set dma registers and raise an irq */
    next_state->dma[type].csr |= DMA_COMPLETE; /* DON'T CHANGE THIS! */

    switch (type) {
    case NEXTDMA_SCSI:
        irq = NEXT_SCSI_DMA_I;
        break;
    }

    next_irq(opaque, irq, 1);
    next_irq(opaque, irq, 0);
}

static void nextscsi_read(void *opaque, uint8_t *buf, int len)
{
    DPRINTF("SCSI READ: %x\n", len);
    abort();
}

static void nextscsi_write(void *opaque, uint8_t *buf, int size)
{
    DPRINTF("SCSI WRITE: %i\n", size);
    nextdma_write(opaque, buf, size, NEXTDMA_SCSI);
}

static void next_scsi_init(DeviceState *pcdev, M68kCPU *cpu)
{
    struct NeXTPC *next_pc = NEXT_PC(pcdev);
    DeviceState *dev;
    SysBusDevice *sysbusdev;
    SysBusESPState *sysbus_esp;
    ESPState *esp;

    dev = qdev_new(TYPE_SYSBUS_ESP);
    sysbus_esp = SYSBUS_ESP(dev);
    esp = &sysbus_esp->esp;
    esp->dma_memory_read = nextscsi_read;
    esp->dma_memory_write = nextscsi_write;
    esp->dma_opaque = pcdev;
    sysbus_esp->it_shift = 0;
    esp->dma_enabled = 1;
    sysbusdev = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(sysbusdev, &error_fatal);
    sysbus_connect_irq(sysbusdev, 0, qdev_get_gpio_in(pcdev, NEXT_SCSI_I));
    sysbus_mmio_map(sysbusdev, 0, 0x2114000);

    next_pc->scsi_reset = qdev_get_gpio_in(dev, 0);
    next_pc->scsi_dma = qdev_get_gpio_in(dev, 1);

    scsi_bus_legacy_handle_cmdline(&esp->bus);
}

static void next_escc_init(DeviceState *pcdev)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_new(TYPE_ESCC);
    qdev_prop_set_uint32(dev, "disabled", 0);
    qdev_prop_set_uint32(dev, "frequency", 9600 * 384);
    qdev_prop_set_uint32(dev, "it_shift", 0);
    qdev_prop_set_bit(dev, "bit_swap", true);
    qdev_prop_set_chr(dev, "chrB", serial_hd(1));
    qdev_prop_set_chr(dev, "chrA", serial_hd(0));
    qdev_prop_set_uint32(dev, "chnBtype", escc_serial);
    qdev_prop_set_uint32(dev, "chnAtype", escc_serial);

    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_connect_irq(s, 0, qdev_get_gpio_in(pcdev, NEXT_SCC_I));
    sysbus_connect_irq(s, 1, qdev_get_gpio_in(pcdev, NEXT_SCC_DMA_I));
    sysbus_mmio_map(s, 0, 0x2118000);
}

static void next_pc_reset(DeviceState *dev)
{
    NeXTPC *s = NEXT_PC(dev);

    /* Set internal registers to initial values */
    /*     0x0000XX00 << vital bits */
    s->scr1 = 0x00011102;
    s->scr2 = 0x00ff0c80;
    s->old_scr2 = s->scr2;

    s->rtc.status = 0x90;

    /* Load RTC RAM - TODO: provide possibility to load contents from file */
    memcpy(s->rtc.ram, rtc_ram2, 32);
}

static void next_pc_realize(DeviceState *dev, Error **errp)
{
    NeXTPC *s = NEXT_PC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    qdev_init_gpio_in(dev, next_irq, NEXT_NUM_IRQS);

    memory_region_init_io(&s->mmiomem, OBJECT(s), &next_mmio_ops, s,
                          "next.mmio", 0xd0000);
    memory_region_init_io(&s->scrmem, OBJECT(s), &next_scr_ops, s,
                          "next.scr", 0x20000);
    sysbus_init_mmio(sbd, &s->mmiomem);
    sysbus_init_mmio(sbd, &s->scrmem);
}

/*
 * If the m68k CPU implemented its inbound irq lines as GPIO lines
 * rather than via the m68k_set_irq_level() function we would not need
 * this cpu link property and could instead provide outbound IRQ lines
 * that the board could wire up to the CPU.
 */
static Property next_pc_properties[] = {
    DEFINE_PROP_LINK("cpu", NeXTPC, cpu, TYPE_M68K_CPU, M68kCPU *),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription next_rtc_vmstate = {
    .name = "next-rtc",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_INT8(phase, NextRtc),
        VMSTATE_UINT8_ARRAY(ram, NextRtc, 32),
        VMSTATE_UINT8(command, NextRtc),
        VMSTATE_UINT8(value, NextRtc),
        VMSTATE_UINT8(status, NextRtc),
        VMSTATE_UINT8(control, NextRtc),
        VMSTATE_UINT8(retval, NextRtc),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription next_pc_vmstate = {
    .name = "next-pc",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(scr1, NeXTPC),
        VMSTATE_UINT32(scr2, NeXTPC),
        VMSTATE_UINT32(old_scr2, NeXTPC),
        VMSTATE_UINT32(int_mask, NeXTPC),
        VMSTATE_UINT32(int_status, NeXTPC),
        VMSTATE_UINT32(led, NeXTPC),
        VMSTATE_UINT8(scsi_csr_1, NeXTPC),
        VMSTATE_UINT8(scsi_csr_2, NeXTPC),
        VMSTATE_STRUCT(rtc, NeXTPC, 0, next_rtc_vmstate, NextRtc),
        VMSTATE_END_OF_LIST()
    },
};

static void next_pc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "NeXT Peripheral Controller";
    dc->realize = next_pc_realize;
    device_class_set_legacy_reset(dc, next_pc_reset);
    device_class_set_props(dc, next_pc_properties);
    dc->vmsd = &next_pc_vmstate;
}

static const TypeInfo next_pc_info = {
    .name = TYPE_NEXT_PC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NeXTPC),
    .class_init = next_pc_class_init,
};

static void next_cube_init(MachineState *machine)
{
    NeXTState *m = NEXT_MACHINE(machine);
    M68kCPU *cpu;
    CPUM68KState *env;
    MemoryRegion *sysmem = get_system_memory();
    const char *bios_name = machine->firmware ?: ROM_FILE;
    DeviceState *pcdev;

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

    /* Peripheral Controller */
    pcdev = qdev_new(TYPE_NEXT_PC);
    object_property_set_link(OBJECT(pcdev), "cpu", OBJECT(cpu), &error_abort);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(pcdev), &error_fatal);

    /* 64MB RAM starting at 0x04000000  */
    memory_region_add_subregion(sysmem, 0x04000000, machine->ram);

    /* Framebuffer */
    sysbus_create_simple(TYPE_NEXTFB, 0x0B000000, NULL);

    /* MMIO */
    sysbus_mmio_map(SYS_BUS_DEVICE(pcdev), 0, 0x02000000);

    /* BMAP IO - acts as a catch-all for now */
    sysbus_mmio_map(SYS_BUS_DEVICE(pcdev), 1, 0x02100000);

    /* BMAP memory */
    memory_region_init_ram_flags_nomigrate(&m->bmapm1, NULL, "next.bmapmem",
                                           64, RAM_SHARED, &error_fatal);
    memory_region_add_subregion(sysmem, 0x020c0000, &m->bmapm1);
    /* The Rev_2.5_v66.bin firmware accesses it at 0x820c0020, too */
    memory_region_init_alias(&m->bmapm2, NULL, "next.bmapmem2", &m->bmapm1,
                             0x0, 64);
    memory_region_add_subregion(sysmem, 0x820c0000, &m->bmapm2);

    /* KBD */
    sysbus_create_simple(TYPE_NEXTKBD, 0x0200e000, NULL);

    /* Load ROM here */
    memory_region_init_rom(&m->rom, NULL, "next.rom", 0x20000, &error_fatal);
    memory_region_add_subregion(sysmem, 0x01000000, &m->rom);
    memory_region_init_alias(&m->rom2, NULL, "next.rom2", &m->rom, 0x0,
                             0x20000);
    memory_region_add_subregion(sysmem, 0x0, &m->rom2);
    if (load_image_targphys(bios_name, 0x01000000, 0x20000) < 8) {
        if (!qtest_enabled()) {
            error_report("Failed to load firmware '%s'.", bios_name);
        }
    } else {
        uint8_t *ptr;
        /* Initial PC is always at offset 4 in firmware binaries */
        ptr = rom_ptr(0x01000004, 4);
        g_assert(ptr != NULL);
        env->pc = ldl_be_p(ptr);
        if (env->pc >= 0x01020000) {
            error_report("'%s' does not seem to be a valid firmware image.",
                         bios_name);
            exit(1);
        }
    }

    /* Serial */
    next_escc_init(pcdev);

    /* TODO: */
    /* Network */
    /* SCSI */
    next_scsi_init(pcdev, cpu);

    /* DMA */
    memory_region_init_io(&m->dmamem, NULL, &next_dma_ops, machine,
                          "next.dma", 0x5000);
    memory_region_add_subregion(sysmem, 0x02000000, &m->dmamem);
}

static void next_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "NeXT Cube";
    mc->init = next_cube_init;
    mc->block_default_type = IF_SCSI;
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
    type_register_static(&next_pc_info);
}

type_init(next_register_type)
