/*
 * 9p synthetic file system support
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Malahal Naineni <malahal@us.ibm.com>
 *  Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

/*
 * Not so fast! You might want to read the 9p developer docs first:
 * https://wiki.qemu.org/Documentation/9p
 */

#include "qemu/osdep.h"
#include "9p.h"
#include "fsdev/qemu-fsdev.h"
#include "9p-synth.h"
#include "qemu/rcu.h"
#include "qemu/rcu_queue.h"
#include "qemu/cutils.h"
#include "sysemu/qtest.h"

/* Root node for synth file system */
static V9fsSynthNode synth_root = {
    .name = "/",
    .actual_attr = {
        .mode = 0555 | S_IFDIR,
        .nlink = 1,
    },
    .attr = &synth_root.actual_attr,
};

static QemuMutex  synth_mutex;
static int synth_node_count;
/* set to 1 when the synth fs is ready */
static int synth_fs;

static V9fsSynthNode *v9fs_add_dir_node(V9fsSynthNode *parent, int mode,
                                        const char *name,
                                        V9fsSynthNodeAttr *attr, int inode)
{
    V9fsSynthNode *node;

    /* Add directory type and remove write bits */
    mode = ((mode & 0777) | S_IFDIR) & ~(S_IWUSR | S_IWGRP | S_IWOTH);
    node = g_new0(V9fsSynthNode, 1);
    if (attr) {
        /* We are adding .. or . entries */
        node->attr = attr;
        node->attr->nlink++;
    } else {
        node->attr = &node->actual_attr;
        node->attr->inode = inode;
        node->attr->nlink = 1;
        /* We don't allow write to directories */
        node->attr->mode   = mode;
        node->attr->write = NULL;
        node->attr->read  = NULL;
    }
    node->private = node;
    pstrcpy(node->name, sizeof(node->name), name);
    QLIST_INSERT_HEAD_RCU(&parent->child, node, sibling);
    return node;
}

int qemu_v9fs_synth_mkdir(V9fsSynthNode *parent, int mode,
                          const char *name, V9fsSynthNode **result)
{
    V9fsSynthNode *node, *tmp;

    if (!synth_fs) {
        return -EAGAIN;
    }
    if (!name || (strlen(name) >= NAME_MAX)) {
        return -EINVAL;
    }
    if (!parent) {
        parent = &synth_root;
    }
    QEMU_LOCK_GUARD(&synth_mutex);
    QLIST_FOREACH(tmp, &parent->child, sibling) {
        if (!strcmp(tmp->name, name)) {
            return -EEXIST;
        }
    }
    /* Add the name */
    node = v9fs_add_dir_node(parent, mode, name, NULL, ++synth_node_count);
    v9fs_add_dir_node(node, parent->attr->mode, "..",
                      parent->attr, parent->attr->inode);
    v9fs_add_dir_node(node, node->attr->mode, ".",
                      node->attr, node->attr->inode);
    *result = node;
    return 0;
}

int qemu_v9fs_synth_add_file(V9fsSynthNode *parent, int mode,
                             const char *name, v9fs_synth_read read,
                             v9fs_synth_write write, void *arg)
{
    V9fsSynthNode *node, *tmp;

    if (!synth_fs) {
        return -EAGAIN;
    }
    if (!name || (strlen(name) >= NAME_MAX)) {
        return -EINVAL;
    }
    if (!parent) {
        parent = &synth_root;
    }

    QEMU_LOCK_GUARD(&synth_mutex);
    QLIST_FOREACH(tmp, &parent->child, sibling) {
        if (!strcmp(tmp->name, name)) {
            return -EEXIST;
        }
    }
    /* Add file type and remove write bits */
    mode = ((mode & 0777) | S_IFREG);
    node = g_new0(V9fsSynthNode, 1);
    node->attr         = &node->actual_attr;
    node->attr->inode  = ++synth_node_count;
    node->attr->nlink  = 1;
    node->attr->read   = read;
    node->attr->write  = write;
    node->attr->mode   = mode;
    node->private      = arg;
    pstrcpy(node->name, sizeof(node->name), name);
    QLIST_INSERT_HEAD_RCU(&parent->child, node, sibling);
    return 0;
}

