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
#include "qemu-common.h"
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


/*
 * VIAs: There are two in every machine,
 */

#define VIA_SIZE (0x2000)

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

/* VIA returns time offset from Jan 1, 1904, not 1970 */
#define RTC_OFFSET 2082844800

static void via1_VBL_update(MOS6522Q800VIA1State *v1s)
{
    MOS6522State *s = MOS6522(v1s);

    /* 60 Hz irq */
    v1s->next_VBL = (qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 16630) /
                    16630 * 16630;

    if (s->ier & VIA1_IRQ_VBLANK) {
        timer_mod(v1s->VBL_timer, v1s->next_VBL);
    } else {
        timer_del(v1s->VBL_timer);
    }
}

static void via1_one_second_update(MOS6522Q800VIA1State *v1s)
{
    MOS6522State *s = MOS6522(v1s);

    v1s->next_second = (qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1000) /
                       1000 * 1000;
    if (s->ier & VIA1_IRQ_ONE_SECOND) {
        timer_mod(v1s->one_second_timer, v1s->next_second);
    } else {
        timer_del(v1s->one_second_timer);
    }
}

static void via1_VBL(void *opaque)
{
    MOS6522Q800VIA1State *v1s = opaque;
    MOS6522State *s = MOS6522(v1s);
    MOS6522DeviceClass *mdc = MOS6522_DEVICE_GET_CLASS(s);

    s->ifr |= VIA1_IRQ_VBLANK;
    mdc->update_irq(s);

    via1_VBL_update(v1s);
}

static void via1_one_second(void *opaque)
{
    MOS6522Q800VIA1State *v1s = opaque;
    MOS6522State *s = MOS6522(v1s);
    MOS6522DeviceClass *mdc = MOS6522_DEVICE_GET_CLASS(s);

    s->ifr |= VIA1_IRQ_ONE_SECOND;
    mdc->update_irq(s);

    via1_one_second_update(v1s);
}

static void via1_irq_request(void *opaque, int irq, int level)
{
    MOS6522Q800VIA1State *v1s = opaque;
    MOS6522State *s = MOS6522(v1s);
    MOS6522DeviceClass *mdc = MOS6522_DEVICE_GET_CLASS(s);

    if (level) {
        s->ifr |= 1 << irq;
    } else {
        s->ifr &= ~(1 << irq);
    }

    mdc->update_irq(s);
}

static void via2_irq_request(void *opaque, int irq, int level)
{
    MOS6522Q800VIA2State *v2s = opaque;
    MOS6522State *s = MOS6522(v2s);
    MOS6522DeviceClass *mdc = MOS6522_DEVICE_GET_CLASS(s);

    if (level) {
        s->ifr |= 1 << irq;
    } else {
        s->ifr &= ~(1 << irq);
    }

    mdc->update_irq(s);
}

