/*
 * USB xHCI controller with PCI bus emulation
 *
 * SPDX-FileCopyrightText: 2011 Securiforest
 * SPDX-FileContributor: Hector Martin <hector@marcansoft.com>
 * SPDX-sourceInfo: Based on usb-ohci.c, emulates Renesas NEC USB 3.0
 * SPDX-FileCopyrightText: 2020 Xilinx
 * SPDX-FileContributor: Sai Pavan Boddu <sai.pavan.boddu@xilinx.com>
 * SPDX-sourceInfo: Moved the pci specific content for hcd-xhci.c to
 *                  hcd-xhci-pci.c
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hcd-xhci-pci.h"
#include "trace.h"
#include "qapi/error.h"

#define OFF_MSIX_TABLE  0x3000
#define OFF_MSIX_PBA    0x3800

static void xhci_pci_intr_update(XHCIState *xhci, int n, bool enable)
{
    XHCIPciState *s = container_of(xhci, XHCIPciState, xhci);
    PCIDevice *pci_dev = PCI_DEVICE(s);

    if (!msix_enabled(pci_dev)) {
        return;
    }
    if (enable == !!xhci->intr[n].msix_used) {
        return;
    }
    if (enable) {
        trace_usb_xhci_irq_msix_use(n);
        msix_vector_use(pci_dev, n);
        xhci->intr[n].msix_used = true;
    } else {
        trace_usb_xhci_irq_msix_unuse(n);
        msix_vector_unuse(pci_dev, n);
        xhci->intr[n].msix_used = false;
    }
}

static bool xhci_pci_intr_raise(XHCIState *xhci, int n, bool level)
{
    XHCIPciState *s = container_of(xhci, XHCIPciState, xhci);
    PCIDevice *pci_dev = PCI_DEVICE(s);

    if (n == 0 &&
        !(msix_enabled(pci_dev) ||
         msi_enabled(pci_dev))) {
        pci_set_irq(pci_dev, level);
    }

    if (msix_enabled(pci_dev) && level) {
        msix_notify(pci_dev, n);
        return true;
    }

    if (msi_enabled(pci_dev) && level) {
        msi_notify(pci_dev, n);
        return true;
    }

    return false;
}

static void xhci_pci_reset(DeviceState *dev)
{
    XHCIPciState *s = XHCI_PCI(dev);

    device_legacy_reset(DEVICE(&s->xhci));
}

static int xhci_pci_vmstate_post_load(void *opaque, int version_id)
{
    XHCIPciState *s = XHCI_PCI(opaque);
    PCIDevice *pci_dev = PCI_DEVICE(s);
    int intr;

   for (intr = 0; intr < s->xhci.numintrs; intr++) {
        if (s->xhci.intr[intr].msix_used) {
            msix_vector_use(pci_dev, intr);
        } else {
            msix_vector_unuse(pci_dev, intr);
        }
    }
   return 0;
}

static void usb_xhci_pci_realize(struct PCIDevice *dev, Error **errp)
{
    int ret;
    Error *err = NULL;
    XHCIPciState *s = XHCI_PCI(dev);

    dev->config[PCI_CLASS_PROG] = 0x30;    /* xHCI */
    dev->config[PCI_INTERRUPT_PIN] = 0x01; /* interrupt pin 1 */
    dev->config[PCI_CACHE_LINE_SIZE] = 0x10;
    dev->config[0x60] = 0x30; /* release number */

    object_property_set_link(OBJECT(&s->xhci), "host", OBJECT(s), NULL);
    s->xhci.intr_update = xhci_pci_intr_update;
    s->xhci.intr_raise = xhci_pci_intr_raise;
    if (!qdev_realize(DEVICE(&s->xhci), NULL, errp)) {
        return;
    }
    if (strcmp(object_get_typename(OBJECT(dev)), TYPE_NEC_XHCI) == 0) {
        s->xhci.nec_quirks = true;
    }

    if (s->msi != ON_OFF_AUTO_OFF) {
        ret = msi_init(dev, 0x70, s->xhci.numintrs, true, false, &err);
        /*
         * Any error other than -ENOTSUP(board's MSI support is broken)
         * is a programming error
         */
        assert(!ret || ret == -ENOTSUP);
        if (ret && s->msi == ON_OFF_AUTO_ON) {
            /* Can't satisfy user's explicit msi=on request, fail */
            error_append_hint(&err, "You have to use msi=auto (default) or "
                    "msi=off with this machine type.\n");
            error_propagate(errp, err);
            return;
        }
        assert(!err || s->msi == ON_OFF_AUTO_AUTO);
        /* With msi=auto, we fall back to MSI off silently */
        error_free(err);
    }
    pci_register_bar(dev, 0,
                     PCI_BASE_ADDRESS_SPACE_MEMORY |
                     PCI_BASE_ADDRESS_MEM_TYPE_64,
                     &s->xhci.mem);

    if (pci_bus_is_express(pci_get_bus(dev)) ||
        xhci_get_flag(&s->xhci, XHCI_FLAG_FORCE_PCIE_ENDCAP)) {
        ret = pcie_endpoint_cap_init(dev, 0xa0);
        assert(ret > 0);
    }

    if (s->msix != ON_OFF_AUTO_OFF) {
        /* TODO check for errors, and should fail when msix=on */
        msix_init(dev, s->xhci.numintrs,
                  &s->xhci.mem, 0, OFF_MSIX_TABLE,
                  &s->xhci.mem, 0, OFF_MSIX_PBA,
                  0x90, NULL);
    }
    s->xhci.as = pci_get_address_space(dev);
}

