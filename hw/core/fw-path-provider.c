/*
 *  Firmware path provider class and helpers.
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
#include "hw/fw-path-provider.h"
#include "qemu/module.h"

char *fw_path_provider_get_dev_path(FWPathProvider *p, BusState *bus,
                                    DeviceState *dev)
{
    FWPathProviderClass *k = FW_PATH_PROVIDER_GET_CLASS(p);

    return k->get_dev_path(p, bus, dev);
}

char *fw_path_provider_try_get_dev_path(Object *o, BusState *bus,
                                        DeviceState *dev)
{
    FWPathProvider *p = (FWPathProvider *)
        object_dynamic_cast(o, TYPE_FW_PATH_PROVIDER);

    if (p) {
        return fw_path_provider_get_dev_path(p, bus, dev);
    }

    return NULL;
}

static const TypeInfo fw_path_provider_info = {
    .name          = TYPE_FW_PATH_PROVIDER,
    .parent        = TYPE_INTERFACE,
    .class_size    = sizeof(FWPathProviderClass),
};

static void fw_path_provider_register_types(void)
{
    type_register_static(&fw_path_provider_info);
}

type_init(fw_path_provider_register_types)
