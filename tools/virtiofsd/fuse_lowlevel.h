/*
 * FUSE: Filesystem in Userspace
 * Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
 *
 * This program can be distributed under the terms of the GNU LGPLv2.
 * See the file COPYING.LIB.
 */

#ifndef FUSE_LOWLEVEL_H_
#define FUSE_LOWLEVEL_H_

/**
 * @file
 *
 * Low level API
 *
 * IMPORTANT: you should define FUSE_USE_VERSION before including this
 * header.  To use the newest API define it to 31 (recommended for any
 * new application).
 */

#ifndef FUSE_USE_VERSION
#error FUSE_USE_VERSION not defined
#endif

#include "fuse_common.h"

#include <sys/statvfs.h>
#include <sys/uio.h>
#include <utime.h>

/*
 * Miscellaneous definitions
 */

/** The node ID of the root inode */
#define FUSE_ROOT_ID 1

/** Inode number type */
typedef uint64_t fuse_ino_t;

/** Request pointer type */
typedef struct fuse_req *fuse_req_t;

/**
 * Session
 *
 * This provides hooks for processing requests, and exiting
 */
struct fuse_session;

/** Directory entry parameters supplied to fuse_reply_entry() */
struct fuse_entry_param {
    /**
     * Unique inode number
     *
     * In lookup, zero means negative entry (from version 2.5)
     * Returning ENOENT also means negative entry, but by setting zero
     * ino the kernel may cache negative entries for entry_timeout
     * seconds.
     */
    fuse_ino_t ino;

    /**
     * Generation number for this entry.
     *
     * If the file system will be exported over NFS, the
     * ino/generation pairs need to be unique over the file
     * system's lifetime (rather than just the mount time). So if
     * the file system reuses an inode after it has been deleted,
     * it must assign a new, previously unused generation number
     * to the inode at the same time.
     *
     */
    uint64_t generation;

    /**
     * Inode attributes.
     *
     * Even if attr_timeout == 0, attr must be correct. For example,
     * for open(), FUSE uses attr.st_size from lookup() to determine
     * how many bytes to request. If this value is not correct,
     * incorrect data will be returned.
     */
    struct stat attr;

    /**
     * Validity timeout (in seconds) for inode attributes. If
     *  attributes only change as a result of requests that come
     *  through the kernel, this should be set to a very large
     *  value.
     */
    double attr_timeout;

    /**
     * Validity timeout (in seconds) for the name. If directory
     *  entries are changed/deleted only as a result of requests
     *  that come through the kernel, this should be set to a very
     *  large value.
     */
    double entry_timeout;

    /**
     * Flags for fuse_attr.flags that do not fit into attr.
     */
    uint32_t attr_flags;
};

/**
 * Additional context associated with requests.
 *
 * Note that the reported client uid, gid and pid may be zero in some
 * situations. For example, if the FUSE file system is running in a
 * PID or user namespace but then accessed from outside the namespace,
 * there is no valid uid/pid/gid that could be reported.
 */
struct fuse_ctx {
    /** User ID of the calling process */
    uid_t uid;

    /** Group ID of the calling process */
    gid_t gid;

    /** Thread ID of the calling process */
    pid_t pid;

    /** Umask of the calling process */
    mode_t umask;
};

struct fuse_forget_data {
    fuse_ino_t ino;
    uint64_t nlookup;
};

/* 'to_set' flags in setattr */
#define FUSE_SET_ATTR_MODE (1 << 0)
#define FUSE_SET_ATTR_UID (1 << 1)
#define FUSE_SET_ATTR_GID (1 << 2)
#define FUSE_SET_ATTR_SIZE (1 << 3)
#define FUSE_SET_ATTR_ATIME (1 << 4)
#define FUSE_SET_ATTR_MTIME (1 << 5)
#define FUSE_SET_ATTR_ATIME_NOW (1 << 7)
#define FUSE_SET_ATTR_MTIME_NOW (1 << 8)
#define FUSE_SET_ATTR_CTIME (1 << 10)
#define FUSE_SET_ATTR_KILL_SUIDGID (1 << 11)

/*
 * Request methods and replies
 */

/**
 * Low level filesystem operations
 *
 * Most of the methods (with the exception of init and destroy)
 * receive a request handle (fuse_req_t) as their first argument.
 * This handle must be passed to one of the specified reply functions.
 *
 * This may be done inside the method invocation, or after the call
 * has returned.  The request handle is valid until one of the reply
 * functions is called.
 *
 * Other pointer arguments (name, fuse_file_info, etc) are not valid
 * after the call has returned, so if they are needed later, their
 * contents have to be copied.
 *
 * In general, all methods are expected to perform any necessary
 * permission checking. However, a filesystem may delegate this task
 * to the kernel by passing the `default_permissions` mount option to
 * `fuse_session_new()`. In this case, methods will only be called if
 * the kernel's permission check has succeeded.
 *
 * The filesystem sometimes needs to handle a return value of -ENOENT
 * from the reply function, which means, that the request was
 * interrupted, and the reply discarded.  For example if
 * fuse_reply_open() return -ENOENT means, that the release method for
 * this file will not be called.
 */
struct fuse_lowlevel_ops {
    /**
     * Initialize filesystem
     *
     * This function is called when libfuse establishes
     * communication with the FUSE kernel module. The file system
     * should use this module to inspect and/or modify the
     * connection parameters provided in the `conn` structure.
     *
     * Note that some parameters may be overwritten by options
     * passed to fuse_session_new() which take precedence over the
     * values set in this handler.
     *
     * There's no reply to this function
     *
     * @param userdata the user data passed to fuse_session_new()
     */
    void (*init)(void *userdata, struct fuse_conn_info *conn);

    /**
     * Clean up filesystem.
     *
     * Called on filesystem exit. When this method is called, the
     * connection to the kernel may be gone already, so that eg. calls
     * to fuse_lowlevel_notify_* will fail.
     *
     * There's no reply to this function
     *
     * @param userdata the user data passed to fuse_session_new()
     */
    void (*destroy)(void *userdata);

    /**
     * Look up a directory entry by name and get its attributes.
     *
     * Valid replies:
     *   fuse_reply_entry
     *   fuse_reply_err
     *
     * @param req request handle
     * @param parent inode number of the parent directory
     * @param name the name to look up
     */
    void (*lookup)(fuse_req_t req, fuse_ino_t parent, const char *name);

    /**
     * Forget about an inode
     *
     * This function is called when the kernel removes an inode
     * from its internal caches.
     *
     * The inode's lookup count increases by one for every call to
     * fuse_reply_entry and fuse_reply_create. The nlookup parameter
     * indicates by how much the lookup count should be decreased.
     *
     * Inodes with a non-zero lookup count may receive request from
     * the kernel even after calls to unlink, rmdir or (when
     * overwriting an existing file) rename. Filesystems must handle
     * such requests properly and it is recommended to defer removal
     * of the inode until the lookup count reaches zero. Calls to
     * unlink, rmdir or rename will be followed closely by forget
     * unless the file or directory is open, in which case the
     * kernel issues forget only after the release or releasedir
     * calls.
     *
     * Note that if a file system will be exported over NFS the
     * inodes lifetime must extend even beyond forget. See the
     * generation field in struct fuse_entry_param above.
     *
     * On unmount the lookup count for all inodes implicitly drops
     * to zero. It is not guaranteed that the file system will
     * receive corresponding forget messages for the affected
     * inodes.
     *
     * Valid replies:
     *   fuse_reply_none
     *
     * @param req request handle
     * @param ino the inode number
     * @param nlookup the number of lookups to forget
     */
    void (*forget)(fuse_req_t req, fuse_ino_t ino, uint64_t nlookup);

