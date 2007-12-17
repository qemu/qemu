/*
 * USB UHCI controller emulation
 *
 * Copyright (c) 2005 Fabrice Bellard
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
#include "usb.h"
#include "pci.h"
#include "qemu-timer.h"

//#define DEBUG
//#define DEBUG_PACKET
//#define DEBUG_ISOCH

#define UHCI_CMD_FGR      (1 << 4)
#define UHCI_CMD_EGSM     (1 << 3)
#define UHCI_CMD_GRESET   (1 << 2)
#define UHCI_CMD_HCRESET  (1 << 1)
#define UHCI_CMD_RS       (1 << 0)

#define UHCI_STS_HCHALTED (1 << 5)
#define UHCI_STS_HCPERR   (1 << 4)
#define UHCI_STS_HSERR    (1 << 3)
#define UHCI_STS_RD       (1 << 2)
#define UHCI_STS_USBERR   (1 << 1)
#define UHCI_STS_USBINT   (1 << 0)

#define TD_CTRL_SPD     (1 << 29)
#define TD_CTRL_ERROR_SHIFT  27
#define TD_CTRL_IOS     (1 << 25)
#define TD_CTRL_IOC     (1 << 24)
#define TD_CTRL_ACTIVE  (1 << 23)
#define TD_CTRL_STALL   (1 << 22)
#define TD_CTRL_BABBLE  (1 << 20)
#define TD_CTRL_NAK     (1 << 19)
#define TD_CTRL_TIMEOUT (1 << 18)

#define UHCI_PORT_RESET (1 << 9)
#define UHCI_PORT_LSDA  (1 << 8)
#define UHCI_PORT_ENC   (1 << 3)
#define UHCI_PORT_EN    (1 << 2)
#define UHCI_PORT_CSC   (1 << 1)
#define UHCI_PORT_CCS   (1 << 0)

#define FRAME_TIMER_FREQ 1000

#define FRAME_MAX_LOOPS  100

#define NB_PORTS 2

typedef struct UHCIPort {
    USBPort port;
    uint16_t ctrl;
} UHCIPort;

typedef struct UHCIState {
    PCIDevice dev;
    uint16_t cmd; /* cmd register */
    uint16_t status;
    uint16_t intr; /* interrupt enable register */
    uint16_t frnum; /* frame number */
    uint32_t fl_base_addr; /* frame list base address */
    uint8_t sof_timing;
    uint8_t status2; /* bit 0 and 1 are used to generate UHCI_STS_USBINT */
    QEMUTimer *frame_timer;
    UHCIPort ports[NB_PORTS];

    /* Interrupts that should be raised at the end of the current frame.  */
    uint32_t pending_int_mask;
    /* For simplicity of implementation we only allow a single pending USB
       request.  This means all usb traffic on this controller is effectively
       suspended until that transfer completes.  When the transfer completes
       the next transfer from that queue will be processed.  However
       other queues will not be processed until the next frame.  The solution
       is to allow multiple pending requests.  */
    uint32_t async_qh;
    uint32_t async_frame_addr;
    USBPacket usb_packet;
    uint8_t usb_buf[2048];
} UHCIState;

typedef struct UHCI_TD {
    uint32_t link;
    uint32_t ctrl; /* see TD_CTRL_xxx */
    uint32_t token;
    uint32_t buffer;
} UHCI_TD;

typedef struct UHCI_QH {
    uint32_t link;
    uint32_t el_link;
} UHCI_QH;

static void uhci_attach(USBPort *port1, USBDevice *dev);

static void uhci_update_irq(UHCIState *s)
{
    int level;
    if (((s->status2 & 1) && (s->intr & (1 << 2))) ||
        ((s->status2 & 2) && (s->intr & (1 << 3))) ||
        ((s->status & UHCI_STS_USBERR) && (s->intr & (1 << 0))) ||
        ((s->status & UHCI_STS_RD) && (s->intr & (1 << 1))) ||
        (s->status & UHCI_STS_HSERR) ||
        (s->status & UHCI_STS_HCPERR)) {
        level = 1;
    } else {
        level = 0;
    }
    qemu_set_irq(s->dev.irq[3], level);
}

