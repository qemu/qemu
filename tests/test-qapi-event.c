/* AUTOMATICALLY GENERATED, DO NOT MODIFY */

/*
 * schema-defined QAPI event functions
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
#include "test-qapi-event.h"
#include "test-qapi-visit.h"
#include "qapi/qmp-output-visitor.h"
#include "qapi/qmp-event.h"


void qapi_event_send_event_a(Error **errp)
{
    QDict *qmp;
    Error *err = NULL;
    QMPEventFuncEmit emit;

    emit = qmp_event_get_func_emit();
    if (!emit) {
        return;
    }

    qmp = qmp_event_build_dict("EVENT_A");

    emit(TEST_QAPI_EVENT_EVENT_A, qmp, &err);

    error_propagate(errp, err);
    QDECREF(qmp);
}

void qapi_event_send_event_b(Error **errp)
{
    QDict *qmp;
    Error *err = NULL;
    QMPEventFuncEmit emit;

    emit = qmp_event_get_func_emit();
    if (!emit) {
        return;
    }

    qmp = qmp_event_build_dict("EVENT_B");

    emit(TEST_QAPI_EVENT_EVENT_B, qmp, &err);

    error_propagate(errp, err);
    QDECREF(qmp);
}

void qapi_event_send_event_c(bool has_a, int64_t a, bool has_b, UserDefOne *b, const char *c, Error **errp)
{
    QDict *qmp;
    Error *err = NULL;
    QMPEventFuncEmit emit;
    QmpOutputVisitor *qov;
    Visitor *v;
    q_obj_EVENT_C_arg param = {
        has_a, a, has_b, b, (char *)c
    };

    emit = qmp_event_get_func_emit();
    if (!emit) {
        return;
    }

    qmp = qmp_event_build_dict("EVENT_C");

    qov = qmp_output_visitor_new();
    v = qmp_output_get_visitor(qov);

    visit_start_struct(v, "EVENT_C", NULL, 0, &err);
    if (err) {
        goto out;
    }
    visit_type_q_obj_EVENT_C_arg_members(v, &param, &err);
    if (!err) {
        visit_check_struct(v, &err);
    }
    visit_end_struct(v);
    if (err) {
        goto out;
    }

    qdict_put_obj(qmp, "data", qmp_output_get_qobject(qov));
    emit(TEST_QAPI_EVENT_EVENT_C, qmp, &err);

out:
    qmp_output_visitor_cleanup(qov);
    error_propagate(errp, err);
    QDECREF(qmp);
}

void qapi_event_send_event_d(EventStructOne *a, const char *b, bool has_c, const char *c, bool has_enum3, EnumOne enum3, Error **errp)
{
    QDict *qmp;
    Error *err = NULL;
    QMPEventFuncEmit emit;
    QmpOutputVisitor *qov;
    Visitor *v;
    q_obj_EVENT_D_arg param = {
        a, (char *)b, has_c, (char *)c, has_enum3, enum3
    };

    emit = qmp_event_get_func_emit();
    if (!emit) {
        return;
    }

    qmp = qmp_event_build_dict("EVENT_D");

    qov = qmp_output_visitor_new();
    v = qmp_output_get_visitor(qov);

    visit_start_struct(v, "EVENT_D", NULL, 0, &err);
    if (err) {
        goto out;
    }
    visit_type_q_obj_EVENT_D_arg_members(v, &param, &err);
    if (!err) {
        visit_check_struct(v, &err);
    }
    visit_end_struct(v);
    if (err) {
        goto out;
    }

    qdict_put_obj(qmp, "data", qmp_output_get_qobject(qov));
    emit(TEST_QAPI_EVENT_EVENT_D, qmp, &err);

out:
    qmp_output_visitor_cleanup(qov);
    error_propagate(errp, err);
    QDECREF(qmp);
}

void qapi_event_send___org_qemu_x_event(__org_qemu_x_Enum __org_qemu_x_member1, const char *__org_qemu_x_member2, bool has_q_wchar_t, int64_t q_wchar_t, Error **errp)
{
    QDict *qmp;
    Error *err = NULL;
    QMPEventFuncEmit emit;
    QmpOutputVisitor *qov;
    Visitor *v;
    __org_qemu_x_Struct param = {
        __org_qemu_x_member1, (char *)__org_qemu_x_member2, has_q_wchar_t, q_wchar_t
    };

    emit = qmp_event_get_func_emit();
    if (!emit) {
        return;
    }

    qmp = qmp_event_build_dict("__ORG.QEMU_X-EVENT");

    qov = qmp_output_visitor_new();
    v = qmp_output_get_visitor(qov);

    visit_start_struct(v, "__ORG.QEMU_X-EVENT", NULL, 0, &err);
    if (err) {
        goto out;
    }
    visit_type___org_qemu_x_Struct_members(v, &param, &err);
    if (!err) {
        visit_check_struct(v, &err);
    }
    visit_end_struct(v);
    if (err) {
        goto out;
    }

    qdict_put_obj(qmp, "data", qmp_output_get_qobject(qov));
    emit(TEST_QAPI_EVENT___ORG_QEMU_X_EVENT, qmp, &err);

out:
    qmp_output_visitor_cleanup(qov);
    error_propagate(errp, err);
    QDECREF(qmp);
}

const char *const test_QAPIEvent_lookup[] = {
    [TEST_QAPI_EVENT_EVENT_A] = "EVENT_A",
    [TEST_QAPI_EVENT_EVENT_B] = "EVENT_B",
    [TEST_QAPI_EVENT_EVENT_C] = "EVENT_C",
    [TEST_QAPI_EVENT_EVENT_D] = "EVENT_D",
    [TEST_QAPI_EVENT___ORG_QEMU_X_EVENT] = "__ORG.QEMU_X-EVENT",
    [TEST_QAPI_EVENT__MAX] = NULL,
};