    /**
     * Get file attributes.
     *
     * If writeback caching is enabled, the kernel may have a
     * better idea of a file's length than the FUSE file system
     * (eg if there has been a write that extended the file size,
     * but that has not yet been passed to the filesystem.n
     *
     * In this case, the st_size value provided by the file system
     * will be ignored.
     *
     * Valid replies:
     *   fuse_reply_attr
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino the inode number
     * @param fi for future use, currently always NULL
     */
    void (*getattr)(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi);

    /**
     * Set file attributes
     *
     * In the 'attr' argument only members indicated by the 'to_set'
     * bitmask contain valid values.  Other members contain undefined
     * values.
     *
     * Unless FUSE_CAP_HANDLE_KILLPRIV is disabled, this method is
     * expected to reset the setuid and setgid bits if the file
     * size or owner is being changed.
     *
     * If the setattr was invoked from the ftruncate() system call
     * under Linux kernel versions 2.6.15 or later, the fi->fh will
     * contain the value set by the open method or will be undefined
     * if the open method didn't set any value.  Otherwise (not
     * ftruncate call, or kernel version earlier than 2.6.15) the fi
     * parameter will be NULL.
     *
     * Valid replies:
     *   fuse_reply_attr
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino the inode number
     * @param attr the attributes
     * @param to_set bit mask of attributes which should be set
     * @param fi file information, or NULL
     */
    void (*setattr)(fuse_req_t req, fuse_ino_t ino, struct stat *attr,
                    int to_set, struct fuse_file_info *fi);

    /**
     * Read symbolic link
     *
     * Valid replies:
     *   fuse_reply_readlink
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino the inode number
     */
    void (*readlink)(fuse_req_t req, fuse_ino_t ino);

    /**
     * Create file node
     *
     * Create a regular file, character device, block device, fifo or
     * socket node.
     *
     * Valid replies:
     *   fuse_reply_entry
     *   fuse_reply_err
     *
     * @param req request handle
     * @param parent inode number of the parent directory
     * @param name to create
     * @param mode file type and mode with which to create the new file
     * @param rdev the device number (only valid if created file is a device)
     */
    void (*mknod)(fuse_req_t req, fuse_ino_t parent, const char *name,
                  mode_t mode, dev_t rdev);

    /**
     * Create a directory
     *
     * Valid replies:
     *   fuse_reply_entry
     *   fuse_reply_err
     *
     * @param req request handle
     * @param parent inode number of the parent directory
     * @param name to create
     * @param mode with which to create the new file
     */
    void (*mkdir)(fuse_req_t req, fuse_ino_t parent, const char *name,
                  mode_t mode);

    /**
     * Remove a file
     *
     * If the file's inode's lookup count is non-zero, the file
     * system is expected to postpone any removal of the inode
     * until the lookup count reaches zero (see description of the
     * forget function).
     *
     * Valid replies:
     *   fuse_reply_err
     *
     * @param req request handle
     * @param parent inode number of the parent directory
     * @param name to remove
     */
    void (*unlink)(fuse_req_t req, fuse_ino_t parent, const char *name);

    /**
     * Remove a directory
     *
     * If the directory's inode's lookup count is non-zero, the
     * file system is expected to postpone any removal of the
     * inode until the lookup count reaches zero (see description
     * of the forget function).
     *
     * Valid replies:
     *   fuse_reply_err
     *
     * @param req request handle
     * @param parent inode number of the parent directory
     * @param name to remove
     */
    void (*rmdir)(fuse_req_t req, fuse_ino_t parent, const char *name);

    /**
     * Create a symbolic link
     *
     * Valid replies:
     *   fuse_reply_entry
     *   fuse_reply_err
     *
     * @param req request handle
     * @param link the contents of the symbolic link
     * @param parent inode number of the parent directory
     * @param name to create
     */
    void (*symlink)(fuse_req_t req, const char *link, fuse_ino_t parent,
                    const char *name);

    /**
     * Rename a file
     *
     * If the target exists it should be atomically replaced. If
     * the target's inode's lookup count is non-zero, the file
     * system is expected to postpone any removal of the inode
     * until the lookup count reaches zero (see description of the
     * forget function).
     *
     * If this request is answered with an error code of ENOSYS, this is
     * treated as a permanent failure with error code EINVAL, i.e. all
     * future bmap requests will fail with EINVAL without being
     * send to the filesystem process.
     *
     * *flags* may be `RENAME_EXCHANGE` or `RENAME_NOREPLACE`. If
     * RENAME_NOREPLACE is specified, the filesystem must not
     * overwrite *newname* if it exists and return an error
     * instead. If `RENAME_EXCHANGE` is specified, the filesystem
     * must atomically exchange the two files, i.e. both must
     * exist and neither may be deleted.
     *
     * Valid replies:
     *   fuse_reply_err
     *
     * @param req request handle
     * @param parent inode number of the old parent directory
     * @param name old name
     * @param newparent inode number of the new parent directory
     * @param newname new name
     */
    void (*rename)(fuse_req_t req, fuse_ino_t parent, const char *name,
                   fuse_ino_t newparent, const char *newname,
                   unsigned int flags);

    /**
     * Create a hard link
     *
     * Valid replies:
     *   fuse_reply_entry
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino the old inode number
     * @param newparent inode number of the new parent directory
     * @param newname new name to create
     */
    void (*link)(fuse_req_t req, fuse_ino_t ino, fuse_ino_t newparent,
                 const char *newname);

    /**
     * Open a file
     *
     * Open flags are available in fi->flags. The following rules
     * apply.
     *
     *  - Creation (O_CREAT, O_EXCL, O_NOCTTY) flags will be
     *    filtered out / handled by the kernel.
     *
     *  - Access modes (O_RDONLY, O_WRONLY, O_RDWR) should be used
     *    by the filesystem to check if the operation is
     *    permitted.  If the ``-o default_permissions`` mount
     *    option is given, this check is already done by the
     *    kernel before calling open() and may thus be omitted by
     *    the filesystem.
     *
     *  - When writeback caching is enabled, the kernel may send
     *    read requests even for files opened with O_WRONLY. The
     *    filesystem should be prepared to handle this.
     *
     *  - When writeback caching is disabled, the filesystem is
     *    expected to properly handle the O_APPEND flag and ensure
     *    that each write is appending to the end of the file.
     *
     *  - When writeback caching is enabled, the kernel will
     *    handle O_APPEND. However, unless all changes to the file
     *    come through the kernel this will not work reliably. The
     *    filesystem should thus either ignore the O_APPEND flag
     *    (and let the kernel handle it), or return an error
     *    (indicating that reliably O_APPEND is not available).
     *
     * Filesystem may store an arbitrary file handle (pointer,
     * index, etc) in fi->fh, and use this in other all other file
     * operations (read, write, flush, release, fsync).
     *
     * Filesystem may also implement stateless file I/O and not store
     * anything in fi->fh.
     *
     * There are also some flags (direct_io, keep_cache) which the
     * filesystem may set in fi, to change the way the file is opened.
     * See fuse_file_info structure in <fuse_common.h> for more details.
     *
     * If this request is answered with an error code of ENOSYS
     * and FUSE_CAP_NO_OPEN_SUPPORT is set in
     * `fuse_conn_info.capable`, this is treated as success and
     * future calls to open and release will also succeed without being
     * sent to the filesystem process.
     *
     * Valid replies:
     *   fuse_reply_open
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino the inode number
     * @param fi file information
     */
    void (*open)(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi);

