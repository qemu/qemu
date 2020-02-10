/*
 * FUSE: Filesystem in Userspace
 * Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
 *
 * This program can be distributed under the terms of the GNU LGPLv2.
 * See the file COPYING.LIB.
 */

#ifndef FUSE_H_
#define FUSE_H_

/*
 *
 * This file defines the library interface of FUSE
 *
 * IMPORTANT: you should define FUSE_USE_VERSION before including this header.
 */

#include "fuse_common.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <time.h>

/*
 * Basic FUSE API
 */

/** Handle for a FUSE filesystem */
struct fuse;

/**
 * Readdir flags, passed to ->readdir()
 */
enum fuse_readdir_flags {
    /**
     * "Plus" mode.
     *
     * The kernel wants to prefill the inode cache during readdir.  The
     * filesystem may honour this by filling in the attributes and setting
     * FUSE_FILL_DIR_FLAGS for the filler function.  The filesystem may also
     * just ignore this flag completely.
     */
    FUSE_READDIR_PLUS = (1 << 0),
};

enum fuse_fill_dir_flags {
    /**
     * "Plus" mode: all file attributes are valid
     *
     * The attributes are used by the kernel to prefill the inode cache
     * during a readdir.
     *
     * It is okay to set FUSE_FILL_DIR_PLUS if FUSE_READDIR_PLUS is not set
     * and vice versa.
     */
    FUSE_FILL_DIR_PLUS = (1 << 1),
};

/**
 * Function to add an entry in a readdir() operation
 *
 * The *off* parameter can be any non-zero value that enables the
 * filesystem to identify the current point in the directory
 * stream. It does not need to be the actual physical position. A
 * value of zero is reserved to indicate that seeking in directories
 * is not supported.
 *
 * @param buf the buffer passed to the readdir() operation
 * @param name the file name of the directory entry
 * @param stat file attributes, can be NULL
 * @param off offset of the next entry or zero
 * @param flags fill flags
 * @return 1 if buffer is full, zero otherwise
 */
typedef int (*fuse_fill_dir_t)(void *buf, const char *name,
                               const struct stat *stbuf, off_t off,
                               enum fuse_fill_dir_flags flags);
/**
 * Configuration of the high-level API
 *
 * This structure is initialized from the arguments passed to
 * fuse_new(), and then passed to the file system's init() handler
 * which should ensure that the configuration is compatible with the
 * file system implementation.
 */
struct fuse_config {
    /**
     * If `set_gid` is non-zero, the st_gid attribute of each file
     * is overwritten with the value of `gid`.
     */
    int set_gid;
    unsigned int gid;

    /**
     * If `set_uid` is non-zero, the st_uid attribute of each file
     * is overwritten with the value of `uid`.
     */
    int set_uid;
    unsigned int uid;

    /**
     * If `set_mode` is non-zero, the any permissions bits set in
     * `umask` are unset in the st_mode attribute of each file.
     */
    int set_mode;
    unsigned int umask;

    /**
     * The timeout in seconds for which name lookups will be
     * cached.
     */
    double entry_timeout;

    /**
     * The timeout in seconds for which a negative lookup will be
     * cached. This means, that if file did not exist (lookup
     * retuned ENOENT), the lookup will only be redone after the
     * timeout, and the file/directory will be assumed to not
     * exist until then. A value of zero means that negative
     * lookups are not cached.
     */
    double negative_timeout;

    /**
     * The timeout in seconds for which file/directory attributes
     * (as returned by e.g. the `getattr` handler) are cached.
     */
    double attr_timeout;

    /**
     * Allow requests to be interrupted
     */
    int intr;

    /**
     * Specify which signal number to send to the filesystem when
     * a request is interrupted.  The default is hardcoded to
     * USR1.
     */
    int intr_signal;

    /**
     * Normally, FUSE assigns inodes to paths only for as long as
     * the kernel is aware of them. With this option inodes are
     * instead remembered for at least this many seconds.  This
     * will require more memory, but may be necessary when using
     * applications that make use of inode numbers.
     *
     * A number of -1 means that inodes will be remembered for the
     * entire life-time of the file-system process.
     */
    int remember;

    /**
     * The default behavior is that if an open file is deleted,
     * the file is renamed to a hidden file (.fuse_hiddenXXX), and
     * only removed when the file is finally released.  This
     * relieves the filesystem implementation of having to deal
     * with this problem. This option disables the hiding
     * behavior, and files are removed immediately in an unlink
     * operation (or in a rename operation which overwrites an
     * existing file).
     *
     * It is recommended that you not use the hard_remove
     * option. When hard_remove is set, the following libc
     * functions fail on unlinked files (returning errno of
     * ENOENT): read(2), write(2), fsync(2), close(2), f*xattr(2),
     * ftruncate(2), fstat(2), fchmod(2), fchown(2)
     */
    int hard_remove;

