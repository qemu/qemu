/*
 * QMP Event related
 *
 * Copyright (c) 2014 Wenchao Xia
 *
 * Authors:
 *  Wenchao Xia   <wenchaoqemu@gmail.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef QMP_EVENT_H
#define QMP_EVENT_H

typedef void (*QMPEventFuncEmit)(unsigned event, QDict *dict);

void qmp_event_set_func_emit(QMPEventFuncEmit emit);

QMPEventFuncEmit qmp_event_get_func_emit(void);

QDict *qmp_event_build_dict(const char *event_name);
#endif
