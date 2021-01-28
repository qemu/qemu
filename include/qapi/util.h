/*
 * QAPI util functions
 *
 * Copyright Fujitsu, Inc. 2014
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef QAPI_UTIL_H
#define QAPI_UTIL_H

typedef struct QEnumLookup {
    const char *const *array;
    int size;
} QEnumLookup;

const char *qapi_enum_lookup(const QEnumLookup *lookup, int val);
int qapi_enum_parse(const QEnumLookup *lookup, const char *buf,
                    int def, Error **errp);
bool qapi_bool_parse(const char *name, const char *value, bool *obj,
                     Error **errp);

int parse_qapi_name(const char *name, bool complete);

/*
 * For any GenericList @list, insert @element at the front.
 *
 * Note that this macro evaluates @element exactly once, so it is safe
 * to have side-effects with that argument.
 */
#define QAPI_LIST_PREPEND(list, element) do { \
    typeof(list) _tmp = g_malloc(sizeof(*(list))); \
    _tmp->value = (element); \
    _tmp->next = (list); \
    (list) = _tmp; \
} while (0)

#endif