static void uhci_reset(UHCIState *s)
{
    uint8_t *pci_conf;
    int i;
    UHCIPort *port;

    pci_conf = s->dev.config;

    pci_conf[0x6a] = 0x01; /* usb clock */
    pci_conf[0x6b] = 0x00;
    s->cmd = 0;
    s->status = 0;
    s->status2 = 0;
    s->intr = 0;
    s->fl_base_addr = 0;
    s->sof_timing = 64;
    for(i = 0; i < NB_PORTS; i++) {
        port = &s->ports[i];
        port->ctrl = 0x0080;
        if (port->port.dev)
            uhci_attach(&port->port, port->port.dev);
    }
}

#if 0
static void uhci_save(QEMUFile *f, void *opaque)
{
    UHCIState *s = opaque;
    uint8_t num_ports = NB_PORTS;
    int i;

    pci_device_save(&s->dev, f);

    qemu_put_8s(f, &num_ports);
    for (i = 0; i < num_ports; ++i)
        qemu_put_be16s(f, &s->ports[i].ctrl);
    qemu_put_be16s(f, &s->cmd);
    qemu_put_be16s(f, &s->status);
    qemu_put_be16s(f, &s->intr);
    qemu_put_be16s(f, &s->frnum);
    qemu_put_be32s(f, &s->fl_base_addr);
    qemu_put_8s(f, &s->sof_timing);
    qemu_put_8s(f, &s->status2);
    qemu_put_timer(f, s->frame_timer);
}

static int uhci_load(QEMUFile *f, void *opaque, int version_id)
{
    UHCIState *s = opaque;
    uint8_t num_ports;
    int i, ret;

    if (version_id > 1)
        return -EINVAL;

    ret = pci_device_load(&s->dev, f);
    if (ret < 0)
        return ret;

    qemu_get_8s(f, &num_ports);
    if (num_ports != NB_PORTS)
        return -EINVAL;

    for (i = 0; i < num_ports; ++i)
        qemu_get_be16s(f, &s->ports[i].ctrl);
    qemu_get_be16s(f, &s->cmd);
    qemu_get_be16s(f, &s->status);
    qemu_get_be16s(f, &s->intr);
    qemu_get_be16s(f, &s->frnum);
    qemu_get_be32s(f, &s->fl_base_addr);
    qemu_get_8s(f, &s->sof_timing);
    qemu_get_8s(f, &s->status2);
    qemu_get_timer(f, s->frame_timer);

    return 0;
}
#endif

static void uhci_ioport_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    UHCIState *s = opaque;

    addr &= 0x1f;
    switch(addr) {
    case 0x0c:
        s->sof_timing = val;
        break;
    }
}

static uint32_t uhci_ioport_readb(void *opaque, uint32_t addr)
{
    UHCIState *s = opaque;
    uint32_t val;

    addr &= 0x1f;
    switch(addr) {
    case 0x0c:
        val = s->sof_timing;
        break;
    default:
        val = 0xff;
        break;
    }
    return val;
}

