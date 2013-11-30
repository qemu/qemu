/*
 * Raspberry Pi emulation (c) 2012-2013 Gregory Estrade
 * This code is licensed under the GNU GPLv2 and later.
 */

/* This is wrong at so many levels, but well, I'm releasing it anyway */

#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "hw/usb.h"
#include "sysemu/dma.h"

#include "bcm2835_usb_regs.h"

#define LOG_REG_LEVEL 2

/* You may have to change these parameters to get an almost-usable mouse
 * support.
 * The problem is that frame scheduling is all done by software, so a LOT of
 * interrupts are generated, which doesn't help... */
#define SOF_INCR 1
#define SOF_DELAY 5000

#define NB_HCHANS 8


#define TYPE_BCM2835_USB "bcm2835_usb"
#define BCM2835_USB(obj) \
        OBJECT_CHECK(bcm2835_usb_state, (obj), TYPE_BCM2835_USB)

typedef struct bcm2835_usb_state_struct bcm2835_usb_state;

typedef struct {
    bcm2835_usb_state *parent;
    int index;

    uint32_t hcchar;
    uint32_t hcsplt;
    uint32_t hcint;
    uint32_t hcintmsk;
    uint32_t hctsiz;
    uint32_t hcdma;
    uint32_t reserved;
    uint32_t hcdmab;

    USBPacket packet;
    uint8_t buffer[8192];
} bcm2835_usb_hc_state;

struct bcm2835_usb_state_struct {
    SysBusDevice busdev;
    MemoryRegion iomem;
    /* DMAContext *dma; */
    AddressSpace *dma;

    USBBus bus;
    USBPort port;
    int attached;
    int reset_done;
    QEMUTimer *sof_timer;


    uint32_t gusbcfg;
    uint32_t hptxfsiz;
    uint32_t hcfg;
    uint32_t dcfg;
    uint32_t grxfsiz;
    uint32_t gnptxfsiz;
    uint32_t dtxfsiz[15];
    uint32_t gahbcfg;
    uint32_t grstctl;
    uint32_t gotgctl;
    uint32_t gotgint;
    uint32_t gintsts;
    uint32_t gintmsk;
    uint32_t gdfifocfg;
    uint32_t hprt0;
    uint32_t haint;
    uint32_t haintmsk;
    uint32_t gnptxsts;
    uint32_t hfnum;
    uint32_t hptxsts;

    bcm2835_usb_hc_state hchan[NB_HCHANS];

    qemu_irq irq;

};

static void bcm2835_usb_update_irq(bcm2835_usb_state *s)
{
    int n;

    s->haint = 0;
    for (n = 0; n < NB_HCHANS; n++) {
        if (s->hchan[n].hcint & s->hchan[n].hcintmsk) {
            s->haint |= (1 << n);
        }
    }
    s->gintsts &= ~gintsts_hcintr;
    if (s->haint & s->haintmsk) {
        s->gintsts |= gintsts_hcintr;
    }

    if ((s->hprt0 & hprt0_prtconndet)
        || (s->hprt0 & hprt0_prtenchng)
   ) {
        s->gintsts |= gintsts_portintr;
    } else {
        s->gintsts &= ~gintsts_portintr;
    }

    s->gintsts |= gintsts_nptxfempty | gintsts_ptxfempty;

    if (!(s->gahbcfg & gahbcfg_glblintrmsk)) {
        qemu_set_irq(s->irq, 0);
    } else {
        /*printf("[QEMU] bcm2835_usb_update_irq gintsts=%08x gintmsk=%08x\n",
            s->gintsts, s->gintmsk);*/
        if (s->gintsts & s->gintmsk) {
            qemu_set_irq(s->irq, 1);
        } else {
            qemu_set_irq(s->irq, 0);
        }
    }
}


static void bcm2835_usb_sof_tick(void *opaque)
{
    bcm2835_usb_state *s = (bcm2835_usb_state *)opaque;
    int64_t now;

    uint32_t num = (s->hfnum & 0x3fff) + SOF_INCR;
    s->hfnum = (num & 0x3fff) | (0x3210 << 16);
    s->gintsts |= gintsts_sofintr;

    bcm2835_usb_update_irq(s);

    now = qemu_clock_get_us(QEMU_CLOCK_VIRTUAL);
    timer_mod(s->sof_timer, now + SOF_DELAY);
}