    /**
     * Honor the st_ino field in the functions getattr() and
     * fill_dir(). This value is used to fill in the st_ino field
     * in the stat(2), lstat(2), fstat(2) functions and the d_ino
     * field in the readdir(2) function. The filesystem does not
     * have to guarantee uniqueness, however some applications
     * rely on this value being unique for the whole filesystem.
     *
     * Note that this does *not* affect the inode that libfuse
     * and the kernel use internally (also called the "nodeid").
     */
    int use_ino;

    /**
     * If use_ino option is not given, still try to fill in the
     * d_ino field in readdir(2). If the name was previously
     * looked up, and is still in the cache, the inode number
     * found there will be used.  Otherwise it will be set to -1.
     * If use_ino option is given, this option is ignored.
     */
    int readdir_ino;

    /**
     * This option disables the use of page cache (file content cache)
     * in the kernel for this filesystem. This has several affects:
     *
     * 1. Each read(2) or write(2) system call will initiate one
     *    or more read or write operations, data will not be
     *    cached in the kernel.
     *
     * 2. The return value of the read() and write() system calls
     *    will correspond to the return values of the read and
     *    write operations. This is useful for example if the
     *    file size is not known in advance (before reading it).
     *
     * Internally, enabling this option causes fuse to set the
     * `direct_io` field of `struct fuse_file_info` - overwriting
     * any value that was put there by the file system.
     */
    int direct_io;

    /**
     * This option disables flushing the cache of the file
     * contents on every open(2).  This should only be enabled on
     * filesystems where the file data is never changed
     * externally (not through the mounted FUSE filesystem).  Thus
     * it is not suitable for network filesystems and other
     * intermediate filesystems.
     *
     * NOTE: if this option is not specified (and neither
     * direct_io) data is still cached after the open(2), so a
     * read(2) system call will not always initiate a read
     * operation.
     *
     * Internally, enabling this option causes fuse to set the
     * `keep_cache` field of `struct fuse_file_info` - overwriting
     * any value that was put there by the file system.
     */
    int kernel_cache;

    /**
     * This option is an alternative to `kernel_cache`. Instead of
     * unconditionally keeping cached data, the cached data is
     * invalidated on open(2) if if the modification time or the
     * size of the file has changed since it was last opened.
     */
    int auto_cache;

    /**
     * The timeout in seconds for which file attributes are cached
     * for the purpose of checking if auto_cache should flush the
     * file data on open.
     */
    int ac_attr_timeout_set;
    double ac_attr_timeout;

    /**
     * If this option is given the file-system handlers for the
     * following operations will not receive path information:
     * read, write, flush, release, fsync, readdir, releasedir,
     * fsyncdir, lock, ioctl and poll.
     *
     * For the truncate, getattr, chmod, chown and utimens
     * operations the path will be provided only if the struct
     * fuse_file_info argument is NULL.
     */
    int nullpath_ok;

    /**
     * The remaining options are used by libfuse internally and
     * should not be touched.
     */
    int show_help;
    char *modules;
    int debug;
};


/**
 * The file system operations:
 *
 * Most of these should work very similarly to the well known UNIX
 * file system operations.  A major exception is that instead of
 * returning an error in 'errno', the operation should return the
 * negated error value (-errno) directly.
 *
 * All methods are optional, but some are essential for a useful
 * filesystem (e.g. getattr).  Open, flush, release, fsync, opendir,
 * releasedir, fsyncdir, access, create, truncate, lock, init and
 * destroy are special purpose methods, without which a full featured
 * filesystem can still be implemented.
 *
 * In general, all methods are expected to perform any necessary
 * permission checking. However, a filesystem may delegate this task
 * to the kernel by passing the `default_permissions` mount option to
 * `fuse_new()`. In this case, methods will only be called if
 * the kernel's permission check has succeeded.
 *
 * Almost all operations take a path which can be of any length.
 */
struct fuse_operations {
    /**
     * Get file attributes.
     *
     * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
     * ignored. The 'st_ino' field is ignored except if the 'use_ino'
     * mount option is given. In that case it is passed to userspace,
     * but libfuse and the kernel will still assign a different
     * inode for internal use (called the "nodeid").
     *
     * `fi` will always be NULL if the file is not currently open, but
     * may also be NULL if the file is open.
     */
    int (*getattr)(const char *, struct stat *, struct fuse_file_info *fi);

    /**
     * Read the target of a symbolic link
     *
     * The buffer should be filled with a null terminated string.  The
     * buffer size argument includes the space for the terminating
     * null character. If the linkname is too long to fit in the
     * buffer, it should be truncated. The return value should be 0
     * for success.
     */
    int (*readlink)(const char *, char *, size_t);

    /**
     * Create a file node
     *
     * This is called for creation of all non-directory, non-symlink
     * nodes.  If the filesystem defines a create() method, then for
     * regular files that will be called instead.
     */
    int (*mknod)(const char *, mode_t, dev_t);

    /**
     * Create a directory
     *
     * Note that the mode argument may not have the type specification
     * bits set, i.e. S_ISDIR(mode) can be false.  To obtain the
     * correct directory type bits use  mode|S_IFDIR
     */
    int (*mkdir)(const char *, mode_t);