    /**
     * Read data
     *
     * Read should send exactly the number of bytes requested except
     * on EOF or error, otherwise the rest of the data will be
     * substituted with zeroes.  An exception to this is when the file
     * has been opened in 'direct_io' mode, in which case the return
     * value of the read system call will reflect the return value of
     * this operation.
     *
     * fi->fh will contain the value set by the open method, or will
     * be undefined if the open method didn't set any value.
     *
     * Valid replies:
     *   fuse_reply_buf
     *   fuse_reply_iov
     *   fuse_reply_data
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino the inode number
     * @param size number of bytes to read
     * @param off offset to read from
     * @param fi file information
     */
    void (*read)(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                 struct fuse_file_info *fi);

    /**
     * Write data
     *
     * Write should return exactly the number of bytes requested
     * except on error.  An exception to this is when the file has
     * been opened in 'direct_io' mode, in which case the return value
     * of the write system call will reflect the return value of this
     * operation.
     *
     * Unless FUSE_CAP_HANDLE_KILLPRIV is disabled, this method is
     * expected to reset the setuid and setgid bits.
     *
     * fi->fh will contain the value set by the open method, or will
     * be undefined if the open method didn't set any value.
     *
     * Valid replies:
     *   fuse_reply_write
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino the inode number
     * @param buf data to write
     * @param size number of bytes to write
     * @param off offset to write to
     * @param fi file information
     */
    void (*write)(fuse_req_t req, fuse_ino_t ino, const char *buf, size_t size,
                  off_t off, struct fuse_file_info *fi);

    /**
     * Flush method
     *
     * This is called on each close() of the opened file.
     *
     * Since file descriptors can be duplicated (dup, dup2, fork), for
     * one open call there may be many flush calls.
     *
     * Filesystems shouldn't assume that flush will always be called
     * after some writes, or that if will be called at all.
     *
     * fi->fh will contain the value set by the open method, or will
     * be undefined if the open method didn't set any value.
     *
     * NOTE: the name of the method is misleading, since (unlike
     * fsync) the filesystem is not forced to flush pending writes.
     * One reason to flush data is if the filesystem wants to return
     * write errors during close.  However, such use is non-portable
     * because POSIX does not require [close] to wait for delayed I/O to
     * complete.
     *
     * If the filesystem supports file locking operations (setlk,
     * getlk) it should remove all locks belonging to 'fi->owner'.
     *
     * If this request is answered with an error code of ENOSYS,
     * this is treated as success and future calls to flush() will
     * succeed automatically without being send to the filesystem
     * process.
     *
     * Valid replies:
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino the inode number
     * @param fi file information
     *
     * [close]:
     * http://pubs.opengroup.org/onlinepubs/9699919799/functions/close.html
     */
    void (*flush)(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi);

    /**
     * Release an open file
     *
     * Release is called when there are no more references to an open
     * file: all file descriptors are closed and all memory mappings
     * are unmapped.
     *
     * For every open call there will be exactly one release call (unless
     * the filesystem is force-unmounted).
     *
     * The filesystem may reply with an error, but error values are
     * not returned to close() or munmap() which triggered the
     * release.
     *
     * fi->fh will contain the value set by the open method, or will
     * be undefined if the open method didn't set any value.
     * fi->flags will contain the same flags as for open.
     *
     * Valid replies:
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino the inode number
     * @param fi file information
     */
    void (*release)(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi);

    /**
     * Synchronize file contents
     *
     * If the datasync parameter is non-zero, then only the user data
     * should be flushed, not the meta data.
     *
     * If this request is answered with an error code of ENOSYS,
     * this is treated as success and future calls to fsync() will
     * succeed automatically without being send to the filesystem
     * process.
     *
     * Valid replies:
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino the inode number
     * @param datasync flag indicating if only data should be flushed
     * @param fi file information
     */
    void (*fsync)(fuse_req_t req, fuse_ino_t ino, int datasync,
                  struct fuse_file_info *fi);

    /**
     * Open a directory
     *
     * Filesystem may store an arbitrary file handle (pointer, index,
     * etc) in fi->fh, and use this in other all other directory
     * stream operations (readdir, releasedir, fsyncdir).
     *
     * If this request is answered with an error code of ENOSYS and
     * FUSE_CAP_NO_OPENDIR_SUPPORT is set in `fuse_conn_info.capable`,
     * this is treated as success and future calls to opendir and
     * releasedir will also succeed without being sent to the filesystem
     * process. In addition, the kernel will cache readdir results
     * as if opendir returned FOPEN_KEEP_CACHE | FOPEN_CACHE_DIR.
     *
     * Valid replies:
     *   fuse_reply_open
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino the inode number
     * @param fi file information
     */
    void (*opendir)(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi);

    /**
     * Read directory
     *
     * Send a buffer filled using fuse_add_direntry(), with size not
     * exceeding the requested size.  Send an empty buffer on end of
     * stream.
     *
     * fi->fh will contain the value set by the opendir method, or
     * will be undefined if the opendir method didn't set any value.
     *
     * Returning a directory entry from readdir() does not affect
     * its lookup count.
     *
     * If off_t is non-zero, then it will correspond to one of the off_t
     * values that was previously returned by readdir() for the same
     * directory handle. In this case, readdir() should skip over entries
     * coming before the position defined by the off_t value. If entries
     * are added or removed while the directory handle is open, they filesystem
     * may still include the entries that have been removed, and may not
     * report the entries that have been created. However, addition or
     * removal of entries must never cause readdir() to skip over unrelated
     * entries or to report them more than once. This means
     * that off_t can not be a simple index that enumerates the entries
     * that have been returned but must contain sufficient information to
     * uniquely determine the next directory entry to return even when the
     * set of entries is changing.
     *
     * The function does not have to report the '.' and '..'
     * entries, but is allowed to do so. Note that, if readdir does
     * not return '.' or '..', they will not be implicitly returned,
     * and this behavior is observable by the caller.
     *
     * Valid replies:
     *   fuse_reply_buf
     *   fuse_reply_data
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino the inode number
     * @param size maximum number of bytes to send
     * @param off offset to continue reading the directory stream
     * @param fi file information
     */
    void (*readdir)(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                    struct fuse_file_info *fi);

