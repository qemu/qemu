/*
 * QEMU m68k Macintosh VIA device support
 *
 * Copyright (c) 2011-2018 Laurent Vivier
 * Copyright (c) 2018 Mark Cave-Ayland
 *
 * Some parts from hw/misc/macio/cuda.c
 *
 * Copyright (c) 2004-2007 Fabrice Bellard
 * Copyright (c) 2007 Jocelyn Mayer
 *
 * some parts from linux-2.6.29, arch/m68k/include/asm/mac_via.h
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "migration/vmstate.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "qemu/timer.h"
#include "hw/misc/mac_via.h"
#include "hw/misc/mos6522.h"
#include "hw/input/adb.h"
#include "sysemu/runstate.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "sysemu/block-backend.h"
#include "sysemu/rtc.h"
#include "trace.h"
#include "qemu/log.h"

/*
 * VIAs: There are two in every machine
 */

/*
 * Not all of these are true post MacII I think.
 * CSA: probably the ones CHRP marks as 'unused' change purposes
 * when the IWM becomes the SWIM.
 * http://www.rs6000.ibm.com/resource/technology/chrpio/via5.mak.html
 * ftp://ftp.austin.ibm.com/pub/technology/spec/chrp/inwork/CHRP_IORef_1.0.pdf
 *
 * also, http://developer.apple.com/technotes/hw/hw_09.html claims the
 * following changes for IIfx:
 * VIA1A_vSccWrReq not available and that VIA1A_vSync has moved to an IOP.
 * Also, "All of the functionality of VIA2 has been moved to other chips".
 */

#define VIA1A_vSccWrReq 0x80   /*
                                * SCC write. (input)
                                * [CHRP] SCC WREQ: Reflects the state of the
                                * Wait/Request pins from the SCC.
                                * [Macintosh Family Hardware]
                                * as CHRP on SE/30,II,IIx,IIcx,IIci.
                                * on IIfx, "0 means an active request"
                                */
#define VIA1A_vRev8     0x40   /*
                                * Revision 8 board ???
                                * [CHRP] En WaitReqB: Lets the WaitReq_L
                                * signal from port B of the SCC appear on
                                * the PA7 input pin. Output.
                                * [Macintosh Family] On the SE/30, this
                                * is the bit to flip screen buffers.
                                * 0=alternate, 1=main.
                                * on II,IIx,IIcx,IIci,IIfx this is a bit
                                * for Rev ID. 0=II,IIx, 1=IIcx,IIci,IIfx
                                */
#define VIA1A_vHeadSel  0x20   /*
                                * Head select for IWM.
                                * [CHRP] unused.
                                * [Macintosh Family] "Floppy disk
                                * state-control line SEL" on all but IIfx
                                */
#define VIA1A_vOverlay  0x10   /*
                                * [Macintosh Family] On SE/30,II,IIx,IIcx
                                * this bit enables the "Overlay" address
                                * map in the address decoders as it is on
                                * reset for mapping the ROM over the reset
                                * vector. 1=use overlay map.
                                * On the IIci,IIfx it is another bit of the
                                * CPU ID: 0=normal IIci, 1=IIci with parity
                                * feature or IIfx.
                                * [CHRP] En WaitReqA: Lets the WaitReq_L
                                * signal from port A of the SCC appear
                                * on the PA7 input pin (CHRP). Output.
                                * [MkLinux] "Drive Select"
                                *  (with 0x20 being 'disk head select')
                                */
#define VIA1A_vSync     0x08   /*
                                * [CHRP] Sync Modem: modem clock select:
                                * 1: select the external serial clock to
                                *    drive the SCC's /RTxCA pin.
                                * 0: Select the 3.6864MHz clock to drive
                                *    the SCC cell.
                                * [Macintosh Family] Correct on all but IIfx
                                */

/*
 * Macintosh Family Hardware sez: bits 0-2 of VIA1A are volume control
 * on Macs which had the PWM sound hardware.  Reserved on newer models.
 * On IIci,IIfx, bits 1-2 are the rest of the CPU ID:
 * bit 2: 1=IIci, 0=IIfx
 * bit 1: 1 on both IIci and IIfx.
 * MkLinux sez bit 0 is 'burnin flag' in this case.
 * CHRP sez: VIA1A bits 0-2 and 5 are 'unused': if programmed as
 * inputs, these bits will read 0.
 */
#define VIA1A_vVolume   0x07    /* Audio volume mask for PWM */
#define VIA1A_CPUID0    0x02    /* CPU id bit 0 on RBV, others */
#define VIA1A_CPUID1    0x04    /* CPU id bit 0 on RBV, others */
#define VIA1A_CPUID2    0x10    /* CPU id bit 0 on RBV, others */
#define VIA1A_CPUID3    0x40    /* CPU id bit 0 on RBV, others */

/*
 * Info on VIA1B is from Macintosh Family Hardware & MkLinux.
 * CHRP offers no info.
 */
#define VIA1B_vSound   0x80    /*
                                * Sound enable (for compatibility with
                                * PWM hardware) 0=enabled.
                                * Also, on IIci w/parity, shows parity error
                                * 0=error, 1=OK.
                                */
#define VIA1B_vMystery 0x40    /*
                                * On IIci, parity enable. 0=enabled,1=disabled
                                * On SE/30, vertical sync interrupt enable.
                                * 0=enabled. This vSync interrupt shows up
                                * as a slot $E interrupt.
                                * On Quadra 800 this bit toggles A/UX mode which
                                * configures the glue logic to deliver some IRQs
                                * at different levels compared to a classic
                                * Mac.
                                */
