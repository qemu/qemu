/*
 * Arm CPU feature test cases
 *
 * Copyright (c) 2019 Red Hat Inc.
 * Authors:
 *  Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "libqtest.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qjson.h"

#define MACHINE     "-machine virt,gic-version=max,accel=tcg "
#define MACHINE_KVM "-machine virt,gic-version=max,accel=kvm:tcg "
#define QUERY_HEAD  "{ 'execute': 'query-cpu-model-expansion', " \
                    "  'arguments': { 'type': 'full', "
#define QUERY_TAIL  "}}"

static bool kvm_enabled(QTestState *qts)
{
    QDict *resp, *qdict;
    bool enabled;

    resp = qtest_qmp(qts, "{ 'execute': 'query-kvm' }");
    g_assert(qdict_haskey(resp, "return"));
    qdict = qdict_get_qdict(resp, "return");
    g_assert(qdict_haskey(qdict, "enabled"));
    enabled = qdict_get_bool(qdict, "enabled");
    qobject_unref(resp);

    return enabled;
}

static QDict *do_query_no_props(QTestState *qts, const char *cpu_type)
{
    return qtest_qmp(qts, QUERY_HEAD "'model': { 'name': %s }"
                          QUERY_TAIL, cpu_type);
}

static QDict *do_query(QTestState *qts, const char *cpu_type,
                       const char *fmt, ...)
{
    QDict *resp;

    if (fmt) {
        QDict *args;
        va_list ap;

        va_start(ap, fmt);
        args = qdict_from_vjsonf_nofail(fmt, ap);
        va_end(ap);

        resp = qtest_qmp(qts, QUERY_HEAD "'model': { 'name': %s, "
                                                    "'props': %p }"
                              QUERY_TAIL, cpu_type, args);
    } else {
        resp = do_query_no_props(qts, cpu_type);
    }

    return resp;
}

static const char *resp_get_error(QDict *resp)
{
    QDict *qdict;

    g_assert(resp);

    qdict = qdict_get_qdict(resp, "error");
    if (qdict) {
        return qdict_get_str(qdict, "desc");
    }

    return NULL;
}

#define assert_error(qts, cpu_type, expected_error, fmt, ...)          \
({                                                                     \
    QDict *_resp;                                                      \
    const char *_error;                                                \
                                                                       \
    _resp = do_query(qts, cpu_type, fmt, ##__VA_ARGS__);               \
    g_assert(_resp);                                                   \
    _error = resp_get_error(_resp);                                    \
    g_assert(_error);                                                  \
    g_assert(g_str_equal(_error, expected_error));                     \
    qobject_unref(_resp);                                              \
})

static bool resp_has_props(QDict *resp)
{
    QDict *qdict;

    g_assert(resp);

    if (!qdict_haskey(resp, "return")) {
        return false;
    }
    qdict = qdict_get_qdict(resp, "return");

    if (!qdict_haskey(qdict, "model")) {
        return false;
    }
    qdict = qdict_get_qdict(qdict, "model");

    return qdict_haskey(qdict, "props");
}

static QDict *resp_get_props(QDict *resp)
{
    QDict *qdict;

    g_assert(resp);
    g_assert(resp_has_props(resp));

    qdict = qdict_get_qdict(resp, "return");
    qdict = qdict_get_qdict(qdict, "model");
    qdict = qdict_get_qdict(qdict, "props");

    return qdict;
}

#define assert_has_feature(qts, cpu_type, feature)                     \
({                                                                     \
    QDict *_resp = do_query_no_props(qts, cpu_type);                   \
    g_assert(_resp);                                                   \
    g_assert(resp_has_props(_resp));                                   \
    g_assert(qdict_get(resp_get_props(_resp), feature));               \
    qobject_unref(_resp);                                              \
})

#define assert_has_not_feature(qts, cpu_type, feature)                 \
({                                                                     \
    QDict *_resp = do_query_no_props(qts, cpu_type);                   \
    g_assert(_resp);                                                   \
    g_assert(!resp_has_props(_resp) ||                                 \
             !qdict_get(resp_get_props(_resp), feature));              \
    qobject_unref(_resp);                                              \
})

static void assert_type_full(QTestState *qts)
{
    const char *error;
    QDict *resp;

    resp = qtest_qmp(qts, "{ 'execute': 'query-cpu-model-expansion', "
                            "'arguments': { 'type': 'static', "
                                           "'model': { 'name': 'foo' }}}");
    g_assert(resp);
    error = resp_get_error(resp);
    g_assert(error);
    g_assert(g_str_equal(error,
                         "The requested expansion type is not supported"));
    qobject_unref(resp);
}

static void assert_bad_props(QTestState *qts, const char *cpu_type)
{
    const char *error;
    QDict *resp;

    resp = qtest_qmp(qts, "{ 'execute': 'query-cpu-model-expansion', "
                            "'arguments': { 'type': 'full', "
                                           "'model': { 'name': %s, "
                                                      "'props': false }}}",
                     cpu_type);
    g_assert(resp);
    error = resp_get_error(resp);
    g_assert(error);
    g_assert(g_str_equal(error,
                         "Invalid parameter type for 'props', expected: dict"));
    qobject_unref(resp);
}

static void test_query_cpu_model_expansion(const void *data)
{
    QTestState *qts;

    qts = qtest_init(MACHINE "-cpu max");

    /* Test common query-cpu-model-expansion input validation */
    assert_type_full(qts);
    assert_bad_props(qts, "max");
    assert_error(qts, "foo", "The CPU type 'foo' is not a recognized "
                 "ARM CPU type", NULL);
    assert_error(qts, "max", "Parameter 'not-a-prop' is unexpected",
                 "{ 'not-a-prop': false }");
    assert_error(qts, "host", "The CPU type 'host' requires KVM", NULL);

    /* Test expected feature presence/absence for some cpu types */
    assert_has_feature(qts, "max", "pmu");
    assert_has_feature(qts, "cortex-a15", "pmu");
    assert_has_not_feature(qts, "cortex-a15", "aarch64");

    if (g_str_equal(qtest_get_arch(), "aarch64")) {
        assert_has_feature(qts, "max", "aarch64");
        assert_has_feature(qts, "cortex-a57", "pmu");
        assert_has_feature(qts, "cortex-a57", "aarch64");

        /* Test that features that depend on KVM generate errors without. */
        assert_error(qts, "max",
                     "'aarch64' feature cannot be disabled "
                     "unless KVM is enabled and 32-bit EL1 "
                     "is supported",
                     "{ 'aarch64': false }");
    }

    qtest_quit(qts);
}