    /**
     * Release an open directory
     *
     * For every opendir call there will be exactly one releasedir
     * call (unless the filesystem is force-unmounted).
     *
     * fi->fh will contain the value set by the opendir method, or
     * will be undefined if the opendir method didn't set any value.
     *
     * Valid replies:
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino the inode number
     * @param fi file information
     */
    void (*releasedir)(fuse_req_t req, fuse_ino_t ino,
                       struct fuse_file_info *fi);

    /**
     * Synchronize directory contents
     *
     * If the datasync parameter is non-zero, then only the directory
     * contents should be flushed, not the meta data.
     *
     * fi->fh will contain the value set by the opendir method, or
     * will be undefined if the opendir method didn't set any value.
     *
     * If this request is answered with an error code of ENOSYS,
     * this is treated as success and future calls to fsyncdir() will
     * succeed automatically without being send to the filesystem
     * process.
     *
     * Valid replies:
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino the inode number
     * @param datasync flag indicating if only data should be flushed
     * @param fi file information
     */
    void (*fsyncdir)(fuse_req_t req, fuse_ino_t ino, int datasync,
                     struct fuse_file_info *fi);

    /**
     * Get file system statistics
     *
     * Valid replies:
     *   fuse_reply_statfs
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino the inode number, zero means "undefined"
     */
    void (*statfs)(fuse_req_t req, fuse_ino_t ino);

    /**
     * Set an extended attribute
     *
     * If this request is answered with an error code of ENOSYS, this is
     * treated as a permanent failure with error code EOPNOTSUPP, i.e. all
     * future setxattr() requests will fail with EOPNOTSUPP without being
     * send to the filesystem process.
     *
     * Valid replies:
     *   fuse_reply_err
     */
    void (*setxattr)(fuse_req_t req, fuse_ino_t ino, const char *name,
                     const char *value, size_t size, int flags,
                     uint32_t setxattr_flags);

    /**
     * Get an extended attribute
     *
     * If size is zero, the size of the value should be sent with
     * fuse_reply_xattr.
     *
     * If the size is non-zero, and the value fits in the buffer, the
     * value should be sent with fuse_reply_buf.
     *
     * If the size is too small for the value, the ERANGE error should
     * be sent.
     *
     * If this request is answered with an error code of ENOSYS, this is
     * treated as a permanent failure with error code EOPNOTSUPP, i.e. all
     * future getxattr() requests will fail with EOPNOTSUPP without being
     * send to the filesystem process.
     *
     * Valid replies:
     *   fuse_reply_buf
     *   fuse_reply_data
     *   fuse_reply_xattr
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino the inode number
     * @param name of the extended attribute
     * @param size maximum size of the value to send
     */
    void (*getxattr)(fuse_req_t req, fuse_ino_t ino, const char *name,
                     size_t size);

    /**
     * List extended attribute names
     *
     * If size is zero, the total size of the attribute list should be
     * sent with fuse_reply_xattr.
     *
     * If the size is non-zero, and the null character separated
     * attribute list fits in the buffer, the list should be sent with
     * fuse_reply_buf.
     *
     * If the size is too small for the list, the ERANGE error should
     * be sent.
     *
     * If this request is answered with an error code of ENOSYS, this is
     * treated as a permanent failure with error code EOPNOTSUPP, i.e. all
     * future listxattr() requests will fail with EOPNOTSUPP without being
     * send to the filesystem process.
     *
     * Valid replies:
     *   fuse_reply_buf
     *   fuse_reply_data
     *   fuse_reply_xattr
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino the inode number
     * @param size maximum size of the list to send
     */
    void (*listxattr)(fuse_req_t req, fuse_ino_t ino, size_t size);

    /**
     * Remove an extended attribute
     *
     * If this request is answered with an error code of ENOSYS, this is
     * treated as a permanent failure with error code EOPNOTSUPP, i.e. all
     * future removexattr() requests will fail with EOPNOTSUPP without being
     * send to the filesystem process.
     *
     * Valid replies:
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino the inode number
     * @param name of the extended attribute
     */
    void (*removexattr)(fuse_req_t req, fuse_ino_t ino, const char *name);

    /**
     * Check file access permissions
     *
     * This will be called for the access() and chdir() system
     * calls.  If the 'default_permissions' mount option is given,
     * this method is not called.
     *
     * This method is not called under Linux kernel versions 2.4.x
     *
     * If this request is answered with an error code of ENOSYS, this is
     * treated as a permanent success, i.e. this and all future access()
     * requests will succeed without being send to the filesystem process.
     *
     * Valid replies:
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino the inode number
     * @param mask requested access mode
     */
    void (*access)(fuse_req_t req, fuse_ino_t ino, int mask);

    /**
     * Create and open a file
     *
     * If the file does not exist, first create it with the specified
     * mode, and then open it.
     *
     * See the description of the open handler for more
     * information.
     *
     * If this method is not implemented or under Linux kernel
     * versions earlier than 2.6.15, the mknod() and open() methods
     * will be called instead.
     *
     * If this request is answered with an error code of ENOSYS, the handler
     * is treated as not implemented (i.e., for this and future requests the
     * mknod() and open() handlers will be called instead).
     *
     * Valid replies:
     *   fuse_reply_create
     *   fuse_reply_err
     *
     * @param req request handle
     * @param parent inode number of the parent directory
     * @param name to create
     * @param mode file type and mode with which to create the new file
     * @param fi file information
     */
    void (*create)(fuse_req_t req, fuse_ino_t parent, const char *name,
                   mode_t mode, struct fuse_file_info *fi);

    /**
     * Test for a POSIX file lock
     *
     * Valid replies:
     *   fuse_reply_lock
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino the inode number
     * @param fi file information
     * @param lock the region/type to test
     */
    void (*getlk)(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi,
                  struct flock *lock);

    /**
     * Acquire, modify or release a POSIX file lock
     *
     * For POSIX threads (NPTL) there's a 1-1 relation between pid and
     * owner, but otherwise this is not always the case.  For checking
     * lock ownership, 'fi->owner' must be used.  The l_pid field in
     * 'struct flock' should only be used to fill in this field in
     * getlk().
     *
     * Note: if the locking methods are not implemented, the kernel
     * will still allow file locking to work locally.  Hence these are
     * only interesting for network filesystems and similar.
     *
     * Valid replies:
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino the inode number
     * @param fi file information
     * @param lock the region/type to set
     * @param sleep locking operation may sleep
     */
    void (*setlk)(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi,
                  struct flock *lock, int sleep);

    /**
     * Map block index within file to block index within device
     *
     * Note: This makes sense only for block device backed filesystems
     * mounted with the 'blkdev' option
     *
     * If this request is answered with an error code of ENOSYS, this is
     * treated as a permanent failure, i.e. all future bmap() requests will
     * fail with the same error code without being send to the filesystem
     * process.
     *
     * Valid replies:
     *   fuse_reply_bmap
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino the inode number
     * @param blocksize unit of block index
     * @param idx block index within file
     */
    void (*bmap)(fuse_req_t req, fuse_ino_t ino, size_t blocksize,
                 uint64_t idx);

