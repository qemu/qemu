/*
 * USB UHCI controller emulation
 *
 * Copyright (c) 2005 Fabrice Bellard
 *
 * Copyright (c) 2008 Max Krasnyansky
 *     Magor rewrite of the UHCI data structures parser and frame processor
 *     Support for fully async operation and multiple outstanding transactions
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
//#define DEBUG_DUMP_DATA

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

#ifdef DEBUG
#define dprintf printf

const char *pid2str(int pid)
{
    switch (pid) {
    case USB_TOKEN_SETUP: return "SETUP";
    case USB_TOKEN_IN:    return "IN";
    case USB_TOKEN_OUT:   return "OUT";
    }
    return "?";
}

#else
#define dprintf(...)
#endif

#ifdef DEBUG_DUMP_DATA
static void dump_data(const uint8_t *data, int len)
{
    int i;

    printf("uhci: data: ");
    for(i = 0; i < len; i++)
        printf(" %02x", data[i]);
    printf("\n");
}
#else
static void dump_data(const uint8_t *data, int len) {}
#endif

/* 
 * Pending async transaction.
 * 'packet' must be the first field because completion
 * handler does "(UHCIAsync *) pkt" cast.
 */
typedef struct UHCIAsync {
    USBPacket packet;
    struct UHCIAsync *next;
    uint32_t  td;
    uint32_t  token;
    int8_t    valid;
    uint8_t   done;
    uint8_t   buffer[2048];
} UHCIAsync;

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

    /* Active packets */
    UHCIAsync *async_pending;
    UHCIAsync *async_pool;
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

static UHCIAsync *uhci_async_alloc(UHCIState *s)
{
    UHCIAsync *async = qemu_malloc(sizeof(UHCIAsync));
    if (async) {
        memset(&async->packet, 0, sizeof(async->packet));
        async->valid = 0;
        async->td    = 0;
        async->token = 0;
        async->done  = 0;
        async->next  = NULL;
    }

    return async;
}

static void uhci_async_free(UHCIState *s, UHCIAsync *async)
{
    qemu_free(async);
}

static void uhci_async_link(UHCIState *s, UHCIAsync *async)
{
    async->next = s->async_pending;
    s->async_pending = async;
}

static void uhci_async_unlink(UHCIState *s, UHCIAsync *async)
{
    UHCIAsync *curr = s->async_pending;
    UHCIAsync **prev = &s->async_pending;

    while (curr) {
	if (curr == async) {
            *prev = curr->next;
            return;
        }

        prev = &curr->next;
        curr = curr->next;
    }
}

static void uhci_async_cancel(UHCIState *s, UHCIAsync *async)
{
    dprintf("uhci: cancel td 0x%x token 0x%x done %u\n",
           async->td, async->token, async->done);

    if (!async->done)
        usb_cancel_packet(&async->packet);
    uhci_async_free(s, async);
}

/*
 * Mark all outstanding async packets as invalid.
 * This is used for canceling them when TDs are removed by the HCD.
 */
static UHCIAsync *uhci_async_validate_begin(UHCIState *s)
{
    UHCIAsync *async = s->async_pending;

    while (async) {
        async->valid--;
        async = async->next;
    }
    return NULL;
}

/*
 * Cancel async packets that are no longer valid
 */
static void uhci_async_validate_end(UHCIState *s)
{
    UHCIAsync *curr = s->async_pending;
    UHCIAsync **prev = &s->async_pending;
    UHCIAsync *next;

    while (curr) {
        if (curr->valid > 0) {
            prev = &curr->next;
            curr = curr->next;
            continue;
        }

        next = curr->next;

        /* Unlink */
        *prev = next;

        uhci_async_cancel(s, curr);

        curr = next;
    }
}

static void uhci_async_cancel_all(UHCIState *s)
{
    UHCIAsync *curr = s->async_pending;
    UHCIAsync *next;

    while (curr) {
        next = curr->next;

        uhci_async_cancel(s, curr);

        curr = next;
    }

    s->async_pending = NULL;
}

