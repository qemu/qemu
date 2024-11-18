/*
 * Special QDict functions used by the block layer
 *
 * Copyright (c) 2013-2018 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "block/qdict.h"
#include "qobject/qbool.h"
#include "qobject/qlist.h"
#include "qobject/qnum.h"
#include "qobject/qstring.h"
#include "qapi/qobject-input-visitor.h"
#include "qemu/cutils.h"
#include "qapi/error.h"

/**
 * qdict_copy_default(): If no entry mapped by 'key' exists in 'dst' yet, the
 * value of 'key' in 'src' is copied there (and the refcount increased
 * accordingly).
 */
void qdict_copy_default(QDict *dst, QDict *src, const char *key)
{
    QObject *val;

    if (qdict_haskey(dst, key)) {
        return;
    }

    val = qdict_get(src, key);
    if (val) {
        qdict_put_obj(dst, key, qobject_ref(val));
    }
}

/**
 * qdict_set_default_str(): If no entry mapped by 'key' exists in 'dst' yet, a
 * new QString initialised by 'val' is put there.
 */
void qdict_set_default_str(QDict *dst, const char *key, const char *val)
{
    if (qdict_haskey(dst, key)) {
        return;
    }

    qdict_put_str(dst, key, val);
}

static void qdict_flatten_qdict(QDict *qdict, QDict *target,
                                const char *prefix);

static void qdict_flatten_qlist(QList *qlist, QDict *target, const char *prefix)
{
    QObject *value;
    const QListEntry *entry;
    QDict *dict_val;
    QList *list_val;
    char *new_key;
    int i;

    /* This function is never called with prefix == NULL, i.e., it is always
     * called from within qdict_flatten_q(list|dict)(). Therefore, it does not
     * need to remove list entries during the iteration (the whole list will be
     * deleted eventually anyway from qdict_flatten_qdict()). */
    assert(prefix);

    entry = qlist_first(qlist);

    for (i = 0; entry; entry = qlist_next(entry), i++) {
        value = qlist_entry_obj(entry);
        dict_val = qobject_to(QDict, value);
        list_val = qobject_to(QList, value);
        new_key = g_strdup_printf("%s.%i", prefix, i);

        /*
         * Flatten non-empty QDict and QList recursively into @target,
         * copy other objects to @target
         */
        if (dict_val && qdict_size(dict_val)) {
            qdict_flatten_qdict(dict_val, target, new_key);
        } else if (list_val && !qlist_empty(list_val)) {
            qdict_flatten_qlist(list_val, target, new_key);
        } else {
            qdict_put_obj(target, new_key, qobject_ref(value));
        }

        g_free(new_key);
    }
}

static void qdict_flatten_qdict(QDict *qdict, QDict *target, const char *prefix)
{
    QObject *value;
    const QDictEntry *entry, *next;
    QDict *dict_val;
    QList *list_val;
    char *key, *new_key;

    entry = qdict_first(qdict);

    while (entry != NULL) {
        next = qdict_next(qdict, entry);
        value = qdict_entry_value(entry);
        dict_val = qobject_to(QDict, value);
        list_val = qobject_to(QList, value);

        if (prefix) {
            key = new_key = g_strdup_printf("%s.%s", prefix, entry->key);
        } else {
            key = entry->key;
            new_key = NULL;
        }

        /*
         * Flatten non-empty QDict and QList recursively into @target,
         * copy other objects to @target.
         * On the root level (if @qdict == @target), remove flattened
         * nested QDicts and QLists from @qdict.
         *
         * (Note that we do not need to remove entries from nested
         * dicts or lists.  Their reference count is decremented on
         * the root level, so there are no leaks.  In fact, if they
         * have a reference count greater than one, we are probably
         * well advised not to modify them altogether.)
         */
        if (dict_val && qdict_size(dict_val)) {
            qdict_flatten_qdict(dict_val, target, key);
            if (target == qdict) {
                qdict_del(qdict, entry->key);
            }
        } else if (list_val && !qlist_empty(list_val)) {
            qdict_flatten_qlist(list_val, target, key);
            if (target == qdict) {
                qdict_del(qdict, entry->key);
            }
        } else if (target != qdict) {
            qdict_put_obj(target, key, qobject_ref(value));
        }

        g_free(new_key);
        entry = next;
    }
}

/**
 * qdict_flatten(): For each nested non-empty QDict with key x, all
 * fields with key y are moved to this QDict and their key is renamed
 * to "x.y". For each nested non-empty QList with key x, the field at
 * index y is moved to this QDict with the key "x.y" (i.e., the
 * reverse of what qdict_array_split() does).
 * This operation is applied recursively for nested QDicts and QLists.
 */
