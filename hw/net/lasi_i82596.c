/*
 * QEMU LASI NIC i82596 emulation
 *
 * Copyright (c) 2019 Helge Deller <deller@gmx.de>
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 *
 * On PA-RISC, this is the Network part of LASI chip.
 * See:
 * https://parisc.wiki.kernel.org/images-parisc/7/79/Lasi_ers.pdf
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "sysemu/sysemu.h"
#include "net/eth.h"
#include "hw/net/lasi_82596.h"
#include "hw/net/i82596.h"
#include "trace.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#define PA_I82596_RESET         0       /* Offsets relative to LASI-LAN-Addr.*/
#define PA_CPU_PORT_L_ACCESS    4
#define PA_CHANNEL_ATTENTION    8
#define PA_GET_MACADDR          12

#define SWAP32(x)   (((uint32_t)(x) << 16) | ((((uint32_t)(x))) >> 16))

static void lasi_82596_mem_write(void *opaque, hwaddr addr,
                            uint64_t val, unsigned size)
{
    SysBusI82596State *d = opaque;

    trace_lasi_82596_mem_writew(addr, val);
    switch (addr) {
    case PA_I82596_RESET:
        i82596_h_reset(&d->state);
        break;
    case PA_CPU_PORT_L_ACCESS:
        d->val_index++;
        if (d->val_index == 0) {
            uint32_t v = d->last_val | (val << 16);
            v = v & ~0xff;
            i82596_ioport_writew(&d->state, d->last_val & 0xff, v);
        }
        d->last_val = val;
        break;
    case PA_CHANNEL_ATTENTION:
        i82596_ioport_writew(&d->state, PORT_CA, val);
        break;
    case PA_GET_MACADDR:
        /*
         * Provided for SeaBIOS only. Write MAC of Network card to addr @val.
         * Needed for the PDC_LAN_STATION_ID_READ PDC call.
         */
        address_space_write(&address_space_memory, val,
                            MEMTXATTRS_UNSPECIFIED, d->state.conf.macaddr.a,
                            ETH_ALEN);
        break;
    }
}

static uint64_t lasi_82596_mem_read(void *opaque, hwaddr addr,
                               unsigned size)
{
    SysBusI82596State *d = opaque;
    uint32_t val;

    if (addr == PA_GET_MACADDR) {
        val = 0xBEEFBABE;
    } else {
        val = i82596_ioport_readw(&d->state, addr);
    }
    trace_lasi_82596_mem_readw(addr, val);
    return val;
}

static const MemoryRegionOps lasi_82596_mem_ops = {
    .read = lasi_82596_mem_read,
    .write = lasi_82596_mem_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static NetClientInfo net_lasi_82596_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = i82596_can_receive,
    .receive = i82596_receive,
    .link_status_changed = i82596_set_link_status,
};

static const VMStateDescription vmstate_lasi_82596 = {
    .name = "i82596",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(state, SysBusI82596State, 0, vmstate_i82596,
                I82596State),
        VMSTATE_END_OF_LIST()
    }
};

static void lasi_82596_realize(DeviceState *dev, Error **errp)
{
    SysBusI82596State *d = SYSBUS_I82596(dev);
    I82596State *s = &d->state;

    memory_region_init_io(&s->mmio, OBJECT(d), &lasi_82596_mem_ops, d,
                "lasi_82596-mmio", PA_GET_MACADDR + 4);

    i82596_common_init(dev, s, &net_lasi_82596_info);
}

SysBusI82596State *lasi_82596_init(MemoryRegion *addr_space, hwaddr hpa,
                                   qemu_irq lan_irq, gboolean match_default)
{
    DeviceState *dev;
    SysBusI82596State *s;
    static const MACAddr HP_MAC = {
        .a = { 0x08, 0x00, 0x09, 0xef, 0x34, 0xf6 } };

    dev = qemu_create_nic_device(TYPE_LASI_82596, match_default, "lasi");
    if (!dev) {
        return NULL;
    }

    s = SYSBUS_I82596(dev);
    s->state.irq = lan_irq;
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    s->state.conf.macaddr = HP_MAC; /* set HP MAC prefix */

    /* LASI 82596 ports in main memory. */
    memory_region_add_subregion(addr_space, hpa, &s->state.mmio);
    return s;
}

static void lasi_82596_reset(DeviceState *dev)
{
    SysBusI82596State *d = SYSBUS_I82596(dev);

    i82596_h_reset(&d->state);
}

static void lasi_82596_instance_init(Object *obj)
{
    SysBusI82596State *d = SYSBUS_I82596(obj);
    I82596State *s = &d->state;

    device_add_bootindex_property(obj, &s->conf.bootindex,
                                  "bootindex", "/ethernet-phy@0",
                                  DEVICE(obj));
}

static Property lasi_82596_properties[] = {
    DEFINE_NIC_PROPERTIES(SysBusI82596State, state.conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void lasi_82596_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = lasi_82596_realize;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    dc->fw_name = "ethernet";
    dc->reset = lasi_82596_reset;
    dc->vmsd = &vmstate_lasi_82596;
    dc->user_creatable = false;
    device_class_set_props(dc, lasi_82596_properties);
}

static const TypeInfo lasi_82596_info = {
    .name          = TYPE_LASI_82596,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SysBusI82596State),
    .class_init    = lasi_82596_class_init,
    .instance_init = lasi_82596_instance_init,
};

static void lasi_82596_register_types(void)
{
    type_register_static(&lasi_82596_info);
}

type_init(lasi_82596_register_types)