static void channel_enable(bcm2835_usb_hc_state *c)
{
    /* int n; */
    USBEndpoint *ep;
    USBDevice *dev;

    uint32_t epnum = (c->hcchar >> hcchar_epnum_shift) & hcchar_epnum_mask;
    uint32_t devaddr = (c->hcchar >> hcchar_devaddr_shift)
                       & hcchar_devaddr_mask;
    uint32_t xfersize = (c->hctsiz >> hctsiz_xfersize_shift)
                        & hctsiz_xfersize_mask;
    /* uint32_t pktcnt = (c->hctsiz >> hctsiz_pktcnt_shift)
                      & hctsiz_pktcnt_mask; */
    uint32_t pid = (c->hctsiz >> hctsiz_pid_shift) & hctsiz_pid_mask;
    uint32_t dma_addr = c->hcdma; /* ??? */
    /* uint32_t dma_addr_b = c->hcdmab; / * ??? */
    int actual_length;
    int qpid;

    if (!c->parent->reset_done) {
        return;
    }

    /*printf("DEV = %d EPNUM = %d EPDIR = %s PKTCNT = %d XFERSIZE = %d\n",
        devaddr, epnum, (c->hcchar & hcchar_epdir ? "IN" : "OUT"),
        pktcnt, xfersize);*/

    if (c->hcchar & hcchar_epdir) {
        /* IN */
        qpid = USB_TOKEN_IN;
    } else {
        /* OUT/SETUP */
        if (pid == DWC_HCTSIZ_SETUP) {
            qpid = USB_TOKEN_SETUP;
        } else {
            qpid = USB_TOKEN_OUT;
        }
    }
    /* printf("QPID = %02x\n", qpid); */
    dev = usb_find_device(&c->parent->port, devaddr);

    assert(dev != NULL);

    ep = usb_ep_get(dev, qpid, epnum);
    usb_packet_setup(&c->packet, qpid, ep, 0, devaddr, 0, 0);

    if (xfersize > 0) {
        dma_memory_read(c->parent->dma, dma_addr, c->buffer, xfersize);

        /*for (n = 0; n < xfersize; n++) {
            printf("%02x", c->buffer[n]);
        }
        printf("\n\n");*/

        usb_packet_addbuf(&c->packet, c->buffer, xfersize);
    }
    usb_handle_packet(dev, &c->packet);
    /*printf("PACKET STATUS = %d actual_length=%d\n",
        c->packet.status, c->packet.actual_length);*/

    if (c->packet.status == USB_RET_SUCCESS) {
        if (qpid == USB_TOKEN_IN) {
            actual_length = c->packet.actual_length;

            xfersize -= actual_length;
            c->hctsiz &= ~(hctsiz_xfersize_mask << hctsiz_xfersize_shift);
            c->hctsiz |= xfersize << hctsiz_xfersize_shift;

            dma_memory_write(c->parent->dma, dma_addr, c->buffer,
                actual_length);

            /*for (n = 0; n < actual_length; n++) {
                printf("%02x", c->buffer[n]);
            }
            printf("\n\n");*/
        }

        c->hcint |= hcint_xfercomp | hcint_chhltd;
        bcm2835_usb_update_irq(c->parent);
    } else if (c->packet.status == USB_RET_NAK) {
        c->hcint |= hcint_chhltd | hcint_nak;
        bcm2835_usb_update_irq(c->parent);
    } else {
        assert(0);
    }

}

static uint32_t bcm2835_usb_hchan_read(bcm2835_usb_state *s, int ch,
    int offset)
{

    bcm2835_usb_hc_state *c = &s->hchan[ch];
    uint32_t res;
    int log = 0;
    const char *reg = "(unmapped)";

    switch (offset) {
    case 0x0:
        reg = "hcchar";
        res = c->hcchar;
        break;
    case 0x4:
        reg = "hcsplt";
        res = c->hcsplt;
        break;
    case 0x8:
        reg = "hcint";
        res = c->hcint;
        break;
    case 0xc:
        reg = "hcintmsk";
        res = c->hcintmsk;
        break;
    case 0x10:
        reg = "hctsiz";
        res = c->hctsiz;
        break;
    case 0x14:
        reg = "hcdma";
        res = c->hcdma;
        break;
    case 0x1c:
        reg = "hcdmab";
        res = c->hcdmab;
        break;
    default:
        res = 0;
        break;
    }

    if (log > LOG_REG_LEVEL) {
        printf("[QEMU] bcm2835_usb: read_hc[%d](%x) %08x <%s>\n", ch,
            (int)offset, res, reg);
    }

    return res;
}
static void bcm2835_usb_hchan_write(bcm2835_usb_state *s, int ch,
    int offset, uint32_t value, int *pset_irq)
{
    int log = 0;
    bcm2835_usb_hc_state *c = &s->hchan[ch];

    const char *reg = "(unmapped)";

    switch (offset) {
    case 0x0:
        reg = "hcchar";
        c->hcchar = value;
        if (value & hcchar_chdis) {
            c->hcchar &= ~(hcchar_chdis | hcchar_chen);
            /* TODO irq */
        }
        if (value & hcchar_chen) {
            channel_enable(c);
        }
        break;
    case 0x4:
        reg = "hcsplt";
        c->hcsplt = value;
        break;
    case 0x8:
        /* Looks like a standard interrupt register */
        reg = "hcint";
        c->hcint &= ~value;
        *pset_irq = 1;
        break;
    case 0xc:
        reg = "hcintmsk";
        c->hcintmsk = value;
        break;
    case 0x10:
        reg = "hctsiz";
        c->hctsiz = value;
        break;
    case 0x14:
        reg = "hcdma";
        c->hcdma = value;
        break;
    case 0x1c:
        reg = "hcdmab";
        c->hcdmab = value;
        break;
    default:
        break;
    }

    if (log > LOG_REG_LEVEL) {
        printf("[QEMU] bcm2835_usb: write_hc[%d](%x) %08x >%s<\n", ch,
            (int)offset, value, reg);
    }

}

