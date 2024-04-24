/*
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qapi/qapi-builtin-types.h"

GStrv read_authkeys(const char *path, Error **errp);
bool check_openssh_pub_keys(strList *keys, size_t *nkeys, Error **errp);
bool check_openssh_pub_key(const char *key, Error **errp);