    /**
     * Ioctl
     *
     * Note: For unrestricted ioctls (not allowed for FUSE
     * servers), data in and out areas can be discovered by giving
     * iovs and setting FUSE_IOCTL_RETRY in *flags*.  For
     * restricted ioctls, kernel prepares in/out data area
     * according to the information encoded in cmd.
     *
     * Valid replies:
     *   fuse_reply_ioctl_retry
     *   fuse_reply_ioctl
     *   fuse_reply_ioctl_iov
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino the inode number
     * @param cmd ioctl command
     * @param arg ioctl argument
     * @param fi file information
     * @param flags for FUSE_IOCTL_* flags
     * @param in_buf data fetched from the caller
     * @param in_bufsz number of fetched bytes
     * @param out_bufsz maximum size of output data
     *
     * Note : the unsigned long request submitted by the application
     * is truncated to 32 bits.
     */
    void (*ioctl)(fuse_req_t req, fuse_ino_t ino, unsigned int cmd, void *arg,
                  struct fuse_file_info *fi, unsigned flags, const void *in_buf,
                  size_t in_bufsz, size_t out_bufsz);

    /**
     * Poll for IO readiness
     *
     * Note: If ph is non-NULL, the client should notify
     * when IO readiness events occur by calling
     * fuse_lowlevel_notify_poll() with the specified ph.
     *
     * Regardless of the number of times poll with a non-NULL ph
     * is received, single notification is enough to clear all.
     * Notifying more times incurs overhead but doesn't harm
     * correctness.
     *
     * The callee is responsible for destroying ph with
     * fuse_pollhandle_destroy() when no longer in use.
     *
     * If this request is answered with an error code of ENOSYS, this is
     * treated as success (with a kernel-defined default poll-mask) and
     * future calls to pull() will succeed the same way without being send
     * to the filesystem process.
     *
     * Valid replies:
     *   fuse_reply_poll
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino the inode number
     * @param fi file information
     * @param ph poll handle to be used for notification
     */
    void (*poll)(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi,
                 struct fuse_pollhandle *ph);

    /**
     * Write data made available in a buffer
     *
     * This is a more generic version of the ->write() method.  If
     * FUSE_CAP_SPLICE_READ is set in fuse_conn_info.want and the
     * kernel supports splicing from the fuse device, then the
     * data will be made available in pipe for supporting zero
     * copy data transfer.
     *
     * buf->count is guaranteed to be one (and thus buf->idx is
     * always zero). The write_buf handler must ensure that
     * bufv->off is correctly updated (reflecting the number of
     * bytes read from bufv->buf[0]).
     *
     * Unless FUSE_CAP_HANDLE_KILLPRIV is disabled, this method is
     * expected to reset the setuid and setgid bits.
     *
     * Valid replies:
     *   fuse_reply_write
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino the inode number
     * @param bufv buffer containing the data
     * @param off offset to write to
     * @param fi file information
     */
    void (*write_buf)(fuse_req_t req, fuse_ino_t ino, struct fuse_bufvec *bufv,
                      off_t off, struct fuse_file_info *fi);

    /**
     * Forget about multiple inodes
     *
     * See description of the forget function for more
     * information.
     *
     * Valid replies:
     *   fuse_reply_none
     *
     * @param req request handle
     */
    void (*forget_multi)(fuse_req_t req, size_t count,
                         struct fuse_forget_data *forgets);

    /**
     * Acquire, modify or release a BSD file lock
     *
     * Note: if the locking methods are not implemented, the kernel
     * will still allow file locking to work locally.  Hence these are
     * only interesting for network filesystems and similar.
     *
     * Valid replies:
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino the inode number
     * @param fi file information
     * @param op the locking operation, see flock(2)
     */
    void (*flock)(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi,
                  int op);

    /**
     * Allocate requested space. If this function returns success then
     * subsequent writes to the specified range shall not fail due to the lack
     * of free space on the file system storage media.
     *
     * If this request is answered with an error code of ENOSYS, this is
     * treated as a permanent failure with error code EOPNOTSUPP, i.e. all
     * future fallocate() requests will fail with EOPNOTSUPP without being
     * send to the filesystem process.
     *
     * Valid replies:
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino the inode number
     * @param offset starting point for allocated region
     * @param length size of allocated region
     * @param mode determines the operation to be performed on the given range,
     *             see fallocate(2)
     */
    void (*fallocate)(fuse_req_t req, fuse_ino_t ino, int mode, off_t offset,
                      off_t length, struct fuse_file_info *fi);

    /**
     * Read directory with attributes
     *
     * Send a buffer filled using fuse_add_direntry_plus(), with size not
     * exceeding the requested size.  Send an empty buffer on end of
     * stream.
     *
     * fi->fh will contain the value set by the opendir method, or
     * will be undefined if the opendir method didn't set any value.
     *
     * In contrast to readdir() (which does not affect the lookup counts),
     * the lookup count of every entry returned by readdirplus(), except "."
     * and "..", is incremented by one.
     *
     * Valid replies:
     *   fuse_reply_buf
     *   fuse_reply_data
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino the inode number
     * @param size maximum number of bytes to send
     * @param off offset to continue reading the directory stream
     * @param fi file information
     */
    void (*readdirplus)(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                        struct fuse_file_info *fi);

    /**
     * Copy a range of data from one file to another
     *
     * Performs an optimized copy between two file descriptors without the
     * additional cost of transferring data through the FUSE kernel module
     * to user space (glibc) and then back into the FUSE filesystem again.
     *
     * In case this method is not implemented, glibc falls back to reading
     * data from the source and writing to the destination. Effectively
     * doing an inefficient copy of the data.
     *
     * If this request is answered with an error code of ENOSYS, this is
     * treated as a permanent failure with error code EOPNOTSUPP, i.e. all
     * future copy_file_range() requests will fail with EOPNOTSUPP without
     * being send to the filesystem process.
     *
     * Valid replies:
     *   fuse_reply_write
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino_in the inode number or the source file
     * @param off_in starting point from were the data should be read
     * @param fi_in file information of the source file
     * @param ino_out the inode number or the destination file
     * @param off_out starting point where the data should be written
     * @param fi_out file information of the destination file
     * @param len maximum size of the data to copy
     * @param flags passed along with the copy_file_range() syscall
     */
    void (*copy_file_range)(fuse_req_t req, fuse_ino_t ino_in, off_t off_in,
                            struct fuse_file_info *fi_in, fuse_ino_t ino_out,
                            off_t off_out, struct fuse_file_info *fi_out,
                            size_t len, int flags);

    /**
     * Find next data or hole after the specified offset
     *
     * If this request is answered with an error code of ENOSYS, this is
     * treated as a permanent failure, i.e. all future lseek() requests will
     * fail with the same error code without being send to the filesystem
     * process.
     *
     * Valid replies:
     *   fuse_reply_lseek
     *   fuse_reply_err
     *
     * @param req request handle
     * @param ino the inode number
     * @param off offset to start search from
     * @param whence either SEEK_DATA or SEEK_HOLE
     * @param fi file information
     */
    void (*lseek)(fuse_req_t req, fuse_ino_t ino, off_t off, int whence,
                  struct fuse_file_info *fi);
};

