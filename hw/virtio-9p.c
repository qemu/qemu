/*
 * Virtio 9p backend
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "virtio.h"
#include "pc.h"
#include "qemu_socket.h"
#include "virtio-9p.h"
#include "fsdev/qemu-fsdev.h"
#include "virtio-9p-debug.h"
#include "virtio-9p-xattr.h"

int debug_9p_pdu;

enum {
    Oread   = 0x00,
    Owrite  = 0x01,
    Ordwr   = 0x02,
    Oexec   = 0x03,
    Oexcl   = 0x04,
    Otrunc  = 0x10,
    Orexec  = 0x20,
    Orclose = 0x40,
    Oappend = 0x80,
};

static int omode_to_uflags(int8_t mode)
{
    int ret = 0;

    switch (mode & 3) {
    case Oread:
        ret = O_RDONLY;
        break;
    case Ordwr:
        ret = O_RDWR;
        break;
    case Owrite:
        ret = O_WRONLY;
        break;
    case Oexec:
        ret = O_RDONLY;
        break;
    }

    if (mode & Otrunc) {
        ret |= O_TRUNC;
    }

    if (mode & Oappend) {
        ret |= O_APPEND;
    }

    if (mode & Oexcl) {
        ret |= O_EXCL;
    }

    return ret;
}

void cred_init(FsCred *credp)
{
    credp->fc_uid = -1;
    credp->fc_gid = -1;
    credp->fc_mode = -1;
    credp->fc_rdev = -1;
}

static int v9fs_do_lstat(V9fsState *s, V9fsString *path, struct stat *stbuf)
{
    return s->ops->lstat(&s->ctx, path->data, stbuf);
}

static ssize_t v9fs_do_readlink(V9fsState *s, V9fsString *path, V9fsString *buf)
{
    ssize_t len;

    buf->data = qemu_malloc(1024);

    len = s->ops->readlink(&s->ctx, path->data, buf->data, 1024 - 1);
    if (len > -1) {
        buf->size = len;
        buf->data[len] = 0;
    }

    return len;
}

static int v9fs_do_close(V9fsState *s, int fd)
{
    return s->ops->close(&s->ctx, fd);
}

static int v9fs_do_closedir(V9fsState *s, DIR *dir)
{
    return s->ops->closedir(&s->ctx, dir);
}

static int v9fs_do_open(V9fsState *s, V9fsString *path, int flags)
{
    return s->ops->open(&s->ctx, path->data, flags);
}

static DIR *v9fs_do_opendir(V9fsState *s, V9fsString *path)
{
    return s->ops->opendir(&s->ctx, path->data);
}

static void v9fs_do_rewinddir(V9fsState *s, DIR *dir)
{
    return s->ops->rewinddir(&s->ctx, dir);
}

static off_t v9fs_do_telldir(V9fsState *s, DIR *dir)
{
    return s->ops->telldir(&s->ctx, dir);
}

static struct dirent *v9fs_do_readdir(V9fsState *s, DIR *dir)
{
    return s->ops->readdir(&s->ctx, dir);
}

static void v9fs_do_seekdir(V9fsState *s, DIR *dir, off_t off)
{
    return s->ops->seekdir(&s->ctx, dir, off);
}

static int v9fs_do_preadv(V9fsState *s, int fd, const struct iovec *iov,
                            int iovcnt, int64_t offset)
{
    return s->ops->preadv(&s->ctx, fd, iov, iovcnt, offset);
}

static int v9fs_do_pwritev(V9fsState *s, int fd, const struct iovec *iov,
                       int iovcnt, int64_t offset)
{
    return s->ops->pwritev(&s->ctx, fd, iov, iovcnt, offset);
}

static int v9fs_do_chmod(V9fsState *s, V9fsString *path, mode_t mode)
{
    FsCred cred;
    cred_init(&cred);
    cred.fc_mode = mode;
    return s->ops->chmod(&s->ctx, path->data, &cred);
}

static int v9fs_do_mknod(V9fsState *s, char *name,
        mode_t mode, dev_t dev, uid_t uid, gid_t gid)
{
    FsCred cred;
    cred_init(&cred);
    cred.fc_uid = uid;
    cred.fc_gid = gid;
    cred.fc_mode = mode;
    cred.fc_rdev = dev;
    return s->ops->mknod(&s->ctx, name, &cred);
}

static int v9fs_do_mkdir(V9fsState *s, char *name, mode_t mode,
                uid_t uid, gid_t gid)
{
    FsCred cred;

    cred_init(&cred);
    cred.fc_uid = uid;
    cred.fc_gid = gid;
    cred.fc_mode = mode;

    return s->ops->mkdir(&s->ctx, name, &cred);
}

static int v9fs_do_fstat(V9fsState *s, int fd, struct stat *stbuf)
{
    return s->ops->fstat(&s->ctx, fd, stbuf);
}

static int v9fs_do_open2(V9fsState *s, char *fullname, uid_t uid, gid_t gid,
        int flags, int mode)
{
    FsCred cred;

    cred_init(&cred);
    cred.fc_uid = uid;
    cred.fc_gid = gid;
    cred.fc_mode = mode & 07777;
    flags = flags;

    return s->ops->open2(&s->ctx, fullname, flags, &cred);
}

static int v9fs_do_symlink(V9fsState *s, V9fsFidState *fidp,
        const char *oldpath, const char *newpath, gid_t gid)
{
    FsCred cred;
    cred_init(&cred);
    cred.fc_uid = fidp->uid;
    cred.fc_gid = gid;
    cred.fc_mode = 0777;

    return s->ops->symlink(&s->ctx, oldpath, newpath, &cred);
}

static int v9fs_do_link(V9fsState *s, V9fsString *oldpath, V9fsString *newpath)
{
    return s->ops->link(&s->ctx, oldpath->data, newpath->data);
}

static int v9fs_do_truncate(V9fsState *s, V9fsString *path, off_t size)
{
    return s->ops->truncate(&s->ctx, path->data, size);
}

static int v9fs_do_rename(V9fsState *s, V9fsString *oldpath,
                            V9fsString *newpath)
{
    return s->ops->rename(&s->ctx, oldpath->data, newpath->data);
}

static int v9fs_do_chown(V9fsState *s, V9fsString *path, uid_t uid, gid_t gid)
{
    FsCred cred;
    cred_init(&cred);
    cred.fc_uid = uid;
    cred.fc_gid = gid;

    return s->ops->chown(&s->ctx, path->data, &cred);
}

static int v9fs_do_utimensat(V9fsState *s, V9fsString *path,
                                           const struct timespec times[2])
{
    return s->ops->utimensat(&s->ctx, path->data, times);
}

static int v9fs_do_remove(V9fsState *s, V9fsString *path)
{
    return s->ops->remove(&s->ctx, path->data);
}

static int v9fs_do_fsync(V9fsState *s, int fd, int datasync)
{
    return s->ops->fsync(&s->ctx, fd, datasync);
}

static int v9fs_do_statfs(V9fsState *s, V9fsString *path, struct statfs *stbuf)
{
    return s->ops->statfs(&s->ctx, path->data, stbuf);
}

static ssize_t v9fs_do_lgetxattr(V9fsState *s, V9fsString *path,
                             V9fsString *xattr_name,
                             void *value, size_t size)
{
    return s->ops->lgetxattr(&s->ctx, path->data,
                             xattr_name->data, value, size);
}

static ssize_t v9fs_do_llistxattr(V9fsState *s, V9fsString *path,
                              void *value, size_t size)
{
    return s->ops->llistxattr(&s->ctx, path->data,
                              value, size);
}

static int v9fs_do_lsetxattr(V9fsState *s, V9fsString *path,
                             V9fsString *xattr_name,
                             void *value, size_t size, int flags)
{
    return s->ops->lsetxattr(&s->ctx, path->data,
                             xattr_name->data, value, size, flags);
}

static int v9fs_do_lremovexattr(V9fsState *s, V9fsString *path,
                                V9fsString *xattr_name)
{
    return s->ops->lremovexattr(&s->ctx, path->data,
                                xattr_name->data);
}


static void v9fs_string_init(V9fsString *str)
{
    str->data = NULL;
    str->size = 0;
}

static void v9fs_string_free(V9fsString *str)
{
    qemu_free(str->data);
    str->data = NULL;
    str->size = 0;
}

static void v9fs_string_null(V9fsString *str)
{
    v9fs_string_free(str);
}

static int number_to_string(void *arg, char type)
{
    unsigned int ret = 0;

    switch (type) {
    case 'u': {
        unsigned int num = *(unsigned int *)arg;

        do {
            ret++;
            num = num/10;
        } while (num);
        break;
    }
    case 'U': {
        unsigned long num = *(unsigned long *)arg;
        do {
            ret++;
            num = num/10;
        } while (num);
        break;
    }
    default:
        printf("Number_to_string: Unknown number format\n");
        return -1;
    }

    return ret;
}

static int GCC_FMT_ATTR(2, 0)
v9fs_string_alloc_printf(char **strp, const char *fmt, va_list ap)
{
    va_list ap2;
    char *iter = (char *)fmt;
    int len = 0;
    int nr_args = 0;
    char *arg_char_ptr;
    unsigned int arg_uint;
    unsigned long arg_ulong;

    /* Find the number of %'s that denotes an argument */
    for (iter = strstr(iter, "%"); iter; iter = strstr(iter, "%")) {
        nr_args++;
        iter++;
    }

    len = strlen(fmt) - 2*nr_args;

    if (!nr_args) {
        goto alloc_print;
    }

    va_copy(ap2, ap);

    iter = (char *)fmt;

    /* Now parse the format string */
    for (iter = strstr(iter, "%"); iter; iter = strstr(iter, "%")) {
        iter++;
        switch (*iter) {
        case 'u':
            arg_uint = va_arg(ap2, unsigned int);
            len += number_to_string((void *)&arg_uint, 'u');
            break;
        case 'l':
            if (*++iter == 'u') {
                arg_ulong = va_arg(ap2, unsigned long);
                len += number_to_string((void *)&arg_ulong, 'U');
            } else {
                return -1;
            }
            break;
        case 's':
            arg_char_ptr = va_arg(ap2, char *);
            len += strlen(arg_char_ptr);
            break;
        case 'c':
            len += 1;
            break;
        default:
            fprintf(stderr,
		    "v9fs_string_alloc_printf:Incorrect format %c", *iter);
            return -1;
        }
        iter++;
    }

alloc_print:
    *strp = qemu_malloc((len + 1) * sizeof(**strp));

    return vsprintf(*strp, fmt, ap);
}

static void GCC_FMT_ATTR(2, 3)
v9fs_string_sprintf(V9fsString *str, const char *fmt, ...)
{
    va_list ap;
    int err;

    v9fs_string_free(str);

    va_start(ap, fmt);
    err = v9fs_string_alloc_printf(&str->data, fmt, ap);
    BUG_ON(err == -1);
    va_end(ap);

    str->size = err;
}

static void v9fs_string_copy(V9fsString *lhs, V9fsString *rhs)
{
    v9fs_string_free(lhs);
    v9fs_string_sprintf(lhs, "%s", rhs->data);
}

static size_t v9fs_string_size(V9fsString *str)
{
    return str->size;
}

static V9fsFidState *lookup_fid(V9fsState *s, int32_t fid)
{
    V9fsFidState *f;

    for (f = s->fid_list; f; f = f->next) {
        if (f->fid == fid) {
            return f;
        }
    }

    return NULL;
}

static V9fsFidState *alloc_fid(V9fsState *s, int32_t fid)
{
    V9fsFidState *f;

    f = lookup_fid(s, fid);
    if (f) {
        return NULL;
    }

    f = qemu_mallocz(sizeof(V9fsFidState));

    f->fid = fid;
    f->fid_type = P9_FID_NONE;

    f->next = s->fid_list;
    s->fid_list = f;

    return f;
}

static int v9fs_xattr_fid_clunk(V9fsState *s, V9fsFidState *fidp)
{
    int retval = 0;

    if (fidp->fs.xattr.copied_len == -1) {
        /* getxattr/listxattr fid */
        goto free_value;
    }
    /*
     * if this is fid for setxattr. clunk should
     * result in setxattr localcall
     */
    if (fidp->fs.xattr.len != fidp->fs.xattr.copied_len) {
        /* clunk after partial write */
        retval = -EINVAL;
        goto free_out;
    }
    if (fidp->fs.xattr.len) {
        retval = v9fs_do_lsetxattr(s, &fidp->path, &fidp->fs.xattr.name,
                                   fidp->fs.xattr.value,
                                   fidp->fs.xattr.len,
                                   fidp->fs.xattr.flags);
    } else {
        retval = v9fs_do_lremovexattr(s, &fidp->path, &fidp->fs.xattr.name);
    }
free_out:
    v9fs_string_free(&fidp->fs.xattr.name);
free_value:
    if (fidp->fs.xattr.value) {
        qemu_free(fidp->fs.xattr.value);
    }
    return retval;
}

static int free_fid(V9fsState *s, int32_t fid)
{
    int retval = 0;
    V9fsFidState **fidpp, *fidp;

    for (fidpp = &s->fid_list; *fidpp; fidpp = &(*fidpp)->next) {
        if ((*fidpp)->fid == fid) {
            break;
        }
    }

    if (*fidpp == NULL) {
        return -ENOENT;
    }

    fidp = *fidpp;
    *fidpp = fidp->next;

    if (fidp->fid_type == P9_FID_FILE) {
        v9fs_do_close(s, fidp->fs.fd);
    } else if (fidp->fid_type == P9_FID_DIR) {
        v9fs_do_closedir(s, fidp->fs.dir);
    } else if (fidp->fid_type == P9_FID_XATTR) {
        retval = v9fs_xattr_fid_clunk(s, fidp);
    }
    v9fs_string_free(&fidp->path);
    qemu_free(fidp);

    return retval;
}

#define P9_QID_TYPE_DIR         0x80
#define P9_QID_TYPE_SYMLINK     0x02

#define P9_STAT_MODE_DIR        0x80000000
#define P9_STAT_MODE_APPEND     0x40000000
#define P9_STAT_MODE_EXCL       0x20000000
#define P9_STAT_MODE_MOUNT      0x10000000
#define P9_STAT_MODE_AUTH       0x08000000
#define P9_STAT_MODE_TMP        0x04000000
#define P9_STAT_MODE_SYMLINK    0x02000000
#define P9_STAT_MODE_LINK       0x01000000
#define P9_STAT_MODE_DEVICE     0x00800000
#define P9_STAT_MODE_NAMED_PIPE 0x00200000
#define P9_STAT_MODE_SOCKET     0x00100000
#define P9_STAT_MODE_SETUID     0x00080000
#define P9_STAT_MODE_SETGID     0x00040000
#define P9_STAT_MODE_SETVTX     0x00010000

