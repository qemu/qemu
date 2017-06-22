/*
 * QDict Module
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/qmp/qnum.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp/qobject.h"
#include "qapi/error.h"
#include "qemu/queue.h"
#include "qemu-common.h"
#include "qemu/cutils.h"

/**
 * qdict_new(): Create a new QDict
 *
 * Return strong reference.
 */
QDict *qdict_new(void)
{
    QDict *qdict;

    qdict = g_malloc0(sizeof(*qdict));
    qobject_init(QOBJECT(qdict), QTYPE_QDICT);

    return qdict;
}

/**
 * qobject_to_qdict(): Convert a QObject into a QDict
 */
QDict *qobject_to_qdict(const QObject *obj)
{
    if (!obj || qobject_type(obj) != QTYPE_QDICT) {
        return NULL;
    }
    return container_of(obj, QDict, base);
}

/**
 * tdb_hash(): based on the hash agorithm from gdbm, via tdb
 * (from module-init-tools)
 */
static unsigned int tdb_hash(const char *name)
{
    unsigned value;	/* Used to compute the hash value.  */
    unsigned   i;	/* Used to cycle through random values. */

    /* Set the initial value from the key size. */
    for (value = 0x238F13AF * strlen(name), i=0; name[i]; i++)
        value = (value + (((const unsigned char *)name)[i] << (i*5 % 24)));

    return (1103515243 * value + 12345);
}

/**
 * alloc_entry(): allocate a new QDictEntry
 */
static QDictEntry *alloc_entry(const char *key, QObject *value)
{
    QDictEntry *entry;

    entry = g_malloc0(sizeof(*entry));
    entry->key = g_strdup(key);
    entry->value = value;

    return entry;
}

/**
 * qdict_entry_value(): Return qdict entry value
 *
 * Return weak reference.
 */
QObject *qdict_entry_value(const QDictEntry *entry)
{
    return entry->value;
}

/**
 * qdict_entry_key(): Return qdict entry key
 *
 * Return a *pointer* to the string, it has to be duplicated before being
 * stored.
 */
const char *qdict_entry_key(const QDictEntry *entry)
{
    return entry->key;
}

/**
 * qdict_find(): List lookup function
 */
static QDictEntry *qdict_find(const QDict *qdict,
                              const char *key, unsigned int bucket)
{
    QDictEntry *entry;

    QLIST_FOREACH(entry, &qdict->table[bucket], next)
        if (!strcmp(entry->key, key))
            return entry;

    return NULL;
}

/**
 * qdict_put_obj(): Put a new QObject into the dictionary
 *
 * Insert the pair 'key:value' into 'qdict', if 'key' already exists
 * its 'value' will be replaced.
 *
 * This is done by freeing the reference to the stored QObject and
 * storing the new one in the same entry.
 *
 * NOTE: ownership of 'value' is transferred to the QDict
 */
void qdict_put_obj(QDict *qdict, const char *key, QObject *value)
{
    unsigned int bucket;
    QDictEntry *entry;

    bucket = tdb_hash(key) % QDICT_BUCKET_MAX;
    entry = qdict_find(qdict, key, bucket);
    if (entry) {
        /* replace key's value */
        qobject_decref(entry->value);
        entry->value = value;
    } else {
        /* allocate a new entry */
        entry = alloc_entry(key, value);
        QLIST_INSERT_HEAD(&qdict->table[bucket], entry, next);
        qdict->size++;
    }
}

/**
 * qdict_get(): Lookup for a given 'key'
 *
 * Return a weak reference to the QObject associated with 'key' if
 * 'key' is present in the dictionary, NULL otherwise.
 */
QObject *qdict_get(const QDict *qdict, const char *key)
{
    QDictEntry *entry;

    entry = qdict_find(qdict, key, tdb_hash(key) % QDICT_BUCKET_MAX);
    return (entry == NULL ? NULL : entry->value);
}

/**
 * qdict_haskey(): Check if 'key' exists
 *
 * Return 1 if 'key' exists in the dict, 0 otherwise
 */
int qdict_haskey(const QDict *qdict, const char *key)
{
    unsigned int bucket = tdb_hash(key) % QDICT_BUCKET_MAX;
    return (qdict_find(qdict, key, bucket) == NULL ? 0 : 1);
}

/**
 * qdict_size(): Return the size of the dictionary
 */
size_t qdict_size(const QDict *qdict)
{
    return qdict->size;
}

/**
 * qdict_get_double(): Get an number mapped by 'key'
 *
 * This function assumes that 'key' exists and it stores a QNum.
 *
 * Return number mapped by 'key'.
 */
