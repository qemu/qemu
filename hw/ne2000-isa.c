/*
 * QEMU NE2000 emulation -- isa bus windup
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
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
#include "pc.h"
#include "isa.h"
#include "qdev.h"
#include "net.h"
#include "ne2000.h"

typedef struct ISANE2000State {
    ISADevice dev;
    uint32_t iobase;
    uint32_t isairq;
    NE2000State ne2000;
} ISANE2000State;

static void isa_ne2000_cleanup(VLANClientState *vc)
{
    NE2000State *s = vc->opaque;
    ISANE2000State *isa = container_of(s, ISANE2000State, ne2000);

    unregister_savevm("ne2000", s);

    isa_unassign_ioport(isa->iobase, 16);
    isa_unassign_ioport(isa->iobase + 0x10, 2);
    isa_unassign_ioport(isa->iobase + 0x1f, 1);
}

static int isa_ne2000_initfn(ISADevice *dev)
{
    ISANE2000State *isa = DO_UPCAST(ISANE2000State, dev, dev);
    NE2000State *s = &isa->ne2000;

    register_ioport_write(isa->iobase, 16, 1, ne2000_ioport_write, s);
    register_ioport_read(isa->iobase, 16, 1, ne2000_ioport_read, s);

    register_ioport_write(isa->iobase + 0x10, 1, 1, ne2000_asic_ioport_write, s);
    register_ioport_read(isa->iobase + 0x10, 1, 1, ne2000_asic_ioport_read, s);
    register_ioport_write(isa->iobase + 0x10, 2, 2, ne2000_asic_ioport_write, s);
    register_ioport_read(isa->iobase + 0x10, 2, 2, ne2000_asic_ioport_read, s);

    register_ioport_write(isa->iobase + 0x1f, 1, 1, ne2000_reset_ioport_write, s);
    register_ioport_read(isa->iobase + 0x1f, 1, 1, ne2000_reset_ioport_read, s);

    isa_init_irq(dev, &s->irq, isa->isairq);

    qdev_get_macaddr(&dev->qdev, s->macaddr);
    ne2000_reset(s);

    s->vc = qdev_get_vlan_client(&dev->qdev,
                                 ne2000_can_receive, ne2000_receive, NULL,
                                 isa_ne2000_cleanup, s);
    qemu_format_nic_info_str(s->vc, s->macaddr);

    register_savevm("ne2000", -1, 2, ne2000_save, ne2000_load, s);
    return 0;
}

void isa_ne2000_init(int base, int irq, NICInfo *nd)
{
    ISADevice *dev;

    qemu_check_nic_model(nd, "ne2k_isa");

    dev = isa_create("ne2k_isa");
    dev->qdev.nd = nd; /* hack alert */
    qdev_prop_set_uint32(&dev->qdev, "iobase", base);
    qdev_prop_set_uint32(&dev->qdev, "irq",    irq);
    qdev_init_nofail(&dev->qdev);
}

static ISADeviceInfo ne2000_isa_info = {
    .qdev.name  = "ne2k_isa",
    .qdev.size  = sizeof(ISANE2000State),
    .init       = isa_ne2000_initfn,
    .qdev.props = (Property[]) {
        DEFINE_PROP_HEX32("iobase", ISANE2000State, iobase, 0x300),
        DEFINE_PROP_UINT32("irq",   ISANE2000State, isairq, 9),
        DEFINE_PROP_END_OF_LIST(),
    },
};

static void ne2000_isa_register_devices(void)
{
    isa_qdev_register(&ne2000_isa_info);
}

device_init(ne2000_isa_register_devices)