#define P9_STAT_MODE_TYPE_BITS (P9_STAT_MODE_DIR |          \
                                P9_STAT_MODE_SYMLINK |      \
                                P9_STAT_MODE_LINK |         \
                                P9_STAT_MODE_DEVICE |       \
                                P9_STAT_MODE_NAMED_PIPE |   \
                                P9_STAT_MODE_SOCKET)

/* This is the algorithm from ufs in spfs */
static void stat_to_qid(const struct stat *stbuf, V9fsQID *qidp)
{
    size_t size;

    size = MIN(sizeof(stbuf->st_ino), sizeof(qidp->path));
    memcpy(&qidp->path, &stbuf->st_ino, size);
    qidp->version = stbuf->st_mtime ^ (stbuf->st_size << 8);
    qidp->type = 0;
    if (S_ISDIR(stbuf->st_mode)) {
        qidp->type |= P9_QID_TYPE_DIR;
    }
    if (S_ISLNK(stbuf->st_mode)) {
        qidp->type |= P9_QID_TYPE_SYMLINK;
    }
}

static int fid_to_qid(V9fsState *s, V9fsFidState *fidp, V9fsQID *qidp)
{
    struct stat stbuf;
    int err;

    err = v9fs_do_lstat(s, &fidp->path, &stbuf);
    if (err) {
        return err;
    }

    stat_to_qid(&stbuf, qidp);
    return 0;
}

static V9fsPDU *alloc_pdu(V9fsState *s)
{
    V9fsPDU *pdu = NULL;

    if (!QLIST_EMPTY(&s->free_list)) {
	pdu = QLIST_FIRST(&s->free_list);
	QLIST_REMOVE(pdu, next);
    }
    return pdu;
}

static void free_pdu(V9fsState *s, V9fsPDU *pdu)
{
    if (pdu) {
	QLIST_INSERT_HEAD(&s->free_list, pdu, next);
    }
}

size_t pdu_packunpack(void *addr, struct iovec *sg, int sg_count,
                        size_t offset, size_t size, int pack)
{
    int i = 0;
    size_t copied = 0;

    for (i = 0; size && i < sg_count; i++) {
        size_t len;
        if (offset >= sg[i].iov_len) {
            /* skip this sg */
            offset -= sg[i].iov_len;
            continue;
        } else {
            len = MIN(sg[i].iov_len - offset, size);
            if (pack) {
                memcpy(sg[i].iov_base + offset, addr, len);
            } else {
                memcpy(addr, sg[i].iov_base + offset, len);
            }
            size -= len;
            copied += len;
            addr += len;
            if (size) {
                offset = 0;
                continue;
            }
        }
    }

    return copied;
}

static size_t pdu_unpack(void *dst, V9fsPDU *pdu, size_t offset, size_t size)
{
    return pdu_packunpack(dst, pdu->elem.out_sg, pdu->elem.out_num,
                         offset, size, 0);
}

static size_t pdu_pack(V9fsPDU *pdu, size_t offset, const void *src,
                        size_t size)
{
    return pdu_packunpack((void *)src, pdu->elem.in_sg, pdu->elem.in_num,
                             offset, size, 1);
}

static int pdu_copy_sg(V9fsPDU *pdu, size_t offset, int rx, struct iovec *sg)
{
    size_t pos = 0;
    int i, j;
    struct iovec *src_sg;
    unsigned int num;

    if (rx) {
        src_sg = pdu->elem.in_sg;
        num = pdu->elem.in_num;
    } else {
        src_sg = pdu->elem.out_sg;
        num = pdu->elem.out_num;
    }

    j = 0;
    for (i = 0; i < num; i++) {
        if (offset <= pos) {
            sg[j].iov_base = src_sg[i].iov_base;
            sg[j].iov_len = src_sg[i].iov_len;
            j++;
        } else if (offset < (src_sg[i].iov_len + pos)) {
            sg[j].iov_base = src_sg[i].iov_base;
            sg[j].iov_len = src_sg[i].iov_len;
            sg[j].iov_base += (offset - pos);
            sg[j].iov_len -= (offset - pos);
            j++;
        }
        pos += src_sg[i].iov_len;
    }

    return j;
}

static size_t pdu_unmarshal(V9fsPDU *pdu, size_t offset, const char *fmt, ...)
{
    size_t old_offset = offset;
    va_list ap;
    int i;

    va_start(ap, fmt);
    for (i = 0; fmt[i]; i++) {
        switch (fmt[i]) {
        case 'b': {
            uint8_t *valp = va_arg(ap, uint8_t *);
            offset += pdu_unpack(valp, pdu, offset, sizeof(*valp));
            break;
        }
        case 'w': {
            uint16_t val, *valp;
            valp = va_arg(ap, uint16_t *);
            val = le16_to_cpupu(valp);
            offset += pdu_unpack(&val, pdu, offset, sizeof(val));
            *valp = val;
            break;
        }
        case 'd': {
            uint32_t val, *valp;
            valp = va_arg(ap, uint32_t *);
            val = le32_to_cpupu(valp);
            offset += pdu_unpack(&val, pdu, offset, sizeof(val));
            *valp = val;
            break;
        }
        case 'q': {
            uint64_t val, *valp;
            valp = va_arg(ap, uint64_t *);
            val = le64_to_cpup(valp);
            offset += pdu_unpack(&val, pdu, offset, sizeof(val));
            *valp = val;
            break;
        }
        case 'v': {
            struct iovec *iov = va_arg(ap, struct iovec *);
            int *iovcnt = va_arg(ap, int *);
            *iovcnt = pdu_copy_sg(pdu, offset, 0, iov);
            break;
        }
        case 's': {
            V9fsString *str = va_arg(ap, V9fsString *);
            offset += pdu_unmarshal(pdu, offset, "w", &str->size);
            /* FIXME: sanity check str->size */
            str->data = qemu_malloc(str->size + 1);
            offset += pdu_unpack(str->data, pdu, offset, str->size);
            str->data[str->size] = 0;
            break;
        }
        case 'Q': {
            V9fsQID *qidp = va_arg(ap, V9fsQID *);
            offset += pdu_unmarshal(pdu, offset, "bdq",
                        &qidp->type, &qidp->version, &qidp->path);
            break;
        }
        case 'S': {
            V9fsStat *statp = va_arg(ap, V9fsStat *);
            offset += pdu_unmarshal(pdu, offset, "wwdQdddqsssssddd",
                        &statp->size, &statp->type, &statp->dev,
                        &statp->qid, &statp->mode, &statp->atime,
                        &statp->mtime, &statp->length,
                        &statp->name, &statp->uid, &statp->gid,
                        &statp->muid, &statp->extension,
                        &statp->n_uid, &statp->n_gid,
                        &statp->n_muid);
            break;
        }
        case 'I': {
            V9fsIattr *iattr = va_arg(ap, V9fsIattr *);
            offset += pdu_unmarshal(pdu, offset, "ddddqqqqq",
                        &iattr->valid, &iattr->mode,
                        &iattr->uid, &iattr->gid, &iattr->size,
                        &iattr->atime_sec, &iattr->atime_nsec,
                        &iattr->mtime_sec, &iattr->mtime_nsec);
            break;
        }
        default:
            break;
        }
    }

    va_end(ap);

    return offset - old_offset;
}

static size_t pdu_marshal(V9fsPDU *pdu, size_t offset, const char *fmt, ...)
{
    size_t old_offset = offset;
    va_list ap;
    int i;

    va_start(ap, fmt);
    for (i = 0; fmt[i]; i++) {
        switch (fmt[i]) {
        case 'b': {
            uint8_t val = va_arg(ap, int);
            offset += pdu_pack(pdu, offset, &val, sizeof(val));
            break;
        }
        case 'w': {
            uint16_t val;
            cpu_to_le16w(&val, va_arg(ap, int));
            offset += pdu_pack(pdu, offset, &val, sizeof(val));
            break;
        }
        case 'd': {
            uint32_t val;
            cpu_to_le32w(&val, va_arg(ap, uint32_t));
            offset += pdu_pack(pdu, offset, &val, sizeof(val));
            break;
        }
        case 'q': {
            uint64_t val;
            cpu_to_le64w(&val, va_arg(ap, uint64_t));
            offset += pdu_pack(pdu, offset, &val, sizeof(val));
            break;
        }
        case 'v': {
            struct iovec *iov = va_arg(ap, struct iovec *);
            int *iovcnt = va_arg(ap, int *);
            *iovcnt = pdu_copy_sg(pdu, offset, 1, iov);
            break;
        }
        case 's': {
            V9fsString *str = va_arg(ap, V9fsString *);
            offset += pdu_marshal(pdu, offset, "w", str->size);
            offset += pdu_pack(pdu, offset, str->data, str->size);
            break;
        }
        case 'Q': {
            V9fsQID *qidp = va_arg(ap, V9fsQID *);
            offset += pdu_marshal(pdu, offset, "bdq",
                        qidp->type, qidp->version, qidp->path);
            break;
        }
        case 'S': {
            V9fsStat *statp = va_arg(ap, V9fsStat *);
            offset += pdu_marshal(pdu, offset, "wwdQdddqsssssddd",
                        statp->size, statp->type, statp->dev,
                        &statp->qid, statp->mode, statp->atime,
                        statp->mtime, statp->length, &statp->name,
                        &statp->uid, &statp->gid, &statp->muid,
                        &statp->extension, statp->n_uid,
                        statp->n_gid, statp->n_muid);
            break;
        }
        case 'A': {
            V9fsStatDotl *statp = va_arg(ap, V9fsStatDotl *);
            offset += pdu_marshal(pdu, offset, "qQdddqqqqqqqqqqqqqqq",
                        statp->st_result_mask,
                        &statp->qid, statp->st_mode,
                        statp->st_uid, statp->st_gid,
                        statp->st_nlink, statp->st_rdev,
                        statp->st_size, statp->st_blksize, statp->st_blocks,
                        statp->st_atime_sec, statp->st_atime_nsec,
                        statp->st_mtime_sec, statp->st_mtime_nsec,
                        statp->st_ctime_sec, statp->st_ctime_nsec,
                        statp->st_btime_sec, statp->st_btime_nsec,
                        statp->st_gen, statp->st_data_version);
            break;
        }
        default:
            break;
        }
    }
    va_end(ap);

    return offset - old_offset;
}

static void complete_pdu(V9fsState *s, V9fsPDU *pdu, ssize_t len)
{
    int8_t id = pdu->id + 1; /* Response */

    if (len < 0) {
        int err = -len;
        len = 7;

        if (s->proto_version != V9FS_PROTO_2000L) {
            V9fsString str;

            str.data = strerror(err);
            str.size = strlen(str.data);

            len += pdu_marshal(pdu, len, "s", &str);
            id = P9_RERROR;
        }

        len += pdu_marshal(pdu, len, "d", err);

        if (s->proto_version == V9FS_PROTO_2000L) {
            id = P9_RLERROR;
        }
    }

    /* fill out the header */
    pdu_marshal(pdu, 0, "dbw", (int32_t)len, id, pdu->tag);

    /* keep these in sync */
    pdu->size = len;
    pdu->id = id;

    /* push onto queue and notify */
    virtqueue_push(s->vq, &pdu->elem, len);

    /* FIXME: we should batch these completions */
    virtio_notify(&s->vdev, s->vq);

    free_pdu(s, pdu);
}

static mode_t v9mode_to_mode(uint32_t mode, V9fsString *extension)
{
    mode_t ret;

    ret = mode & 0777;
    if (mode & P9_STAT_MODE_DIR) {
        ret |= S_IFDIR;
    }

    if (mode & P9_STAT_MODE_SYMLINK) {
        ret |= S_IFLNK;
    }
    if (mode & P9_STAT_MODE_SOCKET) {
        ret |= S_IFSOCK;
    }
    if (mode & P9_STAT_MODE_NAMED_PIPE) {
        ret |= S_IFIFO;
    }
    if (mode & P9_STAT_MODE_DEVICE) {
        if (extension && extension->data[0] == 'c') {
            ret |= S_IFCHR;
        } else {
            ret |= S_IFBLK;
        }
    }

    if (!(ret&~0777)) {
        ret |= S_IFREG;
    }

    if (mode & P9_STAT_MODE_SETUID) {
        ret |= S_ISUID;
    }
    if (mode & P9_STAT_MODE_SETGID) {
        ret |= S_ISGID;
    }
    if (mode & P9_STAT_MODE_SETVTX) {
        ret |= S_ISVTX;
    }

    return ret;
}

static int donttouch_stat(V9fsStat *stat)
{
    if (stat->type == -1 &&
        stat->dev == -1 &&
        stat->qid.type == -1 &&
        stat->qid.version == -1 &&
        stat->qid.path == -1 &&
        stat->mode == -1 &&
        stat->atime == -1 &&
        stat->mtime == -1 &&
        stat->length == -1 &&
        !stat->name.size &&
        !stat->uid.size &&
        !stat->gid.size &&
        !stat->muid.size &&
        stat->n_uid == -1 &&
        stat->n_gid == -1 &&
        stat->n_muid == -1) {
        return 1;
    }

    return 0;
}

static void v9fs_stat_free(V9fsStat *stat)
{
    v9fs_string_free(&stat->name);
    v9fs_string_free(&stat->uid);
    v9fs_string_free(&stat->gid);
    v9fs_string_free(&stat->muid);
    v9fs_string_free(&stat->extension);
}

static uint32_t stat_to_v9mode(const struct stat *stbuf)
{
    uint32_t mode;

    mode = stbuf->st_mode & 0777;
    if (S_ISDIR(stbuf->st_mode)) {
        mode |= P9_STAT_MODE_DIR;
    }

    if (S_ISLNK(stbuf->st_mode)) {
        mode |= P9_STAT_MODE_SYMLINK;
    }

    if (S_ISSOCK(stbuf->st_mode)) {
        mode |= P9_STAT_MODE_SOCKET;
    }

    if (S_ISFIFO(stbuf->st_mode)) {
        mode |= P9_STAT_MODE_NAMED_PIPE;
    }

    if (S_ISBLK(stbuf->st_mode) || S_ISCHR(stbuf->st_mode)) {
        mode |= P9_STAT_MODE_DEVICE;
    }

    if (stbuf->st_mode & S_ISUID) {
        mode |= P9_STAT_MODE_SETUID;
    }

    if (stbuf->st_mode & S_ISGID) {
        mode |= P9_STAT_MODE_SETGID;
    }

    if (stbuf->st_mode & S_ISVTX) {
        mode |= P9_STAT_MODE_SETVTX;
    }

    return mode;
}

