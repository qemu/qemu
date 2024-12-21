/*
 * dwc-hsotg (dwc2) USB host controller emulation
 *
 * Based on hw/usb/hcd-ehci.c and hw/usb/hcd-ohci.c
 *
 * Note that to use this emulation with the dwc-otg driver in the
 * Raspbian kernel, you must pass the option "dwc_otg.fiq_fsm_enable=0"
 * on the kernel command line.
 *
 * Some useful documentation used to develop this emulation can be
 * found online (as of April 2020) at:
 *
 * http://www.capital-micro.com/PDF/CME-M7_Family_User_Guide_EN.pdf
 * which has a pretty complete description of the controller starting
 * on page 370.
 *
 * https://sourceforge.net/p/wive-ng/wive-ng-mt/ci/master/tree/docs/DataSheets/RT3050_5x_V2.0_081408_0902.pdf
 * which has a description of the controller registers starting on
 * page 130.
 *
 * Copyright (c) 2020 Paul Zimmerman <pauldzim@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/usb/dwc2-regs.h"
#include "hw/usb/hcd-dwc2.h"
#include "migration/vmstate.h"
#include "trace.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "hw/qdev-properties.h"

#define USB_HZ_FS       12000000
#define USB_HZ_HS       96000000
#define USB_FRMINTVL    12000

/* nifty macros from Arnon's EHCI version  */
#define get_field(data, field) \
    (((data) & field##_MASK) >> field##_SHIFT)

#define set_field(data, newval, field) do { \
    uint32_t val = *(data); \
    val &= ~field##_MASK; \
    val |= ((newval) << field##_SHIFT) & field##_MASK; \
    *(data) = val; \
} while (0)

#define get_bit(data, bitmask) \
    (!!((data) & (bitmask)))

/* update irq line */
static inline void dwc2_update_irq(DWC2State *s)
{
    static int oldlevel;
    int level = 0;

    if ((s->gintsts & s->gintmsk) && (s->gahbcfg & GAHBCFG_GLBL_INTR_EN)) {
        level = 1;
    }
    if (level != oldlevel) {
        oldlevel = level;
        trace_usb_dwc2_update_irq(level);
        qemu_set_irq(s->irq, level);
    }
}

/* flag interrupt condition */
static inline void dwc2_raise_global_irq(DWC2State *s, uint32_t intr)
{
    if (!(s->gintsts & intr)) {
        s->gintsts |= intr;
        trace_usb_dwc2_raise_global_irq(intr);
        dwc2_update_irq(s);
    }
}

static inline void dwc2_lower_global_irq(DWC2State *s, uint32_t intr)
{
    if (s->gintsts & intr) {
        s->gintsts &= ~intr;
        trace_usb_dwc2_lower_global_irq(intr);
        dwc2_update_irq(s);
    }
}

static inline void dwc2_raise_host_irq(DWC2State *s, uint32_t host_intr)
{
    if (!(s->haint & host_intr)) {
        s->haint |= host_intr;
        s->haint &= 0xffff;
        trace_usb_dwc2_raise_host_irq(host_intr);
        if (s->haint & s->haintmsk) {
            dwc2_raise_global_irq(s, GINTSTS_HCHINT);
        }
    }
}

static inline void dwc2_lower_host_irq(DWC2State *s, uint32_t host_intr)
{
    if (s->haint & host_intr) {
        s->haint &= ~host_intr;
        trace_usb_dwc2_lower_host_irq(host_intr);
        if (!(s->haint & s->haintmsk)) {
            dwc2_lower_global_irq(s, GINTSTS_HCHINT);
        }
    }
}

static inline void dwc2_update_hc_irq(DWC2State *s, int index)
{
    uint32_t host_intr = 1 << (index >> 3);

    if (s->hreg1[index + 2] & s->hreg1[index + 3]) {
        dwc2_raise_host_irq(s, host_intr);
    } else {
        dwc2_lower_host_irq(s, host_intr);
    }
}

/* set a timer for EOF */
static void dwc2_eof_timer(DWC2State *s)
{
    timer_mod(s->eof_timer, s->sof_time + s->usb_frame_time);
}

/* Set a timer for EOF and generate SOF event */
static void dwc2_sof(DWC2State *s)
{
    s->sof_time += s->usb_frame_time;
    trace_usb_dwc2_sof(s->sof_time);
    dwc2_eof_timer(s);
    dwc2_raise_global_irq(s, GINTSTS_SOF);
}

/* Do frame processing on frame boundary */
static void dwc2_frame_boundary(void *opaque)
{
    DWC2State *s = opaque;
    int64_t now;
    uint16_t frcnt;

    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    /* Frame boundary, so do EOF stuff here */

    /* Increment frame number */
    frcnt = (uint16_t)((now - s->sof_time) / s->fi);
    s->frame_number = (s->frame_number + frcnt) & 0xffff;
    s->hfnum = s->frame_number & HFNUM_MAX_FRNUM;

    /* Do SOF stuff here */
    dwc2_sof(s);
}

/* Start sending SOF tokens on the USB bus */
static void dwc2_bus_start(DWC2State *s)
{
    trace_usb_dwc2_bus_start();
    s->sof_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    dwc2_eof_timer(s);
}

/* Stop sending SOF tokens on the USB bus */
static void dwc2_bus_stop(DWC2State *s)
{
    trace_usb_dwc2_bus_stop();
    timer_del(s->eof_timer);
}

static USBDevice *dwc2_find_device(DWC2State *s, uint8_t addr)
{
    USBDevice *dev;

    trace_usb_dwc2_find_device(addr);

    if (!(s->hprt0 & HPRT0_ENA)) {
        trace_usb_dwc2_port_disabled(0);
    } else {
        dev = usb_find_device(&s->uport, addr);
        if (dev != NULL) {
            trace_usb_dwc2_device_found(0);
            return dev;
        }
    }

    trace_usb_dwc2_device_not_found();
    return NULL;
}

static const char *pstatus[] = {
    "USB_RET_SUCCESS", "USB_RET_NODEV", "USB_RET_NAK", "USB_RET_STALL",
    "USB_RET_BABBLE", "USB_RET_IOERROR", "USB_RET_ASYNC",
    "USB_RET_ADD_TO_QUEUE", "USB_RET_REMOVE_FROM_QUEUE"
};

static uint32_t pintr[] = {
    HCINTMSK_XFERCOMPL, HCINTMSK_XACTERR, HCINTMSK_NAK, HCINTMSK_STALL,
    HCINTMSK_BBLERR, HCINTMSK_XACTERR, HCINTMSK_XACTERR, HCINTMSK_XACTERR,
    HCINTMSK_XACTERR
};

static const char *types[] = {
    "Ctrl", "Isoc", "Bulk", "Intr"
};

static const char *dirs[] = {
    "Out", "In"
};

static void dwc2_handle_packet(DWC2State *s, uint32_t devadr, USBDevice *dev,
                               USBEndpoint *ep, uint32_t index, bool send)
{
    DWC2Packet *p;
    uint32_t hcchar = s->hreg1[index];
    uint32_t hctsiz = s->hreg1[index + 4];
    uint32_t hcdma = s->hreg1[index + 5];
    uint32_t chan, epnum, epdir, eptype, mps, pid, pcnt, len, tlen, intr = 0;
    uint32_t tpcnt, stsidx, actual = 0;
    bool do_intr = false, done = false;

    epnum = get_field(hcchar, HCCHAR_EPNUM);
    epdir = get_bit(hcchar, HCCHAR_EPDIR);
    eptype = get_field(hcchar, HCCHAR_EPTYPE);
    mps = get_field(hcchar, HCCHAR_MPS);
    pid = get_field(hctsiz, TSIZ_SC_MC_PID);
    pcnt = get_field(hctsiz, TSIZ_PKTCNT);
    len = get_field(hctsiz, TSIZ_XFERSIZE);
    if (len > DWC2_MAX_XFER_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: HCTSIZ transfer size too large\n", __func__);
        return;
    }

    chan = index >> 3;
    p = &s->packet[chan];

    trace_usb_dwc2_handle_packet(chan, dev, &p->packet, epnum, types[eptype],
                                 dirs[epdir], mps, len, pcnt);

    if (mps == 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                "%s: Bad HCCHAR_MPS set to zero\n", __func__);
        return;
    }

    if (eptype == USB_ENDPOINT_XFER_CONTROL && pid == TSIZ_SC_MC_PID_SETUP) {
        pid = USB_TOKEN_SETUP;
    } else {
        pid = epdir ? USB_TOKEN_IN : USB_TOKEN_OUT;
    }

    if (send) {
        tlen = len;
        if (p->small) {
            if (tlen > mps) {
                tlen = mps;
            }
        }

        if (pid != USB_TOKEN_IN) {
            trace_usb_dwc2_memory_read(hcdma, tlen);
            if (dma_memory_read(&s->dma_as, hcdma, s->usb_buf[chan], tlen,
                                MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: dma_memory_read failed\n",
                              __func__);
            }
        }

        usb_packet_init(&p->packet);
        usb_packet_setup(&p->packet, pid, ep, 0, hcdma,
                         pid != USB_TOKEN_IN, true);
        usb_packet_addbuf(&p->packet, s->usb_buf[chan], tlen);
        p->async = DWC2_ASYNC_NONE;
        usb_handle_packet(dev, &p->packet);
    } else {
        tlen = p->len;
    }

    stsidx = -p->packet.status;
    assert(stsidx < sizeof(pstatus) / sizeof(*pstatus));
    actual = p->packet.actual_length;
    trace_usb_dwc2_packet_status(pstatus[stsidx], actual);

babble:
    if (p->packet.status != USB_RET_SUCCESS &&
            p->packet.status != USB_RET_NAK &&
            p->packet.status != USB_RET_STALL &&
            p->packet.status != USB_RET_ASYNC) {
        trace_usb_dwc2_packet_error(pstatus[stsidx]);
    }

    if (p->packet.status == USB_RET_ASYNC) {
        trace_usb_dwc2_async_packet(&p->packet, chan, dev, epnum,
                                    dirs[epdir], tlen);
        usb_device_flush_ep_queue(dev, ep);
        assert(p->async != DWC2_ASYNC_INFLIGHT);
        p->devadr = devadr;
        p->epnum = epnum;
        p->epdir = epdir;
        p->mps = mps;
        p->pid = pid;
        p->index = index;
        p->pcnt = pcnt;
        p->len = tlen;
        p->async = DWC2_ASYNC_INFLIGHT;
        p->needs_service = false;
        return;
    }

    if (p->packet.status == USB_RET_SUCCESS) {
        if (actual > tlen) {
            p->packet.status = USB_RET_BABBLE;
            goto babble;
        }

        if (pid == USB_TOKEN_IN) {
            trace_usb_dwc2_memory_write(hcdma, actual);
            if (dma_memory_write(&s->dma_as, hcdma, s->usb_buf[chan], actual,
                                 MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: dma_memory_write failed\n",
                              __func__);
            }
        }

        tpcnt = actual / mps;
        if (actual % mps) {
            tpcnt++;
            if (pid == USB_TOKEN_IN) {
                done = true;
            }
        }

        pcnt -= tpcnt < pcnt ? tpcnt : pcnt;
        set_field(&hctsiz, pcnt, TSIZ_PKTCNT);
        len -= actual < len ? actual : len;
        set_field(&hctsiz, len, TSIZ_XFERSIZE);
        s->hreg1[index + 4] = hctsiz;
        hcdma += actual;
        s->hreg1[index + 5] = hcdma;

        if (!pcnt || len == 0 || actual == 0) {
            done = true;
        }
    } else {
        intr |= pintr[stsidx];
        if (p->packet.status == USB_RET_NAK &&
            (eptype == USB_ENDPOINT_XFER_CONTROL ||
             eptype == USB_ENDPOINT_XFER_BULK)) {
            /*
             * for ctrl/bulk, automatically retry on NAK,
             * but send the interrupt anyway
             */
            intr &= ~HCINTMSK_RESERVED14_31;
            s->hreg1[index + 2] |= intr;
            do_intr = true;
        } else {
            intr |= HCINTMSK_CHHLTD;
            done = true;
        }
    }

    usb_packet_cleanup(&p->packet);

    if (done) {
        hcchar &= ~HCCHAR_CHENA;
        s->hreg1[index] = hcchar;
        if (!(intr & HCINTMSK_CHHLTD)) {
            intr |= HCINTMSK_CHHLTD | HCINTMSK_XFERCOMPL;
        }
        intr &= ~HCINTMSK_RESERVED14_31;
        s->hreg1[index + 2] |= intr;
        p->needs_service = false;
        trace_usb_dwc2_packet_done(pstatus[stsidx], actual, len, pcnt);
        dwc2_update_hc_irq(s, index);
        return;
    }

    p->devadr = devadr;
    p->epnum = epnum;
    p->epdir = epdir;
    p->mps = mps;
    p->pid = pid;
    p->index = index;
    p->pcnt = pcnt;
    p->len = len;
    p->needs_service = true;
    trace_usb_dwc2_packet_next(pstatus[stsidx], len, pcnt);
    if (do_intr) {
        dwc2_update_hc_irq(s, index);
    }
}

/* Attach or detach a device on root hub */

static const char *speeds[] = {
    "low", "full", "high"
};

static void dwc2_attach(USBPort *port)
{
    DWC2State *s = port->opaque;
    int hispd = 0;

    trace_usb_dwc2_attach(port);
    assert(port->index == 0);

    if (!port->dev || !port->dev->attached) {
        return;
    }

    assert(port->dev->speed <= USB_SPEED_HIGH);
    trace_usb_dwc2_attach_speed(speeds[port->dev->speed]);
    s->hprt0 &= ~HPRT0_SPD_MASK;

    switch (port->dev->speed) {
    case USB_SPEED_LOW:
        s->hprt0 |= HPRT0_SPD_LOW_SPEED << HPRT0_SPD_SHIFT;
        break;
    case USB_SPEED_FULL:
        s->hprt0 |= HPRT0_SPD_FULL_SPEED << HPRT0_SPD_SHIFT;
        break;
    case USB_SPEED_HIGH:
        s->hprt0 |= HPRT0_SPD_HIGH_SPEED << HPRT0_SPD_SHIFT;
        hispd = 1;
        break;
    }

    if (hispd) {
        s->usb_frame_time = NANOSECONDS_PER_SECOND / 8000;        /* 125000 */
        if (NANOSECONDS_PER_SECOND >= USB_HZ_HS) {
            s->usb_bit_time = NANOSECONDS_PER_SECOND / USB_HZ_HS; /* 10.4 */
        } else {
            s->usb_bit_time = 1;
        }
    } else {
        s->usb_frame_time = NANOSECONDS_PER_SECOND / 1000;        /* 1000000 */
        if (NANOSECONDS_PER_SECOND >= USB_HZ_FS) {
            s->usb_bit_time = NANOSECONDS_PER_SECOND / USB_HZ_FS; /* 83.3 */
        } else {
            s->usb_bit_time = 1;
        }
    }

    s->fi = USB_FRMINTVL - 1;
    s->hprt0 |= HPRT0_CONNDET | HPRT0_CONNSTS;

    dwc2_bus_start(s);
    dwc2_raise_global_irq(s, GINTSTS_PRTINT);
}

static void dwc2_detach(USBPort *port)
{
    DWC2State *s = port->opaque;

    trace_usb_dwc2_detach(port);
    assert(port->index == 0);

    dwc2_bus_stop(s);

    s->hprt0 &= ~(HPRT0_SPD_MASK | HPRT0_SUSP | HPRT0_ENA | HPRT0_CONNSTS);
    s->hprt0 |= HPRT0_CONNDET | HPRT0_ENACHG;

    dwc2_raise_global_irq(s, GINTSTS_PRTINT);
}

static void dwc2_child_detach(USBPort *port, USBDevice *child)
{
    trace_usb_dwc2_child_detach(port, child);
    assert(port->index == 0);
}

static void dwc2_wakeup(USBPort *port)
{
    DWC2State *s = port->opaque;

    trace_usb_dwc2_wakeup(port);
    assert(port->index == 0);

    if (s->hprt0 & HPRT0_SUSP) {
        s->hprt0 |= HPRT0_RES;
        dwc2_raise_global_irq(s, GINTSTS_PRTINT);
    }

    qemu_bh_schedule(s->async_bh);
}

static void dwc2_async_packet_complete(USBPort *port, USBPacket *packet)
{
    DWC2State *s = port->opaque;
    DWC2Packet *p;
    USBDevice *dev;
    USBEndpoint *ep;

    assert(port->index == 0);
    p = container_of(packet, DWC2Packet, packet);
    dev = dwc2_find_device(s, p->devadr);
    ep = usb_ep_get(dev, p->pid, p->epnum);
    trace_usb_dwc2_async_packet_complete(port, packet, p->index >> 3, dev,
                                         p->epnum, dirs[p->epdir], p->len);
    assert(p->async == DWC2_ASYNC_INFLIGHT);

    if (packet->status == USB_RET_REMOVE_FROM_QUEUE) {
        usb_cancel_packet(packet);
        usb_packet_cleanup(packet);
        return;
    }

    dwc2_handle_packet(s, p->devadr, dev, ep, p->index, false);

    p->async = DWC2_ASYNC_FINISHED;
    qemu_bh_schedule(s->async_bh);
}

static USBPortOps dwc2_port_ops = {
    .attach = dwc2_attach,
    .detach = dwc2_detach,
    .child_detach = dwc2_child_detach,
    .wakeup = dwc2_wakeup,
    .complete = dwc2_async_packet_complete,
};

static uint32_t dwc2_get_frame_remaining(DWC2State *s)
{
    uint32_t fr = 0;
    int64_t tks;

    tks = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - s->sof_time;
    if (tks < 0) {
        tks = 0;
    }

    /* avoid muldiv if possible */
    if (tks >= s->usb_frame_time) {
        goto out;
    }
    if (tks < s->usb_bit_time) {
        fr = s->fi;
        goto out;
    }

    /* tks = number of ns since SOF, divided by 83 (fs) or 10 (hs) */
    tks = tks / s->usb_bit_time;
    if (tks >= (int64_t)s->fi) {
        goto out;
    }

    /* remaining = frame interval minus tks */
    fr = (uint32_t)((int64_t)s->fi - tks);

out:
    return fr;
}

static void dwc2_work_bh(void *opaque)
{
    DWC2State *s = opaque;
    DWC2Packet *p;
    USBDevice *dev;
    USBEndpoint *ep;
    int64_t t_now, expire_time;
    int chan;
    bool found = false;

    trace_usb_dwc2_work_bh();
    if (s->working) {
        return;
    }
    s->working = true;

    t_now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    chan = s->next_chan;

    do {
        p = &s->packet[chan];
        if (p->needs_service) {
            dev = dwc2_find_device(s, p->devadr);
            ep = usb_ep_get(dev, p->pid, p->epnum);
            trace_usb_dwc2_work_bh_service(s->next_chan, chan, dev, p->epnum);
            dwc2_handle_packet(s, p->devadr, dev, ep, p->index, true);
            found = true;
        }
        if (++chan == DWC2_NB_CHAN) {
            chan = 0;
        }
        if (found) {
            s->next_chan = chan;
            trace_usb_dwc2_work_bh_next(chan);
        }
    } while (chan != s->next_chan);

    if (found) {
        expire_time = t_now + NANOSECONDS_PER_SECOND / 4000;
        timer_mod(s->frame_timer, expire_time);
    }
    s->working = false;
}

static void dwc2_enable_chan(DWC2State *s,  uint32_t index)
{
    USBDevice *dev;
    USBEndpoint *ep;
    uint32_t hcchar;
    uint32_t hctsiz;
    uint32_t devadr, epnum, epdir, eptype, pid, len;
    DWC2Packet *p;

    assert((index >> 3) < DWC2_NB_CHAN);
    p = &s->packet[index >> 3];
    hcchar = s->hreg1[index];
    hctsiz = s->hreg1[index + 4];
    devadr = get_field(hcchar, HCCHAR_DEVADDR);
    epnum = get_field(hcchar, HCCHAR_EPNUM);
    epdir = get_bit(hcchar, HCCHAR_EPDIR);
    eptype = get_field(hcchar, HCCHAR_EPTYPE);
    pid = get_field(hctsiz, TSIZ_SC_MC_PID);
    len = get_field(hctsiz, TSIZ_XFERSIZE);

    dev = dwc2_find_device(s, devadr);

    trace_usb_dwc2_enable_chan(index >> 3, dev, &p->packet, epnum);
    if (dev == NULL) {
        return;
    }

    if (eptype == USB_ENDPOINT_XFER_CONTROL && pid == TSIZ_SC_MC_PID_SETUP) {
        pid = USB_TOKEN_SETUP;
    } else {
        pid = epdir ? USB_TOKEN_IN : USB_TOKEN_OUT;
    }

    ep = usb_ep_get(dev, pid, epnum);

    /*
     * Hack: Networking doesn't like us delivering large transfers, it kind
     * of works but the latency is horrible. So if the transfer is <= the mtu
     * size, we take that as a hint that this might be a network transfer,
     * and do the transfer packet-by-packet.
     */
    if (len > 1536) {
        p->small = false;
    } else {
        p->small = true;
    }

    dwc2_handle_packet(s, devadr, dev, ep, index, true);
    qemu_bh_schedule(s->async_bh);
}

static const char *glbregnm[] = {
    "GOTGCTL  ", "GOTGINT  ", "GAHBCFG  ", "GUSBCFG  ", "GRSTCTL  ",
    "GINTSTS  ", "GINTMSK  ", "GRXSTSR  ", "GRXSTSP  ", "GRXFSIZ  ",
    "GNPTXFSIZ", "GNPTXSTS ", "GI2CCTL  ", "GPVNDCTL ", "GGPIO    ",
    "GUID     ", "GSNPSID  ", "GHWCFG1  ", "GHWCFG2  ", "GHWCFG3  ",
    "GHWCFG4  ", "GLPMCFG  ", "GPWRDN   ", "GDFIFOCFG", "GADPCTL  ",
    "GREFCLK  ", "GINTMSK2 ", "GINTSTS2 "
};

static uint64_t dwc2_glbreg_read(void *ptr, hwaddr addr, int index,
                                 unsigned size)
{
    DWC2State *s = ptr;
    uint32_t val;

    if (addr > GINTSTS2) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        return 0;
    }

    val = s->glbreg[index];

    switch (addr) {
    case GRSTCTL:
        /* clear any self-clearing bits that were set */
        val &= ~(GRSTCTL_TXFFLSH | GRSTCTL_RXFFLSH | GRSTCTL_IN_TKNQ_FLSH |
                 GRSTCTL_FRMCNTRRST | GRSTCTL_HSFTRST | GRSTCTL_CSFTRST);
        s->glbreg[index] = val;
        break;
    default:
        break;
    }

    trace_usb_dwc2_glbreg_read(addr, glbregnm[index], val);
    return val;
}