static void uhci_ioport_writew(void *opaque, uint32_t addr, uint32_t val)
{
    UHCIState *s = opaque;

    addr &= 0x1f;
#ifdef DEBUG
    printf("uhci writew port=0x%04x val=0x%04x\n", addr, val);
#endif
    switch(addr) {
    case 0x00:
        if ((val & UHCI_CMD_RS) && !(s->cmd & UHCI_CMD_RS)) {
            /* start frame processing */
            qemu_mod_timer(s->frame_timer, qemu_get_clock(vm_clock));
            s->status &= ~UHCI_STS_HCHALTED;
        } else if (!(val & UHCI_CMD_RS)) {
            s->status |= UHCI_STS_HCHALTED;
        }
        if (val & UHCI_CMD_GRESET) {
            UHCIPort *port;
            USBDevice *dev;
            int i;

            /* send reset on the USB bus */
            for(i = 0; i < NB_PORTS; i++) {
                port = &s->ports[i];
                dev = port->port.dev;
                if (dev) {
                    usb_send_msg(dev, USB_MSG_RESET);
                }
            }
            uhci_reset(s);
            return;
        }
        if (val & UHCI_CMD_HCRESET) {
            uhci_reset(s);
            return;
        }
        s->cmd = val;
        break;
    case 0x02:
        s->status &= ~val;
        /* XXX: the chip spec is not coherent, so we add a hidden
           register to distinguish between IOC and SPD */
        if (val & UHCI_STS_USBINT)
            s->status2 = 0;
        uhci_update_irq(s);
        break;
    case 0x04:
        s->intr = val;
        uhci_update_irq(s);
        break;
    case 0x06:
        if (s->status & UHCI_STS_HCHALTED)
            s->frnum = val & 0x7ff;
        break;
    case 0x10 ... 0x1f:
        {
            UHCIPort *port;
            USBDevice *dev;
            int n;

            n = (addr >> 1) & 7;
            if (n >= NB_PORTS)
                return;
            port = &s->ports[n];
            dev = port->port.dev;
            if (dev) {
                /* port reset */
                if ( (val & UHCI_PORT_RESET) &&
                     !(port->ctrl & UHCI_PORT_RESET) ) {
                    usb_send_msg(dev, USB_MSG_RESET);
                }
            }
            port->ctrl = (port->ctrl & 0x01fb) | (val & ~0x01fb);
            /* some bits are reset when a '1' is written to them */
            port->ctrl &= ~(val & 0x000a);
        }
        break;
    }
}

static uint32_t uhci_ioport_readw(void *opaque, uint32_t addr)
{
    UHCIState *s = opaque;
    uint32_t val;

    addr &= 0x1f;
    switch(addr) {
    case 0x00:
        val = s->cmd;
        break;
    case 0x02:
        val = s->status;
        break;
    case 0x04:
        val = s->intr;
        break;
    case 0x06:
        val = s->frnum;
        break;
    case 0x10 ... 0x1f:
        {
            UHCIPort *port;
            int n;
            n = (addr >> 1) & 7;
            if (n >= NB_PORTS)
                goto read_default;
            port = &s->ports[n];
            val = port->ctrl;
        }
        break;
    default:
    read_default:
        val = 0xff7f; /* disabled port */
        break;
    }
#ifdef DEBUG
    printf("uhci readw port=0x%04x val=0x%04x\n", addr, val);
#endif
    return val;
}

static void uhci_ioport_writel(void *opaque, uint32_t addr, uint32_t val)
{
    UHCIState *s = opaque;

    addr &= 0x1f;
#ifdef DEBUG
    printf("uhci writel port=0x%04x val=0x%08x\n", addr, val);
#endif
    switch(addr) {
    case 0x08:
        s->fl_base_addr = val & ~0xfff;
        break;
    }
}

static uint32_t uhci_ioport_readl(void *opaque, uint32_t addr)
{
    UHCIState *s = opaque;
    uint32_t val;

    addr &= 0x1f;
    switch(addr) {
    case 0x08:
        val = s->fl_base_addr;
        break;
    default:
        val = 0xffffffff;
        break;
    }
    return val;
}

/* signal resume if controller suspended */
static void uhci_resume (void *opaque)
{
    UHCIState *s = (UHCIState *)opaque;

    if (!s)
        return;

    if (s->cmd & UHCI_CMD_EGSM) {
        s->cmd |= UHCI_CMD_FGR;
        s->status |= UHCI_STS_RD;
        uhci_update_irq(s);
    }
}