static int stat_to_v9stat(V9fsState *s, V9fsString *name,
                            const struct stat *stbuf,
                            V9fsStat *v9stat)
{
    int err;
    const char *str;

    memset(v9stat, 0, sizeof(*v9stat));

    stat_to_qid(stbuf, &v9stat->qid);
    v9stat->mode = stat_to_v9mode(stbuf);
    v9stat->atime = stbuf->st_atime;
    v9stat->mtime = stbuf->st_mtime;
    v9stat->length = stbuf->st_size;

    v9fs_string_null(&v9stat->uid);
    v9fs_string_null(&v9stat->gid);
    v9fs_string_null(&v9stat->muid);

    v9stat->n_uid = stbuf->st_uid;
    v9stat->n_gid = stbuf->st_gid;
    v9stat->n_muid = 0;

    v9fs_string_null(&v9stat->extension);

    if (v9stat->mode & P9_STAT_MODE_SYMLINK) {
        err = v9fs_do_readlink(s, name, &v9stat->extension);
        if (err == -1) {
            err = -errno;
            return err;
        }
        v9stat->extension.data[err] = 0;
        v9stat->extension.size = err;
    } else if (v9stat->mode & P9_STAT_MODE_DEVICE) {
        v9fs_string_sprintf(&v9stat->extension, "%c %u %u",
                S_ISCHR(stbuf->st_mode) ? 'c' : 'b',
                major(stbuf->st_rdev), minor(stbuf->st_rdev));
    } else if (S_ISDIR(stbuf->st_mode) || S_ISREG(stbuf->st_mode)) {
        v9fs_string_sprintf(&v9stat->extension, "%s %lu",
                "HARDLINKCOUNT", (unsigned long)stbuf->st_nlink);
    }

    str = strrchr(name->data, '/');
    if (str) {
        str += 1;
    } else {
        str = name->data;
    }

    v9fs_string_sprintf(&v9stat->name, "%s", str);

    v9stat->size = 61 +
        v9fs_string_size(&v9stat->name) +
        v9fs_string_size(&v9stat->uid) +
        v9fs_string_size(&v9stat->gid) +
        v9fs_string_size(&v9stat->muid) +
        v9fs_string_size(&v9stat->extension);
    return 0;
}

#define P9_STATS_MODE          0x00000001ULL
#define P9_STATS_NLINK         0x00000002ULL
#define P9_STATS_UID           0x00000004ULL
#define P9_STATS_GID           0x00000008ULL
#define P9_STATS_RDEV          0x00000010ULL
#define P9_STATS_ATIME         0x00000020ULL
#define P9_STATS_MTIME         0x00000040ULL
#define P9_STATS_CTIME         0x00000080ULL
#define P9_STATS_INO           0x00000100ULL
#define P9_STATS_SIZE          0x00000200ULL
#define P9_STATS_BLOCKS        0x00000400ULL

#define P9_STATS_BTIME         0x00000800ULL
#define P9_STATS_GEN           0x00001000ULL
#define P9_STATS_DATA_VERSION  0x00002000ULL

#define P9_STATS_BASIC         0x000007ffULL /* Mask for fields up to BLOCKS */
#define P9_STATS_ALL           0x00003fffULL /* Mask for All fields above */


static void stat_to_v9stat_dotl(V9fsState *s, const struct stat *stbuf,
                            V9fsStatDotl *v9lstat)
{
    memset(v9lstat, 0, sizeof(*v9lstat));

    v9lstat->st_mode = stbuf->st_mode;
    v9lstat->st_nlink = stbuf->st_nlink;
    v9lstat->st_uid = stbuf->st_uid;
    v9lstat->st_gid = stbuf->st_gid;
    v9lstat->st_rdev = stbuf->st_rdev;
    v9lstat->st_size = stbuf->st_size;
    v9lstat->st_blksize = stbuf->st_blksize;
    v9lstat->st_blocks = stbuf->st_blocks;
    v9lstat->st_atime_sec = stbuf->st_atime;
    v9lstat->st_atime_nsec = stbuf->st_atim.tv_nsec;
    v9lstat->st_mtime_sec = stbuf->st_mtime;
    v9lstat->st_mtime_nsec = stbuf->st_mtim.tv_nsec;
    v9lstat->st_ctime_sec = stbuf->st_ctime;
    v9lstat->st_ctime_nsec = stbuf->st_ctim.tv_nsec;
    /* Currently we only support BASIC fields in stat */
    v9lstat->st_result_mask = P9_STATS_BASIC;

    stat_to_qid(stbuf, &v9lstat->qid);
}

static struct iovec *adjust_sg(struct iovec *sg, int len, int *iovcnt)
{
    while (len && *iovcnt) {
        if (len < sg->iov_len) {
            sg->iov_len -= len;
            sg->iov_base += len;
            len = 0;
        } else {
            len -= sg->iov_len;
            sg++;
            *iovcnt -= 1;
        }
    }

    return sg;
}

static struct iovec *cap_sg(struct iovec *sg, int cap, int *cnt)
{
    int i;
    int total = 0;

    for (i = 0; i < *cnt; i++) {
        if ((total + sg[i].iov_len) > cap) {
            sg[i].iov_len -= ((total + sg[i].iov_len) - cap);
            i++;
            break;
        }
        total += sg[i].iov_len;
    }

    *cnt = i;

    return sg;
}

static void print_sg(struct iovec *sg, int cnt)
{
    int i;

    printf("sg[%d]: {", cnt);
    for (i = 0; i < cnt; i++) {
        if (i) {
            printf(", ");
        }
        printf("(%p, %zd)", sg[i].iov_base, sg[i].iov_len);
    }
    printf("}\n");
}

static void v9fs_fix_path(V9fsString *dst, V9fsString *src, int len)
{
    V9fsString str;
    v9fs_string_init(&str);
    v9fs_string_copy(&str, dst);
    v9fs_string_sprintf(dst, "%s%s", src->data, str.data+len);
    v9fs_string_free(&str);
}

static void v9fs_version(V9fsState *s, V9fsPDU *pdu)
{
    V9fsString version;
    size_t offset = 7;

    pdu_unmarshal(pdu, offset, "ds", &s->msize, &version);

    if (!strcmp(version.data, "9P2000.u")) {
        s->proto_version = V9FS_PROTO_2000U;
    } else if (!strcmp(version.data, "9P2000.L")) {
        s->proto_version = V9FS_PROTO_2000L;
    } else {
        v9fs_string_sprintf(&version, "unknown");
    }

    offset += pdu_marshal(pdu, offset, "ds", s->msize, &version);
    complete_pdu(s, pdu, offset);

    v9fs_string_free(&version);
}

static void v9fs_attach(V9fsState *s, V9fsPDU *pdu)
{
    int32_t fid, afid, n_uname;
    V9fsString uname, aname;
    V9fsFidState *fidp;
    V9fsQID qid;
    size_t offset = 7;
    ssize_t err;

    pdu_unmarshal(pdu, offset, "ddssd", &fid, &afid, &uname, &aname, &n_uname);

    fidp = alloc_fid(s, fid);
    if (fidp == NULL) {
        err = -EINVAL;
        goto out;
    }

    fidp->uid = n_uname;

    v9fs_string_sprintf(&fidp->path, "%s", "/");
    err = fid_to_qid(s, fidp, &qid);
    if (err) {
        err = -EINVAL;
        free_fid(s, fid);
        goto out;
    }

    offset += pdu_marshal(pdu, offset, "Q", &qid);

    err = offset;
out:
    complete_pdu(s, pdu, err);
    v9fs_string_free(&uname);
    v9fs_string_free(&aname);
}

static void v9fs_stat_post_lstat(V9fsState *s, V9fsStatState *vs, int err)
{
    if (err == -1) {
        err = -errno;
        goto out;
    }

    err = stat_to_v9stat(s, &vs->fidp->path, &vs->stbuf, &vs->v9stat);
    if (err) {
        goto out;
    }
    vs->offset += pdu_marshal(vs->pdu, vs->offset, "wS", 0, &vs->v9stat);
    err = vs->offset;

out:
    complete_pdu(s, vs->pdu, err);
    v9fs_stat_free(&vs->v9stat);
    qemu_free(vs);
}

static void v9fs_stat(V9fsState *s, V9fsPDU *pdu)
{
    int32_t fid;
    V9fsStatState *vs;
    ssize_t err = 0;

    vs = qemu_malloc(sizeof(*vs));
    vs->pdu = pdu;
    vs->offset = 7;

    memset(&vs->v9stat, 0, sizeof(vs->v9stat));

    pdu_unmarshal(vs->pdu, vs->offset, "d", &fid);

    vs->fidp = lookup_fid(s, fid);
    if (vs->fidp == NULL) {
        err = -ENOENT;
        goto out;
    }

    err = v9fs_do_lstat(s, &vs->fidp->path, &vs->stbuf);
    v9fs_stat_post_lstat(s, vs, err);
    return;

out:
    complete_pdu(s, vs->pdu, err);
    v9fs_stat_free(&vs->v9stat);
    qemu_free(vs);
}

static void v9fs_getattr_post_lstat(V9fsState *s, V9fsStatStateDotl *vs,
                                                                int err)
{
    if (err == -1) {
        err = -errno;
        goto out;
    }

    stat_to_v9stat_dotl(s, &vs->stbuf, &vs->v9stat_dotl);
    vs->offset += pdu_marshal(vs->pdu, vs->offset, "A", &vs->v9stat_dotl);
    err = vs->offset;

out:
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
}

static void v9fs_getattr(V9fsState *s, V9fsPDU *pdu)
{
    int32_t fid;
    V9fsStatStateDotl *vs;
    ssize_t err = 0;
    V9fsFidState *fidp;
    uint64_t request_mask;

    vs = qemu_malloc(sizeof(*vs));
    vs->pdu = pdu;
    vs->offset = 7;

    memset(&vs->v9stat_dotl, 0, sizeof(vs->v9stat_dotl));

    pdu_unmarshal(vs->pdu, vs->offset, "dq", &fid, &request_mask);

    fidp = lookup_fid(s, fid);
    if (fidp == NULL) {
        err = -ENOENT;
        goto out;
    }

    /* Currently we only support BASIC fields in stat, so there is no
     * need to look at request_mask.
     */
    err = v9fs_do_lstat(s, &fidp->path, &vs->stbuf);
    v9fs_getattr_post_lstat(s, vs, err);
    return;

out:
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
}

/* From Linux kernel code */
#define ATTR_MODE    (1 << 0)
#define ATTR_UID     (1 << 1)
#define ATTR_GID     (1 << 2)
#define ATTR_SIZE    (1 << 3)
#define ATTR_ATIME   (1 << 4)
#define ATTR_MTIME   (1 << 5)
#define ATTR_CTIME   (1 << 6)
#define ATTR_MASK    127
#define ATTR_ATIME_SET  (1 << 7)
#define ATTR_MTIME_SET  (1 << 8)

static void v9fs_setattr_post_truncate(V9fsState *s, V9fsSetattrState *vs,
                                                                  int err)
{
    if (err == -1) {
        err = -errno;
        goto out;
    }
    err = vs->offset;

out:
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
}

static void v9fs_setattr_post_chown(V9fsState *s, V9fsSetattrState *vs, int err)
{
    if (err == -1) {
        err = -errno;
        goto out;
    }

    if (vs->v9iattr.valid & (ATTR_SIZE)) {
        err = v9fs_do_truncate(s, &vs->fidp->path, vs->v9iattr.size);
    }
    v9fs_setattr_post_truncate(s, vs, err);
    return;

out:
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
}

static void v9fs_setattr_post_utimensat(V9fsState *s, V9fsSetattrState *vs,
                                                                   int err)
{
    if (err == -1) {
        err = -errno;
        goto out;
    }

    /* If the only valid entry in iattr is ctime we can call
     * chown(-1,-1) to update the ctime of the file
     */
    if ((vs->v9iattr.valid & (ATTR_UID | ATTR_GID)) ||
            ((vs->v9iattr.valid & ATTR_CTIME)
            && !((vs->v9iattr.valid & ATTR_MASK) & ~ATTR_CTIME))) {
        if (!(vs->v9iattr.valid & ATTR_UID)) {
            vs->v9iattr.uid = -1;
        }
        if (!(vs->v9iattr.valid & ATTR_GID)) {
            vs->v9iattr.gid = -1;
        }
        err = v9fs_do_chown(s, &vs->fidp->path, vs->v9iattr.uid,
                                                vs->v9iattr.gid);
    }
    v9fs_setattr_post_chown(s, vs, err);
    return;

out:
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
}

static void v9fs_setattr_post_chmod(V9fsState *s, V9fsSetattrState *vs, int err)
{
    if (err == -1) {
        err = -errno;
        goto out;
    }

    if (vs->v9iattr.valid & (ATTR_ATIME | ATTR_MTIME)) {
        struct timespec times[2];
        if (vs->v9iattr.valid & ATTR_ATIME) {
            if (vs->v9iattr.valid & ATTR_ATIME_SET) {
                times[0].tv_sec = vs->v9iattr.atime_sec;
                times[0].tv_nsec = vs->v9iattr.atime_nsec;
            } else {
                times[0].tv_nsec = UTIME_NOW;
            }
        } else {
            times[0].tv_nsec = UTIME_OMIT;
        }

        if (vs->v9iattr.valid & ATTR_MTIME) {
            if (vs->v9iattr.valid & ATTR_MTIME_SET) {
                times[1].tv_sec = vs->v9iattr.mtime_sec;
                times[1].tv_nsec = vs->v9iattr.mtime_nsec;
            } else {
                times[1].tv_nsec = UTIME_NOW;
            }
        } else {
            times[1].tv_nsec = UTIME_OMIT;
        }
        err = v9fs_do_utimensat(s, &vs->fidp->path, times);
    }
    v9fs_setattr_post_utimensat(s, vs, err);
    return;

out:
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
}