#define VIA1B_vADBS2   0x20    /* ADB state input bit 1 (unused on IIfx) */
#define VIA1B_vADBS1   0x10    /* ADB state input bit 0 (unused on IIfx) */
#define VIA1B_vADBInt  0x08    /* ADB interrupt 0=interrupt (unused on IIfx)*/
#define VIA1B_vRTCEnb  0x04    /* Enable Real time clock. 0=enabled. */
#define VIA1B_vRTCClk  0x02    /* Real time clock serial-clock line. */
#define VIA1B_vRTCData 0x01    /* Real time clock serial-data line. */

/*
 *    VIA2 A register is the interrupt lines raised off the nubus
 *    slots.
 *      The below info is from 'Macintosh Family Hardware.'
 *      MkLinux calls the 'IIci internal video IRQ' below the 'RBV slot 0 irq.'
 *      It also notes that the slot $9 IRQ is the 'Ethernet IRQ' and
 *      defines the 'Video IRQ' as 0x40 for the 'EVR' VIA work-alike.
 *      Perhaps OSS uses vRAM1 and vRAM2 for ADB.
 */

#define VIA2A_vRAM1    0x80    /* RAM size bit 1 (IIci: reserved) */
#define VIA2A_vRAM0    0x40    /* RAM size bit 0 (IIci: internal video IRQ) */
#define VIA2A_vIRQE    0x20    /* IRQ from slot $E */
#define VIA2A_vIRQD    0x10    /* IRQ from slot $D */
#define VIA2A_vIRQC    0x08    /* IRQ from slot $C */
#define VIA2A_vIRQB    0x04    /* IRQ from slot $B */
#define VIA2A_vIRQA    0x02    /* IRQ from slot $A */
#define VIA2A_vIRQ9    0x01    /* IRQ from slot $9 */

/*
 * RAM size bits decoded as follows:
 * bit1 bit0  size of ICs in bank A
 *  0    0    256 kbit
 *  0    1    1 Mbit
 *  1    0    4 Mbit
 *  1    1   16 Mbit
 */

/*
 *    Register B has the fun stuff in it
 */

#define VIA2B_vVBL    0x80    /*
                               * VBL output to VIA1 (60.15Hz) driven by
                               * timer T1.
                               * on IIci, parity test: 0=test mode.
                               * [MkLinux] RBV_PARODD: 1=odd,0=even.
                               */
#define VIA2B_vSndJck 0x40    /*
                               * External sound jack status.
                               * 0=plug is inserted.  On SE/30, always 0
                               */
#define VIA2B_vTfr0   0x20    /* Transfer mode bit 0 ack from NuBus */
#define VIA2B_vTfr1   0x10    /* Transfer mode bit 1 ack from NuBus */
#define VIA2B_vMode32 0x08    /*
                               * 24/32bit switch - doubles as cache flush
                               * on II, AMU/PMMU control.
                               *   if AMU, 0=24bit to 32bit translation
                               *   if PMMU, 1=PMMU is accessing page table.
                               * on SE/30 tied low.
                               * on IIx,IIcx,IIfx, unused.
                               * on IIci/RBV, cache control. 0=flush cache.
                               */
#define VIA2B_vPower  0x04   /*
                              * Power off, 0=shut off power.
                              * on SE/30 this signal sent to PDS card.
                              */
#define VIA2B_vBusLk  0x02   /*
                              * Lock NuBus transactions, 0=locked.
                              * on SE/30 sent to PDS card.
                              */
#define VIA2B_vCDis   0x01   /*
                              * Cache control. On IIci, 1=disable cache card
                              * on others, 0=disable processor's instruction
                              * and data caches.
                              */

/* interrupt flags */

#define IRQ_SET         0x80

/* common */

#define VIA_IRQ_TIMER1      0x40
#define VIA_IRQ_TIMER2      0x20

/*
 * Apple sez: http://developer.apple.com/technotes/ov/ov_04.html
 * Another example of a valid function that has no ROM support is the use
 * of the alternate video page for page-flipping animation. Since there
 * is no ROM call to flip pages, it is necessary to go play with the
 * right bit in the VIA chip (6522 Versatile Interface Adapter).
 * [CSA: don't know which one this is, but it's one of 'em!]
 */

/*
 *    6522 registers - see databook.
 * CSA: Assignments for VIA1 confirmed from CHRP spec.
 */

/* partial address decode.  0xYYXX : XX part for RBV, YY part for VIA */
/* Note: 15 VIA regs, 8 RBV regs */

#define vBufB    0x0000  /* [VIA/RBV]  Register B */
#define vBufAH   0x0200  /* [VIA only] Buffer A, with handshake. DON'T USE! */
#define vDirB    0x0400  /* [VIA only] Data Direction Register B. */
#define vDirA    0x0600  /* [VIA only] Data Direction Register A. */
#define vT1CL    0x0800  /* [VIA only] Timer one counter low. */
#define vT1CH    0x0a00  /* [VIA only] Timer one counter high. */
#define vT1LL    0x0c00  /* [VIA only] Timer one latches low. */
#define vT1LH    0x0e00  /* [VIA only] Timer one latches high. */
#define vT2CL    0x1000  /* [VIA only] Timer two counter low. */
#define vT2CH    0x1200  /* [VIA only] Timer two counter high. */
#define vSR      0x1400  /* [VIA only] Shift register. */
#define vACR     0x1600  /* [VIA only] Auxilary control register. */
#define vPCR     0x1800  /* [VIA only] Peripheral control register. */
                         /*
                          *           CHRP sez never ever to *write* this.
                          *            Mac family says never to *change* this.
                          * In fact we need to initialize it once at start.
                          */
#define vIFR     0x1a00  /* [VIA/RBV]  Interrupt flag register. */
#define vIER     0x1c00  /* [VIA/RBV]  Interrupt enable register. */
#define vBufA    0x1e00  /* [VIA/RBV] register A (no handshake) */

/* from linux 2.6 drivers/macintosh/via-macii.c */

/* Bits in ACR */

