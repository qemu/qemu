/*
 * QEMU JSON writer
 *
 * Copyright Alexander Graf
 *
 * Authors:
 *  Alexander Graf <agraf@suse.de>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */
#ifndef QEMU_QJSON_H
#define QEMU_QJSON_H

typedef struct QJSON QJSON;

QJSON *qjson_new(void);
void qjson_destroy(QJSON *json);
void json_prop_str(QJSON *json, const char *name, const char *str);
void json_prop_int(QJSON *json, const char *name, int64_t val);
void json_end_array(QJSON *json);
void json_start_array(QJSON *json, const char *name);
void json_end_object(QJSON *json);
void json_start_object(QJSON *json, const char *name);
const char *qjson_get_str(QJSON *json);
void qjson_finish(QJSON *json);

#endif /* QEMU_QJSON_H */