static uint64_t bcm2835_usb_read(void *opaque, hwaddr offset,
    unsigned size)
{
    bcm2835_usb_state *s = (bcm2835_usb_state *)opaque;
    uint32_t res = 0;

    const char *reg = "(unmapped)";
    int log = 1;

    int i;

    assert(size == 4);

    switch (offset) {
    case 0x0:
        reg = "gotgctl";
        res = s->gotgctl;
        break;
    case 0x4:
        reg = "gotgint";
        res = s->gotgint;
        break;
    case 0x8:
        reg = "gahbcfg";
        res = s->gahbcfg;
        break;
    case 0xc:
        reg = "gusbcfg";
        res = s->gusbcfg;
        break;
    case 0x10:
        reg = "grstctl";
        res = s->grstctl;
        break;
    case 0x14:
        reg = "gintsts";
        res = s->gintsts;
        /* Enforce Host mode */
        res |= gintsts_curmode;
        break;
    case 0x18:
        reg = "gintmsk";
        res = s->gintmsk;
        break;
    case 0x24:
        reg = "grxfsiz";
        res = s->grxfsiz;
        break;
    case 0x28:
        reg = "gnptxfsiz";
        res = s->gnptxfsiz;
        break;
    case 0x2c:
        reg = "gnptxsts";
        res = s->gnptxsts;
        break;
    case 0x40:
        reg = "gsnpsid";
        res = 0x4f54280a;
        break;
    case 0x44:
        reg = "ghwcfg1";
        res = 0;
        break;
    case 0x48:
        reg = "ghwcfg2";
        res = 0x228ddd50;
        break;
    case 0x4c:
        reg = "ghwcfg3";
        res = 0x0ff000e8;
        break;
    case 0x50:
        reg = "ghwcfg4";
        res = 0x1ff00020;
        break;
    case 0x5c:
        reg = "gdfifocfg";
        res = s->gdfifocfg;
        break;
    case 0x100:
        reg = "hptxfsiz";
        res = s->hptxfsiz;
        break;
    case 0x400:
        reg = "hcfg";
        res = s->hcfg;
        break;
    case 0x408:
        reg = "hfnum";
        res = s->hfnum;
        break;
    case 0x410:
        reg = "hptxsts";
        res = s->hptxsts;
        break;
    case 0x414:
        reg = "haint";
        res = s->haint;
        break;
    case 0x418:
        reg = "haintmsk";
        res = s->haintmsk;
        break;
    case 0x440:
        reg = "hprt0";
        res = s->hprt0;
        res &= ~hprt0_prtconnsts;
        if (s->attached) {
            res |= hprt0_prtconnsts;
        }
        break;
    case 0x800:
        reg = "dcfg";
        res = s->dcfg;
        break;

    case 0xe00:
    case 0x54:
    case 0x58:
        reg = "(power-related)";
        res = 0;
        break;

    default:
        if ((offset >= 0x104) && (offset < 0x104 + (15 << 2))) {
            reg = "dtxfsiz[0..14]";
            res = s->dtxfsiz[(offset - 0x104) >> 2];
        } else if ((offset >= 0x500) && (offset < 0x500 + 0x20*NB_HCHANS)) {
            i = (offset - 0x500) >> 5;
            res = bcm2835_usb_hchan_read(s, i, offset & 0x1f);
            log = 0;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                "bcm2835_usb_read: Bad offset %x\n", (int)offset);
            res = 0;
        }
        break;
    }

    if (log > LOG_REG_LEVEL) {
        printf("[QEMU] bcm2835_usb: read(%x) %08x <%s>\n", (int)offset,
            res, reg);
    }

    return res;
}

