/*
 * Dealing with identifiers
 *
 * Copyright (C) 2014 Red Hat, Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>,
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1
 * or later.  See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/ctype.h"
#include "qemu/id.h"

bool id_wellformed(const char *id)
{
    int i;

    if (!qemu_isalpha(id[0])) {
        return false;
    }
    for (i = 1; id[i]; i++) {
        if (!qemu_isalnum(id[i]) && !strchr("-._", id[i])) {
            return false;
        }
    }
    return true;
}

#define ID_SPECIAL_CHAR '#'

static const char *const id_subsys_str[ID_MAX] = {
    [ID_QDEV]  = "qdev",
    [ID_BLOCK] = "block",
    [ID_CHR] = "chr",
};

/*
 *  Generates an ID of the form PREFIX SUBSYSTEM NUMBER
 *  where:
 *
 *  - PREFIX is the reserved character '#'
 *  - SUBSYSTEM identifies the subsystem creating the ID
 *  - NUMBER is a decimal number unique within SUBSYSTEM.
 *
 *    Example: "#block146"
 *
 * Note that these IDs do not satisfy id_wellformed().
 *
 * The caller is responsible for freeing the returned string with g_free()
 */
char *id_generate(IdSubSystems id)
{
    static uint64_t id_counters[ID_MAX];
    uint32_t rnd;

    assert(id < ARRAY_SIZE(id_subsys_str));
    assert(id_subsys_str[id]);

    rnd = g_random_int_range(0, 100);

    return g_strdup_printf("%c%s%" PRIu64 "%02" PRId32, ID_SPECIAL_CHAR,
                                                        id_subsys_str[id],
                                                        id_counters[id]++,
                                                        rnd);
}
