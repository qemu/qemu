/*
 * "Inventra" High-speed Dual-Role Controller (MUSB-HDRC), Mentor Graphics,
 * USB2.0 OTG compliant core used in various chips.
 *
 * Copyright (C) 2008 Nokia Corporation
 * Written by Andrzej Zaborowski <andrew@openedhand.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Only host-mode and non-DMA accesses are currently supported.
 */
#include "qemu-common.h"
#include "qemu-timer.h"
#include "usb.h"
#include "irq.h"
#include "hw.h"

/* Common USB registers */
#define MUSB_HDRC_FADDR		0x00	/* 8-bit */
#define MUSB_HDRC_POWER		0x01	/* 8-bit */

#define MUSB_HDRC_INTRTX	0x02	/* 16-bit */
#define MUSB_HDRC_INTRRX	0x04
#define MUSB_HDRC_INTRTXE	0x06  
#define MUSB_HDRC_INTRRXE	0x08  
#define MUSB_HDRC_INTRUSB	0x0a	/* 8 bit */
#define MUSB_HDRC_INTRUSBE	0x0b	/* 8 bit */
#define MUSB_HDRC_FRAME		0x0c	/* 16-bit */
#define MUSB_HDRC_INDEX		0x0e	/* 8 bit */
#define MUSB_HDRC_TESTMODE	0x0f	/* 8 bit */

/* Per-EP registers in indexed mode */
#define MUSB_HDRC_EP_IDX	0x10	/* 8-bit */

/* EP FIFOs */
#define MUSB_HDRC_FIFO		0x20

/* Additional Control Registers */
#define	MUSB_HDRC_DEVCTL	0x60	/* 8 bit */

/* These are indexed */
#define MUSB_HDRC_TXFIFOSZ	0x62	/* 8 bit (see masks) */
#define MUSB_HDRC_RXFIFOSZ	0x63	/* 8 bit (see masks) */
#define MUSB_HDRC_TXFIFOADDR	0x64	/* 16 bit offset shifted right 3 */
#define MUSB_HDRC_RXFIFOADDR	0x66	/* 16 bit offset shifted right 3 */

/* Some more registers */
#define MUSB_HDRC_VCTRL		0x68	/* 8 bit */
#define MUSB_HDRC_HWVERS	0x6c	/* 8 bit */

/* Added in HDRC 1.9(?) & MHDRC 1.4 */
/* ULPI pass-through */
#define MUSB_HDRC_ULPI_VBUSCTL	0x70
#define MUSB_HDRC_ULPI_REGDATA	0x74
#define MUSB_HDRC_ULPI_REGADDR	0x75
#define MUSB_HDRC_ULPI_REGCTL	0x76

/* Extended config & PHY control */
#define MUSB_HDRC_ENDCOUNT	0x78	/* 8 bit */
#define MUSB_HDRC_DMARAMCFG	0x79	/* 8 bit */
#define MUSB_HDRC_PHYWAIT	0x7a	/* 8 bit */
#define MUSB_HDRC_PHYVPLEN	0x7b	/* 8 bit */
#define MUSB_HDRC_HS_EOF1	0x7c	/* 8 bit, units of 546.1 us */
#define MUSB_HDRC_FS_EOF1	0x7d	/* 8 bit, units of 533.3 ns */
#define MUSB_HDRC_LS_EOF1	0x7e	/* 8 bit, units of 1.067 us */

/* Per-EP BUSCTL registers */
#define MUSB_HDRC_BUSCTL	0x80

/* Per-EP registers in flat mode */
#define MUSB_HDRC_EP		0x100

/* offsets to registers in flat model */
#define MUSB_HDRC_TXMAXP	0x00	/* 16 bit apparently */
#define MUSB_HDRC_TXCSR		0x02	/* 16 bit apparently */
#define MUSB_HDRC_CSR0		MUSB_HDRC_TXCSR		/* re-used for EP0 */
#define MUSB_HDRC_RXMAXP	0x04	/* 16 bit apparently */
#define MUSB_HDRC_RXCSR		0x06	/* 16 bit apparently */
#define MUSB_HDRC_RXCOUNT	0x08	/* 16 bit apparently */
#define MUSB_HDRC_COUNT0	MUSB_HDRC_RXCOUNT	/* re-used for EP0 */
#define MUSB_HDRC_TXTYPE	0x0a	/* 8 bit apparently */
#define MUSB_HDRC_TYPE0		MUSB_HDRC_TXTYPE	/* re-used for EP0 */
#define MUSB_HDRC_TXINTERVAL	0x0b	/* 8 bit apparently */
#define MUSB_HDRC_NAKLIMIT0	MUSB_HDRC_TXINTERVAL	/* re-used for EP0 */
#define MUSB_HDRC_RXTYPE	0x0c	/* 8 bit apparently */
#define MUSB_HDRC_RXINTERVAL	0x0d	/* 8 bit apparently */
#define MUSB_HDRC_FIFOSIZE	0x0f	/* 8 bit apparently */
#define MUSB_HDRC_CONFIGDATA	MGC_O_HDRC_FIFOSIZE	/* re-used for EP0 */

/* "Bus control" registers */
#define MUSB_HDRC_TXFUNCADDR	0x00
#define MUSB_HDRC_TXHUBADDR	0x02
#define MUSB_HDRC_TXHUBPORT	0x03

#define MUSB_HDRC_RXFUNCADDR	0x04
#define MUSB_HDRC_RXHUBADDR	0x06
#define MUSB_HDRC_RXHUBPORT	0x07

/*
 * MUSBHDRC Register bit masks
 */

/* POWER */
#define MGC_M_POWER_ISOUPDATE		0x80 
#define	MGC_M_POWER_SOFTCONN		0x40
#define	MGC_M_POWER_HSENAB		0x20
#define	MGC_M_POWER_HSMODE		0x10
#define MGC_M_POWER_RESET		0x08
#define MGC_M_POWER_RESUME		0x04
#define MGC_M_POWER_SUSPENDM		0x02
#define MGC_M_POWER_ENSUSPEND		0x01

/* INTRUSB */
#define MGC_M_INTR_SUSPEND		0x01
#define MGC_M_INTR_RESUME		0x02
#define MGC_M_INTR_RESET		0x04
#define MGC_M_INTR_BABBLE		0x04
#define MGC_M_INTR_SOF			0x08 
#define MGC_M_INTR_CONNECT		0x10
#define MGC_M_INTR_DISCONNECT		0x20
#define MGC_M_INTR_SESSREQ		0x40
#define MGC_M_INTR_VBUSERROR		0x80	/* FOR SESSION END */
#define MGC_M_INTR_EP0			0x01	/* FOR EP0 INTERRUPT */

/* DEVCTL */
#define MGC_M_DEVCTL_BDEVICE		0x80   
#define MGC_M_DEVCTL_FSDEV		0x40
#define MGC_M_DEVCTL_LSDEV		0x20
#define MGC_M_DEVCTL_VBUS		0x18
#define MGC_S_DEVCTL_VBUS		3
#define MGC_M_DEVCTL_HM			0x04
#define MGC_M_DEVCTL_HR			0x02
#define MGC_M_DEVCTL_SESSION		0x01

/* TESTMODE */
#define MGC_M_TEST_FORCE_HOST		0x80
#define MGC_M_TEST_FIFO_ACCESS		0x40
#define MGC_M_TEST_FORCE_FS		0x20
#define MGC_M_TEST_FORCE_HS		0x10
#define MGC_M_TEST_PACKET		0x08
#define MGC_M_TEST_K			0x04
#define MGC_M_TEST_J			0x02
#define MGC_M_TEST_SE0_NAK		0x01