#define VIA1ACR_vShiftCtrl         0x1c        /* Shift register control bits */
#define VIA1ACR_vShiftExtClk       0x0c        /* Shift on external clock */
#define VIA1ACR_vShiftOut          0x10        /* Shift out if 1 */

/*
 * Apple Macintosh Family Hardware Refenece
 * Table 19-10 ADB transaction states
 */

#define ADB_STATE_NEW       0
#define ADB_STATE_EVEN      1
#define ADB_STATE_ODD       2
#define ADB_STATE_IDLE      3

#define VIA1B_vADB_StateMask    (VIA1B_vADBS1 | VIA1B_vADBS2)
#define VIA1B_vADB_StateShift   4

#define VIA_TIMER_FREQ (783360)
#define VIA_ADB_POLL_FREQ 50 /* XXX: not real */

/*
 * Guide to the Macintosh Family Hardware ch. 12 "Displays" p. 401 gives the
 * precise 60Hz interrupt frequency as ~60.15Hz with a period of 16625.8 us
 */
#define VIA_60HZ_TIMER_PERIOD_NS   16625800

/* VIA returns time offset from Jan 1, 1904, not 1970 */
#define RTC_OFFSET 2082844800

enum {
    REG_0,
    REG_1,
    REG_2,
    REG_3,
    REG_TEST,
    REG_WPROTECT,
    REG_PRAM_ADDR,
    REG_PRAM_ADDR_LAST = REG_PRAM_ADDR + 19,
    REG_PRAM_SECT,
    REG_PRAM_SECT_LAST = REG_PRAM_SECT + 7,
    REG_INVALID,
    REG_EMPTY = 0xff,
};

static void via1_sixty_hz_update(MOS6522Q800VIA1State *v1s)
{
    /* 60 Hz irq */
    v1s->next_sixty_hz = (qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                          VIA_60HZ_TIMER_PERIOD_NS) /
                          VIA_60HZ_TIMER_PERIOD_NS * VIA_60HZ_TIMER_PERIOD_NS;
    timer_mod(v1s->sixty_hz_timer, v1s->next_sixty_hz);
}

static void via1_one_second_update(MOS6522Q800VIA1State *v1s)
{
    v1s->next_second = (qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1000) /
                       1000 * 1000;
    timer_mod(v1s->one_second_timer, v1s->next_second);
}

static void via1_sixty_hz(void *opaque)
{
    MOS6522Q800VIA1State *v1s = opaque;
    MOS6522State *s = MOS6522(v1s);
    qemu_irq irq = qdev_get_gpio_in(DEVICE(s), VIA1_IRQ_60HZ_BIT);

    /* Negative edge trigger */
    qemu_irq_lower(irq);
    qemu_irq_raise(irq);

    via1_sixty_hz_update(v1s);
}

static void via1_one_second(void *opaque)
{
    MOS6522Q800VIA1State *v1s = opaque;
    MOS6522State *s = MOS6522(v1s);
    qemu_irq irq = qdev_get_gpio_in(DEVICE(s), VIA1_IRQ_ONE_SECOND_BIT);

    /* Negative edge trigger */
    qemu_irq_lower(irq);
    qemu_irq_raise(irq);

    via1_one_second_update(v1s);
}


static void pram_update(MOS6522Q800VIA1State *v1s)
{
    if (v1s->blk) {
        if (blk_pwrite(v1s->blk, 0, sizeof(v1s->PRAM), v1s->PRAM, 0) < 0) {
            qemu_log("pram_update: cannot write to file\n");
        }
    }
}

/*
 * RTC Commands
 *
 * Command byte    Register addressed by the command
 *
 * z0000001        Seconds register 0 (lowest-order byte)
 * z0000101        Seconds register 1
 * z0001001        Seconds register 2
 * z0001101        Seconds register 3 (highest-order byte)
 * 00110001        Test register (write-only)
 * 00110101        Write-Protect Register (write-only)
 * z010aa01        RAM address 100aa ($10-$13) (first 20 bytes only)
 * z1aaaa01        RAM address 0aaaa ($00-$0F) (first 20 bytes only)
 * z0111aaa        Extended memory designator and sector number
 *
 * For a read request, z=1, for a write z=0
 * The letter a indicates bits whose value depend on what parameter
 * RAM byte you want to address
 */
static int via1_rtc_compact_cmd(uint8_t value)
{
    uint8_t read = value & 0x80;

    value &= 0x7f;

    /* the last 2 bits of a command byte must always be 0b01 ... */
    if ((value & 0x78) == 0x38) {
        /* except for the extended memory designator */
        return read | (REG_PRAM_SECT + (value & 0x07));
    }
    if ((value & 0x03) == 0x01) {
        value >>= 2;
        if ((value & 0x1c) == 0) {
            /* seconds registers */
            return read | (REG_0 + (value & 0x03));
        } else if ((value == 0x0c) && !read) {
            return REG_TEST;
        } else if ((value == 0x0d) && !read) {
            return REG_WPROTECT;
        } else if ((value & 0x1c) == 0x08) {
            /* RAM address 0x10 to 0x13 */
            return read | (REG_PRAM_ADDR + 0x10 + (value & 0x03));
        } else if ((value & 0x43) == 0x41) {
            /* RAM address 0x00 to 0x0f */
            return read | (REG_PRAM_ADDR + (value & 0x0f));
        }
    }
    return REG_INVALID;
}