void qdict_flatten(QDict *qdict)
{
    qdict_flatten_qdict(qdict, qdict, NULL);
}

/* extract all the src QDict entries starting by start into dst.
 * If dst is NULL then the entries are simply removed from src. */
void qdict_extract_subqdict(QDict *src, QDict **dst, const char *start)

{
    const QDictEntry *entry, *next;
    const char *p;

    if (dst) {
        *dst = qdict_new();
    }
    entry = qdict_first(src);

    while (entry != NULL) {
        next = qdict_next(src, entry);
        if (strstart(entry->key, start, &p)) {
            if (dst) {
                qdict_put_obj(*dst, p, qobject_ref(entry->value));
            }
            qdict_del(src, entry->key);
        }
        entry = next;
    }
}

static int qdict_count_prefixed_entries(const QDict *src, const char *start)
{
    const QDictEntry *entry;
    int count = 0;

    for (entry = qdict_first(src); entry; entry = qdict_next(src, entry)) {
        if (strstart(entry->key, start, NULL)) {
            if (count == INT_MAX) {
                return -ERANGE;
            }
            count++;
        }
    }

    return count;
}

/**
 * qdict_array_split(): This function moves array-like elements of a QDict into
 * a new QList. Every entry in the original QDict with a key "%u" or one
 * prefixed "%u.", where %u designates an unsigned integer starting at 0 and
 * incrementally counting up, will be moved to a new QDict at index %u in the
 * output QList with the key prefix removed, if that prefix is "%u.". If the
 * whole key is just "%u", the whole QObject will be moved unchanged without
 * creating a new QDict. The function terminates when there is no entry in the
 * QDict with a prefix directly (incrementally) following the last one; it also
 * returns if there are both entries with "%u" and "%u." for the same index %u.
 * Example: {"0.a": 42, "0.b": 23, "1.x": 0, "4.y": 1, "o.o": 7, "2": 66}
 *      (or {"1.x": 0, "4.y": 1, "0.a": 42, "o.o": 7, "0.b": 23, "2": 66})
 *       => [{"a": 42, "b": 23}, {"x": 0}, 66]
 *      and {"4.y": 1, "o.o": 7} (remainder of the old QDict)
 */
void qdict_array_split(QDict *src, QList **dst)
{
    unsigned i;

    *dst = qlist_new();

    for (i = 0; i < UINT_MAX; i++) {
        QObject *subqobj;
        bool is_subqdict;
        QDict *subqdict;
        char indexstr[32], prefix[32];
        size_t snprintf_ret;

        snprintf_ret = snprintf(indexstr, 32, "%u", i);
        assert(snprintf_ret < 32);

        subqobj = qdict_get(src, indexstr);

        snprintf_ret = snprintf(prefix, 32, "%u.", i);
        assert(snprintf_ret < 32);

        /* Overflow is the same as positive non-zero results */
        is_subqdict = qdict_count_prefixed_entries(src, prefix);

        /*
         * There may be either a single subordinate object (named
         * "%u") or multiple objects (each with a key prefixed "%u."),
         * but not both.
         */
        if (!subqobj == !is_subqdict) {
            break;
        }

        if (is_subqdict) {
            qdict_extract_subqdict(src, &subqdict, prefix);
            assert(qdict_size(subqdict) > 0);
            qlist_append_obj(*dst, QOBJECT(subqdict));
        } else {
            qobject_ref(subqobj);
            qdict_del(src, indexstr);
            qlist_append_obj(*dst, subqobj);
        }
    }
}

/**
 * qdict_split_flat_key:
 * @key: the key string to split
 * @prefix: non-NULL pointer to hold extracted prefix
 * @suffix: non-NULL pointer to remaining suffix
 *
 * Given a flattened key such as 'foo.0.bar', split it into two parts
 * at the first '.' separator. Allows double dot ('..') to escape the
 * normal separator.
 *
 * e.g.
 *    'foo.0.bar' -> prefix='foo' and suffix='0.bar'
 *    'foo..0.bar' -> prefix='foo.0' and suffix='bar'
 *
 * The '..' sequence will be unescaped in the returned 'prefix'
 * string. The 'suffix' string will be left in escaped format, so it
 * can be fed back into the qdict_split_flat_key() key as the input
 * later.
 *
 * The caller is responsible for freeing the string returned in @prefix
 * using g_free().
 */