static void uhci_attach(USBPort *port1, USBDevice *dev)
{
    UHCIState *s = port1->opaque;
    UHCIPort *port = &s->ports[port1->index];

    if (dev) {
        if (port->port.dev) {
            usb_attach(port1, NULL);
        }
        /* set connect status */
        port->ctrl |= UHCI_PORT_CCS | UHCI_PORT_CSC;

        /* update speed */
        if (dev->speed == USB_SPEED_LOW)
            port->ctrl |= UHCI_PORT_LSDA;
        else
            port->ctrl &= ~UHCI_PORT_LSDA;

        uhci_resume(s);

        port->port.dev = dev;
        /* send the attach message */
        usb_send_msg(dev, USB_MSG_ATTACH);
    } else {
        /* set connect status */
        if (port->ctrl & UHCI_PORT_CCS) {
            port->ctrl &= ~UHCI_PORT_CCS;
            port->ctrl |= UHCI_PORT_CSC;
        }
        /* disable port */
        if (port->ctrl & UHCI_PORT_EN) {
            port->ctrl &= ~UHCI_PORT_EN;
            port->ctrl |= UHCI_PORT_ENC;
        }

        uhci_resume(s);

        dev = port->port.dev;
        if (dev) {
            /* send the detach message */
            usb_send_msg(dev, USB_MSG_DETACH);
        }
        port->port.dev = NULL;
    }
}

static int uhci_broadcast_packet(UHCIState *s, USBPacket *p)
{
    UHCIPort *port;
    USBDevice *dev;
    int i, ret;

#ifdef DEBUG_PACKET
    {
        const char *pidstr;
        switch(p->pid) {
        case USB_TOKEN_SETUP: pidstr = "SETUP"; break;
        case USB_TOKEN_IN: pidstr = "IN"; break;
        case USB_TOKEN_OUT: pidstr = "OUT"; break;
        default: pidstr = "?"; break;
        }
        printf("frame %d: pid=%s addr=0x%02x ep=%d len=%d\n",
               s->frnum, pidstr, p->devaddr, p->devep, p->len);
        if (p->pid != USB_TOKEN_IN) {
            printf("     data_out=");
            for(i = 0; i < p->len; i++) {
                printf(" %02x", p->data[i]);
            }
            printf("\n");
        }
    }
#endif
    for(i = 0; i < NB_PORTS; i++) {
        port = &s->ports[i];
        dev = port->port.dev;
        if (dev && (port->ctrl & UHCI_PORT_EN)) {
            ret = dev->handle_packet(dev, p);
            if (ret != USB_RET_NODEV) {
#ifdef DEBUG_PACKET
                if (ret == USB_RET_ASYNC) {
                    printf("usb-uhci: Async packet\n");
                } else {
                    printf("     ret=%d ", ret);
                    if (p->pid == USB_TOKEN_IN && ret > 0) {
                        printf("data_in=");
                        for(i = 0; i < ret; i++) {
                            printf(" %02x", p->data[i]);
                        }
                    }
                    printf("\n");
                }
#endif
                return ret;
            }
        }
    }
    return USB_RET_NODEV;
}

static void uhci_async_complete_packet(USBPacket * packet, void *opaque);