static void synth_fill_statbuf(V9fsSynthNode *node, struct stat *stbuf)
{
    stbuf->st_dev = 0;
    stbuf->st_ino = node->attr->inode;
    stbuf->st_mode = node->attr->mode;
    stbuf->st_nlink = node->attr->nlink;
    stbuf->st_uid = 0;
    stbuf->st_gid = 0;
    stbuf->st_rdev = 0;
    stbuf->st_size = 0;
    stbuf->st_blksize = 0;
    stbuf->st_blocks = 0;
    stbuf->st_atime = 0;
    stbuf->st_mtime = 0;
    stbuf->st_ctime = 0;
}

static int synth_lstat(FsContext *fs_ctx,
                            V9fsPath *fs_path, struct stat *stbuf)
{
    V9fsSynthNode *node = *(V9fsSynthNode **)fs_path->data;

    synth_fill_statbuf(node, stbuf);
    return 0;
}

static int synth_fstat(FsContext *fs_ctx, int fid_type,
                            V9fsFidOpenState *fs, struct stat *stbuf)
{
    V9fsSynthOpenState *synth_open = fs->private;
    synth_fill_statbuf(synth_open->node, stbuf);
    return 0;
}

static int synth_opendir(FsContext *ctx,
                             V9fsPath *fs_path, V9fsFidOpenState *fs)
{
    V9fsSynthOpenState *synth_open;
    V9fsSynthNode *node = *(V9fsSynthNode **)fs_path->data;

    /*
     * V9fsSynthOpenState contains 'struct dirent' which have OS-specific
     * properties, thus it's zero cleared on allocation here and below
     * in synth_open.
     */
    synth_open = g_new0(V9fsSynthOpenState, 1);
    synth_open->node = node;
    node->open_count++;
    fs->private = synth_open;
    return 0;
}

static int synth_closedir(FsContext *ctx, V9fsFidOpenState *fs)
{
    V9fsSynthOpenState *synth_open = fs->private;
    V9fsSynthNode *node = synth_open->node;

    node->open_count--;
    g_free(synth_open);
    fs->private = NULL;
    return 0;
}

static off_t synth_telldir(FsContext *ctx, V9fsFidOpenState *fs)
{
    V9fsSynthOpenState *synth_open = fs->private;
    return synth_open->offset;
}

static void synth_seekdir(FsContext *ctx, V9fsFidOpenState *fs, off_t off)
{
    V9fsSynthOpenState *synth_open = fs->private;
    synth_open->offset = off;
}

static void synth_rewinddir(FsContext *ctx, V9fsFidOpenState *fs)
{
    synth_seekdir(ctx, fs, 0);
}

static void synth_direntry(V9fsSynthNode *node,
                                struct dirent *entry, off_t off)
{
    size_t sz = strlen(node->name) + 1;
    /*
     * 'entry' is always inside of V9fsSynthOpenState which have NAME_MAX
     * back padding. Ensure we do not overflow it.
     */
    g_assert(sizeof(struct dirent) + NAME_MAX >=
             offsetof(struct dirent, d_name) + sz);
    memcpy(entry->d_name, node->name, sz);
    entry->d_ino = node->attr->inode;
#ifdef CONFIG_DARWIN
    entry->d_seekoff = off + 1;
#else
    entry->d_off = off + 1;
#endif
}

static struct dirent *synth_get_dentry(V9fsSynthNode *dir,
                                            struct dirent *entry, off_t off)
{
    int i = 0;
    V9fsSynthNode *node;

    rcu_read_lock();
    QLIST_FOREACH(node, &dir->child, sibling) {
        /* This is the off child of the directory */
        if (i == off) {
            break;
        }
        i++;
    }
    rcu_read_unlock();
    if (!node) {
        /* end of directory */
        return NULL;
    }
    synth_direntry(node, entry, off);
    return entry;
}