static void qdict_split_flat_key(const char *key, char **prefix,
                                 const char **suffix)
{
    const char *separator;
    size_t i, j;

    /* Find first '.' separator, but if there is a pair '..'
     * that acts as an escape, so skip over '..' */
    separator = NULL;
    do {
        if (separator) {
            separator += 2;
        } else {
            separator = key;
        }
        separator = strchr(separator, '.');
    } while (separator && separator[1] == '.');

    if (separator) {
        *prefix = g_strndup(key, separator - key);
        *suffix = separator + 1;
    } else {
        *prefix = g_strdup(key);
        *suffix = NULL;
    }

    /* Unescape the '..' sequence into '.' */
    for (i = 0, j = 0; (*prefix)[i] != '\0'; i++, j++) {
        if ((*prefix)[i] == '.') {
            assert((*prefix)[i + 1] == '.');
            i++;
        }
        (*prefix)[j] = (*prefix)[i];
    }
    (*prefix)[j] = '\0';
}

/**
 * qdict_is_list:
 * @maybe_list: dict to check if keys represent list elements.
 *
 * Determine whether all keys in @maybe_list are valid list elements.
 * If @maybe_list is non-zero in length and all the keys look like
 * valid list indexes, this will return 1. If @maybe_list is zero
 * length or all keys are non-numeric then it will return 0 to indicate
 * it is a normal qdict. If there is a mix of numeric and non-numeric
 * keys, or the list indexes are non-contiguous, an error is reported.
 *
 * Returns: 1 if a valid list, 0 if a dict, -1 on error
 */
static int qdict_is_list(QDict *maybe_list, Error **errp)
{
    const QDictEntry *ent;
    ssize_t len = 0;
    ssize_t max = -1;
    int is_list = -1;
    int64_t val;

    for (ent = qdict_first(maybe_list); ent != NULL;
         ent = qdict_next(maybe_list, ent)) {
        int is_index = !qemu_strtoi64(ent->key, NULL, 10, &val);

        if (is_list == -1) {
            is_list = is_index;
        }

        if (is_index != is_list) {
            error_setg(errp, "Cannot mix list and non-list keys");
            return -1;
        }

        if (is_index) {
            len++;
            if (val > max) {
                max = val;
            }
        }
    }

    if (is_list == -1) {
        assert(!qdict_size(maybe_list));
        is_list = 0;
    }

    /* NB this isn't a perfect check - e.g. it won't catch
     * a list containing '1', '+1', '01', '3', but that
     * does not matter - we've still proved that the
     * input is a list. It is up the caller to do a
     * stricter check if desired */
    if (len != (max + 1)) {
        error_setg(errp, "List indices are not contiguous, "
                   "saw %zd elements but %zd largest index",
                   len, max);
        return -1;
    }

    return is_list;
}

/**
 * qdict_crumple:
 * @src: the original flat dictionary (only scalar values) to crumple
 *
 * Takes a flat dictionary whose keys use '.' separator to indicate
 * nesting, and values are scalars, empty dictionaries or empty lists,
 * and crumples it into a nested structure.
 *
 * To include a literal '.' in a key name, it must be escaped as '..'
 *
 * For example, an input of:
 *
 * { 'foo.0.bar': 'one', 'foo.0.wizz': '1',
 *   'foo.1.bar': 'two', 'foo.1.wizz': '2' }
 *
 * will result in an output of:
 *
 * {
 *   'foo': [
 *      { 'bar': 'one', 'wizz': '1' },
 *      { 'bar': 'two', 'wizz': '2' }
 *   ],
 * }
 *
 * The following scenarios in the input dict will result in an
 * error being returned:
 *
 *  - Any values in @src are non-scalar types
 *  - If keys in @src imply that a particular level is both a
 *    list and a dict. e.g., "foo.0.bar" and "foo.eek.bar".
 *  - If keys in @src imply that a particular level is a list,
 *    but the indices are non-contiguous. e.g. "foo.0.bar" and
 *    "foo.2.bar" without any "foo.1.bar" present.
 *  - If keys in @src represent list indexes, but are not in
 *    the "%zu" format. e.g. "foo.+0.bar"
 *
 * Returns: either a QDict or QList for the nested data structure, or NULL
 * on error
 */
