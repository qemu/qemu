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
#include "qapi/qmp/qint.h"
#include "qapi/qmp/qfloat.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp/qobject.h"
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
 * qdict_get_obj(): Get a QObject of a specific type
 */
static QObject *qdict_get_obj(const QDict *qdict, const char *key, QType type)
{
    QObject *obj;

    obj = qdict_get(qdict, key);
    assert(obj != NULL);
    assert(qobject_type(obj) == type);

    return obj;
}

/**
 * qdict_get_double(): Get an number mapped by 'key'
 *
 * This function assumes that 'key' exists and it stores a
 * QFloat or QInt object.
 *
 * Return number mapped by 'key'.
 */
double qdict_get_double(const QDict *qdict, const char *key)
{
    QObject *obj = qdict_get(qdict, key);

    assert(obj);
    switch (qobject_type(obj)) {
    case QTYPE_QFLOAT:
        return qfloat_get_double(qobject_to_qfloat(obj));
    case QTYPE_QINT:
        return qint_get_int(qobject_to_qint(obj));
    default:
        abort();
    }
}

/**
 * qdict_get_int(): Get an integer mapped by 'key'
 *
 * This function assumes that 'key' exists and it stores a
 * QInt object.
 *
 * Return integer mapped by 'key'.
 */
int64_t qdict_get_int(const QDict *qdict, const char *key)
{
    return qint_get_int(qobject_to_qint(qdict_get(qdict, key)));
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
 * qdict_get_qlist(): Get the QList mapped by 'key'
 *
 * This function assumes that 'key' exists and it stores a
 * QList object.
 *
 * Return QList mapped by 'key'.
 */
QList *qdict_get_qlist(const QDict *qdict, const char *key)
{
    return qobject_to_qlist(qdict_get_obj(qdict, key, QTYPE_QLIST));
}

/**
 * qdict_get_qdict(): Get the QDict mapped by 'key'
 *
 * This function assumes that 'key' exists and it stores a
 * QDict object.
 *
 * Return QDict mapped by 'key'.
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
 * Return integer mapped by 'key', if it is not present in
 * the dictionary or if the stored object is not of QInt type
 * 'def_value' will be returned.
 */
int64_t qdict_get_try_int(const QDict *qdict, const char *key,
                          int64_t def_value)
{
    QInt *qint = qobject_to_qint(qdict_get(qdict, key));

    return qint ? qint_get_int(qint) : def_value;
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

    qdict_put(dst, key, qstring_from_str(val));
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