static void via1_rtc_update(MOS6522Q800VIA1State *v1s)
{
    MOS6522State *s = MOS6522(v1s);
    int cmd, sector, addr;
    uint32_t time;

    if (s->b & VIA1B_vRTCEnb) {
        return;
    }

    if (s->dirb & VIA1B_vRTCData) {
        /* send bits to the RTC */
        if (!(v1s->last_b & VIA1B_vRTCClk) && (s->b & VIA1B_vRTCClk)) {
            v1s->data_out <<= 1;
            v1s->data_out |= s->b & VIA1B_vRTCData;
            v1s->data_out_cnt++;
        }
        trace_via1_rtc_update_data_out(v1s->data_out_cnt, v1s->data_out);
    } else {
        trace_via1_rtc_update_data_in(v1s->data_in_cnt, v1s->data_in);
        /* receive bits from the RTC */
        if ((v1s->last_b & VIA1B_vRTCClk) &&
            !(s->b & VIA1B_vRTCClk) &&
            v1s->data_in_cnt) {
            s->b = (s->b & ~VIA1B_vRTCData) |
                   ((v1s->data_in >> 7) & VIA1B_vRTCData);
            v1s->data_in <<= 1;
            v1s->data_in_cnt--;
        }
        return;
    }

    if (v1s->data_out_cnt != 8) {
        return;
    }

    v1s->data_out_cnt = 0;

    trace_via1_rtc_internal_status(v1s->cmd, v1s->alt, v1s->data_out);
    /* first byte: it's a command */
    if (v1s->cmd == REG_EMPTY) {

        cmd = via1_rtc_compact_cmd(v1s->data_out);
        trace_via1_rtc_internal_cmd(cmd);

        if (cmd == REG_INVALID) {
            trace_via1_rtc_cmd_invalid(v1s->data_out);
            return;
        }

        if (cmd & 0x80) { /* this is a read command */
            switch (cmd & 0x7f) {
            case REG_0...REG_3: /* seconds registers */
                /*
                 * register 0 is lowest-order byte
                 * register 3 is highest-order byte
                 */

                time = v1s->tick_offset + (qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)
                       / NANOSECONDS_PER_SECOND);
                trace_via1_rtc_internal_time(time);
                v1s->data_in = (time >> ((cmd & 0x03) << 3)) & 0xff;
                v1s->data_in_cnt = 8;
                trace_via1_rtc_cmd_seconds_read((cmd & 0x7f) - REG_0,
                                                v1s->data_in);
                break;
            case REG_PRAM_ADDR...REG_PRAM_ADDR_LAST:
                /* PRAM address 0x00 -> 0x13 */
                v1s->data_in = v1s->PRAM[(cmd & 0x7f) - REG_PRAM_ADDR];
                v1s->data_in_cnt = 8;
                trace_via1_rtc_cmd_pram_read((cmd & 0x7f) - REG_PRAM_ADDR,
                                             v1s->data_in);
                break;
            case REG_PRAM_SECT...REG_PRAM_SECT_LAST:
                /*
                 * extended memory designator and sector number
                 * the only two-byte read command
                 */
                trace_via1_rtc_internal_set_cmd(cmd);
                v1s->cmd = cmd;
                break;
            default:
                g_assert_not_reached();
                break;
            }
            return;
        }

        /* this is a write command, needs a parameter */
        if (cmd == REG_WPROTECT || !v1s->wprotect) {
            trace_via1_rtc_internal_set_cmd(cmd);
            v1s->cmd = cmd;
        } else {
            trace_via1_rtc_internal_ignore_cmd(cmd);
        }
        return;
    }

    /* second byte: it's a parameter */
    if (v1s->alt == REG_EMPTY) {
        switch (v1s->cmd & 0x7f) {
        case REG_0...REG_3: /* seconds register */
            /* FIXME */
            trace_via1_rtc_cmd_seconds_write(v1s->cmd - REG_0, v1s->data_out);
            v1s->cmd = REG_EMPTY;
            break;
        case REG_TEST:
            /* device control: nothing to do */
            trace_via1_rtc_cmd_test_write(v1s->data_out);
            v1s->cmd = REG_EMPTY;
            break;
        case REG_WPROTECT:
            /* Write Protect register */
            trace_via1_rtc_cmd_wprotect_write(v1s->data_out);
            v1s->wprotect = !!(v1s->data_out & 0x80);
            v1s->cmd = REG_EMPTY;
            break;
        case REG_PRAM_ADDR...REG_PRAM_ADDR_LAST:
            /* PRAM address 0x00 -> 0x13 */
            trace_via1_rtc_cmd_pram_write(v1s->cmd - REG_PRAM_ADDR,
                                          v1s->data_out);
            v1s->PRAM[v1s->cmd - REG_PRAM_ADDR] = v1s->data_out;
            pram_update(v1s);
            v1s->cmd = REG_EMPTY;
            break;
        case REG_PRAM_SECT...REG_PRAM_SECT_LAST:
            addr = (v1s->data_out >> 2) & 0x1f;
            sector = (v1s->cmd & 0x7f) - REG_PRAM_SECT;
            if (v1s->cmd & 0x80) {
                /* it's a read */
                v1s->data_in = v1s->PRAM[sector * 32 + addr];
                v1s->data_in_cnt = 8;
                trace_via1_rtc_cmd_pram_sect_read(sector, addr,
                                                  sector * 32 + addr,
                                                  v1s->data_in);
                v1s->cmd = REG_EMPTY;
            } else {
                /* it's a write, we need one more parameter */
                trace_via1_rtc_internal_set_alt(addr, sector, addr);
                v1s->alt = addr;
            }
            break;
        default:
            g_assert_not_reached();
            break;
        }
        return;
    }

    /* third byte: it's the data of a REG_PRAM_SECT write */
    g_assert(REG_PRAM_SECT <= v1s->cmd && v1s->cmd <= REG_PRAM_SECT_LAST);
    sector = v1s->cmd - REG_PRAM_SECT;
    v1s->PRAM[sector * 32 + v1s->alt] = v1s->data_out;
    pram_update(v1s);
    trace_via1_rtc_cmd_pram_sect_write(sector, v1s->alt, sector * 32 + v1s->alt,
                                       v1s->data_out);
    v1s->alt = REG_EMPTY;
    v1s->cmd = REG_EMPTY;
}

