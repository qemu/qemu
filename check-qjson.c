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
#include <glib.h>

#include "qstring.h"
#include "qint.h"
#include "qdict.h"
#include "qlist.h"
#include "qfloat.h"
#include "qbool.h"
#include "qjson.h"

#include "qemu-common.h"

static void escaped_string(void)
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

        g_assert(obj != NULL);
        g_assert(qobject_type(obj) == QTYPE_QSTRING);
        
        str = qobject_to_qstring(obj);
        g_assert_cmpstr(qstring_get_str(str), ==, test_cases[i].decoded);

        if (test_cases[i].skip == 0) {
            str = qobject_to_json(obj);
            g_assert_cmpstr(qstring_get_str(str), ==, test_cases[i].encoded);
            qobject_decref(obj);
        }

        QDECREF(str);
    }
}

static void simple_string(void)
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

        g_assert(obj != NULL);
        g_assert(qobject_type(obj) == QTYPE_QSTRING);
        
        str = qobject_to_qstring(obj);
        g_assert(strcmp(qstring_get_str(str), test_cases[i].decoded) == 0);

        str = qobject_to_json(obj);
        g_assert(strcmp(qstring_get_str(str), test_cases[i].encoded) == 0);

        qobject_decref(obj);
        
        QDECREF(str);
    }
}

static void single_quote_string(void)
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

        g_assert(obj != NULL);
        g_assert(qobject_type(obj) == QTYPE_QSTRING);
        
        str = qobject_to_qstring(obj);
        g_assert(strcmp(qstring_get_str(str), test_cases[i].decoded) == 0);

        QDECREF(str);
    }
}

static void vararg_string(void)
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

        g_assert(obj != NULL);
        g_assert(qobject_type(obj) == QTYPE_QSTRING);
        
        str = qobject_to_qstring(obj);
        g_assert(strcmp(qstring_get_str(str), test_cases[i].decoded) == 0);

        QDECREF(str);
    }
}

static void simple_number(void)
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
        g_assert(obj != NULL);
        g_assert(qobject_type(obj) == QTYPE_QINT);

        qint = qobject_to_qint(obj);
        g_assert(qint_get_int(qint) == test_cases[i].decoded);
        if (test_cases[i].skip == 0) {
            QString *str;

            str = qobject_to_json(obj);
            g_assert(strcmp(qstring_get_str(str), test_cases[i].encoded) == 0);
            QDECREF(str);
        }

        QDECREF(qint);
    }
}

static void float_number(void)
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
        g_assert(obj != NULL);
        g_assert(qobject_type(obj) == QTYPE_QFLOAT);

        qfloat = qobject_to_qfloat(obj);
        g_assert(qfloat_get_double(qfloat) == test_cases[i].decoded);

        if (test_cases[i].skip == 0) {
            QString *str;

            str = qobject_to_json(obj);
            g_assert(strcmp(qstring_get_str(str), test_cases[i].encoded) == 0);
            QDECREF(str);
        }

        QDECREF(qfloat);
    }
}

static void vararg_number(void)
{
    QObject *obj;
    QInt *qint;
    QFloat *qfloat;
    int value = 0x2342;
    int64_t value64 = 0x2342342343LL;
    double valuef = 2.323423423;

    obj = qobject_from_jsonf("%d", value);
    g_assert(obj != NULL);
    g_assert(qobject_type(obj) == QTYPE_QINT);

    qint = qobject_to_qint(obj);
    g_assert(qint_get_int(qint) == value);

    QDECREF(qint);

    obj = qobject_from_jsonf("%" PRId64, value64);
    g_assert(obj != NULL);
    g_assert(qobject_type(obj) == QTYPE_QINT);

    qint = qobject_to_qint(obj);
    g_assert(qint_get_int(qint) == value64);

    QDECREF(qint);

    obj = qobject_from_jsonf("%f", valuef);
    g_assert(obj != NULL);
    g_assert(qobject_type(obj) == QTYPE_QFLOAT);

    qfloat = qobject_to_qfloat(obj);
    g_assert(qfloat_get_double(qfloat) == valuef);

    QDECREF(qfloat);
}

static void keyword_literal(void)
{
    QObject *obj;
    QBool *qbool;
    QString *str;

    obj = qobject_from_json("true");
    g_assert(obj != NULL);
    g_assert(qobject_type(obj) == QTYPE_QBOOL);

    qbool = qobject_to_qbool(obj);
    g_assert(qbool_get_int(qbool) != 0);

    str = qobject_to_json(obj);
    g_assert(strcmp(qstring_get_str(str), "true") == 0);
    QDECREF(str);

    QDECREF(qbool);

    obj = qobject_from_json("false");
    g_assert(obj != NULL);
    g_assert(qobject_type(obj) == QTYPE_QBOOL);

    qbool = qobject_to_qbool(obj);
    g_assert(qbool_get_int(qbool) == 0);

    str = qobject_to_json(obj);
    g_assert(strcmp(qstring_get_str(str), "false") == 0);
    QDECREF(str);

    QDECREF(qbool);

    obj = qobject_from_jsonf("%i", false);
    g_assert(obj != NULL);
    g_assert(qobject_type(obj) == QTYPE_QBOOL);

    qbool = qobject_to_qbool(obj);
    g_assert(qbool_get_int(qbool) == 0);

    QDECREF(qbool);
    
    obj = qobject_from_jsonf("%i", true);
    g_assert(obj != NULL);
    g_assert(qobject_type(obj) == QTYPE_QBOOL);

    qbool = qobject_to_qbool(obj);
    g_assert(qbool_get_int(qbool) != 0);

    QDECREF(qbool);
}

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