static void v9fs_setattr(V9fsState *s, V9fsPDU *pdu)
{
    int32_t fid;
    V9fsSetattrState *vs;
    int err = 0;

    vs = qemu_malloc(sizeof(*vs));
    vs->pdu = pdu;
    vs->offset = 7;

    pdu_unmarshal(pdu, vs->offset, "dI", &fid, &vs->v9iattr);

    vs->fidp = lookup_fid(s, fid);
    if (vs->fidp == NULL) {
        err = -EINVAL;
        goto out;
    }

    if (vs->v9iattr.valid & ATTR_MODE) {
        err = v9fs_do_chmod(s, &vs->fidp->path, vs->v9iattr.mode);
    }

    v9fs_setattr_post_chmod(s, vs, err);
    return;

out:
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
}

static void v9fs_walk_complete(V9fsState *s, V9fsWalkState *vs, int err)
{
    complete_pdu(s, vs->pdu, err);

    if (vs->nwnames) {
        for (vs->name_idx = 0; vs->name_idx < vs->nwnames; vs->name_idx++) {
            v9fs_string_free(&vs->wnames[vs->name_idx]);
        }

        qemu_free(vs->wnames);
        qemu_free(vs->qids);
    }
}

static void v9fs_walk_marshal(V9fsWalkState *vs)
{
    int i;
    vs->offset = 7;
    vs->offset += pdu_marshal(vs->pdu, vs->offset, "w", vs->nwnames);

    for (i = 0; i < vs->nwnames; i++) {
        vs->offset += pdu_marshal(vs->pdu, vs->offset, "Q", &vs->qids[i]);
    }
}

static void v9fs_walk_post_newfid_lstat(V9fsState *s, V9fsWalkState *vs,
                                                                int err)
{
    if (err == -1) {
        free_fid(s, vs->newfidp->fid);
        v9fs_string_free(&vs->path);
        err = -ENOENT;
        goto out;
    }

    stat_to_qid(&vs->stbuf, &vs->qids[vs->name_idx]);

    vs->name_idx++;
    if (vs->name_idx < vs->nwnames) {
        v9fs_string_sprintf(&vs->path, "%s/%s", vs->newfidp->path.data,
                                            vs->wnames[vs->name_idx].data);
        v9fs_string_copy(&vs->newfidp->path, &vs->path);

        err = v9fs_do_lstat(s, &vs->newfidp->path, &vs->stbuf);
        v9fs_walk_post_newfid_lstat(s, vs, err);
        return;
    }

    v9fs_string_free(&vs->path);
    v9fs_walk_marshal(vs);
    err = vs->offset;
out:
    v9fs_walk_complete(s, vs, err);
}

static void v9fs_walk_post_oldfid_lstat(V9fsState *s, V9fsWalkState *vs,
        int err)
{
    if (err == -1) {
        v9fs_string_free(&vs->path);
        err = -ENOENT;
        goto out;
    }

    stat_to_qid(&vs->stbuf, &vs->qids[vs->name_idx]);
    vs->name_idx++;
    if (vs->name_idx < vs->nwnames) {

        v9fs_string_sprintf(&vs->path, "%s/%s",
                vs->fidp->path.data, vs->wnames[vs->name_idx].data);
        v9fs_string_copy(&vs->fidp->path, &vs->path);

        err = v9fs_do_lstat(s, &vs->fidp->path, &vs->stbuf);
        v9fs_walk_post_oldfid_lstat(s, vs, err);
        return;
    }

    v9fs_string_free(&vs->path);
    v9fs_walk_marshal(vs);
    err = vs->offset;
out:
    v9fs_walk_complete(s, vs, err);
}

static void v9fs_walk(V9fsState *s, V9fsPDU *pdu)
{
    int32_t fid, newfid;
    V9fsWalkState *vs;
    int err = 0;
    int i;

    vs = qemu_malloc(sizeof(*vs));
    vs->pdu = pdu;
    vs->wnames = NULL;
    vs->qids = NULL;
    vs->offset = 7;

    vs->offset += pdu_unmarshal(vs->pdu, vs->offset, "ddw", &fid,
                                            &newfid, &vs->nwnames);

    if (vs->nwnames) {
        vs->wnames = qemu_mallocz(sizeof(vs->wnames[0]) * vs->nwnames);

        vs->qids = qemu_mallocz(sizeof(vs->qids[0]) * vs->nwnames);

        for (i = 0; i < vs->nwnames; i++) {
            vs->offset += pdu_unmarshal(vs->pdu, vs->offset, "s",
                                            &vs->wnames[i]);
        }
    }

    vs->fidp = lookup_fid(s, fid);
    if (vs->fidp == NULL) {
        err = -ENOENT;
        goto out;
    }

    /* FIXME: is this really valid? */
    if (fid == newfid) {

        BUG_ON(vs->fidp->fid_type != P9_FID_NONE);
        v9fs_string_init(&vs->path);
        vs->name_idx = 0;

        if (vs->name_idx < vs->nwnames) {
            v9fs_string_sprintf(&vs->path, "%s/%s",
                vs->fidp->path.data, vs->wnames[vs->name_idx].data);
            v9fs_string_copy(&vs->fidp->path, &vs->path);

            err = v9fs_do_lstat(s, &vs->fidp->path, &vs->stbuf);
            v9fs_walk_post_oldfid_lstat(s, vs, err);
            return;
        }
    } else {
        vs->newfidp = alloc_fid(s, newfid);
        if (vs->newfidp == NULL) {
            err = -EINVAL;
            goto out;
        }

        vs->newfidp->uid = vs->fidp->uid;
        v9fs_string_init(&vs->path);
        vs->name_idx = 0;
        v9fs_string_copy(&vs->newfidp->path, &vs->fidp->path);

        if (vs->name_idx < vs->nwnames) {
            v9fs_string_sprintf(&vs->path, "%s/%s", vs->newfidp->path.data,
                                vs->wnames[vs->name_idx].data);
            v9fs_string_copy(&vs->newfidp->path, &vs->path);

            err = v9fs_do_lstat(s, &vs->newfidp->path, &vs->stbuf);
            v9fs_walk_post_newfid_lstat(s, vs, err);
            return;
        }
    }

    v9fs_walk_marshal(vs);
    err = vs->offset;
out:
    v9fs_walk_complete(s, vs, err);
}

static int32_t get_iounit(V9fsState *s, V9fsString *name)
{
    struct statfs stbuf;
    int32_t iounit = 0;

    /*
     * iounit should be multiples of f_bsize (host filesystem block size
     * and as well as less than (client msize - P9_IOHDRSZ))
     */
    if (!v9fs_do_statfs(s, name, &stbuf)) {
        iounit = stbuf.f_bsize;
        iounit *= (s->msize - P9_IOHDRSZ)/stbuf.f_bsize;
    }

    if (!iounit) {
        iounit = s->msize - P9_IOHDRSZ;
    }
    return iounit;
}

static void v9fs_open_post_opendir(V9fsState *s, V9fsOpenState *vs, int err)
{
    if (vs->fidp->fs.dir == NULL) {
        err = -errno;
        goto out;
    }
    vs->fidp->fid_type = P9_FID_DIR;
    vs->offset += pdu_marshal(vs->pdu, vs->offset, "Qd", &vs->qid, 0);
    err = vs->offset;
out:
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);

}

static void v9fs_open_post_getiounit(V9fsState *s, V9fsOpenState *vs)
{
    int err;
    vs->offset += pdu_marshal(vs->pdu, vs->offset, "Qd", &vs->qid, vs->iounit);
    err = vs->offset;
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
}

static void v9fs_open_post_open(V9fsState *s, V9fsOpenState *vs, int err)
{
    if (vs->fidp->fs.fd == -1) {
        err = -errno;
        goto out;
    }
    vs->fidp->fid_type = P9_FID_FILE;
    vs->iounit = get_iounit(s, &vs->fidp->path);
    v9fs_open_post_getiounit(s, vs);
    return;
out:
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
}

static void v9fs_open_post_lstat(V9fsState *s, V9fsOpenState *vs, int err)
{
    int flags;

    if (err) {
        err = -errno;
        goto out;
    }

    stat_to_qid(&vs->stbuf, &vs->qid);

    if (S_ISDIR(vs->stbuf.st_mode)) {
        vs->fidp->fs.dir = v9fs_do_opendir(s, &vs->fidp->path);
        v9fs_open_post_opendir(s, vs, err);
    } else {
        if (s->proto_version == V9FS_PROTO_2000L) {
            flags = vs->mode;
            flags &= ~(O_NOCTTY | O_ASYNC | O_CREAT);
            /* Ignore direct disk access hint until the server supports it. */
            flags &= ~O_DIRECT;
        } else {
            flags = omode_to_uflags(vs->mode);
        }
        vs->fidp->fs.fd = v9fs_do_open(s, &vs->fidp->path, flags);
        v9fs_open_post_open(s, vs, err);
    }
    return;
out:
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
}

static void v9fs_open(V9fsState *s, V9fsPDU *pdu)
{
    int32_t fid;
    V9fsOpenState *vs;
    ssize_t err = 0;

    vs = qemu_malloc(sizeof(*vs));
    vs->pdu = pdu;
    vs->offset = 7;
    vs->mode = 0;

    if (s->proto_version == V9FS_PROTO_2000L) {
        pdu_unmarshal(vs->pdu, vs->offset, "dd", &fid, &vs->mode);
    } else {
        pdu_unmarshal(vs->pdu, vs->offset, "db", &fid, &vs->mode);
    }

    vs->fidp = lookup_fid(s, fid);
    if (vs->fidp == NULL) {
        err = -ENOENT;
        goto out;
    }

    BUG_ON(vs->fidp->fid_type != P9_FID_NONE);

    err = v9fs_do_lstat(s, &vs->fidp->path, &vs->stbuf);

    v9fs_open_post_lstat(s, vs, err);
    return;
out:
    complete_pdu(s, pdu, err);
    qemu_free(vs);
}

static void v9fs_post_lcreate(V9fsState *s, V9fsLcreateState *vs, int err)
{
    if (err == 0) {
        v9fs_string_copy(&vs->fidp->path, &vs->fullname);
        stat_to_qid(&vs->stbuf, &vs->qid);
        vs->offset += pdu_marshal(vs->pdu, vs->offset, "Qd", &vs->qid,
                &vs->iounit);
        err = vs->offset;
    } else {
        vs->fidp->fid_type = P9_FID_NONE;
        err = -errno;
        if (vs->fidp->fs.fd > 0) {
            close(vs->fidp->fs.fd);
        }
    }

    complete_pdu(s, vs->pdu, err);
    v9fs_string_free(&vs->name);
    v9fs_string_free(&vs->fullname);
    qemu_free(vs);
}

static void v9fs_lcreate_post_get_iounit(V9fsState *s, V9fsLcreateState *vs,
        int err)
{
    if (err) {
        err = -errno;
        goto out;
    }
    err = v9fs_do_lstat(s, &vs->fullname, &vs->stbuf);

out:
    v9fs_post_lcreate(s, vs, err);
}

static void v9fs_lcreate_post_do_open2(V9fsState *s, V9fsLcreateState *vs,
        int err)
{
    if (vs->fidp->fs.fd == -1) {
        err = -errno;
        goto out;
    }
    vs->fidp->fid_type = P9_FID_FILE;
    vs->iounit =  get_iounit(s, &vs->fullname);
    v9fs_lcreate_post_get_iounit(s, vs, err);
    return;

out:
    v9fs_post_lcreate(s, vs, err);
}

static void v9fs_lcreate(V9fsState *s, V9fsPDU *pdu)
{
    int32_t dfid, flags, mode;
    gid_t gid;
    V9fsLcreateState *vs;
    ssize_t err = 0;

    vs = qemu_malloc(sizeof(*vs));
    vs->pdu = pdu;
    vs->offset = 7;

    v9fs_string_init(&vs->fullname);

    pdu_unmarshal(vs->pdu, vs->offset, "dsddd", &dfid, &vs->name, &flags,
            &mode, &gid);

    vs->fidp = lookup_fid(s, dfid);
    if (vs->fidp == NULL) {
        err = -ENOENT;
        goto out;
    }

    v9fs_string_sprintf(&vs->fullname, "%s/%s", vs->fidp->path.data,
             vs->name.data);

    /* Ignore direct disk access hint until the server supports it. */
    flags &= ~O_DIRECT;

    vs->fidp->fs.fd = v9fs_do_open2(s, vs->fullname.data, vs->fidp->uid,
            gid, flags, mode);
    v9fs_lcreate_post_do_open2(s, vs, err);
    return;

out:
    complete_pdu(s, vs->pdu, err);
    v9fs_string_free(&vs->name);
    qemu_free(vs);
}

static void v9fs_post_do_fsync(V9fsState *s, V9fsPDU *pdu, int err)
{
    if (err == -1) {
        err = -errno;
    }
    complete_pdu(s, pdu, err);
}

static void v9fs_fsync(V9fsState *s, V9fsPDU *pdu)
{
    int32_t fid;
    size_t offset = 7;
    V9fsFidState *fidp;
    int datasync;
    int err;

    pdu_unmarshal(pdu, offset, "dd", &fid, &datasync);
    fidp = lookup_fid(s, fid);
    if (fidp == NULL) {
        err = -ENOENT;
        v9fs_post_do_fsync(s, pdu, err);
        return;
    }
    err = v9fs_do_fsync(s, fidp->fs.fd, datasync);
    v9fs_post_do_fsync(s, pdu, err);
}

static void v9fs_clunk(V9fsState *s, V9fsPDU *pdu)
{
    int32_t fid;
    size_t offset = 7;
    int err;

    pdu_unmarshal(pdu, offset, "d", &fid);

    err = free_fid(s, fid);
    if (err < 0) {
        goto out;
    }

    offset = 7;
    err = offset;
out:
    complete_pdu(s, pdu, err);
}

static void v9fs_read_post_readdir(V9fsState *, V9fsReadState *, ssize_t);

static void v9fs_read_post_seekdir(V9fsState *s, V9fsReadState *vs, ssize_t err)
{
    if (err) {
        goto out;
    }
    v9fs_stat_free(&vs->v9stat);
    v9fs_string_free(&vs->name);
    vs->offset += pdu_marshal(vs->pdu, vs->offset, "d", vs->count);
    vs->offset += vs->count;
    err = vs->offset;
out:
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
    return;
}