/* return -1 if fatal error (frame must be stopped)
          0 if TD successful
          1 if TD unsuccessful or inactive
*/
static int uhci_handle_td(UHCIState *s, UHCI_TD *td, uint32_t *int_mask,
                          int completion)
{
    uint8_t pid;
    int len = 0, max_len, err, ret = 0;

    /* ??? This is wrong for async completion.  */
    if (td->ctrl & TD_CTRL_IOC) {
        *int_mask |= 0x01;
    }

    if (!(td->ctrl & TD_CTRL_ACTIVE))
        return 1;

    /* TD is active */
    max_len = ((td->token >> 21) + 1) & 0x7ff;
    pid = td->token & 0xff;

    if (completion && (s->async_qh || s->async_frame_addr)) {
        ret = s->usb_packet.len;
        if (ret >= 0) {
            len = ret;
            if (len > max_len) {
                len = max_len;
                ret = USB_RET_BABBLE;
            }
            if (len > 0) {
                /* write the data back */
                cpu_physical_memory_write(td->buffer, s->usb_buf, len);
            }
        } else {
            len = 0;
        }
        s->async_qh = 0;
        s->async_frame_addr = 0;
    } else if (!completion) {
        s->usb_packet.pid = pid;
        s->usb_packet.devaddr = (td->token >> 8) & 0x7f;
        s->usb_packet.devep = (td->token >> 15) & 0xf;
        s->usb_packet.data = s->usb_buf;
        s->usb_packet.len = max_len;
        s->usb_packet.complete_cb = uhci_async_complete_packet;
        s->usb_packet.complete_opaque = s;
        switch(pid) {
        case USB_TOKEN_OUT:
        case USB_TOKEN_SETUP:
            cpu_physical_memory_read(td->buffer, s->usb_buf, max_len);
            ret = uhci_broadcast_packet(s, &s->usb_packet);
            len = max_len;
            break;
        case USB_TOKEN_IN:
            ret = uhci_broadcast_packet(s, &s->usb_packet);
            if (ret >= 0) {
                len = ret;
                if (len > max_len) {
                    len = max_len;
                    ret = USB_RET_BABBLE;
                }
                if (len > 0) {
                    /* write the data back */
                    cpu_physical_memory_write(td->buffer, s->usb_buf, len);
                }
            } else {
                len = 0;
            }
            break;
        default:
            /* invalid pid : frame interrupted */
            s->status |= UHCI_STS_HCPERR;
            uhci_update_irq(s);
            return -1;
        }
    }

    if (ret == USB_RET_ASYNC) {
        return 2;
    }
    if (td->ctrl & TD_CTRL_IOS)
        td->ctrl &= ~TD_CTRL_ACTIVE;
    if (ret >= 0) {
        td->ctrl = (td->ctrl & ~0x7ff) | ((len - 1) & 0x7ff);
        /* The NAK bit may have been set by a previous frame, so clear it
           here.  The docs are somewhat unclear, but win2k relies on this
           behavior.  */
        td->ctrl &= ~(TD_CTRL_ACTIVE | TD_CTRL_NAK);
        if (pid == USB_TOKEN_IN &&
            (td->ctrl & TD_CTRL_SPD) &&
            len < max_len) {
            *int_mask |= 0x02;
            /* short packet: do not update QH */
            return 1;
        } else {
            /* success */
            return 0;
        }
    } else {
        switch(ret) {
        default:
        case USB_RET_NODEV:
        do_timeout:
            td->ctrl |= TD_CTRL_TIMEOUT;
            err = (td->ctrl >> TD_CTRL_ERROR_SHIFT) & 3;
            if (err != 0) {
                err--;
                if (err == 0) {
                    td->ctrl &= ~TD_CTRL_ACTIVE;
                    s->status |= UHCI_STS_USBERR;
                    uhci_update_irq(s);
                }
            }
            td->ctrl = (td->ctrl & ~(3 << TD_CTRL_ERROR_SHIFT)) |
                (err << TD_CTRL_ERROR_SHIFT);
            return 1;
        case USB_RET_NAK:
            td->ctrl |= TD_CTRL_NAK;
            if (pid == USB_TOKEN_SETUP)
                goto do_timeout;
            return 1;
        case USB_RET_STALL:
            td->ctrl |= TD_CTRL_STALL;
            td->ctrl &= ~TD_CTRL_ACTIVE;
            return 1;
        case USB_RET_BABBLE:
            td->ctrl |= TD_CTRL_BABBLE | TD_CTRL_STALL;
            td->ctrl &= ~TD_CTRL_ACTIVE;
            /* frame interrupted */
            return -1;
        }
    }
}