/* CSR0 */
#define	MGC_M_CSR0_FLUSHFIFO		0x0100
#define MGC_M_CSR0_TXPKTRDY		0x0002
#define MGC_M_CSR0_RXPKTRDY		0x0001

/* CSR0 in Peripheral mode */
#define MGC_M_CSR0_P_SVDSETUPEND	0x0080
#define MGC_M_CSR0_P_SVDRXPKTRDY	0x0040
#define MGC_M_CSR0_P_SENDSTALL		0x0020
#define MGC_M_CSR0_P_SETUPEND		0x0010
#define MGC_M_CSR0_P_DATAEND		0x0008
#define MGC_M_CSR0_P_SENTSTALL		0x0004

/* CSR0 in Host mode */
#define MGC_M_CSR0_H_NO_PING		0x0800
#define MGC_M_CSR0_H_WR_DATATOGGLE	0x0400	/* set to allow setting: */
#define MGC_M_CSR0_H_DATATOGGLE		0x0200	/* data toggle control */
#define	MGC_M_CSR0_H_NAKTIMEOUT		0x0080
#define MGC_M_CSR0_H_STATUSPKT		0x0040
#define MGC_M_CSR0_H_REQPKT		0x0020
#define MGC_M_CSR0_H_ERROR		0x0010
#define MGC_M_CSR0_H_SETUPPKT		0x0008
#define MGC_M_CSR0_H_RXSTALL		0x0004

/* CONFIGDATA */
#define MGC_M_CONFIGDATA_MPRXE		0x80	/* auto bulk pkt combining */
#define MGC_M_CONFIGDATA_MPTXE		0x40	/* auto bulk pkt splitting */
#define MGC_M_CONFIGDATA_BIGENDIAN	0x20
#define MGC_M_CONFIGDATA_HBRXE		0x10	/* HB-ISO for RX */
#define MGC_M_CONFIGDATA_HBTXE		0x08	/* HB-ISO for TX */
#define MGC_M_CONFIGDATA_DYNFIFO	0x04	/* dynamic FIFO sizing */
#define MGC_M_CONFIGDATA_SOFTCONE	0x02	/* SoftConnect */
#define MGC_M_CONFIGDATA_UTMIDW		0x01	/* Width, 0 => 8b, 1 => 16b */

/* TXCSR in Peripheral and Host mode */
#define MGC_M_TXCSR_AUTOSET		0x8000
#define MGC_M_TXCSR_ISO			0x4000
#define MGC_M_TXCSR_MODE		0x2000
#define MGC_M_TXCSR_DMAENAB		0x1000
#define MGC_M_TXCSR_FRCDATATOG		0x0800
#define MGC_M_TXCSR_DMAMODE		0x0400
#define MGC_M_TXCSR_CLRDATATOG		0x0040
#define MGC_M_TXCSR_FLUSHFIFO		0x0008
#define MGC_M_TXCSR_FIFONOTEMPTY	0x0002
#define MGC_M_TXCSR_TXPKTRDY		0x0001

/* TXCSR in Peripheral mode */
#define MGC_M_TXCSR_P_INCOMPTX		0x0080
#define MGC_M_TXCSR_P_SENTSTALL		0x0020
#define MGC_M_TXCSR_P_SENDSTALL		0x0010
#define MGC_M_TXCSR_P_UNDERRUN		0x0004

/* TXCSR in Host mode */
#define MGC_M_TXCSR_H_WR_DATATOGGLE	0x0200
#define MGC_M_TXCSR_H_DATATOGGLE	0x0100
#define MGC_M_TXCSR_H_NAKTIMEOUT	0x0080
#define MGC_M_TXCSR_H_RXSTALL		0x0020
#define MGC_M_TXCSR_H_ERROR		0x0004

/* RXCSR in Peripheral and Host mode */
#define MGC_M_RXCSR_AUTOCLEAR		0x8000
#define MGC_M_RXCSR_DMAENAB		0x2000
#define MGC_M_RXCSR_DISNYET		0x1000
#define MGC_M_RXCSR_DMAMODE		0x0800
#define MGC_M_RXCSR_INCOMPRX		0x0100
#define MGC_M_RXCSR_CLRDATATOG		0x0080
#define MGC_M_RXCSR_FLUSHFIFO		0x0010
#define MGC_M_RXCSR_DATAERROR		0x0008
#define MGC_M_RXCSR_FIFOFULL		0x0002
#define MGC_M_RXCSR_RXPKTRDY		0x0001

/* RXCSR in Peripheral mode */
#define MGC_M_RXCSR_P_ISO		0x4000
#define MGC_M_RXCSR_P_SENTSTALL		0x0040
#define MGC_M_RXCSR_P_SENDSTALL		0x0020
#define MGC_M_RXCSR_P_OVERRUN		0x0004

/* RXCSR in Host mode */
#define MGC_M_RXCSR_H_AUTOREQ		0x4000
#define MGC_M_RXCSR_H_WR_DATATOGGLE	0x0400
#define MGC_M_RXCSR_H_DATATOGGLE	0x0200
#define MGC_M_RXCSR_H_RXSTALL		0x0040
#define MGC_M_RXCSR_H_REQPKT		0x0020
#define MGC_M_RXCSR_H_ERROR		0x0004

/* HUBADDR */
#define MGC_M_HUBADDR_MULTI_TT		0x80

/* ULPI: Added in HDRC 1.9(?) & MHDRC 1.4 */
#define MGC_M_ULPI_VBCTL_USEEXTVBUSIND	0x02
#define MGC_M_ULPI_VBCTL_USEEXTVBUS	0x01
#define MGC_M_ULPI_REGCTL_INT_ENABLE	0x08
#define MGC_M_ULPI_REGCTL_READNOTWRITE	0x04
#define MGC_M_ULPI_REGCTL_COMPLETE	0x02
#define MGC_M_ULPI_REGCTL_REG		0x01

/* #define MUSB_DEBUG */

#ifdef MUSB_DEBUG
#define TRACE(fmt,...) fprintf(stderr, "%s@%d: " fmt "\n", __FUNCTION__, \
                               __LINE__, ##__VA_ARGS__)
#else
#define TRACE(...)
#endif


static void musb_attach(USBPort *port);
static void musb_detach(USBPort *port);
static void musb_schedule_cb(USBDevice *dev, USBPacket *p);
static void musb_device_destroy(USBBus *bus, USBDevice *dev);

static USBPortOps musb_port_ops = {
    .attach = musb_attach,
    .detach = musb_detach,
    .complete = musb_schedule_cb,
};

static USBBusOps musb_bus_ops = {
    .device_destroy = musb_device_destroy,
};

typedef struct MUSBPacket MUSBPacket;
typedef struct MUSBEndPoint MUSBEndPoint;

struct MUSBPacket {
    USBPacket p;
    MUSBEndPoint *ep;
    int dir;
};

struct MUSBEndPoint {
    uint16_t faddr[2];
    uint8_t haddr[2];
    uint8_t hport[2];
    uint16_t csr[2];
    uint16_t maxp[2];
    uint16_t rxcount;
    uint8_t type[2];
    uint8_t interval[2];
    uint8_t config;
    uint8_t fifosize;
    int timeout[2];	/* Always in microframes */