    /** Remove a file */
    int (*unlink)(const char *);

    /** Remove a directory */
    int (*rmdir)(const char *);

    /** Create a symbolic link */
    int (*symlink)(const char *, const char *);

    /**
     * Rename a file
     *
     * *flags* may be `RENAME_EXCHANGE` or `RENAME_NOREPLACE`. If
     * RENAME_NOREPLACE is specified, the filesystem must not
     * overwrite *newname* if it exists and return an error
     * instead. If `RENAME_EXCHANGE` is specified, the filesystem
     * must atomically exchange the two files, i.e. both must
     * exist and neither may be deleted.
     */
    int (*rename)(const char *, const char *, unsigned int flags);

    /** Create a hard link to a file */
    int (*link)(const char *, const char *);

    /**
     * Change the permission bits of a file
     *
     * `fi` will always be NULL if the file is not currenlty open, but
     * may also be NULL if the file is open.
     */
    int (*chmod)(const char *, mode_t, struct fuse_file_info *fi);

    /**
     * Change the owner and group of a file
     *
     * `fi` will always be NULL if the file is not currenlty open, but
     * may also be NULL if the file is open.
     *
     * Unless FUSE_CAP_HANDLE_KILLPRIV is disabled, this method is
     * expected to reset the setuid and setgid bits.
     */
    int (*chown)(const char *, uid_t, gid_t, struct fuse_file_info *fi);

    /**
     * Change the size of a file
     *
     * `fi` will always be NULL if the file is not currenlty open, but
     * may also be NULL if the file is open.
     *
     * Unless FUSE_CAP_HANDLE_KILLPRIV is disabled, this method is
     * expected to reset the setuid and setgid bits.
     */
    int (*truncate)(const char *, off_t, struct fuse_file_info *fi);

    /**
     * Open a file
     *
     * Open flags are available in fi->flags. The following rules
     * apply.
     *
     *  - Creation (O_CREAT, O_EXCL, O_NOCTTY) flags will be
     *    filtered out / handled by the kernel.
     *
     *  - Access modes (O_RDONLY, O_WRONLY, O_RDWR, O_EXEC, O_SEARCH)
     *    should be used by the filesystem to check if the operation is
     *    permitted.  If the ``-o default_permissions`` mount option is
     *    given, this check is already done by the kernel before calling
     *    open() and may thus be omitted by the filesystem.
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
     * future calls to open will also succeed without being send
     * to the filesystem process.
     *
     */
    int (*open)(const char *, struct fuse_file_info *);

    /**
     * Read data from an open file
     *
     * Read should return exactly the number of bytes requested except
     * on EOF or error, otherwise the rest of the data will be
     * substituted with zeroes.  An exception to this is when the
     * 'direct_io' mount option is specified, in which case the return
     * value of the read system call will reflect the return value of
     * this operation.
     */
    int (*read)(const char *, char *, size_t, off_t, struct fuse_file_info *);

    /**
     * Write data to an open file
     *
     * Write should return exactly the number of bytes requested
     * except on error.  An exception to this is when the 'direct_io'
     * mount option is specified (see read operation).
     *
     * Unless FUSE_CAP_HANDLE_KILLPRIV is disabled, this method is
     * expected to reset the setuid and setgid bits.
     */
    int (*write)(const char *, const char *, size_t, off_t,
                 struct fuse_file_info *);

    /**
     * Get file system statistics
     *
     * The 'f_favail', 'f_fsid' and 'f_flag' fields are ignored
     */
    int (*statfs)(const char *, struct statvfs *);

    /**
     * Possibly flush cached data
     *
     * BIG NOTE: This is not equivalent to fsync().  It's not a
     * request to sync dirty data.
     *
     * Flush is called on each close() of a file descriptor, as opposed to
     * release which is called on the close of the last file descriptor for
     * a file.  Under Linux, errors returned by flush() will be passed to
     * userspace as errors from close(), so flush() is a good place to write
     * back any cached dirty data. However, many applications ignore errors
     * on close(), and on non-Linux systems, close() may succeed even if flush()
     * returns an error. For these reasons, filesystems should not assume
     * that errors returned by flush will ever be noticed or even
     * delivered.
     *
     * NOTE: The flush() method may be called more than once for each
     * open().  This happens if more than one file descriptor refers to an
     * open file handle, e.g. due to dup(), dup2() or fork() calls.  It is
     * not possible to determine if a flush is final, so each flush should
     * be treated equally.  Multiple write-flush sequences are relatively
     * rare, so this shouldn't be a problem.
     *
     * Filesystems shouldn't assume that flush will be called at any
     * particular point.  It may be called more times than expected, or not
     * at all.
     *
     * [close]:
     * http://pubs.opengroup.org/onlinepubs/9699919799/functions/close.html
     */
    int (*flush)(const char *, struct fuse_file_info *);