static void v9fs_read_post_dir_lstat(V9fsState *s, V9fsReadState *vs,
                                    ssize_t err)
{
    if (err) {
        err = -errno;
        goto out;
    }
    err = stat_to_v9stat(s, &vs->name, &vs->stbuf, &vs->v9stat);
    if (err) {
        goto out;
    }

    vs->len = pdu_marshal(vs->pdu, vs->offset + 4 + vs->count, "S",
                            &vs->v9stat);
    if ((vs->len != (vs->v9stat.size + 2)) ||
            ((vs->count + vs->len) > vs->max_count)) {
        v9fs_do_seekdir(s, vs->fidp->fs.dir, vs->dir_pos);
        v9fs_read_post_seekdir(s, vs, err);
        return;
    }
    vs->count += vs->len;
    v9fs_stat_free(&vs->v9stat);
    v9fs_string_free(&vs->name);
    vs->dir_pos = vs->dent->d_off;
    vs->dent = v9fs_do_readdir(s, vs->fidp->fs.dir);
    v9fs_read_post_readdir(s, vs, err);
    return;
out:
    v9fs_do_seekdir(s, vs->fidp->fs.dir, vs->dir_pos);
    v9fs_read_post_seekdir(s, vs, err);
    return;

}

static void v9fs_read_post_readdir(V9fsState *s, V9fsReadState *vs, ssize_t err)
{
    if (vs->dent) {
        memset(&vs->v9stat, 0, sizeof(vs->v9stat));
        v9fs_string_init(&vs->name);
        v9fs_string_sprintf(&vs->name, "%s/%s", vs->fidp->path.data,
                            vs->dent->d_name);
        err = v9fs_do_lstat(s, &vs->name, &vs->stbuf);
        v9fs_read_post_dir_lstat(s, vs, err);
        return;
    }

    vs->offset += pdu_marshal(vs->pdu, vs->offset, "d", vs->count);
    vs->offset += vs->count;
    err = vs->offset;
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
    return;
}

static void v9fs_read_post_telldir(V9fsState *s, V9fsReadState *vs, ssize_t err)
{
    vs->dent = v9fs_do_readdir(s, vs->fidp->fs.dir);
    v9fs_read_post_readdir(s, vs, err);
    return;
}

static void v9fs_read_post_rewinddir(V9fsState *s, V9fsReadState *vs,
                                       ssize_t err)
{
    vs->dir_pos = v9fs_do_telldir(s, vs->fidp->fs.dir);
    v9fs_read_post_telldir(s, vs, err);
    return;
}

static void v9fs_read_post_preadv(V9fsState *s, V9fsReadState *vs, ssize_t err)
{
    if (err  < 0) {
        /* IO error return the error */
        err = -errno;
        goto out;
    }
    vs->total += vs->len;
    vs->sg = adjust_sg(vs->sg, vs->len, &vs->cnt);
    if (vs->total < vs->count && vs->len > 0) {
        do {
            if (0) {
                print_sg(vs->sg, vs->cnt);
            }
            vs->len = v9fs_do_preadv(s, vs->fidp->fs.fd, vs->sg, vs->cnt,
                      vs->off);
            if (vs->len > 0) {
                vs->off += vs->len;
            }
        } while (vs->len == -1 && errno == EINTR);
        if (vs->len == -1) {
            err  = -errno;
        }
        v9fs_read_post_preadv(s, vs, err);
        return;
    }
    vs->offset += pdu_marshal(vs->pdu, vs->offset, "d", vs->total);
    vs->offset += vs->count;
    err = vs->offset;

out:
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
}

static void v9fs_xattr_read(V9fsState *s, V9fsReadState *vs)
{
    ssize_t err = 0;
    int read_count;
    int64_t xattr_len;

    xattr_len = vs->fidp->fs.xattr.len;
    read_count = xattr_len - vs->off;
    if (read_count > vs->count) {
        read_count = vs->count;
    } else if (read_count < 0) {
        /*
         * read beyond XATTR value
         */
        read_count = 0;
    }
    vs->offset += pdu_marshal(vs->pdu, vs->offset, "d", read_count);
    vs->offset += pdu_pack(vs->pdu, vs->offset,
                           ((char *)vs->fidp->fs.xattr.value) + vs->off,
                           read_count);
    err = vs->offset;
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
}

static void v9fs_read(V9fsState *s, V9fsPDU *pdu)
{
    int32_t fid;
    V9fsReadState *vs;
    ssize_t err = 0;

    vs = qemu_malloc(sizeof(*vs));
    vs->pdu = pdu;
    vs->offset = 7;
    vs->total = 0;
    vs->len = 0;
    vs->count = 0;

    pdu_unmarshal(vs->pdu, vs->offset, "dqd", &fid, &vs->off, &vs->count);

    vs->fidp = lookup_fid(s, fid);
    if (vs->fidp == NULL) {
        err = -EINVAL;
        goto out;
    }

    if (vs->fidp->fid_type == P9_FID_DIR) {
        vs->max_count = vs->count;
        vs->count = 0;
        if (vs->off == 0) {
            v9fs_do_rewinddir(s, vs->fidp->fs.dir);
        }
        v9fs_read_post_rewinddir(s, vs, err);
        return;
    } else if (vs->fidp->fid_type == P9_FID_FILE) {
        vs->sg = vs->iov;
        pdu_marshal(vs->pdu, vs->offset + 4, "v", vs->sg, &vs->cnt);
        vs->sg = cap_sg(vs->sg, vs->count, &vs->cnt);
        if (vs->total <= vs->count) {
            vs->len = v9fs_do_preadv(s, vs->fidp->fs.fd, vs->sg, vs->cnt,
                                    vs->off);
            if (vs->len > 0) {
                vs->off += vs->len;
            }
            err = vs->len;
            v9fs_read_post_preadv(s, vs, err);
        }
        return;
    } else if (vs->fidp->fid_type == P9_FID_XATTR) {
        v9fs_xattr_read(s, vs);
        return;
    } else {
        err = -EINVAL;
    }
out:
    complete_pdu(s, pdu, err);
    qemu_free(vs);
}

typedef struct V9fsReadDirState {
    V9fsPDU *pdu;
    V9fsFidState *fidp;
    V9fsQID qid;
    off_t saved_dir_pos;
    struct dirent *dent;
    int32_t count;
    int32_t max_count;
    size_t offset;
    int64_t initial_offset;
    V9fsString name;
} V9fsReadDirState;

static void v9fs_readdir_post_seekdir(V9fsState *s, V9fsReadDirState *vs)
{
    vs->offset += pdu_marshal(vs->pdu, vs->offset, "d", vs->count);
    vs->offset += vs->count;
    complete_pdu(s, vs->pdu, vs->offset);
    qemu_free(vs);
    return;
}

/* Size of each dirent on the wire: size of qid (13) + size of offset (8)
 * size of type (1) + size of name.size (2) + strlen(name.data)
 */
#define V9_READDIR_DATA_SZ (24 + strlen(vs->name.data))

static void v9fs_readdir_post_readdir(V9fsState *s, V9fsReadDirState *vs)
{
    int len;
    size_t size;

    if (vs->dent) {
        v9fs_string_init(&vs->name);
        v9fs_string_sprintf(&vs->name, "%s", vs->dent->d_name);

        if ((vs->count + V9_READDIR_DATA_SZ) > vs->max_count) {
            /* Ran out of buffer. Set dir back to old position and return */
            v9fs_do_seekdir(s, vs->fidp->fs.dir, vs->saved_dir_pos);
            v9fs_readdir_post_seekdir(s, vs);
            return;
        }

        /* Fill up just the path field of qid because the client uses
         * only that. To fill the entire qid structure we will have
         * to stat each dirent found, which is expensive
         */
        size = MIN(sizeof(vs->dent->d_ino), sizeof(vs->qid.path));
        memcpy(&vs->qid.path, &vs->dent->d_ino, size);
        /* Fill the other fields with dummy values */
        vs->qid.type = 0;
        vs->qid.version = 0;

        len = pdu_marshal(vs->pdu, vs->offset+4+vs->count, "Qqbs",
                              &vs->qid, vs->dent->d_off,
                              vs->dent->d_type, &vs->name);
        vs->count += len;
        v9fs_string_free(&vs->name);
        vs->saved_dir_pos = vs->dent->d_off;
        vs->dent = v9fs_do_readdir(s, vs->fidp->fs.dir);
        v9fs_readdir_post_readdir(s, vs);
        return;
    }

    vs->offset += pdu_marshal(vs->pdu, vs->offset, "d", vs->count);
    vs->offset += vs->count;
    complete_pdu(s, vs->pdu, vs->offset);
    qemu_free(vs);
    return;
}

static void v9fs_readdir_post_telldir(V9fsState *s, V9fsReadDirState *vs)
{
    vs->dent = v9fs_do_readdir(s, vs->fidp->fs.dir);
    v9fs_readdir_post_readdir(s, vs);
    return;
}

static void v9fs_readdir_post_setdir(V9fsState *s, V9fsReadDirState *vs)
{
    vs->saved_dir_pos = v9fs_do_telldir(s, vs->fidp->fs.dir);
    v9fs_readdir_post_telldir(s, vs);
    return;
}

static void v9fs_readdir(V9fsState *s, V9fsPDU *pdu)
{
    int32_t fid;
    V9fsReadDirState *vs;
    ssize_t err = 0;
    size_t offset = 7;

    vs = qemu_malloc(sizeof(*vs));
    vs->pdu = pdu;
    vs->offset = 7;
    vs->count = 0;

    pdu_unmarshal(vs->pdu, offset, "dqd", &fid, &vs->initial_offset,
                                                        &vs->max_count);

    vs->fidp = lookup_fid(s, fid);
    if (vs->fidp == NULL || !(vs->fidp->fs.dir)) {
        err = -EINVAL;
        goto out;
    }

    if (vs->initial_offset == 0) {
        v9fs_do_rewinddir(s, vs->fidp->fs.dir);
    } else {
        v9fs_do_seekdir(s, vs->fidp->fs.dir, vs->initial_offset);
    }

    v9fs_readdir_post_setdir(s, vs);
    return;

out:
    complete_pdu(s, pdu, err);
    qemu_free(vs);
    return;
}

static void v9fs_write_post_pwritev(V9fsState *s, V9fsWriteState *vs,
                                   ssize_t err)
{
    if (err  < 0) {
        /* IO error return the error */
        err = -errno;
        goto out;
    }
    vs->total += vs->len;
    vs->sg = adjust_sg(vs->sg, vs->len, &vs->cnt);
    if (vs->total < vs->count && vs->len > 0) {
        do {
            if (0) {
                print_sg(vs->sg, vs->cnt);
            }
            vs->len = v9fs_do_pwritev(s, vs->fidp->fs.fd, vs->sg, vs->cnt,
                      vs->off);
            if (vs->len > 0) {
                vs->off += vs->len;
            }
        } while (vs->len == -1 && errno == EINTR);
        if (vs->len == -1) {
            err  = -errno;
        }
        v9fs_write_post_pwritev(s, vs, err);
        return;
    }
    vs->offset += pdu_marshal(vs->pdu, vs->offset, "d", vs->total);
    err = vs->offset;
out:
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
}

static void v9fs_xattr_write(V9fsState *s, V9fsWriteState *vs)
{
    int i, to_copy;
    ssize_t err = 0;
    int write_count;
    int64_t xattr_len;

    xattr_len = vs->fidp->fs.xattr.len;
    write_count = xattr_len - vs->off;
    if (write_count > vs->count) {
        write_count = vs->count;
    } else if (write_count < 0) {
        /*
         * write beyond XATTR value len specified in
         * xattrcreate
         */
        err = -ENOSPC;
        goto out;
    }
    vs->offset += pdu_marshal(vs->pdu, vs->offset, "d", write_count);
    err = vs->offset;
    vs->fidp->fs.xattr.copied_len += write_count;
    /*
     * Now copy the content from sg list
     */
    for (i = 0; i < vs->cnt; i++) {
        if (write_count > vs->sg[i].iov_len) {
            to_copy = vs->sg[i].iov_len;
        } else {
            to_copy = write_count;
        }
        memcpy((char *)vs->fidp->fs.xattr.value + vs->off,
               vs->sg[i].iov_base, to_copy);
        /* updating vs->off since we are not using below */
        vs->off += to_copy;
        write_count -= to_copy;
    }
out:
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
}

static void v9fs_write(V9fsState *s, V9fsPDU *pdu)
{
    int32_t fid;
    V9fsWriteState *vs;
    ssize_t err;

    vs = qemu_malloc(sizeof(*vs));

    vs->pdu = pdu;
    vs->offset = 7;
    vs->sg = vs->iov;
    vs->total = 0;
    vs->len = 0;

    pdu_unmarshal(vs->pdu, vs->offset, "dqdv", &fid, &vs->off, &vs->count,
                  vs->sg, &vs->cnt);

    vs->fidp = lookup_fid(s, fid);
    if (vs->fidp == NULL) {
        err = -EINVAL;
        goto out;
    }

    if (vs->fidp->fid_type == P9_FID_FILE) {
        if (vs->fidp->fs.fd == -1) {
            err = -EINVAL;
            goto out;
        }
    } else if (vs->fidp->fid_type == P9_FID_XATTR) {
        /*
         * setxattr operation
         */
        v9fs_xattr_write(s, vs);
        return;
    } else {
        err = -EINVAL;
        goto out;
    }
    vs->sg = cap_sg(vs->sg, vs->count, &vs->cnt);
    if (vs->total <= vs->count) {
        vs->len = v9fs_do_pwritev(s, vs->fidp->fs.fd, vs->sg, vs->cnt, vs->off);
        if (vs->len > 0) {
            vs->off += vs->len;
        }
        err = vs->len;
        v9fs_write_post_pwritev(s, vs, err);
    }
    return;
out:
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
}

static void v9fs_create_post_getiounit(V9fsState *s, V9fsCreateState *vs)
{
    int err;
    v9fs_string_copy(&vs->fidp->path, &vs->fullname);
    stat_to_qid(&vs->stbuf, &vs->qid);

    vs->offset += pdu_marshal(vs->pdu, vs->offset, "Qd", &vs->qid, vs->iounit);
    err = vs->offset;

    complete_pdu(s, vs->pdu, err);
    v9fs_string_free(&vs->name);
    v9fs_string_free(&vs->extension);
    v9fs_string_free(&vs->fullname);
    qemu_free(vs);
}