static void dwc2_glbreg_write(void *ptr, hwaddr addr, int index, uint64_t val,
                              unsigned size)
{
    DWC2State *s = ptr;
    uint64_t orig = val;
    uint32_t *mmio;
    uint32_t old;
    int iflg = 0;

    if (addr > GINTSTS2) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        return;
    }

    mmio = &s->glbreg[index];
    old = *mmio;

    switch (addr) {
    case GOTGCTL:
        /* don't allow setting of read-only bits */
        val &= ~(GOTGCTL_MULT_VALID_BC_MASK | GOTGCTL_BSESVLD |
                 GOTGCTL_ASESVLD | GOTGCTL_DBNC_SHORT | GOTGCTL_CONID_B |
                 GOTGCTL_HSTNEGSCS | GOTGCTL_SESREQSCS);
        /* don't allow clearing of read-only bits */
        val |= old & (GOTGCTL_MULT_VALID_BC_MASK | GOTGCTL_BSESVLD |
                      GOTGCTL_ASESVLD | GOTGCTL_DBNC_SHORT | GOTGCTL_CONID_B |
                      GOTGCTL_HSTNEGSCS | GOTGCTL_SESREQSCS);
        break;
    case GAHBCFG:
        if ((val & GAHBCFG_GLBL_INTR_EN) && !(old & GAHBCFG_GLBL_INTR_EN)) {
            iflg = 1;
        }
        break;
    case GRSTCTL:
        val |= GRSTCTL_AHBIDLE;
        val &= ~GRSTCTL_DMAREQ;
        if (!(old & GRSTCTL_TXFFLSH) && (val & GRSTCTL_TXFFLSH)) {
                /* TODO - TX fifo flush */
            qemu_log_mask(LOG_UNIMP, "%s: Tx FIFO flush not implemented\n",
                          __func__);
        }
        if (!(old & GRSTCTL_RXFFLSH) && (val & GRSTCTL_RXFFLSH)) {
                /* TODO - RX fifo flush */
            qemu_log_mask(LOG_UNIMP, "%s: Rx FIFO flush not implemented\n",
                          __func__);
        }
        if (!(old & GRSTCTL_IN_TKNQ_FLSH) && (val & GRSTCTL_IN_TKNQ_FLSH)) {
                /* TODO - device IN token queue flush */
            qemu_log_mask(LOG_UNIMP, "%s: Token queue flush not implemented\n",
                          __func__);
        }
        if (!(old & GRSTCTL_FRMCNTRRST) && (val & GRSTCTL_FRMCNTRRST)) {
                /* TODO - host frame counter reset */
            qemu_log_mask(LOG_UNIMP,
                          "%s: Frame counter reset not implemented\n",
                          __func__);
        }
        if (!(old & GRSTCTL_HSFTRST) && (val & GRSTCTL_HSFTRST)) {
                /* TODO - host soft reset */
            qemu_log_mask(LOG_UNIMP, "%s: Host soft reset not implemented\n",
                          __func__);
        }
        if (!(old & GRSTCTL_CSFTRST) && (val & GRSTCTL_CSFTRST)) {
                /* TODO - core soft reset */
            qemu_log_mask(LOG_UNIMP, "%s: Core soft reset not implemented\n",
                          __func__);
        }
        /* don't allow clearing of self-clearing bits */
        val |= old & (GRSTCTL_TXFFLSH | GRSTCTL_RXFFLSH |
                      GRSTCTL_IN_TKNQ_FLSH | GRSTCTL_FRMCNTRRST |
                      GRSTCTL_HSFTRST | GRSTCTL_CSFTRST);
        break;
    case GINTSTS:
        /* clear the write-1-to-clear bits */
        val |= ~old;
        val = ~val;
        /* don't allow clearing of read-only bits */
        val |= old & (GINTSTS_PTXFEMP | GINTSTS_HCHINT | GINTSTS_PRTINT |
                      GINTSTS_OEPINT | GINTSTS_IEPINT | GINTSTS_GOUTNAKEFF |
                      GINTSTS_GINNAKEFF | GINTSTS_NPTXFEMP | GINTSTS_RXFLVL |
                      GINTSTS_OTGINT | GINTSTS_CURMODE_HOST);
        iflg = 1;
        break;
    case GINTMSK:
        iflg = 1;
        break;
    default:
        break;
    }

    trace_usb_dwc2_glbreg_write(addr, glbregnm[index], orig, old, val);
    *mmio = val;

    if (iflg) {
        dwc2_update_irq(s);
    }
}

