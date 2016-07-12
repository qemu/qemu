/*
 *  Firmware patch provider class and helpers definitions.
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

#ifndef FW_PATH_PROVIDER_H
#define FW_PATH_PROVIDER_H

#include "qemu-common.h"
#include "qom/object.h"

#define TYPE_FW_PATH_PROVIDER "fw-path-provider"

#define FW_PATH_PROVIDER_CLASS(klass) \
     OBJECT_CLASS_CHECK(FWPathProviderClass, (klass), TYPE_FW_PATH_PROVIDER)
#define FW_PATH_PROVIDER_GET_CLASS(obj) \
    OBJECT_GET_CLASS(FWPathProviderClass, (obj), TYPE_FW_PATH_PROVIDER)
#define FW_PATH_PROVIDER(obj) \
     INTERFACE_CHECK(FWPathProvider, (obj), TYPE_FW_PATH_PROVIDER)

typedef struct FWPathProvider {
    Object parent_obj;
} FWPathProvider;

typedef struct FWPathProviderClass {
    InterfaceClass parent_class;

    char *(*get_dev_path)(FWPathProvider *p, BusState *bus, DeviceState *dev);
} FWPathProviderClass;

char *fw_path_provider_get_dev_path(FWPathProvider *p, BusState *bus,
                                    DeviceState *dev);
char *fw_path_provider_try_get_dev_path(Object *o, BusState *bus,
                                        DeviceState *dev);

#endif /* FW_PATH_PROVIDER_H */
