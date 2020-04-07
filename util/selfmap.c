/*
 * Utility function to get QEMU's own process map
 *
 * Copyright (c) 2020 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/selfmap.h"

GSList *read_self_maps(void)
{
    gchar *maps;
    GSList *map_info = NULL;

    if (g_file_get_contents("/proc/self/maps", &maps, NULL, NULL)) {
        gchar **lines = g_strsplit(maps, "\n", 0);
        int i, entries = g_strv_length(lines);

        for (i = 0; i < entries; i++) {
            gchar **fields = g_strsplit(lines[i], " ", 6);
            if (g_strv_length(fields) > 4) {
                MapInfo *e = g_new0(MapInfo, 1);
                int errors;
                const char *end;

                errors  = qemu_strtoul(fields[0], &end, 16, &e->start);
                errors += qemu_strtoul(end + 1, NULL, 16, &e->end);

                e->is_read  = fields[1][0] == 'r';
                e->is_write = fields[1][1] == 'w';
                e->is_exec  = fields[1][2] == 'x';
                e->is_priv  = fields[1][3] == 'p';

                errors += qemu_strtoul(fields[2], NULL, 16, &e->offset);
                e->dev = g_strdup(fields[3]);
                errors += qemu_strtou64(fields[4], NULL, 10, &e->inode);

                /*
                 * The last field may have leading spaces which we
                 * need to strip.
                 */
                if (g_strv_length(fields) == 6) {
                    e->path = g_strdup(g_strchug(fields[5]));
                }
                map_info = g_slist_prepend(map_info, e);
            }

            g_strfreev(fields);
        }
        g_strfreev(lines);
        g_free(maps);
    }

    /* ensure the map data is in the same order we collected it */
    return g_slist_reverse(map_info);
}

/**
 * free_self_maps:
 * @info: a GSlist
 *
 * Free a list of MapInfo structures.
 */
static void free_info(gpointer data)
{
    MapInfo *e = (MapInfo *) data;
    g_free(e->dev);
    g_free(e->path);
    g_free(e);
}

void free_self_maps(GSList *info)
{
    g_slist_free_full(info, &free_info);
}