static uint64_t dwc2_fszreg_read(void *ptr, hwaddr addr, int index,
                                 unsigned size)
{
    DWC2State *s = ptr;
    uint32_t val;

    if (addr != HPTXFSIZ) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        return 0;
    }

    val = s->fszreg[index];

    trace_usb_dwc2_fszreg_read(addr, val);
    return val;
}

static void dwc2_fszreg_write(void *ptr, hwaddr addr, int index, uint64_t val,
                              unsigned size)
{
    DWC2State *s = ptr;
    uint64_t orig = val;
    uint32_t *mmio;
    uint32_t old;

    if (addr != HPTXFSIZ) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        return;
    }

    mmio = &s->fszreg[index];
    old = *mmio;

    trace_usb_dwc2_fszreg_write(addr, orig, old, val);
    *mmio = val;
}

static const char *hreg0nm[] = {
    "HCFG     ", "HFIR     ", "HFNUM    ", "<rsvd>   ", "HPTXSTS  ",
    "HAINT    ", "HAINTMSK ", "HFLBADDR ", "<rsvd>   ", "<rsvd>   ",
    "<rsvd>   ", "<rsvd>   ", "<rsvd>   ", "<rsvd>   ", "<rsvd>   ",
    "<rsvd>   ", "HPRT0    "
};