/**
 * Reply with an error code or success.
 *
 * Possible requests:
 *   all except forget
 *
 * Whereever possible, error codes should be chosen from the list of
 * documented error conditions in the corresponding system calls
 * manpage.
 *
 * An error code of ENOSYS is sometimes treated specially. This is
 * indicated in the documentation of the affected handler functions.
 *
 * The following requests may be answered with a zero error code:
 * unlink, rmdir, rename, flush, release, fsync, fsyncdir, setxattr,
 * removexattr, setlk.
 *
 * @param req request handle
 * @param err the positive error value, or zero for success
 * @return zero for success, -errno for failure to send reply
 */
int fuse_reply_err(fuse_req_t req, int err);

/**
 * Don't send reply
 *
 * Possible requests:
 *   forget
 *   forget_multi
 *   retrieve_reply
 *
 * @param req request handle
 */
void fuse_reply_none(fuse_req_t req);

/**
 * Reply with a directory entry
 *
 * Possible requests:
 *   lookup, mknod, mkdir, symlink, link
 *
 * Side effects:
 *   increments the lookup count on success
 *
 * @param req request handle
 * @param e the entry parameters
 * @return zero for success, -errno for failure to send reply
 */
int fuse_reply_entry(fuse_req_t req, const struct fuse_entry_param *e);

/**
 * Reply with a directory entry and open parameters
 *
 * currently the following members of 'fi' are used:
 *   fh, direct_io, keep_cache
 *
 * Possible requests:
 *   create
 *
 * Side effects:
 *   increments the lookup count on success
 *
 * @param req request handle
 * @param e the entry parameters
 * @param fi file information
 * @return zero for success, -errno for failure to send reply
 */
int fuse_reply_create(fuse_req_t req, const struct fuse_entry_param *e,
                      const struct fuse_file_info *fi);

/**
 * Reply with attributes
 *
 * Possible requests:
 *   getattr, setattr
 *
 * @param req request handle
 * @param attr the attributes
 * @param attr_timeout validity timeout (in seconds) for the attributes
 * @return zero for success, -errno for failure to send reply
 */
int fuse_reply_attr(fuse_req_t req, const struct stat *attr,
                    double attr_timeout);

/**
 * Reply with the contents of a symbolic link
 *
 * Possible requests:
 *   readlink
 *
 * @param req request handle
 * @param link symbolic link contents
 * @return zero for success, -errno for failure to send reply
 */
int fuse_reply_readlink(fuse_req_t req, const char *link);

/**
 * Reply with open parameters
 *
 * currently the following members of 'fi' are used:
 *   fh, direct_io, keep_cache
 *
 * Possible requests:
 *   open, opendir
 *
 * @param req request handle
 * @param fi file information
 * @return zero for success, -errno for failure to send reply
 */
int fuse_reply_open(fuse_req_t req, const struct fuse_file_info *fi);

/**
 * Reply with number of bytes written
 *
 * Possible requests:
 *   write
 *
 * @param req request handle
 * @param count the number of bytes written
 * @return zero for success, -errno for failure to send reply
 */
int fuse_reply_write(fuse_req_t req, size_t count);

/**
 * Reply with data
 *
 * Possible requests:
 *   read, readdir, getxattr, listxattr
 *
 * @param req request handle
 * @param buf buffer containing data
 * @param size the size of data in bytes
 * @return zero for success, -errno for failure to send reply
 */
int fuse_reply_buf(fuse_req_t req, const char *buf, size_t size);

/**
 * Reply with data copied/moved from buffer(s)
 *
 * Possible requests:
 *   read, readdir, getxattr, listxattr
 *
 * Side effects:
 *   when used to return data from a readdirplus() (but not readdir())
 *   call, increments the lookup count of each returned entry by one
 *   on success.
 *
 * @param req request handle
 * @param bufv buffer vector
 * @return zero for success, -errno for failure to send reply
 */
int fuse_reply_data(fuse_req_t req, struct fuse_bufvec *bufv);

/**
 * Reply with data vector
 *
 * Possible requests:
 *   read, readdir, getxattr, listxattr
 *
 * @param req request handle
 * @param iov the vector containing the data
 * @param count the size of vector
 * @return zero for success, -errno for failure to send reply
 */
int fuse_reply_iov(fuse_req_t req, const struct iovec *iov, int count);

/**
 * Reply with filesystem statistics
 *
 * Possible requests:
 *   statfs
 *
 * @param req request handle
 * @param stbuf filesystem statistics
 * @return zero for success, -errno for failure to send reply
 */
int fuse_reply_statfs(fuse_req_t req, const struct statvfs *stbuf);

/**
 * Reply with needed buffer size
 *
 * Possible requests:
 *   getxattr, listxattr
 *
 * @param req request handle
 * @param count the buffer size needed in bytes
 * @return zero for success, -errno for failure to send reply
 */
int fuse_reply_xattr(fuse_req_t req, size_t count);

/**
 * Reply with file lock information
 *
 * Possible requests:
 *   getlk
 *
 * @param req request handle
 * @param lock the lock information
 * @return zero for success, -errno for failure to send reply
 */
int fuse_reply_lock(fuse_req_t req, const struct flock *lock);

/**
 * Reply with block index
 *
 * Possible requests:
 *   bmap
 *
 * @param req request handle
 * @param idx block index within device
 * @return zero for success, -errno for failure to send reply
 */
int fuse_reply_bmap(fuse_req_t req, uint64_t idx);

/*
 * Filling a buffer in readdir
 */

/**
 * Add a directory entry to the buffer
 *
 * Buffer needs to be large enough to hold the entry.  If it's not,
 * then the entry is not filled in but the size of the entry is still
 * returned.  The caller can check this by comparing the bufsize
 * parameter with the returned entry size.  If the entry size is
 * larger than the buffer size, the operation failed.
 *
 * From the 'stbuf' argument the st_ino field and bits 12-15 of the
 * st_mode field are used.  The other fields are ignored.
 *
 * *off* should be any non-zero value that the filesystem can use to
 * identify the current point in the directory stream. It does not
 * need to be the actual physical position. A value of zero is
 * reserved to mean "from the beginning", and should therefore never
 * be used (the first call to fuse_add_direntry should be passed the
 * offset of the second directory entry).
 *
 * @param req request handle
 * @param buf the point where the new entry will be added to the buffer
 * @param bufsize remaining size of the buffer
 * @param name the name of the entry
 * @param stbuf the file attributes
 * @param off the offset of the next entry
 * @return the space needed for the entry
 */
size_t fuse_add_direntry(fuse_req_t req, char *buf, size_t bufsize,
                         const char *name, const struct stat *stbuf, off_t off);

/**
 * Add a directory entry to the buffer with the attributes
 *
 * See documentation of `fuse_add_direntry()` for more details.
 *
 * @param req request handle
 * @param buf the point where the new entry will be added to the buffer
 * @param bufsize remaining size of the buffer
 * @param name the name of the entry
 * @param e the directory entry
 * @param off the offset of the next entry
 * @return the space needed for the entry
 */
size_t fuse_add_direntry_plus(fuse_req_t req, char *buf, size_t bufsize,
                              const char *name,
                              const struct fuse_entry_param *e, off_t off);

