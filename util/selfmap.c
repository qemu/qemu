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

IntervalTreeRoot *read_self_maps(void)
{
    IntervalTreeRoot *root;
    gchar *maps, **lines;
    guint i, nlines;

    if (!g_file_get_contents("/proc/self/maps", &maps, NULL, NULL)) {
        return NULL;
    }

    root = g_new0(IntervalTreeRoot, 1);
    lines = g_strsplit(maps, "\n", 0);
    nlines = g_strv_length(lines);

    for (i = 0; i < nlines; i++) {
        gchar **fields = g_strsplit(lines[i], " ", 6);
        guint nfields = g_strv_length(fields);

        if (nfields > 4) {
            uint64_t start, end, offset, inode;
            int errors = 0;
            const char *p;

            errors |= qemu_strtou64(fields[0], &p, 16, &start);
            errors |= qemu_strtou64(p + 1, NULL, 16, &end);
            errors |= qemu_strtou64(fields[2], NULL, 16, &offset);
            errors |= qemu_strtou64(fields[4], NULL, 10, &inode);

            if (!errors) {
                size_t dev_len, path_len;
                MapInfo *e;

                dev_len = strlen(fields[3]) + 1;
                if (nfields == 6) {
                    p = fields[5];
                    p += strspn(p, " ");
                    path_len = strlen(p) + 1;
                } else {
                    p = NULL;
                    path_len = 0;
                }

                e = g_malloc0(sizeof(*e) + dev_len + path_len);

                e->itree.start = start;
                e->itree.last = end - 1;
                e->offset = offset;
                e->inode = inode;

                e->is_read  = fields[1][0] == 'r';
                e->is_write = fields[1][1] == 'w';
                e->is_exec  = fields[1][2] == 'x';
                e->is_priv  = fields[1][3] == 'p';

                memcpy(e->dev, fields[3], dev_len);
                if (path_len) {
                    e->path = memcpy(e->dev + dev_len, p, path_len);
                }

                interval_tree_insert(&e->itree, root);
            }
        }
        g_strfreev(fields);
    }
    g_strfreev(lines);
    g_free(maps);

    return root;
}

/**
 * free_self_maps:
 * @root: an interval tree
 *
 * Free a tree of MapInfo structures.
 * Since we allocated each MapInfo in one chunk, we need not consider the
 * contents and can simply free each RBNode.
 */

static void free_rbnode(RBNode *n)
{
    if (n) {
        free_rbnode(n->rb_left);
        free_rbnode(n->rb_right);
        g_free(n);
    }
}

void free_self_maps(IntervalTreeRoot *root)
{
    if (root) {
        free_rbnode(root->rb_root.rb_node);
        g_free(root);
    }
}
