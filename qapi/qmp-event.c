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
    int64_t sec, usec;

    err = qemu_gettimeofday(&tv);
    if (err < 0) {
        /* Put -1 to indicate failure of getting host time */
        sec = -1;
        usec = -1;
    } else {
        sec = tv.tv_sec;
        usec = tv.tv_usec;
    }

    obj = qobject_from_jsonf("{ 'seconds': %" PRId64 ", "
                             "'microseconds': %" PRId64 " }",
                             sec, usec);
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
