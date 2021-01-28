/*
 * QMP commands related to the monitor (common to sysemu and tools)
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"

#include "monitor-internal.h"
#include "qemu-version.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-control.h"
#include "qapi/qapi-emit-events.h"
#include "qapi/qapi-introspect.h"

/*
 * Accept QMP capabilities in @list for @mon.
 * On success, set mon->qmp.capab[], and return true.
 * On error, set @errp, and return false.
 */
static bool qmp_caps_accept(MonitorQMP *mon, QMPCapabilityList *list,
                            Error **errp)
{
    GString *unavailable = NULL;
    bool capab[QMP_CAPABILITY__MAX];

    memset(capab, 0, sizeof(capab));

    for (; list; list = list->next) {
        if (!mon->capab_offered[list->value]) {
            if (!unavailable) {
                unavailable = g_string_new(QMPCapability_str(list->value));
            } else {
                g_string_append_printf(unavailable, ", %s",
                                      QMPCapability_str(list->value));
            }
        }
        capab[list->value] = true;
    }

    if (unavailable) {
        error_setg(errp, "Capability %s not available", unavailable->str);
        g_string_free(unavailable, true);
        return false;
    }

    memcpy(mon->capab, capab, sizeof(capab));
    return true;
}

void qmp_qmp_capabilities(bool has_enable, QMPCapabilityList *enable,
                          Error **errp)
{
    Monitor *cur_mon = monitor_cur();
    MonitorQMP *mon;

    assert(monitor_is_qmp(cur_mon));
    mon = container_of(cur_mon, MonitorQMP, common);

    if (mon->commands == &qmp_commands) {
        error_set(errp, ERROR_CLASS_COMMAND_NOT_FOUND,
                  "Capabilities negotiation is already complete, command "
                  "ignored");
        return;
    }

    if (!qmp_caps_accept(mon, enable, errp)) {
        return;
    }

    mon->commands = &qmp_commands;
}

VersionInfo *qmp_query_version(Error **errp)
{
    VersionInfo *info = g_new0(VersionInfo, 1);

    info->qemu = g_new0(VersionTriple, 1);
    info->qemu->major = QEMU_VERSION_MAJOR;
    info->qemu->minor = QEMU_VERSION_MINOR;
    info->qemu->micro = QEMU_VERSION_MICRO;
    info->package = g_strdup(QEMU_PKGVERSION);

    return info;
}

static void query_commands_cb(const QmpCommand *cmd, void *opaque)
{
    CommandInfo *info;
    CommandInfoList **list = opaque;

    if (!cmd->enabled) {
        return;
    }

    info = g_malloc0(sizeof(*info));
    info->name = g_strdup(cmd->name);
    QAPI_LIST_PREPEND(*list, info);
}

CommandInfoList *qmp_query_commands(Error **errp)
{
    CommandInfoList *list = NULL;
    Monitor *cur_mon = monitor_cur();
    MonitorQMP *mon;

    assert(monitor_is_qmp(cur_mon));
    mon = container_of(cur_mon, MonitorQMP, common);

    qmp_for_each_command(mon->commands, query_commands_cb, &list);

    return list;
}

EventInfoList *qmp_query_events(Error **errp)
{
    /*
     * TODO This deprecated command is the only user of
     * QAPIEvent_str() and QAPIEvent_lookup[].  When the command goes,
     * they should go, too.
     */
    EventInfoList *ev_list = NULL;
    QAPIEvent e;

    for (e = 0 ; e < QAPI_EVENT__MAX ; e++) {
        const char *event_name = QAPIEvent_str(e);
        EventInfo *info;

        assert(event_name != NULL);
        info = g_malloc0(sizeof(*info));
        info->name = g_strdup(event_name);

        QAPI_LIST_PREPEND(ev_list, info);
    }

    return ev_list;
}

/*
 * Minor hack: generated marshalling suppressed for this command
 * ('gen': false in the schema) so we can parse the JSON string
 * directly into QObject instead of first parsing it with
 * visit_type_SchemaInfoList() into a SchemaInfoList, then marshal it
 * to QObject with generated output marshallers, every time.  Instead,
 * we do it in test-qobject-input-visitor.c, just to make sure
 * qapi-gen.py's output actually conforms to the schema.
 */
void qmp_query_qmp_schema(QDict *qdict, QObject **ret_data,
                                 Error **errp)
{
    *ret_data = qobject_from_qlit(&qmp_schema_qlit);
}