static UHCIAsync *uhci_async_find_td(UHCIState *s, uint32_t addr, uint32_t token)
{
    UHCIAsync *async = s->async_pending;
    UHCIAsync *match = NULL;
    int count = 0;

    /*
     * We're looking for the best match here. ie both td addr and token.
     * Otherwise we return last good match. ie just token.
     * It's ok to match just token because it identifies the transaction
     * rather well, token includes: device addr, endpoint, size, etc.
     *
     * Also since we queue async transactions in reverse order by returning
     * last good match we restores the order.
     *
     * It's expected that we wont have a ton of outstanding transactions.
     * If we ever do we'd want to optimize this algorithm.
     */

    while (async) {
        if (async->token == token) {
            /* Good match */
            match = async;

            if (async->td == addr) {
                /* Best match */
                break;
            }
        }

        async = async->next;
        count++;
    }

    if (count > 64)
	fprintf(stderr, "uhci: warning lots of async transactions\n");

    return match;
}

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

    dprintf("uhci: full reset\n");

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

    uhci_async_cancel_all(s);
}

static void uhci_save(QEMUFile *f, void *opaque)
{
    UHCIState *s = opaque;
    uint8_t num_ports = NB_PORTS;
    int i;

    uhci_async_cancel_all(s);

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
    dprintf("uhci: writew port=0x%04x val=0x%04x\n", addr, val);

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

    dprintf("uhci: readw port=0x%04x val=0x%04x\n", addr, val);

    return val;
}

static void uhci_ioport_writel(void *opaque, uint32_t addr, uint32_t val)
{
    UHCIState *s = opaque;

    addr &= 0x1f;
    dprintf("uhci: writel port=0x%04x val=0x%08x\n", addr, val);

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
    int i, ret;

    dprintf("uhci: packet enter. pid %s addr 0x%02x ep %d len %d\n",
           pid2str(p->pid), p->devaddr, p->devep, p->len);
    if (p->pid == USB_TOKEN_OUT || p->pid == USB_TOKEN_SETUP)
        dump_data(p->data, p->len);

    ret = USB_RET_NODEV;
    for (i = 0; i < NB_PORTS && ret == USB_RET_NODEV; i++) {
        UHCIPort *port = &s->ports[i];
        USBDevice *dev = port->port.dev;

        if (dev && (port->ctrl & UHCI_PORT_EN))
            ret = dev->handle_packet(dev, p);
    }

    dprintf("uhci: packet exit. ret %d len %d\n", ret, p->len);
    if (p->pid == USB_TOKEN_IN && ret > 0)
        dump_data(p->data, ret);

    return ret;
}

static void uhci_async_complete(USBPacket * packet, void *opaque);
static void uhci_process_frame(UHCIState *s);

/* return -1 if fatal error (frame must be stopped)
          0 if TD successful
          1 if TD unsuccessful or inactive
*/
static int uhci_complete_td(UHCIState *s, UHCI_TD *td, UHCIAsync *async, uint32_t *int_mask)
{
    int len = 0, max_len, err, ret;
    uint8_t pid;

    max_len = ((td->token >> 21) + 1) & 0x7ff;
    pid = td->token & 0xff;

    ret = async->packet.len;

    if (td->ctrl & TD_CTRL_IOC)
        *int_mask |= 0x01;

    if (td->ctrl & TD_CTRL_IOS)
        td->ctrl &= ~TD_CTRL_ACTIVE;

    if (ret < 0)
        goto out;

    len = async->packet.len;
    td->ctrl = (td->ctrl & ~0x7ff) | ((len - 1) & 0x7ff);

    /* The NAK bit may have been set by a previous frame, so clear it
       here.  The docs are somewhat unclear, but win2k relies on this
       behavior.  */
    td->ctrl &= ~(TD_CTRL_ACTIVE | TD_CTRL_NAK);

    if (pid == USB_TOKEN_IN) {
        if (len > max_len) {
            len = max_len;
            ret = USB_RET_BABBLE;
            goto out;
        }

        if (len > 0) {
            /* write the data back */
            cpu_physical_memory_write(td->buffer, async->buffer, len);
        }

        if ((td->ctrl & TD_CTRL_SPD) && len < max_len) {
            *int_mask |= 0x02;
            /* short packet: do not update QH */
            dprintf("uhci: short packet. td 0x%x token 0x%x\n", async->td, async->token);
            return 1;
        }
    }

    /* success */
    return 0;

out:
    switch(ret) {
    case USB_RET_STALL:
        td->ctrl |= TD_CTRL_STALL;
        td->ctrl &= ~TD_CTRL_ACTIVE;
        return 1;

    case USB_RET_BABBLE:
        td->ctrl |= TD_CTRL_BABBLE | TD_CTRL_STALL;
        td->ctrl &= ~TD_CTRL_ACTIVE;
        /* frame interrupted */
        return -1;

    case USB_RET_NAK:
        td->ctrl |= TD_CTRL_NAK;
        if (pid == USB_TOKEN_SETUP)
            break;
	return 1;

    case USB_RET_NODEV:
    default:
	break;
    }

    /* Retry the TD if error count is not zero */

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
}

