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

#ifndef QDICT_H
#define QDICT_H

#include "qapi/qmp/qobject.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qmp/qnum.h"
#include "qemu/queue.h"

#define QDICT_BUCKET_MAX 512

typedef struct QDictEntry {
    char *key;
    QObject *value;
    QLIST_ENTRY(QDictEntry) next;
} QDictEntry;

typedef struct QDict {
    QObject base;
    size_t size;
    QLIST_HEAD(,QDictEntry) table[QDICT_BUCKET_MAX];
} QDict;

/* Object API */
QDict *qdict_new(void);
const char *qdict_entry_key(const QDictEntry *entry);
QObject *qdict_entry_value(const QDictEntry *entry);
size_t qdict_size(const QDict *qdict);
void qdict_put_obj(QDict *qdict, const char *key, QObject *value);
void qdict_del(QDict *qdict, const char *key);
int qdict_haskey(const QDict *qdict, const char *key);
QObject *qdict_get(const QDict *qdict, const char *key);
QDict *qobject_to_qdict(const QObject *obj);
void qdict_iter(const QDict *qdict,
                void (*iter)(const char *key, QObject *obj, void *opaque),
                void *opaque);
const QDictEntry *qdict_first(const QDict *qdict);
const QDictEntry *qdict_next(const QDict *qdict, const QDictEntry *entry);
void qdict_destroy_obj(QObject *obj);

/* Helper to qdict_put_obj(), accepts any object */
#define qdict_put(qdict, key, obj) \
        qdict_put_obj(qdict, key, QOBJECT(obj))

/* Helpers for int, bool, and string */
#define qdict_put_int(qdict, key, value) \
        qdict_put(qdict, key, qnum_from_int(value))
#define qdict_put_bool(qdict, key, value) \
        qdict_put(qdict, key, qbool_from_bool(value))
#define qdict_put_str(qdict, key, value) \
        qdict_put(qdict, key, qstring_from_str(value))

/* High level helpers */
double qdict_get_double(const QDict *qdict, const char *key);
int64_t qdict_get_int(const QDict *qdict, const char *key);
bool qdict_get_bool(const QDict *qdict, const char *key);
QList *qdict_get_qlist(const QDict *qdict, const char *key);
QDict *qdict_get_qdict(const QDict *qdict, const char *key);
const char *qdict_get_str(const QDict *qdict, const char *key);
int64_t qdict_get_try_int(const QDict *qdict, const char *key,
                          int64_t def_value);
bool qdict_get_try_bool(const QDict *qdict, const char *key, bool def_value);
const char *qdict_get_try_str(const QDict *qdict, const char *key);

void qdict_copy_default(QDict *dst, QDict *src, const char *key);
void qdict_set_default_str(QDict *dst, const char *key, const char *val);

QDict *qdict_clone_shallow(const QDict *src);
void qdict_flatten(QDict *qdict);

void qdict_extract_subqdict(QDict *src, QDict **dst, const char *start);
void qdict_array_split(QDict *src, QList **dst);
int qdict_array_entries(QDict *src, const char *subqdict);
QObject *qdict_crumple(const QDict *src, Error **errp);

void qdict_join(QDict *dest, QDict *src, bool overwrite);

#endif /* QDICT_H */