static void adb_via_poll(void *opaque)
{
    MOS6522Q800VIA1State *v1s = MOS6522_Q800_VIA1(opaque);
    MOS6522State *s = MOS6522(v1s);
    ADBBusState *adb_bus = &v1s->adb_bus;
    uint8_t obuf[9];
    uint8_t *data = &s->sr;
    int olen;

    /*
     * Setting vADBInt below indicates that an autopoll reply has been
     * received, however we must block autopoll until the point where
     * the entire reply has been read back to the host
     */
    adb_autopoll_block(adb_bus);

    if (v1s->adb_data_in_size > 0 && v1s->adb_data_in_index == 0) {
        /*
         * For older Linux kernels that switch to IDLE mode after sending the
         * ADB command, detect if there is an existing response and return that
         * as a "fake" autopoll reply or bus timeout accordingly
         */
        *data = v1s->adb_data_out[0];
        olen = v1s->adb_data_in_size;

        s->b &= ~VIA1B_vADBInt;
        qemu_irq_raise(v1s->adb_data_ready);
    } else {
        /*
         * Otherwise poll as normal
         */
        v1s->adb_data_in_index = 0;
        v1s->adb_data_out_index = 0;
        olen = adb_poll(adb_bus, obuf, adb_bus->autopoll_mask);

        if (olen > 0) {
            /* Autopoll response */
            *data = obuf[0];
            olen--;
            memcpy(v1s->adb_data_in, &obuf[1], olen);
            v1s->adb_data_in_size = olen;

            s->b &= ~VIA1B_vADBInt;
            qemu_irq_raise(v1s->adb_data_ready);
        } else {
            *data = v1s->adb_autopoll_cmd;
            obuf[0] = 0xff;
            obuf[1] = 0xff;
            olen = 2;

            memcpy(v1s->adb_data_in, obuf, olen);
            v1s->adb_data_in_size = olen;

            s->b &= ~VIA1B_vADBInt;
            qemu_irq_raise(v1s->adb_data_ready);
        }
    }

    trace_via1_adb_poll(*data, (s->b & VIA1B_vADBInt) ? "+" : "-",
                        adb_bus->status, v1s->adb_data_in_index, olen);
}

static int adb_via_send_len(uint8_t data)
{
    /* Determine the send length from the given ADB command */
    uint8_t cmd = data & 0xc;
    uint8_t reg = data & 0x3;

    switch (cmd) {
    case 0x8:
        /* Listen command */
        switch (reg) {
        case 2:
            /* Register 2 is only used for the keyboard */
            return 3;
        case 3:
            /*
             * Fortunately our devices only implement writes
             * to register 3 which is fixed at 2 bytes
             */
            return 3;
        default:
            qemu_log_mask(LOG_UNIMP, "ADB unknown length for register %d\n",
                          reg);
            return 1;
        }
    default:
        /* Talk, BusReset */
        return 1;
    }
}

static void adb_via_send(MOS6522Q800VIA1State *v1s, int state, uint8_t data)
{
    MOS6522State *ms = MOS6522(v1s);
    ADBBusState *adb_bus = &v1s->adb_bus;
    uint16_t autopoll_mask;

    switch (state) {
    case ADB_STATE_NEW:
        /*
         * Command byte: vADBInt tells host autopoll data already present
         * in VIA shift register and ADB transceiver
         */
        adb_autopoll_block(adb_bus);

        if (adb_bus->status & ADB_STATUS_POLLREPLY) {
            /* Tell the host the existing data is from autopoll */
            ms->b &= ~VIA1B_vADBInt;
        } else {
            ms->b |= VIA1B_vADBInt;
            v1s->adb_data_out_index = 0;
            v1s->adb_data_out[v1s->adb_data_out_index++] = data;
        }

        trace_via1_adb_send(" NEW", data, (ms->b & VIA1B_vADBInt) ? "+" : "-");
        qemu_irq_raise(v1s->adb_data_ready);
        break;

    case ADB_STATE_EVEN:
    case ADB_STATE_ODD:
        ms->b |= VIA1B_vADBInt;
        v1s->adb_data_out[v1s->adb_data_out_index++] = data;

        trace_via1_adb_send(state == ADB_STATE_EVEN ? "EVEN" : " ODD",
                            data, (ms->b & VIA1B_vADBInt) ? "+" : "-");
        qemu_irq_raise(v1s->adb_data_ready);
        break;

    case ADB_STATE_IDLE:
        return;
    }

    /* If the command is complete, execute it */
    if (v1s->adb_data_out_index == adb_via_send_len(v1s->adb_data_out[0])) {
        v1s->adb_data_in_size = adb_request(adb_bus, v1s->adb_data_in,
                                            v1s->adb_data_out,
                                            v1s->adb_data_out_index);
        v1s->adb_data_in_index = 0;

        if (adb_bus->status & ADB_STATUS_BUSTIMEOUT) {
            /*
             * Bus timeout (but allow first EVEN and ODD byte to indicate
             * timeout via vADBInt and SRQ status)
             */
            v1s->adb_data_in[0] = 0xff;
            v1s->adb_data_in[1] = 0xff;
            v1s->adb_data_in_size = 2;
        }

        /*
         * If last command is TALK, store it for use by autopoll and adjust
         * the autopoll mask accordingly
         */
        if ((v1s->adb_data_out[0] & 0xc) == 0xc) {
            v1s->adb_autopoll_cmd = v1s->adb_data_out[0];

            autopoll_mask = 1 << (v1s->adb_autopoll_cmd >> 4);
            adb_set_autopoll_mask(adb_bus, autopoll_mask);
        }
    }
}

