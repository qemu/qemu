/*
 * mmio_interface.c
 *
 *  Copyright (C) 2017 : GreenSocs
 *      http://www.greensocs.com/ , email: info@greensocs.com
 *
 *  Developed by :
 *  Frederic Konrad   <fred.konrad@greensocs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option)any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "trace.h"
#include "hw/qdev-properties.h"
#include "hw/misc/mmio_interface.h"
#include "qapi/error.h"

#ifndef DEBUG_MMIO_INTERFACE
#define DEBUG_MMIO_INTERFACE 0
#endif

static uint64_t mmio_interface_counter;

#define DPRINTF(fmt, ...) do {                                                 \
    if (DEBUG_MMIO_INTERFACE) {                                                \
        qemu_log("mmio_interface: 0x%" PRIX64 ": " fmt, s->id, ## __VA_ARGS__);\
    }                                                                          \
} while (0);

static void mmio_interface_init(Object *obj)
{
    MMIOInterface *s = MMIO_INTERFACE(obj);

    if (DEBUG_MMIO_INTERFACE) {
        s->id = mmio_interface_counter++;
    }

    DPRINTF("interface created\n");
    s->host_ptr = 0;
    s->subregion = 0;
}

static void mmio_interface_realize(DeviceState *dev, Error **errp)
{
    MMIOInterface *s = MMIO_INTERFACE(dev);

    DPRINTF("realize from 0x%" PRIX64 " to 0x%" PRIX64 " map host pointer"
            " %p\n", s->start, s->end, s->host_ptr);

    if (!s->host_ptr) {
        error_setg(errp, "host_ptr property must be set");
        return;
    }

    if (!s->subregion) {
        error_setg(errp, "subregion property must be set");
        return;
    }

    memory_region_init_ram_ptr(&s->ram_mem, OBJECT(s), "ram",
                               s->end - s->start + 1, s->host_ptr);
    memory_region_set_readonly(&s->ram_mem, s->ro);
    memory_region_add_subregion(s->subregion, s->start, &s->ram_mem);
}

static void mmio_interface_unrealize(DeviceState *dev, Error **errp)
{
    MMIOInterface *s = MMIO_INTERFACE(dev);

    DPRINTF("unrealize from 0x%" PRIX64 " to 0x%" PRIX64 " map host pointer"
            " %p\n", s->start, s->end, s->host_ptr);
    memory_region_del_subregion(s->subregion, &s->ram_mem);
}

static void mmio_interface_finalize(Object *obj)
{
    MMIOInterface *s = MMIO_INTERFACE(obj);

    DPRINTF("finalize from 0x%" PRIX64 " to 0x%" PRIX64 " map host pointer"
            " %p\n", s->start, s->end, s->host_ptr);
    object_unparent(OBJECT(&s->ram_mem));
}

static Property mmio_interface_properties[] = {
    DEFINE_PROP_UINT64("start", MMIOInterface, start, 0),
    DEFINE_PROP_UINT64("end", MMIOInterface, end, 0),
    DEFINE_PROP_PTR("host_ptr", MMIOInterface, host_ptr),
    DEFINE_PROP_BOOL("ro", MMIOInterface, ro, false),
    DEFINE_PROP_MEMORY_REGION("subregion", MMIOInterface, subregion),
    DEFINE_PROP_END_OF_LIST(),
};

static void mmio_interface_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = mmio_interface_realize;
    dc->unrealize = mmio_interface_unrealize;
    dc->props = mmio_interface_properties;
    /* Reason: pointer property "host_ptr", and this device
     * is an implementation detail of the memory subsystem,
     * not intended to be created directly by the user.
     */
    dc->user_creatable = false;
}

static const TypeInfo mmio_interface_info = {
    .name          = TYPE_MMIO_INTERFACE,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(MMIOInterface),
    .instance_init = mmio_interface_init,
    .instance_finalize = mmio_interface_finalize,
    .class_init    = mmio_interface_class_init,
};

static void mmio_interface_register_types(void)
{
    type_register_static(&mmio_interface_info);
}

type_init(mmio_interface_register_types)