static void v9fs_post_create(V9fsState *s, V9fsCreateState *vs, int err)
{
    if (err == 0) {
        vs->iounit = get_iounit(s, &vs->fidp->path);
        v9fs_create_post_getiounit(s, vs);
        return;
    }

    complete_pdu(s, vs->pdu, err);
    v9fs_string_free(&vs->name);
    v9fs_string_free(&vs->extension);
    v9fs_string_free(&vs->fullname);
    qemu_free(vs);
}

static void v9fs_create_post_perms(V9fsState *s, V9fsCreateState *vs, int err)
{
    if (err) {
        err = -errno;
    }
    v9fs_post_create(s, vs, err);
}

static void v9fs_create_post_opendir(V9fsState *s, V9fsCreateState *vs,
                                                                    int err)
{
    if (!vs->fidp->fs.dir) {
        err = -errno;
    }
    vs->fidp->fid_type = P9_FID_DIR;
    v9fs_post_create(s, vs, err);
}

static void v9fs_create_post_dir_lstat(V9fsState *s, V9fsCreateState *vs,
                                                                    int err)
{
    if (err) {
        err = -errno;
        goto out;
    }

    vs->fidp->fs.dir = v9fs_do_opendir(s, &vs->fullname);
    v9fs_create_post_opendir(s, vs, err);
    return;

out:
    v9fs_post_create(s, vs, err);
}

static void v9fs_create_post_mkdir(V9fsState *s, V9fsCreateState *vs, int err)
{
    if (err) {
        err = -errno;
        goto out;
    }

    err = v9fs_do_lstat(s, &vs->fullname, &vs->stbuf);
    v9fs_create_post_dir_lstat(s, vs, err);
    return;

out:
    v9fs_post_create(s, vs, err);
}

static void v9fs_create_post_fstat(V9fsState *s, V9fsCreateState *vs, int err)
{
    if (err) {
        vs->fidp->fid_type = P9_FID_NONE;
        close(vs->fidp->fs.fd);
        err = -errno;
    }
    v9fs_post_create(s, vs, err);
    return;
}

static void v9fs_create_post_open2(V9fsState *s, V9fsCreateState *vs, int err)
{
    if (vs->fidp->fs.fd == -1) {
        err = -errno;
        goto out;
    }
    vs->fidp->fid_type = P9_FID_FILE;
    err = v9fs_do_fstat(s, vs->fidp->fs.fd, &vs->stbuf);
    v9fs_create_post_fstat(s, vs, err);

    return;

out:
    v9fs_post_create(s, vs, err);

}

static void v9fs_create_post_lstat(V9fsState *s, V9fsCreateState *vs, int err)
{

    if (err == 0 || errno != ENOENT) {
        err = -errno;
        goto out;
    }

    if (vs->perm & P9_STAT_MODE_DIR) {
        err = v9fs_do_mkdir(s, vs->fullname.data, vs->perm & 0777,
                vs->fidp->uid, -1);
        v9fs_create_post_mkdir(s, vs, err);
    } else if (vs->perm & P9_STAT_MODE_SYMLINK) {
        err = v9fs_do_symlink(s, vs->fidp, vs->extension.data,
                vs->fullname.data, -1);
        v9fs_create_post_perms(s, vs, err);
    } else if (vs->perm & P9_STAT_MODE_LINK) {
        int32_t nfid = atoi(vs->extension.data);
        V9fsFidState *nfidp = lookup_fid(s, nfid);
        if (nfidp == NULL) {
            err = -errno;
            v9fs_post_create(s, vs, err);
        }
        err = v9fs_do_link(s, &nfidp->path, &vs->fullname);
        v9fs_create_post_perms(s, vs, err);
    } else if (vs->perm & P9_STAT_MODE_DEVICE) {
        char ctype;
        uint32_t major, minor;
        mode_t nmode = 0;

        if (sscanf(vs->extension.data, "%c %u %u", &ctype, &major,
                                        &minor) != 3) {
            err = -errno;
            v9fs_post_create(s, vs, err);
        }

        switch (ctype) {
        case 'c':
            nmode = S_IFCHR;
            break;
        case 'b':
            nmode = S_IFBLK;
            break;
        default:
            err = -EIO;
            v9fs_post_create(s, vs, err);
        }

        nmode |= vs->perm & 0777;
        err = v9fs_do_mknod(s, vs->fullname.data, nmode,
                makedev(major, minor), vs->fidp->uid, -1);
        v9fs_create_post_perms(s, vs, err);
    } else if (vs->perm & P9_STAT_MODE_NAMED_PIPE) {
        err = v9fs_do_mknod(s, vs->fullname.data, S_IFIFO | (vs->perm & 0777),
                0, vs->fidp->uid, -1);
        v9fs_post_create(s, vs, err);
    } else if (vs->perm & P9_STAT_MODE_SOCKET) {
        err = v9fs_do_mknod(s, vs->fullname.data, S_IFSOCK | (vs->perm & 0777),
                0, vs->fidp->uid, -1);
        v9fs_post_create(s, vs, err);
    } else {
        vs->fidp->fs.fd = v9fs_do_open2(s, vs->fullname.data, vs->fidp->uid,
                -1, omode_to_uflags(vs->mode)|O_CREAT, vs->perm);

        v9fs_create_post_open2(s, vs, err);
    }

    return;

out:
    v9fs_post_create(s, vs, err);
}

static void v9fs_create(V9fsState *s, V9fsPDU *pdu)
{
    int32_t fid;
    V9fsCreateState *vs;
    int err = 0;

    vs = qemu_malloc(sizeof(*vs));
    vs->pdu = pdu;
    vs->offset = 7;

    v9fs_string_init(&vs->fullname);

    pdu_unmarshal(vs->pdu, vs->offset, "dsdbs", &fid, &vs->name,
                                &vs->perm, &vs->mode, &vs->extension);

    vs->fidp = lookup_fid(s, fid);
    if (vs->fidp == NULL) {
        err = -EINVAL;
        goto out;
    }

    v9fs_string_sprintf(&vs->fullname, "%s/%s", vs->fidp->path.data,
                                                        vs->name.data);

    err = v9fs_do_lstat(s, &vs->fullname, &vs->stbuf);
    v9fs_create_post_lstat(s, vs, err);
    return;

out:
    complete_pdu(s, vs->pdu, err);
    v9fs_string_free(&vs->name);
    v9fs_string_free(&vs->extension);
    qemu_free(vs);
}

static void v9fs_post_symlink(V9fsState *s, V9fsSymlinkState *vs, int err)
{
    if (err == 0) {
        stat_to_qid(&vs->stbuf, &vs->qid);
        vs->offset += pdu_marshal(vs->pdu, vs->offset, "Q", &vs->qid);
        err = vs->offset;
    } else {
        err = -errno;
    }
    complete_pdu(s, vs->pdu, err);
    v9fs_string_free(&vs->name);
    v9fs_string_free(&vs->symname);
    v9fs_string_free(&vs->fullname);
    qemu_free(vs);
}

static void v9fs_symlink_post_do_symlink(V9fsState *s, V9fsSymlinkState *vs,
        int err)
{
    if (err) {
        goto out;
    }
    err = v9fs_do_lstat(s, &vs->fullname, &vs->stbuf);
out:
    v9fs_post_symlink(s, vs, err);
}

static void v9fs_symlink(V9fsState *s, V9fsPDU *pdu)
{
    int32_t dfid;
    V9fsSymlinkState *vs;
    int err = 0;
    gid_t gid;

    vs = qemu_malloc(sizeof(*vs));
    vs->pdu = pdu;
    vs->offset = 7;

    v9fs_string_init(&vs->fullname);

    pdu_unmarshal(vs->pdu, vs->offset, "dssd", &dfid, &vs->name,
            &vs->symname, &gid);

    vs->dfidp = lookup_fid(s, dfid);
    if (vs->dfidp == NULL) {
        err = -EINVAL;
        goto out;
    }

    v9fs_string_sprintf(&vs->fullname, "%s/%s", vs->dfidp->path.data,
            vs->name.data);
    err = v9fs_do_symlink(s, vs->dfidp, vs->symname.data,
            vs->fullname.data, gid);
    v9fs_symlink_post_do_symlink(s, vs, err);
    return;

out:
    complete_pdu(s, vs->pdu, err);
    v9fs_string_free(&vs->name);
    v9fs_string_free(&vs->symname);
    qemu_free(vs);
}

static void v9fs_flush(V9fsState *s, V9fsPDU *pdu)
{
    /* A nop call with no return */
    complete_pdu(s, pdu, 7);
}

static void v9fs_link(V9fsState *s, V9fsPDU *pdu)
{
    int32_t dfid, oldfid;
    V9fsFidState *dfidp, *oldfidp;
    V9fsString name, fullname;
    size_t offset = 7;
    int err = 0;

    v9fs_string_init(&fullname);

    pdu_unmarshal(pdu, offset, "dds", &dfid, &oldfid, &name);

    dfidp = lookup_fid(s, dfid);
    if (dfidp == NULL) {
        err = -errno;
        goto out;
    }

    oldfidp = lookup_fid(s, oldfid);
    if (oldfidp == NULL) {
        err = -errno;
        goto out;
    }

    v9fs_string_sprintf(&fullname, "%s/%s", dfidp->path.data, name.data);
    err = offset;
    err = v9fs_do_link(s, &oldfidp->path, &fullname);
    if (err) {
        err = -errno;
    }
    v9fs_string_free(&fullname);

out:
    v9fs_string_free(&name);
    complete_pdu(s, pdu, err);
}

static void v9fs_remove_post_remove(V9fsState *s, V9fsRemoveState *vs,
                                                                int err)
{
    if (err < 0) {
        err = -errno;
    } else {
        err = vs->offset;
    }

    /* For TREMOVE we need to clunk the fid even on failed remove */
    free_fid(s, vs->fidp->fid);

    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
}

static void v9fs_remove(V9fsState *s, V9fsPDU *pdu)
{
    int32_t fid;
    V9fsRemoveState *vs;
    int err = 0;

    vs = qemu_malloc(sizeof(*vs));
    vs->pdu = pdu;
    vs->offset = 7;

    pdu_unmarshal(vs->pdu, vs->offset, "d", &fid);

    vs->fidp = lookup_fid(s, fid);
    if (vs->fidp == NULL) {
        err = -EINVAL;
        goto out;
    }

    err = v9fs_do_remove(s, &vs->fidp->path);
    v9fs_remove_post_remove(s, vs, err);
    return;

out:
    complete_pdu(s, pdu, err);
    qemu_free(vs);
}

static void v9fs_wstat_post_truncate(V9fsState *s, V9fsWstatState *vs, int err)
{
    if (err < 0) {
        goto out;
    }

    err = vs->offset;

out:
    v9fs_stat_free(&vs->v9stat);
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
}

static void v9fs_wstat_post_rename(V9fsState *s, V9fsWstatState *vs, int err)
{
    if (err < 0) {
        goto out;
    }
    if (vs->v9stat.length != -1) {
        if (v9fs_do_truncate(s, &vs->fidp->path, vs->v9stat.length) < 0) {
            err = -errno;
        }
    }
    v9fs_wstat_post_truncate(s, vs, err);
    return;

out:
    v9fs_stat_free(&vs->v9stat);
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
}

static int v9fs_complete_rename(V9fsState *s, V9fsRenameState *vs)
{
    int err = 0;
    char *old_name, *new_name;
    char *end;

    if (vs->newdirfid != -1) {
        V9fsFidState *dirfidp;
        dirfidp = lookup_fid(s, vs->newdirfid);

        if (dirfidp == NULL) {
            err = -ENOENT;
            goto out;
        }

        BUG_ON(dirfidp->fid_type != P9_FID_NONE);

        new_name = qemu_mallocz(dirfidp->path.size + vs->name.size + 2);

        strcpy(new_name, dirfidp->path.data);
        strcat(new_name, "/");
        strcat(new_name + dirfidp->path.size, vs->name.data);
    } else {
        old_name = vs->fidp->path.data;
        end = strrchr(old_name, '/');
        if (end) {
            end++;
        } else {
            end = old_name;
        }
        new_name = qemu_mallocz(end - old_name + vs->name.size + 1);

        strncat(new_name, old_name, end - old_name);
        strncat(new_name + (end - old_name), vs->name.data, vs->name.size);
    }

    v9fs_string_free(&vs->name);
    vs->name.data = qemu_strdup(new_name);
    vs->name.size = strlen(new_name);

    if (strcmp(new_name, vs->fidp->path.data) != 0) {
        if (v9fs_do_rename(s, &vs->fidp->path, &vs->name)) {
            err = -errno;
        } else {
            V9fsFidState *fidp;
            /*
            * Fixup fid's pointing to the old name to
            * start pointing to the new name
            */
            for (fidp = s->fid_list; fidp; fidp = fidp->next) {
                if (vs->fidp == fidp) {
                    /*
                    * we replace name of this fid towards the end
                    * so that our below strcmp will work
                    */
                    continue;
                }
                if (!strncmp(vs->fidp->path.data, fidp->path.data,
                    strlen(vs->fidp->path.data))) {
                    /* replace the name */
                    v9fs_fix_path(&fidp->path, &vs->name,
                                  strlen(vs->fidp->path.data));
                }
            }
            v9fs_string_copy(&vs->fidp->path, &vs->name);
        }
    }
out:
    v9fs_string_free(&vs->name);
    return err;
}

static void v9fs_rename_post_rename(V9fsState *s, V9fsRenameState *vs, int err)
{
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
}

static void v9fs_wstat_post_chown(V9fsState *s, V9fsWstatState *vs, int err)
{
    if (err < 0) {
        goto out;
    }

    if (vs->v9stat.name.size != 0) {
        V9fsRenameState *vr;

        vr = qemu_mallocz(sizeof(V9fsRenameState));
        vr->newdirfid = -1;
        vr->pdu = vs->pdu;
        vr->fidp = vs->fidp;
        vr->offset = vs->offset;
        vr->name.size = vs->v9stat.name.size;
        vr->name.data = qemu_strdup(vs->v9stat.name.data);

        err = v9fs_complete_rename(s, vr);
        qemu_free(vr);
    }
    v9fs_wstat_post_rename(s, vs, err);
    return;

out:
    v9fs_stat_free(&vs->v9stat);
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
}