static int uhci_handle_td(UHCIState *s, uint32_t addr, UHCI_TD *td, uint32_t *int_mask)
{
    UHCIAsync *async;
    int len = 0, max_len;
    uint8_t pid;

    /* Is active ? */
    if (!(td->ctrl & TD_CTRL_ACTIVE))
        return 1;

    async = uhci_async_find_td(s, addr, td->token);
    if (async) {
        /* Already submitted */
        async->valid = 32;

        if (!async->done)
            return 1;

        uhci_async_unlink(s, async);
        goto done;
    }

    /* Allocate new packet */
    async = uhci_async_alloc(s);
    if (!async)
        return 1;

    async->valid = 10;
    async->td    = addr;
    async->token = td->token;

    max_len = ((td->token >> 21) + 1) & 0x7ff;
    pid = td->token & 0xff;

    async->packet.pid     = pid;
    async->packet.devaddr = (td->token >> 8) & 0x7f;
    async->packet.devep   = (td->token >> 15) & 0xf;
    async->packet.data    = async->buffer;
    async->packet.len     = max_len;
    async->packet.complete_cb     = uhci_async_complete;
    async->packet.complete_opaque = s;

    switch(pid) {
    case USB_TOKEN_OUT:
    case USB_TOKEN_SETUP:
        cpu_physical_memory_read(td->buffer, async->buffer, max_len);
        len = uhci_broadcast_packet(s, &async->packet);
        if (len >= 0)
            len = max_len;
        break;

    case USB_TOKEN_IN:
        len = uhci_broadcast_packet(s, &async->packet);
        break;

    default:
        /* invalid pid : frame interrupted */
        uhci_async_free(s, async);
        s->status |= UHCI_STS_HCPERR;
        uhci_update_irq(s);
        return -1;
    }
 
    if (len == USB_RET_ASYNC) {
        uhci_async_link(s, async);
        return 2;
    }

    async->packet.len = len;

done:
    len = uhci_complete_td(s, td, async, int_mask);
    uhci_async_free(s, async);
    return len;
}

static void uhci_async_complete(USBPacket *packet, void *opaque)
{
    UHCIState *s = opaque;
    UHCIAsync *async = (UHCIAsync *) packet;

    dprintf("uhci: async complete. td 0x%x token 0x%x\n", async->td, async->token);

    async->done = 1;

    uhci_process_frame(s);
}

static int is_valid(uint32_t link)
{
    return (link & 1) == 0;
}

static int is_qh(uint32_t link)
{
    return (link & 2) != 0;
}

static int depth_first(uint32_t link)
{
    return (link & 4) != 0;
}

/* QH DB used for detecting QH loops */
#define UHCI_MAX_QUEUES 128
typedef struct {
    uint32_t addr[UHCI_MAX_QUEUES];
    int      count;
} QhDb;

static void qhdb_reset(QhDb *db)
{
    db->count = 0;
}

/* Add QH to DB. Returns 1 if already present or DB is full. */
static int qhdb_insert(QhDb *db, uint32_t addr)
{
    int i;
    for (i = 0; i < db->count; i++)
        if (db->addr[i] == addr)
            return 1;

    if (db->count >= UHCI_MAX_QUEUES)
        return 1;

    db->addr[db->count++] = addr;
    return 0;
}

