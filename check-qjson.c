/*
 * Copyright IBM, Corp. 2009
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */
#include <check.h>

#include "qstring.h"
#include "qint.h"
#include "qdict.h"
#include "qlist.h"
#include "qfloat.h"
#include "qbool.h"
#include "qjson.h"

#include "qemu-common.h"

START_TEST(escaped_string)
{
    int i;
    struct {
        const char *encoded;
        const char *decoded;
        int skip;
    } test_cases[] = {
        { "\"\\b\"", "\b" },
        { "\"\\f\"", "\f" },
        { "\"\\n\"", "\n" },
        { "\"\\r\"", "\r" },
        { "\"\\t\"", "\t" },
        { "\"/\"", "/" },
        { "\"\\/\"", "/", .skip = 1 },
        { "\"\\\\\"", "\\" },
        { "\"\\\"\"", "\"" },
        { "\"hello world \\\"embedded string\\\"\"",
          "hello world \"embedded string\"" },
        { "\"hello world\\nwith new line\"", "hello world\nwith new line" },
        { "\"single byte utf-8 \\u0020\"", "single byte utf-8  ", .skip = 1 },
        { "\"double byte utf-8 \\u00A2\"", "double byte utf-8 \xc2\xa2" },
        { "\"triple byte utf-8 \\u20AC\"", "triple byte utf-8 \xe2\x82\xac" },
        {}
    };

    for (i = 0; test_cases[i].encoded; i++) {
        QObject *obj;
        QString *str;

        obj = qobject_from_json(test_cases[i].encoded);

        fail_unless(obj != NULL);
        fail_unless(qobject_type(obj) == QTYPE_QSTRING);
        
        str = qobject_to_qstring(obj);
        fail_unless(strcmp(qstring_get_str(str), test_cases[i].decoded) == 0,
                    "%s != %s\n", qstring_get_str(str), test_cases[i].decoded);

        if (test_cases[i].skip == 0) {
            str = qobject_to_json(obj);
            fail_unless(strcmp(qstring_get_str(str),test_cases[i].encoded) == 0,
                        "%s != %s\n", qstring_get_str(str),
                                      test_cases[i].encoded);

            qobject_decref(obj);
        }

        QDECREF(str);
    }
}
END_TEST

START_TEST(simple_string)
{
    int i;
    struct {
        const char *encoded;
        const char *decoded;
    } test_cases[] = {
        { "\"hello world\"", "hello world" },
        { "\"the quick brown fox jumped over the fence\"",
          "the quick brown fox jumped over the fence" },
        {}
    };

    for (i = 0; test_cases[i].encoded; i++) {
        QObject *obj;
        QString *str;

        obj = qobject_from_json(test_cases[i].encoded);

        fail_unless(obj != NULL);
        fail_unless(qobject_type(obj) == QTYPE_QSTRING);
        
        str = qobject_to_qstring(obj);
        fail_unless(strcmp(qstring_get_str(str), test_cases[i].decoded) == 0);

        str = qobject_to_json(obj);
        fail_unless(strcmp(qstring_get_str(str), test_cases[i].encoded) == 0);

        qobject_decref(obj);
        
        QDECREF(str);
    }
}
END_TEST

START_TEST(single_quote_string)
{
    int i;
    struct {
        const char *encoded;
        const char *decoded;
    } test_cases[] = {
        { "'hello world'", "hello world" },
        { "'the quick brown fox \\' jumped over the fence'",
          "the quick brown fox ' jumped over the fence" },
        {}
    };

    for (i = 0; test_cases[i].encoded; i++) {
        QObject *obj;
        QString *str;

        obj = qobject_from_json(test_cases[i].encoded);

        fail_unless(obj != NULL);
        fail_unless(qobject_type(obj) == QTYPE_QSTRING);
        
        str = qobject_to_qstring(obj);
        fail_unless(strcmp(qstring_get_str(str), test_cases[i].decoded) == 0);

        QDECREF(str);
    }
}
END_TEST

START_TEST(vararg_string)
{
    int i;
    struct {
        const char *decoded;
    } test_cases[] = {
        { "hello world" },
        { "the quick brown fox jumped over the fence" },
        {}
    };

    for (i = 0; test_cases[i].decoded; i++) {
        QObject *obj;
        QString *str;

        obj = qobject_from_jsonf("%s", test_cases[i].decoded);

        fail_unless(obj != NULL);
        fail_unless(qobject_type(obj) == QTYPE_QSTRING);
        
        str = qobject_to_qstring(obj);
        fail_unless(strcmp(qstring_get_str(str), test_cases[i].decoded) == 0);

        QDECREF(str);
    }
}
END_TEST