static uint64_t dwc2_hreg0_read(void *ptr, hwaddr addr, int index,
                                unsigned size)
{
    DWC2State *s = ptr;
    uint32_t val;

    if (addr < HCFG || addr > HPRT0) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        return 0;
    }

    val = s->hreg0[index];

    switch (addr) {
    case HFNUM:
        val = (dwc2_get_frame_remaining(s) << HFNUM_FRREM_SHIFT) |
              (s->hfnum << HFNUM_FRNUM_SHIFT);
        break;
    default:
        break;
    }

    trace_usb_dwc2_hreg0_read(addr, hreg0nm[index], val);
    return val;
}

static void dwc2_hreg0_write(void *ptr, hwaddr addr, int index, uint64_t val,
                             unsigned size)
{
    DWC2State *s = ptr;
    USBDevice *dev = s->uport.dev;
    uint64_t orig = val;
    uint32_t *mmio;
    uint32_t tval, told, old;
    int prst = 0;
    int iflg = 0;

    if (addr < HCFG || addr > HPRT0) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        return;
    }

    mmio = &s->hreg0[index];
    old = *mmio;

    switch (addr) {
    case HFIR:
        break;
    case HFNUM:
    case HPTXSTS:
    case HAINT:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write to read-only register\n",
                      __func__);
        return;
    case HAINTMSK:
        val &= 0xffff;
        break;
    case HPRT0:
        /* don't allow clearing of read-only bits */
        val |= old & (HPRT0_SPD_MASK | HPRT0_LNSTS_MASK | HPRT0_OVRCURRACT |
                      HPRT0_CONNSTS);
        /* don't allow clearing of self-clearing bits */
        val |= old & (HPRT0_SUSP | HPRT0_RES);
        /* don't allow setting of self-setting bits */
        if (!(old & HPRT0_ENA) && (val & HPRT0_ENA)) {
            val &= ~HPRT0_ENA;
        }
        /* clear the write-1-to-clear bits */
        tval = val & (HPRT0_OVRCURRCHG | HPRT0_ENACHG | HPRT0_ENA |
                      HPRT0_CONNDET);
        told = old & (HPRT0_OVRCURRCHG | HPRT0_ENACHG | HPRT0_ENA |
                      HPRT0_CONNDET);
        tval |= ~told;
        tval = ~tval;
        tval &= (HPRT0_OVRCURRCHG | HPRT0_ENACHG | HPRT0_ENA |
                 HPRT0_CONNDET);
        val &= ~(HPRT0_OVRCURRCHG | HPRT0_ENACHG | HPRT0_ENA |
                 HPRT0_CONNDET);
        val |= tval;
        if (!(val & HPRT0_RST) && (old & HPRT0_RST)) {
            if (dev && dev->attached) {
                val |= HPRT0_ENA | HPRT0_ENACHG;
                prst = 1;
            }
        }
        if (val & (HPRT0_OVRCURRCHG | HPRT0_ENACHG | HPRT0_CONNDET)) {
            iflg = 1;
        } else {
            iflg = -1;
        }
        break;
    default:
        break;
    }

    if (prst) {
        trace_usb_dwc2_hreg0_write(addr, hreg0nm[index], orig, old,
                                   val & ~HPRT0_CONNDET);
        trace_usb_dwc2_hreg0_action("call usb_port_reset");
        usb_port_reset(&s->uport);
        val &= ~HPRT0_CONNDET;
    } else {
        trace_usb_dwc2_hreg0_write(addr, hreg0nm[index], orig, old, val);
    }

    *mmio = val;

    if (iflg > 0) {
        trace_usb_dwc2_hreg0_action("enable PRTINT");
        dwc2_raise_global_irq(s, GINTSTS_PRTINT);
    } else if (iflg < 0) {
        trace_usb_dwc2_hreg0_action("disable PRTINT");
        dwc2_lower_global_irq(s, GINTSTS_PRTINT);
    }
}

