/*
 * QEMU simple authorization driver
 *
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "authz/simple.h"
#include "trace.h"
#include "qemu/module.h"
#include "qom/object_interfaces.h"

static bool qauthz_simple_is_allowed(QAuthZ *authz,
                                     const char *identity,
                                     Error **errp)
{
    QAuthZSimple *sauthz = QAUTHZ_SIMPLE(authz);

    trace_qauthz_simple_is_allowed(authz, sauthz->identity, identity);
    return g_str_equal(identity, sauthz->identity);
}

static void
qauthz_simple_prop_set_identity(Object *obj,
                                const char *value,
                                Error **errp G_GNUC_UNUSED)
{
    QAuthZSimple *sauthz = QAUTHZ_SIMPLE(obj);

    g_free(sauthz->identity);
    sauthz->identity = g_strdup(value);
}


static char *
qauthz_simple_prop_get_identity(Object *obj,
                                Error **errp G_GNUC_UNUSED)
{
    QAuthZSimple *sauthz = QAUTHZ_SIMPLE(obj);

    return g_strdup(sauthz->identity);
}


static void
qauthz_simple_finalize(Object *obj)
{
    QAuthZSimple *sauthz = QAUTHZ_SIMPLE(obj);

    g_free(sauthz->identity);
}


static void
qauthz_simple_class_init(ObjectClass *oc, void *data)
{
    QAuthZClass *authz = QAUTHZ_CLASS(oc);

    authz->is_allowed = qauthz_simple_is_allowed;

    object_class_property_add_str(oc, "identity",
                                  qauthz_simple_prop_get_identity,
                                  qauthz_simple_prop_set_identity,
                                  NULL);
}


QAuthZSimple *qauthz_simple_new(const char *id,
                                const char *identity,
                                Error **errp)
{
    return QAUTHZ_SIMPLE(
        object_new_with_props(TYPE_QAUTHZ_SIMPLE,
                              object_get_objects_root(),
                              id, errp,
                              "identity", identity,
                              NULL));
}


static const TypeInfo qauthz_simple_info = {
    .parent = TYPE_QAUTHZ,
    .name = TYPE_QAUTHZ_SIMPLE,
    .instance_size = sizeof(QAuthZSimple),
    .instance_finalize = qauthz_simple_finalize,
    .class_size = sizeof(QAuthZSimpleClass),
    .class_init = qauthz_simple_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};


static void
qauthz_simple_register_types(void)
{
    type_register_static(&qauthz_simple_info);
}


type_init(qauthz_simple_register_types);