    uint8_t *buf[2];
    int fifolen[2];
    int fifostart[2];
    int fifoaddr[2];
    MUSBPacket packey[2];
    int status[2];
    int ext_size[2];

    /* For callbacks' use */
    int epnum;
    int interrupt[2];
    MUSBState *musb;
    USBCallback *delayed_cb[2];
    QEMUTimer *intv_timer[2];
};

struct MUSBState {
    qemu_irq *irqs;
    USBBus bus;
    USBPort port;

    int idx;
    uint8_t devctl;
    uint8_t power;
    uint8_t faddr;

    uint8_t intr;
    uint8_t mask;
    uint16_t tx_intr;
    uint16_t tx_mask;
    uint16_t rx_intr;
    uint16_t rx_mask;

    int setup_len;
    int session;

    uint8_t buf[0x8000];

        /* Duplicating the world since 2008!...  probably we should have 32
         * logical, single endpoints instead.  */
    MUSBEndPoint ep[16];
};

struct MUSBState *musb_init(qemu_irq *irqs)
{
    MUSBState *s = qemu_mallocz(sizeof(*s));
    int i;

    s->irqs = irqs;

    s->faddr = 0x00;
    s->power = MGC_M_POWER_HSENAB;
    s->tx_intr = 0x0000;
    s->rx_intr = 0x0000;
    s->tx_mask = 0xffff;
    s->rx_mask = 0xffff;
    s->intr = 0x00;
    s->mask = 0x06;
    s->idx = 0;

    /* TODO: _DW */
    s->ep[0].config = MGC_M_CONFIGDATA_SOFTCONE | MGC_M_CONFIGDATA_DYNFIFO;
    for (i = 0; i < 16; i ++) {
        s->ep[i].fifosize = 64;
        s->ep[i].maxp[0] = 0x40;
        s->ep[i].maxp[1] = 0x40;
        s->ep[i].musb = s;
        s->ep[i].epnum = i;
    }

    usb_bus_new(&s->bus, &musb_bus_ops, NULL /* FIXME */);
    usb_register_port(&s->bus, &s->port, s, 0, &musb_port_ops,
                      USB_SPEED_MASK_LOW | USB_SPEED_MASK_FULL);
    usb_port_location(&s->port, NULL, 1);

    return s;
}

static void musb_vbus_set(MUSBState *s, int level)
{
    if (level)
        s->devctl |= 3 << MGC_S_DEVCTL_VBUS;
    else
        s->devctl &= ~MGC_M_DEVCTL_VBUS;

    qemu_set_irq(s->irqs[musb_set_vbus], level);
}

static void musb_intr_set(MUSBState *s, int line, int level)
{
    if (!level) {
        s->intr &= ~(1 << line);
        qemu_irq_lower(s->irqs[line]);
    } else if (s->mask & (1 << line)) {
        s->intr |= 1 << line;
        qemu_irq_raise(s->irqs[line]);
    }
}

static void musb_tx_intr_set(MUSBState *s, int line, int level)
{
    if (!level) {
        s->tx_intr &= ~(1 << line);
        if (!s->tx_intr)
            qemu_irq_lower(s->irqs[musb_irq_tx]);
    } else if (s->tx_mask & (1 << line)) {
        s->tx_intr |= 1 << line;
        qemu_irq_raise(s->irqs[musb_irq_tx]);
    }
}

static void musb_rx_intr_set(MUSBState *s, int line, int level)
{
    if (line) {
        if (!level) {
            s->rx_intr &= ~(1 << line);
            if (!s->rx_intr)
                qemu_irq_lower(s->irqs[musb_irq_rx]);
        } else if (s->rx_mask & (1 << line)) {
            s->rx_intr |= 1 << line;
            qemu_irq_raise(s->irqs[musb_irq_rx]);
        }
    } else
        musb_tx_intr_set(s, line, level);
}

uint32_t musb_core_intr_get(MUSBState *s)
{
    return (s->rx_intr << 15) | s->tx_intr;
}

void musb_core_intr_clear(MUSBState *s, uint32_t mask)
{
    if (s->rx_intr) {
        s->rx_intr &= mask >> 15;
        if (!s->rx_intr)
            qemu_irq_lower(s->irqs[musb_irq_rx]);
    }

    if (s->tx_intr) {
        s->tx_intr &= mask & 0xffff;
        if (!s->tx_intr)
            qemu_irq_lower(s->irqs[musb_irq_tx]);
    }
}

void musb_set_size(MUSBState *s, int epnum, int size, int is_tx)
{
    s->ep[epnum].ext_size[!is_tx] = size;
    s->ep[epnum].fifostart[0] = 0;
    s->ep[epnum].fifostart[1] = 0;
    s->ep[epnum].fifolen[0] = 0;
    s->ep[epnum].fifolen[1] = 0;
}

static void musb_session_update(MUSBState *s, int prev_dev, int prev_sess)
{
    int detect_prev = prev_dev && prev_sess;
    int detect = !!s->port.dev && s->session;

    if (detect && !detect_prev) {
        /* Let's skip the ID pin sense and VBUS sense formalities and
         * and signal a successful SRP directly.  This should work at least
         * for the Linux driver stack.  */
        musb_intr_set(s, musb_irq_connect, 1);

        if (s->port.dev->speed == USB_SPEED_LOW) {
            s->devctl &= ~MGC_M_DEVCTL_FSDEV;
            s->devctl |= MGC_M_DEVCTL_LSDEV;
        } else {
            s->devctl |= MGC_M_DEVCTL_FSDEV;
            s->devctl &= ~MGC_M_DEVCTL_LSDEV;
        }

        /* A-mode?  */
        s->devctl &= ~MGC_M_DEVCTL_BDEVICE;

        /* Host-mode bit?  */
        s->devctl |= MGC_M_DEVCTL_HM;
#if 1
        musb_vbus_set(s, 1);
#endif
    } else if (!detect && detect_prev) {
#if 1
        musb_vbus_set(s, 0);
#endif
    }
}

/* Attach or detach a device on our only port.  */
static void musb_attach(USBPort *port)
{
    MUSBState *s = (MUSBState *) port->opaque;

    musb_intr_set(s, musb_irq_vbus_request, 1);
    musb_session_update(s, 0, s->session);
}

static void musb_detach(USBPort *port)
{
    MUSBState *s = (MUSBState *) port->opaque;

    musb_intr_set(s, musb_irq_disconnect, 1);
    musb_session_update(s, 1, s->session);
}

static void musb_cb_tick0(void *opaque)
{
    MUSBEndPoint *ep = (MUSBEndPoint *) opaque;

    ep->delayed_cb[0](&ep->packey[0].p, opaque);
}

static void musb_cb_tick1(void *opaque)
{
    MUSBEndPoint *ep = (MUSBEndPoint *) opaque;

    ep->delayed_cb[1](&ep->packey[1].p, opaque);
}

#define musb_cb_tick	(dir ? musb_cb_tick1 : musb_cb_tick0)

static void musb_schedule_cb(USBDevice *dev, USBPacket *packey)
{
    MUSBPacket *p = container_of(packey, MUSBPacket, p);
    MUSBEndPoint *ep = p->ep;
    int dir = p->dir;
    int timeout = 0;

    if (ep->status[dir] == USB_RET_NAK)
        timeout = ep->timeout[dir];
    else if (ep->interrupt[dir])
        timeout = 8;
    else
        return musb_cb_tick(ep);

    if (!ep->intv_timer[dir])
        ep->intv_timer[dir] = qemu_new_timer_ns(vm_clock, musb_cb_tick, ep);

    qemu_mod_timer(ep->intv_timer[dir], qemu_get_clock_ns(vm_clock) +
                   muldiv64(timeout, get_ticks_per_sec(), 8000));
}

