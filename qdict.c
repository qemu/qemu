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

#include "qint.h"
#include "qfloat.h"
#include "qdict.h"
#include "qbool.h"
#include "qstring.h"
#include "qobject.h"
#include "qemu-queue.h"
#include "qemu-common.h"

static void qdict_destroy_obj(QObject *obj);

static const QType qdict_type = {
    .code = QTYPE_QDICT,
    .destroy = qdict_destroy_obj,
};

/**
 * qdict_new(): Create a new QDict
 *
 * Return strong reference.
 */
QDict *qdict_new(void)
{
    QDict *qdict;

    qdict = g_malloc0(sizeof(*qdict));
    QOBJECT_INIT(qdict, &qdict_type);

    return qdict;
}

/**
 * qobject_to_qdict(): Convert a QObject into a QDict
 */
QDict *qobject_to_qdict(const QObject *obj)
{
    if (qobject_type(obj) != QTYPE_QDICT)
        return NULL;

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
static QObject *qdict_get_obj(const QDict *qdict, const char *key,
                              qtype_code type)
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
    QObject *obj = qdict_get_obj(qdict, key, QTYPE_QINT);
    return qint_get_int(qobject_to_qint(obj));
}

/**
 * qdict_get_bool(): Get a bool mapped by 'key'
 *
 * This function assumes that 'key' exists and it stores a
 * QBool object.
 *
 * Return bool mapped by 'key'.
 */
int qdict_get_bool(const QDict *qdict, const char *key)
{
    QObject *obj = qdict_get_obj(qdict, key, QTYPE_QBOOL);
    return qbool_get_int(qobject_to_qbool(obj));
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
    return qobject_to_qdict(qdict_get_obj(qdict, key, QTYPE_QDICT));
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
    QObject *obj = qdict_get_obj(qdict, key, QTYPE_QSTRING);
    return qstring_get_str(qobject_to_qstring(obj));
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
    QObject *obj;

    obj = qdict_get(qdict, key);
    if (!obj || qobject_type(obj) != QTYPE_QINT)
        return def_value;

    return qint_get_int(qobject_to_qint(obj));
}

/**
 * qdict_get_try_bool(): Try to get a bool mapped by 'key'
 *
 * Return bool mapped by 'key', if it is not present in the
 * dictionary or if the stored object is not of QBool type
 * 'def_value' will be returned.
 */
int qdict_get_try_bool(const QDict *qdict, const char *key, int def_value)
{
    QObject *obj;

    obj = qdict_get(qdict, key);
    if (!obj || qobject_type(obj) != QTYPE_QBOOL)
        return def_value;

    return qbool_get_int(qobject_to_qbool(obj));
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
    QObject *obj;

    obj = qdict_get(qdict, key);
    if (!obj || qobject_type(obj) != QTYPE_QSTRING)
        return NULL;

    return qstring_get_str(qobject_to_qstring(obj));
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
static void qdict_destroy_obj(QObject *obj)
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
