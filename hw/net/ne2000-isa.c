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

#include "qemu/osdep.h"
#include "hw/isa/isa.h"
#include "hw/net/ne2000-isa.h"
#include "migration/vmstate.h"
#include "ne2000.h"
#include "sysemu/sysemu.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/module.h"
#include "qom/object.h"

OBJECT_DECLARE_SIMPLE_TYPE(ISANE2000State, ISA_NE2000)

struct ISANE2000State {
    ISADevice parent_obj;

    uint32_t iobase;
    uint32_t isairq;
    NE2000State ne2000;
};

static NetClientInfo net_ne2000_isa_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .receive = ne2000_receive,
};

static const VMStateDescription vmstate_isa_ne2000 = {
    .name = "ne2000",
    .version_id = 2,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(ne2000, ISANE2000State, 0, vmstate_ne2000, NE2000State),
        VMSTATE_END_OF_LIST()
    }
};

static void isa_ne2000_realizefn(DeviceState *dev, Error **errp)
{
    ISADevice *isadev = ISA_DEVICE(dev);
    ISANE2000State *isa = ISA_NE2000(dev);
    NE2000State *s = &isa->ne2000;

    ne2000_setup_io(s, DEVICE(isadev), 0x20);
    isa_register_ioport(isadev, &s->io, isa->iobase);

    isa_init_irq(isadev, &s->irq, isa->isairq);

    qemu_macaddr_default_if_unset(&s->c.macaddr);
    ne2000_reset(s);

    s->nic = qemu_new_nic(&net_ne2000_isa_info, &s->c,
                          object_get_typename(OBJECT(dev)), dev->id, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->c.macaddr.a);
}

static Property ne2000_isa_properties[] = {
    DEFINE_PROP_UINT32("iobase", ISANE2000State, iobase, 0x300),
    DEFINE_PROP_UINT32("irq",   ISANE2000State, isairq, 9),
    DEFINE_NIC_PROPERTIES(ISANE2000State, ne2000.c),
    DEFINE_PROP_END_OF_LIST(),
};

static void isa_ne2000_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = isa_ne2000_realizefn;
    device_class_set_props(dc, ne2000_isa_properties);
    dc->vmsd = &vmstate_isa_ne2000;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static void isa_ne2000_get_bootindex(Object *obj, Visitor *v,
                                     const char *name, void *opaque,
                                     Error **errp)
{
    ISANE2000State *isa = ISA_NE2000(obj);
    NE2000State *s = &isa->ne2000;

    visit_type_int32(v, name, &s->c.bootindex, errp);
}

static void isa_ne2000_set_bootindex(Object *obj, Visitor *v,
                                     const char *name, void *opaque,
                                     Error **errp)
{
    ISANE2000State *isa = ISA_NE2000(obj);
    NE2000State *s = &isa->ne2000;
    int32_t boot_index;
    Error *local_err = NULL;

    if (!visit_type_int32(v, name, &boot_index, errp)) {
        return;
    }
    /* check whether bootindex is present in fw_boot_order list  */
    check_boot_index(boot_index, &local_err);
    if (local_err) {
        goto out;
    }
    /* change bootindex to a new one */
    s->c.bootindex = boot_index;

out:
    error_propagate(errp, local_err);
}

static void isa_ne2000_instance_init(Object *obj)
{
    object_property_add(obj, "bootindex", "int32",
                        isa_ne2000_get_bootindex,
                        isa_ne2000_set_bootindex, NULL, NULL);
    object_property_set_int(obj, "bootindex", -1, NULL);
}
static const TypeInfo ne2000_isa_info = {
    .name          = TYPE_ISA_NE2000,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(ISANE2000State),
    .class_init    = isa_ne2000_class_initfn,
    .instance_init = isa_ne2000_instance_init,
};

static void ne2000_isa_register_types(void)
{
    type_register_static(&ne2000_isa_info);
}

type_init(ne2000_isa_register_types)
