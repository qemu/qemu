/*
 * libqos driver framework
 *
 * Copyright (c) 2018 Emanuele Giuseppe Esposito <e.emanuelegiuseppe@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
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
#include "libqos/qgraph.h"

void apply_to_node(const char *name, bool is_machine, bool is_abstract);
void apply_to_qlist(QList *list, bool is_machine);
QGuestAllocator *get_machine_allocator(QOSGraphObject *obj);
void *allocate_objects(QTestState *qts, char **path, QGuestAllocator **p_alloc);

#endif
