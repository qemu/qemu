/* AUTOMATICALLY GENERATED, DO NOT MODIFY */

/*
 * schema-defined QMP->QAPI command dispatch
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
#include "qemu-common.h"
#include "qemu/module.h"
#include "qapi/qmp/types.h"
#include "qapi/qmp/dispatch.h"
#include "qapi/visitor.h"
#include "qapi/qmp-output-visitor.h"
#include "qapi/qmp-input-visitor.h"
#include "qapi/dealloc-visitor.h"
#include "test-qapi-types.h"
#include "test-qapi-visit.h"
#include "test-qmp-commands.h"


static void qmp_marshal_output___org_qemu_x_Union1(__org_qemu_x_Union1 *ret_in, QObject **ret_out, Error **errp)
{
    Error *err = NULL;
    QmpOutputVisitor *qov = qmp_output_visitor_new();
    QapiDeallocVisitor *qdv;
    Visitor *v;

    v = qmp_output_get_visitor(qov);
    visit_type___org_qemu_x_Union1(v, "unused", &ret_in, &err);
    if (err) {
        goto out;
    }
    *ret_out = qmp_output_get_qobject(qov);

out:
    error_propagate(errp, err);
    qmp_output_visitor_cleanup(qov);
    qdv = qapi_dealloc_visitor_new();
    v = qapi_dealloc_get_visitor(qdv);
    visit_type___org_qemu_x_Union1(v, "unused", &ret_in, NULL);
    qapi_dealloc_visitor_cleanup(qdv);
}

static void qmp_marshal___org_qemu_x_command(QDict *args, QObject **ret, Error **errp)
{
    Error *err = NULL;
    __org_qemu_x_Union1 *retval;
    QmpInputVisitor *qiv = qmp_input_visitor_new(QOBJECT(args), true);
    QapiDeallocVisitor *qdv;
    Visitor *v;
    q_obj___org_qemu_x_command_arg arg = {0};

    v = qmp_input_get_visitor(qiv);
    visit_start_struct(v, NULL, NULL, 0, &err);
    if (err) {
        goto out;
    }
    visit_type_q_obj___org_qemu_x_command_arg_members(v, &arg, &err);
    if (!err) {
        visit_check_struct(v, &err);
    }
    visit_end_struct(v);
    if (err) {
        goto out;
    }

    retval = qmp___org_qemu_x_command(arg.a, arg.b, arg.c, arg.d, &err);
    if (err) {
        goto out;
    }

    qmp_marshal_output___org_qemu_x_Union1(retval, ret, &err);

out:
    error_propagate(errp, err);
    qmp_input_visitor_cleanup(qiv);
    qdv = qapi_dealloc_visitor_new();
    v = qapi_dealloc_get_visitor(qdv);
    visit_start_struct(v, NULL, NULL, 0, NULL);
    visit_type_q_obj___org_qemu_x_command_arg_members(v, &arg, NULL);
    visit_end_struct(v);
    qapi_dealloc_visitor_cleanup(qdv);
}

static void qmp_marshal_output_int(int64_t ret_in, QObject **ret_out, Error **errp)
{
    Error *err = NULL;
    QmpOutputVisitor *qov = qmp_output_visitor_new();
    QapiDeallocVisitor *qdv;
    Visitor *v;

    v = qmp_output_get_visitor(qov);
    visit_type_int(v, "unused", &ret_in, &err);
    if (err) {
        goto out;
    }
    *ret_out = qmp_output_get_qobject(qov);

out:
    error_propagate(errp, err);
    qmp_output_visitor_cleanup(qov);
    qdv = qapi_dealloc_visitor_new();
    v = qapi_dealloc_get_visitor(qdv);
    visit_type_int(v, "unused", &ret_in, NULL);
    qapi_dealloc_visitor_cleanup(qdv);
}

static void qmp_marshal_guest_get_time(QDict *args, QObject **ret, Error **errp)
{
    Error *err = NULL;
    int64_t retval;
    QmpInputVisitor *qiv = qmp_input_visitor_new(QOBJECT(args), true);
    QapiDeallocVisitor *qdv;
    Visitor *v;
    q_obj_guest_get_time_arg arg = {0};

    v = qmp_input_get_visitor(qiv);
    visit_start_struct(v, NULL, NULL, 0, &err);
    if (err) {
        goto out;
    }
    visit_type_q_obj_guest_get_time_arg_members(v, &arg, &err);
    if (!err) {
        visit_check_struct(v, &err);
    }
    visit_end_struct(v);
    if (err) {
        goto out;
    }

    retval = qmp_guest_get_time(arg.a, arg.has_b, arg.b, &err);
    if (err) {
        goto out;
    }

    qmp_marshal_output_int(retval, ret, &err);

out:
    error_propagate(errp, err);
    qmp_input_visitor_cleanup(qiv);
    qdv = qapi_dealloc_visitor_new();
    v = qapi_dealloc_get_visitor(qdv);
    visit_start_struct(v, NULL, NULL, 0, NULL);
    visit_type_q_obj_guest_get_time_arg_members(v, &arg, NULL);
    visit_end_struct(v);
    qapi_dealloc_visitor_cleanup(qdv);
}

static void qmp_marshal_output_any(QObject *ret_in, QObject **ret_out, Error **errp)
{
    Error *err = NULL;
    QmpOutputVisitor *qov = qmp_output_visitor_new();
    QapiDeallocVisitor *qdv;
    Visitor *v;

    v = qmp_output_get_visitor(qov);
    visit_type_any(v, "unused", &ret_in, &err);
    if (err) {
        goto out;
    }
    *ret_out = qmp_output_get_qobject(qov);

out:
    error_propagate(errp, err);
    qmp_output_visitor_cleanup(qov);
    qdv = qapi_dealloc_visitor_new();
    v = qapi_dealloc_get_visitor(qdv);
    visit_type_any(v, "unused", &ret_in, NULL);
    qapi_dealloc_visitor_cleanup(qdv);
}

static void qmp_marshal_guest_sync(QDict *args, QObject **ret, Error **errp)
{
    Error *err = NULL;
    QObject *retval;
    QmpInputVisitor *qiv = qmp_input_visitor_new(QOBJECT(args), true);
    QapiDeallocVisitor *qdv;
    Visitor *v;
    q_obj_guest_sync_arg arg = {0};

    v = qmp_input_get_visitor(qiv);
    visit_start_struct(v, NULL, NULL, 0, &err);
    if (err) {
        goto out;
    }
    visit_type_q_obj_guest_sync_arg_members(v, &arg, &err);
    if (!err) {
        visit_check_struct(v, &err);
    }
    visit_end_struct(v);
    if (err) {
        goto out;
    }

    retval = qmp_guest_sync(arg.arg, &err);
    if (err) {
        goto out;
    }

    qmp_marshal_output_any(retval, ret, &err);

out:
    error_propagate(errp, err);
    qmp_input_visitor_cleanup(qiv);
    qdv = qapi_dealloc_visitor_new();
    v = qapi_dealloc_get_visitor(qdv);
    visit_start_struct(v, NULL, NULL, 0, NULL);
    visit_type_q_obj_guest_sync_arg_members(v, &arg, NULL);
    visit_end_struct(v);
    qapi_dealloc_visitor_cleanup(qdv);
}

static void qmp_marshal_user_def_cmd(QDict *args, QObject **ret, Error **errp)
{
    Error *err = NULL;

    (void)args;

    qmp_user_def_cmd(&err);
    error_propagate(errp, err);
}

static void qmp_marshal_output_Empty2(Empty2 *ret_in, QObject **ret_out, Error **errp)
{
    Error *err = NULL;
    QmpOutputVisitor *qov = qmp_output_visitor_new();
    QapiDeallocVisitor *qdv;
    Visitor *v;

    v = qmp_output_get_visitor(qov);
    visit_type_Empty2(v, "unused", &ret_in, &err);
    if (err) {
        goto out;
    }
    *ret_out = qmp_output_get_qobject(qov);

out:
    error_propagate(errp, err);
    qmp_output_visitor_cleanup(qov);
    qdv = qapi_dealloc_visitor_new();
    v = qapi_dealloc_get_visitor(qdv);
    visit_type_Empty2(v, "unused", &ret_in, NULL);
    qapi_dealloc_visitor_cleanup(qdv);
}

static void qmp_marshal_user_def_cmd0(QDict *args, QObject **ret, Error **errp)
{
    Error *err = NULL;
    Empty2 *retval;

    (void)args;

    retval = qmp_user_def_cmd0(&err);
    if (err) {
        goto out;
    }

    qmp_marshal_output_Empty2(retval, ret, &err);

out:
    error_propagate(errp, err);
}

static void qmp_marshal_user_def_cmd1(QDict *args, QObject **ret, Error **errp)
{
    Error *err = NULL;
    QmpInputVisitor *qiv = qmp_input_visitor_new(QOBJECT(args), true);
    QapiDeallocVisitor *qdv;
    Visitor *v;
    q_obj_user_def_cmd1_arg arg = {0};

    v = qmp_input_get_visitor(qiv);
    visit_start_struct(v, NULL, NULL, 0, &err);
    if (err) {
        goto out;
    }
    visit_type_q_obj_user_def_cmd1_arg_members(v, &arg, &err);
    if (!err) {
        visit_check_struct(v, &err);
    }
    visit_end_struct(v);
    if (err) {
        goto out;
    }

    qmp_user_def_cmd1(arg.ud1a, &err);

out:
    error_propagate(errp, err);
    qmp_input_visitor_cleanup(qiv);
    qdv = qapi_dealloc_visitor_new();
    v = qapi_dealloc_get_visitor(qdv);
    visit_start_struct(v, NULL, NULL, 0, NULL);
    visit_type_q_obj_user_def_cmd1_arg_members(v, &arg, NULL);
    visit_end_struct(v);
    qapi_dealloc_visitor_cleanup(qdv);
}

static void qmp_marshal_output_UserDefTwo(UserDefTwo *ret_in, QObject **ret_out, Error **errp)
{
    Error *err = NULL;
    QmpOutputVisitor *qov = qmp_output_visitor_new();
    QapiDeallocVisitor *qdv;
    Visitor *v;

    v = qmp_output_get_visitor(qov);
    visit_type_UserDefTwo(v, "unused", &ret_in, &err);
    if (err) {
        goto out;
    }
    *ret_out = qmp_output_get_qobject(qov);

out:
    error_propagate(errp, err);
    qmp_output_visitor_cleanup(qov);
    qdv = qapi_dealloc_visitor_new();
    v = qapi_dealloc_get_visitor(qdv);
    visit_type_UserDefTwo(v, "unused", &ret_in, NULL);
    qapi_dealloc_visitor_cleanup(qdv);
}

static void qmp_marshal_user_def_cmd2(QDict *args, QObject **ret, Error **errp)
{
    Error *err = NULL;
    UserDefTwo *retval;
    QmpInputVisitor *qiv = qmp_input_visitor_new(QOBJECT(args), true);
    QapiDeallocVisitor *qdv;
    Visitor *v;
    q_obj_user_def_cmd2_arg arg = {0};

    v = qmp_input_get_visitor(qiv);
    visit_start_struct(v, NULL, NULL, 0, &err);
    if (err) {
        goto out;
    }
    visit_type_q_obj_user_def_cmd2_arg_members(v, &arg, &err);
    if (!err) {
        visit_check_struct(v, &err);
    }
    visit_end_struct(v);
    if (err) {
        goto out;
    }

    retval = qmp_user_def_cmd2(arg.ud1a, arg.has_ud1b, arg.ud1b, &err);
    if (err) {
        goto out;
    }

    qmp_marshal_output_UserDefTwo(retval, ret, &err);

out:
    error_propagate(errp, err);
    qmp_input_visitor_cleanup(qiv);
    qdv = qapi_dealloc_visitor_new();
    v = qapi_dealloc_get_visitor(qdv);
    visit_start_struct(v, NULL, NULL, 0, NULL);
    visit_type_q_obj_user_def_cmd2_arg_members(v, &arg, NULL);
    visit_end_struct(v);
    qapi_dealloc_visitor_cleanup(qdv);
}

static void qmp_init_marshal(void)
{
    qmp_register_command("__org.qemu_x-command", qmp_marshal___org_qemu_x_command, QCO_NO_OPTIONS);
    qmp_register_command("guest-get-time", qmp_marshal_guest_get_time, QCO_NO_OPTIONS);
    qmp_register_command("guest-sync", qmp_marshal_guest_sync, QCO_NO_OPTIONS);
    qmp_register_command("user_def_cmd", qmp_marshal_user_def_cmd, QCO_NO_OPTIONS);
    qmp_register_command("user_def_cmd0", qmp_marshal_user_def_cmd0, QCO_NO_OPTIONS);
    qmp_register_command("user_def_cmd1", qmp_marshal_user_def_cmd1, QCO_NO_OPTIONS);
    qmp_register_command("user_def_cmd2", qmp_marshal_user_def_cmd2, QCO_NO_OPTIONS);
}

qapi_init(qmp_init_marshal);
