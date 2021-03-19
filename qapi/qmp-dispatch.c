/*
 * Core Definitions for QAPI/QMP Dispatch
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "block/aio.h"
#include "qapi/compat-policy.h"
#include "qapi/error.h"
#include "qapi/qmp/dispatch.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qobject-output-visitor.h"
#include "sysemu/runstate.h"
#include "qapi/qmp/qbool.h"
#include "qemu/coroutine.h"
#include "qemu/main-loop.h"

CompatPolicy compat_policy;

Visitor *qobject_input_visitor_new_qmp(QObject *obj)
{
    Visitor *v = qobject_input_visitor_new(obj);

    qobject_input_visitor_set_policy(v, compat_policy.deprecated_input);
    return v;
}

Visitor *qobject_output_visitor_new_qmp(QObject **result)
{
    Visitor *v = qobject_output_visitor_new(result);

    qobject_output_visitor_set_policy(v, compat_policy.deprecated_output);
    return v;
}

static QDict *qmp_dispatch_check_obj(QDict *dict, bool allow_oob,
                                     Error **errp)
{
    const char *exec_key = NULL;
    const QDictEntry *ent;
    const char *arg_name;
    const QObject *arg_obj;

    for (ent = qdict_first(dict); ent;
         ent = qdict_next(dict, ent)) {
        arg_name = qdict_entry_key(ent);
        arg_obj = qdict_entry_value(ent);

        if (!strcmp(arg_name, "execute")
            || (!strcmp(arg_name, "exec-oob") && allow_oob)) {
            if (qobject_type(arg_obj) != QTYPE_QSTRING) {
                error_setg(errp, "QMP input member '%s' must be a string",
                           arg_name);
                return NULL;
            }
            if (exec_key) {
                error_setg(errp, "QMP input member '%s' clashes with '%s'",
                           arg_name, exec_key);
                return NULL;
            }
            exec_key = arg_name;
        } else if (!strcmp(arg_name, "arguments")) {
            if (qobject_type(arg_obj) != QTYPE_QDICT) {
                error_setg(errp,
                           "QMP input member 'arguments' must be an object");
                return NULL;
            }
        } else if (!strcmp(arg_name, "id")) {
            continue;
        } else {
            error_setg(errp, "QMP input member '%s' is unexpected",
                       arg_name);
            return NULL;
        }
    }

    if (!exec_key) {
        error_setg(errp, "QMP input lacks member 'execute'");
        return NULL;
    }

    return dict;
}

QDict *qmp_error_response(Error *err)
{
    QDict *rsp;

    rsp = qdict_from_jsonf_nofail("{ 'error': { 'class': %s, 'desc': %s } }",
                                  QapiErrorClass_str(error_get_class(err)),
                                  error_get_pretty(err));
    error_free(err);
    return rsp;
}

/*
 * Does @qdict look like a command to be run out-of-band?
 */
bool qmp_is_oob(const QDict *dict)
{
    return qdict_haskey(dict, "exec-oob")
        && !qdict_haskey(dict, "execute");
}

typedef struct QmpDispatchBH {
    const QmpCommand *cmd;
    Monitor *cur_mon;
    QDict *args;
    QObject **ret;
    Error **errp;
    Coroutine *co;
} QmpDispatchBH;

static void do_qmp_dispatch_bh(void *opaque)
{
    QmpDispatchBH *data = opaque;

    assert(monitor_cur() == NULL);
    monitor_set_cur(qemu_coroutine_self(), data->cur_mon);
    data->cmd->fn(data->args, data->ret, data->errp);
    monitor_set_cur(qemu_coroutine_self(), NULL);
    aio_co_wake(data->co);
}

/*
 * Runs outside of coroutine context for OOB commands, but in coroutine
 * context for everything else.
 */