static const char *hreg1nm[] = {
    "HCCHAR  ", "HCSPLT  ", "HCINT   ", "HCINTMSK", "HCTSIZ  ", "HCDMA   ",
    "<rsvd>  ", "HCDMAB  "
};

static uint64_t dwc2_hreg1_read(void *ptr, hwaddr addr, int index,
                                unsigned size)
{
    DWC2State *s = ptr;
    uint32_t val;

    if (addr < HCCHAR(0) || addr > HCDMAB(DWC2_NB_CHAN - 1)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        return 0;
    }

    val = s->hreg1[index];

    trace_usb_dwc2_hreg1_read(addr, hreg1nm[index & 7], addr >> 5, val);
    return val;
}

static void dwc2_hreg1_write(void *ptr, hwaddr addr, int index, uint64_t val,
                             unsigned size)
{
    DWC2State *s = ptr;
    uint64_t orig = val;
    uint32_t *mmio;
    uint32_t old;
    int iflg = 0;
    int enflg = 0;
    int disflg = 0;

    if (addr < HCCHAR(0) || addr > HCDMAB(DWC2_NB_CHAN - 1)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        return;
    }

    mmio = &s->hreg1[index];
    old = *mmio;

    switch (HSOTG_REG(0x500) + (addr & 0x1c)) {
    case HCCHAR(0):
        if ((val & HCCHAR_CHDIS) && !(old & HCCHAR_CHDIS)) {
            val &= ~(HCCHAR_CHENA | HCCHAR_CHDIS);
            disflg = 1;
        } else {
            val |= old & HCCHAR_CHDIS;
            if ((val & HCCHAR_CHENA) && !(old & HCCHAR_CHENA)) {
                val &= ~HCCHAR_CHDIS;
                enflg = 1;
            } else {
                val |= old & HCCHAR_CHENA;
            }
        }
        break;
    case HCINT(0):
        /* clear the write-1-to-clear bits */
        val |= ~old;
        val = ~val;
        val &= ~HCINTMSK_RESERVED14_31;
        iflg = 1;
        break;
    case HCINTMSK(0):
        val &= ~HCINTMSK_RESERVED14_31;
        iflg = 1;
        break;
    case HCDMAB(0):
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write to read-only register\n",
                      __func__);
        return;
    default:
        break;
    }

    trace_usb_dwc2_hreg1_write(addr, hreg1nm[index & 7], index >> 3, orig,
                               old, val);
    *mmio = val;

    if (disflg) {
        /* set ChHltd in HCINT */
        s->hreg1[(index & ~7) + 2] |= HCINTMSK_CHHLTD;
        iflg = 1;
    }

    if (enflg) {
        dwc2_enable_chan(s, index & ~7);
    }

    if (iflg) {
        dwc2_update_hc_irq(s, index & ~7);
    }
}

