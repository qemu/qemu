/*
 * libqos driver framework
 *
 * Copyright (c) 2018 Emanuele Giuseppe Esposito <e.emanuelegiuseppe@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#ifndef QOS_EXTERNAL_H
#define QOS_EXTERNAL_H

#include "qgraph.h"

#include "libqos-malloc.h"
#include "qapi/qapi-types-machine.h"
#include "qapi/qapi-types-qom.h"

void machines_apply_to_node(MachineInfoList *mach_info);
void types_apply_to_node(ObjectTypeInfoList *type_info);
void *allocate_objects(QTestState *qts, char **path, QGuestAllocator **p_alloc);

#endif