    /**
     * Release an open file
     *
     * Release is called when there are no more references to an open
     * file: all file descriptors are closed and all memory mappings
     * are unmapped.
     *
     * For every open() call there will be exactly one release() call
     * with the same flags and file handle.  It is possible to
     * have a file opened more than once, in which case only the last
     * release will mean, that no more reads/writes will happen on the
     * file.  The return value of release is ignored.
     */
    int (*release)(const char *, struct fuse_file_info *);

    /*
     * Synchronize file contents
     *
     * If the datasync parameter is non-zero, then only the user data
     * should be flushed, not the meta data.
     */
    int (*fsync)(const char *, int, struct fuse_file_info *);

    /** Set extended attributes */
    int (*setxattr)(const char *, const char *, const char *, size_t, int);

    /** Get extended attributes */
    int (*getxattr)(const char *, const char *, char *, size_t);

    /** List extended attributes */
    int (*listxattr)(const char *, char *, size_t);

    /** Remove extended attributes */
    int (*removexattr)(const char *, const char *);

    /*
     * Open directory
     *
     * Unless the 'default_permissions' mount option is given,
     * this method should check if opendir is permitted for this
     * directory. Optionally opendir may also return an arbitrary
     * filehandle in the fuse_file_info structure, which will be
     * passed to readdir, releasedir and fsyncdir.
     */
    int (*opendir)(const char *, struct fuse_file_info *);

    /*
     * Read directory
     *
     * The filesystem may choose between two modes of operation:
     *
     * 1) The readdir implementation ignores the offset parameter, and
     * passes zero to the filler function's offset.  The filler
     * function will not return '1' (unless an error happens), so the
     * whole directory is read in a single readdir operation.
     *
     * 2) The readdir implementation keeps track of the offsets of the
     * directory entries.  It uses the offset parameter and always
     * passes non-zero offset to the filler function.  When the buffer
     * is full (or an error happens) the filler function will return
     * '1'.
     */
    int (*readdir)(const char *, void *, fuse_fill_dir_t, off_t,
                   struct fuse_file_info *, enum fuse_readdir_flags);

    /**
     *  Release directory
     */
    int (*releasedir)(const char *, struct fuse_file_info *);

    /**
     * Synchronize directory contents
     *
     * If the datasync parameter is non-zero, then only the user data
     * should be flushed, not the meta data
     */
    int (*fsyncdir)(const char *, int, struct fuse_file_info *);

    /**
     * Initialize filesystem
     *
     * The return value will passed in the `private_data` field of
     * `struct fuse_context` to all file operations, and as a
     * parameter to the destroy() method. It overrides the initial
     * value provided to fuse_main() / fuse_new().
     */
    void *(*init)(struct fuse_conn_info *conn, struct fuse_config *cfg);

    /**
     * Clean up filesystem
     *
     * Called on filesystem exit.
     */
    void (*destroy)(void *private_data);

    /**
     * Check file access permissions
     *
     * This will be called for the access() system call.  If the
     * 'default_permissions' mount option is given, this method is not
     * called.
     *
     * This method is not called under Linux kernel versions 2.4.x
     */
    int (*access)(const char *, int);

    /**
     * Create and open a file
     *
     * If the file does not exist, first create it with the specified
     * mode, and then open it.
     *
     * If this method is not implemented or under Linux kernel
     * versions earlier than 2.6.15, the mknod() and open() methods
     * will be called instead.
     */
    int (*create)(const char *, mode_t, struct fuse_file_info *);

    /**
     * Perform POSIX file locking operation
     *
     * The cmd argument will be either F_GETLK, F_SETLK or F_SETLKW.
     *
     * For the meaning of fields in 'struct flock' see the man page
     * for fcntl(2).  The l_whence field will always be set to
     * SEEK_SET.
     *
     * For checking lock ownership, the 'fuse_file_info->owner'
     * argument must be used.
     *
     * For F_GETLK operation, the library will first check currently
     * held locks, and if a conflicting lock is found it will return
     * information without calling this method.  This ensures, that
     * for local locks the l_pid field is correctly filled in. The
     * results may not be accurate in case of race conditions and in
     * the presence of hard links, but it's unlikely that an
     * application would rely on accurate GETLK results in these
     * cases.  If a conflicting lock is not found, this method will be
     * called, and the filesystem may fill out l_pid by a meaningful
     * value, or it may leave this field zero.
     *
     * For F_SETLK and F_SETLKW the l_pid field will be set to the pid
     * of the process performing the locking operation.
     *
     * Note: if this method is not implemented, the kernel will still
     * allow file locking to work locally.  Hence it is only
     * interesting for network filesystems and similar.
     */
    int (*lock)(const char *, struct fuse_file_info *, int cmd, struct flock *);

    /**
     * Change the access and modification times of a file with
     * nanosecond resolution
     *
     * This supersedes the old utime() interface.  New applications
     * should use this.
     *
     * `fi` will always be NULL if the file is not currenlty open, but
     * may also be NULL if the file is open.
     *
     * See the utimensat(2) man page for details.
     */
    int (*utimens)(const char *, const struct timespec tv[2],
                   struct fuse_file_info *fi);

