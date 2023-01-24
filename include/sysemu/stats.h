/*
 * Copyright (c) 2022 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#ifndef STATS_H
#define STATS_H

#include "qapi/qapi-types-stats.h"

typedef void StatRetrieveFunc(StatsResultList **result, StatsTarget target,
                              strList *names, strList *targets, Error **errp);
typedef void SchemaRetrieveFunc(StatsSchemaList **result, Error **errp);

/*
 * Register callbacks for the QMP query-stats command.
 *
 * @provider: stats provider checked against QMP command arguments
 * @stats_fn: routine to query stats:
 * @schema_fn: routine to query stat schemas:
 */
void add_stats_callbacks(StatsProvider provider,
                         StatRetrieveFunc *stats_fn,
                         SchemaRetrieveFunc *schemas_fn);

/*
 * Helper routines for adding stats entries to the results lists.
 */
void add_stats_entry(StatsResultList **, StatsProvider, const char *id,
                     StatsList *stats_list);
void add_stats_schema(StatsSchemaList **, StatsProvider, StatsTarget,
                      StatsSchemaValueList *);

/*
 * True if a string matches the filter passed to the stats_fn callabck,
 * false otherwise.
 *
 * Note that an empty list means no filtering, i.e. all strings will
 * return true.
 */
bool apply_str_list_filter(const char *string, strList *list);

#endif /* STATS_H */