static void adb_via_receive(MOS6522Q800VIA1State *v1s, int state, uint8_t *data)
{
    MOS6522State *ms = MOS6522(v1s);
    ADBBusState *adb_bus = &v1s->adb_bus;
    uint16_t pending;

    switch (state) {
    case ADB_STATE_NEW:
        ms->b |= VIA1B_vADBInt;
        return;

    case ADB_STATE_IDLE:
        ms->b |= VIA1B_vADBInt;
        adb_autopoll_unblock(adb_bus);

        trace_via1_adb_receive("IDLE", *data,
                        (ms->b & VIA1B_vADBInt) ? "+" : "-", adb_bus->status,
                        v1s->adb_data_in_index, v1s->adb_data_in_size);

        break;

    case ADB_STATE_EVEN:
    case ADB_STATE_ODD:
        switch (v1s->adb_data_in_index) {
        case 0:
            /* First EVEN byte: vADBInt indicates bus timeout */
            *data = v1s->adb_data_in[v1s->adb_data_in_index];
            if (adb_bus->status & ADB_STATUS_BUSTIMEOUT) {
                ms->b &= ~VIA1B_vADBInt;
            } else {
                ms->b |= VIA1B_vADBInt;
            }

            trace_via1_adb_receive(state == ADB_STATE_EVEN ? "EVEN" : " ODD",
                                   *data, (ms->b & VIA1B_vADBInt) ? "+" : "-",
                                   adb_bus->status, v1s->adb_data_in_index,
                                   v1s->adb_data_in_size);

            v1s->adb_data_in_index++;
            break;

        case 1:
            /* First ODD byte: vADBInt indicates SRQ */
            *data = v1s->adb_data_in[v1s->adb_data_in_index];
            pending = adb_bus->pending & ~(1 << (v1s->adb_autopoll_cmd >> 4));
            if (pending) {
                ms->b &= ~VIA1B_vADBInt;
            } else {
                ms->b |= VIA1B_vADBInt;
            }

            trace_via1_adb_receive(state == ADB_STATE_EVEN ? "EVEN" : " ODD",
                                   *data, (ms->b & VIA1B_vADBInt) ? "+" : "-",
                                   adb_bus->status, v1s->adb_data_in_index,
                                   v1s->adb_data_in_size);

            v1s->adb_data_in_index++;
            break;

        default:
            /*
             * Otherwise vADBInt indicates end of data. Note that Linux
             * specifically checks for the sequence 0x0 0xff to confirm the
             * end of the poll reply, so provide these extra bytes below to
             * keep it happy
             */
            if (v1s->adb_data_in_index < v1s->adb_data_in_size) {
                /* Next data byte */
                *data = v1s->adb_data_in[v1s->adb_data_in_index];
                ms->b |= VIA1B_vADBInt;
            } else if (v1s->adb_data_in_index == v1s->adb_data_in_size) {
                if (adb_bus->status & ADB_STATUS_BUSTIMEOUT) {
                    /* Bus timeout (no more data) */
                    *data = 0xff;
                } else {
                    /* Return 0x0 after reply */
                    *data = 0;
                }
                ms->b &= ~VIA1B_vADBInt;
            } else {
                /* Bus timeout (no more data) */
                *data = 0xff;
                ms->b &= ~VIA1B_vADBInt;
                adb_bus->status = 0;
                adb_autopoll_unblock(adb_bus);
            }

            trace_via1_adb_receive(state == ADB_STATE_EVEN ? "EVEN" : " ODD",
                                   *data, (ms->b & VIA1B_vADBInt) ? "+" : "-",
                                   adb_bus->status, v1s->adb_data_in_index,
                                   v1s->adb_data_in_size);

            if (v1s->adb_data_in_index <= v1s->adb_data_in_size) {
                v1s->adb_data_in_index++;
            }
            break;
        }

        qemu_irq_raise(v1s->adb_data_ready);
        break;
    }
}

static void via1_adb_update(MOS6522Q800VIA1State *v1s)
{
    MOS6522State *s = MOS6522(v1s);
    int oldstate, state;

    oldstate = (v1s->last_b & VIA1B_vADB_StateMask) >> VIA1B_vADB_StateShift;
    state = (s->b & VIA1B_vADB_StateMask) >> VIA1B_vADB_StateShift;

    if (state != oldstate) {
        if (s->acr & VIA1ACR_vShiftOut) {
            /* output mode */
            adb_via_send(v1s, state, s->sr);
        } else {
            /* input mode */
            adb_via_receive(v1s, state, &s->sr);
        }
    }
}

static void via1_auxmode_update(MOS6522Q800VIA1State *v1s)
{
    MOS6522State *s = MOS6522(v1s);
    int oldirq, irq;

    oldirq = (v1s->last_b & VIA1B_vMystery) ? 1 : 0;
    irq = (s->b & VIA1B_vMystery) ? 1 : 0;

    /* Check to see if the A/UX mode bit has changed */
    if (irq != oldirq) {
        trace_via1_auxmode(irq);
        qemu_set_irq(v1s->auxmode_irq, irq);
    }
}

static uint64_t mos6522_q800_via1_read(void *opaque, hwaddr addr, unsigned size)
{
    MOS6522Q800VIA1State *s = MOS6522_Q800_VIA1(opaque);
    MOS6522State *ms = MOS6522(s);

    addr = (addr >> 9) & 0xf;
    return mos6522_read(ms, addr, size);
}

static void mos6522_q800_via1_write(void *opaque, hwaddr addr, uint64_t val,
                                    unsigned size)
{
    MOS6522Q800VIA1State *v1s = MOS6522_Q800_VIA1(opaque);
    MOS6522State *ms = MOS6522(v1s);

    addr = (addr >> 9) & 0xf;
    mos6522_write(ms, addr, val, size);

    switch (addr) {
    case VIA_REG_B:
        via1_rtc_update(v1s);
        via1_adb_update(v1s);
        via1_auxmode_update(v1s);

        v1s->last_b = ms->b;
        break;
    }
}