QObject *qdict_crumple(const QDict *src, Error **errp)
{
    const QDictEntry *ent;
    QDict *two_level, *multi_level = NULL, *child_dict;
    QDict *dict_val;
    QList *list_val;
    QObject *dst = NULL, *child;
    size_t i;
    char *prefix = NULL;
    const char *suffix = NULL;
    int is_list;

    two_level = qdict_new();

    /* Step 1: split our totally flat dict into a two level dict */
    for (ent = qdict_first(src); ent != NULL; ent = qdict_next(src, ent)) {
        dict_val = qobject_to(QDict, ent->value);
        list_val = qobject_to(QList, ent->value);
        if ((dict_val && qdict_size(dict_val))
            || (list_val && !qlist_empty(list_val))) {
            error_setg(errp, "Value %s is not flat", ent->key);
            goto error;
        }

        qdict_split_flat_key(ent->key, &prefix, &suffix);
        child = qdict_get(two_level, prefix);
        child_dict = qobject_to(QDict, child);

        if (child) {
            /*
             * If @child_dict, then all previous keys with this prefix
             * had a suffix.  If @suffix, this one has one as well,
             * and we're good, else there's a clash.
             */
            if (!child_dict || !suffix) {
                error_setg(errp, "Cannot mix scalar and non-scalar keys");
                goto error;
            }
        }

        if (suffix) {
            if (!child_dict) {
                child_dict = qdict_new();
                qdict_put(two_level, prefix, child_dict);
            }
            qdict_put_obj(child_dict, suffix, qobject_ref(ent->value));
        } else {
            qdict_put_obj(two_level, prefix, qobject_ref(ent->value));
        }

        g_free(prefix);
        prefix = NULL;
    }

    /* Step 2: optionally process the two level dict recursively
     * into a multi-level dict */
    multi_level = qdict_new();
    for (ent = qdict_first(two_level); ent != NULL;
         ent = qdict_next(two_level, ent)) {
        dict_val = qobject_to(QDict, ent->value);
        if (dict_val && qdict_size(dict_val)) {
            child = qdict_crumple(dict_val, errp);
            if (!child) {
                goto error;
            }

            qdict_put_obj(multi_level, ent->key, child);
        } else {
            qdict_put_obj(multi_level, ent->key, qobject_ref(ent->value));
        }
    }
    qobject_unref(two_level);
    two_level = NULL;

    /* Step 3: detect if we need to turn our dict into list */
    is_list = qdict_is_list(multi_level, errp);
    if (is_list < 0) {
        goto error;
    }

    if (is_list) {
        dst = QOBJECT(qlist_new());

        for (i = 0; i < qdict_size(multi_level); i++) {
            char *key = g_strdup_printf("%zu", i);

            child = qdict_get(multi_level, key);
            g_free(key);

            if (!child) {
                error_setg(errp, "Missing list index %zu", i);
                goto error;
            }

            qlist_append_obj(qobject_to(QList, dst), qobject_ref(child));
        }
        qobject_unref(multi_level);
        multi_level = NULL;
    } else {
        dst = QOBJECT(multi_level);
    }

    return dst;

 error:
    g_free(prefix);
    qobject_unref(multi_level);
    qobject_unref(two_level);
    qobject_unref(dst);
    return NULL;
}

/**
 * qdict_crumple_for_keyval_qiv:
 * @src: the flat dictionary (only scalar values) to crumple
 * @errp: location to store error
 *
 * Like qdict_crumple(), but additionally transforms scalar values so
 * the result can be passed to qobject_input_visitor_new_keyval().
 *
 * The block subsystem uses this function to prepare its flat QDict
 * with possibly confused scalar types for a visit.  It should not be
 * used for anything else, and it should go away once the block
 * subsystem has been cleaned up.
 */
static QObject *qdict_crumple_for_keyval_qiv(QDict *src, Error **errp)
{
    QDict *tmp = NULL;
    char *buf;
    const char *s;
    const QDictEntry *ent;
    QObject *dst;

    for (ent = qdict_first(src); ent; ent = qdict_next(src, ent)) {
        buf = NULL;
        switch (qobject_type(ent->value)) {
        case QTYPE_QNULL:
        case QTYPE_QSTRING:
            continue;
        case QTYPE_QNUM:
            s = buf = qnum_to_string(qobject_to(QNum, ent->value));
            break;
        case QTYPE_QDICT:
        case QTYPE_QLIST:
            /* @src isn't flat; qdict_crumple() will fail */
            continue;
        case QTYPE_QBOOL:
            s = qbool_get_bool(qobject_to(QBool, ent->value))
                ? "on" : "off";
            break;
        default:
            abort();
        }

        if (!tmp) {
            tmp = qdict_clone_shallow(src);
        }
        qdict_put_str(tmp, ent->key, s);
        g_free(buf);
    }

    dst = qdict_crumple(tmp ?: src, errp);
    qobject_unref(tmp);
    return dst;
}