    /**
     * Map block index within file to block index within device
     *
     * Note: This makes sense only for block device backed filesystems
     * mounted with the 'blkdev' option
     */
    int (*bmap)(const char *, size_t blocksize, uint64_t *idx);

    /**
     * Ioctl
     *
     * flags will have FUSE_IOCTL_COMPAT set for 32bit ioctls in
     * 64bit environment.  The size and direction of data is
     * determined by _IOC_*() decoding of cmd.  For _IOC_NONE,
     * data will be NULL, for _IOC_WRITE data is out area, for
     * _IOC_READ in area and if both are set in/out area.  In all
     * non-NULL cases, the area is of _IOC_SIZE(cmd) bytes.
     *
     * If flags has FUSE_IOCTL_DIR then the fuse_file_info refers to a
     * directory file handle.
     *
     * Note : the unsigned long request submitted by the application
     * is truncated to 32 bits.
     */
    int (*ioctl)(const char *, unsigned int cmd, void *arg,
                 struct fuse_file_info *, unsigned int flags, void *data);

    /**
     * Poll for IO readiness events
     *
     * Note: If ph is non-NULL, the client should notify
     * when IO readiness events occur by calling
     * fuse_notify_poll() with the specified ph.
     *
     * Regardless of the number of times poll with a non-NULL ph
     * is received, single notification is enough to clear all.
     * Notifying more times incurs overhead but doesn't harm
     * correctness.
     *
     * The callee is responsible for destroying ph with
     * fuse_pollhandle_destroy() when no longer in use.
     */
    int (*poll)(const char *, struct fuse_file_info *,
                struct fuse_pollhandle *ph, unsigned *reventsp);

    /*
     * Write contents of buffer to an open file
     *
     * Similar to the write() method, but data is supplied in a
     * generic buffer.  Use fuse_buf_copy() to transfer data to
     * the destination.
     *
     * Unless FUSE_CAP_HANDLE_KILLPRIV is disabled, this method is
     * expected to reset the setuid and setgid bits.
     */
    int (*write_buf)(const char *, struct fuse_bufvec *buf, off_t off,
                     struct fuse_file_info *);

    /*
     *  Store data from an open file in a buffer
     *
     * Similar to the read() method, but data is stored and
     * returned in a generic buffer.
     *
     * No actual copying of data has to take place, the source
     * file descriptor may simply be stored in the buffer for
     * later data transfer.
     *
     * The buffer must be allocated dynamically and stored at the
     * location pointed to by bufp.  If the buffer contains memory
     * regions, they too must be allocated using malloc().  The
     * allocated memory will be freed by the caller.
     */
    int (*read_buf)(const char *, struct fuse_bufvec **bufp, size_t size,
                    off_t off, struct fuse_file_info *);
    /**
     * Perform BSD file locking operation
     *
     * The op argument will be either LOCK_SH, LOCK_EX or LOCK_UN
     *
     * Nonblocking requests will be indicated by ORing LOCK_NB to
     * the above operations
     *
     * For more information see the flock(2) manual page.
     *
     * Additionally fi->owner will be set to a value unique to
     * this open file.  This same value will be supplied to
     * ->release() when the file is released.
     *
     * Note: if this method is not implemented, the kernel will still
     * allow file locking to work locally.  Hence it is only
     * interesting for network filesystems and similar.
     */
    int (*flock)(const char *, struct fuse_file_info *, int op);

    /**
     * Allocates space for an open file
     *
     * This function ensures that required space is allocated for specified
     * file.  If this function returns success then any subsequent write
     * request to specified range is guaranteed not to fail because of lack
     * of space on the file system media.
     */
    int (*fallocate)(const char *, int, off_t, off_t, struct fuse_file_info *);

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
     */
    ssize_t (*copy_file_range)(const char *path_in,
                               struct fuse_file_info *fi_in, off_t offset_in,
                               const char *path_out,
                               struct fuse_file_info *fi_out, off_t offset_out,
                               size_t size, int flags);

    /**
     * Find next data or hole after the specified offset
     */
    off_t (*lseek)(const char *, off_t off, int whence,
                   struct fuse_file_info *);
};

/*
 * Extra context that may be needed by some filesystems
 *
 * The uid, gid and pid fields are not filled in case of a writepage
 * operation.
 */
struct fuse_context {
    /** Pointer to the fuse object */
    struct fuse *fuse;

    /** User ID of the calling process */
    uid_t uid;

    /** Group ID of the calling process */
    gid_t gid;

    /** Process ID of the calling thread */
    pid_t pid;

    /** Private filesystem data */
    void *private_data;

    /** Umask of the calling process */
    mode_t umask;
};