static int musb_timeout(int ttype, int speed, int val)
{
#if 1
    return val << 3;
#endif

    switch (ttype) {
    case USB_ENDPOINT_XFER_CONTROL:
        if (val < 2)
            return 0;
        else if (speed == USB_SPEED_HIGH)
            return 1 << (val - 1);
        else
            return 8 << (val - 1);

    case USB_ENDPOINT_XFER_INT:
        if (speed == USB_SPEED_HIGH)
            if (val < 2)
                return 0;
            else
                return 1 << (val - 1);
        else
            return val << 3;

    case USB_ENDPOINT_XFER_BULK:
    case USB_ENDPOINT_XFER_ISOC:
        if (val < 2)
            return 0;
        else if (speed == USB_SPEED_HIGH)
            return 1 << (val - 1);
        else
            return 8 << (val - 1);
        /* TODO: what with low-speed Bulk and Isochronous?  */
    }

    hw_error("bad interval\n");
}

static void musb_packet(MUSBState *s, MUSBEndPoint *ep,
                int epnum, int pid, int len, USBCallback cb, int dir)
{
    int ret;
    int idx = epnum && dir;
    int ttype;

    /* ep->type[0,1] contains:
     * in bits 7:6 the speed (0 - invalid, 1 - high, 2 - full, 3 - slow)
     * in bits 5:4 the transfer type (BULK / INT)
     * in bits 3:0 the EP num
     */
    ttype = epnum ? (ep->type[idx] >> 4) & 3 : 0;

    ep->timeout[dir] = musb_timeout(ttype,
                    ep->type[idx] >> 6, ep->interval[idx]);
    ep->interrupt[dir] = ttype == USB_ENDPOINT_XFER_INT;
    ep->delayed_cb[dir] = cb;

    ep->packey[dir].p.pid = pid;
    /* A wild guess on the FADDR semantics... */
    ep->packey[dir].p.devaddr = ep->faddr[idx];
    ep->packey[dir].p.devep = ep->type[idx] & 0xf;
    ep->packey[dir].p.data = (void *) ep->buf[idx];
    ep->packey[dir].p.len = len;
    ep->packey[dir].ep = ep;
    ep->packey[dir].dir = dir;

    if (s->port.dev)
        ret = usb_handle_packet(s->port.dev, &ep->packey[dir].p);
    else
        ret = USB_RET_NODEV;

    if (ret == USB_RET_ASYNC) {
        ep->status[dir] = len;
        return;
    }

    ep->status[dir] = ret;
    musb_schedule_cb(s->port.dev, &ep->packey[dir].p);
}

static void musb_tx_packet_complete(USBPacket *packey, void *opaque)
{
    /* Unfortunately we can't use packey->devep because that's the remote
     * endpoint number and may be different than our local.  */
    MUSBEndPoint *ep = (MUSBEndPoint *) opaque;
    int epnum = ep->epnum;
    MUSBState *s = ep->musb;

    ep->fifostart[0] = 0;
    ep->fifolen[0] = 0;
#ifdef CLEAR_NAK
    if (ep->status[0] != USB_RET_NAK) {
#endif
        if (epnum)
            ep->csr[0] &= ~(MGC_M_TXCSR_FIFONOTEMPTY | MGC_M_TXCSR_TXPKTRDY);
        else
            ep->csr[0] &= ~MGC_M_CSR0_TXPKTRDY;
#ifdef CLEAR_NAK
    }
#endif

    /* Clear all of the error bits first */
    if (epnum)
        ep->csr[0] &= ~(MGC_M_TXCSR_H_ERROR | MGC_M_TXCSR_H_RXSTALL |
                        MGC_M_TXCSR_H_NAKTIMEOUT);
    else
        ep->csr[0] &= ~(MGC_M_CSR0_H_ERROR | MGC_M_CSR0_H_RXSTALL |
                        MGC_M_CSR0_H_NAKTIMEOUT | MGC_M_CSR0_H_NO_PING);

    if (ep->status[0] == USB_RET_STALL) {
        /* Command not supported by target! */
        ep->status[0] = 0;

        if (epnum)
            ep->csr[0] |= MGC_M_TXCSR_H_RXSTALL;
        else
            ep->csr[0] |= MGC_M_CSR0_H_RXSTALL;
    }

    if (ep->status[0] == USB_RET_NAK) {
        ep->status[0] = 0;

        /* NAK timeouts are only generated in Bulk transfers and
         * Data-errors in Isochronous.  */
        if (ep->interrupt[0]) {
            return;
        }

        if (epnum)
            ep->csr[0] |= MGC_M_TXCSR_H_NAKTIMEOUT;
        else
            ep->csr[0] |= MGC_M_CSR0_H_NAKTIMEOUT;
    }

    if (ep->status[0] < 0) {
        if (ep->status[0] == USB_RET_BABBLE)
            musb_intr_set(s, musb_irq_rst_babble, 1);

        /* Pretend we've tried three times already and failed (in
         * case of USB_TOKEN_SETUP).  */
        if (epnum)
            ep->csr[0] |= MGC_M_TXCSR_H_ERROR;
        else
            ep->csr[0] |= MGC_M_CSR0_H_ERROR;

        musb_tx_intr_set(s, epnum, 1);
        return;
    }
    /* TODO: check len for over/underruns of an OUT packet?  */

#ifdef SETUPLEN_HACK
    if (!epnum && ep->packey[0].pid == USB_TOKEN_SETUP)
        s->setup_len = ep->packey[0].data[6];
#endif

    /* In DMA mode: if no error, assert DMA request for this EP,
     * and skip the interrupt.  */
    musb_tx_intr_set(s, epnum, 1);
}