/**
 * Reply to ask for data fetch and output buffer preparation.  ioctl
 * will be retried with the specified input data fetched and output
 * buffer prepared.
 *
 * Possible requests:
 *   ioctl
 *
 * @param req request handle
 * @param in_iov iovec specifying data to fetch from the caller
 * @param in_count number of entries in in_iov
 * @param out_iov iovec specifying addresses to write output to
 * @param out_count number of entries in out_iov
 * @return zero for success, -errno for failure to send reply
 */
int fuse_reply_ioctl_retry(fuse_req_t req, const struct iovec *in_iov,
                           size_t in_count, const struct iovec *out_iov,
                           size_t out_count);

/**
 * Reply to finish ioctl
 *
 * Possible requests:
 *   ioctl
 *
 * @param req request handle
 * @param result result to be passed to the caller
 * @param buf buffer containing output data
 * @param size length of output data
 */
int fuse_reply_ioctl(fuse_req_t req, int result, const void *buf, size_t size);

/**
 * Reply to finish ioctl with iov buffer
 *
 * Possible requests:
 *   ioctl
 *
 * @param req request handle
 * @param result result to be passed to the caller
 * @param iov the vector containing the data
 * @param count the size of vector
 */
int fuse_reply_ioctl_iov(fuse_req_t req, int result, const struct iovec *iov,
                         int count);

/**
 * Reply with poll result event mask
 *
 * @param req request handle
 * @param revents poll result event mask
 */
int fuse_reply_poll(fuse_req_t req, unsigned revents);

/**
 * Reply with offset
 *
 * Possible requests:
 *   lseek
 *
 * @param req request handle
 * @param off offset of next data or hole
 * @return zero for success, -errno for failure to send reply
 */
int fuse_reply_lseek(fuse_req_t req, off_t off);

/*
 * Notification
 */

/**
 * Notify IO readiness event
 *
 * For more information, please read comment for poll operation.
 *
 * @param ph poll handle to notify IO readiness event for
 */
int fuse_lowlevel_notify_poll(struct fuse_pollhandle *ph);

/**
 * Notify to invalidate cache for an inode.
 *
 * Added in FUSE protocol version 7.12. If the kernel does not support
 * this (or a newer) version, the function will return -ENOSYS and do
 * nothing.
 *
 * If the filesystem has writeback caching enabled, invalidating an
 * inode will first trigger a writeback of all dirty pages. The call
 * will block until all writeback requests have completed and the
 * inode has been invalidated. It will, however, not wait for
 * completion of pending writeback requests that have been issued
 * before.
 *
 * If there are no dirty pages, this function will never block.
 *
 * @param se the session object
 * @param ino the inode number
 * @param off the offset in the inode where to start invalidating
 *            or negative to invalidate attributes only
 * @param len the amount of cache to invalidate or 0 for all
 * @return zero for success, -errno for failure
 */
int fuse_lowlevel_notify_inval_inode(struct fuse_session *se, fuse_ino_t ino,
                                     off_t off, off_t len);

/**
 * Notify to invalidate parent attributes and the dentry matching
 * parent/name
 *
 * To avoid a deadlock this function must not be called in the
 * execution path of a related filesystem operation or within any code
 * that could hold a lock that could be needed to execute such an
 * operation. As of kernel 4.18, a "related operation" is a lookup(),
 * symlink(), mknod(), mkdir(), unlink(), rename(), link() or create()
 * request for the parent, and a setattr(), unlink(), rmdir(),
 * rename(), setxattr(), removexattr(), readdir() or readdirplus()
 * request for the inode itself.
 *
 * When called correctly, this function will never block.
 *
 * Added in FUSE protocol version 7.12. If the kernel does not support
 * this (or a newer) version, the function will return -ENOSYS and do
 * nothing.
 *
 * @param se the session object
 * @param parent inode number
 * @param name file name
 * @param namelen strlen() of file name
 * @return zero for success, -errno for failure
 */
int fuse_lowlevel_notify_inval_entry(struct fuse_session *se, fuse_ino_t parent,
                                     const char *name, size_t namelen);

/**
 * This function behaves like fuse_lowlevel_notify_inval_entry() with
 * the following additional effect (at least as of Linux kernel 4.8):
 *
 * If the provided *child* inode matches the inode that is currently
 * associated with the cached dentry, and if there are any inotify
 * watches registered for the dentry, then the watchers are informed
 * that the dentry has been deleted.
 *
 * To avoid a deadlock this function must not be called while
 * executing a related filesystem operation or while holding a lock
 * that could be needed to execute such an operation (see the
 * description of fuse_lowlevel_notify_inval_entry() for more
 * details).
 *
 * When called correctly, this function will never block.
 *
 * Added in FUSE protocol version 7.18. If the kernel does not support
 * this (or a newer) version, the function will return -ENOSYS and do
 * nothing.
 *
 * @param se the session object
 * @param parent inode number
 * @param child inode number
 * @param name file name
 * @param namelen strlen() of file name
 * @return zero for success, -errno for failure
 */
int fuse_lowlevel_notify_delete(struct fuse_session *se, fuse_ino_t parent,
                                fuse_ino_t child, const char *name,
                                size_t namelen);

/**
 * Store data to the kernel buffers
 *
 * Synchronously store data in the kernel buffers belonging to the
 * given inode.  The stored data is marked up-to-date (no read will be
 * performed against it, unless it's invalidated or evicted from the
 * cache).
 *
 * If the stored data overflows the current file size, then the size
 * is extended, similarly to a write(2) on the filesystem.
 *
 * If this function returns an error, then the store wasn't fully
 * completed, but it may have been partially completed.
 *
 * Added in FUSE protocol version 7.15. If the kernel does not support
 * this (or a newer) version, the function will return -ENOSYS and do
 * nothing.
 *
 * @param se the session object
 * @param ino the inode number
 * @param offset the starting offset into the file to store to
 * @param bufv buffer vector
 * @return zero for success, -errno for failure
 */
int fuse_lowlevel_notify_store(struct fuse_session *se, fuse_ino_t ino,
                               off_t offset, struct fuse_bufvec *bufv);

/*
 * Utility functions
 */

/**
 * Get the userdata from the request
 *
 * @param req request handle
 * @return the user data passed to fuse_session_new()
 */
void *fuse_req_userdata(fuse_req_t req);

/**
 * Get the context from the request
 *
 * The pointer returned by this function will only be valid for the
 * request's lifetime
 *
 * @param req request handle
 * @return the context structure
 */
const struct fuse_ctx *fuse_req_ctx(fuse_req_t req);

/**
 * Callback function for an interrupt
 *
 * @param req interrupted request
 * @param data user data
 */
typedef void (*fuse_interrupt_func_t)(fuse_req_t req, void *data);

/**
 * Register/unregister callback for an interrupt
 *
 * If an interrupt has already happened, then the callback function is
 * called from within this function, hence it's not possible for
 * interrupts to be lost.
 *
 * @param req request handle
 * @param func the callback function or NULL for unregister
 * @param data user data passed to the callback function
 */
void fuse_req_interrupt_func(fuse_req_t req, fuse_interrupt_func_t func,
                             void *data);

/**
 * Check if a request has already been interrupted
 *
 * @param req request handle
 * @return 1 if the request has been interrupted, 0 otherwise
 */