static void via1_rtc_update(MacVIAState *m)
{
    MOS6522Q800VIA1State *v1s = &m->mos6522_via1;
    MOS6522State *s = MOS6522(v1s);

    if (s->b & VIA1B_vRTCEnb) {
        return;
    }

    if (s->dirb & VIA1B_vRTCData) {
        /* send bits to the RTC */
        if (!(v1s->last_b & VIA1B_vRTCClk) && (s->b & VIA1B_vRTCClk)) {
            m->data_out <<= 1;
            m->data_out |= s->b & VIA1B_vRTCData;
            m->data_out_cnt++;
        }
    } else {
        /* receive bits from the RTC */
        if ((v1s->last_b & VIA1B_vRTCClk) &&
            !(s->b & VIA1B_vRTCClk) &&
            m->data_in_cnt) {
            s->b = (s->b & ~VIA1B_vRTCData) |
                   ((m->data_in >> 7) & VIA1B_vRTCData);
            m->data_in <<= 1;
            m->data_in_cnt--;
        }
    }

    if (m->data_out_cnt == 8) {
        m->data_out_cnt = 0;

        if (m->cmd == 0) {
            if (m->data_out & 0x80) {
                /* this is a read command */
                uint32_t time = m->tick_offset +
                               (qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) /
                               NANOSECONDS_PER_SECOND);
                if (m->data_out == 0x81) {        /* seconds register 0 */
                    m->data_in = time & 0xff;
                    m->data_in_cnt = 8;
                } else if (m->data_out == 0x85) { /* seconds register 1 */
                    m->data_in = (time >> 8) & 0xff;
                    m->data_in_cnt = 8;
                } else if (m->data_out == 0x89) { /* seconds register 2 */
                    m->data_in = (time >> 16) & 0xff;
                    m->data_in_cnt = 8;
                } else if (m->data_out == 0x8d) { /* seconds register 3 */
                    m->data_in = (time >> 24) & 0xff;
                    m->data_in_cnt = 8;
                } else if ((m->data_out & 0xf3) == 0xa1) {
                    /* PRAM address 0x10 -> 0x13 */
                    int addr = (m->data_out >> 2) & 0x03;
                    m->data_in = v1s->PRAM[addr];
                    m->data_in_cnt = 8;
                } else if ((m->data_out & 0xf3) == 0xa1) {
                    /* PRAM address 0x00 -> 0x0f */
                    int addr = (m->data_out >> 2) & 0x0f;
                    m->data_in = v1s->PRAM[addr];
                    m->data_in_cnt = 8;
                } else if ((m->data_out & 0xf8) == 0xb8) {
                    /* extended memory designator and sector number */
                    m->cmd = m->data_out;
                }
            } else {
                /* this is a write command */
                m->cmd = m->data_out;
            }
        } else {
            if (m->cmd & 0x80) {
                if ((m->cmd & 0xf8) == 0xb8) {
                    /* extended memory designator and sector number */
                    int sector = m->cmd & 0x07;
                    int addr = (m->data_out >> 2) & 0x1f;

                    m->data_in = v1s->PRAM[sector * 8 + addr];
                    m->data_in_cnt = 8;
                }
            } else if (!m->wprotect) {
                /* this is a write command */
                if (m->alt != 0) {
                    /* extended memory designator and sector number */
                    int sector = m->cmd & 0x07;
                    int addr = (m->alt >> 2) & 0x1f;

                    v1s->PRAM[sector * 8 + addr] = m->data_out;

                    m->alt = 0;
                } else if (m->cmd == 0x01) { /* seconds register 0 */
                    /* FIXME */
                } else if (m->cmd == 0x05) { /* seconds register 1 */
                    /* FIXME */
                } else if (m->cmd == 0x09) { /* seconds register 2 */
                    /* FIXME */
                } else if (m->cmd == 0x0d) { /* seconds register 3 */
                    /* FIXME */
                } else if (m->cmd == 0x31) {
                    /* Test Register */
                } else if (m->cmd == 0x35) {
                    /* Write Protect register */
                    m->wprotect = m->data_out & 1;
                } else if ((m->cmd & 0xf3) == 0xa1) {
                    /* PRAM address 0x10 -> 0x13 */
                    int addr = (m->cmd >> 2) & 0x03;
                    v1s->PRAM[addr] = m->data_out;
                } else if ((m->cmd & 0xf3) == 0xa1) {
                    /* PRAM address 0x00 -> 0x0f */
                    int addr = (m->cmd >> 2) & 0x0f;
                    v1s->PRAM[addr] = m->data_out;
                } else if ((m->cmd & 0xf8) == 0xb8) {
                    /* extended memory designator and sector number */
                    m->alt = m->cmd;
                }
            }
        }
        m->data_out = 0;
    }
}

static int adb_via_poll(MacVIAState *s, int state, uint8_t *data)
{
    if (state != ADB_STATE_IDLE) {
        return 0;
    }

    if (s->adb_data_in_size < s->adb_data_in_index) {
        return 0;
    }

    if (s->adb_data_out_index != 0) {
        return 0;
    }

    s->adb_data_in_index = 0;
    s->adb_data_out_index = 0;
    s->adb_data_in_size = adb_poll(&s->adb_bus, s->adb_data_in, 0xffff);

    if (s->adb_data_in_size) {
        *data = s->adb_data_in[s->adb_data_in_index++];
        qemu_irq_raise(s->adb_data_ready);
    }

    return s->adb_data_in_size;
}

static int adb_via_send(MacVIAState *s, int state, uint8_t data)
{
    switch (state) {
    case ADB_STATE_NEW:
        s->adb_data_out_index = 0;
        break;
    case ADB_STATE_EVEN:
        if ((s->adb_data_out_index & 1) == 0) {
            return 0;
        }
        break;
    case ADB_STATE_ODD:
        if (s->adb_data_out_index & 1) {
            return 0;
        }
        break;
    case ADB_STATE_IDLE:
        return 0;
    }

    assert(s->adb_data_out_index < sizeof(s->adb_data_out) - 1);

    s->adb_data_out[s->adb_data_out_index++] = data;
    qemu_irq_raise(s->adb_data_ready);
    return 1;
}