static const MemoryRegionOps mos6522_q800_via1_ops = {
    .read = mos6522_q800_via1_read,
    .write = mos6522_q800_via1_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static uint64_t mos6522_q800_via2_read(void *opaque, hwaddr addr, unsigned size)
{
    MOS6522Q800VIA2State *s = MOS6522_Q800_VIA2(opaque);
    MOS6522State *ms = MOS6522(s);
    uint64_t val;

    addr = (addr >> 9) & 0xf;
    val = mos6522_read(ms, addr, size);

    switch (addr) {
    case VIA_REG_IFR:
        /*
         * On a Q800 an emulated VIA2 is integrated into the onboard logic. The
         * expectation of most OSs is that the DRQ bit is live, rather than
         * latched as it would be on a real VIA so do the same here.
         *
         * Note: DRQ is negative edge triggered
         */
        val &= ~VIA2_IRQ_SCSI_DATA;
        val |= (~ms->last_irq_levels & VIA2_IRQ_SCSI_DATA);
        break;
    }

    return val;
}

static void mos6522_q800_via2_write(void *opaque, hwaddr addr, uint64_t val,
                                    unsigned size)
{
    MOS6522Q800VIA2State *s = MOS6522_Q800_VIA2(opaque);
    MOS6522State *ms = MOS6522(s);

    addr = (addr >> 9) & 0xf;
    mos6522_write(ms, addr, val, size);
}

static const MemoryRegionOps mos6522_q800_via2_ops = {
    .read = mos6522_q800_via2_read,
    .write = mos6522_q800_via2_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void via1_postload_update_cb(void *opaque, bool running, RunState state)
{
    MOS6522Q800VIA1State *v1s = MOS6522_Q800_VIA1(opaque);

    qemu_del_vm_change_state_handler(v1s->vmstate);
    v1s->vmstate = NULL;

    pram_update(v1s);
}

static int via1_post_load(void *opaque, int version_id)
{
    MOS6522Q800VIA1State *v1s = MOS6522_Q800_VIA1(opaque);

    if (v1s->blk) {
        v1s->vmstate = qemu_add_vm_change_state_handler(
                           via1_postload_update_cb, v1s);
    }

    return 0;
}

/* VIA 1 */
static void mos6522_q800_via1_reset_hold(Object *obj)
{
    MOS6522Q800VIA1State *v1s = MOS6522_Q800_VIA1(obj);
    MOS6522State *ms = MOS6522(v1s);
    MOS6522DeviceClass *mdc = MOS6522_GET_CLASS(ms);
    ADBBusState *adb_bus = &v1s->adb_bus;

    if (mdc->parent_phases.hold) {
        mdc->parent_phases.hold(obj);
    }

    ms->timers[0].frequency = VIA_TIMER_FREQ;
    ms->timers[1].frequency = VIA_TIMER_FREQ;

    ms->b = VIA1B_vADB_StateMask | VIA1B_vADBInt | VIA1B_vRTCEnb;

    /* ADB/RTC */
    adb_set_autopoll_enabled(adb_bus, true);
    v1s->cmd = REG_EMPTY;
    v1s->alt = REG_EMPTY;
}

static void mos6522_q800_via1_realize(DeviceState *dev, Error **errp)
{
    MOS6522Q800VIA1State *v1s = MOS6522_Q800_VIA1(dev);
    ADBBusState *adb_bus = &v1s->adb_bus;
    struct tm tm;
    int ret;

    v1s->one_second_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, via1_one_second,
                                         v1s);
    via1_one_second_update(v1s);
    v1s->sixty_hz_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, via1_sixty_hz,
                                       v1s);
    via1_sixty_hz_update(v1s);

    qemu_get_timedate(&tm, 0);
    v1s->tick_offset = (uint32_t)mktimegm(&tm) + RTC_OFFSET;

    adb_register_autopoll_callback(adb_bus, adb_via_poll, v1s);
    v1s->adb_data_ready = qdev_get_gpio_in(dev, VIA1_IRQ_ADB_READY_BIT);

    if (v1s->blk) {
        int64_t len = blk_getlength(v1s->blk);
        if (len < 0) {
            error_setg_errno(errp, -len,
                             "could not get length of backing image");
            return;
        }
        ret = blk_set_perm(v1s->blk,
                           BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE,
                           BLK_PERM_ALL, errp);
        if (ret < 0) {
            return;
        }

        ret = blk_pread(v1s->blk, 0, sizeof(v1s->PRAM), v1s->PRAM, 0);
        if (ret < 0) {
            error_setg(errp, "can't read PRAM contents");
            return;
        }
    }
}