/**
 * qdict_array_entries(): Returns the number of direct array entries if the
 * sub-QDict of src specified by the prefix in subqdict (or src itself for
 * prefix == "") is valid as an array, i.e. the length of the created list if
 * the sub-QDict would become empty after calling qdict_array_split() on it. If
 * the array is not valid, -EINVAL is returned.
 */
int qdict_array_entries(QDict *src, const char *subqdict)
{
    const QDictEntry *entry;
    unsigned i;
    unsigned entries = 0;
    size_t subqdict_len = strlen(subqdict);

    assert(!subqdict_len || subqdict[subqdict_len - 1] == '.');

    /* qdict_array_split() loops until UINT_MAX, but as we want to return
     * negative errors, we only have a signed return value here. Any additional
     * entries will lead to -EINVAL. */
    for (i = 0; i < INT_MAX; i++) {
        QObject *subqobj;
        int subqdict_entries;
        char *prefix = g_strdup_printf("%s%u.", subqdict, i);

        subqdict_entries = qdict_count_prefixed_entries(src, prefix);

        /* Remove ending "." */
        prefix[strlen(prefix) - 1] = 0;
        subqobj = qdict_get(src, prefix);

        g_free(prefix);

        if (subqdict_entries < 0) {
            return subqdict_entries;
        }

        /* There may be either a single subordinate object (named "%u") or
         * multiple objects (each with a key prefixed "%u."), but not both. */
        if (subqobj && subqdict_entries) {
            return -EINVAL;
        } else if (!subqobj && !subqdict_entries) {
            break;
        }

        entries += subqdict_entries ? subqdict_entries : 1;
    }

    /* Consider everything handled that isn't part of the given sub-QDict */
    for (entry = qdict_first(src); entry; entry = qdict_next(src, entry)) {
        if (!strstart(qdict_entry_key(entry), subqdict, NULL)) {
            entries++;
        }
    }

    /* Anything left in the sub-QDict that wasn't handled? */
    if (qdict_size(src) != entries) {
        return -EINVAL;
    }

    return i;
}

/**
 * qdict_join(): Absorb the src QDict into the dest QDict, that is, move all
 * elements from src to dest.
 *
 * If an element from src has a key already present in dest, it will not be
 * moved unless overwrite is true.
 *
 * If overwrite is true, the conflicting values in dest will be discarded and
 * replaced by the corresponding values from src.
 *
 * Therefore, with overwrite being true, the src QDict will always be empty when
 * this function returns. If overwrite is false, the src QDict will be empty
 * iff there were no conflicts.
 */
void qdict_join(QDict *dest, QDict *src, bool overwrite)
{
    const QDictEntry *entry, *next;

    entry = qdict_first(src);
    while (entry) {
        next = qdict_next(src, entry);

        if (overwrite || !qdict_haskey(dest, entry->key)) {
            qdict_put_obj(dest, entry->key, qobject_ref(entry->value));
            qdict_del(src, entry->key);
        }

        entry = next;
    }
}

/**
 * qdict_rename_keys(): Rename keys in qdict according to the replacements
 * specified in the array renames. The array must be terminated by an entry
 * with from = NULL.
 *
 * The renames are performed individually in the order of the array, so entries
 * may be renamed multiple times and may or may not conflict depending on the
 * order of the renames array.
 *
 * Returns true for success, false in error cases.
 */
bool qdict_rename_keys(QDict *qdict, const QDictRenames *renames, Error **errp)
{
    QObject *qobj;

    while (renames->from) {
        if (qdict_haskey(qdict, renames->from)) {
            if (qdict_haskey(qdict, renames->to)) {
                error_setg(errp, "'%s' and its alias '%s' can't be used at the "
                           "same time", renames->to, renames->from);
                return false;
            }

            qobj = qdict_get(qdict, renames->from);
            qdict_put_obj(qdict, renames->to, qobject_ref(qobj));
            qdict_del(qdict, renames->from);
        }

        renames++;
    }
    return true;
}

/*
 * Create a QObject input visitor for flat @qdict with possibly
 * confused scalar types.
 *
 * The block subsystem uses this function to visit its flat QDict with
 * possibly confused scalar types.  It should not be used for anything
 * else, and it should go away once the block subsystem has been
 * cleaned up.
 */
Visitor *qobject_input_visitor_new_flat_confused(QDict *qdict,
                                                 Error **errp)
{
    QObject *crumpled;
    Visitor *v;

    crumpled = qdict_crumple_for_keyval_qiv(qdict, errp);
    if (!crumpled) {
        return NULL;
    }

    v = qobject_input_visitor_new_keyval(crumpled);
    qobject_unref(crumpled);
    return v;
}
