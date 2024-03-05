/*
 * QAPI common helper functions
 *
 * This file provides helper functions related to types defined
 * in the QAPI schema.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qapi/qapi-types-common.h"

HumanReadableText *human_readable_text_from_str(GString *str);

/*
 * Produce and return a NULL-terminated array of strings from @list.
 * The result is g_malloc()'d and all strings are g_strdup()'d.  It
 * can be freed with g_strfreev(), or by g_auto(GStrv) automatic
 * cleanup.
 */
char **strv_from_str_list(const strList *list);