START_TEST(simple_number)
{
    int i;
    struct {
        const char *encoded;
        int64_t decoded;
        int skip;
    } test_cases[] = {
        { "0", 0 },
        { "1234", 1234 },
        { "1", 1 },
        { "-32", -32 },
        { "-0", 0, .skip = 1 },
        { },
    };

    for (i = 0; test_cases[i].encoded; i++) {
        QObject *obj;
        QInt *qint;

        obj = qobject_from_json(test_cases[i].encoded);
        fail_unless(obj != NULL);
        fail_unless(qobject_type(obj) == QTYPE_QINT);

        qint = qobject_to_qint(obj);
        fail_unless(qint_get_int(qint) == test_cases[i].decoded);
        if (test_cases[i].skip == 0) {
            QString *str;

            str = qobject_to_json(obj);
            fail_unless(strcmp(qstring_get_str(str), test_cases[i].encoded) == 0);
            QDECREF(str);
        }

        QDECREF(qint);
    }
}
END_TEST

START_TEST(float_number)
{
    int i;
    struct {
        const char *encoded;
        double decoded;
        int skip;
    } test_cases[] = {
        { "32.43", 32.43 },
        { "0.222", 0.222 },
        { "-32.12313", -32.12313 },
        { "-32.20e-10", -32.20e-10, .skip = 1 },
        { },
    };

    for (i = 0; test_cases[i].encoded; i++) {
        QObject *obj;
        QFloat *qfloat;

        obj = qobject_from_json(test_cases[i].encoded);
        fail_unless(obj != NULL);
        fail_unless(qobject_type(obj) == QTYPE_QFLOAT);

        qfloat = qobject_to_qfloat(obj);
        fail_unless(qfloat_get_double(qfloat) == test_cases[i].decoded);

        if (test_cases[i].skip == 0) {
            QString *str;

            str = qobject_to_json(obj);
            fail_unless(strcmp(qstring_get_str(str), test_cases[i].encoded) == 0);
            QDECREF(str);
        }

        QDECREF(qfloat);
    }
}
END_TEST

START_TEST(vararg_number)
{
    QObject *obj;
    QInt *qint;
    QFloat *qfloat;
    int value = 0x2342;
    int64_t value64 = 0x2342342343LL;
    double valuef = 2.323423423;

    obj = qobject_from_jsonf("%d", value);
    fail_unless(obj != NULL);
    fail_unless(qobject_type(obj) == QTYPE_QINT);

    qint = qobject_to_qint(obj);
    fail_unless(qint_get_int(qint) == value);

    QDECREF(qint);

    obj = qobject_from_jsonf("%" PRId64, value64);
    fail_unless(obj != NULL);
    fail_unless(qobject_type(obj) == QTYPE_QINT);

    qint = qobject_to_qint(obj);
    fail_unless(qint_get_int(qint) == value64);

    QDECREF(qint);

    obj = qobject_from_jsonf("%f", valuef);
    fail_unless(obj != NULL);
    fail_unless(qobject_type(obj) == QTYPE_QFLOAT);

    qfloat = qobject_to_qfloat(obj);
    fail_unless(qfloat_get_double(qfloat) == valuef);

    QDECREF(qfloat);
}
END_TEST

START_TEST(keyword_literal)
{
    QObject *obj;
    QBool *qbool;
    QString *str;

    obj = qobject_from_json("true");
    fail_unless(obj != NULL);
    fail_unless(qobject_type(obj) == QTYPE_QBOOL);

    qbool = qobject_to_qbool(obj);
    fail_unless(qbool_get_int(qbool) != 0);

    str = qobject_to_json(obj);
    fail_unless(strcmp(qstring_get_str(str), "true") == 0);
    QDECREF(str);

    QDECREF(qbool);

    obj = qobject_from_json("false");
    fail_unless(obj != NULL);
    fail_unless(qobject_type(obj) == QTYPE_QBOOL);

    qbool = qobject_to_qbool(obj);
    fail_unless(qbool_get_int(qbool) == 0);

    str = qobject_to_json(obj);
    fail_unless(strcmp(qstring_get_str(str), "false") == 0);
    QDECREF(str);

    QDECREF(qbool);

    obj = qobject_from_jsonf("%i", false);
    fail_unless(obj != NULL);
    fail_unless(qobject_type(obj) == QTYPE_QBOOL);

    qbool = qobject_to_qbool(obj);
    fail_unless(qbool_get_int(qbool) == 0);

    QDECREF(qbool);
    
    obj = qobject_from_jsonf("%i", true);
    fail_unless(obj != NULL);
    fail_unless(qobject_type(obj) == QTYPE_QBOOL);

    qbool = qobject_to_qbool(obj);
    fail_unless(qbool_get_int(qbool) != 0);

    QDECREF(qbool);
}
END_TEST

