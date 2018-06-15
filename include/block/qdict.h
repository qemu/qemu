/*
 * Special QDict functions used by the block layer
 *
 * Copyright (c) 2013-2018 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#ifndef BLOCK_QDICT_H
#define BLOCK_QDICT_H

#include "qapi/qmp/qdict.h"

void qdict_copy_default(QDict *dst, QDict *src, const char *key);
void qdict_set_default_str(QDict *dst, const char *key, const char *val);

void qdict_join(QDict *dest, QDict *src, bool overwrite);

void qdict_extract_subqdict(QDict *src, QDict **dst, const char *start);
void qdict_array_split(QDict *src, QList **dst);
int qdict_array_entries(QDict *src, const char *subqdict);
QObject *qdict_crumple(const QDict *src, Error **errp);
void qdict_flatten(QDict *qdict);

typedef struct QDictRenames {
    const char *from;
    const char *to;
} QDictRenames;
bool qdict_rename_keys(QDict *qdict, const QDictRenames *renames, Error **errp);

Visitor *qobject_input_visitor_new_flat_confused(QDict *qdict,
                                                 Error **errp);
#endif