static void musb_rx_packet_complete(USBPacket *packey, void *opaque)
{
    /* Unfortunately we can't use packey->devep because that's the remote
     * endpoint number and may be different than our local.  */
    MUSBEndPoint *ep = (MUSBEndPoint *) opaque;
    int epnum = ep->epnum;
    MUSBState *s = ep->musb;

    ep->fifostart[1] = 0;
    ep->fifolen[1] = 0;

#ifdef CLEAR_NAK
    if (ep->status[1] != USB_RET_NAK) {
#endif
        ep->csr[1] &= ~MGC_M_RXCSR_H_REQPKT;
        if (!epnum)
            ep->csr[0] &= ~MGC_M_CSR0_H_REQPKT;
#ifdef CLEAR_NAK
    }
#endif

    /* Clear all of the imaginable error bits first */
    ep->csr[1] &= ~(MGC_M_RXCSR_H_ERROR | MGC_M_RXCSR_H_RXSTALL |
                    MGC_M_RXCSR_DATAERROR);
    if (!epnum)
        ep->csr[0] &= ~(MGC_M_CSR0_H_ERROR | MGC_M_CSR0_H_RXSTALL |
                        MGC_M_CSR0_H_NAKTIMEOUT | MGC_M_CSR0_H_NO_PING);

    if (ep->status[1] == USB_RET_STALL) {
        ep->status[1] = 0;
        packey->len = 0;

        ep->csr[1] |= MGC_M_RXCSR_H_RXSTALL;
        if (!epnum)
            ep->csr[0] |= MGC_M_CSR0_H_RXSTALL;
    }

    if (ep->status[1] == USB_RET_NAK) {
        ep->status[1] = 0;

        /* NAK timeouts are only generated in Bulk transfers and
         * Data-errors in Isochronous.  */
        if (ep->interrupt[1])
            return musb_packet(s, ep, epnum, USB_TOKEN_IN,
                            packey->len, musb_rx_packet_complete, 1);

        ep->csr[1] |= MGC_M_RXCSR_DATAERROR;
        if (!epnum)
            ep->csr[0] |= MGC_M_CSR0_H_NAKTIMEOUT;
    }

    if (ep->status[1] < 0) {
        if (ep->status[1] == USB_RET_BABBLE) {
            musb_intr_set(s, musb_irq_rst_babble, 1);
            return;
        }

        /* Pretend we've tried three times already and failed (in
         * case of a control transfer).  */
        ep->csr[1] |= MGC_M_RXCSR_H_ERROR;
        if (!epnum)
            ep->csr[0] |= MGC_M_CSR0_H_ERROR;

        musb_rx_intr_set(s, epnum, 1);
        return;
    }
    /* TODO: check len for over/underruns of an OUT packet?  */
    /* TODO: perhaps make use of e->ext_size[1] here.  */

    packey->len = ep->status[1];

    if (!(ep->csr[1] & (MGC_M_RXCSR_H_RXSTALL | MGC_M_RXCSR_DATAERROR))) {
        ep->csr[1] |= MGC_M_RXCSR_FIFOFULL | MGC_M_RXCSR_RXPKTRDY;
        if (!epnum)
            ep->csr[0] |= MGC_M_CSR0_RXPKTRDY;

        ep->rxcount = packey->len; /* XXX: MIN(packey->len, ep->maxp[1]); */
        /* In DMA mode: assert DMA request for this EP */
    }

    /* Only if DMA has not been asserted */
    musb_rx_intr_set(s, epnum, 1);
}

static void musb_device_destroy(USBBus *bus, USBDevice *dev)
{
    MUSBState *s = container_of(bus, MUSBState, bus);
    int ep, dir;

    for (ep = 0; ep < 16; ep++) {
        for (dir = 0; dir < 2; dir++) {
            if (s->ep[ep].packey[dir].p.owner != dev) {
                continue;
            }
            usb_cancel_packet(&s->ep[ep].packey[dir].p);
            /* status updates needed here? */
        }
    }
}

static void musb_tx_rdy(MUSBState *s, int epnum)
{
    MUSBEndPoint *ep = s->ep + epnum;
    int pid;
    int total, valid = 0;
    TRACE("start %d, len %d",  ep->fifostart[0], ep->fifolen[0] );
    ep->fifostart[0] += ep->fifolen[0];
    ep->fifolen[0] = 0;

    /* XXX: how's the total size of the packet retrieved exactly in
     * the generic case?  */
    total = ep->maxp[0] & 0x3ff;

    if (ep->ext_size[0]) {
        total = ep->ext_size[0];
        ep->ext_size[0] = 0;
        valid = 1;
    }

    /* If the packet is not fully ready yet, wait for a next segment.  */
    if (epnum && (ep->fifostart[0]) < total)
        return;

    if (!valid)
        total = ep->fifostart[0];

    pid = USB_TOKEN_OUT;
    if (!epnum && (ep->csr[0] & MGC_M_CSR0_H_SETUPPKT)) {
        pid = USB_TOKEN_SETUP;
        if (total != 8) {
            TRACE("illegal SETUPPKT length of %i bytes", total);
        }
        /* Controller should retry SETUP packets three times on errors
         * but it doesn't make sense for us to do that.  */
    }

    return musb_packet(s, ep, epnum, pid,
                    total, musb_tx_packet_complete, 0);
}

static void musb_rx_req(MUSBState *s, int epnum)
{
    MUSBEndPoint *ep = s->ep + epnum;
    int total;

    /* If we already have a packet, which didn't fit into the
     * 64 bytes of the FIFO, only move the FIFO start and return. (Obsolete) */
    if (ep->packey[1].p.pid == USB_TOKEN_IN && ep->status[1] >= 0 &&
                    (ep->fifostart[1]) + ep->rxcount <
                    ep->packey[1].p.len) {
        TRACE("0x%08x, %d",  ep->fifostart[1], ep->rxcount );
        ep->fifostart[1] += ep->rxcount;
        ep->fifolen[1] = 0;

        ep->rxcount = MIN(ep->packey[0].p.len - (ep->fifostart[1]),
                        ep->maxp[1]);

        ep->csr[1] &= ~MGC_M_RXCSR_H_REQPKT;
        if (!epnum)
            ep->csr[0] &= ~MGC_M_CSR0_H_REQPKT;

        /* Clear all of the error bits first */
        ep->csr[1] &= ~(MGC_M_RXCSR_H_ERROR | MGC_M_RXCSR_H_RXSTALL |
                        MGC_M_RXCSR_DATAERROR);
        if (!epnum)
            ep->csr[0] &= ~(MGC_M_CSR0_H_ERROR | MGC_M_CSR0_H_RXSTALL |
                            MGC_M_CSR0_H_NAKTIMEOUT | MGC_M_CSR0_H_NO_PING);

        ep->csr[1] |= MGC_M_RXCSR_FIFOFULL | MGC_M_RXCSR_RXPKTRDY;
        if (!epnum)
            ep->csr[0] |= MGC_M_CSR0_RXPKTRDY;
        musb_rx_intr_set(s, epnum, 1);
        return;
    }

    /* The driver sets maxp[1] to 64 or less because it knows the hardware
     * FIFO is this deep.  Bigger packets get split in
     * usb_generic_handle_packet but we can also do the splitting locally
     * for performance.  It turns out we can also have a bigger FIFO and
     * ignore the limit set in ep->maxp[1].  The Linux MUSB driver deals
     * OK with single packets of even 32KB and we avoid splitting, however
     * usb_msd.c sometimes sends a packet bigger than what Linux expects
     * (e.g. 8192 bytes instead of 4096) and we get an OVERRUN.  Splitting
     * hides this overrun from Linux.  Up to 4096 everything is fine
     * though.  Currently this is disabled.
     *
     * XXX: mind ep->fifosize.  */
    total = MIN(ep->maxp[1] & 0x3ff, sizeof(s->buf));

#ifdef SETUPLEN_HACK
    /* Why should *we* do that instead of Linux?  */
    if (!epnum) {
        if (ep->packey[0].p.devaddr == 2) {
            total = MIN(s->setup_len, 8);
        } else {
            total = MIN(s->setup_len, 64);
        }
        s->setup_len -= total;
    }
#endif

    return musb_packet(s, ep, epnum, USB_TOKEN_IN,
                    total, musb_rx_packet_complete, 1);
}

static uint8_t musb_read_fifo(MUSBEndPoint *ep)
{
    uint8_t value;
    if (ep->fifolen[1] >= 64) {
        /* We have a FIFO underrun */
        TRACE("EP%d FIFO is now empty, stop reading", ep->epnum);
        return 0x00000000;
    }
    /* In DMA mode clear RXPKTRDY and set REQPKT automatically
     * (if AUTOREQ is set) */

    ep->csr[1] &= ~MGC_M_RXCSR_FIFOFULL;
    value=ep->buf[1][ep->fifostart[1] + ep->fifolen[1] ++];
    TRACE("EP%d 0x%02x, %d", ep->epnum, value, ep->fifolen[1] );
    return value;
}