static void bcm2835_usb_write(void *opaque, hwaddr offset,
    uint64_t value, unsigned size)
{
    bcm2835_usb_state *s = (bcm2835_usb_state *)opaque;

    const char *reg = "(unmapped)";
    int log = 1;

    int i;
    int set_irq = 0;

    assert(size == 4);

    switch (offset) {
    case 0x0:
        reg = "gotgctl";
        s->gotgctl = value;
        break;
    case 0x4:
        /* Looks like a standard interrupt register */
        reg = "gotgint";
        s->gotgint &= ~value;
        break;
    case 0x8:
        reg = "gahbcfg";
        s->gahbcfg = value;
        set_irq = 1;
        break;
    case 0xc:
        reg = "gusbcfg";
        s->gusbcfg = value;
        break;
    case 0x10:
        reg = "grstctl";
        s->grstctl &= ~0x7c0;
        s->grstctl |= value & 0x7c0;
        break;
    case 0x14:
        reg = "gintsts";
        /*if (value & gintsts_sofintr)
            s->gintsts &= ~gintsts_sofintr;*/
        s->gintsts &= ~value;
        /* Enforce Host mode */
        s->gintsts |= gintsts_curmode;
        set_irq = 1;
        break;
    case 0x18:
        reg = "gintmsk";
        s->gintmsk = value;
        break;
    case 0x24:
        reg = "grxfsiz";
        s->grxfsiz = value;
        break;
    case 0x28:
        reg = "gnptxfsiz";
        s->gnptxfsiz = value;
        break;
    case 0x5c:
        reg = "gdfifocfg";
        s->gdfifocfg = value;
        break;
    case 0x100:
        reg = "hptxfsiz";
        s->hptxfsiz = value;
        break;
    case 0x400:
        reg = "hcfg";
        s->hcfg = value;
        break;
    case 0x408:
        reg = "hfnum";
        /* Probably RO */
        break;
    case 0x410:
        reg = "hptxsts";
        /* Probably RO */
        break;
    case 0x414:
        reg = "haint";
        /* Probably RO */
        break;
    case 0x418:
        reg = "haintmsk";
        s->haintmsk = value & ((1 << NB_HCHANS) - 1);
        set_irq = 1;
        break;
    case 0x440:
        reg = "hprt0";
        if (!(s->hprt0 & hprt0_prtpwr) && (value & hprt0_prtpwr)) {
            /* Trigger the port status change interrupt on power on */
            if (s->attached) {
                s->hprt0 |= hprt0_prtconndet;
                set_irq = 1;
                /* Reset the device (that's probably not the right place) */
                usb_device_reset(s->port.dev);
                s->reset_done = 1;
                timer_mod(s->sof_timer, 0);
            }
        }
        s->hprt0 &= ~hprt0_prtpwr;
        s->hprt0 |= value & hprt0_prtpwr;

        if ((s->hprt0 & hprt0_prtres) ^ (value & hprt0_prtres)) {
            s->hprt0 |= hprt0_prtenchng;
            set_irq = 1;
        }
        s->hprt0 &= ~(hprt0_prtena | hprt0_prtres);
        if (value & hprt0_prtres) {
            s->hprt0 |= hprt0_prtres;
        } else {
            s->hprt0 |= hprt0_prtena;
        }

        /* Interrupt clears */
        if (value & hprt0_prtconndet) {
            s->hprt0 &= ~hprt0_prtconndet;
            set_irq = 1;
        }
        if (value & hprt0_prtenchng) {
            s->hprt0 &= ~hprt0_prtenchng;
            set_irq = 1;
        }

        break;

    case 0xe00:
    case 0x54:
    case 0x58:
        reg = "(power-related)";
        break;

    default:
        if ((offset >= 0x104) && (offset < 0x104 + (15 << 2))) {
            reg = "dtxfsiz[0..14]";
            s->dtxfsiz[(offset - 0x104) >> 2] = value;
        } else if ((offset >= 0x500) && (offset < 0x500 + 0x20*NB_HCHANS)) {
            i = (offset - 0x500) >> 5;
            bcm2835_usb_hchan_write(s, i, offset & 0x1f, value, &set_irq);
            log = 0;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                "bcm2835_usb_write: Bad offset %x\n", (int)offset);
        }
        break;
    }

    if (log > LOG_REG_LEVEL) {
        printf("[QEMU] bcm2835_usb: write(%x) %08x >%s<\n", (int)offset,
            (uint32_t)value, reg);
    }
    if (set_irq) {
        bcm2835_usb_update_irq(s);
    }
}