static void uhci_process_frame(UHCIState *s)
{
    uint32_t frame_addr, link, old_td_ctrl, val, int_mask;
    uint32_t curr_qh;
    int cnt, ret;
    UHCI_TD td;
    UHCI_QH qh;
    QhDb qhdb;

    frame_addr = s->fl_base_addr + ((s->frnum & 0x3ff) << 2);

    dprintf("uhci: processing frame %d addr 0x%x\n" , s->frnum, frame_addr);

    cpu_physical_memory_read(frame_addr, (uint8_t *)&link, 4);
    le32_to_cpus(&link);

    int_mask = 0;
    curr_qh  = 0;

    qhdb_reset(&qhdb);

    for (cnt = FRAME_MAX_LOOPS; is_valid(link) && cnt; cnt--) {
        if (is_qh(link)) {
            /* QH */

            if (qhdb_insert(&qhdb, link)) {
                /*
                 * We're going in circles. Which is not a bug because
                 * HCD is allowed to do that as part of the BW management. 
                 * In our case though it makes no sense to spin here. Sync transations 
                 * are already done, and async completion handler will re-process 
                 * the frame when something is ready.
                 */
                dprintf("uhci: detected loop. qh 0x%x\n", link);
                break;
            }

            cpu_physical_memory_read(link & ~0xf, (uint8_t *) &qh, sizeof(qh));
            le32_to_cpus(&qh.link);
            le32_to_cpus(&qh.el_link);

            dprintf("uhci: QH 0x%x load. link 0x%x elink 0x%x\n",
                    link, qh.link, qh.el_link);

            if (!is_valid(qh.el_link)) {
                /* QH w/o elements */
                curr_qh = 0;
                link = qh.link;
            } else {
                /* QH with elements */
            	curr_qh = link;
            	link = qh.el_link;
            }
            continue;
        }

        /* TD */
        cpu_physical_memory_read(link & ~0xf, (uint8_t *) &td, sizeof(td));
        le32_to_cpus(&td.link);
        le32_to_cpus(&td.ctrl);
        le32_to_cpus(&td.token);
        le32_to_cpus(&td.buffer);

        dprintf("uhci: TD 0x%x load. link 0x%x ctrl 0x%x token 0x%x qh 0x%x\n", 
                link, td.link, td.ctrl, td.token, curr_qh);

        old_td_ctrl = td.ctrl;
        ret = uhci_handle_td(s, link, &td, &int_mask);
        if (old_td_ctrl != td.ctrl) {
            /* update the status bits of the TD */
            val = cpu_to_le32(td.ctrl);
            cpu_physical_memory_write((link & ~0xf) + 4,
                                      (const uint8_t *)&val, sizeof(val));
        }

        if (ret < 0) {
            /* interrupted frame */
            break;
        }

        if (ret == 2 || ret == 1) {
            dprintf("uhci: TD 0x%x %s. link 0x%x ctrl 0x%x token 0x%x qh 0x%x\n",
                    link, ret == 2 ? "pend" : "skip",
                    td.link, td.ctrl, td.token, curr_qh);

            link = curr_qh ? qh.link : td.link;
            continue;
        }

        /* completed TD */

        dprintf("uhci: TD 0x%x done. link 0x%x ctrl 0x%x token 0x%x qh 0x%x\n", 
                link, td.link, td.ctrl, td.token, curr_qh);

        link = td.link;

        if (curr_qh) {
	    /* update QH element link */
            qh.el_link = link;
            val = cpu_to_le32(qh.el_link);
            cpu_physical_memory_write((curr_qh & ~0xf) + 4,
                                          (const uint8_t *)&val, sizeof(val));

            if (!depth_first(link)) {
               /* done with this QH */

               dprintf("uhci: QH 0x%x done. link 0x%x elink 0x%x\n",
                       curr_qh, qh.link, qh.el_link);

               curr_qh = 0;
               link    = qh.link;
            }
        }

        /* go to the next entry */
    }

    s->pending_int_mask = int_mask;
}

static void uhci_frame_timer(void *opaque)
{
    UHCIState *s = opaque;
    int64_t expire_time;

    if (!(s->cmd & UHCI_CMD_RS)) {
        /* Full stop */
        qemu_del_timer(s->frame_timer);
        /* set hchalted bit in status - UHCI11D 2.1.2 */
        s->status |= UHCI_STS_HCHALTED;

        dprintf("uhci: halted\n");
        return;
    }

    /* Complete the previous frame */
    if (s->pending_int_mask) {
        s->status2 |= s->pending_int_mask;
        s->status  |= UHCI_STS_USBINT;
        uhci_update_irq(s);
    }

    /* Start new frame */
    s->frnum = (s->frnum + 1) & 0x7ff;

    dprintf("uhci: new frame #%u\n" , s->frnum);

    uhci_async_validate_begin(s);

    uhci_process_frame(s);

    uhci_async_validate_end(s);

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

    register_savevm("uhci", 0, 1, uhci_save, uhci_load, s);
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

    register_savevm("uhci", 0, 1, uhci_save, uhci_load, s);
}
