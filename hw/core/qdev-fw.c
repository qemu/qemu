/*
 *  qdev fw helpers
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License,
 *  or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/qdev.h"
#include "hw/fw-path-provider.h"

const char *qdev_fw_name(DeviceState *dev)
{
    DeviceClass *dc = DEVICE_GET_CLASS(dev);

    if (dc->fw_name) {
        return dc->fw_name;
    }

    return object_get_typename(OBJECT(dev));
}

static char *bus_get_fw_dev_path(BusState *bus, DeviceState *dev)
{
    BusClass *bc = BUS_GET_CLASS(bus);

    if (bc->get_fw_dev_path) {
        return bc->get_fw_dev_path(dev);
    }

    return NULL;
}

static char *qdev_get_fw_dev_path_from_handler(BusState *bus, DeviceState *dev)
{
    Object *obj = OBJECT(dev);
    char *d = NULL;

    while (!d && obj->parent) {
        obj = obj->parent;
        d = fw_path_provider_try_get_dev_path(obj, bus, dev);
    }
    return d;
}

char *qdev_get_own_fw_dev_path_from_handler(BusState *bus, DeviceState *dev)
{
    Object *obj = OBJECT(dev);

    return fw_path_provider_try_get_dev_path(obj, bus, dev);
}

static int qdev_get_fw_dev_path_helper(DeviceState *dev, char *p, int size)
{
    int l = 0;

    if (dev && dev->parent_bus) {
        char *d;
        l = qdev_get_fw_dev_path_helper(dev->parent_bus->parent, p, size);
        d = qdev_get_fw_dev_path_from_handler(dev->parent_bus, dev);
        if (!d) {
            d = bus_get_fw_dev_path(dev->parent_bus, dev);
        }
        if (d) {
            l += snprintf(p + l, size - l, "%s", d);
            g_free(d);
        } else {
            return l;
        }
    }
    l += snprintf(p + l , size - l, "/");

    return l;
}

char *qdev_get_fw_dev_path(DeviceState *dev)
{
    char path[128];
    int l;

    l = qdev_get_fw_dev_path_helper(dev, path, 128);

    path[l - 1] = '\0';

    return g_strdup(path);
}
