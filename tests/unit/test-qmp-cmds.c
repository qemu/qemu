#include "qemu/osdep.h"
#include "qapi/compat-policy.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qnum.h"
#include "qapi/qmp/qstring.h"
#include "qapi/error.h"
#include "qapi/qobject-input-visitor.h"
#include "tests/test-qapi-types.h"
#include "tests/test-qapi-visit.h"
#include "test-qapi-commands.h"
#include "test-qapi-init-commands.h"

static QmpCommandList qmp_commands;

UserDefThree *qmp_test_cmd_return_def_three(Error **errp)
{
    return NULL;
}

void qmp_user_def_cmd(Error **errp)
{
}

void qmp_test_flags_command(Error **errp)
{
}

void qmp_cmd_success_response(Error **errp)
{
}

void qmp_coroutine_cmd(Error **errp)
{
}

Empty2 *qmp_user_def_cmd0(Error **errp)
{
    return g_new0(Empty2, 1);
}

void qmp_user_def_cmd1(UserDefOne * ud1, Error **errp)
{
}

FeatureStruct1 *qmp_test_features0(FeatureStruct0 *fs0,
                                   FeatureStruct1 *fs1,
                                   FeatureStruct2 *fs2,
                                   FeatureStruct3 *fs3,
                                   FeatureStruct4 *fs4,
                                   CondFeatureStruct1 *cfs1,
                                   CondFeatureStruct2 *cfs2,
                                   CondFeatureStruct3 *cfs3,
                                   CondFeatureStruct4 *cfs4,
                                   Error **errp)
{
    return g_new0(FeatureStruct1, 1);
}

void qmp_test_command_features1(Error **errp)
{
}

void qmp_test_command_features3(Error **errp)
{
}

void qmp_test_command_cond_features1(Error **errp)
{
}

void qmp_test_command_cond_features2(Error **errp)
{
}

void qmp_test_command_cond_features3(Error **errp)
{
}

UserDefTwo *qmp_user_def_cmd2(UserDefOne *ud1a, UserDefOne *ud1b,
                              Error **errp)
{
    UserDefTwo *ret;
    UserDefOne *ud1c = g_new0(UserDefOne, 1);
    UserDefOne *ud1d = g_new0(UserDefOne, 1);

    ud1c->string = strdup(ud1a->string);
    ud1c->integer = ud1a->integer;
    ud1d->string = strdup(ud1b ? ud1b->string : "blah0");
    ud1d->integer = ud1b ? ud1b->integer : 0;

    ret = g_new0(UserDefTwo, 1);
    ret->string0 = strdup("blah1");
    ret->dict1 = g_new0(UserDefTwoDict, 1);
    ret->dict1->string1 = strdup("blah2");
    ret->dict1->dict2 = g_new0(UserDefTwoDictDict, 1);
    ret->dict1->dict2->userdef = ud1c;
    ret->dict1->dict2->string = strdup("blah3");
    ret->dict1->dict3 = g_new0(UserDefTwoDictDict, 1);
    ret->dict1->dict3->userdef = ud1d;
    ret->dict1->dict3->string = strdup("blah4");

    return ret;
}

int64_t qmp_guest_get_time(int64_t a, bool has_b, int64_t b, Error **errp)
{
    return a + (has_b ? b : 0);
}

QObject *qmp_guest_sync(QObject *arg, Error **errp)
{
    return arg;
}

void qmp_boxed_struct(UserDefZero *arg, Error **errp)
{
}

void qmp_boxed_union(UserDefFlatUnion *arg, Error **errp)
{
}

void qmp_boxed_empty(Empty1 *arg, Error **errp)
{
}

void qmp___org_qemu_x_command(__org_qemu_x_EnumList *a,
                              __org_qemu_x_StructList *b,
                              __org_qemu_x_Union *c,
                              __org_qemu_x_Alt *d,
                              Error **errp)
{
    /* Also test that 'wchar-t' was munged to 'q_wchar_t' */
    if (b && b->value && !b->value->has_q_wchar_t) {
        b->value->q_wchar_t = 1;
    }
}


G_GNUC_PRINTF(2, 3)
static QObject *do_qmp_dispatch(bool allow_oob, const char *template, ...)
{
    va_list ap;
    QDict *req, *resp;
    QObject *ret;

    va_start(ap, template);
    req = qdict_from_vjsonf_nofail(template, ap);
    va_end(ap);

    resp = qmp_dispatch(&qmp_commands, QOBJECT(req), allow_oob, NULL);
    g_assert(resp);
    ret = qdict_get(resp, "return");
    g_assert(ret);
    g_assert(qdict_size(resp) == 1);

    qobject_ref(ret);
    qobject_unref(resp);
    qobject_unref(req);
    return ret;
}