typedef struct LiteralQDictEntry LiteralQDictEntry;
typedef struct LiteralQObject LiteralQObject;

struct LiteralQObject
{
    int type;
    union {
        int64_t qint;
        const char *qstr;
        LiteralQDictEntry *qdict;
        LiteralQObject *qlist;
    } value;
};

struct LiteralQDictEntry
{
    const char *key;
    LiteralQObject value;
};

#define QLIT_QINT(val) (LiteralQObject){.type = QTYPE_QINT, .value.qint = (val)}
#define QLIT_QSTR(val) (LiteralQObject){.type = QTYPE_QSTRING, .value.qstr = (val)}
#define QLIT_QDICT(val) (LiteralQObject){.type = QTYPE_QDICT, .value.qdict = (val)}
#define QLIT_QLIST(val) (LiteralQObject){.type = QTYPE_QLIST, .value.qlist = (val)}

typedef struct QListCompareHelper
{
    int index;
    LiteralQObject *objs;
    int result;
} QListCompareHelper;

static int compare_litqobj_to_qobj(LiteralQObject *lhs, QObject *rhs);

static void compare_helper(QObject *obj, void *opaque)
{
    QListCompareHelper *helper = opaque;

    if (helper->result == 0) {
        return;
    }

    if (helper->objs[helper->index].type == QTYPE_NONE) {
        helper->result = 0;
        return;
    }

    helper->result = compare_litqobj_to_qobj(&helper->objs[helper->index++], obj);
}

static int compare_litqobj_to_qobj(LiteralQObject *lhs, QObject *rhs)
{
    if (lhs->type != qobject_type(rhs)) {
        return 0;
    }

    switch (lhs->type) {
    case QTYPE_QINT:
        return lhs->value.qint == qint_get_int(qobject_to_qint(rhs));
    case QTYPE_QSTRING:
        return (strcmp(lhs->value.qstr, qstring_get_str(qobject_to_qstring(rhs))) == 0);
    case QTYPE_QDICT: {
        int i;

        for (i = 0; lhs->value.qdict[i].key; i++) {
            QObject *obj = qdict_get(qobject_to_qdict(rhs), lhs->value.qdict[i].key);

            if (!compare_litqobj_to_qobj(&lhs->value.qdict[i].value, obj)) {
                return 0;
            }
        }

        return 1;
    }
    case QTYPE_QLIST: {
        QListCompareHelper helper;

        helper.index = 0;
        helper.objs = lhs->value.qlist;
        helper.result = 1;
        
        qlist_iter(qobject_to_qlist(rhs), compare_helper, &helper);

        return helper.result;
    }
    default:
        break;
    }

    return 0;
}

START_TEST(simple_dict)
{
    int i;
    struct {
        const char *encoded;
        LiteralQObject decoded;
    } test_cases[] = {
        {
            .encoded = "{\"foo\": 42, \"bar\": \"hello world\"}",
            .decoded = QLIT_QDICT(((LiteralQDictEntry[]){
                        { "foo", QLIT_QINT(42) },
                        { "bar", QLIT_QSTR("hello world") },
                        { }
                    })),
        }, {
            .encoded = "{}",
            .decoded = QLIT_QDICT(((LiteralQDictEntry[]){
                        { }
                    })),
        }, {
            .encoded = "{\"foo\": 43}",
            .decoded = QLIT_QDICT(((LiteralQDictEntry[]){
                        { "foo", QLIT_QINT(43) },
                        { }
                    })),
        },
        { }
    };

    for (i = 0; test_cases[i].encoded; i++) {
        QObject *obj;
        QString *str;

        obj = qobject_from_json(test_cases[i].encoded);
        fail_unless(obj != NULL);
        fail_unless(qobject_type(obj) == QTYPE_QDICT);

        fail_unless(compare_litqobj_to_qobj(&test_cases[i].decoded, obj) == 1);

        str = qobject_to_json(obj);
        qobject_decref(obj);

        obj = qobject_from_json(qstring_get_str(str));
        fail_unless(obj != NULL);
        fail_unless(qobject_type(obj) == QTYPE_QDICT);

        fail_unless(compare_litqobj_to_qobj(&test_cases[i].decoded, obj) == 1);
        qobject_decref(obj);
        QDECREF(str);
    }
}
END_TEST

