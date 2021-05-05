/*
 * Arm PrimeCell PL080/PL081 DMA controller
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/dma/pl080.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"

#define PL080_CONF_E    0x1
#define PL080_CONF_M1   0x2
#define PL080_CONF_M2   0x4

#define PL080_CCONF_H   0x40000
#define PL080_CCONF_A   0x20000
#define PL080_CCONF_L   0x10000
#define PL080_CCONF_ITC 0x08000
#define PL080_CCONF_IE  0x04000
#define PL080_CCONF_E   0x00001

#define PL080_CCTRL_I   0x80000000
#define PL080_CCTRL_DI  0x08000000
#define PL080_CCTRL_SI  0x04000000
#define PL080_CCTRL_D   0x02000000
#define PL080_CCTRL_S   0x01000000

static const VMStateDescription vmstate_pl080_channel = {
    .name = "pl080_channel",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(src, pl080_channel),
        VMSTATE_UINT32(dest, pl080_channel),
        VMSTATE_UINT32(lli, pl080_channel),
        VMSTATE_UINT32(ctrl, pl080_channel),
        VMSTATE_UINT32(conf, pl080_channel),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_pl080 = {
    .name = "pl080",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(tc_int, PL080State),
        VMSTATE_UINT8(tc_mask, PL080State),
        VMSTATE_UINT8(err_int, PL080State),
        VMSTATE_UINT8(err_mask, PL080State),
        VMSTATE_UINT32(conf, PL080State),
        VMSTATE_UINT32(sync, PL080State),
        VMSTATE_UINT32(req_single, PL080State),
        VMSTATE_UINT32(req_burst, PL080State),
        VMSTATE_UINT8(tc_int, PL080State),
        VMSTATE_UINT8(tc_int, PL080State),
        VMSTATE_UINT8(tc_int, PL080State),
        VMSTATE_STRUCT_ARRAY(chan, PL080State, PL080_MAX_CHANNELS,
                             1, vmstate_pl080_channel, pl080_channel),
        VMSTATE_INT32(running, PL080State),
        VMSTATE_END_OF_LIST()
    }
};

static const unsigned char pl080_id[] =
{ 0x80, 0x10, 0x04, 0x0a, 0x0d, 0xf0, 0x05, 0xb1 };

static const unsigned char pl081_id[] =
{ 0x81, 0x10, 0x04, 0x0a, 0x0d, 0xf0, 0x05, 0xb1 };

static void pl080_update(PL080State *s)
{
    bool tclevel = (s->tc_int & s->tc_mask);
    bool errlevel = (s->err_int & s->err_mask);

    qemu_set_irq(s->interr, errlevel);
    qemu_set_irq(s->inttc, tclevel);
    qemu_set_irq(s->irq, errlevel || tclevel);
}

static void pl080_run(PL080State *s)
{
    int c;
    int flow;
    pl080_channel *ch;
    int swidth;
    int dwidth;
    int xsize;
    int n;
    int src_id;
    int dest_id;
    int size;
    uint8_t buff[4];
    uint32_t req;

    s->tc_mask = 0;
    for (c = 0; c < s->nchannels; c++) {
        if (s->chan[c].conf & PL080_CCONF_ITC)
            s->tc_mask |= 1 << c;
        if (s->chan[c].conf & PL080_CCONF_IE)
            s->err_mask |= 1 << c;
    }

    if ((s->conf & PL080_CONF_E) == 0)
        return;

    /* If we are already in the middle of a DMA operation then indicate that
       there may be new DMA requests and return immediately.  */
    if (s->running) {
        s->running++;
        return;
    }
    s->running = 1;
    while (s->running) {
        for (c = 0; c < s->nchannels; c++) {
            ch = &s->chan[c];
again:
            /* Test if thiws channel has any pending DMA requests.  */
            if ((ch->conf & (PL080_CCONF_H | PL080_CCONF_E))
                    != PL080_CCONF_E)
                continue;
            flow = (ch->conf >> 11) & 7;
            if (flow >= 4) {
                hw_error(
                    "pl080_run: Peripheral flow control not implemented\n");
            }
            src_id = (ch->conf >> 1) & 0x1f;
            dest_id = (ch->conf >> 6) & 0x1f;
            size = ch->ctrl & 0xfff;
            req = s->req_single | s->req_burst;
            switch (flow) {
            case 0:
                break;
            case 1:
                if ((req & (1u << dest_id)) == 0)
                    size = 0;
                break;
            case 2:
                if ((req & (1u << src_id)) == 0)
                    size = 0;
                break;
            case 3:
                if ((req & (1u << src_id)) == 0
                        || (req & (1u << dest_id)) == 0)
                    size = 0;
                break;
            }
            if (!size)
                continue;

            /* Transfer one element.  */
            /* ??? Should transfer multiple elements for a burst request.  */
            /* ??? Unclear what the proper behavior is when source and
               destination widths are different.  */
            swidth = 1 << ((ch->ctrl >> 18) & 7);
            dwidth = 1 << ((ch->ctrl >> 21) & 7);
            for (n = 0; n < dwidth; n+= swidth) {
                address_space_read(&s->downstream_as, ch->src,
                                   MEMTXATTRS_UNSPECIFIED, buff + n, swidth);
                if (ch->ctrl & PL080_CCTRL_SI)
                    ch->src += swidth;
            }
            xsize = (dwidth < swidth) ? swidth : dwidth;
            /* ??? This may pad the value incorrectly for dwidth < 32.  */
            for (n = 0; n < xsize; n += dwidth) {
                address_space_write(&s->downstream_as, ch->dest + n,
                                    MEMTXATTRS_UNSPECIFIED, buff + n, dwidth);
                if (ch->ctrl & PL080_CCTRL_DI)
                    ch->dest += swidth;
            }

            size--;
            ch->ctrl = (ch->ctrl & 0xfffff000) | size;
            if (size == 0) {
                /* Transfer complete.  */
                if (ch->lli) {
                    ch->src = address_space_ldl_le(&s->downstream_as,
                                                   ch->lli,
                                                   MEMTXATTRS_UNSPECIFIED,
                                                   NULL);
                    ch->dest = address_space_ldl_le(&s->downstream_as,
                                                    ch->lli + 4,
                                                    MEMTXATTRS_UNSPECIFIED,
                                                    NULL);
                    ch->ctrl = address_space_ldl_le(&s->downstream_as,
                                                    ch->lli + 12,
                                                    MEMTXATTRS_UNSPECIFIED,
                                                    NULL);
                    ch->lli = address_space_ldl_le(&s->downstream_as,
                                                   ch->lli + 8,
                                                   MEMTXATTRS_UNSPECIFIED,
                                                   NULL);
                } else {
                    ch->conf &= ~PL080_CCONF_E;
                }
                if (ch->ctrl & PL080_CCTRL_I) {
                    s->tc_int |= 1 << c;
                }
            }
            goto again;
        }
        if (--s->running)
            s->running = 1;
    }
}