static struct dirent *synth_readdir(FsContext *ctx, V9fsFidOpenState *fs)
{
    struct dirent *entry;
    V9fsSynthOpenState *synth_open = fs->private;
    V9fsSynthNode *node = synth_open->node;
    entry = synth_get_dentry(node, &synth_open->dent, synth_open->offset);
    if (entry) {
        synth_open->offset++;
    }
    return entry;
}

static int synth_open(FsContext *ctx, V9fsPath *fs_path,
                           int flags, V9fsFidOpenState *fs)
{
    V9fsSynthOpenState *synth_open;
    V9fsSynthNode *node = *(V9fsSynthNode **)fs_path->data;

    synth_open = g_new0(V9fsSynthOpenState, 1);
    synth_open->node = node;
    node->open_count++;
    fs->private = synth_open;
    return 0;
}

static int synth_open2(FsContext *fs_ctx, V9fsPath *dir_path,
                            const char *name, int flags,
                            FsCred *credp, V9fsFidOpenState *fs)
{
    errno = ENOSYS;
    return -1;
}

static int synth_close(FsContext *ctx, V9fsFidOpenState *fs)
{
    V9fsSynthOpenState *synth_open = fs->private;
    V9fsSynthNode *node = synth_open->node;

    node->open_count--;
    g_free(synth_open);
    fs->private = NULL;
    return 0;
}

static ssize_t synth_pwritev(FsContext *ctx, V9fsFidOpenState *fs,
                                  const struct iovec *iov,
                                  int iovcnt, off_t offset)
{
    int i, count = 0, wcount;
    V9fsSynthOpenState *synth_open = fs->private;
    V9fsSynthNode *node = synth_open->node;
    if (!node->attr->write) {
        errno = EPERM;
        return -1;
    }
    for (i = 0; i < iovcnt; i++) {
        wcount = node->attr->write(iov[i].iov_base, iov[i].iov_len,
                                   offset, node->private);
        offset += wcount;
        count  += wcount;
        /* If we wrote less than requested. we are done */
        if (wcount < iov[i].iov_len) {
            break;
        }
    }
    return count;
}

static ssize_t synth_preadv(FsContext *ctx, V9fsFidOpenState *fs,
                                 const struct iovec *iov,
                                 int iovcnt, off_t offset)
{
    int i, count = 0, rcount;
    V9fsSynthOpenState *synth_open = fs->private;
    V9fsSynthNode *node = synth_open->node;
    if (!node->attr->read) {
        errno = EPERM;
        return -1;
    }
    for (i = 0; i < iovcnt; i++) {
        rcount = node->attr->read(iov[i].iov_base, iov[i].iov_len,
                                  offset, node->private);
        offset += rcount;
        count  += rcount;
        /* If we read less than requested. we are done */
        if (rcount < iov[i].iov_len) {
            break;
        }
    }
    return count;
}

static int synth_truncate(FsContext *ctx, V9fsPath *path, off_t offset)
{
    errno = ENOSYS;
    return -1;
}

static int synth_chmod(FsContext *fs_ctx, V9fsPath *path, FsCred *credp)
{
    errno = EPERM;
    return -1;
}

static int synth_mknod(FsContext *fs_ctx, V9fsPath *path,
                       const char *buf, FsCred *credp)
{
    errno = EPERM;
    return -1;
}

static int synth_mkdir(FsContext *fs_ctx, V9fsPath *path,
                       const char *buf, FsCred *credp)
{
    errno = EPERM;
    return -1;
}

static ssize_t synth_readlink(FsContext *fs_ctx, V9fsPath *path,
                                   char *buf, size_t bufsz)
{
    errno = ENOSYS;
    return -1;
}

static int synth_symlink(FsContext *fs_ctx, const char *oldpath,
                              V9fsPath *newpath, const char *buf, FsCred *credp)
{
    errno = EPERM;
    return -1;
}