static int adb_via_receive(MacVIAState *s, int state, uint8_t *data)
{
    switch (state) {
    case ADB_STATE_NEW:
        return 0;

    case ADB_STATE_EVEN:
        if (s->adb_data_in_size <= 0) {
            qemu_irq_raise(s->adb_data_ready);
            return 0;
        }

        if (s->adb_data_in_index >= s->adb_data_in_size) {
            *data = 0;
            qemu_irq_raise(s->adb_data_ready);
            return 1;
        }

        if ((s->adb_data_in_index & 1) == 0) {
            return 0;
        }

        break;

    case ADB_STATE_ODD:
        if (s->adb_data_in_size <= 0) {
            qemu_irq_raise(s->adb_data_ready);
            return 0;
        }

        if (s->adb_data_in_index >= s->adb_data_in_size) {
            *data = 0;
            qemu_irq_raise(s->adb_data_ready);
            return 1;
        }

        if (s->adb_data_in_index & 1) {
            return 0;
        }

        break;

    case ADB_STATE_IDLE:
        if (s->adb_data_out_index == 0) {
            return 0;
        }

        s->adb_data_in_size = adb_request(&s->adb_bus, s->adb_data_in,
                                          s->adb_data_out,
                                          s->adb_data_out_index);
        s->adb_data_out_index = 0;
        s->adb_data_in_index = 0;
        if (s->adb_data_in_size < 0) {
            *data = 0xff;
            qemu_irq_raise(s->adb_data_ready);
            return -1;
        }

        if (s->adb_data_in_size == 0) {
            return 0;
        }

        break;
    }

    assert(s->adb_data_in_index < sizeof(s->adb_data_in) - 1);

    *data = s->adb_data_in[s->adb_data_in_index++];
    qemu_irq_raise(s->adb_data_ready);
    if (*data == 0xff || *data == 0) {
        return 0;
    }
    return 1;
}

static void via1_adb_update(MacVIAState *m)
{
    MOS6522Q800VIA1State *v1s = MOS6522_Q800_VIA1(&m->mos6522_via1);
    MOS6522State *s = MOS6522(v1s);
    int state;
    int ret;

    state = (s->b & VIA1B_vADB_StateMask) >> VIA1B_vADB_StateShift;

    if (s->acr & VIA1ACR_vShiftOut) {
        /* output mode */
        ret = adb_via_send(m, state, s->sr);
        if (ret > 0) {
            s->b &= ~VIA1B_vADBInt;
        } else {
            s->b |= VIA1B_vADBInt;
        }
    } else {
        /* input mode */
        ret = adb_via_receive(m, state, &s->sr);
        if (ret > 0 && s->sr != 0xff) {
            s->b &= ~VIA1B_vADBInt;
        } else {
            s->b |= VIA1B_vADBInt;
        }
    }
}

static void via_adb_poll(void *opaque)
{
    MacVIAState *m = opaque;
    MOS6522Q800VIA1State *v1s = MOS6522_Q800_VIA1(&m->mos6522_via1);
    MOS6522State *s = MOS6522(v1s);
    int state;

    if (s->b & VIA1B_vADBInt) {
        state = (s->b & VIA1B_vADB_StateMask) >> VIA1B_vADB_StateShift;
        if (adb_via_poll(m, state, &s->sr)) {
            s->b &= ~VIA1B_vADBInt;
        }
    }

    timer_mod(m->adb_poll_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
              (NANOSECONDS_PER_SECOND / VIA_ADB_POLL_FREQ));
}

static uint64_t mos6522_q800_via1_read(void *opaque, hwaddr addr, unsigned size)
{
    MOS6522Q800VIA1State *s = MOS6522_Q800_VIA1(opaque);
    MOS6522State *ms = MOS6522(s);
    int64_t now = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);

    /*
     * If IRQs are disabled, timers are disabled, but we need to update
     * VIA1_IRQ_VBLANK and VIA1_IRQ_ONE_SECOND bits in the IFR
     */

    if (now >= s->next_VBL) {
        ms->ifr |= VIA1_IRQ_VBLANK;
        via1_VBL_update(s);
    }
    if (now >= s->next_second) {
        ms->ifr |= VIA1_IRQ_ONE_SECOND;
        via1_one_second_update(s);
    }

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

    via1_one_second_update(v1s);
    via1_VBL_update(v1s);
}