double qdict_get_double(const QDict *qdict, const char *key)
{
    return qnum_get_double(qobject_to_qnum(qdict_get(qdict, key)));
}

/**
 * qdict_get_int(): Get an integer mapped by 'key'
 *
 * This function assumes that 'key' exists and it stores a
 * QNum representable as int.
 *
 * Return integer mapped by 'key'.
 */
int64_t qdict_get_int(const QDict *qdict, const char *key)
{
    return qnum_get_int(qobject_to_qnum(qdict_get(qdict, key)));
}

/**
 * qdict_get_bool(): Get a bool mapped by 'key'
 *
 * This function assumes that 'key' exists and it stores a
 * QBool object.
 *
 * Return bool mapped by 'key'.
 */
bool qdict_get_bool(const QDict *qdict, const char *key)
{
    return qbool_get_bool(qobject_to_qbool(qdict_get(qdict, key)));
}

/**
 * qdict_get_qlist(): If @qdict maps @key to a QList, return it, else NULL.
 */
QList *qdict_get_qlist(const QDict *qdict, const char *key)
{
    return qobject_to_qlist(qdict_get(qdict, key));
}

/**
 * qdict_get_qdict(): If @qdict maps @key to a QDict, return it, else NULL.
 */
QDict *qdict_get_qdict(const QDict *qdict, const char *key)
{
    return qobject_to_qdict(qdict_get(qdict, key));
}

/**
 * qdict_get_str(): Get a pointer to the stored string mapped
 * by 'key'
 *
 * This function assumes that 'key' exists and it stores a
 * QString object.
 *
 * Return pointer to the string mapped by 'key'.
 */
const char *qdict_get_str(const QDict *qdict, const char *key)
{
    return qstring_get_str(qobject_to_qstring(qdict_get(qdict, key)));
}

/**
 * qdict_get_try_int(): Try to get integer mapped by 'key'
 *
 * Return integer mapped by 'key', if it is not present in the
 * dictionary or if the stored object is not a QNum representing an
 * integer, 'def_value' will be returned.
 */
int64_t qdict_get_try_int(const QDict *qdict, const char *key,
                          int64_t def_value)
{
    QNum *qnum = qobject_to_qnum(qdict_get(qdict, key));
    int64_t val;

    if (!qnum || !qnum_get_try_int(qnum, &val)) {
        return def_value;
    }

    return val;
}

/**
 * qdict_get_try_bool(): Try to get a bool mapped by 'key'
 *
 * Return bool mapped by 'key', if it is not present in the
 * dictionary or if the stored object is not of QBool type
 * 'def_value' will be returned.
 */
bool qdict_get_try_bool(const QDict *qdict, const char *key, bool def_value)
{
    QBool *qbool = qobject_to_qbool(qdict_get(qdict, key));

    return qbool ? qbool_get_bool(qbool) : def_value;
}

/**
 * qdict_get_try_str(): Try to get a pointer to the stored string
 * mapped by 'key'
 *
 * Return a pointer to the string mapped by 'key', if it is not present
 * in the dictionary or if the stored object is not of QString type
 * NULL will be returned.
 */
const char *qdict_get_try_str(const QDict *qdict, const char *key)
{
    QString *qstr = qobject_to_qstring(qdict_get(qdict, key));

    return qstr ? qstring_get_str(qstr) : NULL;
}

/**
 * qdict_iter(): Iterate over all the dictionary's stored values.
 *
 * This function allows the user to provide an iterator, which will be
 * called for each stored value in the dictionary.
 */
void qdict_iter(const QDict *qdict,
                void (*iter)(const char *key, QObject *obj, void *opaque),
                void *opaque)
{
    int i;
    QDictEntry *entry;

    for (i = 0; i < QDICT_BUCKET_MAX; i++) {
        QLIST_FOREACH(entry, &qdict->table[i], next)
            iter(entry->key, entry->value, opaque);
    }
}

static QDictEntry *qdict_next_entry(const QDict *qdict, int first_bucket)
{
    int i;

    for (i = first_bucket; i < QDICT_BUCKET_MAX; i++) {
        if (!QLIST_EMPTY(&qdict->table[i])) {
            return QLIST_FIRST(&qdict->table[i]);
        }
    }

    return NULL;
}

/**
 * qdict_first(): Return first qdict entry for iteration.
 */
const QDictEntry *qdict_first(const QDict *qdict)
{
    return qdict_next_entry(qdict, 0);
}

/**
 * qdict_next(): Return next qdict entry in an iteration.
 */