/**
 * Main function of FUSE.
 *
 * This is for the lazy.  This is all that has to be called from the
 * main() function.
 *
 * This function does the following:
 *   - parses command line options, and handles --help and
 *     --version
 *   - installs signal handlers for INT, HUP, TERM and PIPE
 *   - registers an exit handler to unmount the filesystem on program exit
 *   - creates a fuse handle
 *   - registers the operations
 *   - calls either the single-threaded or the multi-threaded event loop
 *
 * Most file systems will have to parse some file-system specific
 * arguments before calling this function. It is recommended to do
 * this with fuse_opt_parse() and a processing function that passes
 * through any unknown options (this can also be achieved by just
 * passing NULL as the processing function). That way, the remaining
 * options can be passed directly to fuse_main().
 *
 * fuse_main() accepts all options that can be passed to
 * fuse_parse_cmdline(), fuse_new(), or fuse_session_new().
 *
 * Option parsing skips argv[0], which is assumed to contain the
 * program name. This element must always be present and is used to
 * construct a basic ``usage: `` message for the --help
 * output. argv[0] may also be set to the empty string. In this case
 * the usage message is suppressed. This can be used by file systems
 * to print their own usage line first. See hello.c for an example of
 * how to do this.
 *
 * Note: this is currently implemented as a macro.
 *
 * The following error codes may be returned from fuse_main():
 *   1: Invalid option arguments
 *   2: No mount point specified
 *   3: FUSE setup failed
 *   4: Mounting failed
 *   5: Failed to daemonize (detach from session)
 *   6: Failed to set up signal handlers
 *   7: An error occured during the life of the file system
 *
 * @param argc the argument counter passed to the main() function
 * @param argv the argument vector passed to the main() function
 * @param op the file system operation
 * @param private_data Initial value for the `private_data`
 *            field of `struct fuse_context`. May be overridden by the
 *            `struct fuse_operations.init` handler.
 * @return 0 on success, nonzero on failure
 *
 * Example usage, see hello.c
 */
/*
 * int fuse_main(int argc, char *argv[], const struct fuse_operations *op,
 * void *private_data);
 */
#define fuse_main(argc, argv, op, private_data) \
    fuse_main_real(argc, argv, op, sizeof(*(op)), private_data)

/*
 * More detailed API
 */

/**
 * Print available options (high- and low-level) to stdout.  This is
 * not an exhaustive list, but includes only those options that may be
 * of interest to an end-user of a file system.
 *
 * The function looks at the argument vector only to determine if
 * there are additional modules to be loaded (module=foo option),
 * and attempts to call their help functions as well.
 *
 * @param args the argument vector.
 */
void fuse_lib_help(struct fuse_args *args);

/**
 * Create a new FUSE filesystem.
 *
 * This function accepts most file-system independent mount options
 * (like context, nodev, ro - see mount(8)), as well as the
 * FUSE-specific mount options from mount.fuse(8).
 *
 * If the --help option is specified, the function writes a help text
 * to stdout and returns NULL.
 *
 * Option parsing skips argv[0], which is assumed to contain the
 * program name. This element must always be present and is used to
 * construct a basic ``usage: `` message for the --help output. If
 * argv[0] is set to the empty string, no usage message is included in
 * the --help output.
 *
 * If an unknown option is passed in, an error message is written to
 * stderr and the function returns NULL.
 *
 * @param args argument vector
 * @param op the filesystem operations
 * @param op_size the size of the fuse_operations structure
 * @param private_data Initial value for the `private_data`
 *            field of `struct fuse_context`. May be overridden by the
 *            `struct fuse_operations.init` handler.
 * @return the created FUSE handle
 */
#if FUSE_USE_VERSION == 30
struct fuse *fuse_new_30(struct fuse_args *args,
                         const struct fuse_operations *op, size_t op_size,
                         void *private_data);
#define fuse_new(args, op, size, data) fuse_new_30(args, op, size, data)
#else
struct fuse *fuse_new(struct fuse_args *args, const struct fuse_operations *op,
                      size_t op_size, void *private_data);
#endif

/**
 * Mount a FUSE file system.
 *
 * @param mountpoint the mount point path
 * @param f the FUSE handle
 *
 * @return 0 on success, -1 on failure.
 **/
int fuse_mount(struct fuse *f, const char *mountpoint);

/**
 * Unmount a FUSE file system.
 *
 * See fuse_session_unmount() for additional information.
 *
 * @param f the FUSE handle
 **/
void fuse_unmount(struct fuse *f);

/**
 * Destroy the FUSE handle.
 *
 * NOTE: This function does not unmount the filesystem.  If this is
 * needed, call fuse_unmount() before calling this function.
 *
 * @param f the FUSE handle
 */
void fuse_destroy(struct fuse *f);

/**
 * FUSE event loop.
 *
 * Requests from the kernel are processed, and the appropriate
 * operations are called.
 *
 * For a description of the return value and the conditions when the
 * event loop exits, refer to the documentation of
 * fuse_session_loop().
 *
 * @param f the FUSE handle
 * @return see fuse_session_loop()
 *
 * See also: fuse_loop_mt()
 */
int fuse_loop(struct fuse *f);