static const MemoryRegionOps mos6522_q800_via1_ops = {
    .read = mos6522_q800_via1_read,
    .write = mos6522_q800_via1_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static uint64_t mos6522_q800_via2_read(void *opaque, hwaddr addr, unsigned size)
{
    MOS6522Q800VIA2State *s = MOS6522_Q800_VIA2(opaque);
    MOS6522State *ms = MOS6522(s);

    addr = (addr >> 9) & 0xf;
    return mos6522_read(ms, addr, size);
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
        .max_access_size = 1,
    },
};

static void mac_via_reset(DeviceState *dev)
{
    MacVIAState *m = MAC_VIA(dev);
    MOS6522Q800VIA1State *v1s = &m->mos6522_via1;

    timer_mod(m->adb_poll_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
              (NANOSECONDS_PER_SECOND / VIA_ADB_POLL_FREQ));

    timer_del(v1s->VBL_timer);
    v1s->next_VBL = 0;
    timer_del(v1s->one_second_timer);
    v1s->next_second = 0;
}

static void mac_via_realize(DeviceState *dev, Error **errp)
{
    MacVIAState *m = MAC_VIA(dev);
    MOS6522State *ms;
    struct tm tm;

    /* Init VIAs 1 and 2 */
    sysbus_init_child_obj(OBJECT(dev), "via1", &m->mos6522_via1,
                          sizeof(m->mos6522_via1), TYPE_MOS6522_Q800_VIA1);

    sysbus_init_child_obj(OBJECT(dev), "via2", &m->mos6522_via2,
                          sizeof(m->mos6522_via2), TYPE_MOS6522_Q800_VIA2);

    /* Pass through mos6522 output IRQs */
    ms = MOS6522(&m->mos6522_via1);
    object_property_add_alias(OBJECT(dev), "irq[0]", OBJECT(ms),
                              SYSBUS_DEVICE_GPIO_IRQ "[0]", &error_abort);
    ms = MOS6522(&m->mos6522_via2);
    object_property_add_alias(OBJECT(dev), "irq[1]", OBJECT(ms),
                              SYSBUS_DEVICE_GPIO_IRQ "[0]", &error_abort);

    /* Pass through mos6522 input IRQs */
    qdev_pass_gpios(DEVICE(&m->mos6522_via1), dev, "via1-irq");
    qdev_pass_gpios(DEVICE(&m->mos6522_via2), dev, "via2-irq");

    /* VIA 1 */
    m->mos6522_via1.one_second_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL,
                                                     via1_one_second,
                                                     &m->mos6522_via1);
    m->mos6522_via1.VBL_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, via1_VBL,
                                              &m->mos6522_via1);

    qemu_get_timedate(&tm, 0);
    m->tick_offset = (uint32_t)mktimegm(&tm) + RTC_OFFSET;

    m->adb_poll_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, via_adb_poll, m);
    m->adb_data_ready = qdev_get_gpio_in_named(dev, "via1-irq",
                                               VIA1_IRQ_ADB_READY_BIT);
}

static void mac_via_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    MacVIAState *m = MAC_VIA(obj);

    /* MMIO */
    memory_region_init(&m->mmio, obj, "mac-via", 2 * VIA_SIZE);
    sysbus_init_mmio(sbd, &m->mmio);

    memory_region_init_io(&m->via1mem, obj, &mos6522_q800_via1_ops,
                          &m->mos6522_via1, "via1", VIA_SIZE);
    memory_region_add_subregion(&m->mmio, 0x0, &m->via1mem);

    memory_region_init_io(&m->via2mem, obj, &mos6522_q800_via2_ops,
                          &m->mos6522_via2, "via2", VIA_SIZE);
    memory_region_add_subregion(&m->mmio, VIA_SIZE, &m->via2mem);

    /* ADB */
    qbus_create_inplace((BusState *)&m->adb_bus, sizeof(m->adb_bus),
                        TYPE_ADB_BUS, DEVICE(obj), "adb.0");
}

