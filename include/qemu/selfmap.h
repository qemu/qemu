/*
 * Utility functions to read our own memory map
 *
 * Copyright (c) 2020 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SELFMAP_H
#define SELFMAP_H

typedef struct {
    unsigned long start;
    unsigned long end;

    /* flags */
    bool is_read;
    bool is_write;
    bool is_exec;
    bool is_priv;

    unsigned long offset;
    gchar *dev;
    uint64_t inode;
    gchar *path;
} MapInfo;


/**
 * read_self_maps:
 *
 * Read /proc/self/maps and return a list of MapInfo structures.
 */
GSList *read_self_maps(void);

/**
 * free_self_maps:
 * @info: a GSlist
 *
 * Free a list of MapInfo structures.
 */
void free_self_maps(GSList *info);

#endif /* SELFMAP_H */
