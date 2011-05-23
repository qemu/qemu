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

#include "hw/virtio.h"
#include "hw/pc.h"
#include "qemu_socket.h"
#include "hw/virtio-pci.h"
#include "virtio-9p.h"
#include "fsdev/qemu-fsdev.h"
#include "virtio-9p-debug.h"
#include "virtio-9p-xattr.h"
#include "virtio-9p-coth.h"

int debug_9p_pdu;
int open_fd_hw;
int total_open_fd;
static int open_fd_rc;

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

static void v9fs_string_init(V9fsString *str)
{
    str->data = NULL;
    str->size = 0;
}

static void v9fs_string_free(V9fsString *str)
{
    g_free(str->data);
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
    *strp = g_malloc((len + 1) * sizeof(**strp));

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

/*
 * Return TRUE if s1 is an ancestor of s2.
 *
 * E.g. "a/b" is an ancestor of "a/b/c" but not of "a/bc/d".
 * As a special case, We treat s1 as ancestor of s2 if they are same!
 */
static int v9fs_path_is_ancestor(V9fsString *s1, V9fsString *s2)
{
    if (!strncmp(s1->data, s2->data, s1->size)) {
        if (s2->data[s1->size] == '\0' || s2->data[s1->size] == '/') {
            return 1;
        }
    }
    return 0;
}

static size_t v9fs_string_size(V9fsString *str)
{
    return str->size;
}

/*
 * returns 0 if fid got re-opened, 1 if not, < 0 on error */
static int v9fs_reopen_fid(V9fsState *s, V9fsFidState *f)
{
    int err = 1;
    if (f->fid_type == P9_FID_FILE) {
        if (f->fs.fd == -1) {
            do {
                err = v9fs_co_open(s, f, f->open_flags);
            } while (err == -EINTR);
        }
    } else if (f->fid_type == P9_FID_DIR) {
        if (f->fs.dir == NULL) {
            do {
                err = v9fs_co_opendir(s, f);
            } while (err == -EINTR);
        }
    }
    return err;
}

static V9fsFidState *get_fid(V9fsState *s, int32_t fid)
{
    int err;
    V9fsFidState *f;

    for (f = s->fid_list; f; f = f->next) {
        BUG_ON(f->clunked);
        if (f->fid == fid) {
            /*
             * Update the fid ref upfront so that
             * we don't get reclaimed when we yield
             * in open later.
             */
            f->ref++;
            /*
             * check whether we need to reopen the
             * file. We might have closed the fd
             * while trying to free up some file
             * descriptors.
             */
            err = v9fs_reopen_fid(s, f);
            if (err < 0) {
                f->ref--;
                return NULL;
            }
            /*
             * Mark the fid as referenced so that the LRU
             * reclaim won't close the file descriptor
             */
            f->flags |= FID_REFERENCED;
            return f;
        }
    }
    return NULL;
}

static V9fsFidState *alloc_fid(V9fsState *s, int32_t fid)
{
    V9fsFidState *f;

    for (f = s->fid_list; f; f = f->next) {
        /* If fid is already there return NULL */
        BUG_ON(f->clunked);
        if (f->fid == fid) {
            return NULL;
        }
    }
    f = g_malloc0(sizeof(V9fsFidState));
    f->fid = fid;
    f->fid_type = P9_FID_NONE;
    f->ref = 1;
    /*
     * Mark the fid as referenced so that the LRU
     * reclaim won't close the file descriptor
     */
    f->flags |= FID_REFERENCED;
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
        retval = v9fs_co_lsetxattr(s, &fidp->path, &fidp->fs.xattr.name,
                                   fidp->fs.xattr.value,
                                   fidp->fs.xattr.len,
                                   fidp->fs.xattr.flags);
    } else {
        retval = v9fs_co_lremovexattr(s, &fidp->path, &fidp->fs.xattr.name);
    }
free_out:
    v9fs_string_free(&fidp->fs.xattr.name);
free_value:
    if (fidp->fs.xattr.value) {
        g_free(fidp->fs.xattr.value);
    }
    return retval;
}

static int free_fid(V9fsState *s, V9fsFidState *fidp)
{
    int retval = 0;

    if (fidp->fid_type == P9_FID_FILE) {
        /* If we reclaimed the fd no need to close */
        if (fidp->fs.fd != -1) {
            retval = v9fs_co_close(s, fidp->fs.fd);
        }
    } else if (fidp->fid_type == P9_FID_DIR) {
        if (fidp->fs.dir != NULL) {
            retval = v9fs_co_closedir(s, fidp->fs.dir);
        }
    } else if (fidp->fid_type == P9_FID_XATTR) {
        retval = v9fs_xattr_fid_clunk(s, fidp);
    }
    v9fs_string_free(&fidp->path);
    g_free(fidp);
    return retval;
}

static void put_fid(V9fsState *s, V9fsFidState *fidp)
{
    BUG_ON(!fidp->ref);
    fidp->ref--;
    /*
     * Don't free the fid if it is in reclaim list
     */
    if (!fidp->ref && fidp->clunked) {
        free_fid(s, fidp);
    }
}

static int clunk_fid(V9fsState *s, int32_t fid)
{
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
    fidp->clunked = 1;
    return 0;
}

void v9fs_reclaim_fd(V9fsState *s)
{
    int reclaim_count = 0;
    V9fsFidState *f, *reclaim_list = NULL;

    for (f = s->fid_list; f; f = f->next) {
        /*
         * Unlink fids cannot be reclaimed. Check
         * for them and skip them. Also skip fids
         * currently being operated on.
         */
        if (f->ref || f->flags & FID_NON_RECLAIMABLE) {
            continue;
        }
        /*
         * if it is a recently referenced fid
         * we leave the fid untouched and clear the
         * reference bit. We come back to it later
         * in the next iteration. (a simple LRU without
         * moving list elements around)
         */
        if (f->flags & FID_REFERENCED) {
            f->flags &= ~FID_REFERENCED;
            continue;
        }
        /*
         * Add fids to reclaim list.
         */
        if (f->fid_type == P9_FID_FILE) {
            if (f->fs.fd != -1) {
                /*
                 * Up the reference count so that
                 * a clunk request won't free this fid
                 */
                f->ref++;
                f->rclm_lst = reclaim_list;
                reclaim_list = f;
                f->fs_reclaim.fd = f->fs.fd;
                f->fs.fd = -1;
                reclaim_count++;
            }
        } else if (f->fid_type == P9_FID_DIR) {
            if (f->fs.dir != NULL) {
                /*
                 * Up the reference count so that
                 * a clunk request won't free this fid
                 */
                f->ref++;
                f->rclm_lst = reclaim_list;
                reclaim_list = f;
                f->fs_reclaim.dir = f->fs.dir;
                f->fs.dir = NULL;
                reclaim_count++;
            }
        }
        if (reclaim_count >= open_fd_rc) {
            break;
        }
    }
    /*
     * Now close the fid in reclaim list. Free them if they
     * are already clunked.
     */
    while (reclaim_list) {
        f = reclaim_list;
        reclaim_list = f->rclm_lst;
        if (f->fid_type == P9_FID_FILE) {
            v9fs_co_close(s, f->fs_reclaim.fd);
        } else if (f->fid_type == P9_FID_DIR) {
            v9fs_co_closedir(s, f->fs_reclaim.dir);
        }
        f->rclm_lst = NULL;
        /*
         * Now drop the fid reference, free it
         * if clunked.
         */
        put_fid(s, f);
    }
}

