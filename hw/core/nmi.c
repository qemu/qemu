/*
 *  NMI monitor handler class and helpers.
 *
 *  Copyright IBM Corp., 2014
 *
 *  Author: Alexey Kardashevskiy <aik@ozlabs.ru>
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
#include "hw/core/nmi.h"
#include "qapi/error.h"

static int do_nmi(Object *o, void *opaque)
{
    bool *handled = opaque;
    NMIState *n = (NMIState *) object_dynamic_cast(o, TYPE_NMI);

    if (n) {
        *handled = true;
        NMI_GET_CLASS(n)->raise_nmi(n);
    }

    return 0;
}

bool nmi_inject(Error **errp)
{
    bool handled = false;

    object_child_foreach_recursive(object_get_root(), do_nmi, &handled);
    if (!handled) {
        error_setg(errp, "machine does not provide NMIs");
        return false;
    }
    return true;
}

static const TypeInfo nmi_info = {
    .name          = TYPE_NMI,
    .parent        = TYPE_INTERFACE,
    .class_size    = sizeof(NMIClass),
};

static void nmi_register_types(void)
{
    type_register_static(&nmi_info);
}

type_init(nmi_register_types)
