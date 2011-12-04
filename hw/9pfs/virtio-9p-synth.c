/*
 * Virtio 9p synthetic file system support
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

#include "hw/virtio.h"
#include "virtio-9p.h"
#include "virtio-9p-xattr.h"
#include "fsdev/qemu-fsdev.h"
#include "virtio-9p-synth.h"

#include <sys/stat.h>

/* Root node for synth file system */
V9fsSynthNode v9fs_synth_root = {
    .name = "/",
    .actual_attr = {
        .mode = 0555 | S_IFDIR,
        .nlink = 1,
    },
    .attr = &v9fs_synth_root.actual_attr,
};

static QemuMutex  v9fs_synth_mutex;
static int v9fs_synth_node_count;
/* set to 1 when the synth fs is ready */
static int v9fs_synth_fs;

static V9fsSynthNode *v9fs_add_dir_node(V9fsSynthNode *parent, int mode,
                                        const char *name,
                                        V9fsSynthNodeAttr *attr, int inode)
{
    V9fsSynthNode *node;

    /* Add directory type and remove write bits */
    mode = ((mode & 0777) | S_IFDIR) & ~(S_IWUSR | S_IWGRP | S_IWOTH);
    node = g_malloc0(sizeof(V9fsSynthNode));
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
    strncpy(node->name, name, sizeof(node->name));
    QLIST_INSERT_HEAD_RCU(&parent->child, node, sibling);
    return node;
}

int qemu_v9fs_synth_mkdir(V9fsSynthNode *parent, int mode,
                          const char *name, V9fsSynthNode **result)
{
    int ret;
    V9fsSynthNode *node, *tmp;

    if (!v9fs_synth_fs) {
        return EAGAIN;
    }
    if (!name || (strlen(name) >= NAME_MAX)) {
        return EINVAL;
    }
    if (!parent) {
        parent = &v9fs_synth_root;
    }
    qemu_mutex_lock(&v9fs_synth_mutex);
    QLIST_FOREACH(tmp, &parent->child, sibling) {
        if (!strcmp(tmp->name, name)) {
            ret = EEXIST;
            goto err_out;
        }
    }
    /* Add the name */
    node = v9fs_add_dir_node(parent, mode, name, NULL, v9fs_synth_node_count++);
    v9fs_add_dir_node(node, parent->attr->mode, "..",
                      parent->attr, parent->attr->inode);
    v9fs_add_dir_node(node, node->attr->mode, ".",
                      node->attr, node->attr->inode);
    *result = node;
    ret = 0;
err_out:
    qemu_mutex_unlock(&v9fs_synth_mutex);
    return ret;
}

int qemu_v9fs_synth_add_file(V9fsSynthNode *parent, int mode,
                             const char *name, v9fs_synth_read read,
                             v9fs_synth_write write, void *arg)
{
    int ret;
    V9fsSynthNode *node, *tmp;

    if (!v9fs_synth_fs) {
        return EAGAIN;
    }
    if (!name || (strlen(name) >= NAME_MAX)) {
        return EINVAL;
    }
    if (!parent) {
        parent = &v9fs_synth_root;
    }

    qemu_mutex_lock(&v9fs_synth_mutex);
    QLIST_FOREACH(tmp, &parent->child, sibling) {
        if (!strcmp(tmp->name, name)) {
            ret = EEXIST;
            goto err_out;
        }
    }
    /* Add file type and remove write bits */
    mode = ((mode & 0777) | S_IFREG);
    node = g_malloc0(sizeof(V9fsSynthNode));
    node->attr         = &node->actual_attr;
    node->attr->inode  = v9fs_synth_node_count++;
    node->attr->nlink  = 1;
    node->attr->read   = read;
    node->attr->write  = write;
    node->attr->mode   = mode;
    node->private      = arg;
    strncpy(node->name, name, sizeof(node->name));
    QLIST_INSERT_HEAD_RCU(&parent->child, node, sibling);
    ret = 0;
err_out:
    qemu_mutex_unlock(&v9fs_synth_mutex);
    return ret;
}

static void v9fs_synth_fill_statbuf(V9fsSynthNode *node, struct stat *stbuf)
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

static int v9fs_synth_lstat(FsContext *fs_ctx,
                            V9fsPath *fs_path, struct stat *stbuf)
{
    V9fsSynthNode *node = *(V9fsSynthNode **)fs_path->data;

    v9fs_synth_fill_statbuf(node, stbuf);
    return 0;
}

static int v9fs_synth_fstat(FsContext *fs_ctx, int fid_type,
                            V9fsFidOpenState *fs, struct stat *stbuf)
{
    V9fsSynthOpenState *synth_open = fs->private;
    v9fs_synth_fill_statbuf(synth_open->node, stbuf);
    return 0;
}