G_GNUC_PRINTF(3, 4)
static void do_qmp_dispatch_error(bool allow_oob, ErrorClass cls,
                                  const char *template, ...)
{
    va_list ap;
    QDict *req, *resp;
    QDict *error;

    va_start(ap, template);
    req = qdict_from_vjsonf_nofail(template, ap);
    va_end(ap);

    resp = qmp_dispatch(&qmp_commands, QOBJECT(req), allow_oob, NULL);
    g_assert(resp);
    error = qdict_get_qdict(resp, "error");
    g_assert(error);
    g_assert_cmpstr(qdict_get_try_str(error, "class"),
                    ==, QapiErrorClass_str(cls));
    g_assert(qdict_get_try_str(error, "desc"));
    g_assert(qdict_size(error) == 2);
    g_assert(qdict_size(resp) == 1);

    qobject_unref(resp);
    qobject_unref(req);
}

/* test commands with no input and no return value */
static void test_dispatch_cmd(void)
{
    QDict *ret;

    ret = qobject_to(QDict,
                     do_qmp_dispatch(false,
                                     "{ 'execute': 'user-def-cmd' }"));
    assert(ret && qdict_size(ret) == 0);
    qobject_unref(ret);
}

static void test_dispatch_cmd_oob(void)
{
    QDict *ret;

    ret = qobject_to(QDict,
                     do_qmp_dispatch(true,
                                     "{ 'exec-oob': 'test-flags-command' }"));
    assert(ret && qdict_size(ret) == 0);
    qobject_unref(ret);
}

/* test commands that return an error due to invalid parameters */
static void test_dispatch_cmd_failure(void)
{
    /* missing arguments */
    do_qmp_dispatch_error(false, ERROR_CLASS_GENERIC_ERROR,
                          "{ 'execute': 'user-def-cmd2' }");

    /* extra arguments */
    do_qmp_dispatch_error(false, ERROR_CLASS_GENERIC_ERROR,
                          "{ 'execute': 'user-def-cmd',"
                          " 'arguments': { 'a': 66 } }");
}

static void test_dispatch_cmd_success_response(void)
{
    QDict *req = qdict_new();
    QDict *resp;

    qdict_put_str(req, "execute", "cmd-success-response");
    resp = qmp_dispatch(&qmp_commands, QOBJECT(req), false, NULL);
    g_assert_null(resp);
    qobject_unref(req);
}

/* test commands that involve both input parameters and return values */
static void test_dispatch_cmd_io(void)
{
    QDict *ret, *ret_dict, *ret_dict_dict, *ret_dict_dict_userdef;
    QDict *ret_dict_dict2, *ret_dict_dict2_userdef;
    QNum *ret3;
    int64_t val;

    ret = qobject_to(QDict, do_qmp_dispatch(false,
        "{ 'execute': 'user-def-cmd2', 'arguments': {"
        " 'ud1a': { 'integer': 42, 'string': 'hello' },"
        " 'ud1b': { 'integer': 422, 'string': 'hello2' } } }"));

    assert(!strcmp(qdict_get_str(ret, "string0"), "blah1"));
    ret_dict = qdict_get_qdict(ret, "dict1");
    assert(!strcmp(qdict_get_str(ret_dict, "string1"), "blah2"));
    ret_dict_dict = qdict_get_qdict(ret_dict, "dict2");
    ret_dict_dict_userdef = qdict_get_qdict(ret_dict_dict, "userdef");
    assert(qdict_get_int(ret_dict_dict_userdef, "integer") == 42);
    assert(!strcmp(qdict_get_str(ret_dict_dict_userdef, "string"), "hello"));
    assert(!strcmp(qdict_get_str(ret_dict_dict, "string"), "blah3"));
    ret_dict_dict2 = qdict_get_qdict(ret_dict, "dict3");
    ret_dict_dict2_userdef = qdict_get_qdict(ret_dict_dict2, "userdef");
    assert(qdict_get_int(ret_dict_dict2_userdef, "integer") == 422);
    assert(!strcmp(qdict_get_str(ret_dict_dict2_userdef, "string"), "hello2"));
    assert(!strcmp(qdict_get_str(ret_dict_dict2, "string"), "blah4"));
    qobject_unref(ret);

    ret3 = qobject_to(QNum, do_qmp_dispatch(false,
        "{ 'execute': 'guest-get-time', 'arguments': { 'a': 66 } }"));
    g_assert(qnum_get_try_int(ret3, &val));
    g_assert_cmpint(val, ==, 66);
    qobject_unref(ret3);
}

static void test_dispatch_cmd_deprecated(void)
{
    #define cmd "{ 'execute': 'test-command-features1' }"
    QDict *ret;

    memset(&compat_policy, 0, sizeof(compat_policy));

    /* accept */
    ret = qobject_to(QDict, do_qmp_dispatch(false, cmd));
    assert(ret && qdict_size(ret) == 0);
    qobject_unref(ret);

    compat_policy.has_deprecated_input = true;
    compat_policy.deprecated_input = COMPAT_POLICY_INPUT_ACCEPT;
    ret = qobject_to(QDict, do_qmp_dispatch(false, cmd));
    assert(ret && qdict_size(ret) == 0);
    qobject_unref(ret);

    compat_policy.deprecated_input = COMPAT_POLICY_INPUT_REJECT;
    do_qmp_dispatch_error(false, ERROR_CLASS_COMMAND_NOT_FOUND, cmd);
    #undef cmd
}