static void uhci_async_complete_packet(USBPacket * packet, void *opaque)
{
    UHCIState *s = opaque;
    UHCI_QH qh;
    UHCI_TD td;
    uint32_t link;
    uint32_t old_td_ctrl;
    uint32_t val;
    uint32_t frame_addr;
    int ret;

    /* Handle async isochronous packet completion */
    frame_addr = s->async_frame_addr;
    if (frame_addr) {
        cpu_physical_memory_read(frame_addr, (uint8_t *)&link, 4);
        le32_to_cpus(&link);

        cpu_physical_memory_read(link & ~0xf, (uint8_t *)&td, sizeof(td));
        le32_to_cpus(&td.link);
        le32_to_cpus(&td.ctrl);
        le32_to_cpus(&td.token);
        le32_to_cpus(&td.buffer);
        old_td_ctrl = td.ctrl;
        ret = uhci_handle_td(s, &td, &s->pending_int_mask, 1);

        /* update the status bits of the TD */
        if (old_td_ctrl != td.ctrl) {
            val = cpu_to_le32(td.ctrl);
            cpu_physical_memory_write((link & ~0xf) + 4,
                                      (const uint8_t *)&val,
                                      sizeof(val));
        }
        if (ret == 2) {
            s->async_frame_addr = frame_addr;
        } else if (ret == 0) {
            /* update qh element link */
            val = cpu_to_le32(td.link);
            cpu_physical_memory_write(frame_addr,
                                      (const uint8_t *)&val,
                                      sizeof(val));
        }
        return;
    }

    link = s->async_qh;
    if (!link) {
        /* This should never happen. It means a TD somehow got removed
           without cancelling the associated async IO request.  */
        return;
    }
    cpu_physical_memory_read(link & ~0xf, (uint8_t *)&qh, sizeof(qh));
    le32_to_cpus(&qh.link);
    le32_to_cpus(&qh.el_link);
    /* Re-process the queue containing the async packet.  */
    while (1) {
        cpu_physical_memory_read(qh.el_link & ~0xf,
                                 (uint8_t *)&td, sizeof(td));
        le32_to_cpus(&td.link);
        le32_to_cpus(&td.ctrl);
        le32_to_cpus(&td.token);
        le32_to_cpus(&td.buffer);
        old_td_ctrl = td.ctrl;
        ret = uhci_handle_td(s, &td, &s->pending_int_mask, 1);

        /* update the status bits of the TD */
        if (old_td_ctrl != td.ctrl) {
            val = cpu_to_le32(td.ctrl);
            cpu_physical_memory_write((qh.el_link & ~0xf) + 4,
                                      (const uint8_t *)&val,
                                      sizeof(val));
        }
        if (ret < 0)
            break; /* interrupted frame */
        if (ret == 2) {
            s->async_qh = link;
            break;
        } else if (ret == 0) {
            /* update qh element link */
            qh.el_link = td.link;
            val = cpu_to_le32(qh.el_link);
            cpu_physical_memory_write((link & ~0xf) + 4,
                                      (const uint8_t *)&val,
                                      sizeof(val));
            if (!(qh.el_link & 4))
                break;
        }
        break;
    }
}