static const VMStateDescription vmstate_mac_via = {
    .name = "mac-via",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        /* VIAs */
        VMSTATE_STRUCT(mos6522_via1.parent_obj, MacVIAState, 0, vmstate_mos6522,
                       MOS6522State),
        VMSTATE_UINT8(mos6522_via1.last_b, MacVIAState),
        VMSTATE_BUFFER(mos6522_via1.PRAM, MacVIAState),
        VMSTATE_TIMER_PTR(mos6522_via1.one_second_timer, MacVIAState),
        VMSTATE_INT64(mos6522_via1.next_second, MacVIAState),
        VMSTATE_TIMER_PTR(mos6522_via1.VBL_timer, MacVIAState),
        VMSTATE_INT64(mos6522_via1.next_VBL, MacVIAState),
        VMSTATE_STRUCT(mos6522_via2.parent_obj, MacVIAState, 0, vmstate_mos6522,
                       MOS6522State),
        /* RTC */
        VMSTATE_UINT32(tick_offset, MacVIAState),
        VMSTATE_UINT8(data_out, MacVIAState),
        VMSTATE_INT32(data_out_cnt, MacVIAState),
        VMSTATE_UINT8(data_in, MacVIAState),
        VMSTATE_UINT8(data_in_cnt, MacVIAState),
        VMSTATE_UINT8(cmd, MacVIAState),
        VMSTATE_INT32(wprotect, MacVIAState),
        VMSTATE_INT32(alt, MacVIAState),
        /* ADB */
        VMSTATE_TIMER_PTR(adb_poll_timer, MacVIAState),
        VMSTATE_INT32(adb_data_in_size, MacVIAState),
        VMSTATE_INT32(adb_data_in_index, MacVIAState),
        VMSTATE_INT32(adb_data_out_index, MacVIAState),
        VMSTATE_BUFFER(adb_data_in, MacVIAState),
        VMSTATE_BUFFER(adb_data_out, MacVIAState),
        VMSTATE_END_OF_LIST()
    }
};

static void mac_via_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = mac_via_realize;
    dc->reset = mac_via_reset;
    dc->vmsd = &vmstate_mac_via;
}

static TypeInfo mac_via_info = {
    .name = TYPE_MAC_VIA,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MacVIAState),
    .instance_init = mac_via_init,
    .class_init = mac_via_class_init,
};

/* VIA 1 */
static void mos6522_q800_via1_portB_write(MOS6522State *s)
{
    MOS6522Q800VIA1State *v1s = container_of(s, MOS6522Q800VIA1State,
                                             parent_obj);
    MacVIAState *m = container_of(v1s, MacVIAState, mos6522_via1);

    via1_rtc_update(m);
    via1_adb_update(m);

    v1s->last_b = s->b;
}

static void mos6522_q800_via1_reset(DeviceState *dev)
{
    MOS6522State *ms = MOS6522(dev);
    MOS6522DeviceClass *mdc = MOS6522_DEVICE_GET_CLASS(ms);

    mdc->parent_reset(dev);

    ms->timers[0].frequency = VIA_TIMER_FREQ;
    ms->timers[1].frequency = VIA_TIMER_FREQ;

    ms->b = VIA1B_vADB_StateMask | VIA1B_vADBInt | VIA1B_vRTCEnb;
}

static void mos6522_q800_via1_init(Object *obj)
{
    qdev_init_gpio_in_named(DEVICE(obj), via1_irq_request, "via1-irq",
                            VIA1_IRQ_NB);
}

static void mos6522_q800_via1_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    MOS6522DeviceClass *mdc = MOS6522_DEVICE_CLASS(oc);

    dc->reset = mos6522_q800_via1_reset;
    mdc->portB_write = mos6522_q800_via1_portB_write;
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

static void mos6522_q800_via2_reset(DeviceState *dev)
{
    MOS6522State *ms = MOS6522(dev);
    MOS6522DeviceClass *mdc = MOS6522_DEVICE_GET_CLASS(ms);

    mdc->parent_reset(dev);

    ms->timers[0].frequency = VIA_TIMER_FREQ;
    ms->timers[1].frequency = VIA_TIMER_FREQ;

    ms->dirb = 0;
    ms->b = 0;
}

static void mos6522_q800_via2_init(Object *obj)
{
    qdev_init_gpio_in_named(DEVICE(obj), via2_irq_request, "via2-irq",
                            VIA2_IRQ_NB);
}

static void mos6522_q800_via2_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    MOS6522DeviceClass *mdc = MOS6522_DEVICE_CLASS(oc);

    dc->reset = mos6522_q800_via2_reset;
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
    type_register_static(&mac_via_info);
}

type_init(mac_via_register_types);
