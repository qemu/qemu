/*
 * Copyright (c) 2006 Fabrice Bellard
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
/*
 * QEMU i82801b11 dmi-to-pci Bridge Emulation
 *
 *  Copyright (c) 2009, 2010, 2011
 *                Isaku Yamahata <yamahata at valinux co jp>
 *                VA Linux Systems Japan K.K.
 *  Copyright (C) 2012 Jason Baron <jbaron@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "qemu/osdep.h"
#include "hw/pci/pci_bridge.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "hw/southbridge/ich9.h"

/*****************************************************************************/
/* ICH9 DMI-to-PCI bridge */
#define I82801ba_SSVID_OFFSET   0x50
#define I82801ba_SSVID_SVID     0
#define I82801ba_SSVID_SSID     0

typedef struct I82801b11Bridge {
    /*< private >*/
    PCIBridge parent_obj;
    /*< public >*/
} I82801b11Bridge;

static void i82801b11_bridge_realize(PCIDevice *d, Error **errp)
{
    int rc;

    pci_bridge_initfn(d, TYPE_PCI_BUS);

    rc = pci_bridge_ssvid_init(d, I82801ba_SSVID_OFFSET,
                               I82801ba_SSVID_SVID, I82801ba_SSVID_SSID,
                               errp);
    if (rc < 0) {
        goto err_bridge;
    }
    pci_config_set_prog_interface(d->config, PCI_CLASS_BRIDGE_PCI_INF_SUB);
    return;

err_bridge:
    pci_bridge_exitfn(d);
}

static const VMStateDescription i82801b11_bridge_dev_vmstate = {
    .name = "i82801b11_bridge",
    .priority = MIG_PRI_PCI_BUS,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBridge),
        VMSTATE_END_OF_LIST()
    }
};

static void i82801b11_bridge_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = PCI_DEVICE_ID_INTEL_82801BA_11;
    k->revision = ICH9_D2P_A2_REVISION;
    k->realize = i82801b11_bridge_realize;
    k->config_write = pci_bridge_write_config;
    dc->vmsd = &i82801b11_bridge_dev_vmstate;
    dc->reset = pci_bridge_reset;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
}

static const TypeInfo i82801b11_bridge_info = {
    .name          = "i82801b11-bridge",
    .parent        = TYPE_PCI_BRIDGE,
    .instance_size = sizeof(I82801b11Bridge),
    .class_init    = i82801b11_bridge_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void d2pbr_register(void)
{
    type_register_static(&i82801b11_bridge_info);
}

type_init(d2pbr_register);