static void uhci_frame_timer(void *opaque)
{
    UHCIState *s = opaque;
    int64_t expire_time;
    uint32_t frame_addr, link, old_td_ctrl, val, int_mask;
    int cnt, ret;
    UHCI_TD td;
    UHCI_QH qh;
    uint32_t old_async_qh;

    if (!(s->cmd & UHCI_CMD_RS)) {
        qemu_del_timer(s->frame_timer);
        /* set hchalted bit in status - UHCI11D 2.1.2 */
        s->status |= UHCI_STS_HCHALTED;
        return;
    }
    /* Complete the previous frame.  */
    s->frnum = (s->frnum + 1) & 0x7ff;
    if (s->pending_int_mask) {
        s->status2 |= s->pending_int_mask;
        s->status |= UHCI_STS_USBINT;
        uhci_update_irq(s);
    }
    old_async_qh = s->async_qh;
    frame_addr = s->fl_base_addr + ((s->frnum & 0x3ff) << 2);
    cpu_physical_memory_read(frame_addr, (uint8_t *)&link, 4);
    le32_to_cpus(&link);
    int_mask = 0;
    cnt = FRAME_MAX_LOOPS;
    while ((link & 1) == 0) {
        if (--cnt == 0)
            break;
        /* valid frame */
        if (link & 2) {
            /* QH */
            if (link == s->async_qh) {
                /* We've found a previously issues packet.
                   Nothing else to do.  */
                old_async_qh = 0;
                break;
            }
            cpu_physical_memory_read(link & ~0xf, (uint8_t *)&qh, sizeof(qh));
            le32_to_cpus(&qh.link);
            le32_to_cpus(&qh.el_link);
        depth_first:
            if (qh.el_link & 1) {
                /* no element : go to next entry */
                link = qh.link;
            } else if (qh.el_link & 2) {
                /* QH */
                link = qh.el_link;
            } else if (s->async_qh) {
                /* We can only cope with one pending packet.  Keep looking
                   for the previously issued packet.  */
                link = qh.link;
            } else {
                /* TD */
                if (--cnt == 0)
                    break;
                cpu_physical_memory_read(qh.el_link & ~0xf,
                                         (uint8_t *)&td, sizeof(td));
                le32_to_cpus(&td.link);
                le32_to_cpus(&td.ctrl);
                le32_to_cpus(&td.token);
                le32_to_cpus(&td.buffer);
                old_td_ctrl = td.ctrl;
                ret = uhci_handle_td(s, &td, &int_mask, 0);

                /* update the status bits of the TD */
                if (old_td_ctrl != td.ctrl) {
                    val = cpu_to_le32(td.ctrl);
                    cpu_physical_memory_write((qh.el_link & ~0xf) + 4,
                                              (const uint8_t *)&val,
                                              sizeof(val));
                }
                if (ret < 0)
                    break; /* interrupted frame */
                if (ret == 2) {
                    s->async_qh = link;
                } else if (ret == 0) {
                    /* update qh element link */
                    qh.el_link = td.link;
                    val = cpu_to_le32(qh.el_link);
                    cpu_physical_memory_write((link & ~0xf) + 4,
                                              (const uint8_t *)&val,
                                              sizeof(val));
                    if (qh.el_link & 4) {
                        /* depth first */
                        goto depth_first;
                    }
                }
                /* go to next entry */
                link = qh.link;
            }
        } else {
            /* TD */
            cpu_physical_memory_read(link & ~0xf, (uint8_t *)&td, sizeof(td));
            le32_to_cpus(&td.link);
            le32_to_cpus(&td.ctrl);
            le32_to_cpus(&td.token);
            le32_to_cpus(&td.buffer);

            /* Handle isochonous transfer.  */
            /* FIXME: might be more than one isoc in frame */
            old_td_ctrl = td.ctrl;
            ret = uhci_handle_td(s, &td, &int_mask, 0);

            /* update the status bits of the TD */
            if (old_td_ctrl != td.ctrl) {
                val = cpu_to_le32(td.ctrl);
                cpu_physical_memory_write((link & ~0xf) + 4,
                                          (const uint8_t *)&val,
                                          sizeof(val));
            }
            if (ret < 0)
                break; /* interrupted frame */
            if (ret == 2) {
                s->async_frame_addr = frame_addr;
            }
            link = td.link;
        }
    }
    s->pending_int_mask = int_mask;
    if (old_async_qh) {
        /* A previously started transfer has disappeared from the transfer
           list.  There's nothing useful we can do with it now, so just
           discard the packet and hope it wasn't too important.  */
#ifdef DEBUG
        printf("Discarding USB packet\n");
#endif
        usb_cancel_packet(&s->usb_packet);
        s->async_qh = 0;
    }

    /* prepare the timer for the next frame */
    expire_time = qemu_get_clock(vm_clock) +
        (ticks_per_sec / FRAME_TIMER_FREQ);
    qemu_mod_timer(s->frame_timer, expire_time);
}

