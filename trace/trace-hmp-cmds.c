/*
 * HMP commands related to tracing
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
#include "monitor/hmp.h"
#include "monitor/monitor.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-trace.h"
#include "qapi/qmp/qdict.h"
#include "trace/control.h"
#ifdef CONFIG_TRACE_SIMPLE
#include "trace/simple.h"
#endif

void hmp_trace_event(Monitor *mon, const QDict *qdict)
{
    const char *tp_name = qdict_get_str(qdict, "name");
    bool new_state = qdict_get_bool(qdict, "option");
    bool has_vcpu = qdict_haskey(qdict, "vcpu");
    int vcpu = qdict_get_try_int(qdict, "vcpu", 0);
    Error *local_err = NULL;

    if (vcpu < 0) {
        monitor_printf(mon, "argument vcpu must be positive");
        return;
    }

    qmp_trace_event_set_state(tp_name, new_state, true, true, has_vcpu, vcpu, &local_err);
    if (local_err) {
        error_report_err(local_err);
    }
}

#ifdef CONFIG_TRACE_SIMPLE
void hmp_trace_file(Monitor *mon, const QDict *qdict)
{
    const char *op = qdict_get_try_str(qdict, "op");
    const char *arg = qdict_get_try_str(qdict, "arg");

    if (!op) {
        st_print_trace_file_status();
    } else if (!strcmp(op, "on")) {
        st_set_trace_file_enabled(true);
    } else if (!strcmp(op, "off")) {
        st_set_trace_file_enabled(false);
    } else if (!strcmp(op, "flush")) {
        st_flush_trace_buffer();
    } else if (!strcmp(op, "set")) {
        if (arg) {
            st_set_trace_file(arg);
        }
    } else {
        monitor_printf(mon, "unexpected argument \"%s\"\n", op);
        hmp_help_cmd(mon, "trace-file");
    }
}
#endif

void hmp_info_trace_events(Monitor *mon, const QDict *qdict)
{
    const char *name = qdict_get_try_str(qdict, "name");
    bool has_vcpu = qdict_haskey(qdict, "vcpu");
    int vcpu = qdict_get_try_int(qdict, "vcpu", 0);
    TraceEventInfoList *events;
    TraceEventInfoList *elem;
    Error *local_err = NULL;

    if (name == NULL) {
        name = "*";
    }
    if (vcpu < 0) {
        monitor_printf(mon, "argument vcpu must be positive");
        return;
    }

    events = qmp_trace_event_get_state(name, has_vcpu, vcpu, &local_err);
    if (local_err) {
        error_report_err(local_err);
        return;
    }

    for (elem = events; elem != NULL; elem = elem->next) {
        monitor_printf(mon, "%s : state %u\n",
                       elem->value->name,
                       elem->value->state == TRACE_EVENT_STATE_ENABLED ? 1 : 0);
    }
    qapi_free_TraceEventInfoList(events);
}

void info_trace_events_completion(ReadLineState *rs, int nb_args, const char *str)
{
    size_t len;

    len = strlen(str);
    readline_set_completion_index(rs, len);
    if (nb_args == 2) {
        TraceEventIter iter;
        TraceEvent *ev;
        char *pattern = g_strdup_printf("%s*", str);
        trace_event_iter_init_pattern(&iter, pattern);
        while ((ev = trace_event_iter_next(&iter)) != NULL) {
            readline_add_completion(rs, trace_event_get_name(ev));
        }
        g_free(pattern);
    }
}

void trace_event_completion(ReadLineState *rs, int nb_args, const char *str)
{
    size_t len;

    len = strlen(str);
    readline_set_completion_index(rs, len);
    if (nb_args == 2) {
        TraceEventIter iter;
        TraceEvent *ev;
        char *pattern = g_strdup_printf("%s*", str);
        trace_event_iter_init_pattern(&iter, pattern);
        while ((ev = trace_event_iter_next(&iter)) != NULL) {
            readline_add_completion(rs, trace_event_get_name(ev));
        }
        g_free(pattern);
    } else if (nb_args == 3) {
        readline_add_completion_of(rs, str, "on");
        readline_add_completion_of(rs, str, "off");
    }
}