static int synth_link(FsContext *fs_ctx, V9fsPath *oldpath,
                           V9fsPath *newpath, const char *buf)
{
    errno = EPERM;
    return -1;
}

static int synth_rename(FsContext *ctx, const char *oldpath,
                             const char *newpath)
{
    errno = EPERM;
    return -1;
}

static int synth_chown(FsContext *fs_ctx, V9fsPath *path, FsCred *credp)
{
    errno = EPERM;
    return -1;
}

static int synth_utimensat(FsContext *fs_ctx, V9fsPath *path,
                                const struct timespec *buf)
{
    errno = EPERM;
    return 0;
}

static int synth_remove(FsContext *ctx, const char *path)
{
    errno = EPERM;
    return -1;
}

static int synth_fsync(FsContext *ctx, int fid_type,
                            V9fsFidOpenState *fs, int datasync)
{
    errno = ENOSYS;
    return 0;
}

static int synth_statfs(FsContext *s, V9fsPath *fs_path,
                             struct statfs *stbuf)
{
    stbuf->f_type = 0xABCD;
    stbuf->f_bsize = 512;
    stbuf->f_blocks = 0;
    stbuf->f_files = synth_node_count;
#ifndef CONFIG_DARWIN
    stbuf->f_namelen = NAME_MAX;
#endif
    return 0;
}

static ssize_t synth_lgetxattr(FsContext *ctx, V9fsPath *path,
                                    const char *name, void *value, size_t size)
{
    errno = ENOTSUP;
    return -1;
}

static ssize_t synth_llistxattr(FsContext *ctx, V9fsPath *path,
                                     void *value, size_t size)
{
    errno = ENOTSUP;
    return -1;
}

static int synth_lsetxattr(FsContext *ctx, V9fsPath *path,
                                const char *name, void *value,
                                size_t size, int flags)
{
    errno = ENOTSUP;
    return -1;
}

static int synth_lremovexattr(FsContext *ctx,
                                   V9fsPath *path, const char *name)
{
    errno = ENOTSUP;
    return -1;
}

static int synth_name_to_path(FsContext *ctx, V9fsPath *dir_path,
                                   const char *name, V9fsPath *target)
{
    V9fsSynthNode *node;
    V9fsSynthNode *dir_node;

    /* "." and ".." are not allowed */
    if (!strcmp(name, ".") || !strcmp(name, "..")) {
        errno = EINVAL;
        return -1;

    }
    if (!dir_path) {
        dir_node = &synth_root;
    } else {
        dir_node = *(V9fsSynthNode **)dir_path->data;
    }
    if (!strcmp(name, "/")) {
        node = dir_node;
        goto out;
    }
    /* search for the name in the childern */
    rcu_read_lock();
    QLIST_FOREACH(node, &dir_node->child, sibling) {
        if (!strcmp(node->name, name)) {
            break;
        }
    }
    rcu_read_unlock();

    if (!node) {
        errno = ENOENT;
        return -1;
    }
out:
    /* Copy the node pointer to fid */
    g_free(target->data);
    target->data = g_memdup(&node, sizeof(void *));
    target->size = sizeof(void *);
    return 0;
}

static int synth_renameat(FsContext *ctx, V9fsPath *olddir,
                               const char *old_name, V9fsPath *newdir,
                               const char *new_name)
{
    errno = EPERM;
    return -1;
}

static int synth_unlinkat(FsContext *ctx, V9fsPath *dir,
                               const char *name, int flags)
{
    errno = EPERM;
    return -1;
}

static ssize_t v9fs_synth_qtest_write(void *buf, int len, off_t offset,
                                      void *arg)
{
    return 1;
}

static ssize_t v9fs_synth_qtest_flush_write(void *buf, int len, off_t offset,
                                            void *arg)
{
    bool should_block = !!*(uint8_t *)buf;

    if (should_block) {
        /* This will cause the server to call us again until we're cancelled */
        errno = EINTR;
        return -1;
    }

    return 1;
}