static uint64_t pl080_read(void *opaque, hwaddr offset,
                           unsigned size)
{
    PL080State *s = (PL080State *)opaque;
    uint32_t i;
    uint32_t mask;

    if (offset >= 0xfe0 && offset < 0x1000) {
        if (s->nchannels == 8) {
            return pl080_id[(offset - 0xfe0) >> 2];
        } else {
            return pl081_id[(offset - 0xfe0) >> 2];
        }
    }
    if (offset >= 0x100 && offset < 0x200) {
        i = (offset & 0xe0) >> 5;
        if (i >= s->nchannels)
            goto bad_offset;
        switch ((offset >> 2) & 7) {
        case 0: /* SrcAddr */
            return s->chan[i].src;
        case 1: /* DestAddr */
            return s->chan[i].dest;
        case 2: /* LLI */
            return s->chan[i].lli;
        case 3: /* Control */
            return s->chan[i].ctrl;
        case 4: /* Configuration */
            return s->chan[i].conf;
        default:
            goto bad_offset;
        }
    }
    switch (offset >> 2) {
    case 0: /* IntStatus */
        return (s->tc_int & s->tc_mask) | (s->err_int & s->err_mask);
    case 1: /* IntTCStatus */
        return (s->tc_int & s->tc_mask);
    case 3: /* IntErrorStatus */
        return (s->err_int & s->err_mask);
    case 5: /* RawIntTCStatus */
        return s->tc_int;
    case 6: /* RawIntErrorStatus */
        return s->err_int;
    case 7: /* EnbldChns */
        mask = 0;
        for (i = 0; i < s->nchannels; i++) {
            if (s->chan[i].conf & PL080_CCONF_E)
                mask |= 1 << i;
        }
        return mask;
    case 8: /* SoftBReq */
    case 9: /* SoftSReq */
    case 10: /* SoftLBReq */
    case 11: /* SoftLSReq */
        /* ??? Implement these. */
        return 0;
    case 12: /* Configuration */
        return s->conf;
    case 13: /* Sync */
        return s->sync;
    default:
    bad_offset:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pl080_read: Bad offset %x\n", (int)offset);
        return 0;
    }
}

