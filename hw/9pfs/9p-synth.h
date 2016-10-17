/*
 * 9p
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_9P_SYNTH_H
#define QEMU_9P_SYNTH_H

typedef struct V9fsSynthNode V9fsSynthNode;
typedef ssize_t (*v9fs_synth_read)(void *buf, int len, off_t offset,
                                   void *arg);
typedef ssize_t (*v9fs_synth_write)(void *buf, int len, off_t offset,
                                    void *arg);
typedef struct V9fsSynthNodeAttr {
    int mode;
    int inode;
    int nlink;
    v9fs_synth_read read;
    v9fs_synth_write write;
} V9fsSynthNodeAttr;

struct V9fsSynthNode {
    QLIST_HEAD(, V9fsSynthNode) child;
    QLIST_ENTRY(V9fsSynthNode) sibling;
    char name[NAME_MAX];
    V9fsSynthNodeAttr *attr;
    V9fsSynthNodeAttr actual_attr;
    void *private;
    int open_count;
};

typedef struct V9fsSynthOpenState {
    off_t offset;
    V9fsSynthNode *node;
    struct dirent dent;
} V9fsSynthOpenState;

int qemu_v9fs_synth_mkdir(V9fsSynthNode *parent, int mode,
                          const char *name, V9fsSynthNode **result);
int qemu_v9fs_synth_add_file(V9fsSynthNode *parent, int mode,
                             const char *name, v9fs_synth_read read,
                             v9fs_synth_write write, void *arg);

#endif