static int v9fs_synth_opendir(FsContext *ctx,
                             V9fsPath *fs_path, V9fsFidOpenState *fs)
{
    V9fsSynthOpenState *synth_open;
    V9fsSynthNode *node = *(V9fsSynthNode **)fs_path->data;

    synth_open = g_malloc(sizeof(*synth_open));
    synth_open->node = node;
    node->open_count++;
    fs->private = synth_open;
    return 0;
}

static int v9fs_synth_closedir(FsContext *ctx, V9fsFidOpenState *fs)
{
    V9fsSynthOpenState *synth_open = fs->private;
    V9fsSynthNode *node = synth_open->node;

    node->open_count--;
    g_free(synth_open);
    fs->private = NULL;
    return 0;
}

static off_t v9fs_synth_telldir(FsContext *ctx, V9fsFidOpenState *fs)
{
    V9fsSynthOpenState *synth_open = fs->private;
    return synth_open->offset;
}

static void v9fs_synth_seekdir(FsContext *ctx, V9fsFidOpenState *fs, off_t off)
{
    V9fsSynthOpenState *synth_open = fs->private;
    synth_open->offset = off;
}

static void v9fs_synth_rewinddir(FsContext *ctx, V9fsFidOpenState *fs)
{
    v9fs_synth_seekdir(ctx, fs, 0);
}

static void v9fs_synth_direntry(V9fsSynthNode *node,
                                struct dirent *entry, off_t off)
{
    strcpy(entry->d_name, node->name);
    entry->d_ino = node->attr->inode;
    entry->d_off = off + 1;
}

static int v9fs_synth_get_dentry(V9fsSynthNode *dir, struct dirent *entry,
                                 struct dirent **result, off_t off)
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
        *result = NULL;
        return 0;
    }
    v9fs_synth_direntry(node, entry, off);
    *result = entry;
    return 0;
}

static int v9fs_synth_readdir_r(FsContext *ctx, V9fsFidOpenState *fs,
                                struct dirent *entry, struct dirent **result)
{
    int ret;
    V9fsSynthOpenState *synth_open = fs->private;
    V9fsSynthNode *node = synth_open->node;
    ret = v9fs_synth_get_dentry(node, entry, result, synth_open->offset);
    if (!ret && *result != NULL) {
        synth_open->offset++;
    }
    return ret;
}

static int v9fs_synth_open(FsContext *ctx, V9fsPath *fs_path,
                           int flags, V9fsFidOpenState *fs)
{
    V9fsSynthOpenState *synth_open;
    V9fsSynthNode *node = *(V9fsSynthNode **)fs_path->data;

    synth_open = g_malloc(sizeof(*synth_open));
    synth_open->node = node;
    node->open_count++;
    fs->private = synth_open;
    return 0;
}

static int v9fs_synth_open2(FsContext *fs_ctx, V9fsPath *dir_path,
                            const char *name, int flags,
                            FsCred *credp, V9fsFidOpenState *fs)
{
    errno = ENOSYS;
    return -1;
}

static int v9fs_synth_close(FsContext *ctx, V9fsFidOpenState *fs)
{
    V9fsSynthOpenState *synth_open = fs->private;
    V9fsSynthNode *node = synth_open->node;

    node->open_count--;
    g_free(synth_open);
    fs->private = NULL;
    return 0;
}

