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

#include "qemu/osdep.h"

#include "qemu-common.h"
#include "qapi/qmp-event.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp/qjson.h"

static QMPEventFuncEmit qmp_emit;

void qmp_event_set_func_emit(QMPEventFuncEmit emit)
{
    qmp_emit = emit;
}

QMPEventFuncEmit qmp_event_get_func_emit(void)
{
    return qmp_emit;
}

static void timestamp_put(QDict *qdict)
{
    int err;
    QObject *obj;
    qemu_timeval tv;

    err = qemu_gettimeofday(&tv);
    /* Put -1 to indicate failure of getting host time */
    obj = qobject_from_jsonf("{ 'seconds': %lld, 'microseconds': %lld }",
                             err < 0 ? -1LL : (long long)tv.tv_sec,
                             err < 0 ? -1LL : (long long)tv.tv_usec);
    qdict_put_obj(qdict, "timestamp", obj);
}

/*
 * Build a QDict, then fill event name and time stamp, caller should free the
 * QDict after usage.
 */
QDict *qmp_event_build_dict(const char *event_name)
{
    QDict *dict = qdict_new();
    qdict_put(dict, "event", qstring_from_str(event_name));
    timestamp_put(dict);
    return dict;
}
