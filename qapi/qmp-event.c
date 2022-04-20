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

#include "qapi/qmp-event.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qjson.h"

static void timestamp_put(QDict *qdict)
{
    QDict *ts;
    int64_t rt = g_get_real_time();

    ts = qdict_from_jsonf_nofail("{ 'seconds': %lld, 'microseconds': %lld }",
                                 (long long)rt / G_USEC_PER_SEC,
                                 (long long)rt % G_USEC_PER_SEC);
    qdict_put(qdict, "timestamp", ts);
}

/*
 * Build a QDict, then fill event name and time stamp, caller should free the
 * QDict after usage.
 */
QDict *qmp_event_build_dict(const char *event_name)
{
    QDict *dict = qdict_new();
    qdict_put_str(dict, "event", event_name);
    timestamp_put(dict);
    return dict;
}