static void uhci_map(PCIDevice *pci_dev, int region_num,
                    uint32_t addr, uint32_t size, int type)
{
    UHCIState *s = (UHCIState *)pci_dev;

    register_ioport_write(addr, 32, 2, uhci_ioport_writew, s);
    register_ioport_read(addr, 32, 2, uhci_ioport_readw, s);
    register_ioport_write(addr, 32, 4, uhci_ioport_writel, s);
    register_ioport_read(addr, 32, 4, uhci_ioport_readl, s);
    register_ioport_write(addr, 32, 1, uhci_ioport_writeb, s);
    register_ioport_read(addr, 32, 1, uhci_ioport_readb, s);
}

void usb_uhci_piix3_init(PCIBus *bus, int devfn)
{
    UHCIState *s;
    uint8_t *pci_conf;
    int i;

    s = (UHCIState *)pci_register_device(bus,
                                        "USB-UHCI", sizeof(UHCIState),
                                        devfn, NULL, NULL);
    pci_conf = s->dev.config;
    pci_conf[0x00] = 0x86;
    pci_conf[0x01] = 0x80;
    pci_conf[0x02] = 0x20;
    pci_conf[0x03] = 0x70;
    pci_conf[0x08] = 0x01; // revision number
    pci_conf[0x09] = 0x00;
    pci_conf[0x0a] = 0x03;
    pci_conf[0x0b] = 0x0c;
    pci_conf[0x0e] = 0x00; // header_type
    pci_conf[0x3d] = 4; // interrupt pin 3
    pci_conf[0x60] = 0x10; // release number

    for(i = 0; i < NB_PORTS; i++) {
        qemu_register_usb_port(&s->ports[i].port, s, i, uhci_attach);
    }
    s->frame_timer = qemu_new_timer(vm_clock, uhci_frame_timer, s);

    uhci_reset(s);

    /* Use region 4 for consistency with real hardware.  BSD guests seem
       to rely on this.  */
    pci_register_io_region(&s->dev, 4, 0x20,
                           PCI_ADDRESS_SPACE_IO, uhci_map);
}

void usb_uhci_piix4_init(PCIBus *bus, int devfn)
{
    UHCIState *s;
    uint8_t *pci_conf;
    int i;

    s = (UHCIState *)pci_register_device(bus,
                                        "USB-UHCI", sizeof(UHCIState),
                                        devfn, NULL, NULL);
    pci_conf = s->dev.config;
    pci_conf[0x00] = 0x86;
    pci_conf[0x01] = 0x80;
    pci_conf[0x02] = 0x12;
    pci_conf[0x03] = 0x71;
    pci_conf[0x08] = 0x01; // revision number
    pci_conf[0x09] = 0x00;
    pci_conf[0x0a] = 0x03;
    pci_conf[0x0b] = 0x0c;
    pci_conf[0x0e] = 0x00; // header_type
    pci_conf[0x3d] = 4; // interrupt pin 3
    pci_conf[0x60] = 0x10; // release number

    for(i = 0; i < NB_PORTS; i++) {
        qemu_register_usb_port(&s->ports[i].port, s, i, uhci_attach);
    }
    s->frame_timer = qemu_new_timer(vm_clock, uhci_frame_timer, s);

    uhci_reset(s);

    /* Use region 4 for consistency with real hardware.  BSD guests seem
       to rely on this.  */
    pci_register_io_region(&s->dev, 4, 0x20,
                           PCI_ADDRESS_SPACE_IO, uhci_map);
}