static void test_query_cpu_model_expansion_kvm(const void *data)
{
    QTestState *qts;

    qts = qtest_init(MACHINE_KVM "-cpu max");

    /*
     * These tests target the 'host' CPU type, so KVM must be enabled.
     */
    if (!kvm_enabled(qts)) {
        qtest_quit(qts);
        return;
    }

    if (g_str_equal(qtest_get_arch(), "aarch64")) {
        assert_has_feature(qts, "host", "aarch64");
        assert_has_feature(qts, "host", "pmu");

        assert_error(qts, "cortex-a15",
            "We cannot guarantee the CPU type 'cortex-a15' works "
            "with KVM on this host", NULL);
    } else {
        assert_has_not_feature(qts, "host", "aarch64");
        assert_has_not_feature(qts, "host", "pmu");
    }

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_data_func("/arm/query-cpu-model-expansion",
                        NULL, test_query_cpu_model_expansion);

    /*
     * For now we only run KVM specific tests with AArch64 QEMU in
     * order avoid attempting to run an AArch32 QEMU with KVM on
     * AArch64 hosts. That won't work and isn't easy to detect.
     */
    if (g_str_equal(qtest_get_arch(), "aarch64")) {
        qtest_add_data_func("/arm/kvm/query-cpu-model-expansion",
                            NULL, test_query_cpu_model_expansion_kvm);
    }

    return g_test_run();
}