QDict *qmp_dispatch(const QmpCommandList *cmds, QObject *request,
                    bool allow_oob, Monitor *cur_mon)
{
    Error *err = NULL;
    bool oob;
    const char *command;
    QDict *args;
    const QmpCommand *cmd;
    QDict *dict;
    QObject *id;
    QObject *ret = NULL;
    QDict *rsp = NULL;

    dict = qobject_to(QDict, request);
    if (!dict) {
        id = NULL;
        error_setg(&err, "QMP input must be a JSON object");
        goto out;
    }

    id = qdict_get(dict, "id");

    if (!qmp_dispatch_check_obj(dict, allow_oob, &err)) {
        goto out;
    }

    command = qdict_get_try_str(dict, "execute");
    oob = false;
    if (!command) {
        assert(allow_oob);
        command = qdict_get_str(dict, "exec-oob");
        oob = true;
    }
    cmd = qmp_find_command(cmds, command);
    if (cmd == NULL) {
        error_set(&err, ERROR_CLASS_COMMAND_NOT_FOUND,
                  "The command %s has not been found", command);
        goto out;
    }
    if (cmd->options & QCO_DEPRECATED) {
        switch (compat_policy.deprecated_input) {
        case COMPAT_POLICY_INPUT_ACCEPT:
            break;
        case COMPAT_POLICY_INPUT_REJECT:
            error_set(&err, ERROR_CLASS_COMMAND_NOT_FOUND,
                      "Deprecated command %s disabled by policy",
                      command);
            goto out;
        case COMPAT_POLICY_INPUT_CRASH:
        default:
            abort();
        }
    }
    if (!cmd->enabled) {
        error_set(&err, ERROR_CLASS_COMMAND_NOT_FOUND,
                  "Command %s has been disabled%s%s",
                  command,
                  cmd->disable_reason ? ": " : "",
                  cmd->disable_reason ?: "");
        goto out;
    }
    if (oob && !(cmd->options & QCO_ALLOW_OOB)) {
        error_setg(&err, "The command %s does not support OOB",
                   command);
        goto out;
    }

    if (!qmp_command_available(cmd, &err)) {
        goto out;
    }

    if (!qdict_haskey(dict, "arguments")) {
        args = qdict_new();
    } else {
        args = qdict_get_qdict(dict, "arguments");
        qobject_ref(args);
    }

    assert(!(oob && qemu_in_coroutine()));
    assert(monitor_cur() == NULL);
    if (!!(cmd->options & QCO_COROUTINE) == qemu_in_coroutine()) {
        monitor_set_cur(qemu_coroutine_self(), cur_mon);
        cmd->fn(args, &ret, &err);
        monitor_set_cur(qemu_coroutine_self(), NULL);
    } else {
       /*
        * Actual context doesn't match the one the command needs.
        *
        * Case 1: we are in coroutine context, but command does not
        * have QCO_COROUTINE.  We need to drop out of coroutine
        * context for executing it.
        *
        * Case 2: we are outside coroutine context, but command has
        * QCO_COROUTINE.  Can't actually happen, because we get here
        * outside coroutine context only when executing a command
        * out of band, and OOB commands never have QCO_COROUTINE.
        */
        assert(!oob && qemu_in_coroutine() && !(cmd->options & QCO_COROUTINE));

        QmpDispatchBH data = {
            .cur_mon    = cur_mon,
            .cmd        = cmd,
            .args       = args,
            .ret        = &ret,
            .errp       = &err,
            .co         = qemu_coroutine_self(),
        };
        aio_bh_schedule_oneshot(qemu_get_aio_context(), do_qmp_dispatch_bh,
                                &data);
        qemu_coroutine_yield();
    }
    qobject_unref(args);
    if (err) {
        /* or assert(!ret) after reviewing all handlers: */
        qobject_unref(ret);
        goto out;
    }

    if (cmd->options & QCO_NO_SUCCESS_RESP) {
        g_assert(!ret);
        return NULL;
    } else if (!ret) {
        /*
         * When the command's schema has no 'returns', cmd->fn()
         * leaves @ret null.  The QMP spec calls for an empty object
         * then; supply it.
         */
        ret = QOBJECT(qdict_new());
    }

    rsp = qdict_new();
    qdict_put_obj(rsp, "return", ret);

out:
    if (err) {
        assert(!rsp);
        rsp = qmp_error_response(err);
    }

    assert(rsp);

    if (id) {
        qdict_put_obj(rsp, "id", qobject_ref(id));
    }

    return rsp;
}