/**
 * Flag session as terminated
 *
 * This function will cause any running event loops to exit on
 * the next opportunity.
 *
 * @param f the FUSE handle
 */
void fuse_exit(struct fuse *f);

/**
 * Get the current context
 *
 * The context is only valid for the duration of a filesystem
 * operation, and thus must not be stored and used later.
 *
 * @return the context
 */
struct fuse_context *fuse_get_context(void);

/**
 * Check if the current request has already been interrupted
 *
 * @return 1 if the request has been interrupted, 0 otherwise
 */
int fuse_interrupted(void);

/**
 * Invalidates cache for the given path.
 *
 * This calls fuse_lowlevel_notify_inval_inode internally.
 *
 * @return 0 on successful invalidation, negative error value otherwise.
 *         This routine may return -ENOENT to indicate that there was
 *         no entry to be invalidated, e.g., because the path has not
 *         been seen before or has been forgotten; this should not be
 *         considered to be an error.
 */
int fuse_invalidate_path(struct fuse *f, const char *path);

/**
 * The real main function
 *
 * Do not call this directly, use fuse_main()
 */
int fuse_main_real(int argc, char *argv[], const struct fuse_operations *op,
                   size_t op_size, void *private_data);

/**
 * Start the cleanup thread when using option "remember".
 *
 * This is done automatically by fuse_loop_mt()
 * @param fuse struct fuse pointer for fuse instance
 * @return 0 on success and -1 on error
 */
int fuse_start_cleanup_thread(struct fuse *fuse);

/**
 * Stop the cleanup thread when using option "remember".
 *
 * This is done automatically by fuse_loop_mt()
 * @param fuse struct fuse pointer for fuse instance
 */
void fuse_stop_cleanup_thread(struct fuse *fuse);

/**
 * Iterate over cache removing stale entries
 * use in conjunction with "-oremember"
 *
 * NOTE: This is already done for the standard sessions
 *
 * @param fuse struct fuse pointer for fuse instance
 * @return the number of seconds until the next cleanup
 */
int fuse_clean_cache(struct fuse *fuse);

/*
 * Stacking API
 */

/**
 * Fuse filesystem object
 *
 * This is opaque object represents a filesystem layer
 */
struct fuse_fs;

/*
 * These functions call the relevant filesystem operation, and return
 * the result.
 *
 * If the operation is not defined, they return -ENOSYS, with the
 * exception of fuse_fs_open, fuse_fs_release, fuse_fs_opendir,
 * fuse_fs_releasedir and fuse_fs_statfs, which return 0.
 */

int fuse_fs_getattr(struct fuse_fs *fs, const char *path, struct stat *buf,
                    struct fuse_file_info *fi);
int fuse_fs_rename(struct fuse_fs *fs, const char *oldpath, const char *newpath,
                   unsigned int flags);
int fuse_fs_unlink(struct fuse_fs *fs, const char *path);
int fuse_fs_rmdir(struct fuse_fs *fs, const char *path);
int fuse_fs_symlink(struct fuse_fs *fs, const char *linkname, const char *path);
int fuse_fs_link(struct fuse_fs *fs, const char *oldpath, const char *newpath);
int fuse_fs_release(struct fuse_fs *fs, const char *path,
                    struct fuse_file_info *fi);
int fuse_fs_open(struct fuse_fs *fs, const char *path,
                 struct fuse_file_info *fi);
int fuse_fs_read(struct fuse_fs *fs, const char *path, char *buf, size_t size,
                 off_t off, struct fuse_file_info *fi);
int fuse_fs_read_buf(struct fuse_fs *fs, const char *path,
                     struct fuse_bufvec **bufp, size_t size, off_t off,
                     struct fuse_file_info *fi);
int fuse_fs_write(struct fuse_fs *fs, const char *path, const char *buf,
                  size_t size, off_t off, struct fuse_file_info *fi);
int fuse_fs_write_buf(struct fuse_fs *fs, const char *path,
                      struct fuse_bufvec *buf, off_t off,
                      struct fuse_file_info *fi);
int fuse_fs_fsync(struct fuse_fs *fs, const char *path, int datasync,
                  struct fuse_file_info *fi);
int fuse_fs_flush(struct fuse_fs *fs, const char *path,
                  struct fuse_file_info *fi);
int fuse_fs_statfs(struct fuse_fs *fs, const char *path, struct statvfs *buf);
int fuse_fs_opendir(struct fuse_fs *fs, const char *path,
                    struct fuse_file_info *fi);
int fuse_fs_readdir(struct fuse_fs *fs, const char *path, void *buf,
                    fuse_fill_dir_t filler, off_t off,
                    struct fuse_file_info *fi, enum fuse_readdir_flags flags);
int fuse_fs_fsyncdir(struct fuse_fs *fs, const char *path, int datasync,
                     struct fuse_file_info *fi);
int fuse_fs_releasedir(struct fuse_fs *fs, const char *path,
                       struct fuse_file_info *fi);
int fuse_fs_create(struct fuse_fs *fs, const char *path, mode_t mode,
                   struct fuse_file_info *fi);