static int synth_init(FsContext *ctx, Error **errp)
{
    QLIST_INIT(&synth_root.child);
    qemu_mutex_init(&synth_mutex);

    /* Add "." and ".." entries for root */
    v9fs_add_dir_node(&synth_root, synth_root.attr->mode,
                      "..", synth_root.attr, synth_root.attr->inode);
    v9fs_add_dir_node(&synth_root, synth_root.attr->mode,
                      ".", synth_root.attr, synth_root.attr->inode);

    /* Mark the subsystem is ready for use */
    synth_fs = 1;

    if (qtest_enabled()) {
        V9fsSynthNode *node = NULL;
        int i, ret;

        /* Directory hierarchy for WALK test */
        for (i = 0; i < P9_MAXWELEM; i++) {
            char *name = g_strdup_printf(QTEST_V9FS_SYNTH_WALK_FILE, i);

            ret = qemu_v9fs_synth_mkdir(node, 0700, name, &node);
            assert(!ret);
            g_free(name);
        }

        /* File for LOPEN test */
        ret = qemu_v9fs_synth_add_file(NULL, 0, QTEST_V9FS_SYNTH_LOPEN_FILE,
                                       NULL, NULL, ctx);
        assert(!ret);

        /* File for WRITE test */
        ret = qemu_v9fs_synth_add_file(NULL, 0, QTEST_V9FS_SYNTH_WRITE_FILE,
                                       NULL, v9fs_synth_qtest_write, ctx);
        assert(!ret);

        /* File for FLUSH test */
        ret = qemu_v9fs_synth_add_file(NULL, 0, QTEST_V9FS_SYNTH_FLUSH_FILE,
                                       NULL, v9fs_synth_qtest_flush_write,
                                       ctx);
        assert(!ret);

        /* Directory for READDIR test */
        {
            V9fsSynthNode *dir = NULL;
            ret = qemu_v9fs_synth_mkdir(
                NULL, 0700, QTEST_V9FS_SYNTH_READDIR_DIR, &dir
            );
            assert(!ret);
            for (i = 0; i < QTEST_V9FS_SYNTH_READDIR_NFILES; ++i) {
                char *name = g_strdup_printf(
                    QTEST_V9FS_SYNTH_READDIR_FILE, i
                );
                ret = qemu_v9fs_synth_add_file(
                    dir, 0, name, NULL, NULL, ctx
                );
                assert(!ret);
                g_free(name);
            }
        }
    }

    return 0;
}

FileOperations synth_ops = {
    .init         = synth_init,
    .lstat        = synth_lstat,
    .readlink     = synth_readlink,
    .close        = synth_close,
    .closedir     = synth_closedir,
    .open         = synth_open,
    .opendir      = synth_opendir,
    .rewinddir    = synth_rewinddir,
    .telldir      = synth_telldir,
    .readdir      = synth_readdir,
    .seekdir      = synth_seekdir,
    .preadv       = synth_preadv,
    .pwritev      = synth_pwritev,
    .chmod        = synth_chmod,
    .mknod        = synth_mknod,
    .mkdir        = synth_mkdir,
    .fstat        = synth_fstat,
    .open2        = synth_open2,
    .symlink      = synth_symlink,
    .link         = synth_link,
    .truncate     = synth_truncate,
    .rename       = synth_rename,
    .chown        = synth_chown,
    .utimensat    = synth_utimensat,
    .remove       = synth_remove,
    .fsync        = synth_fsync,
    .statfs       = synth_statfs,
    .lgetxattr    = synth_lgetxattr,
    .llistxattr   = synth_llistxattr,
    .lsetxattr    = synth_lsetxattr,
    .lremovexattr = synth_lremovexattr,
    .name_to_path = synth_name_to_path,
    .renameat     = synth_renameat,
    .unlinkat     = synth_unlinkat,
};