static void simple_dict(void)
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
        g_assert(obj != NULL);
        g_assert(qobject_type(obj) == QTYPE_QDICT);

        g_assert(compare_litqobj_to_qobj(&test_cases[i].decoded, obj) == 1);

        str = qobject_to_json(obj);
        qobject_decref(obj);

        obj = qobject_from_json(qstring_get_str(str));
        g_assert(obj != NULL);
        g_assert(qobject_type(obj) == QTYPE_QDICT);

        g_assert(compare_litqobj_to_qobj(&test_cases[i].decoded, obj) == 1);
        qobject_decref(obj);
        QDECREF(str);
    }
}

static void simple_list(void)
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
        g_assert(obj != NULL);
        g_assert(qobject_type(obj) == QTYPE_QLIST);

        g_assert(compare_litqobj_to_qobj(&test_cases[i].decoded, obj) == 1);

        str = qobject_to_json(obj);
        qobject_decref(obj);

        obj = qobject_from_json(qstring_get_str(str));
        g_assert(obj != NULL);
        g_assert(qobject_type(obj) == QTYPE_QLIST);

        g_assert(compare_litqobj_to_qobj(&test_cases[i].decoded, obj) == 1);
        qobject_decref(obj);
        QDECREF(str);
    }
}

static void simple_whitespace(void)
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
        g_assert(obj != NULL);
        g_assert(qobject_type(obj) == QTYPE_QLIST);

        g_assert(compare_litqobj_to_qobj(&test_cases[i].decoded, obj) == 1);

        str = qobject_to_json(obj);
        qobject_decref(obj);

        obj = qobject_from_json(qstring_get_str(str));
        g_assert(obj != NULL);
        g_assert(qobject_type(obj) == QTYPE_QLIST);

        g_assert(compare_litqobj_to_qobj(&test_cases[i].decoded, obj) == 1);

        qobject_decref(obj);
        QDECREF(str);
    }
}

static void simple_varargs(void)
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
    g_assert(embedded_obj != NULL);

    obj = qobject_from_jsonf("[%d, 2, %p]", 1, embedded_obj);
    g_assert(obj != NULL);

    g_assert(compare_litqobj_to_qobj(&decoded, obj) == 1);

    qobject_decref(obj);
}

static void empty_input(void)
{
    const char *empty = "";

    QObject *obj = qobject_from_json(empty);
    g_assert(obj == NULL);
}

static void unterminated_string(void)
{
    QObject *obj = qobject_from_json("\"abc");
    g_assert(obj == NULL);
}

static void unterminated_sq_string(void)
{
    QObject *obj = qobject_from_json("'abc");
    g_assert(obj == NULL);
}

static void unterminated_escape(void)
{
    QObject *obj = qobject_from_json("\"abc\\\"");
    g_assert(obj == NULL);
}

static void unterminated_array(void)
{
    QObject *obj = qobject_from_json("[32");
    g_assert(obj == NULL);
}

static void unterminated_array_comma(void)
{
    QObject *obj = qobject_from_json("[32,");
    g_assert(obj == NULL);
}

static void invalid_array_comma(void)
{
    QObject *obj = qobject_from_json("[32,}");
    g_assert(obj == NULL);
}

static void unterminated_dict(void)
{
    QObject *obj = qobject_from_json("{'abc':32");
    g_assert(obj == NULL);
}

static void unterminated_dict_comma(void)
{
    QObject *obj = qobject_from_json("{'abc':32,");
    g_assert(obj == NULL);
}

static void invalid_dict_comma(void)
{
    QObject *obj = qobject_from_json("{'abc':32,}");
    g_assert(obj == NULL);
}

static void unterminated_literal(void)
{
    QObject *obj = qobject_from_json("nul");
    g_assert(obj == NULL);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/literals/string/simple", simple_string);
    g_test_add_func("/literals/string/escaped", escaped_string);
    g_test_add_func("/literals/string/single_quote", single_quote_string);
    g_test_add_func("/literals/string/vararg", vararg_string);

    g_test_add_func("/literals/number/simple", simple_number);
    g_test_add_func("/literals/number/float", float_number);
    g_test_add_func("/literals/number/vararg", vararg_number);

    g_test_add_func("/literals/keyword", keyword_literal);

    g_test_add_func("/dicts/simple_dict", simple_dict);
    g_test_add_func("/lists/simple_list", simple_list);

    g_test_add_func("/whitespace/simple_whitespace", simple_whitespace);

    g_test_add_func("/varargs/simple_varargs", simple_varargs);

    g_test_add_func("/errors/empty_input", empty_input);
    g_test_add_func("/errors/unterminated/string", unterminated_string);
    g_test_add_func("/errors/unterminated/escape", unterminated_escape);
    g_test_add_func("/errors/unterminated/sq_string", unterminated_sq_string);
    g_test_add_func("/errors/unterminated/array", unterminated_array);
    g_test_add_func("/errors/unterminated/array_comma", unterminated_array_comma);
    g_test_add_func("/errors/unterminated/dict", unterminated_dict);
    g_test_add_func("/errors/unterminated/dict_comma", unterminated_dict_comma);
    g_test_add_func("/errors/invalid_array_comma", invalid_array_comma);
    g_test_add_func("/errors/invalid_dict_comma", invalid_dict_comma);
    g_test_add_func("/errors/unterminated/literal", unterminated_literal);

    return g_test_run();
}
