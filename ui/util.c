/*
 * Copyright (C) 2021 Red Hat, Inc.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"

#include "hw/pci/pci_device.h"
#include "hw/pci/pci_bus.h"
#include "qapi/error.h"
#include "ui/console.h"

/*
 * Recursively (in reverse order) appends addresses of PCI devices as it moves
 * up in the PCI hierarchy.
 *
 * @returns true on success, false when the buffer wasn't large enough
 */
static bool append_pci_address(char *buf, size_t buf_size, const PCIDevice *pci)
{
    PCIBus *bus = pci_get_bus(pci);
    /*
     * equivalent to if (!pci_bus_is_root(bus)), but the function is not built
     * with PCI_CONFIG=n, avoid using an #ifdef by checking directly
     */
    if (bus->parent_dev != NULL) {
        append_pci_address(buf, buf_size, bus->parent_dev);
    }

    size_t len = strlen(buf);
    ssize_t written = snprintf(buf + len, buf_size - len, "/%02x.%x",
        PCI_SLOT(pci->devfn), PCI_FUNC(pci->devfn));

    return written > 0 && written < buf_size - len;
}

bool qemu_console_fill_device_address(QemuConsole *con,
                                      char *device_address,
                                      size_t size,
                                      Error **errp)
{
    DeviceState *dev = DEVICE(object_property_get_link(OBJECT(con),
                                                       "device",
                                                       &error_abort));
    PCIDevice *pci = (PCIDevice *) object_dynamic_cast(OBJECT(dev),
                                                       TYPE_PCI_DEVICE);

    if (pci == NULL) {
        error_setg(errp, "Setting device address of a display device: "
                   "Not a PCI device.");
        return false;
    }

    strncpy(device_address, "pci/0000", size);
    if (!append_pci_address(device_address, size, pci)) {
        error_setg(errp, "Setting device address of a display device: "
                   "Too many PCI devices in the chain.");
        return false;
    }

    return true;
}