static void mos6522_q800_via1_init(Object *obj)
{
    MOS6522Q800VIA1State *v1s = MOS6522_Q800_VIA1(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(v1s);

    memory_region_init_io(&v1s->via_mem, obj, &mos6522_q800_via1_ops, v1s,
                          "via1", VIA_SIZE);
    sysbus_init_mmio(sbd, &v1s->via_mem);

    /* ADB */
    qbus_init((BusState *)&v1s->adb_bus, sizeof(v1s->adb_bus),
              TYPE_ADB_BUS, DEVICE(v1s), "adb.0");

    /* A/UX mode */
    qdev_init_gpio_out(DEVICE(obj), &v1s->auxmode_irq, 1);
}

static const VMStateDescription vmstate_q800_via1 = {
    .name = "q800-via1",
    .version_id = 0,
    .minimum_version_id = 0,
    .post_load = via1_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(parent_obj, MOS6522Q800VIA1State, 0, vmstate_mos6522,
                       MOS6522State),
        VMSTATE_UINT8(last_b, MOS6522Q800VIA1State),
        /* RTC */
        VMSTATE_BUFFER(PRAM, MOS6522Q800VIA1State),
        VMSTATE_UINT32(tick_offset, MOS6522Q800VIA1State),
        VMSTATE_UINT8(data_out, MOS6522Q800VIA1State),
        VMSTATE_INT32(data_out_cnt, MOS6522Q800VIA1State),
        VMSTATE_UINT8(data_in, MOS6522Q800VIA1State),
        VMSTATE_UINT8(data_in_cnt, MOS6522Q800VIA1State),
        VMSTATE_UINT8(cmd, MOS6522Q800VIA1State),
        VMSTATE_INT32(wprotect, MOS6522Q800VIA1State),
        VMSTATE_INT32(alt, MOS6522Q800VIA1State),
        /* ADB */
        VMSTATE_INT32(adb_data_in_size, MOS6522Q800VIA1State),
        VMSTATE_INT32(adb_data_in_index, MOS6522Q800VIA1State),
        VMSTATE_INT32(adb_data_out_index, MOS6522Q800VIA1State),
        VMSTATE_BUFFER(adb_data_in, MOS6522Q800VIA1State),
        VMSTATE_BUFFER(adb_data_out, MOS6522Q800VIA1State),
        VMSTATE_UINT8(adb_autopoll_cmd, MOS6522Q800VIA1State),
        /* Timers */
        VMSTATE_TIMER_PTR(one_second_timer, MOS6522Q800VIA1State),
        VMSTATE_INT64(next_second, MOS6522Q800VIA1State),
        VMSTATE_TIMER_PTR(sixty_hz_timer, MOS6522Q800VIA1State),
        VMSTATE_INT64(next_sixty_hz, MOS6522Q800VIA1State),
        VMSTATE_END_OF_LIST()
    }
};

static Property mos6522_q800_via1_properties[] = {
    DEFINE_PROP_DRIVE("drive", MOS6522Q800VIA1State, blk),
    DEFINE_PROP_END_OF_LIST(),
};

static void mos6522_q800_via1_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);
    MOS6522DeviceClass *mdc = MOS6522_CLASS(oc);

    dc->realize = mos6522_q800_via1_realize;
    resettable_class_set_parent_phases(rc, NULL, mos6522_q800_via1_reset_hold,
                                       NULL, &mdc->parent_phases);
    dc->vmsd = &vmstate_q800_via1;
    device_class_set_props(dc, mos6522_q800_via1_properties);
}

static const TypeInfo mos6522_q800_via1_type_info = {
    .name = TYPE_MOS6522_Q800_VIA1,
    .parent = TYPE_MOS6522,
    .instance_size = sizeof(MOS6522Q800VIA1State),
    .instance_init = mos6522_q800_via1_init,
    .class_init = mos6522_q800_via1_class_init,
};

/* VIA 2 */
static void mos6522_q800_via2_portB_write(MOS6522State *s)
{
    if (s->dirb & VIA2B_vPower && (s->b & VIA2B_vPower) == 0) {
        /* shutdown */
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    }
}

static void mos6522_q800_via2_reset_hold(Object *obj)
{
    MOS6522State *ms = MOS6522(obj);
    MOS6522DeviceClass *mdc = MOS6522_GET_CLASS(ms);

    if (mdc->parent_phases.hold) {
        mdc->parent_phases.hold(obj);
    }

    ms->timers[0].frequency = VIA_TIMER_FREQ;
    ms->timers[1].frequency = VIA_TIMER_FREQ;

    ms->dirb = 0;
    ms->b = 0;
    ms->dira = 0;
    ms->a = 0x7f;
}

static void via2_nubus_irq_request(void *opaque, int n, int level)
{
    MOS6522Q800VIA2State *v2s = opaque;
    MOS6522State *s = MOS6522(v2s);
    qemu_irq irq = qdev_get_gpio_in(DEVICE(s), VIA2_IRQ_NUBUS_BIT);

    if (level) {
        /* Port A nubus IRQ inputs are active LOW */
        s->a &= ~(1 << n);
    } else {
        s->a |= (1 << n);
    }

    /* Negative edge trigger */
    qemu_set_irq(irq, !level);
}

static void mos6522_q800_via2_init(Object *obj)
{
    MOS6522Q800VIA2State *v2s = MOS6522_Q800_VIA2(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(v2s);

    memory_region_init_io(&v2s->via_mem, obj, &mos6522_q800_via2_ops, v2s,
                          "via2", VIA_SIZE);
    sysbus_init_mmio(sbd, &v2s->via_mem);

    qdev_init_gpio_in_named(DEVICE(obj), via2_nubus_irq_request, "nubus-irq",
                            VIA2_NUBUS_IRQ_NB);
}

static const VMStateDescription vmstate_q800_via2 = {
    .name = "q800-via2",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(parent_obj, MOS6522Q800VIA2State, 0, vmstate_mos6522,
                       MOS6522State),
        VMSTATE_END_OF_LIST()
    }
};

static void mos6522_q800_via2_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);
    MOS6522DeviceClass *mdc = MOS6522_CLASS(oc);

    resettable_class_set_parent_phases(rc, NULL, mos6522_q800_via2_reset_hold,
                                       NULL, &mdc->parent_phases);
    dc->vmsd = &vmstate_q800_via2;
    mdc->portB_write = mos6522_q800_via2_portB_write;
}

static const TypeInfo mos6522_q800_via2_type_info = {
    .name = TYPE_MOS6522_Q800_VIA2,
    .parent = TYPE_MOS6522,
    .instance_size = sizeof(MOS6522Q800VIA2State),
    .instance_init = mos6522_q800_via2_init,
    .class_init = mos6522_q800_via2_class_init,
};

static void mac_via_register_types(void)
{
    type_register_static(&mos6522_q800_via1_type_info);
    type_register_static(&mos6522_q800_via2_type_info);
}

type_init(mac_via_register_types);
