/*
 * Utility functions to read our own memory map
 *
 * Copyright (c) 2020 Linaro Ltd
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SELFMAP_H
#define SELFMAP_H

#include "qemu/interval-tree.h"

typedef struct {
    IntervalTreeNode itree;

    /* flags */
    bool is_read;
    bool is_write;
    bool is_exec;
    bool is_priv;

    uint64_t offset;
    uint64_t inode;
    const char *path;
    char dev[];
} MapInfo;

/**
 * read_self_maps:
 *
 * Read /proc/self/maps and return a tree of MapInfo structures.
 */
IntervalTreeRoot *read_self_maps(void);

/**
 * free_self_maps:
 * @info: an interval tree
 *
 * Free a tree of MapInfo structures.
 */
void free_self_maps(IntervalTreeRoot *root);

#endif /* SELFMAP_H */