static int v9fs_mark_fids_unreclaim(V9fsState *s, V9fsString *str)
{
    int err;
    V9fsFidState *fidp, head_fid;

    head_fid.next = s->fid_list;
    for (fidp = s->fid_list; fidp; fidp = fidp->next) {
        if (!strcmp(fidp->path.data, str->data)) {
            /* Mark the fid non reclaimable. */
            fidp->flags |= FID_NON_RECLAIMABLE;

            /* reopen the file/dir if already closed */
            err = v9fs_reopen_fid(s, fidp);
            if (err < 0) {
                return -1;
            }
            /*
             * Go back to head of fid list because
             * the list could have got updated when
             * switched to the worker thread
             */
            if (err == 0) {
                fidp = &head_fid;
            }
        }
    }
    return 0;
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

    memset(&qidp->path, 0, sizeof(qidp->path));
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

    err = v9fs_co_lstat(s, &fidp->path, &stbuf);
    if (err < 0) {
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
        if (debug_9p_pdu) {
            pprint_pdu(pdu);
        }
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
            offset += pdu_unpack(&val, pdu, offset, sizeof(val));
            *valp = le16_to_cpu(val);
            break;
        }
        case 'd': {
            uint32_t val, *valp;
            valp = va_arg(ap, uint32_t *);
            offset += pdu_unpack(&val, pdu, offset, sizeof(val));
            *valp = le32_to_cpu(val);
            break;
        }
        case 'q': {
            uint64_t val, *valp;
            valp = va_arg(ap, uint64_t *);
            offset += pdu_unpack(&val, pdu, offset, sizeof(val));
            *valp = le64_to_cpu(val);
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
            str->data = g_malloc(str->size + 1);
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
        err = v9fs_co_readlink(s, name, &v9stat->extension);
        if (err < 0) {
            return err;
        }
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

static void v9fs_version(void *opaque)
{
    V9fsPDU *pdu = opaque;
    V9fsState *s = pdu->s;
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
    return;
}

static void v9fs_attach(void *opaque)
{
    V9fsPDU *pdu = opaque;
    V9fsState *s = pdu->s;
    int32_t fid, afid, n_uname;
    V9fsString uname, aname;
    V9fsFidState *fidp;
    size_t offset = 7;
    V9fsQID qid;
    ssize_t err;

    pdu_unmarshal(pdu, offset, "ddssd", &fid, &afid, &uname, &aname, &n_uname);

    fidp = alloc_fid(s, fid);
    if (fidp == NULL) {
        err = -EINVAL;
        goto out_nofid;
    }
    fidp->uid = n_uname;
    v9fs_string_sprintf(&fidp->path, "%s", "/");
    err = fid_to_qid(s, fidp, &qid);
    if (err < 0) {
        err = -EINVAL;
        clunk_fid(s, fid);
        goto out;
    }
    offset += pdu_marshal(pdu, offset, "Q", &qid);
    err = offset;
out:
    put_fid(s, fidp);
out_nofid:
    complete_pdu(s, pdu, err);
    v9fs_string_free(&uname);
    v9fs_string_free(&aname);
}

static void v9fs_stat(void *opaque)
{
    int32_t fid;
    V9fsStat v9stat;
    ssize_t err = 0;
    size_t offset = 7;
    struct stat stbuf;
    V9fsFidState *fidp;
    V9fsPDU *pdu = opaque;
    V9fsState *s = pdu->s;

    pdu_unmarshal(pdu, offset, "d", &fid);

    fidp = get_fid(s, fid);
    if (fidp == NULL) {
        err = -ENOENT;
        goto out_nofid;
    }
    err = v9fs_co_lstat(s, &fidp->path, &stbuf);
    if (err < 0) {
        goto out;
    }
    err = stat_to_v9stat(s, &fidp->path, &stbuf, &v9stat);
    if (err < 0) {
        goto out;
    }
    offset += pdu_marshal(pdu, offset, "wS", 0, &v9stat);
    err = offset;
    v9fs_stat_free(&v9stat);
out:
    put_fid(s, fidp);
out_nofid:
    complete_pdu(s, pdu, err);
}

static void v9fs_getattr(void *opaque)
{
    int32_t fid;
    size_t offset = 7;
    ssize_t retval = 0;
    struct stat stbuf;
    V9fsFidState *fidp;
    uint64_t request_mask;
    V9fsStatDotl v9stat_dotl;
    V9fsPDU *pdu = opaque;
    V9fsState *s = pdu->s;

    pdu_unmarshal(pdu, offset, "dq", &fid, &request_mask);

    fidp = get_fid(s, fid);
    if (fidp == NULL) {
        retval = -ENOENT;
        goto out_nofid;
    }
    /*
     * Currently we only support BASIC fields in stat, so there is no
     * need to look at request_mask.
     */
    retval = v9fs_co_lstat(s, &fidp->path, &stbuf);
    if (retval < 0) {
        goto out;
    }
    stat_to_v9stat_dotl(s, &stbuf, &v9stat_dotl);
    retval = offset;
    retval += pdu_marshal(pdu, offset, "A", &v9stat_dotl);
out:
    put_fid(s, fidp);
out_nofid:
    complete_pdu(s, pdu, retval);
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

static void v9fs_setattr(void *opaque)
{
    int err = 0;
    int32_t fid;
    V9fsFidState *fidp;
    size_t offset = 7;
    V9fsIattr v9iattr;
    V9fsPDU *pdu = opaque;
    V9fsState *s = pdu->s;

    pdu_unmarshal(pdu, offset, "dI", &fid, &v9iattr);

    fidp = get_fid(s, fid);
    if (fidp == NULL) {
        err = -EINVAL;
        goto out_nofid;
    }
    if (v9iattr.valid & ATTR_MODE) {
        err = v9fs_co_chmod(s, &fidp->path, v9iattr.mode);
        if (err < 0) {
            goto out;
        }
    }
    if (v9iattr.valid & (ATTR_ATIME | ATTR_MTIME)) {
        struct timespec times[2];
        if (v9iattr.valid & ATTR_ATIME) {
            if (v9iattr.valid & ATTR_ATIME_SET) {
                times[0].tv_sec = v9iattr.atime_sec;
                times[0].tv_nsec = v9iattr.atime_nsec;
            } else {
                times[0].tv_nsec = UTIME_NOW;
            }
        } else {
            times[0].tv_nsec = UTIME_OMIT;
        }
        if (v9iattr.valid & ATTR_MTIME) {
            if (v9iattr.valid & ATTR_MTIME_SET) {
                times[1].tv_sec = v9iattr.mtime_sec;
                times[1].tv_nsec = v9iattr.mtime_nsec;
            } else {
                times[1].tv_nsec = UTIME_NOW;
            }
        } else {
            times[1].tv_nsec = UTIME_OMIT;
        }
        err = v9fs_co_utimensat(s, &fidp->path, times);
        if (err < 0) {
            goto out;
        }
    }
    /*
     * If the only valid entry in iattr is ctime we can call
     * chown(-1,-1) to update the ctime of the file
     */
    if ((v9iattr.valid & (ATTR_UID | ATTR_GID)) ||
        ((v9iattr.valid & ATTR_CTIME)
         && !((v9iattr.valid & ATTR_MASK) & ~ATTR_CTIME))) {
        if (!(v9iattr.valid & ATTR_UID)) {
            v9iattr.uid = -1;
        }
        if (!(v9iattr.valid & ATTR_GID)) {
            v9iattr.gid = -1;
        }
        err = v9fs_co_chown(s, &fidp->path, v9iattr.uid,
                            v9iattr.gid);
        if (err < 0) {
            goto out;
        }
    }
    if (v9iattr.valid & (ATTR_SIZE)) {
        err = v9fs_co_truncate(s, &fidp->path, v9iattr.size);
        if (err < 0) {
            goto out;
        }
    }
    err = offset;
out:
    put_fid(s, fidp);
out_nofid:
    complete_pdu(s, pdu, err);
}

static int v9fs_walk_marshal(V9fsPDU *pdu, uint16_t nwnames, V9fsQID *qids)
{
    int i;
    size_t offset = 7;
    offset += pdu_marshal(pdu, offset, "w", nwnames);
    for (i = 0; i < nwnames; i++) {
        offset += pdu_marshal(pdu, offset, "Q", &qids[i]);
    }
    return offset;
}

static void v9fs_walk(void *opaque)
{
    int name_idx;
    V9fsQID *qids = NULL;
    int i, err = 0;
    V9fsString path;
    uint16_t nwnames;
    struct stat stbuf;
    size_t offset = 7;
    int32_t fid, newfid;
    V9fsString *wnames = NULL;
    V9fsFidState *fidp;
    V9fsFidState *newfidp = NULL;;
    V9fsPDU *pdu = opaque;
    V9fsState *s = pdu->s;

    offset += pdu_unmarshal(pdu, offset, "ddw", &fid,
                            &newfid, &nwnames);

    if (nwnames && nwnames <= P9_MAXWELEM) {
        wnames = g_malloc0(sizeof(wnames[0]) * nwnames);
        qids   = g_malloc0(sizeof(qids[0]) * nwnames);
        for (i = 0; i < nwnames; i++) {
            offset += pdu_unmarshal(pdu, offset, "s", &wnames[i]);
        }

    } else if (nwnames > P9_MAXWELEM) {
        err = -EINVAL;
        goto out_nofid;
    }
    fidp = get_fid(s, fid);
    if (fidp == NULL) {
        err = -ENOENT;
        goto out_nofid;
    }
    if (fid == newfid) {
        BUG_ON(fidp->fid_type != P9_FID_NONE);
        v9fs_string_init(&path);
        for (name_idx = 0; name_idx < nwnames; name_idx++) {
            v9fs_string_sprintf(&path, "%s/%s",
                                fidp->path.data, wnames[name_idx].data);
            v9fs_string_copy(&fidp->path, &path);

            err = v9fs_co_lstat(s, &fidp->path, &stbuf);
            if (err < 0) {
                v9fs_string_free(&path);
                goto out;
            }
            stat_to_qid(&stbuf, &qids[name_idx]);
        }
        v9fs_string_free(&path);
    } else {
        newfidp = alloc_fid(s, newfid);
        if (newfidp == NULL) {
            err = -EINVAL;
            goto out;
        }
        newfidp->uid = fidp->uid;
        v9fs_string_init(&path);
        v9fs_string_copy(&newfidp->path, &fidp->path);
        for (name_idx = 0; name_idx < nwnames; name_idx++) {
            v9fs_string_sprintf(&path, "%s/%s", newfidp->path.data,
                                wnames[name_idx].data);
            v9fs_string_copy(&newfidp->path, &path);
            err = v9fs_co_lstat(s, &newfidp->path, &stbuf);
            if (err < 0) {
                clunk_fid(s, newfidp->fid);
                v9fs_string_free(&path);
                goto out;
            }
            stat_to_qid(&stbuf, &qids[name_idx]);
        }
        v9fs_string_free(&path);
    }
    err = v9fs_walk_marshal(pdu, nwnames, qids);
out:
    put_fid(s, fidp);
    if (newfidp) {
        put_fid(s, newfidp);
    }
out_nofid:
    complete_pdu(s, pdu, err);
    if (nwnames && nwnames <= P9_MAXWELEM) {
        for (name_idx = 0; name_idx < nwnames; name_idx++) {
            v9fs_string_free(&wnames[name_idx]);
        }
        g_free(wnames);
        g_free(qids);
    }
}

static int32_t get_iounit(V9fsState *s, V9fsString *name)
{
    struct statfs stbuf;
    int32_t iounit = 0;

    /*
     * iounit should be multiples of f_bsize (host filesystem block size
     * and as well as less than (client msize - P9_IOHDRSZ))
     */
    if (!v9fs_co_statfs(s, name, &stbuf)) {
        iounit = stbuf.f_bsize;
        iounit *= (s->msize - P9_IOHDRSZ)/stbuf.f_bsize;
    }
    if (!iounit) {
        iounit = s->msize - P9_IOHDRSZ;
    }
    return iounit;
}

static void v9fs_open(void *opaque)
{
    int flags;
    int iounit;
    int32_t fid;
    int32_t mode;
    V9fsQID qid;
    ssize_t err = 0;
    size_t offset = 7;
    struct stat stbuf;
    V9fsFidState *fidp;
    V9fsPDU *pdu = opaque;
    V9fsState *s = pdu->s;

    if (s->proto_version == V9FS_PROTO_2000L) {
        pdu_unmarshal(pdu, offset, "dd", &fid, &mode);
    } else {
        pdu_unmarshal(pdu, offset, "db", &fid, &mode);
    }
    fidp = get_fid(s, fid);
    if (fidp == NULL) {
        err = -ENOENT;
        goto out_nofid;
    }
    BUG_ON(fidp->fid_type != P9_FID_NONE);

    err = v9fs_co_lstat(s, &fidp->path, &stbuf);
    if (err < 0) {
        goto out;
    }
    stat_to_qid(&stbuf, &qid);
    if (S_ISDIR(stbuf.st_mode)) {
        err = v9fs_co_opendir(s, fidp);
        if (err < 0) {
            goto out;
        }
        fidp->fid_type = P9_FID_DIR;
        offset += pdu_marshal(pdu, offset, "Qd", &qid, 0);
        err = offset;
    } else {
        if (s->proto_version == V9FS_PROTO_2000L) {
            flags = mode;
            flags &= ~(O_NOCTTY | O_ASYNC | O_CREAT);
            /* Ignore direct disk access hint until the server supports it. */
            flags &= ~O_DIRECT;
        } else {
            flags = omode_to_uflags(mode);
        }
        err = v9fs_co_open(s, fidp, flags);
        if (err < 0) {
            goto out;
        }
        fidp->fid_type = P9_FID_FILE;
        fidp->open_flags = flags;
        if (flags & O_EXCL) {
            /*
             * We let the host file system do O_EXCL check
             * We should not reclaim such fd
             */
            fidp->flags |= FID_NON_RECLAIMABLE;
        }
        iounit = get_iounit(s, &fidp->path);
        offset += pdu_marshal(pdu, offset, "Qd", &qid, iounit);
        err = offset;
    }
out:
    put_fid(s, fidp);
out_nofid:
    complete_pdu(s, pdu, err);
}

static void v9fs_lcreate(void *opaque)
{
    int32_t dfid, flags, mode;
    gid_t gid;
    ssize_t err = 0;
    ssize_t offset = 7;
    V9fsString fullname;
    V9fsString name;
    V9fsFidState *fidp;
    struct stat stbuf;
    V9fsQID qid;
    int32_t iounit;
    V9fsPDU *pdu = opaque;

    v9fs_string_init(&fullname);
    pdu_unmarshal(pdu, offset, "dsddd", &dfid, &name, &flags,
                  &mode, &gid);

    fidp = get_fid(pdu->s, dfid);
    if (fidp == NULL) {
        err = -ENOENT;
        goto out_nofid;
    }
    v9fs_string_sprintf(&fullname, "%s/%s", fidp->path.data, name.data);

    /* Ignore direct disk access hint until the server supports it. */
    flags &= ~O_DIRECT;

    err = v9fs_co_open2(pdu->s, fidp, fullname.data, gid, flags, mode);
    if (err < 0) {
        goto out;
    }
    fidp->fid_type = P9_FID_FILE;
    fidp->open_flags = flags;
    if (flags & O_EXCL) {
        /*
         * We let the host file system do O_EXCL check
         * We should not reclaim such fd
         */
        fidp->flags |= FID_NON_RECLAIMABLE;
    }
    iounit =  get_iounit(pdu->s, &fullname);

    err = v9fs_co_lstat(pdu->s, &fullname, &stbuf);
    if (err < 0) {
        fidp->fid_type = P9_FID_NONE;
        if (fidp->fs.fd > 0) {
            v9fs_co_close(pdu->s, fidp->fs.fd);
        }
        goto out;
    }
    v9fs_string_copy(&fidp->path, &fullname);
    stat_to_qid(&stbuf, &qid);
    offset += pdu_marshal(pdu, offset, "Qd", &qid, iounit);
    err = offset;
out:
    put_fid(pdu->s, fidp);
out_nofid:
    complete_pdu(pdu->s, pdu, err);
    v9fs_string_free(&name);
    v9fs_string_free(&fullname);
}

static void v9fs_fsync(void *opaque)
{
    int err;
    int32_t fid;
    int datasync;
    size_t offset = 7;
    V9fsFidState *fidp;
    V9fsPDU *pdu = opaque;
    V9fsState *s = pdu->s;

    pdu_unmarshal(pdu, offset, "dd", &fid, &datasync);
    fidp = get_fid(s, fid);
    if (fidp == NULL) {
        err = -ENOENT;
        goto out_nofid;
    }
    err = v9fs_co_fsync(s, fidp, datasync);
    if (!err) {
        err = offset;
    }
    put_fid(s, fidp);
out_nofid:
    complete_pdu(s, pdu, err);
}

static void v9fs_clunk(void *opaque)
{
    int err;
    int32_t fid;
    size_t offset = 7;
    V9fsFidState *fidp;
    V9fsPDU *pdu = opaque;
    V9fsState *s = pdu->s;

    pdu_unmarshal(pdu, offset, "d", &fid);

    fidp = get_fid(s, fid);
    if (fidp == NULL) {
        err = -ENOENT;
        goto out_nofid;
    }
    err = clunk_fid(s, fidp->fid);
    if (err < 0) {
        goto out;
    }
    err = offset;
out:
    put_fid(s, fidp);
out_nofid:
    complete_pdu(s, pdu, err);
}

static int v9fs_xattr_read(V9fsState *s, V9fsPDU *pdu,
                           V9fsFidState *fidp, int64_t off, int32_t max_count)
{
    size_t offset = 7;
    int read_count;
    int64_t xattr_len;

    xattr_len = fidp->fs.xattr.len;
    read_count = xattr_len - off;
    if (read_count > max_count) {
        read_count = max_count;
    } else if (read_count < 0) {
        /*
         * read beyond XATTR value
         */
        read_count = 0;
    }
    offset += pdu_marshal(pdu, offset, "d", read_count);
    offset += pdu_pack(pdu, offset,
                       ((char *)fidp->fs.xattr.value) + off,
                       read_count);
    return offset;
}

static int v9fs_do_readdir_with_stat(V9fsState *s, V9fsPDU *pdu,
                                     V9fsFidState *fidp, int32_t max_count)
{
    V9fsString name;
    V9fsStat v9stat;
    int len, err = 0;
    int32_t count = 0;
    struct stat stbuf;
    off_t saved_dir_pos;
    struct dirent *dent, *result;

    /* save the directory position */
    saved_dir_pos = v9fs_co_telldir(s, fidp);
    if (saved_dir_pos < 0) {
        return saved_dir_pos;
    }

    dent = g_malloc(sizeof(struct dirent));

    while (1) {
        v9fs_string_init(&name);
        err = v9fs_co_readdir_r(s, fidp, dent, &result);
        if (err || !result) {
            break;
        }
        v9fs_string_sprintf(&name, "%s/%s", fidp->path.data, dent->d_name);
        err = v9fs_co_lstat(s, &name, &stbuf);
        if (err < 0) {
            goto out;
        }
        err = stat_to_v9stat(s, &name, &stbuf, &v9stat);
        if (err < 0) {
            goto out;
        }
        /* 11 = 7 + 4 (7 = start offset, 4 = space for storing count) */
        len = pdu_marshal(pdu, 11 + count, "S", &v9stat);
        if ((len != (v9stat.size + 2)) || ((count + len) > max_count)) {
            /* Ran out of buffer. Set dir back to old position and return */
            v9fs_co_seekdir(s, fidp, saved_dir_pos);
            v9fs_stat_free(&v9stat);
            v9fs_string_free(&name);
            g_free(dent);
            return count;
        }
        count += len;
        v9fs_stat_free(&v9stat);
        v9fs_string_free(&name);
        saved_dir_pos = dent->d_off;
    }
out:
    g_free(dent);
    v9fs_string_free(&name);
    if (err < 0) {
        return err;
    }
    return count;
}

static void v9fs_read(void *opaque)
{
    int32_t fid;
    int64_t off;
    ssize_t err = 0;
    int32_t count = 0;
    size_t offset = 7;
    int32_t max_count;
    V9fsFidState *fidp;
    V9fsPDU *pdu = opaque;
    V9fsState *s = pdu->s;

    pdu_unmarshal(pdu, offset, "dqd", &fid, &off, &max_count);

    fidp = get_fid(s, fid);
    if (fidp == NULL) {
        err = -EINVAL;
        goto out_nofid;
    }
    if (fidp->fid_type == P9_FID_DIR) {

        if (off == 0) {
            v9fs_co_rewinddir(s, fidp);
        }
        count = v9fs_do_readdir_with_stat(s, pdu, fidp, max_count);
        if (count < 0) {
            err = count;
            goto out;
        }
        err = offset;
        err += pdu_marshal(pdu, offset, "d", count);
        err += count;
    } else if (fidp->fid_type == P9_FID_FILE) {
        int32_t cnt;
        int32_t len;
        struct iovec *sg;
        struct iovec iov[128]; /* FIXME: bad, bad, bad */

        sg = iov;
        pdu_marshal(pdu, offset + 4, "v", sg, &cnt);
        sg = cap_sg(sg, max_count, &cnt);
        do {
            if (0) {
                print_sg(sg, cnt);
            }
            /* Loop in case of EINTR */
            do {
                len = v9fs_co_preadv(s, fidp, sg, cnt, off);
                if (len >= 0) {
                    off   += len;
                    count += len;
                }
            } while (len == -EINTR);
            if (len < 0) {
                /* IO error return the error */
                err = len;
                goto out;
            }
            sg = adjust_sg(sg, len, &cnt);
        } while (count < max_count && len > 0);
        err = offset;
        err += pdu_marshal(pdu, offset, "d", count);
        err += count;
    } else if (fidp->fid_type == P9_FID_XATTR) {
        err = v9fs_xattr_read(s, pdu, fidp, off, max_count);
    } else {
        err = -EINVAL;
    }
out:
    put_fid(s, fidp);
out_nofid:
    complete_pdu(s, pdu, err);
}

static size_t v9fs_readdir_data_size(V9fsString *name)
{
    /*
     * Size of each dirent on the wire: size of qid (13) + size of offset (8)
     * size of type (1) + size of name.size (2) + strlen(name.data)
     */
    return 24 + v9fs_string_size(name);
}

static int v9fs_do_readdir(V9fsState *s, V9fsPDU *pdu,
                           V9fsFidState *fidp, int32_t max_count)
{
    size_t size;
    V9fsQID qid;
    V9fsString name;
    int len, err = 0;
    int32_t count = 0;
    off_t saved_dir_pos;
    struct dirent *dent, *result;

    /* save the directory position */
    saved_dir_pos = v9fs_co_telldir(s, fidp);
    if (saved_dir_pos < 0) {
        return saved_dir_pos;
    }

    dent = g_malloc(sizeof(struct dirent));

    while (1) {
        err = v9fs_co_readdir_r(s, fidp, dent, &result);
        if (err || !result) {
            break;
        }
        v9fs_string_init(&name);
        v9fs_string_sprintf(&name, "%s", dent->d_name);
        if ((count + v9fs_readdir_data_size(&name)) > max_count) {
            /* Ran out of buffer. Set dir back to old position and return */
            v9fs_co_seekdir(s, fidp, saved_dir_pos);
            v9fs_string_free(&name);
            g_free(dent);
            return count;
        }
        /*
         * Fill up just the path field of qid because the client uses
         * only that. To fill the entire qid structure we will have
         * to stat each dirent found, which is expensive
         */
        size = MIN(sizeof(dent->d_ino), sizeof(qid.path));
        memcpy(&qid.path, &dent->d_ino, size);
        /* Fill the other fields with dummy values */
        qid.type = 0;
        qid.version = 0;

        /* 11 = 7 + 4 (7 = start offset, 4 = space for storing count) */
        len = pdu_marshal(pdu, 11 + count, "Qqbs",
                          &qid, dent->d_off,
                          dent->d_type, &name);
        count += len;
        v9fs_string_free(&name);
        saved_dir_pos = dent->d_off;
    }
    g_free(dent);
    if (err < 0) {
        return err;
    }
    return count;
}

static void v9fs_readdir(void *opaque)
{
    int32_t fid;
    V9fsFidState *fidp;
    ssize_t retval = 0;
    size_t offset = 7;
    int64_t initial_offset;
    int32_t count, max_count;
    V9fsPDU *pdu = opaque;
    V9fsState *s = pdu->s;

    pdu_unmarshal(pdu, offset, "dqd", &fid, &initial_offset, &max_count);

    fidp = get_fid(s, fid);
    if (fidp == NULL) {
        retval = -EINVAL;
        goto out_nofid;
    }
    if (!fidp->fs.dir) {
        retval = -EINVAL;
        goto out;
    }
    if (initial_offset == 0) {
        v9fs_co_rewinddir(s, fidp);
    } else {
        v9fs_co_seekdir(s, fidp, initial_offset);
    }
    count = v9fs_do_readdir(s, pdu, fidp, max_count);
    if (count < 0) {
        retval = count;
        goto out;
    }
    retval = offset;
    retval += pdu_marshal(pdu, offset, "d", count);
    retval += count;
out:
    put_fid(s, fidp);
out_nofid:
    complete_pdu(s, pdu, retval);
}

static int v9fs_xattr_write(V9fsState *s, V9fsPDU *pdu, V9fsFidState *fidp,
                            int64_t off, int32_t count,
                            struct iovec *sg, int cnt)
{
    int i, to_copy;
    ssize_t err = 0;
    int write_count;
    int64_t xattr_len;
    size_t offset = 7;


    xattr_len = fidp->fs.xattr.len;
    write_count = xattr_len - off;
    if (write_count > count) {
        write_count = count;
    } else if (write_count < 0) {
        /*
         * write beyond XATTR value len specified in
         * xattrcreate
         */
        err = -ENOSPC;
        goto out;
    }
    offset += pdu_marshal(pdu, offset, "d", write_count);
    err = offset;
    fidp->fs.xattr.copied_len += write_count;
    /*
     * Now copy the content from sg list
     */
    for (i = 0; i < cnt; i++) {
        if (write_count > sg[i].iov_len) {
            to_copy = sg[i].iov_len;
        } else {
            to_copy = write_count;
        }
        memcpy((char *)fidp->fs.xattr.value + off, sg[i].iov_base, to_copy);
        /* updating vs->off since we are not using below */
        off += to_copy;
        write_count -= to_copy;
    }
out:
    return err;
}

static void v9fs_write(void *opaque)
{
    int cnt;
    ssize_t err;
    int32_t fid;
    int64_t off;
    int32_t count;
    int32_t len = 0;
    int32_t total = 0;
    size_t offset = 7;
    V9fsFidState *fidp;
    struct iovec iov[128]; /* FIXME: bad, bad, bad */
    struct iovec *sg = iov;
    V9fsPDU *pdu = opaque;
    V9fsState *s = pdu->s;

    pdu_unmarshal(pdu, offset, "dqdv", &fid, &off, &count, sg, &cnt);

    fidp = get_fid(s, fid);
    if (fidp == NULL) {
        err = -EINVAL;
        goto out_nofid;
    }
    if (fidp->fid_type == P9_FID_FILE) {
        if (fidp->fs.fd == -1) {
            err = -EINVAL;
            goto out;
        }
    } else if (fidp->fid_type == P9_FID_XATTR) {
        /*
         * setxattr operation
         */
        err = v9fs_xattr_write(s, pdu, fidp, off, count, sg, cnt);
        goto out;
    } else {
        err = -EINVAL;
        goto out;
    }
    sg = cap_sg(sg, count, &cnt);
    do {
        if (0) {
            print_sg(sg, cnt);
        }
        /* Loop in case of EINTR */
        do {
            len = v9fs_co_pwritev(s, fidp, sg, cnt, off);
            if (len >= 0) {
                off   += len;
                total += len;
            }
        } while (len == -EINTR);
        if (len < 0) {
            /* IO error return the error */
            err = len;
            goto out;
        }
        sg = adjust_sg(sg, len, &cnt);
    } while (total < count && len > 0);
    offset += pdu_marshal(pdu, offset, "d", total);
    err = offset;
out:
    put_fid(s, fidp);
out_nofid:
    complete_pdu(s, pdu, err);
}

static void v9fs_create(void *opaque)
{
    int32_t fid;
    int err = 0;
    size_t offset = 7;
    V9fsFidState *fidp;
    V9fsQID qid;
    int32_t perm;
    int8_t mode;
    struct stat stbuf;
    V9fsString name;
    V9fsString extension;
    V9fsString fullname;
    int iounit;
    V9fsPDU *pdu = opaque;

    v9fs_string_init(&fullname);

    pdu_unmarshal(pdu, offset, "dsdbs", &fid, &name,
                  &perm, &mode, &extension);

    fidp = get_fid(pdu->s, fid);
    if (fidp == NULL) {
        err = -EINVAL;
        goto out_nofid;
    }

    v9fs_string_sprintf(&fullname, "%s/%s", fidp->path.data, name.data);
    err = v9fs_co_lstat(pdu->s, &fullname, &stbuf);
    if (!err) {
        err = -EEXIST;
        goto out;
    } else if (err != -ENOENT) {
        goto out;
    }
    if (perm & P9_STAT_MODE_DIR) {
        err = v9fs_co_mkdir(pdu->s, fullname.data, perm & 0777,
                            fidp->uid, -1);
        if (err < 0) {
            goto out;
        }
        v9fs_string_copy(&fidp->path, &fullname);
        err = v9fs_co_opendir(pdu->s, fidp);
        if (err < 0) {
            goto out;
        }
        fidp->fid_type = P9_FID_DIR;
    } else if (perm & P9_STAT_MODE_SYMLINK) {
        err = v9fs_co_symlink(pdu->s, fidp, extension.data,
                              fullname.data, -1);
        if (err < 0) {
            goto out;
        }
    } else if (perm & P9_STAT_MODE_LINK) {
        int32_t nfid = atoi(extension.data);
        V9fsFidState *nfidp = get_fid(pdu->s, nfid);
        if (nfidp == NULL) {
            err = -EINVAL;
            goto out;
        }
        err = v9fs_co_link(pdu->s, &nfidp->path, &fullname);
        if (err < 0) {
            put_fid(pdu->s, nfidp);
            goto out;
        }
        put_fid(pdu->s, nfidp);
    } else if (perm & P9_STAT_MODE_DEVICE) {
        char ctype;
        uint32_t major, minor;
        mode_t nmode = 0;

        if (sscanf(extension.data, "%c %u %u", &ctype, &major, &minor) != 3) {
            err = -errno;
            goto out;
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
            goto out;
        }

        nmode |= perm & 0777;
        err = v9fs_co_mknod(pdu->s, &fullname, fidp->uid, -1,
                            makedev(major, minor), nmode);
        if (err < 0) {
            goto out;
        }
    } else if (perm & P9_STAT_MODE_NAMED_PIPE) {
        err = v9fs_co_mknod(pdu->s, &fullname, fidp->uid, -1,
                            0, S_IFIFO | (perm & 0777));
        if (err < 0) {
            goto out;
        }
    } else if (perm & P9_STAT_MODE_SOCKET) {
        err = v9fs_co_mknod(pdu->s, &fullname, fidp->uid, -1,
                            0, S_IFSOCK | (perm & 0777));
        if (err < 0) {
            goto out;
        }
    } else {
        err = v9fs_co_open2(pdu->s, fidp, fullname.data, -1,
                            omode_to_uflags(mode)|O_CREAT, perm);
        if (err < 0) {
            goto out;
        }
        fidp->fid_type = P9_FID_FILE;
        fidp->open_flags = omode_to_uflags(mode);
        if (fidp->open_flags & O_EXCL) {
            /*
             * We let the host file system do O_EXCL check
             * We should not reclaim such fd
             */
            fidp->flags |= FID_NON_RECLAIMABLE;
        }
    }
    err = v9fs_co_lstat(pdu->s, &fullname, &stbuf);
    if (err < 0) {
        fidp->fid_type = P9_FID_NONE;
        if (fidp->fs.fd) {
            v9fs_co_close(pdu->s, fidp->fs.fd);
        }
        goto out;
    }
    iounit = get_iounit(pdu->s, &fidp->path);
    v9fs_string_copy(&fidp->path, &fullname);
    stat_to_qid(&stbuf, &qid);
    offset += pdu_marshal(pdu, offset, "Qd", &qid, iounit);
    err = offset;
out:
    put_fid(pdu->s, fidp);
out_nofid:
   complete_pdu(pdu->s, pdu, err);
   v9fs_string_free(&name);
   v9fs_string_free(&extension);
   v9fs_string_free(&fullname);
}

static void v9fs_symlink(void *opaque)
{
    V9fsPDU *pdu = opaque;
    V9fsString name;
    V9fsString symname;
    V9fsString fullname;
    V9fsFidState *dfidp;
    V9fsQID qid;
    struct stat stbuf;
    int32_t dfid;
    int err = 0;
    gid_t gid;
    size_t offset = 7;

    v9fs_string_init(&fullname);
    pdu_unmarshal(pdu, offset, "dssd", &dfid, &name, &symname, &gid);

    dfidp = get_fid(pdu->s, dfid);
    if (dfidp == NULL) {
        err = -EINVAL;
        goto out_nofid;
    }

    v9fs_string_sprintf(&fullname, "%s/%s", dfidp->path.data, name.data);
    err = v9fs_co_symlink(pdu->s, dfidp, symname.data, fullname.data, gid);
    if (err < 0) {
        goto out;
    }
    err = v9fs_co_lstat(pdu->s, &fullname, &stbuf);
    if (err < 0) {
        goto out;
    }
    stat_to_qid(&stbuf, &qid);
    offset += pdu_marshal(pdu, offset, "Q", &qid);
    err = offset;
out:
    put_fid(pdu->s, dfidp);
out_nofid:
    complete_pdu(pdu->s, pdu, err);
    v9fs_string_free(&name);
    v9fs_string_free(&symname);
    v9fs_string_free(&fullname);
}

static void v9fs_flush(void *opaque)
{
    V9fsPDU *pdu = opaque;
    V9fsState *s = pdu->s;
    /* A nop call with no return */
    complete_pdu(s, pdu, 7);
    return;
}

static void v9fs_link(void *opaque)
{
    V9fsPDU *pdu = opaque;
    V9fsState *s = pdu->s;
    int32_t dfid, oldfid;
    V9fsFidState *dfidp, *oldfidp;
    V9fsString name, fullname;
    size_t offset = 7;
    int err = 0;

    v9fs_string_init(&fullname);

    pdu_unmarshal(pdu, offset, "dds", &dfid, &oldfid, &name);

    dfidp = get_fid(s, dfid);
    if (dfidp == NULL) {
        err = -ENOENT;
        goto out_nofid;
    }

    oldfidp = get_fid(s, oldfid);
    if (oldfidp == NULL) {
        err = -ENOENT;
        goto out;
    }

    v9fs_string_sprintf(&fullname, "%s/%s", dfidp->path.data, name.data);
    err = v9fs_co_link(s, &oldfidp->path, &fullname);
    if (!err) {
        err = offset;
    }
    v9fs_string_free(&fullname);

out:
    put_fid(s, dfidp);
out_nofid:
    v9fs_string_free(&name);
    complete_pdu(s, pdu, err);
}

static void v9fs_remove(void *opaque)
{
    int32_t fid;
    int err = 0;
    size_t offset = 7;
    V9fsFidState *fidp;
    V9fsPDU *pdu = opaque;

    pdu_unmarshal(pdu, offset, "d", &fid);

    fidp = get_fid(pdu->s, fid);
    if (fidp == NULL) {
        err = -EINVAL;
        goto out_nofid;
    }
    /*
     * IF the file is unlinked, we cannot reopen
     * the file later. So don't reclaim fd
     */
    err = v9fs_mark_fids_unreclaim(pdu->s, &fidp->path);
    if (err < 0) {
        goto out_err;
    }
    err = v9fs_co_remove(pdu->s, &fidp->path);
    if (!err) {
        err = offset;
    }
out_err:
    /* For TREMOVE we need to clunk the fid even on failed remove */
    clunk_fid(pdu->s, fidp->fid);
    put_fid(pdu->s, fidp);
out_nofid:
    complete_pdu(pdu->s, pdu, err);
}

static int v9fs_complete_rename(V9fsState *s, V9fsFidState *fidp,
                                int32_t newdirfid, V9fsString *name)
{
    char *end;
    int err = 0;
    V9fsFidState *dirfidp = NULL;
    char *old_name, *new_name;

    if (newdirfid != -1) {
        dirfidp = get_fid(s, newdirfid);
        if (dirfidp == NULL) {
            err = -ENOENT;
            goto out_nofid;
        }
        BUG_ON(dirfidp->fid_type != P9_FID_NONE);

        new_name = g_malloc0(dirfidp->path.size + name->size + 2);

        strcpy(new_name, dirfidp->path.data);
        strcat(new_name, "/");
        strcat(new_name + dirfidp->path.size, name->data);
    } else {
        old_name = fidp->path.data;
        end = strrchr(old_name, '/');
        if (end) {
            end++;
        } else {
            end = old_name;
        }
        new_name = g_malloc0(end - old_name + name->size + 1);

        strncat(new_name, old_name, end - old_name);
        strncat(new_name + (end - old_name), name->data, name->size);
    }

    v9fs_string_free(name);
    name->data = new_name;
    name->size = strlen(new_name);

    if (strcmp(new_name, fidp->path.data) != 0) {
        err = v9fs_co_rename(s, &fidp->path, name);
        if (err < 0) {
            goto out;
        }
        V9fsFidState *tfidp;
        /*
         * Fixup fid's pointing to the old name to
         * start pointing to the new name
         */
        for (tfidp = s->fid_list; tfidp; tfidp = tfidp->next) {
            if (fidp == tfidp) {
                /*
                 * we replace name of this fid towards the end
                 * so that our below strcmp will work
                 */
                continue;
            }
            if (v9fs_path_is_ancestor(&fidp->path, &tfidp->path)) {
                /* replace the name */
                v9fs_fix_path(&tfidp->path, name, strlen(fidp->path.data));
            }
        }
        v9fs_string_copy(&fidp->path, name);
    }
out:
    if (dirfidp) {
        put_fid(s, dirfidp);
    }
out_nofid:
    return err;
}

static void v9fs_rename(void *opaque)
{
    int32_t fid;
    ssize_t err = 0;
    size_t offset = 7;
    V9fsString name;
    int32_t newdirfid;
    V9fsFidState *fidp;
    V9fsPDU *pdu = opaque;
    V9fsState *s = pdu->s;

    pdu_unmarshal(pdu, offset, "dds", &fid, &newdirfid, &name);

    fidp = get_fid(s, fid);
    if (fidp == NULL) {
        err = -ENOENT;
        goto out_nofid;
    }
    BUG_ON(fidp->fid_type != P9_FID_NONE);

    err = v9fs_complete_rename(s, fidp, newdirfid, &name);
    if (!err) {
        err = offset;
    }
    put_fid(s, fidp);
out_nofid:
    complete_pdu(s, pdu, err);
    v9fs_string_free(&name);
}

static int v9fs_complete_renameat(V9fsState *s, int32_t olddirfid,
                                  V9fsString *old_name, int32_t newdirfid,
                                  V9fsString *new_name)
{
    int err = 0;
    V9fsString old_full_name, new_full_name;
    V9fsFidState *newdirfidp = NULL, *olddirfidp = NULL;

    olddirfidp = get_fid(s, olddirfid);
    if (olddirfidp == NULL) {
        err = -ENOENT;
        goto out;
    }
    v9fs_string_init(&old_full_name);
    v9fs_string_init(&new_full_name);

    v9fs_string_sprintf(&old_full_name, "%s/%s",
                        olddirfidp->path.data, old_name->data);
    if (newdirfid != -1) {
        newdirfidp = get_fid(s, newdirfid);
        if (newdirfidp == NULL) {
            err = -ENOENT;
            goto out;
        }
        v9fs_string_sprintf(&new_full_name, "%s/%s",
                            newdirfidp->path.data, new_name->data);
    } else {
        v9fs_string_sprintf(&new_full_name, "%s/%s",
                            olddirfidp->path.data, new_name->data);
    }

    if (strcmp(old_full_name.data, new_full_name.data) != 0) {
        V9fsFidState *tfidp;
        err = v9fs_co_rename(s, &old_full_name, &new_full_name);
        if (err < 0) {
            goto out;
        }
        /*
         * Fixup fid's pointing to the old name to
         * start pointing to the new name
         */
        for (tfidp = s->fid_list; tfidp; tfidp = tfidp->next) {
            if (v9fs_path_is_ancestor(&old_full_name, &tfidp->path)) {
                /* replace the name */
                v9fs_fix_path(&tfidp->path, &new_full_name, old_full_name.size);
            }
        }
    }
out:
    if (olddirfidp) {
        put_fid(s, olddirfidp);
    }
    if (newdirfidp) {
        put_fid(s, newdirfidp);
    }
    v9fs_string_free(&old_full_name);
    v9fs_string_free(&new_full_name);
    return err;
}

static void v9fs_renameat(void *opaque)
{
    ssize_t err = 0;
    size_t offset = 7;
    V9fsPDU *pdu = opaque;
    V9fsState *s = pdu->s;
    int32_t olddirfid, newdirfid;
    V9fsString old_name, new_name;

    pdu_unmarshal(pdu, offset, "dsds", &olddirfid,
                  &old_name, &newdirfid, &new_name);

    err = v9fs_complete_renameat(s, olddirfid, &old_name, newdirfid, &new_name);
    if (!err) {
        err = offset;
    }
    complete_pdu(s, pdu, err);
    v9fs_string_free(&old_name);
    v9fs_string_free(&new_name);
}

static void v9fs_wstat(void *opaque)
{
    int32_t fid;
    int err = 0;
    int16_t unused;
    V9fsStat v9stat;
    size_t offset = 7;
    struct stat stbuf;
    V9fsFidState *fidp;
    V9fsPDU *pdu = opaque;
    V9fsState *s = pdu->s;

    pdu_unmarshal(pdu, offset, "dwS", &fid, &unused, &v9stat);

    fidp = get_fid(s, fid);
    if (fidp == NULL) {
        err = -EINVAL;
        goto out_nofid;
    }
    /* do we need to sync the file? */
    if (donttouch_stat(&v9stat)) {
        err = v9fs_co_fsync(s, fidp, 0);
        goto out;
    }
    if (v9stat.mode != -1) {
        uint32_t v9_mode;
        err = v9fs_co_lstat(s, &fidp->path, &stbuf);
        if (err < 0) {
            goto out;
        }
        v9_mode = stat_to_v9mode(&stbuf);
        if ((v9stat.mode & P9_STAT_MODE_TYPE_BITS) !=
            (v9_mode & P9_STAT_MODE_TYPE_BITS)) {
            /* Attempting to change the type */
            err = -EIO;
            goto out;
        }
        err = v9fs_co_chmod(s, &fidp->path,
                            v9mode_to_mode(v9stat.mode,
                                           &v9stat.extension));
        if (err < 0) {
            goto out;
        }
    }
    if (v9stat.mtime != -1 || v9stat.atime != -1) {
        struct timespec times[2];
        if (v9stat.atime != -1) {
            times[0].tv_sec = v9stat.atime;
            times[0].tv_nsec = 0;
        } else {
            times[0].tv_nsec = UTIME_OMIT;
        }
        if (v9stat.mtime != -1) {
            times[1].tv_sec = v9stat.mtime;
            times[1].tv_nsec = 0;
        } else {
            times[1].tv_nsec = UTIME_OMIT;
        }
        err = v9fs_co_utimensat(s, &fidp->path, times);
        if (err < 0) {
            goto out;
        }
    }
    if (v9stat.n_gid != -1 || v9stat.n_uid != -1) {
        err = v9fs_co_chown(s, &fidp->path, v9stat.n_uid, v9stat.n_gid);
        if (err < 0) {
            goto out;
        }
    }
    if (v9stat.name.size != 0) {
        err = v9fs_complete_rename(s, fidp, -1, &v9stat.name);
        if (err < 0) {
            goto out;
        }
    }
    if (v9stat.length != -1) {
        err = v9fs_co_truncate(s, &fidp->path, v9stat.length);
        if (err < 0) {
            goto out;
        }
    }
    err = offset;
out:
    put_fid(s, fidp);
out_nofid:
    v9fs_stat_free(&v9stat);
    complete_pdu(s, pdu, err);
}

static int v9fs_fill_statfs(V9fsState *s, V9fsPDU *pdu, struct statfs *stbuf)
{
    uint32_t f_type;
    uint32_t f_bsize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    uint64_t f_bavail;
    uint64_t f_files;
    uint64_t f_ffree;
    uint64_t fsid_val;
    uint32_t f_namelen;
    size_t offset = 7;
    int32_t bsize_factor;

    /*
     * compute bsize factor based on host file system block size
     * and client msize
     */
    bsize_factor = (s->msize - P9_IOHDRSZ)/stbuf->f_bsize;
    if (!bsize_factor) {
        bsize_factor = 1;
    }
    f_type  = stbuf->f_type;
    f_bsize = stbuf->f_bsize;
    f_bsize *= bsize_factor;
    /*
     * f_bsize is adjusted(multiplied) by bsize factor, so we need to
     * adjust(divide) the number of blocks, free blocks and available
     * blocks by bsize factor
     */
    f_blocks = stbuf->f_blocks/bsize_factor;
    f_bfree  = stbuf->f_bfree/bsize_factor;
    f_bavail = stbuf->f_bavail/bsize_factor;
    f_files  = stbuf->f_files;
    f_ffree  = stbuf->f_ffree;
    fsid_val = (unsigned int) stbuf->f_fsid.__val[0] |
               (unsigned long long)stbuf->f_fsid.__val[1] << 32;
    f_namelen = stbuf->f_namelen;

    return pdu_marshal(pdu, offset, "ddqqqqqqd",
                       f_type, f_bsize, f_blocks, f_bfree,
                       f_bavail, f_files, f_ffree,
                       fsid_val, f_namelen);
}

static void v9fs_statfs(void *opaque)
{
    int32_t fid;
    ssize_t retval = 0;
    size_t offset = 7;
    V9fsFidState *fidp;
    struct statfs stbuf;
    V9fsPDU *pdu = opaque;
    V9fsState *s = pdu->s;

    pdu_unmarshal(pdu, offset, "d", &fid);
    fidp = get_fid(s, fid);
    if (fidp == NULL) {
        retval = -ENOENT;
        goto out_nofid;
    }
    retval = v9fs_co_statfs(s, &fidp->path, &stbuf);
    if (retval < 0) {
        goto out;
    }
    retval = offset;
    retval += v9fs_fill_statfs(s, pdu, &stbuf);
out:
    put_fid(s, fidp);
out_nofid:
    complete_pdu(s, pdu, retval);
    return;
}

static void v9fs_mknod(void *opaque)
{

    int mode;
    gid_t gid;
    int32_t fid;
    V9fsQID qid;
    int err = 0;
    int major, minor;
    size_t offset = 7;
    V9fsString name;
    struct stat stbuf;
    V9fsString fullname;
    V9fsFidState *fidp;
    V9fsPDU *pdu = opaque;
    V9fsState *s = pdu->s;

    v9fs_string_init(&fullname);
    pdu_unmarshal(pdu, offset, "dsdddd", &fid, &name, &mode,
                  &major, &minor, &gid);

    fidp = get_fid(s, fid);
    if (fidp == NULL) {
        err = -ENOENT;
        goto out_nofid;
    }
    v9fs_string_sprintf(&fullname, "%s/%s", fidp->path.data, name.data);
    err = v9fs_co_mknod(s, &fullname, fidp->uid, gid,
                        makedev(major, minor), mode);
    if (err < 0) {
        goto out;
    }
    err = v9fs_co_lstat(s, &fullname, &stbuf);
    if (err < 0) {
        goto out;
    }
    stat_to_qid(&stbuf, &qid);
    err = offset;
    err += pdu_marshal(pdu, offset, "Q", &qid);
out:
    put_fid(s, fidp);
out_nofid:
    complete_pdu(s, pdu, err);
    v9fs_string_free(&fullname);
    v9fs_string_free(&name);
}

/*
 * Implement posix byte range locking code
 * Server side handling of locking code is very simple, because 9p server in
 * QEMU can handle only one client. And most of the lock handling
 * (like conflict, merging) etc is done by the VFS layer itself, so no need to
 * do any thing in * qemu 9p server side lock code path.
 * So when a TLOCK request comes, always return success
 */
static void v9fs_lock(void *opaque)
{
    int8_t status;
    V9fsFlock *flock;
    size_t offset = 7;
    struct stat stbuf;
    V9fsFidState *fidp;
    int32_t fid, err = 0;
    V9fsPDU *pdu = opaque;
    V9fsState *s = pdu->s;

    flock = g_malloc(sizeof(*flock));
    pdu_unmarshal(pdu, offset, "dbdqqds", &fid, &flock->type,
                  &flock->flags, &flock->start, &flock->length,
                  &flock->proc_id, &flock->client_id);
    status = P9_LOCK_ERROR;

    /* We support only block flag now (that too ignored currently) */
    if (flock->flags & ~P9_LOCK_FLAGS_BLOCK) {
        err = -EINVAL;
        goto out_nofid;
    }
    fidp = get_fid(s, fid);
    if (fidp == NULL) {
        err = -ENOENT;
        goto out_nofid;
    }
    err = v9fs_co_fstat(s, fidp->fs.fd, &stbuf);
    if (err < 0) {
        goto out;
    }
    status = P9_LOCK_SUCCESS;
out:
    put_fid(s, fidp);
out_nofid:
    err = offset;
    err += pdu_marshal(pdu, offset, "b", status);
    complete_pdu(s, pdu, err);
    v9fs_string_free(&flock->client_id);
    g_free(flock);
}

/*
 * When a TGETLOCK request comes, always return success because all lock
 * handling is done by client's VFS layer.
 */
static void v9fs_getlock(void *opaque)
{
    size_t offset = 7;
    struct stat stbuf;
    V9fsFidState *fidp;
    V9fsGetlock *glock;
    int32_t fid, err = 0;
    V9fsPDU *pdu = opaque;
    V9fsState *s = pdu->s;

    glock = g_malloc(sizeof(*glock));
    pdu_unmarshal(pdu, offset, "dbqqds", &fid, &glock->type,
                  &glock->start, &glock->length, &glock->proc_id,
                  &glock->client_id);

    fidp = get_fid(s, fid);
    if (fidp == NULL) {
        err = -ENOENT;
        goto out_nofid;
    }
    err = v9fs_co_fstat(s, fidp->fs.fd, &stbuf);
    if (err < 0) {
        goto out;
    }
    glock->type = F_UNLCK;
    offset += pdu_marshal(pdu, offset, "bqqds", glock->type,
                          glock->start, glock->length, glock->proc_id,
                          &glock->client_id);
    err = offset;
out:
    put_fid(s, fidp);
out_nofid:
    complete_pdu(s, pdu, err);
    v9fs_string_free(&glock->client_id);
    g_free(glock);
}

static void v9fs_mkdir(void *opaque)
{
    V9fsPDU *pdu = opaque;
    size_t offset = 7;
    int32_t fid;
    struct stat stbuf;
    V9fsString name, fullname;
    V9fsQID qid;
    V9fsFidState *fidp;
    gid_t gid;
    int mode;
    int err = 0;

    v9fs_string_init(&fullname);
    pdu_unmarshal(pdu, offset, "dsdd", &fid, &name, &mode, &gid);

    fidp = get_fid(pdu->s, fid);
    if (fidp == NULL) {
        err = -ENOENT;
        goto out_nofid;
    }
    v9fs_string_sprintf(&fullname, "%s/%s", fidp->path.data, name.data);
    err = v9fs_co_mkdir(pdu->s, fullname.data, mode, fidp->uid, gid);
    if (err < 0) {
        goto out;
    }
    err = v9fs_co_lstat(pdu->s, &fullname, &stbuf);
    if (err < 0) {
        goto out;
    }
    stat_to_qid(&stbuf, &qid);
    offset += pdu_marshal(pdu, offset, "Q", &qid);
    err = offset;
out:
    put_fid(pdu->s, fidp);
out_nofid:
    complete_pdu(pdu->s, pdu, err);
    v9fs_string_free(&fullname);
    v9fs_string_free(&name);
}

static void v9fs_xattrwalk(void *opaque)
{
    int64_t size;
    V9fsString name;
    ssize_t err = 0;
    size_t offset = 7;
    int32_t fid, newfid;
    V9fsFidState *file_fidp;
    V9fsFidState *xattr_fidp = NULL;
    V9fsPDU *pdu = opaque;
    V9fsState *s = pdu->s;

    pdu_unmarshal(pdu, offset, "dds", &fid, &newfid, &name);
    file_fidp = get_fid(s, fid);
    if (file_fidp == NULL) {
        err = -ENOENT;
        goto out_nofid;
    }
    xattr_fidp = alloc_fid(s, newfid);
    if (xattr_fidp == NULL) {
        err = -EINVAL;
        goto out;
    }
    v9fs_string_copy(&xattr_fidp->path, &file_fidp->path);
    if (name.data[0] == 0) {
        /*
         * listxattr request. Get the size first
         */
        size = v9fs_co_llistxattr(s, &xattr_fidp->path, NULL, 0);
        if (size < 0) {
            err = size;
            clunk_fid(s, xattr_fidp->fid);
            goto out;
        }
        /*
         * Read the xattr value
         */
        xattr_fidp->fs.xattr.len = size;
        xattr_fidp->fid_type = P9_FID_XATTR;
        xattr_fidp->fs.xattr.copied_len = -1;
        if (size) {
            xattr_fidp->fs.xattr.value = g_malloc(size);
            err = v9fs_co_llistxattr(s, &xattr_fidp->path,
                                     xattr_fidp->fs.xattr.value,
                                     xattr_fidp->fs.xattr.len);
            if (err < 0) {
                clunk_fid(s, xattr_fidp->fid);
                goto out;
            }
        }
        offset += pdu_marshal(pdu, offset, "q", size);
        err = offset;
    } else {
        /*
         * specific xattr fid. We check for xattr
         * presence also collect the xattr size
         */
        size = v9fs_co_lgetxattr(s, &xattr_fidp->path,
                                 &name, NULL, 0);
        if (size < 0) {
            err = size;
            clunk_fid(s, xattr_fidp->fid);
            goto out;
        }
        /*
         * Read the xattr value
         */
        xattr_fidp->fs.xattr.len = size;
        xattr_fidp->fid_type = P9_FID_XATTR;
        xattr_fidp->fs.xattr.copied_len = -1;
        if (size) {
            xattr_fidp->fs.xattr.value = g_malloc(size);
            err = v9fs_co_lgetxattr(s, &xattr_fidp->path,
                                    &name, xattr_fidp->fs.xattr.value,
                                    xattr_fidp->fs.xattr.len);
            if (err < 0) {
                clunk_fid(s, xattr_fidp->fid);
                goto out;
            }
        }
        offset += pdu_marshal(pdu, offset, "q", size);
        err = offset;
    }
out:
    put_fid(s, file_fidp);
    if (xattr_fidp) {
        put_fid(s, xattr_fidp);
    }
out_nofid:
    complete_pdu(s, pdu, err);
    v9fs_string_free(&name);
}

static void v9fs_xattrcreate(void *opaque)
{
    int flags;
    int32_t fid;
    int64_t size;
    ssize_t err = 0;
    V9fsString name;
    size_t offset = 7;
    V9fsFidState *file_fidp;
    V9fsFidState *xattr_fidp;
    V9fsPDU *pdu = opaque;
    V9fsState *s = pdu->s;

    pdu_unmarshal(pdu, offset, "dsqd",
                  &fid, &name, &size, &flags);

    file_fidp = get_fid(s, fid);
    if (file_fidp == NULL) {
        err = -EINVAL;
        goto out_nofid;
    }
    /* Make the file fid point to xattr */
    xattr_fidp = file_fidp;
    xattr_fidp->fid_type = P9_FID_XATTR;
    xattr_fidp->fs.xattr.copied_len = 0;
    xattr_fidp->fs.xattr.len = size;
    xattr_fidp->fs.xattr.flags = flags;
    v9fs_string_init(&xattr_fidp->fs.xattr.name);
    v9fs_string_copy(&xattr_fidp->fs.xattr.name, &name);
    if (size) {
        xattr_fidp->fs.xattr.value = g_malloc(size);
    } else {
        xattr_fidp->fs.xattr.value = NULL;
    }
    err = offset;
    put_fid(s, file_fidp);
out_nofid:
    complete_pdu(s, pdu, err);
    v9fs_string_free(&name);
}

static void v9fs_readlink(void *opaque)
{
    V9fsPDU *pdu = opaque;
    size_t offset = 7;
    V9fsString target;
    int32_t fid;
    int err = 0;
    V9fsFidState *fidp;

    pdu_unmarshal(pdu, offset, "d", &fid);
    fidp = get_fid(pdu->s, fid);
    if (fidp == NULL) {
        err = -ENOENT;
        goto out_nofid;
    }

    v9fs_string_init(&target);
    err = v9fs_co_readlink(pdu->s, &fidp->path, &target);
    if (err < 0) {
        goto out;
    }
    offset += pdu_marshal(pdu, offset, "s", &target);
    err = offset;
    v9fs_string_free(&target);
out:
    put_fid(pdu->s, fidp);
out_nofid:
    complete_pdu(pdu->s, pdu, err);
}

static CoroutineEntry *pdu_co_handlers[] = {
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
    [P9_TRENAMEAT] = v9fs_renameat,
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

static void v9fs_op_not_supp(void *opaque)
{
    V9fsPDU *pdu = opaque;
    complete_pdu(pdu->s, pdu, -EOPNOTSUPP);
}

static void submit_pdu(V9fsState *s, V9fsPDU *pdu)
{
    Coroutine *co;
    CoroutineEntry *handler;

    if (debug_9p_pdu) {
        pprint_pdu(pdu);
    }
    if (pdu->id >= ARRAY_SIZE(pdu_co_handlers) ||
        (pdu_co_handlers[pdu->id] == NULL)) {
        handler = v9fs_op_not_supp;
    } else {
        handler = pdu_co_handlers[pdu->id];
    }
    co = qemu_coroutine_create(handler);
    qemu_coroutine_enter(co, pdu);
}

void handle_9p_output(VirtIODevice *vdev, VirtQueue *vq)
{
    V9fsState *s = (V9fsState *)vdev;
    V9fsPDU *pdu;
    ssize_t len;

    while ((pdu = alloc_pdu(s)) &&
            (len = virtqueue_pop(vq, &pdu->elem)) != 0) {
        uint8_t *ptr;
        pdu->s = s;
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

void virtio_9p_set_fd_limit(void)
{
    struct rlimit rlim;
    if (getrlimit(RLIMIT_NOFILE, &rlim) < 0) {
        fprintf(stderr, "Failed to get the resource limit\n");
        exit(1);
    }
    open_fd_hw = rlim.rlim_cur - MIN(400, rlim.rlim_cur/3);
    open_fd_rc = rlim.rlim_cur/2;
}