START_TEST(simple_list)
{
    int i;
    struct {
        const char *encoded;
        LiteralQObject decoded;
    } test_cases[] = {
        {
            .encoded = "[43,42]",
            .decoded = QLIT_QLIST(((LiteralQObject[]){
                        QLIT_QINT(43),
                        QLIT_QINT(42),
                        { }
                    })),
        },
        {
            .encoded = "[43]",
            .decoded = QLIT_QLIST(((LiteralQObject[]){
                        QLIT_QINT(43),
                        { }
                    })),
        },
        {
            .encoded = "[]",
            .decoded = QLIT_QLIST(((LiteralQObject[]){
                        { }
                    })),
        },
        {
            .encoded = "[{}]",
            .decoded = QLIT_QLIST(((LiteralQObject[]){
                        QLIT_QDICT(((LiteralQDictEntry[]){
                                    {},
                                        })),
                        {},
                            })),
        },
        { }
    };

    for (i = 0; test_cases[i].encoded; i++) {
        QObject *obj;
        QString *str;

        obj = qobject_from_json(test_cases[i].encoded);
        fail_unless(obj != NULL);
        fail_unless(qobject_type(obj) == QTYPE_QLIST);

        fail_unless(compare_litqobj_to_qobj(&test_cases[i].decoded, obj) == 1);

        str = qobject_to_json(obj);
        qobject_decref(obj);

        obj = qobject_from_json(qstring_get_str(str));
        fail_unless(obj != NULL);
        fail_unless(qobject_type(obj) == QTYPE_QLIST);

        fail_unless(compare_litqobj_to_qobj(&test_cases[i].decoded, obj) == 1);
        qobject_decref(obj);
        QDECREF(str);
    }
}
END_TEST

START_TEST(simple_whitespace)
{
    int i;
    struct {
        const char *encoded;
        LiteralQObject decoded;
    } test_cases[] = {
        {
            .encoded = " [ 43 , 42 ]",
            .decoded = QLIT_QLIST(((LiteralQObject[]){
                        QLIT_QINT(43),
                        QLIT_QINT(42),
                        { }
                    })),
        },
        {
            .encoded = " [ 43 , { 'h' : 'b' }, [ ], 42 ]",
            .decoded = QLIT_QLIST(((LiteralQObject[]){
                        QLIT_QINT(43),
                        QLIT_QDICT(((LiteralQDictEntry[]){
                                    { "h", QLIT_QSTR("b") },
                                    { }})),
                        QLIT_QLIST(((LiteralQObject[]){
                                    { }})),
                        QLIT_QINT(42),
                        { }
                    })),
        },
        {
            .encoded = " [ 43 , { 'h' : 'b' , 'a' : 32 }, [ ], 42 ]",
            .decoded = QLIT_QLIST(((LiteralQObject[]){
                        QLIT_QINT(43),
                        QLIT_QDICT(((LiteralQDictEntry[]){
                                    { "h", QLIT_QSTR("b") },
                                    { "a", QLIT_QINT(32) },
                                    { }})),
                        QLIT_QLIST(((LiteralQObject[]){
                                    { }})),
                        QLIT_QINT(42),
                        { }
                    })),
        },
        { }
    };

    for (i = 0; test_cases[i].encoded; i++) {
        QObject *obj;
        QString *str;

        obj = qobject_from_json(test_cases[i].encoded);
        fail_unless(obj != NULL);
        fail_unless(qobject_type(obj) == QTYPE_QLIST);

        fail_unless(compare_litqobj_to_qobj(&test_cases[i].decoded, obj) == 1);

        str = qobject_to_json(obj);
        qobject_decref(obj);

        obj = qobject_from_json(qstring_get_str(str));
        fail_unless(obj != NULL);
        fail_unless(qobject_type(obj) == QTYPE_QLIST);

        fail_unless(compare_litqobj_to_qobj(&test_cases[i].decoded, obj) == 1);

        qobject_decref(obj);
        QDECREF(str);
    }
}
END_TEST

START_TEST(simple_varargs)
{
    QObject *embedded_obj;
    QObject *obj;
    LiteralQObject decoded = QLIT_QLIST(((LiteralQObject[]){
            QLIT_QINT(1),
            QLIT_QINT(2),
            QLIT_QLIST(((LiteralQObject[]){
                        QLIT_QINT(32),
                        QLIT_QINT(42),
                        {}})),
            {}}));

    embedded_obj = qobject_from_json("[32, 42]");
    fail_unless(embedded_obj != NULL);

    obj = qobject_from_jsonf("[%d, 2, %p]", 1, embedded_obj);
    fail_unless(obj != NULL);

    fail_unless(compare_litqobj_to_qobj(&decoded, obj) == 1);

    qobject_decref(obj);
}
END_TEST

