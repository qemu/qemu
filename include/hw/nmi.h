/*
 *  NMI monitor handler class and helpers definitions.
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

#ifndef NMI_H
#define NMI_H

#include "qom/object.h"

#define TYPE_NMI "nmi"

#define NMI_CLASS(klass) \
     OBJECT_CLASS_CHECK(NMIClass, (klass), TYPE_NMI)
#define NMI_GET_CLASS(obj) \
    OBJECT_GET_CLASS(NMIClass, (obj), TYPE_NMI)
#define NMI(obj) \
     INTERFACE_CHECK(NMI, (obj), TYPE_NMI)

typedef struct NMIState NMIState;

typedef struct NMIClass {
    InterfaceClass parent_class;

    void (*nmi_monitor_handler)(NMIState *n, int cpu_index, Error **errp);
} NMIClass;

void nmi_monitor_handle(int cpu_index, Error **errp);

#endif /* NMI_H */