static void musb_write_fifo(MUSBEndPoint *ep, uint8_t value)
{
    TRACE("EP%d = %02x", ep->epnum, value);
    if (ep->fifolen[0] >= 64) {
        /* We have a FIFO overrun */
        TRACE("EP%d FIFO exceeded 64 bytes, stop feeding data", ep->epnum);
        return;
     }

     ep->buf[0][ep->fifostart[0] + ep->fifolen[0] ++] = value;
     ep->csr[0] |= MGC_M_TXCSR_FIFONOTEMPTY;
}

static void musb_ep_frame_cancel(MUSBEndPoint *ep, int dir)
{
    if (ep->intv_timer[dir])
        qemu_del_timer(ep->intv_timer[dir]);
}

/* Bus control */
static uint8_t musb_busctl_readb(void *opaque, int ep, int addr)
{
    MUSBState *s = (MUSBState *) opaque;

    switch (addr) {
    /* For USB2.0 HS hubs only */
    case MUSB_HDRC_TXHUBADDR:
        return s->ep[ep].haddr[0];
    case MUSB_HDRC_TXHUBPORT:
        return s->ep[ep].hport[0];
    case MUSB_HDRC_RXHUBADDR:
        return s->ep[ep].haddr[1];
    case MUSB_HDRC_RXHUBPORT:
        return s->ep[ep].hport[1];

    default:
        TRACE("unknown register 0x%02x", addr);
        return 0x00;
    };
}

static void musb_busctl_writeb(void *opaque, int ep, int addr, uint8_t value)
{
    MUSBState *s = (MUSBState *) opaque;

    switch (addr) {
    case MUSB_HDRC_TXFUNCADDR:
        s->ep[ep].faddr[0] = value;
        break;
    case MUSB_HDRC_RXFUNCADDR:
        s->ep[ep].faddr[1] = value;
        break;
    case MUSB_HDRC_TXHUBADDR:
        s->ep[ep].haddr[0] = value;
        break;
    case MUSB_HDRC_TXHUBPORT:
        s->ep[ep].hport[0] = value;
        break;
    case MUSB_HDRC_RXHUBADDR:
        s->ep[ep].haddr[1] = value;
        break;
    case MUSB_HDRC_RXHUBPORT:
        s->ep[ep].hport[1] = value;
        break;

    default:
        TRACE("unknown register 0x%02x", addr);
        break;
    };
}

static uint16_t musb_busctl_readh(void *opaque, int ep, int addr)
{
    MUSBState *s = (MUSBState *) opaque;

    switch (addr) {
    case MUSB_HDRC_TXFUNCADDR:
        return s->ep[ep].faddr[0];
    case MUSB_HDRC_RXFUNCADDR:
        return s->ep[ep].faddr[1];

    default:
        return musb_busctl_readb(s, ep, addr) |
                (musb_busctl_readb(s, ep, addr | 1) << 8);
    };
}

static void musb_busctl_writeh(void *opaque, int ep, int addr, uint16_t value)
{
    MUSBState *s = (MUSBState *) opaque;

    switch (addr) {
    case MUSB_HDRC_TXFUNCADDR:
        s->ep[ep].faddr[0] = value;
        break;
    case MUSB_HDRC_RXFUNCADDR:
        s->ep[ep].faddr[1] = value;
        break;

    default:
        musb_busctl_writeb(s, ep, addr, value & 0xff);
        musb_busctl_writeb(s, ep, addr | 1, value >> 8);
    };
}

/* Endpoint control */
static uint8_t musb_ep_readb(void *opaque, int ep, int addr)
{
    MUSBState *s = (MUSBState *) opaque;

    switch (addr) {
    case MUSB_HDRC_TXTYPE:
        return s->ep[ep].type[0];
    case MUSB_HDRC_TXINTERVAL:
        return s->ep[ep].interval[0];
    case MUSB_HDRC_RXTYPE:
        return s->ep[ep].type[1];
    case MUSB_HDRC_RXINTERVAL:
        return s->ep[ep].interval[1];
    case (MUSB_HDRC_FIFOSIZE & ~1):
        return 0x00;
    case MUSB_HDRC_FIFOSIZE:
        return ep ? s->ep[ep].fifosize : s->ep[ep].config;
    case MUSB_HDRC_RXCOUNT:
        return s->ep[ep].rxcount;

    default:
        TRACE("unknown register 0x%02x", addr);
        return 0x00;
    };
}

static void musb_ep_writeb(void *opaque, int ep, int addr, uint8_t value)
{
    MUSBState *s = (MUSBState *) opaque;

    switch (addr) {
    case MUSB_HDRC_TXTYPE:
        s->ep[ep].type[0] = value;
        break;
    case MUSB_HDRC_TXINTERVAL:
        s->ep[ep].interval[0] = value;
        musb_ep_frame_cancel(&s->ep[ep], 0);
        break;
    case MUSB_HDRC_RXTYPE:
        s->ep[ep].type[1] = value;
        break;
    case MUSB_HDRC_RXINTERVAL:
        s->ep[ep].interval[1] = value;
        musb_ep_frame_cancel(&s->ep[ep], 1);
        break;
    case (MUSB_HDRC_FIFOSIZE & ~1):
        break;
    case MUSB_HDRC_FIFOSIZE:
        TRACE("somebody messes with fifosize (now %i bytes)", value);
        s->ep[ep].fifosize = value;
        break;
    default:
        TRACE("unknown register 0x%02x", addr);
        break;
    };
}

static uint16_t musb_ep_readh(void *opaque, int ep, int addr)
{
    MUSBState *s = (MUSBState *) opaque;
    uint16_t ret;

    switch (addr) {
    case MUSB_HDRC_TXMAXP:
        return s->ep[ep].maxp[0];
    case MUSB_HDRC_TXCSR:
        return s->ep[ep].csr[0];
    case MUSB_HDRC_RXMAXP:
        return s->ep[ep].maxp[1];
    case MUSB_HDRC_RXCSR:
        ret = s->ep[ep].csr[1];

        /* TODO: This and other bits probably depend on
         * ep->csr[1] & MGC_M_RXCSR_AUTOCLEAR.  */
        if (s->ep[ep].csr[1] & MGC_M_RXCSR_AUTOCLEAR)
            s->ep[ep].csr[1] &= ~MGC_M_RXCSR_RXPKTRDY;

        return ret;
    case MUSB_HDRC_RXCOUNT:
        return s->ep[ep].rxcount;

    default:
        return musb_ep_readb(s, ep, addr) |
                (musb_ep_readb(s, ep, addr | 1) << 8);
    };
}