static void pl080_write(void *opaque, hwaddr offset,
                        uint64_t value, unsigned size)
{
    PL080State *s = (PL080State *)opaque;
    int i;

    if (offset >= 0x100 && offset < 0x200) {
        i = (offset & 0xe0) >> 5;
        if (i >= s->nchannels)
            goto bad_offset;
        switch ((offset >> 2) & 7) {
        case 0: /* SrcAddr */
            s->chan[i].src = value;
            break;
        case 1: /* DestAddr */
            s->chan[i].dest = value;
            break;
        case 2: /* LLI */
            s->chan[i].lli = value;
            break;
        case 3: /* Control */
            s->chan[i].ctrl = value;
            break;
        case 4: /* Configuration */
            s->chan[i].conf = value;
            pl080_run(s);
            break;
        }
        return;
    }
    switch (offset >> 2) {
    case 2: /* IntTCClear */
        s->tc_int &= ~value;
        break;
    case 4: /* IntErrorClear */
        s->err_int &= ~value;
        break;
    case 8: /* SoftBReq */
    case 9: /* SoftSReq */
    case 10: /* SoftLBReq */
    case 11: /* SoftLSReq */
        /* ??? Implement these.  */
        qemu_log_mask(LOG_UNIMP, "pl080_write: Soft DMA not implemented\n");
        break;
    case 12: /* Configuration */
        s->conf = value;
        if (s->conf & (PL080_CONF_M1 | PL080_CONF_M2)) {
            qemu_log_mask(LOG_UNIMP,
                          "pl080_write: Big-endian DMA not implemented\n");
        }
        pl080_run(s);
        break;
    case 13: /* Sync */
        s->sync = value;
        break;
    default:
    bad_offset:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "pl080_write: Bad offset %x\n", (int)offset);
    }
    pl080_update(s);
}

static const MemoryRegionOps pl080_ops = {
    .read = pl080_read,
    .write = pl080_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void pl080_reset(DeviceState *dev)
{
    PL080State *s = PL080(dev);
    int i;

    s->tc_int = 0;
    s->tc_mask = 0;
    s->err_int = 0;
    s->err_mask = 0;
    s->conf = 0;
    s->sync = 0;
    s->req_single = 0;
    s->req_burst = 0;
    s->running = 0;

    for (i = 0; i < s->nchannels; i++) {
        s->chan[i].src = 0;
        s->chan[i].dest = 0;
        s->chan[i].lli = 0;
        s->chan[i].ctrl = 0;
        s->chan[i].conf = 0;
    }
}

static void pl080_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    PL080State *s = PL080(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &pl080_ops, s, "pl080", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    sysbus_init_irq(sbd, &s->interr);
    sysbus_init_irq(sbd, &s->inttc);
    s->nchannels = 8;
}

static void pl080_realize(DeviceState *dev, Error **errp)
{
    PL080State *s = PL080(dev);

    if (!s->downstream) {
        error_setg(errp, "PL080 'downstream' link not set");
        return;
    }

    address_space_init(&s->downstream_as, s->downstream, "pl080-downstream");
}

static void pl081_init(Object *obj)
{
    PL080State *s = PL080(obj);

    s->nchannels = 2;
}

static Property pl080_properties[] = {
    DEFINE_PROP_LINK("downstream", PL080State, downstream,
                     TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_PROP_END_OF_LIST(),
};

static void pl080_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &vmstate_pl080;
    dc->realize = pl080_realize;
    device_class_set_props(dc, pl080_properties);
    dc->reset = pl080_reset;
}

static const TypeInfo pl080_info = {
    .name          = TYPE_PL080,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PL080State),
    .instance_init = pl080_init,
    .class_init    = pl080_class_init,
};

static const TypeInfo pl081_info = {
    .name          = TYPE_PL081,
    .parent        = TYPE_PL080,
    .instance_init = pl081_init,
};

/* The PL080 and PL081 are the same except for the number of channels
   they implement (8 and 2 respectively).  */
static void pl080_register_types(void)
{
    type_register_static(&pl080_info);
    type_register_static(&pl081_info);
}

type_init(pl080_register_types)