static void test_dispatch_cmd_arg_deprecated(void)
{
    #define cmd "{ 'execute': 'test-features0'," \
        " 'arguments': { 'fs1': { 'foo': 42 } } }"
    QDict *ret;

    memset(&compat_policy, 0, sizeof(compat_policy));

    /* accept */
    ret = qobject_to(QDict, do_qmp_dispatch(false, cmd));
    assert(ret && qdict_size(ret) == 1);
    qobject_unref(ret);

    compat_policy.has_deprecated_input = true;
    compat_policy.deprecated_input = COMPAT_POLICY_INPUT_ACCEPT;
    ret = qobject_to(QDict, do_qmp_dispatch(false, cmd));
    assert(ret && qdict_size(ret) == 1);
    qobject_unref(ret);

    compat_policy.deprecated_input = COMPAT_POLICY_INPUT_REJECT;
    do_qmp_dispatch_error(false, ERROR_CLASS_GENERIC_ERROR, cmd);
    #undef cmd
}

static void test_dispatch_cmd_ret_deprecated(void)
{
    #define cmd "{ 'execute': 'test-features0' }"
    QDict *ret;

    memset(&compat_policy, 0, sizeof(compat_policy));

    /* default accept */
    ret = qobject_to(QDict, do_qmp_dispatch(false, cmd));
    assert(ret && qdict_size(ret) == 1);
    qobject_unref(ret);

    compat_policy.has_deprecated_output = true;
    compat_policy.deprecated_output = COMPAT_POLICY_OUTPUT_ACCEPT;
    ret = qobject_to(QDict, do_qmp_dispatch(false, cmd));
    assert(ret && qdict_size(ret) == 1);
    qobject_unref(ret);

    compat_policy.deprecated_output = COMPAT_POLICY_OUTPUT_HIDE;
    ret = qobject_to(QDict, do_qmp_dispatch(false, cmd));
    assert(ret && qdict_size(ret) == 0);
    qobject_unref(ret);
    #undef cmd
}

/* test generated dealloc functions for generated types */
static void test_dealloc_types(void)
{
    UserDefOne *ud1test, *ud1a, *ud1b;
    UserDefOneList *ud1list;

    ud1test = g_new0(UserDefOne, 1);
    ud1test->integer = 42;
    ud1test->string = g_strdup("hi there 42");

    qapi_free_UserDefOne(ud1test);

    ud1a = g_new0(UserDefOne, 1);
    ud1a->integer = 43;
    ud1a->string = g_strdup("hi there 43");

    ud1b = g_new0(UserDefOne, 1);
    ud1b->integer = 44;
    ud1b->string = g_strdup("hi there 44");

    ud1list = g_new0(UserDefOneList, 1);
    ud1list->value = ud1a;
    ud1list->next = g_new0(UserDefOneList, 1);
    ud1list->next->value = ud1b;

    qapi_free_UserDefOneList(ud1list);
}

/* test generated deallocation on an object whose construction was prematurely
 * terminated due to an error */
static void test_dealloc_partial(void)
{
    static const char text[] = "don't leak me";

    UserDefTwo *ud2 = NULL;
    Error *err = NULL;

    /* create partial object */
    {
        QDict *ud2_dict;
        Visitor *v;

        ud2_dict = qdict_new();
        qdict_put_str(ud2_dict, "string0", text);

        v = qobject_input_visitor_new(QOBJECT(ud2_dict));
        visit_type_UserDefTwo(v, NULL, &ud2, &err);
        visit_free(v);
        qobject_unref(ud2_dict);
    }

    /* verify that visit_type_XXX() cleans up properly on error */
    error_free_or_abort(&err);
    assert(!ud2);

    /* Manually create a partial object, leaving ud2->dict1 at NULL */
    ud2 = g_new0(UserDefTwo, 1);
    ud2->string0 = g_strdup(text);

    /* tear down partial object */
    qapi_free_UserDefTwo(ud2);
}


int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/qmp/dispatch_cmd", test_dispatch_cmd);
    g_test_add_func("/qmp/dispatch_cmd_oob", test_dispatch_cmd_oob);
    g_test_add_func("/qmp/dispatch_cmd_failure", test_dispatch_cmd_failure);
    g_test_add_func("/qmp/dispatch_cmd_io", test_dispatch_cmd_io);
    g_test_add_func("/qmp/dispatch_cmd_success_response",
                    test_dispatch_cmd_success_response);
    g_test_add_func("/qmp/dispatch_cmd_deprecated",
                    test_dispatch_cmd_deprecated);
    g_test_add_func("/qmp/dispatch_cmd_arg_deprecated",
                    test_dispatch_cmd_arg_deprecated);
    g_test_add_func("/qmp/dispatch_cmd_ret_deprecated",
                    test_dispatch_cmd_ret_deprecated);
    g_test_add_func("/qmp/dealloc_types", test_dealloc_types);
    g_test_add_func("/qmp/dealloc_partial", test_dealloc_partial);

    test_qmp_init_marshal(&qmp_commands);
    g_test_run();

    return 0;
}