static void usb_xhci_pci_exit(PCIDevice *dev)
{
    XHCIPciState *s = XHCI_PCI(dev);
    /* destroy msix memory region */
    if (dev->msix_table && dev->msix_pba
        && dev->msix_entry_used) {
        msix_uninit(dev, &s->xhci.mem, &s->xhci.mem);
    }
}

static const VMStateDescription vmstate_xhci_pci = {
    .name = "xhci",
    .version_id = 1,
    .post_load = xhci_pci_vmstate_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, XHCIPciState),
        VMSTATE_MSIX(parent_obj, XHCIPciState),
        VMSTATE_STRUCT(xhci, XHCIPciState, 1, vmstate_xhci, XHCIState),
        VMSTATE_END_OF_LIST()
    }
};

static void xhci_instance_init(Object *obj)
{
    XHCIPciState *s = XHCI_PCI(obj);
    /*
     * QEMU_PCI_CAP_EXPRESS initialization does not depend on QEMU command
     * line, therefore, no need to wait to realize like other devices
     */
    PCI_DEVICE(obj)->cap_present |= QEMU_PCI_CAP_EXPRESS;
    object_initialize_child(obj, "xhci-core", &s->xhci, TYPE_XHCI);
    qdev_alias_all_properties(DEVICE(&s->xhci), obj);
}

static void xhci_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset   = xhci_pci_reset;
    dc->vmsd    = &vmstate_xhci_pci;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
    k->realize      = usb_xhci_pci_realize;
    k->exit         = usb_xhci_pci_exit;
    k->class_id     = PCI_CLASS_SERIAL_USB;
}

static const TypeInfo xhci_pci_info = {
    .name          = TYPE_XHCI_PCI,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(XHCIPciState),
    .class_init    = xhci_class_init,
    .instance_init = xhci_instance_init,
    .abstract      = true,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { }
    },
};

static void qemu_xhci_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->vendor_id    = PCI_VENDOR_ID_REDHAT;
    k->device_id    = PCI_DEVICE_ID_REDHAT_XHCI;
    k->revision     = 0x01;
}

static void qemu_xhci_instance_init(Object *obj)
{
    XHCIPciState *s = XHCI_PCI(obj);
    XHCIState *xhci = &s->xhci;

    s->msi      = ON_OFF_AUTO_OFF;
    s->msix     = ON_OFF_AUTO_AUTO;
    xhci->numintrs = XHCI_MAXINTRS;
    xhci->numslots = XHCI_MAXSLOTS;
    xhci_set_flag(xhci, XHCI_FLAG_SS_FIRST);
}

static const TypeInfo qemu_xhci_info = {
    .name          = TYPE_QEMU_XHCI,
    .parent        = TYPE_XHCI_PCI,
    .class_init    = qemu_xhci_class_init,
    .instance_init = qemu_xhci_instance_init,
};

static void xhci_register_types(void)
{
    type_register_static(&xhci_pci_info);
    type_register_static(&qemu_xhci_info);
}

type_init(xhci_register_types)