int fuse_req_interrupted(fuse_req_t req);

/**
 * Check if the session is connected via virtio
 *
 * @param se session object
 * @return 1 if the session is a virtio session
 */
int fuse_lowlevel_is_virtio(struct fuse_session *se);

/*
 * Inquiry functions
 */

/**
 * Print low-level version information to stdout.
 */
void fuse_lowlevel_version(void);

/**
 * Print available low-level options to stdout. This is not an
 * exhaustive list, but includes only those options that may be of
 * interest to an end-user of a file system.
 */
void fuse_lowlevel_help(void);

/**
 * Print available options for `fuse_parse_cmdline()`.
 */
void fuse_cmdline_help(void);

/*
 * Filesystem setup & teardown
 */

struct fuse_cmdline_opts {
    int foreground;
    int debug;
    int nodefault_subtype;
    int show_version;
    int show_help;
    int print_capabilities;
    int syslog;
    int log_level;
    unsigned int max_idle_threads;
    unsigned long rlimit_nofile;
};

/**
 * Utility function to parse common options for simple file systems
 * using the low-level API. A help text that describes the available
 * options can be printed with `fuse_cmdline_help`. A single
 * non-option argument is treated as the mountpoint. Multiple
 * non-option arguments will result in an error.
 *
 * If neither -o subtype= or -o fsname= options are given, a new
 * subtype option will be added and set to the basename of the program
 * (the fsname will remain unset, and then defaults to "fuse").
 *
 * Known options will be removed from *args*, unknown options will
 * remain.
 *
 * @param args argument vector (input+output)
 * @param opts output argument for parsed options
 * @return 0 on success, -1 on failure
 */
int fuse_parse_cmdline(struct fuse_args *args, struct fuse_cmdline_opts *opts);

/**
 * Create a low level session.
 *
 * Returns a session structure suitable for passing to
 * fuse_session_mount() and fuse_session_loop().
 *
 * This function accepts most file-system independent mount options
 * (like context, nodev, ro - see mount(8)), as well as the general
 * fuse mount options listed in mount.fuse(8) (e.g. -o allow_root and
 * -o default_permissions, but not ``-o use_ino``).  Instead of `-o
 * debug`, debugging may also enabled with `-d` or `--debug`.
 *
 * If not all options are known, an error message is written to stderr
 * and the function returns NULL.
 *
 * Option parsing skips argv[0], which is assumed to contain the
 * program name. To prevent accidentally passing an option in
 * argv[0], this element must always be present (even if no options
 * are specified). It may be set to the empty string ('\0') if no
 * reasonable value can be provided.
 *
 * @param args argument vector
 * @param op the (low-level) filesystem operations
 * @param op_size sizeof(struct fuse_lowlevel_ops)
 * @param userdata user data
 *
 * @return the fuse session on success, NULL on failure
 **/
struct fuse_session *fuse_session_new(struct fuse_args *args,
                                      const struct fuse_lowlevel_ops *op,
                                      size_t op_size, void *userdata);

/**
 * Mount a FUSE file system.
 *
 * @param se session object
 *
 * @return 0 on success, -1 on failure.
 **/
int fuse_session_mount(struct fuse_session *se);

/**
 * Enter a single threaded, blocking event loop.
 *
 * When the event loop terminates because the connection to the FUSE
 * kernel module has been closed, this function returns zero. This
 * happens when the filesystem is unmounted regularly (by the
 * filesystem owner or root running the umount(8) or fusermount(1)
 * command), or if connection is explicitly severed by writing ``1``
 * to the``abort`` file in ``/sys/fs/fuse/connections/NNN``. The only
 * way to distinguish between these two conditions is to check if the
 * filesystem is still mounted after the session loop returns.
 *
 * When some error occurs during request processing, the function
 * returns a negated errno(3) value.
 *
 * If the loop has been terminated because of a signal handler
 * installed by fuse_set_signal_handlers(), this function returns the
 * (positive) signal value that triggered the exit.
 *
 * @param se the session
 * @return 0, -errno, or a signal value
 */
int fuse_session_loop(struct fuse_session *se);

/**
 * Flag a session as terminated.
 *
 * This function is invoked by the POSIX signal handlers, when
 * registered using fuse_set_signal_handlers(). It will cause any
 * running event loops to terminate on the next opportunity.
 *
 * @param se the session
 */
void fuse_session_exit(struct fuse_session *se);

/**
 * Reset the terminated flag of a session
 *
 * @param se the session
 */
void fuse_session_reset(struct fuse_session *se);

/**
 * Query the terminated flag of a session
 *
 * @param se the session
 * @return 1 if exited, 0 if not exited
 */
int fuse_session_exited(struct fuse_session *se);

/**
 * Ensure that file system is unmounted.
 *
 * In regular operation, the file system is typically unmounted by the
 * user calling umount(8) or fusermount(1), which then terminates the
 * FUSE session loop. However, the session loop may also terminate as
 * a result of an explicit call to fuse_session_exit() (e.g. by a
 * signal handler installed by fuse_set_signal_handler()). In this
 * case the filesystem remains mounted, but any attempt to access it
 * will block (while the filesystem process is still running) or give
 * an ESHUTDOWN error (after the filesystem process has terminated).
 *
 * If the communication channel with the FUSE kernel module is still
 * open (i.e., if the session loop was terminated by an explicit call
 * to fuse_session_exit()), this function will close it and unmount
 * the filesystem. If the communication channel has been closed by the
 * kernel, this method will do (almost) nothing.
 *
 * NOTE: The above semantics mean that if the connection to the kernel
 * is terminated via the ``/sys/fs/fuse/connections/NNN/abort`` file,
 * this method will *not* unmount the filesystem.
 *
 * @param se the session
 */
void fuse_session_unmount(struct fuse_session *se);

/**
 * Destroy a session
 *
 * @param se the session
 */
void fuse_session_destroy(struct fuse_session *se);

/*
 * Custom event loop support
 */

/**
 * Return file descriptor for communication with kernel.
 *
 * The file selector can be used to integrate FUSE with a custom event
 * loop. Whenever data is available for reading on the provided fd,
 * the event loop should call `fuse_session_receive_buf` followed by
 * `fuse_session_process_buf` to process the request.
 *
 * The returned file descriptor is valid until `fuse_session_unmount`
 * is called.
 *
 * @param se the session
 * @return a file descriptor
 */
int fuse_session_fd(struct fuse_session *se);

/**
 * Process a raw request supplied in a generic buffer
 *
 * The fuse_buf may contain a memory buffer or a pipe file descriptor.
 *
 * @param se the session
 * @param buf the fuse_buf containing the request
 */
void fuse_session_process_buf(struct fuse_session *se,
                              const struct fuse_buf *buf);

/**
 * Read a raw request from the kernel into the supplied buffer.
 *
 * Depending on file system options, system capabilities, and request
 * size the request is either read into a memory buffer or spliced
 * into a temporary pipe.
 *
 * @param se the session
 * @param buf the fuse_buf to store the request in
 * @return the actual size of the raw request, or -errno on error
 */
int fuse_session_receive_buf(struct fuse_session *se, struct fuse_buf *buf);

#endif /* FUSE_LOWLEVEL_H_ */