const QDictEntry *qdict_next(const QDict *qdict, const QDictEntry *entry)
{
    QDictEntry *ret;

    ret = QLIST_NEXT(entry, next);
    if (!ret) {
        unsigned int bucket = tdb_hash(entry->key) % QDICT_BUCKET_MAX;
        ret = qdict_next_entry(qdict, bucket + 1);
    }

    return ret;
}

/**
 * qdict_clone_shallow(): Clones a given QDict. Its entries are not copied, but
 * another reference is added.
 */
QDict *qdict_clone_shallow(const QDict *src)
{
    QDict *dest;
    QDictEntry *entry;
    int i;

    dest = qdict_new();

    for (i = 0; i < QDICT_BUCKET_MAX; i++) {
        QLIST_FOREACH(entry, &src->table[i], next) {
            qobject_incref(entry->value);
            qdict_put_obj(dest, entry->key, entry->value);
        }
    }

    return dest;
}

/**
 * qentry_destroy(): Free all the memory allocated by a QDictEntry
 */
static void qentry_destroy(QDictEntry *e)
{
    assert(e != NULL);
    assert(e->key != NULL);
    assert(e->value != NULL);

    qobject_decref(e->value);
    g_free(e->key);
    g_free(e);
}

/**
 * qdict_del(): Delete a 'key:value' pair from the dictionary
 *
 * This will destroy all data allocated by this entry.
 */
void qdict_del(QDict *qdict, const char *key)
{
    QDictEntry *entry;

    entry = qdict_find(qdict, key, tdb_hash(key) % QDICT_BUCKET_MAX);
    if (entry) {
        QLIST_REMOVE(entry, next);
        qentry_destroy(entry);
        qdict->size--;
    }
}

/**
 * qdict_destroy_obj(): Free all the memory allocated by a QDict
 */
void qdict_destroy_obj(QObject *obj)
{
    int i;
    QDict *qdict;

    assert(obj != NULL);
    qdict = qobject_to_qdict(obj);

    for (i = 0; i < QDICT_BUCKET_MAX; i++) {
        QDictEntry *entry = QLIST_FIRST(&qdict->table[i]);
        while (entry) {
            QDictEntry *tmp = QLIST_NEXT(entry, next);
            QLIST_REMOVE(entry, next);
            qentry_destroy(entry);
            entry = tmp;
        }
    }

    g_free(qdict);
}

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
        qobject_incref(val);
        qdict_put_obj(dst, key, val);
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
        new_key = g_strdup_printf("%s.%i", prefix, i);

        if (qobject_type(value) == QTYPE_QDICT) {
            qdict_flatten_qdict(qobject_to_qdict(value), target, new_key);
        } else if (qobject_type(value) == QTYPE_QLIST) {
            qdict_flatten_qlist(qobject_to_qlist(value), target, new_key);
        } else {
            /* All other types are moved to the target unchanged. */
            qobject_incref(value);
            qdict_put_obj(target, new_key, value);
        }

        g_free(new_key);
    }
}

static void qdict_flatten_qdict(QDict *qdict, QDict *target, const char *prefix)
{
    QObject *value;
    const QDictEntry *entry, *next;
    char *new_key;
    bool delete;

    entry = qdict_first(qdict);

    while (entry != NULL) {

        next = qdict_next(qdict, entry);
        value = qdict_entry_value(entry);
        new_key = NULL;
        delete = false;

        if (prefix) {
            new_key = g_strdup_printf("%s.%s", prefix, entry->key);
        }

        if (qobject_type(value) == QTYPE_QDICT) {
            /* Entries of QDicts are processed recursively, the QDict object
             * itself disappears. */
            qdict_flatten_qdict(qobject_to_qdict(value), target,
                                new_key ? new_key : entry->key);
            delete = true;
        } else if (qobject_type(value) == QTYPE_QLIST) {
            qdict_flatten_qlist(qobject_to_qlist(value), target,
                                new_key ? new_key : entry->key);
            delete = true;
        } else if (prefix) {
            /* All other objects are moved to the target unchanged. */
            qobject_incref(value);
            qdict_put_obj(target, new_key, value);
            delete = true;
        }

        g_free(new_key);

        if (delete) {
            qdict_del(qdict, entry->key);

            /* Restart loop after modifying the iterated QDict */
            entry = qdict_first(qdict);
            continue;
        }

        entry = next;
    }
}

