/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QAPI_TYPE_INFO_H
#define QAPI_TYPE_INFO_H

#include "qapi/util.h"

/**
 * QAPITypeInfo - QAPI type metadata
 *
 * @name: QAPI type name (e.g. "str", "OnOffAuto", "int32List").
 *        QOM uses this as the property type string.
 * @masked_name: Name of the type in the output of query-qmp-schema,
 *        assigned by scripts/qapi code generator (e.g. "368").
 * @lookup: QEnumLookup for this type, or NULL for non-enum types.
 */
typedef struct QAPITypeInfo {
    const char *name;
    const char *masked_name;
    const QEnumLookup *lookup;
} QAPITypeInfo;

#endif /* QAPI_TYPE_INFO_H */