static const char *pcgregnm[] = {
        "PCGCTL   ", "PCGCCTL1 "
};

static uint64_t dwc2_pcgreg_read(void *ptr, hwaddr addr, int index,
                                 unsigned size)
{
    DWC2State *s = ptr;
    uint32_t val;

    if (addr < PCGCTL || addr > PCGCCTL1) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        return 0;
    }

    val = s->pcgreg[index];

    trace_usb_dwc2_pcgreg_read(addr, pcgregnm[index], val);
    return val;
}

static void dwc2_pcgreg_write(void *ptr, hwaddr addr, int index,
                              uint64_t val, unsigned size)
{
    DWC2State *s = ptr;
    uint64_t orig = val;
    uint32_t *mmio;
    uint32_t old;

    if (addr < PCGCTL || addr > PCGCCTL1) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        return;
    }

    mmio = &s->pcgreg[index];
    old = *mmio;

    trace_usb_dwc2_pcgreg_write(addr, pcgregnm[index], orig, old, val);
    *mmio = val;
}

static uint64_t dwc2_hsotg_read(void *ptr, hwaddr addr, unsigned size)
{
    uint64_t val;

    switch (addr) {
    case HSOTG_REG(0x000) ... HSOTG_REG(0x0fc):
        val = dwc2_glbreg_read(ptr, addr, (addr - HSOTG_REG(0x000)) >> 2, size);
        break;
    case HSOTG_REG(0x100):
        val = dwc2_fszreg_read(ptr, addr, (addr - HSOTG_REG(0x100)) >> 2, size);
        break;
    case HSOTG_REG(0x104) ... HSOTG_REG(0x3fc):
        /* Gadget-mode registers, just return 0 for now */
        val = 0;
        break;
    case HSOTG_REG(0x400) ... HSOTG_REG(0x4fc):
        val = dwc2_hreg0_read(ptr, addr, (addr - HSOTG_REG(0x400)) >> 2, size);
        break;
    case HSOTG_REG(0x500) ... HSOTG_REG(0x7fc):
        val = dwc2_hreg1_read(ptr, addr, (addr - HSOTG_REG(0x500)) >> 2, size);
        break;
    case HSOTG_REG(0x800) ... HSOTG_REG(0xdfc):
        /* Gadget-mode registers, just return 0 for now */
        val = 0;
        break;
    case HSOTG_REG(0xe00) ... HSOTG_REG(0xffc):
        val = dwc2_pcgreg_read(ptr, addr, (addr - HSOTG_REG(0xe00)) >> 2, size);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        val = 0;
        break;
    }

    return val;
}

static void dwc2_hsotg_write(void *ptr, hwaddr addr, uint64_t val,
                             unsigned size)
{
    switch (addr) {
    case HSOTG_REG(0x000) ... HSOTG_REG(0x0fc):
        dwc2_glbreg_write(ptr, addr, (addr - HSOTG_REG(0x000)) >> 2, val, size);
        break;
    case HSOTG_REG(0x100):
        dwc2_fszreg_write(ptr, addr, (addr - HSOTG_REG(0x100)) >> 2, val, size);
        break;
    case HSOTG_REG(0x104) ... HSOTG_REG(0x3fc):
        /* Gadget-mode registers, do nothing for now */
        break;
    case HSOTG_REG(0x400) ... HSOTG_REG(0x4fc):
        dwc2_hreg0_write(ptr, addr, (addr - HSOTG_REG(0x400)) >> 2, val, size);
        break;
    case HSOTG_REG(0x500) ... HSOTG_REG(0x7fc):
        dwc2_hreg1_write(ptr, addr, (addr - HSOTG_REG(0x500)) >> 2, val, size);
        break;
    case HSOTG_REG(0x800) ... HSOTG_REG(0xdfc):
        /* Gadget-mode registers, do nothing for now */
        break;
    case HSOTG_REG(0xe00) ... HSOTG_REG(0xffc):
        dwc2_pcgreg_write(ptr, addr, (addr - HSOTG_REG(0xe00)) >> 2, val, size);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        break;
    }
}