static void v9fs_rename(V9fsState *s, V9fsPDU *pdu)
{
    int32_t fid;
    V9fsRenameState *vs;
    ssize_t err = 0;

    vs = qemu_malloc(sizeof(*vs));
    vs->pdu = pdu;
    vs->offset = 7;

    pdu_unmarshal(vs->pdu, vs->offset, "dds", &fid, &vs->newdirfid, &vs->name);

    vs->fidp = lookup_fid(s, fid);
    if (vs->fidp == NULL) {
        err = -ENOENT;
        goto out;
    }

    BUG_ON(vs->fidp->fid_type != P9_FID_NONE);

    err = v9fs_complete_rename(s, vs);
    v9fs_rename_post_rename(s, vs, err);
    return;
out:
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
}

static void v9fs_wstat_post_utime(V9fsState *s, V9fsWstatState *vs, int err)
{
    if (err < 0) {
        goto out;
    }

    if (vs->v9stat.n_gid != -1 || vs->v9stat.n_uid != -1) {
        if (v9fs_do_chown(s, &vs->fidp->path, vs->v9stat.n_uid,
                    vs->v9stat.n_gid)) {
            err = -errno;
        }
    }
    v9fs_wstat_post_chown(s, vs, err);
    return;

out:
    v9fs_stat_free(&vs->v9stat);
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
}

static void v9fs_wstat_post_chmod(V9fsState *s, V9fsWstatState *vs, int err)
{
    if (err < 0) {
        goto out;
    }

    if (vs->v9stat.mtime != -1 || vs->v9stat.atime != -1) {
        struct timespec times[2];
        if (vs->v9stat.atime != -1) {
            times[0].tv_sec = vs->v9stat.atime;
            times[0].tv_nsec = 0;
        } else {
            times[0].tv_nsec = UTIME_OMIT;
        }
        if (vs->v9stat.mtime != -1) {
            times[1].tv_sec = vs->v9stat.mtime;
            times[1].tv_nsec = 0;
        } else {
            times[1].tv_nsec = UTIME_OMIT;
        }

        if (v9fs_do_utimensat(s, &vs->fidp->path, times)) {
            err = -errno;
        }
    }

    v9fs_wstat_post_utime(s, vs, err);
    return;

out:
    v9fs_stat_free(&vs->v9stat);
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
}

static void v9fs_wstat_post_fsync(V9fsState *s, V9fsWstatState *vs, int err)
{
    if (err == -1) {
        err = -errno;
    }
    v9fs_stat_free(&vs->v9stat);
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
}

static void v9fs_wstat_post_lstat(V9fsState *s, V9fsWstatState *vs, int err)
{
    uint32_t v9_mode;

    if (err == -1) {
        err = -errno;
        goto out;
    }

    v9_mode = stat_to_v9mode(&vs->stbuf);

    if ((vs->v9stat.mode & P9_STAT_MODE_TYPE_BITS) !=
        (v9_mode & P9_STAT_MODE_TYPE_BITS)) {
            /* Attempting to change the type */
            err = -EIO;
            goto out;
    }

    if (v9fs_do_chmod(s, &vs->fidp->path, v9mode_to_mode(vs->v9stat.mode,
                    &vs->v9stat.extension))) {
            err = -errno;
     }
    v9fs_wstat_post_chmod(s, vs, err);
    return;

out:
    v9fs_stat_free(&vs->v9stat);
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
}

static void v9fs_wstat(V9fsState *s, V9fsPDU *pdu)
{
    int32_t fid;
    V9fsWstatState *vs;
    int err = 0;

    vs = qemu_malloc(sizeof(*vs));
    vs->pdu = pdu;
    vs->offset = 7;

    pdu_unmarshal(pdu, vs->offset, "dwS", &fid, &vs->unused, &vs->v9stat);

    vs->fidp = lookup_fid(s, fid);
    if (vs->fidp == NULL) {
        err = -EINVAL;
        goto out;
    }

    /* do we need to sync the file? */
    if (donttouch_stat(&vs->v9stat)) {
        err = v9fs_do_fsync(s, vs->fidp->fs.fd, 0);
        v9fs_wstat_post_fsync(s, vs, err);
        return;
    }

    if (vs->v9stat.mode != -1) {
        err = v9fs_do_lstat(s, &vs->fidp->path, &vs->stbuf);
        v9fs_wstat_post_lstat(s, vs, err);
        return;
    }

    v9fs_wstat_post_chmod(s, vs, err);
    return;

out:
    v9fs_stat_free(&vs->v9stat);
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
}

static void v9fs_statfs_post_statfs(V9fsState *s, V9fsStatfsState *vs, int err)
{
    int32_t bsize_factor;

    if (err) {
        err = -errno;
        goto out;
    }

    /*
     * compute bsize factor based on host file system block size
     * and client msize
     */
    bsize_factor = (s->msize - P9_IOHDRSZ)/vs->stbuf.f_bsize;
    if (!bsize_factor) {
        bsize_factor = 1;
    }
    vs->v9statfs.f_type = vs->stbuf.f_type;
    vs->v9statfs.f_bsize = vs->stbuf.f_bsize;
    vs->v9statfs.f_bsize *= bsize_factor;
    /*
     * f_bsize is adjusted(multiplied) by bsize factor, so we need to
     * adjust(divide) the number of blocks, free blocks and available
     * blocks by bsize factor
     */
    vs->v9statfs.f_blocks = vs->stbuf.f_blocks/bsize_factor;
    vs->v9statfs.f_bfree = vs->stbuf.f_bfree/bsize_factor;
    vs->v9statfs.f_bavail = vs->stbuf.f_bavail/bsize_factor;
    vs->v9statfs.f_files = vs->stbuf.f_files;
    vs->v9statfs.f_ffree = vs->stbuf.f_ffree;
    vs->v9statfs.fsid_val = (unsigned int) vs->stbuf.f_fsid.__val[0] |
			(unsigned long long)vs->stbuf.f_fsid.__val[1] << 32;
    vs->v9statfs.f_namelen = vs->stbuf.f_namelen;

    vs->offset += pdu_marshal(vs->pdu, vs->offset, "ddqqqqqqd",
         vs->v9statfs.f_type, vs->v9statfs.f_bsize, vs->v9statfs.f_blocks,
         vs->v9statfs.f_bfree, vs->v9statfs.f_bavail, vs->v9statfs.f_files,
         vs->v9statfs.f_ffree, vs->v9statfs.fsid_val,
         vs->v9statfs.f_namelen);

out:
    complete_pdu(s, vs->pdu, vs->offset);
    qemu_free(vs);
}

static void v9fs_statfs(V9fsState *s, V9fsPDU *pdu)
{
    V9fsStatfsState *vs;
    ssize_t err = 0;

    vs = qemu_malloc(sizeof(*vs));
    vs->pdu = pdu;
    vs->offset = 7;

    memset(&vs->v9statfs, 0, sizeof(vs->v9statfs));

    pdu_unmarshal(vs->pdu, vs->offset, "d", &vs->fid);

    vs->fidp = lookup_fid(s, vs->fid);
    if (vs->fidp == NULL) {
        err = -ENOENT;
        goto out;
    }

    err = v9fs_do_statfs(s, &vs->fidp->path, &vs->stbuf);
    v9fs_statfs_post_statfs(s, vs, err);
    return;

out:
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
}

static void v9fs_mknod_post_lstat(V9fsState *s, V9fsMkState *vs, int err)
{
    if (err == -1) {
        err = -errno;
        goto out;
    }

    stat_to_qid(&vs->stbuf, &vs->qid);
    vs->offset += pdu_marshal(vs->pdu, vs->offset, "Q", &vs->qid);
    err = vs->offset;
out:
    complete_pdu(s, vs->pdu, err);
    v9fs_string_free(&vs->fullname);
    v9fs_string_free(&vs->name);
    qemu_free(vs);
}

static void v9fs_mknod_post_mknod(V9fsState *s, V9fsMkState *vs, int err)
{
    if (err == -1) {
        err = -errno;
        goto out;
    }

    err = v9fs_do_lstat(s, &vs->fullname, &vs->stbuf);
    v9fs_mknod_post_lstat(s, vs, err);
    return;
out:
    complete_pdu(s, vs->pdu, err);
    v9fs_string_free(&vs->fullname);
    v9fs_string_free(&vs->name);
    qemu_free(vs);
}

static void v9fs_mknod(V9fsState *s, V9fsPDU *pdu)
{
    int32_t fid;
    V9fsMkState *vs;
    int err = 0;
    V9fsFidState *fidp;
    gid_t gid;
    int mode;
    int major, minor;

    vs = qemu_malloc(sizeof(*vs));
    vs->pdu = pdu;
    vs->offset = 7;

    v9fs_string_init(&vs->fullname);
    pdu_unmarshal(vs->pdu, vs->offset, "dsdddd", &fid, &vs->name, &mode,
        &major, &minor, &gid);

    fidp = lookup_fid(s, fid);
    if (fidp == NULL) {
        err = -ENOENT;
        goto out;
    }

    v9fs_string_sprintf(&vs->fullname, "%s/%s", fidp->path.data, vs->name.data);
    err = v9fs_do_mknod(s, vs->fullname.data, mode, makedev(major, minor),
        fidp->uid, gid);
    v9fs_mknod_post_mknod(s, vs, err);
    return;

out:
    complete_pdu(s, vs->pdu, err);
    v9fs_string_free(&vs->fullname);
    v9fs_string_free(&vs->name);
    qemu_free(vs);
}

/*
 * Implement posix byte range locking code
 * Server side handling of locking code is very simple, because 9p server in
 * QEMU can handle only one client. And most of the lock handling
 * (like conflict, merging) etc is done by the VFS layer itself, so no need to
 * do any thing in * qemu 9p server side lock code path.
 * So when a TLOCK request comes, always return success
 */

static void v9fs_lock(V9fsState *s, V9fsPDU *pdu)
{
    int32_t fid, err = 0;
    V9fsLockState *vs;

    vs = qemu_mallocz(sizeof(*vs));
    vs->pdu = pdu;
    vs->offset = 7;

    vs->flock = qemu_malloc(sizeof(*vs->flock));
    pdu_unmarshal(vs->pdu, vs->offset, "dbdqqds", &fid, &vs->flock->type,
                &vs->flock->flags, &vs->flock->start, &vs->flock->length,
                            &vs->flock->proc_id, &vs->flock->client_id);

    vs->status = P9_LOCK_ERROR;

    /* We support only block flag now (that too ignored currently) */
    if (vs->flock->flags & ~P9_LOCK_FLAGS_BLOCK) {
        err = -EINVAL;
        goto out;
    }
    vs->fidp = lookup_fid(s, fid);
    if (vs->fidp == NULL) {
        err = -ENOENT;
        goto out;
    }

    err = v9fs_do_fstat(s, vs->fidp->fs.fd, &vs->stbuf);
    if (err < 0) {
        err = -errno;
        goto out;
    }
    vs->status = P9_LOCK_SUCCESS;
out:
    vs->offset += pdu_marshal(vs->pdu, vs->offset, "b", vs->status);
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs->flock);
    qemu_free(vs);
}

/*
 * When a TGETLOCK request comes, always return success because all lock
 * handling is done by client's VFS layer.
 */

static void v9fs_getlock(V9fsState *s, V9fsPDU *pdu)
{
    int32_t fid, err = 0;
    V9fsGetlockState *vs;

    vs = qemu_mallocz(sizeof(*vs));
    vs->pdu = pdu;
    vs->offset = 7;

    vs->glock = qemu_malloc(sizeof(*vs->glock));
    pdu_unmarshal(vs->pdu, vs->offset, "dbqqds", &fid, &vs->glock->type,
                &vs->glock->start, &vs->glock->length, &vs->glock->proc_id,
		&vs->glock->client_id);

    vs->fidp = lookup_fid(s, fid);
    if (vs->fidp == NULL) {
        err = -ENOENT;
        goto out;
    }

    err = v9fs_do_fstat(s, vs->fidp->fs.fd, &vs->stbuf);
    if (err < 0) {
        err = -errno;
        goto out;
    }
    vs->glock->type = F_UNLCK;
    vs->offset += pdu_marshal(vs->pdu, vs->offset, "bqqds", vs->glock->type,
                vs->glock->start, vs->glock->length, vs->glock->proc_id,
		&vs->glock->client_id);
out:
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs->glock);
    qemu_free(vs);
}

static void v9fs_mkdir_post_lstat(V9fsState *s, V9fsMkState *vs, int err)
{
    if (err == -1) {
        err = -errno;
        goto out;
    }

    stat_to_qid(&vs->stbuf, &vs->qid);
    vs->offset += pdu_marshal(vs->pdu, vs->offset, "Q", &vs->qid);
    err = vs->offset;
out:
    complete_pdu(s, vs->pdu, err);
    v9fs_string_free(&vs->fullname);
    v9fs_string_free(&vs->name);
    qemu_free(vs);
}

static void v9fs_mkdir_post_mkdir(V9fsState *s, V9fsMkState *vs, int err)
{
    if (err == -1) {
        err = -errno;
        goto out;
    }

    err = v9fs_do_lstat(s, &vs->fullname, &vs->stbuf);
    v9fs_mkdir_post_lstat(s, vs, err);
    return;
out:
    complete_pdu(s, vs->pdu, err);
    v9fs_string_free(&vs->fullname);
    v9fs_string_free(&vs->name);
    qemu_free(vs);
}

static void v9fs_mkdir(V9fsState *s, V9fsPDU *pdu)
{
    int32_t fid;
    V9fsMkState *vs;
    int err = 0;
    V9fsFidState *fidp;
    gid_t gid;
    int mode;

    vs = qemu_malloc(sizeof(*vs));
    vs->pdu = pdu;
    vs->offset = 7;

    v9fs_string_init(&vs->fullname);
    pdu_unmarshal(vs->pdu, vs->offset, "dsdd", &fid, &vs->name, &mode,
        &gid);

    fidp = lookup_fid(s, fid);
    if (fidp == NULL) {
        err = -ENOENT;
        goto out;
    }

    v9fs_string_sprintf(&vs->fullname, "%s/%s", fidp->path.data, vs->name.data);
    err = v9fs_do_mkdir(s, vs->fullname.data, mode, fidp->uid, gid);
    v9fs_mkdir_post_mkdir(s, vs, err);
    return;

out:
    complete_pdu(s, vs->pdu, err);
    v9fs_string_free(&vs->fullname);
    v9fs_string_free(&vs->name);
    qemu_free(vs);
}