static ssize_t v9fs_synth_pwritev(FsContext *ctx, V9fsFidOpenState *fs,
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

static ssize_t v9fs_synth_preadv(FsContext *ctx, V9fsFidOpenState *fs,
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

static int v9fs_synth_truncate(FsContext *ctx, V9fsPath *path, off_t offset)
{
    errno = ENOSYS;
    return -1;
}

static int v9fs_synth_chmod(FsContext *fs_ctx, V9fsPath *path, FsCred *credp)
{
    errno = EPERM;
    return -1;
}

static int v9fs_synth_mknod(FsContext *fs_ctx, V9fsPath *path,
                       const char *buf, FsCred *credp)
{
    errno = EPERM;
    return -1;
}

static int v9fs_synth_mkdir(FsContext *fs_ctx, V9fsPath *path,
                       const char *buf, FsCred *credp)
{
    errno = EPERM;
    return -1;
}

static ssize_t v9fs_synth_readlink(FsContext *fs_ctx, V9fsPath *path,
                                   char *buf, size_t bufsz)
{
    errno = ENOSYS;
    return -1;
}

static int v9fs_synth_symlink(FsContext *fs_ctx, const char *oldpath,
                              V9fsPath *newpath, const char *buf, FsCred *credp)
{
    errno = EPERM;
    return -1;
}

static int v9fs_synth_link(FsContext *fs_ctx, V9fsPath *oldpath,
                           V9fsPath *newpath, const char *buf)
{
    errno = EPERM;
    return -1;
}

static int v9fs_synth_rename(FsContext *ctx, const char *oldpath,
                             const char *newpath)
{
    errno = EPERM;
    return -1;
}

static int v9fs_synth_chown(FsContext *fs_ctx, V9fsPath *path, FsCred *credp)
{
    errno = EPERM;
    return -1;
}

static int v9fs_synth_utimensat(FsContext *fs_ctx, V9fsPath *path,
                                const struct timespec *buf)
{
    errno = EPERM;
    return 0;
}

static int v9fs_synth_remove(FsContext *ctx, const char *path)
{
    errno = EPERM;
    return -1;
}

static int v9fs_synth_fsync(FsContext *ctx, int fid_type,
                            V9fsFidOpenState *fs, int datasync)
{
    errno = ENOSYS;
    return 0;
}

static int v9fs_synth_statfs(FsContext *s, V9fsPath *fs_path,
                             struct statfs *stbuf)
{
    stbuf->f_type = 0xABCD;
    stbuf->f_bsize = 512;
    stbuf->f_blocks = 0;
    stbuf->f_files = v9fs_synth_node_count;
    stbuf->f_namelen = NAME_MAX;
    return 0;
}

static ssize_t v9fs_synth_lgetxattr(FsContext *ctx, V9fsPath *path,
                                    const char *name, void *value, size_t size)
{
    errno = ENOTSUP;
    return -1;
}

static ssize_t v9fs_synth_llistxattr(FsContext *ctx, V9fsPath *path,
                                     void *value, size_t size)
{
    errno = ENOTSUP;
    return -1;
}

static int v9fs_synth_lsetxattr(FsContext *ctx, V9fsPath *path,
                                const char *name, void *value,
                                size_t size, int flags)
{
    errno = ENOTSUP;
    return -1;
}

static int v9fs_synth_lremovexattr(FsContext *ctx,
                                   V9fsPath *path, const char *name)
{
    errno = ENOTSUP;
    return -1;
}

static int v9fs_synth_name_to_path(FsContext *ctx, V9fsPath *dir_path,
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
        dir_node = &v9fs_synth_root;
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
    target->data = g_malloc(sizeof(void *));
    memcpy(target->data, &node, sizeof(void *));
    target->size = sizeof(void *);
    return 0;
}

static int v9fs_synth_renameat(FsContext *ctx, V9fsPath *olddir,
                               const char *old_name, V9fsPath *newdir,
                               const char *new_name)
{
    errno = EPERM;
    return -1;
}

static int v9fs_synth_unlinkat(FsContext *ctx, V9fsPath *dir,
                               const char *name, int flags)
{
    errno = EPERM;
    return -1;
}

static int v9fs_synth_init(FsContext *ctx)
{
    QLIST_INIT(&v9fs_synth_root.child);
    qemu_mutex_init(&v9fs_synth_mutex);

    /* Add "." and ".." entries for root */
    v9fs_add_dir_node(&v9fs_synth_root, v9fs_synth_root.attr->mode,
                      "..", v9fs_synth_root.attr, v9fs_synth_root.attr->inode);
    v9fs_add_dir_node(&v9fs_synth_root, v9fs_synth_root.attr->mode,
                      ".", v9fs_synth_root.attr, v9fs_synth_root.attr->inode);

    /* Mark the subsystem is ready for use */
    v9fs_synth_fs = 1;
    return 0;
}

FileOperations synth_ops = {
    .init         = v9fs_synth_init,
    .lstat        = v9fs_synth_lstat,
    .readlink     = v9fs_synth_readlink,
    .close        = v9fs_synth_close,
    .closedir     = v9fs_synth_closedir,
    .open         = v9fs_synth_open,
    .opendir      = v9fs_synth_opendir,
    .rewinddir    = v9fs_synth_rewinddir,
    .telldir      = v9fs_synth_telldir,
    .readdir_r    = v9fs_synth_readdir_r,
    .seekdir      = v9fs_synth_seekdir,
    .preadv       = v9fs_synth_preadv,
    .pwritev      = v9fs_synth_pwritev,
    .chmod        = v9fs_synth_chmod,
    .mknod        = v9fs_synth_mknod,
    .mkdir        = v9fs_synth_mkdir,
    .fstat        = v9fs_synth_fstat,
    .open2        = v9fs_synth_open2,
    .symlink      = v9fs_synth_symlink,
    .link         = v9fs_synth_link,
    .truncate     = v9fs_synth_truncate,
    .rename       = v9fs_synth_rename,
    .chown        = v9fs_synth_chown,
    .utimensat    = v9fs_synth_utimensat,
    .remove       = v9fs_synth_remove,
    .fsync        = v9fs_synth_fsync,
    .statfs       = v9fs_synth_statfs,
    .lgetxattr    = v9fs_synth_lgetxattr,
    .llistxattr   = v9fs_synth_llistxattr,
    .lsetxattr    = v9fs_synth_lsetxattr,
    .lremovexattr = v9fs_synth_lremovexattr,
    .name_to_path = v9fs_synth_name_to_path,
    .renameat     = v9fs_synth_renameat,
    .unlinkat     = v9fs_synth_unlinkat,
};
