/*
 * Parsing KEY=VALUE,... strings
 *
 * Copyright (C) 2017 Red Hat Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>,
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

/*
 * KEY=VALUE,... syntax:
 *
 *   key-vals     = [ key-val { ',' key-val } [ ',' ] ]
 *   key-val      = key '=' val
 *   key          = key-fragment { '.' key-fragment }
 *   key-fragment = / [^=,.]* /
 *   val          = { / [^,]* / | ',,' }
 *
 * Semantics defined by reduction to JSON:
 *
 *   key-vals defines a tree of objects rooted at R
 *   where for each key-val = key-fragment . ... = val in key-vals
 *       R op key-fragment op ... = val'
 *       where (left-associative) op is member reference L.key-fragment
 *             val' is val with ',,' replaced by ','
 *   and only R may be empty.
 *
 *   Duplicate keys are permitted; all but the last one are ignored.
 *
 *   The equations must have a solution.  Counter-example: a.b=1,a=2
 *   doesn't have one, because R.a must be an object to satisfy a.b=1
 *   and a string to satisfy a=2.
 *
 * Key-fragments must be valid QAPI names.
 *
 * The length of any key-fragment must be between 1 and 127.
 *
 * Design flaw: there is no way to denote an empty non-root object.
 * While interpreting "key absent" as empty object seems natural
 * (removing a key-val from the input string removes the member when
 * there are more, so why not when it's the last), it doesn't work:
 * "key absent" already means "optional object absent", which isn't
 * the same as "empty object present".
 *
 * Additional syntax for use with an implied key:
 *
 *   key-vals-ik  = val-no-key [ ',' key-vals ]
 *   val-no-key   = / [^=,]* /
 *
 * where no-key is syntactic sugar for implied-key=val-no-key.
 *
 * TODO support lists
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qmp/qstring.h"
#include "qapi/util.h"
#include "qemu/option.h"

/*
 * Ensure @cur maps @key_in_cur the right way.
 * If @value is null, it needs to map to a QDict, else to this
 * QString.
 * If @cur doesn't have @key_in_cur, put an empty QDict or @value,
 * respectively.
 * Else, if it needs to map to a QDict, and already does, do nothing.
 * Else, if it needs to map to this QString, and already maps to a
 * QString, replace it by @value.
 * Else, fail because we have conflicting needs on how to map
 * @key_in_cur.
 * In any case, take over the reference to @value, i.e. if the caller
 * wants to hold on to a reference, it needs to QINCREF().
 * Use @key up to @key_cursor to identify the key in error messages.
 * On success, return the mapped value.
 * On failure, store an error through @errp and return NULL.
 */
static QObject *keyval_parse_put(QDict *cur,
                                 const char *key_in_cur, QString *value,
                                 const char *key, const char *key_cursor,
                                 Error **errp)
{
    QObject *old, *new;

    old = qdict_get(cur, key_in_cur);
    if (old) {
        if (qobject_type(old) != (value ? QTYPE_QSTRING : QTYPE_QDICT)) {
            error_setg(errp, "Parameters '%.*s.*' used inconsistently",
                       (int)(key_cursor - key), key);
            QDECREF(value);
            return NULL;
        }
        if (!value) {
            return old;         /* already QDict, do nothing */
        }
        new = QOBJECT(value);   /* replacement */
    } else {
        new = value ? QOBJECT(value) : QOBJECT(qdict_new());
    }
    qdict_put_obj(cur, key_in_cur, new);
    return new;
}

/*
 * Parse one KEY=VALUE from @params, store result in @qdict.
 * The first fragment of KEY applies to @qdict.  Subsequent fragments
 * apply to nested QDicts, which are created on demand.  @implied_key
 * is as in keyval_parse().
 * On success, return a pointer to the next KEY=VALUE, or else to '\0'.
 * On failure, return NULL.
 */
static const char *keyval_parse_one(QDict *qdict, const char *params,
                                    const char *implied_key,
                                    Error **errp)
{
    const char *key, *key_end, *s;
    size_t len;
    char key_in_cur[128];
    QDict *cur;
    int ret;
    QObject *next;
    QString *val;

    key = params;
    len = strcspn(params, "=,");
    if (implied_key && len && key[len] != '=') {
        /* Desugar implied key */
        key = implied_key;
        len = strlen(implied_key);
    }
    key_end = key + len;

    /*
     * Loop over key fragments: @s points to current fragment, it
     * applies to @cur.  @key_in_cur[] holds the previous fragment.
     */
    cur = qdict;
    s = key;
    for (;;) {
        ret = parse_qapi_name(s, false);
        len = ret < 0 ? 0 : ret;
        assert(s + len <= key_end);
        if (!len || (s + len < key_end && s[len] != '.')) {
            assert(key != implied_key);
            error_setg(errp, "Invalid parameter '%.*s'",
                       (int)(key_end - key), key);
            return NULL;
        }
        if (len >= sizeof(key_in_cur)) {
            assert(key != implied_key);
            error_setg(errp, "Parameter%s '%.*s' is too long",
                       s != key || s + len != key_end ? " fragment" : "",
                       (int)len, s);
            return NULL;
        }

        if (s != key) {
            next = keyval_parse_put(cur, key_in_cur, NULL,
                                    key, s - 1, errp);
            if (!next) {
                return NULL;
            }
            cur = qobject_to_qdict(next);
            assert(cur);
        }

        memcpy(key_in_cur, s, len);
        key_in_cur[len] = 0;
        s += len;

        if (*s != '.') {
            break;
        }
        s++;
    }

    if (key == implied_key) {
        assert(!*s);
        s = params;
    } else {
        if (*s != '=') {
            error_setg(errp, "Expected '=' after parameter '%.*s'",
                       (int)(s - key), key);
            return NULL;
        }
        s++;
    }

    val = qstring_new();
    for (;;) {
        if (!*s) {
            break;
        } else if (*s == ',') {
            s++;
            if (*s != ',') {
                break;
            }
        }
        qstring_append_chr(val, *s++);
    }

    if (!keyval_parse_put(cur, key_in_cur, val, key, key_end, errp)) {
        return NULL;
    }
    return s;
}

/*
 * Parse @params in QEMU's traditional KEY=VALUE,... syntax.
 * If @implied_key, the first KEY= can be omitted.  @implied_key is
 * implied then, and VALUE can't be empty or contain ',' or '='.
 * On success, return a dictionary of the parsed keys and values.
 * On failure, store an error through @errp and return NULL.
 */
QDict *keyval_parse(const char *params, const char *implied_key,
                    Error **errp)
{
    QDict *qdict = qdict_new();
    const char *s;

    s = params;
    while (*s) {
        s = keyval_parse_one(qdict, s, implied_key, errp);
        if (!s) {
            QDECREF(qdict);
            return NULL;
        }
        implied_key = NULL;
    }

    return qdict;
}