int fuse_fs_lock(struct fuse_fs *fs, const char *path,
                 struct fuse_file_info *fi, int cmd, struct flock *lock);
int fuse_fs_flock(struct fuse_fs *fs, const char *path,
                  struct fuse_file_info *fi, int op);
int fuse_fs_chmod(struct fuse_fs *fs, const char *path, mode_t mode,
                  struct fuse_file_info *fi);
int fuse_fs_chown(struct fuse_fs *fs, const char *path, uid_t uid, gid_t gid,
                  struct fuse_file_info *fi);
int fuse_fs_truncate(struct fuse_fs *fs, const char *path, off_t size,
                     struct fuse_file_info *fi);
int fuse_fs_utimens(struct fuse_fs *fs, const char *path,
                    const struct timespec tv[2], struct fuse_file_info *fi);
int fuse_fs_access(struct fuse_fs *fs, const char *path, int mask);
int fuse_fs_readlink(struct fuse_fs *fs, const char *path, char *buf,
                     size_t len);
int fuse_fs_mknod(struct fuse_fs *fs, const char *path, mode_t mode,
                  dev_t rdev);
int fuse_fs_mkdir(struct fuse_fs *fs, const char *path, mode_t mode);
int fuse_fs_setxattr(struct fuse_fs *fs, const char *path, const char *name,
                     const char *value, size_t size, int flags);
int fuse_fs_getxattr(struct fuse_fs *fs, const char *path, const char *name,
                     char *value, size_t size);
int fuse_fs_listxattr(struct fuse_fs *fs, const char *path, char *list,
                      size_t size);
int fuse_fs_removexattr(struct fuse_fs *fs, const char *path, const char *name);
int fuse_fs_bmap(struct fuse_fs *fs, const char *path, size_t blocksize,
                 uint64_t *idx);
int fuse_fs_ioctl(struct fuse_fs *fs, const char *path, unsigned int cmd,
                  void *arg, struct fuse_file_info *fi, unsigned int flags,
                  void *data);
int fuse_fs_poll(struct fuse_fs *fs, const char *path,
                 struct fuse_file_info *fi, struct fuse_pollhandle *ph,
                 unsigned *reventsp);
int fuse_fs_fallocate(struct fuse_fs *fs, const char *path, int mode,
                      off_t offset, off_t length, struct fuse_file_info *fi);
ssize_t fuse_fs_copy_file_range(struct fuse_fs *fs, const char *path_in,
                                struct fuse_file_info *fi_in, off_t off_in,
                                const char *path_out,
                                struct fuse_file_info *fi_out, off_t off_out,
                                size_t len, int flags);
off_t fuse_fs_lseek(struct fuse_fs *fs, const char *path, off_t off, int whence,
                    struct fuse_file_info *fi);
void fuse_fs_init(struct fuse_fs *fs, struct fuse_conn_info *conn,
                  struct fuse_config *cfg);
void fuse_fs_destroy(struct fuse_fs *fs);

int fuse_notify_poll(struct fuse_pollhandle *ph);

/**
 * Create a new fuse filesystem object
 *
 * This is usually called from the factory of a fuse module to create
 * a new instance of a filesystem.
 *
 * @param op the filesystem operations
 * @param op_size the size of the fuse_operations structure
 * @param private_data Initial value for the `private_data`
 *            field of `struct fuse_context`. May be overridden by the
 *            `struct fuse_operations.init` handler.
 * @return a new filesystem object
 */
struct fuse_fs *fuse_fs_new(const struct fuse_operations *op, size_t op_size,
                            void *private_data);

/**
 * Factory for creating filesystem objects
 *
 * The function may use and remove options from 'args' that belong
 * to this module.
 *
 * For now the 'fs' vector always contains exactly one filesystem.
 * This is the filesystem which will be below the newly created
 * filesystem in the stack.
 *
 * @param args the command line arguments
 * @param fs NULL terminated filesystem object vector
 * @return the new filesystem object
 */
typedef struct fuse_fs *(*fuse_module_factory_t)(struct fuse_args *args,
                                                 struct fuse_fs *fs[]);
/**
 * Register filesystem module
 *
 * If the "-omodules=*name*_:..." option is present, filesystem
 * objects are created and pushed onto the stack with the *factory_*
 * function.
 *
 * @param name_ the name of this filesystem module
 * @param factory_ the factory function for this filesystem module
 */
#define FUSE_REGISTER_MODULE(name_, factory_) \
    fuse_module_factory_t fuse_module_##name_##_factory = factory_

/** Get session from fuse object */
struct fuse_session *fuse_get_session(struct fuse *f);

/**
 * Open a FUSE file descriptor and set up the mount for the given
 * mountpoint and flags.
 *
 * @param mountpoint reference to the mount in the file system
 * @param options mount options
 * @return the FUSE file descriptor or -1 upon error
 */
int fuse_open_channel(const char *mountpoint, const char *options);

#endif /* FUSE_H_ */