START_TEST(empty_input)
{
    const char *empty = "";

    QObject *obj = qobject_from_json(empty);
    fail_unless(obj == NULL);
}
END_TEST

START_TEST(unterminated_string)
{
    QObject *obj = qobject_from_json("\"abc");
    fail_unless(obj == NULL);
}
END_TEST

START_TEST(unterminated_sq_string)
{
    QObject *obj = qobject_from_json("'abc");
    fail_unless(obj == NULL);
}
END_TEST

START_TEST(unterminated_escape)
{
    QObject *obj = qobject_from_json("\"abc\\\"");
    fail_unless(obj == NULL);
}
END_TEST

START_TEST(unterminated_array)
{
    QObject *obj = qobject_from_json("[32");
    fail_unless(obj == NULL);
}
END_TEST

START_TEST(unterminated_array_comma)
{
    QObject *obj = qobject_from_json("[32,");
    fail_unless(obj == NULL);
}
END_TEST

START_TEST(invalid_array_comma)
{
    QObject *obj = qobject_from_json("[32,}");
    fail_unless(obj == NULL);
}
END_TEST

START_TEST(unterminated_dict)
{
    QObject *obj = qobject_from_json("{'abc':32");
    fail_unless(obj == NULL);
}
END_TEST

START_TEST(unterminated_dict_comma)
{
    QObject *obj = qobject_from_json("{'abc':32,");
    fail_unless(obj == NULL);
}
END_TEST

#if 0
START_TEST(invalid_dict_comma)
{
    QObject *obj = qobject_from_json("{'abc':32,}");
    fail_unless(obj == NULL);
}
END_TEST

START_TEST(unterminated_literal)
{
    QObject *obj = qobject_from_json("nul");
    fail_unless(obj == NULL);
}
END_TEST
#endif

static Suite *qjson_suite(void)
{
    Suite *suite;
    TCase *string_literals, *number_literals, *keyword_literals;
    TCase *dicts, *lists, *whitespace, *varargs, *errors;

    string_literals = tcase_create("String Literals");
    tcase_add_test(string_literals, simple_string);
    tcase_add_test(string_literals, escaped_string);
    tcase_add_test(string_literals, single_quote_string);
    tcase_add_test(string_literals, vararg_string);

    number_literals = tcase_create("Number Literals");
    tcase_add_test(number_literals, simple_number);
    tcase_add_test(number_literals, float_number);
    tcase_add_test(number_literals, vararg_number);

    keyword_literals = tcase_create("Keywords");
    tcase_add_test(keyword_literals, keyword_literal);
    dicts = tcase_create("Objects");
    tcase_add_test(dicts, simple_dict);
    lists = tcase_create("Lists");
    tcase_add_test(lists, simple_list);

    whitespace = tcase_create("Whitespace");
    tcase_add_test(whitespace, simple_whitespace);

    varargs = tcase_create("Varargs");
    tcase_add_test(varargs, simple_varargs);

    errors = tcase_create("Invalid JSON");
    tcase_add_test(errors, empty_input);
    tcase_add_test(errors, unterminated_string);
    tcase_add_test(errors, unterminated_escape);
    tcase_add_test(errors, unterminated_sq_string);
    tcase_add_test(errors, unterminated_array);
    tcase_add_test(errors, unterminated_array_comma);
    tcase_add_test(errors, invalid_array_comma);
    tcase_add_test(errors, unterminated_dict);
    tcase_add_test(errors, unterminated_dict_comma);
#if 0
    /* FIXME: this print parse error messages on stderr.  */
    tcase_add_test(errors, invalid_dict_comma);
    tcase_add_test(errors, unterminated_literal);
#endif

    suite = suite_create("QJSON test-suite");
    suite_add_tcase(suite, string_literals);
    suite_add_tcase(suite, number_literals);
    suite_add_tcase(suite, keyword_literals);
    suite_add_tcase(suite, dicts);
    suite_add_tcase(suite, lists);
    suite_add_tcase(suite, whitespace);
    suite_add_tcase(suite, varargs);
    suite_add_tcase(suite, errors);

    return suite;
}

int main(void)
{
    int nf;
    Suite *s;
    SRunner *sr;

    s = qjson_suite();
    sr = srunner_create(s);
        
    srunner_run_all(sr, CK_NORMAL);
    nf = srunner_ntests_failed(sr);
    srunner_free(sr);
    
    return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