static const MemoryRegionOps dwc2_mmio_hsotg_ops = {
    .read = dwc2_hsotg_read,
    .write = dwc2_hsotg_write,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t dwc2_hreg2_read(void *ptr, hwaddr addr, unsigned size)
{
    /* TODO - implement FIFOs to support slave mode */
    trace_usb_dwc2_hreg2_read(addr, addr >> 12, 0);
    qemu_log_mask(LOG_UNIMP, "%s: FIFO read not implemented\n", __func__);
    return 0;
}

static void dwc2_hreg2_write(void *ptr, hwaddr addr, uint64_t val,
                             unsigned size)
{
    uint64_t orig = val;

    /* TODO - implement FIFOs to support slave mode */
    trace_usb_dwc2_hreg2_write(addr, addr >> 12, orig, 0, val);
    qemu_log_mask(LOG_UNIMP, "%s: FIFO write not implemented\n", __func__);
}

static const MemoryRegionOps dwc2_mmio_hreg2_ops = {
    .read = dwc2_hreg2_read,
    .write = dwc2_hreg2_write,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void dwc2_wakeup_endpoint(USBBus *bus, USBEndpoint *ep,
                                 unsigned int stream)
{
    DWC2State *s = container_of(bus, DWC2State, bus);

    trace_usb_dwc2_wakeup_endpoint(ep, stream);

    /* TODO - do something here? */
    qemu_bh_schedule(s->async_bh);
}

static USBBusOps dwc2_bus_ops = {
    .wakeup_endpoint = dwc2_wakeup_endpoint,
};

static void dwc2_work_timer(void *opaque)
{
    DWC2State *s = opaque;

    trace_usb_dwc2_work_timer();
    qemu_bh_schedule(s->async_bh);
}

static void dwc2_reset_enter(Object *obj, ResetType type)
{
    DWC2Class *c = DWC2_USB_GET_CLASS(obj);
    DWC2State *s = DWC2_USB(obj);
    int i;

    trace_usb_dwc2_reset_enter();

    if (c->parent_phases.enter) {
        c->parent_phases.enter(obj, type);
    }

    timer_del(s->frame_timer);
    qemu_bh_cancel(s->async_bh);

    if (s->uport.dev && s->uport.dev->attached) {
        usb_detach(&s->uport);
    }

    dwc2_bus_stop(s);

    s->gotgctl = GOTGCTL_BSESVLD | GOTGCTL_ASESVLD | GOTGCTL_CONID_B;
    s->gotgint = 0;
    s->gahbcfg = 0;
    s->gusbcfg = 5 << GUSBCFG_USBTRDTIM_SHIFT;
    s->grstctl = GRSTCTL_AHBIDLE;
    s->gintsts = GINTSTS_CONIDSTSCHNG | GINTSTS_PTXFEMP | GINTSTS_NPTXFEMP |
                 GINTSTS_CURMODE_HOST;
    s->gintmsk = 0;
    s->grxstsr = 0;
    s->grxstsp = 0;
    s->grxfsiz = 1024;
    s->gnptxfsiz = 1024 << FIFOSIZE_DEPTH_SHIFT;
    s->gnptxsts = (4 << FIFOSIZE_DEPTH_SHIFT) | 1024;
    s->gi2cctl = GI2CCTL_I2CDATSE0 | GI2CCTL_ACK;
    s->gpvndctl = 0;
    s->ggpio = 0;
    s->guid = 0;
    s->gsnpsid = 0x4f54294a;
    s->ghwcfg1 = 0;
    s->ghwcfg2 = (8 << GHWCFG2_DEV_TOKEN_Q_DEPTH_SHIFT) |
                 (4 << GHWCFG2_HOST_PERIO_TX_Q_DEPTH_SHIFT) |
                 (4 << GHWCFG2_NONPERIO_TX_Q_DEPTH_SHIFT) |
                 GHWCFG2_DYNAMIC_FIFO |
                 GHWCFG2_PERIO_EP_SUPPORTED |
                 ((DWC2_NB_CHAN - 1) << GHWCFG2_NUM_HOST_CHAN_SHIFT) |
                 (GHWCFG2_INT_DMA_ARCH << GHWCFG2_ARCHITECTURE_SHIFT) |
                 (GHWCFG2_OP_MODE_NO_SRP_CAPABLE_HOST << GHWCFG2_OP_MODE_SHIFT);
    s->ghwcfg3 = (4096 << GHWCFG3_DFIFO_DEPTH_SHIFT) |
                 (4 << GHWCFG3_PACKET_SIZE_CNTR_WIDTH_SHIFT) |
                 (4 << GHWCFG3_XFER_SIZE_CNTR_WIDTH_SHIFT);
    s->ghwcfg4 = 0;
    s->glpmcfg = 0;
    s->gpwrdn = GPWRDN_PWRDNRSTN;
    s->gdfifocfg = 0;
    s->gadpctl = 0;
    s->grefclk = 0;
    s->gintmsk2 = 0;
    s->gintsts2 = 0;

    s->hptxfsiz = 500 << FIFOSIZE_DEPTH_SHIFT;

    s->hcfg = 2 << HCFG_RESVALID_SHIFT;
    s->hfir = 60000;
    s->hfnum = 0x3fff;
    s->hptxsts = (16 << TXSTS_QSPCAVAIL_SHIFT) | 32768;
    s->haint = 0;
    s->haintmsk = 0;
    s->hprt0 = 0;

    memset(s->hreg1, 0, sizeof(s->hreg1));
    memset(s->pcgreg, 0, sizeof(s->pcgreg));

    s->sof_time = 0;
    s->frame_number = 0;
    s->fi = USB_FRMINTVL - 1;
    s->next_chan = 0;
    s->working = false;

    for (i = 0; i < DWC2_NB_CHAN; i++) {
        s->packet[i].needs_service = false;
    }
}

static void dwc2_reset_hold(Object *obj, ResetType type)
{
    DWC2Class *c = DWC2_USB_GET_CLASS(obj);
    DWC2State *s = DWC2_USB(obj);

    trace_usb_dwc2_reset_hold();

    if (c->parent_phases.hold) {
        c->parent_phases.hold(obj, type);
    }

    dwc2_update_irq(s);
}

static void dwc2_reset_exit(Object *obj, ResetType type)
{
    DWC2Class *c = DWC2_USB_GET_CLASS(obj);
    DWC2State *s = DWC2_USB(obj);

    trace_usb_dwc2_reset_exit();

    if (c->parent_phases.exit) {
        c->parent_phases.exit(obj, type);
    }

    s->hprt0 = HPRT0_PWR;
    if (s->uport.dev && s->uport.dev->attached) {
        usb_attach(&s->uport);
        usb_device_reset(s->uport.dev);
    }
}

static void dwc2_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    DWC2State *s = DWC2_USB(dev);
    Object *obj;

    obj = object_property_get_link(OBJECT(dev), "dma-mr", &error_abort);

    s->dma_mr = MEMORY_REGION(obj);
    address_space_init(&s->dma_as, s->dma_mr, "dwc2");

    usb_bus_new(&s->bus, sizeof(s->bus), &dwc2_bus_ops, dev);
    usb_register_port(&s->bus, &s->uport, s, 0, &dwc2_port_ops,
                      USB_SPEED_MASK_LOW | USB_SPEED_MASK_FULL |
                      (s->usb_version == 2 ? USB_SPEED_MASK_HIGH : 0));
    s->uport.dev = 0;

    s->usb_frame_time = NANOSECONDS_PER_SECOND / 1000;          /* 1000000 */
    if (NANOSECONDS_PER_SECOND >= USB_HZ_FS) {
        s->usb_bit_time = NANOSECONDS_PER_SECOND / USB_HZ_FS;   /* 83.3 */
    } else {
        s->usb_bit_time = 1;
    }

    s->fi = USB_FRMINTVL - 1;
    s->eof_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, dwc2_frame_boundary, s);
    s->frame_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, dwc2_work_timer, s);
    s->async_bh = qemu_bh_new_guarded(dwc2_work_bh, s,
                                      &dev->mem_reentrancy_guard);

    sysbus_init_irq(sbd, &s->irq);
}

