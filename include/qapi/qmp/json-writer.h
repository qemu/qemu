/*
 * JSON Writer
 *
 * Copyright (c) 2020 Red Hat Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef JSON_WRITER_H
#define JSON_WRITER_H

JSONWriter *json_writer_new(bool pretty);
const char *json_writer_get(JSONWriter *);
GString *json_writer_get_and_free(JSONWriter *);
void json_writer_free(JSONWriter *);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(JSONWriter, json_writer_free)

void json_writer_start_object(JSONWriter *, const char *name);
void json_writer_end_object(JSONWriter *);
void json_writer_start_array(JSONWriter *, const char *name);
void json_writer_end_array(JSONWriter *);
void json_writer_bool(JSONWriter *, const char *name, bool val);
void json_writer_null(JSONWriter *, const char *name);
void json_writer_int64(JSONWriter *, const char *name, int64_t val);
void json_writer_uint64(JSONWriter *, const char *name, uint64_t val);
void json_writer_double(JSONWriter *, const char *name, double val);
void json_writer_str(JSONWriter *, const char *name, const char *str);

#endif