static void musb_ep_writeh(void *opaque, int ep, int addr, uint16_t value)
{
    MUSBState *s = (MUSBState *) opaque;

    switch (addr) {
    case MUSB_HDRC_TXMAXP:
        s->ep[ep].maxp[0] = value;
        break;
    case MUSB_HDRC_TXCSR:
        if (ep) {
            s->ep[ep].csr[0] &= value & 0xa6;
            s->ep[ep].csr[0] |= value & 0xff59;
        } else {
            s->ep[ep].csr[0] &= value & 0x85;
            s->ep[ep].csr[0] |= value & 0xf7a;
        }

        musb_ep_frame_cancel(&s->ep[ep], 0);

        if ((ep && (value & MGC_M_TXCSR_FLUSHFIFO)) ||
                        (!ep && (value & MGC_M_CSR0_FLUSHFIFO))) {
            s->ep[ep].fifolen[0] = 0;
            s->ep[ep].fifostart[0] = 0;
            if (ep)
                s->ep[ep].csr[0] &=
                        ~(MGC_M_TXCSR_FIFONOTEMPTY | MGC_M_TXCSR_TXPKTRDY);
            else
                s->ep[ep].csr[0] &=
                        ~(MGC_M_CSR0_TXPKTRDY | MGC_M_CSR0_RXPKTRDY);
        }
        if (
                        (ep &&
#ifdef CLEAR_NAK
                         (value & MGC_M_TXCSR_TXPKTRDY) &&
                         !(value & MGC_M_TXCSR_H_NAKTIMEOUT)) ||
#else
                         (value & MGC_M_TXCSR_TXPKTRDY)) ||
#endif
                        (!ep &&
#ifdef CLEAR_NAK
                         (value & MGC_M_CSR0_TXPKTRDY) &&
                         !(value & MGC_M_CSR0_H_NAKTIMEOUT)))
#else
                         (value & MGC_M_CSR0_TXPKTRDY)))
#endif
            musb_tx_rdy(s, ep);
        if (!ep &&
                        (value & MGC_M_CSR0_H_REQPKT) &&
#ifdef CLEAR_NAK
                        !(value & (MGC_M_CSR0_H_NAKTIMEOUT |
                                        MGC_M_CSR0_RXPKTRDY)))
#else
                        !(value & MGC_M_CSR0_RXPKTRDY))
#endif
            musb_rx_req(s, ep);
        break;

    case MUSB_HDRC_RXMAXP:
        s->ep[ep].maxp[1] = value;
        break;
    case MUSB_HDRC_RXCSR:
        /* (DMA mode only) */
        if (
                (value & MGC_M_RXCSR_H_AUTOREQ) &&
                !(value & MGC_M_RXCSR_RXPKTRDY) &&
                (s->ep[ep].csr[1] & MGC_M_RXCSR_RXPKTRDY))
            value |= MGC_M_RXCSR_H_REQPKT;

        s->ep[ep].csr[1] &= 0x102 | (value & 0x4d);
        s->ep[ep].csr[1] |= value & 0xfeb0;

        musb_ep_frame_cancel(&s->ep[ep], 1);

        if (value & MGC_M_RXCSR_FLUSHFIFO) {
            s->ep[ep].fifolen[1] = 0;
            s->ep[ep].fifostart[1] = 0;
            s->ep[ep].csr[1] &= ~(MGC_M_RXCSR_FIFOFULL | MGC_M_RXCSR_RXPKTRDY);
            /* If double buffering and we have two packets ready, flush
             * only the first one and set up the fifo at the second packet.  */
        }
#ifdef CLEAR_NAK
        if ((value & MGC_M_RXCSR_H_REQPKT) && !(value & MGC_M_RXCSR_DATAERROR))
#else
        if (value & MGC_M_RXCSR_H_REQPKT)
#endif
            musb_rx_req(s, ep);
        break;
    case MUSB_HDRC_RXCOUNT:
        s->ep[ep].rxcount = value;
        break;

    default:
        musb_ep_writeb(s, ep, addr, value & 0xff);
        musb_ep_writeb(s, ep, addr | 1, value >> 8);
    };
}

/* Generic control */
static uint32_t musb_readb(void *opaque, target_phys_addr_t addr)
{
    MUSBState *s = (MUSBState *) opaque;
    int ep, i;
    uint8_t ret;

    switch (addr) {
    case MUSB_HDRC_FADDR:
        return s->faddr;
    case MUSB_HDRC_POWER:
        return s->power;
    case MUSB_HDRC_INTRUSB:
        ret = s->intr;
        for (i = 0; i < sizeof(ret) * 8; i ++)
            if (ret & (1 << i))
                musb_intr_set(s, i, 0);
        return ret;
    case MUSB_HDRC_INTRUSBE:
        return s->mask;
    case MUSB_HDRC_INDEX:
        return s->idx;
    case MUSB_HDRC_TESTMODE:
        return 0x00;

    case MUSB_HDRC_EP_IDX ... (MUSB_HDRC_EP_IDX + 0xf):
        return musb_ep_readb(s, s->idx, addr & 0xf);

    case MUSB_HDRC_DEVCTL:
        return s->devctl;

    case MUSB_HDRC_TXFIFOSZ:
    case MUSB_HDRC_RXFIFOSZ:
    case MUSB_HDRC_VCTRL:
        /* TODO */
        return 0x00;

    case MUSB_HDRC_HWVERS:
        return (1 << 10) | 400;

    case (MUSB_HDRC_VCTRL | 1):
    case (MUSB_HDRC_HWVERS | 1):
    case (MUSB_HDRC_DEVCTL | 1):
        return 0x00;

    case MUSB_HDRC_BUSCTL ... (MUSB_HDRC_BUSCTL + 0x7f):
        ep = (addr >> 3) & 0xf;
        return musb_busctl_readb(s, ep, addr & 0x7);

    case MUSB_HDRC_EP ... (MUSB_HDRC_EP + 0xff):
        ep = (addr >> 4) & 0xf;
        return musb_ep_readb(s, ep, addr & 0xf);

    case MUSB_HDRC_FIFO ... (MUSB_HDRC_FIFO + 0x3f):
        ep = ((addr - MUSB_HDRC_FIFO) >> 2) & 0xf;
        return musb_read_fifo(s->ep + ep);

    default:
        TRACE("unknown register 0x%02x", (int) addr);
        return 0x00;
    };
}

static void musb_writeb(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    MUSBState *s = (MUSBState *) opaque;
    int ep;

    switch (addr) {
    case MUSB_HDRC_FADDR:
        s->faddr = value & 0x7f;
        break;
    case MUSB_HDRC_POWER:
        s->power = (value & 0xef) | (s->power & 0x10);
        /* MGC_M_POWER_RESET is also read-only in Peripheral Mode */
        if ((value & MGC_M_POWER_RESET) && s->port.dev) {
            usb_send_msg(s->port.dev, USB_MSG_RESET);
            /* Negotiate high-speed operation if MGC_M_POWER_HSENAB is set.  */
            if ((value & MGC_M_POWER_HSENAB) &&
                            s->port.dev->speed == USB_SPEED_HIGH)
                s->power |= MGC_M_POWER_HSMODE;	/* Success */
            /* Restart frame counting.  */
        }
        if (value & MGC_M_POWER_SUSPENDM) {
            /* When all transfers finish, suspend and if MGC_M_POWER_ENSUSPEND
             * is set, also go into low power mode.  Frame counting stops.  */
            /* XXX: Cleared when the interrupt register is read */
        }
        if (value & MGC_M_POWER_RESUME) {
            /* Wait 20ms and signal resuming on the bus.  Frame counting
             * restarts.  */
        }
        break;
    case MUSB_HDRC_INTRUSB:
        break;
    case MUSB_HDRC_INTRUSBE:
        s->mask = value & 0xff;
        break;
    case MUSB_HDRC_INDEX:
        s->idx = value & 0xf;
        break;
    case MUSB_HDRC_TESTMODE:
        break;

    case MUSB_HDRC_EP_IDX ... (MUSB_HDRC_EP_IDX + 0xf):
        musb_ep_writeb(s, s->idx, addr & 0xf, value);
        break;

    case MUSB_HDRC_DEVCTL:
        s->session = !!(value & MGC_M_DEVCTL_SESSION);
        musb_session_update(s,
                        !!s->port.dev,
                        !!(s->devctl & MGC_M_DEVCTL_SESSION));

        /* It seems this is the only R/W bit in this register?  */
        s->devctl &= ~MGC_M_DEVCTL_SESSION;
        s->devctl |= value & MGC_M_DEVCTL_SESSION;
        break;

    case MUSB_HDRC_TXFIFOSZ:
    case MUSB_HDRC_RXFIFOSZ:
    case MUSB_HDRC_VCTRL:
        /* TODO */
        break;

    case (MUSB_HDRC_VCTRL | 1):
    case (MUSB_HDRC_DEVCTL | 1):
        break;

    case MUSB_HDRC_BUSCTL ... (MUSB_HDRC_BUSCTL + 0x7f):
        ep = (addr >> 3) & 0xf;
        musb_busctl_writeb(s, ep, addr & 0x7, value);
        break;

    case MUSB_HDRC_EP ... (MUSB_HDRC_EP + 0xff):
        ep = (addr >> 4) & 0xf;
        musb_ep_writeb(s, ep, addr & 0xf, value);
        break;

    case MUSB_HDRC_FIFO ... (MUSB_HDRC_FIFO + 0x3f):
        ep = ((addr - MUSB_HDRC_FIFO) >> 2) & 0xf;
        musb_write_fifo(s->ep + ep, value & 0xff);
        break;

    default:
        TRACE("unknown register 0x%02x", (int) addr);
        break;
    };
}