/**
 * qdict_flatten(): For each nested QDict with key x, all fields with key y
 * are moved to this QDict and their key is renamed to "x.y". For each nested
 * QList with key x, the field at index y is moved to this QDict with the key
 * "x.y" (i.e., the reverse of what qdict_array_split() does).
 * This operation is applied recursively for nested QDicts and QLists.
 */
void qdict_flatten(QDict *qdict)
{
    qdict_flatten_qdict(qdict, qdict, NULL);
}

/* extract all the src QDict entries starting by start into dst */
void qdict_extract_subqdict(QDict *src, QDict **dst, const char *start)

{
    const QDictEntry *entry, *next;
    const char *p;

    *dst = qdict_new();
    entry = qdict_first(src);

    while (entry != NULL) {
        next = qdict_next(src, entry);
        if (strstart(entry->key, start, &p)) {
            qobject_incref(entry->value);
            qdict_put_obj(*dst, p, entry->value);
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

        // There may be either a single subordinate object (named "%u") or
        // multiple objects (each with a key prefixed "%u."), but not both.
        if (!subqobj == !is_subqdict) {
            break;
        }

        if (is_subqdict) {
            qdict_extract_subqdict(src, &subqdict, prefix);
            assert(qdict_size(subqdict) > 0);
        } else {
            qobject_incref(subqobj);
            qdict_del(src, indexstr);
        }

        qlist_append_obj(*dst, subqobj ?: QOBJECT(subqdict));
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

        if (qemu_strtoi64(ent->key, NULL, 10, &val) == 0) {
            if (is_list == -1) {
                is_list = 1;
            } else if (!is_list) {
                error_setg(errp,
                           "Cannot mix list and non-list keys");
                return -1;
            }
            len++;
            if (val > max) {
                max = val;
            }
        } else {
            if (is_list == -1) {
                is_list = 0;
            } else if (is_list) {
                error_setg(errp,
                           "Cannot mix list and non-list keys");
                return -1;
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
 * nesting, and values are scalars, and crumples it into a nested
 * structure.
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
    QDict *two_level, *multi_level = NULL;
    QObject *dst = NULL, *child;
    size_t i;
    char *prefix = NULL;
    const char *suffix = NULL;
    int is_list;

    two_level = qdict_new();

    /* Step 1: split our totally flat dict into a two level dict */
    for (ent = qdict_first(src); ent != NULL; ent = qdict_next(src, ent)) {
        if (qobject_type(ent->value) == QTYPE_QDICT ||
            qobject_type(ent->value) == QTYPE_QLIST) {
            error_setg(errp, "Value %s is not a scalar",
                       ent->key);
            goto error;
        }

        qdict_split_flat_key(ent->key, &prefix, &suffix);

        child = qdict_get(two_level, prefix);
        if (suffix) {
            if (child) {
                if (qobject_type(child) != QTYPE_QDICT) {
                    error_setg(errp, "Key %s prefix is already set as a scalar",
                               prefix);
                    goto error;
                }
            } else {
                child = QOBJECT(qdict_new());
                qdict_put_obj(two_level, prefix, child);
            }
            qobject_incref(ent->value);
            qdict_put_obj(qobject_to_qdict(child), suffix, ent->value);
        } else {
            if (child) {
                error_setg(errp, "Key %s prefix is already set as a dict",
                           prefix);
                goto error;
            }
            qobject_incref(ent->value);
            qdict_put_obj(two_level, prefix, ent->value);
        }

        g_free(prefix);
        prefix = NULL;
    }

    /* Step 2: optionally process the two level dict recursively
     * into a multi-level dict */
    multi_level = qdict_new();
    for (ent = qdict_first(two_level); ent != NULL;
         ent = qdict_next(two_level, ent)) {

        if (qobject_type(ent->value) == QTYPE_QDICT) {
            child = qdict_crumple(qobject_to_qdict(ent->value), errp);
            if (!child) {
                goto error;
            }

            qdict_put_obj(multi_level, ent->key, child);
        } else {
            qobject_incref(ent->value);
            qdict_put_obj(multi_level, ent->key, ent->value);
        }
    }
    QDECREF(two_level);
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

            qobject_incref(child);
            qlist_append_obj(qobject_to_qlist(dst), child);
        }
        QDECREF(multi_level);
        multi_level = NULL;
    } else {
        dst = QOBJECT(multi_level);
    }

    return dst;

 error:
    g_free(prefix);
    QDECREF(multi_level);
    QDECREF(two_level);
    qobject_decref(dst);
    return NULL;
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
            qobject_incref(entry->value);
            qdict_put_obj(dest, entry->key, entry->value);
            qdict_del(src, entry->key);
        }

        entry = next;
    }
}