static void v9fs_post_xattr_getvalue(V9fsState *s, V9fsXattrState *vs, int err)
{

    if (err < 0) {
        err = -errno;
        free_fid(s, vs->xattr_fidp->fid);
        goto out;
    }
    vs->offset += pdu_marshal(vs->pdu, vs->offset, "q", vs->size);
    err = vs->offset;
out:
    complete_pdu(s, vs->pdu, err);
    v9fs_string_free(&vs->name);
    qemu_free(vs);
    return;
}

static void v9fs_post_xattr_check(V9fsState *s, V9fsXattrState *vs, ssize_t err)
{
    if (err < 0) {
        err = -errno;
        free_fid(s, vs->xattr_fidp->fid);
        goto out;
    }
    /*
     * Read the xattr value
     */
    vs->xattr_fidp->fs.xattr.len = vs->size;
    vs->xattr_fidp->fid_type = P9_FID_XATTR;
    vs->xattr_fidp->fs.xattr.copied_len = -1;
    if (vs->size) {
        vs->xattr_fidp->fs.xattr.value = qemu_malloc(vs->size);
        err = v9fs_do_lgetxattr(s, &vs->xattr_fidp->path,
                                &vs->name, vs->xattr_fidp->fs.xattr.value,
                                vs->xattr_fidp->fs.xattr.len);
    }
    v9fs_post_xattr_getvalue(s, vs, err);
    return;
out:
    complete_pdu(s, vs->pdu, err);
    v9fs_string_free(&vs->name);
    qemu_free(vs);
}

static void v9fs_post_lxattr_getvalue(V9fsState *s,
                                      V9fsXattrState *vs, int err)
{
    if (err < 0) {
        err = -errno;
        free_fid(s, vs->xattr_fidp->fid);
        goto out;
    }
    vs->offset += pdu_marshal(vs->pdu, vs->offset, "q", vs->size);
    err = vs->offset;
out:
    complete_pdu(s, vs->pdu, err);
    v9fs_string_free(&vs->name);
    qemu_free(vs);
    return;
}

static void v9fs_post_lxattr_check(V9fsState *s,
                                   V9fsXattrState *vs, ssize_t err)
{
    if (err < 0) {
        err = -errno;
        free_fid(s, vs->xattr_fidp->fid);
        goto out;
    }
    /*
     * Read the xattr value
     */
    vs->xattr_fidp->fs.xattr.len = vs->size;
    vs->xattr_fidp->fid_type = P9_FID_XATTR;
    vs->xattr_fidp->fs.xattr.copied_len = -1;
    if (vs->size) {
        vs->xattr_fidp->fs.xattr.value = qemu_malloc(vs->size);
        err = v9fs_do_llistxattr(s, &vs->xattr_fidp->path,
                                 vs->xattr_fidp->fs.xattr.value,
                                 vs->xattr_fidp->fs.xattr.len);
    }
    v9fs_post_lxattr_getvalue(s, vs, err);
    return;
out:
    complete_pdu(s, vs->pdu, err);
    v9fs_string_free(&vs->name);
    qemu_free(vs);
}

static void v9fs_xattrwalk(V9fsState *s, V9fsPDU *pdu)
{
    ssize_t err = 0;
    V9fsXattrState *vs;
    int32_t fid, newfid;

    vs = qemu_malloc(sizeof(*vs));
    vs->pdu = pdu;
    vs->offset = 7;

    pdu_unmarshal(vs->pdu, vs->offset, "dds", &fid, &newfid, &vs->name);
    vs->file_fidp = lookup_fid(s, fid);
    if (vs->file_fidp == NULL) {
        err = -ENOENT;
        goto out;
    }

    vs->xattr_fidp = alloc_fid(s, newfid);
    if (vs->xattr_fidp == NULL) {
        err = -EINVAL;
        goto out;
    }

    v9fs_string_copy(&vs->xattr_fidp->path, &vs->file_fidp->path);
    if (vs->name.data[0] == 0) {
        /*
         * listxattr request. Get the size first
         */
        vs->size = v9fs_do_llistxattr(s, &vs->xattr_fidp->path,
                                      NULL, 0);
        if (vs->size < 0) {
            err = vs->size;
        }
        v9fs_post_lxattr_check(s, vs, err);
        return;
    } else {
        /*
         * specific xattr fid. We check for xattr
         * presence also collect the xattr size
         */
        vs->size = v9fs_do_lgetxattr(s, &vs->xattr_fidp->path,
                                     &vs->name, NULL, 0);
        if (vs->size < 0) {
            err = vs->size;
        }
        v9fs_post_xattr_check(s, vs, err);
        return;
    }
out:
    complete_pdu(s, vs->pdu, err);
    v9fs_string_free(&vs->name);
    qemu_free(vs);
}

static void v9fs_xattrcreate(V9fsState *s, V9fsPDU *pdu)
{
    int flags;
    int32_t fid;
    ssize_t err = 0;
    V9fsXattrState *vs;

    vs = qemu_malloc(sizeof(*vs));
    vs->pdu = pdu;
    vs->offset = 7;

    pdu_unmarshal(vs->pdu, vs->offset, "dsqd",
                  &fid, &vs->name, &vs->size, &flags);

    vs->file_fidp = lookup_fid(s, fid);
    if (vs->file_fidp == NULL) {
        err = -EINVAL;
        goto out;
    }

    /* Make the file fid point to xattr */
    vs->xattr_fidp = vs->file_fidp;
    vs->xattr_fidp->fid_type = P9_FID_XATTR;
    vs->xattr_fidp->fs.xattr.copied_len = 0;
    vs->xattr_fidp->fs.xattr.len = vs->size;
    vs->xattr_fidp->fs.xattr.flags = flags;
    v9fs_string_init(&vs->xattr_fidp->fs.xattr.name);
    v9fs_string_copy(&vs->xattr_fidp->fs.xattr.name, &vs->name);
    if (vs->size)
        vs->xattr_fidp->fs.xattr.value = qemu_malloc(vs->size);
    else
        vs->xattr_fidp->fs.xattr.value = NULL;

out:
    complete_pdu(s, vs->pdu, err);
    v9fs_string_free(&vs->name);
    qemu_free(vs);
}

static void v9fs_readlink_post_readlink(V9fsState *s, V9fsReadLinkState *vs,
                                                    int err)
{
    if (err < 0) {
        err = -errno;
        goto out;
    }
    vs->offset += pdu_marshal(vs->pdu, vs->offset, "s", &vs->target);
    err = vs->offset;
out:
    complete_pdu(s, vs->pdu, err);
    v9fs_string_free(&vs->target);
    qemu_free(vs);
}

static void v9fs_readlink(V9fsState *s, V9fsPDU *pdu)
{
    int32_t fid;
    V9fsReadLinkState *vs;
    int err = 0;
    V9fsFidState *fidp;

    vs = qemu_malloc(sizeof(*vs));
    vs->pdu = pdu;
    vs->offset = 7;

    pdu_unmarshal(vs->pdu, vs->offset, "d", &fid);

    fidp = lookup_fid(s, fid);
    if (fidp == NULL) {
        err = -ENOENT;
        goto out;
    }

    v9fs_string_init(&vs->target);
    err = v9fs_do_readlink(s, &fidp->path, &vs->target);
    v9fs_readlink_post_readlink(s, vs, err);
    return;
out:
    complete_pdu(s, vs->pdu, err);
    qemu_free(vs);
}

typedef void (pdu_handler_t)(V9fsState *s, V9fsPDU *pdu);

static pdu_handler_t *pdu_handlers[] = {
    [P9_TREADDIR] = v9fs_readdir,
    [P9_TSTATFS] = v9fs_statfs,
    [P9_TGETATTR] = v9fs_getattr,
    [P9_TSETATTR] = v9fs_setattr,
    [P9_TXATTRWALK] = v9fs_xattrwalk,
    [P9_TXATTRCREATE] = v9fs_xattrcreate,
    [P9_TMKNOD] = v9fs_mknod,
    [P9_TRENAME] = v9fs_rename,
    [P9_TLOCK] = v9fs_lock,
    [P9_TGETLOCK] = v9fs_getlock,
    [P9_TREADLINK] = v9fs_readlink,
    [P9_TMKDIR] = v9fs_mkdir,
    [P9_TVERSION] = v9fs_version,
    [P9_TLOPEN] = v9fs_open,
    [P9_TATTACH] = v9fs_attach,
    [P9_TSTAT] = v9fs_stat,
    [P9_TWALK] = v9fs_walk,
    [P9_TCLUNK] = v9fs_clunk,
    [P9_TFSYNC] = v9fs_fsync,
    [P9_TOPEN] = v9fs_open,
    [P9_TREAD] = v9fs_read,
#if 0
    [P9_TAUTH] = v9fs_auth,
#endif
    [P9_TFLUSH] = v9fs_flush,
    [P9_TLINK] = v9fs_link,
    [P9_TSYMLINK] = v9fs_symlink,
    [P9_TCREATE] = v9fs_create,
    [P9_TLCREATE] = v9fs_lcreate,
    [P9_TWRITE] = v9fs_write,
    [P9_TWSTAT] = v9fs_wstat,
    [P9_TREMOVE] = v9fs_remove,
};

static void submit_pdu(V9fsState *s, V9fsPDU *pdu)
{
    pdu_handler_t *handler;

    if (debug_9p_pdu) {
        pprint_pdu(pdu);
    }

    BUG_ON(pdu->id >= ARRAY_SIZE(pdu_handlers));

    handler = pdu_handlers[pdu->id];
    BUG_ON(handler == NULL);

    handler(s, pdu);
}

static void handle_9p_output(VirtIODevice *vdev, VirtQueue *vq)
{
    V9fsState *s = (V9fsState *)vdev;
    V9fsPDU *pdu;
    ssize_t len;

    while ((pdu = alloc_pdu(s)) &&
            (len = virtqueue_pop(vq, &pdu->elem)) != 0) {
        uint8_t *ptr;

        BUG_ON(pdu->elem.out_num == 0 || pdu->elem.in_num == 0);
        BUG_ON(pdu->elem.out_sg[0].iov_len < 7);

        ptr = pdu->elem.out_sg[0].iov_base;

        memcpy(&pdu->size, ptr, 4);
        pdu->id = ptr[4];
        memcpy(&pdu->tag, ptr + 5, 2);

        submit_pdu(s, pdu);
    }

    free_pdu(s, pdu);
}

static uint32_t virtio_9p_get_features(VirtIODevice *vdev, uint32_t features)
{
    features |= 1 << VIRTIO_9P_MOUNT_TAG;
    return features;
}

static V9fsState *to_virtio_9p(VirtIODevice *vdev)
{
    return (V9fsState *)vdev;
}

static void virtio_9p_get_config(VirtIODevice *vdev, uint8_t *config)
{
    struct virtio_9p_config *cfg;
    V9fsState *s = to_virtio_9p(vdev);

    cfg = qemu_mallocz(sizeof(struct virtio_9p_config) +
                        s->tag_len);
    stw_raw(&cfg->tag_len, s->tag_len);
    memcpy(cfg->tag, s->tag, s->tag_len);
    memcpy(config, cfg, s->config_size);
    qemu_free(cfg);
}

VirtIODevice *virtio_9p_init(DeviceState *dev, V9fsConf *conf)
 {
    V9fsState *s;
    int i, len;
    struct stat stat;
    FsTypeEntry *fse;


    s = (V9fsState *)virtio_common_init("virtio-9p",
                                    VIRTIO_ID_9P,
                                    sizeof(struct virtio_9p_config)+
                                    MAX_TAG_LEN,
                                    sizeof(V9fsState));

    /* initialize pdu allocator */
    QLIST_INIT(&s->free_list);
    for (i = 0; i < (MAX_REQ - 1); i++) {
	QLIST_INSERT_HEAD(&s->free_list, &s->pdus[i], next);
    }

    s->vq = virtio_add_queue(&s->vdev, MAX_REQ, handle_9p_output);

    fse = get_fsdev_fsentry(conf->fsdev_id);

    if (!fse) {
        /* We don't have a fsdev identified by fsdev_id */
        fprintf(stderr, "Virtio-9p device couldn't find fsdev with the "
                "id = %s\n", conf->fsdev_id ? conf->fsdev_id : "NULL");
        exit(1);
    }

    if (!fse->path || !conf->tag) {
        /* we haven't specified a mount_tag or the path */
        fprintf(stderr, "fsdev with id %s needs path "
                "and Virtio-9p device needs mount_tag arguments\n",
                conf->fsdev_id);
        exit(1);
    }

    if (!strcmp(fse->security_model, "passthrough")) {
        /* Files on the Fileserver set to client user credentials */
        s->ctx.fs_sm = SM_PASSTHROUGH;
        s->ctx.xops = passthrough_xattr_ops;
    } else if (!strcmp(fse->security_model, "mapped")) {
        /* Files on the fileserver are set to QEMU credentials.
         * Client user credentials are saved in extended attributes.
         */
        s->ctx.fs_sm = SM_MAPPED;
        s->ctx.xops = mapped_xattr_ops;
    } else if (!strcmp(fse->security_model, "none")) {
        /*
         * Files on the fileserver are set to QEMU credentials.
         */
        s->ctx.fs_sm = SM_NONE;
        s->ctx.xops = none_xattr_ops;
    } else {
        fprintf(stderr, "Default to security_model=none. You may want"
                " enable advanced security model using "
                "security option:\n\t security_model=passthrough \n\t "
                "security_model=mapped\n");
        s->ctx.fs_sm = SM_NONE;
        s->ctx.xops = none_xattr_ops;
    }

    if (lstat(fse->path, &stat)) {
        fprintf(stderr, "share path %s does not exist\n", fse->path);
        exit(1);
    } else if (!S_ISDIR(stat.st_mode)) {
        fprintf(stderr, "share path %s is not a directory \n", fse->path);
        exit(1);
    }

    s->ctx.fs_root = qemu_strdup(fse->path);
    len = strlen(conf->tag);
    if (len > MAX_TAG_LEN) {
        len = MAX_TAG_LEN;
    }
    /* s->tag is non-NULL terminated string */
    s->tag = qemu_malloc(len);
    memcpy(s->tag, conf->tag, len);
    s->tag_len = len;
    s->ctx.uid = -1;

    s->ops = fse->ops;
    s->vdev.get_features = virtio_9p_get_features;
    s->config_size = sizeof(struct virtio_9p_config) +
                        s->tag_len;
    s->vdev.get_config = virtio_9p_get_config;

    return &s->vdev;
}