static void bcm2835_usb_attach(USBPort *port1)
{
    bcm2835_usb_state *s = port1->opaque;
    s->attached = 1;
}
static void bcm2835_usb_detach(USBPort *port1)
{
}
static void bcm2835_usb_child_detach(USBPort *port1, USBDevice *child)
{
}
static void bcm2835_usb_wakeup(USBPort *port1)
{
}
static void bcm2835_usb_async_complete(USBPort *port, USBPacket *packet)
{
    printf("******************* ASYNC COMPLETE\n");
}


static const MemoryRegionOps bcm2835_usb_ops = {
    .read = bcm2835_usb_read,
    .write = bcm2835_usb_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_bcm2835_usb = {
    .name = TYPE_BCM2835_USB,
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static USBPortOps bcm2835_usb_port_ops = {
    .attach = bcm2835_usb_attach,
    .detach = bcm2835_usb_detach,
    .child_detach = bcm2835_usb_child_detach,
    .wakeup = bcm2835_usb_wakeup,
    .complete = bcm2835_usb_async_complete,
};

static USBBusOps bcm2835_usb_bus_ops = {
};

static int bcm2835_usb_init(SysBusDevice *sbd)
{
    /* bcm2835_usb_state *s = FROM_SYSBUS(bcm2835_usb_state, dev); */
    int n;
    DeviceState *dev = DEVICE(sbd);
    bcm2835_usb_state *s = BCM2835_USB(dev);

    /* s->dma = &dma_context_memory; */
    s->dma = &address_space_memory;

    s->gusbcfg = 0x20402700;
    s->hptxfsiz = 0x02002000;
    s->hcfg = 0x00000001;
    s->dcfg = 0x00000000;
    s->grxfsiz = 0x00001000;
    s->gnptxfsiz = 0x01001000;
    for (n = 0; n < 15; n++) {
        s->dtxfsiz[n] = 0x02002000;
    }
    s->gahbcfg = 0x0000000e;
    s->grstctl = 0x80000000;
    s->gotgctl = 0x001c0000;
    s->gotgint = 0;
    s->gintsts = 0;
    s->gintmsk = 0;
    s->gdfifocfg = 0x00000000;
    /* s->hprt0 = 0x00000400; */
    s->hprt0 = DWC_HPRT0_PRTSPD_FULL_SPEED << hprt0_prtspd_shift;
    s->gnptxsts = 0x080100;
    s->hfnum = 0;
    s->hptxsts = 0x080200;

    for (n = 0; n < NB_HCHANS; n++) {
        s->hchan[n].parent = s;
        s->hchan[n].index = n;

        s->hchan[n].hcchar = 0;
        s->hchan[n].hcsplt = 0;
        s->hchan[n].hcint = 0;
        s->hchan[n].hcintmsk = 0;
        s->hchan[n].hctsiz = 0;
        s->hchan[n].hcdma = 0;
        s->hchan[n].hcdmab = 0;

        usb_packet_init(&s->hchan[n].packet);
    }

    memory_region_init_io(&s->iomem, OBJECT(s), &bcm2835_usb_ops, s,
        TYPE_BCM2835_USB, 0x20000);
    sysbus_init_mmio(sbd, &s->iomem);
    vmstate_register(dev, -1, &vmstate_bcm2835_usb, s);

    sysbus_init_irq(sbd, &s->irq);

    s->attached = 0;
    s->reset_done = 0;

    s->sof_timer = timer_new_us(QEMU_CLOCK_VIRTUAL, bcm2835_usb_sof_tick, s);

    usb_bus_new(&s->bus, sizeof(s->bus), &bcm2835_usb_bus_ops, dev);
    usb_register_port(&s->bus, &s->port, s, 0, &bcm2835_usb_port_ops,
        USB_SPEED_MASK_LOW | USB_SPEED_MASK_FULL);
    return 0;
}

static void bcm2835_usb_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = bcm2835_usb_init;
}

static TypeInfo bcm2835_usb_info = {
    .name          = TYPE_BCM2835_USB,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(bcm2835_usb_state),
    .class_init    = bcm2835_usb_class_init,
};

static void bcm2835_usb_register_types(void)
{
    type_register_static(&bcm2835_usb_info);
}

type_init(bcm2835_usb_register_types)