static uint32_t musb_readh(void *opaque, target_phys_addr_t addr)
{
    MUSBState *s = (MUSBState *) opaque;
    int ep, i;
    uint16_t ret;

    switch (addr) {
    case MUSB_HDRC_INTRTX:
        ret = s->tx_intr;
        /* Auto clear */
        for (i = 0; i < sizeof(ret) * 8; i ++)
            if (ret & (1 << i))
                musb_tx_intr_set(s, i, 0);
        return ret;
    case MUSB_HDRC_INTRRX:
        ret = s->rx_intr;
        /* Auto clear */
        for (i = 0; i < sizeof(ret) * 8; i ++)
            if (ret & (1 << i))
                musb_rx_intr_set(s, i, 0);
        return ret;
    case MUSB_HDRC_INTRTXE:
        return s->tx_mask;
    case MUSB_HDRC_INTRRXE:
        return s->rx_mask;

    case MUSB_HDRC_FRAME:
        /* TODO */
        return 0x0000;
    case MUSB_HDRC_TXFIFOADDR:
        return s->ep[s->idx].fifoaddr[0];
    case MUSB_HDRC_RXFIFOADDR:
        return s->ep[s->idx].fifoaddr[1];

    case MUSB_HDRC_EP_IDX ... (MUSB_HDRC_EP_IDX + 0xf):
        return musb_ep_readh(s, s->idx, addr & 0xf);

    case MUSB_HDRC_BUSCTL ... (MUSB_HDRC_BUSCTL + 0x7f):
        ep = (addr >> 3) & 0xf;
        return musb_busctl_readh(s, ep, addr & 0x7);

    case MUSB_HDRC_EP ... (MUSB_HDRC_EP + 0xff):
        ep = (addr >> 4) & 0xf;
        return musb_ep_readh(s, ep, addr & 0xf);

    case MUSB_HDRC_FIFO ... (MUSB_HDRC_FIFO + 0x3f):
        ep = ((addr - MUSB_HDRC_FIFO) >> 2) & 0xf;
        return (musb_read_fifo(s->ep + ep) | musb_read_fifo(s->ep + ep) << 8);

    default:
        return musb_readb(s, addr) | (musb_readb(s, addr | 1) << 8);
    };
}

static void musb_writeh(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    MUSBState *s = (MUSBState *) opaque;
    int ep;

    switch (addr) {
    case MUSB_HDRC_INTRTXE:
        s->tx_mask = value;
        /* XXX: the masks seem to apply on the raising edge like with
         * edge-triggered interrupts, thus no need to update.  I may be
         * wrong though.  */
        break;
    case MUSB_HDRC_INTRRXE:
        s->rx_mask = value;
        break;

    case MUSB_HDRC_FRAME:
        /* TODO */
        break;
    case MUSB_HDRC_TXFIFOADDR:
        s->ep[s->idx].fifoaddr[0] = value;
        s->ep[s->idx].buf[0] =
                s->buf + ((value << 3) & 0x7ff );
        break;
    case MUSB_HDRC_RXFIFOADDR:
        s->ep[s->idx].fifoaddr[1] = value;
        s->ep[s->idx].buf[1] =
                s->buf + ((value << 3) & 0x7ff);
        break;

    case MUSB_HDRC_EP_IDX ... (MUSB_HDRC_EP_IDX + 0xf):
        musb_ep_writeh(s, s->idx, addr & 0xf, value);
        break;

    case MUSB_HDRC_BUSCTL ... (MUSB_HDRC_BUSCTL + 0x7f):
        ep = (addr >> 3) & 0xf;
        musb_busctl_writeh(s, ep, addr & 0x7, value);
        break;

    case MUSB_HDRC_EP ... (MUSB_HDRC_EP + 0xff):
        ep = (addr >> 4) & 0xf;
        musb_ep_writeh(s, ep, addr & 0xf, value);
        break;

    case MUSB_HDRC_FIFO ... (MUSB_HDRC_FIFO + 0x3f):
        ep = ((addr - MUSB_HDRC_FIFO) >> 2) & 0xf;
        musb_write_fifo(s->ep + ep, value & 0xff);
        musb_write_fifo(s->ep + ep, (value >> 8) & 0xff);
        break;

    default:
        musb_writeb(s, addr, value & 0xff);
        musb_writeb(s, addr | 1, value >> 8);
    };
}

static uint32_t musb_readw(void *opaque, target_phys_addr_t addr)
{
    MUSBState *s = (MUSBState *) opaque;
    int ep;

    switch (addr) {
    case MUSB_HDRC_FIFO ... (MUSB_HDRC_FIFO + 0x3f):
        ep = ((addr - MUSB_HDRC_FIFO) >> 2) & 0xf;
        return ( musb_read_fifo(s->ep + ep)       |
                 musb_read_fifo(s->ep + ep) << 8  |
                 musb_read_fifo(s->ep + ep) << 16 |
                 musb_read_fifo(s->ep + ep) << 24 );
    default:
        TRACE("unknown register 0x%02x", (int) addr);
        return 0x00000000;
    };
}

static void musb_writew(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    MUSBState *s = (MUSBState *) opaque;
    int ep;

    switch (addr) {
    case MUSB_HDRC_FIFO ... (MUSB_HDRC_FIFO + 0x3f):
        ep = ((addr - MUSB_HDRC_FIFO) >> 2) & 0xf;
        musb_write_fifo(s->ep + ep, value & 0xff);
        musb_write_fifo(s->ep + ep, (value >> 8 ) & 0xff);
        musb_write_fifo(s->ep + ep, (value >> 16) & 0xff);
        musb_write_fifo(s->ep + ep, (value >> 24) & 0xff);
            break;
    default:
        TRACE("unknown register 0x%02x", (int) addr);
        break;
    };
}

CPUReadMemoryFunc * const musb_read[] = {
    musb_readb,
    musb_readh,
    musb_readw,
};

CPUWriteMemoryFunc * const musb_write[] = {
    musb_writeb,
    musb_writeh,
    musb_writew,
};