static void dwc2_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    DWC2State *s = DWC2_USB(obj);

    memory_region_init(&s->container, obj, "dwc2", DWC2_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->container);

    memory_region_init_io(&s->hsotg, obj, &dwc2_mmio_hsotg_ops, s,
                          "dwc2-io", 4 * KiB);
    memory_region_add_subregion(&s->container, 0x0000, &s->hsotg);

    memory_region_init_io(&s->fifos, obj, &dwc2_mmio_hreg2_ops, s,
                          "dwc2-fifo", 64 * KiB);
    memory_region_add_subregion(&s->container, 0x1000, &s->fifos);
}

static const VMStateDescription vmstate_dwc2_state_packet = {
    .name = "dwc2/packet",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(devadr, DWC2Packet),
        VMSTATE_UINT32(epnum, DWC2Packet),
        VMSTATE_UINT32(epdir, DWC2Packet),
        VMSTATE_UINT32(mps, DWC2Packet),
        VMSTATE_UINT32(pid, DWC2Packet),
        VMSTATE_UINT32(index, DWC2Packet),
        VMSTATE_UINT32(pcnt, DWC2Packet),
        VMSTATE_UINT32(len, DWC2Packet),
        VMSTATE_INT32(async, DWC2Packet),
        VMSTATE_BOOL(small, DWC2Packet),
        VMSTATE_BOOL(needs_service, DWC2Packet),
        VMSTATE_END_OF_LIST()
    },
};

const VMStateDescription vmstate_dwc2_state = {
    .name = "dwc2",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(glbreg, DWC2State,
                             DWC2_GLBREG_SIZE / sizeof(uint32_t)),
        VMSTATE_UINT32_ARRAY(fszreg, DWC2State,
                             DWC2_FSZREG_SIZE / sizeof(uint32_t)),
        VMSTATE_UINT32_ARRAY(hreg0, DWC2State,
                             DWC2_HREG0_SIZE / sizeof(uint32_t)),
        VMSTATE_UINT32_ARRAY(hreg1, DWC2State,
                             DWC2_HREG1_SIZE / sizeof(uint32_t)),
        VMSTATE_UINT32_ARRAY(pcgreg, DWC2State,
                             DWC2_PCGREG_SIZE / sizeof(uint32_t)),

        VMSTATE_TIMER_PTR(eof_timer, DWC2State),
        VMSTATE_TIMER_PTR(frame_timer, DWC2State),
        VMSTATE_INT64(sof_time, DWC2State),
        VMSTATE_INT64(usb_frame_time, DWC2State),
        VMSTATE_INT64(usb_bit_time, DWC2State),
        VMSTATE_UINT32(usb_version, DWC2State),
        VMSTATE_UINT16(frame_number, DWC2State),
        VMSTATE_UINT16(fi, DWC2State),
        VMSTATE_UINT16(next_chan, DWC2State),
        VMSTATE_BOOL(working, DWC2State),

        VMSTATE_STRUCT_ARRAY(packet, DWC2State, DWC2_NB_CHAN, 1,
                             vmstate_dwc2_state_packet, DWC2Packet),
        VMSTATE_UINT8_2DARRAY(usb_buf, DWC2State, DWC2_NB_CHAN,
                              DWC2_MAX_XFER_SIZE),

        VMSTATE_END_OF_LIST()
    }
};

static const Property dwc2_usb_properties[] = {
    DEFINE_PROP_UINT32("usb_version", DWC2State, usb_version, 2),
};

static void dwc2_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    DWC2Class *c = DWC2_USB_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = dwc2_realize;
    dc->vmsd = &vmstate_dwc2_state;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
    device_class_set_props(dc, dwc2_usb_properties);
    resettable_class_set_parent_phases(rc, dwc2_reset_enter, dwc2_reset_hold,
                                       dwc2_reset_exit, &c->parent_phases);
}

static const TypeInfo dwc2_usb_type_info = {
    .name          = TYPE_DWC2_USB,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DWC2State),
    .instance_init = dwc2_init,
    .class_size    = sizeof(DWC2Class),
    .class_init    = dwc2_class_init,
};

static void dwc2_usb_register_types(void)
{
    type_register_static(&dwc2_usb_type_info);
}

type_init(dwc2_usb_register_types)
