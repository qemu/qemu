/*
 * FUSE: Filesystem in Userspace
 * Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
 *
 * This program can be distributed under the terms of the GNU LGPLv2.
 * See the file COPYING.LIB.
 */

/** @file */

#if !defined(FUSE_H_) && !defined(FUSE_LOWLEVEL_H_)
#error \
    "Never include <fuse_common.h> directly; use <fuse.h> or <fuse_lowlevel.h> instead."
#endif

#ifndef FUSE_COMMON_H_
#define FUSE_COMMON_H_

#include "fuse_log.h"
#include "fuse_opt.h"

/** Major version of FUSE library interface */
#define FUSE_MAJOR_VERSION 3

/** Minor version of FUSE library interface */
#define FUSE_MINOR_VERSION 2

#define FUSE_MAKE_VERSION(maj, min) ((maj) * 10 + (min))
#define FUSE_VERSION FUSE_MAKE_VERSION(FUSE_MAJOR_VERSION, FUSE_MINOR_VERSION)

/**
 * Information about an open file.
 *
 * File Handles are created by the open, opendir, and create methods and closed
 * by the release and releasedir methods.  Multiple file handles may be
 * concurrently open for the same file.  Generally, a client will create one
 * file handle per file descriptor, though in some cases multiple file
 * descriptors can share a single file handle.
 */
struct fuse_file_info {
    /** Open flags. Available in open() and release() */
    int flags;

    /*
     * In case of a write operation indicates if this was caused
     * by a delayed write from the page cache. If so, then the
     * context's pid, uid, and gid fields will not be valid, and
     * the *fh* value may not match the *fh* value that would
     * have been sent with the corresponding individual write
     * requests if write caching had been disabled.
     */
    unsigned int writepage:1;

    /** Can be filled in by open, to use direct I/O on this file. */
    unsigned int direct_io:1;

    /*
     *  Can be filled in by open. It signals the kernel that any
     *  currently cached file data (ie., data that the filesystem
     *  provided the last time the file was open) need not be
     *  invalidated. Has no effect when set in other contexts (in
     *  particular it does nothing when set by opendir()).
     */
    unsigned int keep_cache:1;

    /*
     *  Indicates a flush operation.  Set in flush operation, also
     *  maybe set in highlevel lock operation and lowlevel release
     *  operation.
     */
    unsigned int flush:1;

    /*
     *  Can be filled in by open, to indicate that the file is not
     *  seekable.
     */
    unsigned int nonseekable:1;

    /*
     * Indicates that flock locks for this file should be
     * released.  If set, lock_owner shall contain a valid value.
     * May only be set in ->release().
     */
    unsigned int flock_release:1;

    /*
     *  Can be filled in by opendir. It signals the kernel to
     *  enable caching of entries returned by readdir().  Has no
     *  effect when set in other contexts (in particular it does
     *  nothing when set by open()).
     */
    unsigned int cache_readdir:1;

    /* Indicates that suid/sgid bits should be removed upon write */
    unsigned int kill_priv:1;


    /** Padding.  Reserved for future use*/
    unsigned int padding:24;
    unsigned int padding2:32;

    /*
     *  File handle id.  May be filled in by filesystem in create,
     * open, and opendir().  Available in most other file operations on the
     * same file handle.
     */
    uint64_t fh;

    /** Lock owner id.  Available in locking operations and flush */
    uint64_t lock_owner;

    /*
     * Requested poll events.  Available in ->poll.  Only set on kernels
     * which support it.  If unsupported, this field is set to zero.
     */
    uint32_t poll_events;
};

/*
 * Capability bits for 'fuse_conn_info.capable' and 'fuse_conn_info.want'
 */

/**
 * Indicates that the filesystem supports asynchronous read requests.
 *
 * If this capability is not requested/available, the kernel will
 * ensure that there is at most one pending read request per
 * file-handle at any time, and will attempt to order read requests by
 * increasing offset.
 *
 * This feature is enabled by default when supported by the kernel.
 */
#define FUSE_CAP_ASYNC_READ (1 << 0)

/**
 * Indicates that the filesystem supports "remote" locking.
 *
 * This feature is enabled by default when supported by the kernel,
 * and if getlk() and setlk() handlers are implemented.
 */
#define FUSE_CAP_POSIX_LOCKS (1 << 1)

/**
 * Indicates that the filesystem supports the O_TRUNC open flag.  If
 * disabled, and an application specifies O_TRUNC, fuse first calls
 * truncate() and then open() with O_TRUNC filtered out.
 *
 * This feature is enabled by default when supported by the kernel.
 */
#define FUSE_CAP_ATOMIC_O_TRUNC (1 << 3)

/**
 * Indicates that the filesystem supports lookups of "." and "..".
 *
 * This feature is disabled by default.
 */
#define FUSE_CAP_EXPORT_SUPPORT (1 << 4)

/**
 * Indicates that the kernel should not apply the umask to the
 * file mode on create operations.
 *
 * This feature is disabled by default.
 */
#define FUSE_CAP_DONT_MASK (1 << 6)

/**
 * Indicates that libfuse should try to use splice() when writing to
 * the fuse device. This may improve performance.
 *
 * This feature is disabled by default.
 */
#define FUSE_CAP_SPLICE_WRITE (1 << 7)

/**
 * Indicates that libfuse should try to move pages instead of copying when
 * writing to / reading from the fuse device. This may improve performance.
 *
 * This feature is disabled by default.
 */
#define FUSE_CAP_SPLICE_MOVE (1 << 8)

/**
 * Indicates that libfuse should try to use splice() when reading from
 * the fuse device. This may improve performance.
 *
 * This feature is enabled by default when supported by the kernel and
 * if the filesystem implements a write_buf() handler.
 */
#define FUSE_CAP_SPLICE_READ (1 << 9)

/**
 * If set, the calls to flock(2) will be emulated using POSIX locks and must
 * then be handled by the filesystem's setlock() handler.
 *
 * If not set, flock(2) calls will be handled by the FUSE kernel module
 * internally (so any access that does not go through the kernel cannot be taken
 * into account).
 *
 * This feature is enabled by default when supported by the kernel and
 * if the filesystem implements a flock() handler.
 */
#define FUSE_CAP_FLOCK_LOCKS (1 << 10)

/**
 * Indicates that the filesystem supports ioctl's on directories.
 *
 * This feature is enabled by default when supported by the kernel.
 */
#define FUSE_CAP_IOCTL_DIR (1 << 11)

/**
 * Traditionally, while a file is open the FUSE kernel module only
 * asks the filesystem for an update of the file's attributes when a
 * client attempts to read beyond EOF. This is unsuitable for
 * e.g. network filesystems, where the file contents may change
 * without the kernel knowing about it.
 *
 * If this flag is set, FUSE will check the validity of the attributes
 * on every read. If the attributes are no longer valid (i.e., if the
 * *attr_timeout* passed to fuse_reply_attr() or set in `struct
 * fuse_entry_param` has passed), it will first issue a `getattr`
 * request. If the new mtime differs from the previous value, any
 * cached file *contents* will be invalidated as well.
 *
 * This flag should always be set when available. If all file changes
 * go through the kernel, *attr_timeout* should be set to a very large
 * number to avoid unnecessary getattr() calls.
 *
 * This feature is enabled by default when supported by the kernel.
 */
#define FUSE_CAP_AUTO_INVAL_DATA (1 << 12)

/**
 * Indicates that the filesystem supports readdirplus.
 *
 * This feature is enabled by default when supported by the kernel and if the
 * filesystem implements a readdirplus() handler.
 */
#define FUSE_CAP_READDIRPLUS (1 << 13)

/**
 * Indicates that the filesystem supports adaptive readdirplus.
 *
 * If FUSE_CAP_READDIRPLUS is not set, this flag has no effect.
 *
 * If FUSE_CAP_READDIRPLUS is set and this flag is not set, the kernel
 * will always issue readdirplus() requests to retrieve directory
 * contents.
 *
 * If FUSE_CAP_READDIRPLUS is set and this flag is set, the kernel
 * will issue both readdir() and readdirplus() requests, depending on
 * how much information is expected to be required.
 *
 * As of Linux 4.20, the algorithm is as follows: when userspace
 * starts to read directory entries, issue a READDIRPLUS request to
 * the filesystem. If any entry attributes have been looked up by the
 * time userspace requests the next batch of entries continue with
 * READDIRPLUS, otherwise switch to plain READDIR.  This will reasult
 * in eg plain "ls" triggering READDIRPLUS first then READDIR after
 * that because it doesn't do lookups.  "ls -l" should result in all
 * READDIRPLUS, except if dentries are already cached.
 *
 * This feature is enabled by default when supported by the kernel and
 * if the filesystem implements both a readdirplus() and a readdir()
 * handler.
 */
#define FUSE_CAP_READDIRPLUS_AUTO (1 << 14)

/**
 * Indicates that the filesystem supports asynchronous direct I/O submission.
 *
 * If this capability is not requested/available, the kernel will ensure that
 * there is at most one pending read and one pending write request per direct
 * I/O file-handle at any time.
 *
 * This feature is enabled by default when supported by the kernel.
 */
#define FUSE_CAP_ASYNC_DIO (1 << 15)

/**
 * Indicates that writeback caching should be enabled. This means that
 * individual write request may be buffered and merged in the kernel
 * before they are send to the filesystem.
 *
 * This feature is disabled by default.
 */
#define FUSE_CAP_WRITEBACK_CACHE (1 << 16)

/**
 * Indicates support for zero-message opens. If this flag is set in
 * the `capable` field of the `fuse_conn_info` structure, then the
 * filesystem may return `ENOSYS` from the open() handler to indicate
 * success. Further attempts to open files will be handled in the
 * kernel. (If this flag is not set, returning ENOSYS will be treated
 * as an error and signaled to the caller).
 *
 * Setting (or unsetting) this flag in the `want` field has *no
 * effect*.
 */
#define FUSE_CAP_NO_OPEN_SUPPORT (1 << 17)

/**
 * Indicates support for parallel directory operations. If this flag
 * is unset, the FUSE kernel module will ensure that lookup() and
 * readdir() requests are never issued concurrently for the same
 * directory.
 *
 * This feature is enabled by default when supported by the kernel.
 */
#define FUSE_CAP_PARALLEL_DIROPS (1 << 18)

/**
 * Indicates support for POSIX ACLs.
 *
 * If this feature is enabled, the kernel will cache and have
 * responsibility for enforcing ACLs. ACL will be stored as xattrs and
 * passed to userspace, which is responsible for updating the ACLs in
 * the filesystem, keeping the file mode in sync with the ACL, and
 * ensuring inheritance of default ACLs when new filesystem nodes are
 * created. Note that this requires that the file system is able to
 * parse and interpret the xattr representation of ACLs.
 *
 * Enabling this feature implicitly turns on the
 * ``default_permissions`` mount option (even if it was not passed to
 * mount(2)).
 *
 * This feature is disabled by default.
 */
#define FUSE_CAP_POSIX_ACL (1 << 19)

/**
 * Indicates that the filesystem is responsible for unsetting
 * setuid and setgid bits when a file is written, truncated, or
 * its owner is changed.
 *
 * This feature is enabled by default when supported by the kernel.
 */
#define FUSE_CAP_HANDLE_KILLPRIV (1 << 20)

/**
 * Indicates support for zero-message opendirs. If this flag is set in
 * the `capable` field of the `fuse_conn_info` structure, then the filesystem
 * may return `ENOSYS` from the opendir() handler to indicate success. Further
 * opendir and releasedir messages will be handled in the kernel. (If this
 * flag is not set, returning ENOSYS will be treated as an error and signalled
 * to the caller.)
 *
 * Setting (or unsetting) this flag in the `want` field has *no effect*.
 */
#define FUSE_CAP_NO_OPENDIR_SUPPORT (1 << 24)

/**
 * Indicates that the kernel supports the FUSE_ATTR_SUBMOUNT flag.
 *
 * Setting (or unsetting) this flag in the `want` field has *no effect*.
 */
#define FUSE_CAP_SUBMOUNTS (1 << 27)

/**
 * Indicates that the filesystem is responsible for clearing
 * security.capability xattr and clearing setuid and setgid bits. Following
 * are the rules.
 * - clear "security.capability" on write, truncate and chown unconditionally
 * - clear suid/sgid if following is true. Note, sgid is cleared only if
 *   group executable bit is set.
 *    o setattr has FATTR_SIZE and FATTR_KILL_SUIDGID set.
 *    o setattr has FATTR_UID or FATTR_GID
 *    o open has O_TRUNC and FUSE_OPEN_KILL_SUIDGID
 *    o create has O_TRUNC and FUSE_OPEN_KILL_SUIDGID flag set.
 *    o write has FUSE_WRITE_KILL_SUIDGID
 */
#define FUSE_CAP_HANDLE_KILLPRIV_V2 (1 << 28)

/**
 * Indicates that file server supports extended struct fuse_setxattr_in
 */
#define FUSE_CAP_SETXATTR_EXT (1 << 29)

/**
 * Indicates that file server supports creating file security context
 */
#define FUSE_CAP_SECURITY_CTX (1ULL << 32)

/**
 * Ioctl flags
 *
 * FUSE_IOCTL_COMPAT: 32bit compat ioctl on 64bit machine
 * FUSE_IOCTL_UNRESTRICTED: not restricted to well-formed ioctls, retry allowed
 * FUSE_IOCTL_RETRY: retry with new iovecs
 * FUSE_IOCTL_DIR: is a directory
 *
 * FUSE_IOCTL_MAX_IOV: maximum of in_iovecs + out_iovecs
 */
#define FUSE_IOCTL_COMPAT (1 << 0)
#define FUSE_IOCTL_UNRESTRICTED (1 << 1)
#define FUSE_IOCTL_RETRY (1 << 2)
#define FUSE_IOCTL_DIR (1 << 4)

#define FUSE_IOCTL_MAX_IOV 256

/**
 * Connection information, passed to the ->init() method
 *
 * Some of the elements are read-write, these can be changed to
 * indicate the value requested by the filesystem.  The requested
 * value must usually be smaller than the indicated value.
 */
struct fuse_conn_info {
    /**
     * Major version of the protocol (read-only)
     */
    unsigned proto_major;

    /**
     * Minor version of the protocol (read-only)
     */
    unsigned proto_minor;

    /**
     * Maximum size of the write buffer
     */
    unsigned max_write;

    /**
     * Maximum size of read requests. A value of zero indicates no
     * limit. However, even if the filesystem does not specify a
     * limit, the maximum size of read requests will still be
     * limited by the kernel.
     *
     * NOTE: For the time being, the maximum size of read requests
     * must be set both here *and* passed to fuse_session_new()
     * using the ``-o max_read=<n>`` mount option. At some point
     * in the future, specifying the mount option will no longer
     * be necessary.
     */
    unsigned max_read;

    /**
     * Maximum readahead
     */
    unsigned max_readahead;

    /**
     * Capability flags that the kernel supports (read-only)
     */
    uint64_t capable;

    /**
     * Capability flags that the filesystem wants to enable.
     *
     * libfuse attempts to initialize this field with
     * reasonable default values before calling the init() handler.
     */
    uint64_t want;

    /**
     * Maximum number of pending "background" requests. A
     * background request is any type of request for which the
     * total number is not limited by other means. As of kernel
     * 4.8, only two types of requests fall into this category:
     *
     *   1. Read-ahead requests
     *   2. Asynchronous direct I/O requests
     *
     * Read-ahead requests are generated (if max_readahead is
     * non-zero) by the kernel to preemptively fill its caches
     * when it anticipates that userspace will soon read more
     * data.
     *
     * Asynchronous direct I/O requests are generated if
     * FUSE_CAP_ASYNC_DIO is enabled and userspace submits a large
     * direct I/O request. In this case the kernel will internally
     * split it up into multiple smaller requests and submit them
     * to the filesystem concurrently.
     *
     * Note that the following requests are *not* background
     * requests: writeback requests (limited by the kernel's
     * flusher algorithm), regular (i.e., synchronous and
     * buffered) userspace read/write requests (limited to one per
     * thread), asynchronous read requests (Linux's io_submit(2)
     * call actually blocks, so these are also limited to one per
     * thread).
     */
    unsigned max_background;

    /**
     * Kernel congestion threshold parameter. If the number of pending
     * background requests exceeds this number, the FUSE kernel module will
     * mark the filesystem as "congested". This instructs the kernel to
     * expect that queued requests will take some time to complete, and to
     * adjust its algorithms accordingly (e.g. by putting a waiting thread
     * to sleep instead of using a busy-loop).
     */
    unsigned congestion_threshold;

    /**
     * When FUSE_CAP_WRITEBACK_CACHE is enabled, the kernel is responsible
     * for updating mtime and ctime when write requests are received. The
     * updated values are passed to the filesystem with setattr() requests.
     * However, if the filesystem does not support the full resolution of
     * the kernel timestamps (nanoseconds), the mtime and ctime values used
     * by kernel and filesystem will differ (and result in an apparent
     * change of times after a cache flush).
     *
     * To prevent this problem, this variable can be used to inform the
     * kernel about the timestamp granularity supported by the file-system.
     * The value should be power of 10.  The default is 1, i.e. full
     * nano-second resolution. Filesystems supporting only second resolution
     * should set this to 1000000000.
     */
    unsigned time_gran;

    /**
     * For future use.
     */
    unsigned reserved[22];
};

struct fuse_session;
struct fuse_pollhandle;
struct fuse_conn_info_opts;

/**
 * This function parses several command-line options that can be used
 * to override elements of struct fuse_conn_info. The pointer returned
 * by this function should be passed to the
 * fuse_apply_conn_info_opts() method by the file system's init()
 * handler.
 *
 * Before using this function, think twice if you really want these
 * parameters to be adjustable from the command line. In most cases,
 * they should be determined by the file system internally.
 *
 * The following options are recognized:
 *
 *   -o max_write=N         sets conn->max_write
 *   -o max_readahead=N     sets conn->max_readahead
 *   -o max_background=N    sets conn->max_background
 *   -o congestion_threshold=N  sets conn->congestion_threshold
 *   -o async_read          sets FUSE_CAP_ASYNC_READ in conn->want
 *   -o sync_read           unsets FUSE_CAP_ASYNC_READ in conn->want
 *   -o atomic_o_trunc      sets FUSE_CAP_ATOMIC_O_TRUNC in conn->want
 *   -o no_remote_lock      Equivalent to -o
 *no_remote_flock,no_remote_posix_lock -o no_remote_flock     Unsets
 *FUSE_CAP_FLOCK_LOCKS in conn->want -o no_remote_posix_lock  Unsets
 *FUSE_CAP_POSIX_LOCKS in conn->want -o [no_]splice_write     (un-)sets
 *FUSE_CAP_SPLICE_WRITE in conn->want -o [no_]splice_move      (un-)sets
 *FUSE_CAP_SPLICE_MOVE in conn->want -o [no_]splice_read      (un-)sets
 *FUSE_CAP_SPLICE_READ in conn->want -o [no_]auto_inval_data  (un-)sets
 *FUSE_CAP_AUTO_INVAL_DATA in conn->want -o readdirplus=no        unsets
 *FUSE_CAP_READDIRPLUS in conn->want -o readdirplus=yes       sets
 *FUSE_CAP_READDIRPLUS and unsets FUSE_CAP_READDIRPLUS_AUTO in conn->want -o
 *readdirplus=auto      sets FUSE_CAP_READDIRPLUS and FUSE_CAP_READDIRPLUS_AUTO
 *in conn->want -o [no_]async_dio        (un-)sets FUSE_CAP_ASYNC_DIO in
 *conn->want -o [no_]writeback_cache  (un-)sets FUSE_CAP_WRITEBACK_CACHE in
 *conn->want -o time_gran=N           sets conn->time_gran
 *
 * Known options will be removed from *args*, unknown options will be
 * passed through unchanged.
 *
 * @param args argument vector (input+output)
 * @return parsed options
 **/
struct fuse_conn_info_opts *fuse_parse_conn_info_opts(struct fuse_args *args);

/**
 * This function applies the (parsed) parameters in *opts* to the
 * *conn* pointer. It may modify the following fields: wants,
 * max_write, max_readahead, congestion_threshold, max_background,
 * time_gran. A field is only set (or unset) if the corresponding
 * option has been explicitly set.
 */
void fuse_apply_conn_info_opts(struct fuse_conn_info_opts *opts,
                               struct fuse_conn_info *conn);

/**
 * Go into the background
 *
 * @param foreground if true, stay in the foreground
 * @return 0 on success, -1 on failure
 */
int fuse_daemonize(int foreground);

/**
 * Get the version of the library
 *
 * @return the version
 */
int fuse_version(void);

/**
 * Get the full package version string of the library
 *
 * @return the package version
 */
const char *fuse_pkgversion(void);

/**
 * Destroy poll handle
 *
 * @param ph the poll handle
 */
void fuse_pollhandle_destroy(struct fuse_pollhandle *ph);

/*
 * Data buffer
 */

/**
 * Buffer flags
 */
enum fuse_buf_flags {
    /**
     * Buffer contains a file descriptor
     *
     * If this flag is set, the .fd field is valid, otherwise the
     * .mem fields is valid.
     */
    FUSE_BUF_IS_FD = (1 << 1),

    /**
     * Seek on the file descriptor
     *
     * If this flag is set then the .pos field is valid and is
     * used to seek to the given offset before performing
     * operation on file descriptor.
     */
    FUSE_BUF_FD_SEEK = (1 << 2),

    /**
     * Retry operation on file descriptor
     *
     * If this flag is set then retry operation on file descriptor
     * until .size bytes have been copied or an error or EOF is
     * detected.
     */
    FUSE_BUF_FD_RETRY = (1 << 3),
};

/**
 * Single data buffer
 *
 * Generic data buffer for I/O, extended attributes, etc...  Data may
 * be supplied as a memory pointer or as a file descriptor
 */
struct fuse_buf {
    /**
     * Size of data in bytes
     */
    size_t size;

    /**
     * Buffer flags
     */
    enum fuse_buf_flags flags;

    /**
     * Memory pointer
     *
     * Used unless FUSE_BUF_IS_FD flag is set.
     */
    void *mem;

    /**
     * File descriptor
     *
     * Used if FUSE_BUF_IS_FD flag is set.
     */
    int fd;

    /**
     * File position
     *
     * Used if FUSE_BUF_FD_SEEK flag is set.
     */
    off_t pos;
};

/**
 * Data buffer vector
 *
 * An array of data buffers, each containing a memory pointer or a
 * file descriptor.
 *
 * Allocate dynamically to add more than one buffer.
 */
struct fuse_bufvec {
    /**
     * Number of buffers in the array
     */
    size_t count;

    /**
     * Index of current buffer within the array
     */
    size_t idx;

    /**
     * Current offset within the current buffer
     */
    size_t off;

    /**
     * Array of buffers
     */
    struct fuse_buf buf[1];
};

/* Initialize bufvec with a single buffer of given size */
#define FUSE_BUFVEC_INIT(size__)                                      \
    ((struct fuse_bufvec){ /* .count= */ 1,                           \
                           /* .idx =  */ 0,                           \
                           /* .off =  */ 0, /* .buf =  */             \
                           { /* [0] = */ {                            \
                               /* .size =  */ (size__),               \
                               /* .flags = */ (enum fuse_buf_flags)0, \
                               /* .mem =   */ NULL,                   \
                               /* .fd =    */ -1,                     \
                               /* .pos =   */ 0,                      \
                           } } })

/**
 * Get total size of data in a fuse buffer vector
 *
 * @param bufv buffer vector
 * @return size of data
 */
size_t fuse_buf_size(const struct fuse_bufvec *bufv);

/**
 * Copy data from one buffer vector to another
 *
 * @param dst destination buffer vector
 * @param src source buffer vector
 * @return actual number of bytes copied or -errno on error
 */
ssize_t fuse_buf_copy(struct fuse_bufvec *dst, struct fuse_bufvec *src);

/**
 * Memory buffer iterator
 *
 */
struct fuse_mbuf_iter {
    /**
     * Data pointer
     */
    void *mem;

    /**
     * Total length, in bytes
     */
    size_t size;

    /**
     * Offset from start of buffer
     */
    size_t pos;
};

/* Initialize memory buffer iterator from a fuse_buf */
#define FUSE_MBUF_ITER_INIT(fbuf) \
    ((struct fuse_mbuf_iter){     \
        .mem = fbuf->mem,         \
        .size = fbuf->size,       \
        .pos = 0,                 \
    })

/**
 * Consume bytes from a memory buffer iterator
 *
 * @param iter memory buffer iterator
 * @param len number of bytes to consume
 * @return pointer to start of consumed bytes or
 *         NULL if advancing beyond end of buffer
 */
void *fuse_mbuf_iter_advance(struct fuse_mbuf_iter *iter, size_t len);

/**
 * Consume a NUL-terminated string from a memory buffer iterator
 *
 * @param iter memory buffer iterator
 * @return pointer to the string or
 *         NULL if advancing beyond end of buffer or there is no NUL-terminator
 */
const char *fuse_mbuf_iter_advance_str(struct fuse_mbuf_iter *iter);

/*
 * Signal handling
 */
/**
 * Exit session on HUP, TERM and INT signals and ignore PIPE signal
 *
 * Stores session in a global variable. May only be called once per
 * process until fuse_remove_signal_handlers() is called.
 *
 * Once either of the POSIX signals arrives, the signal handler calls
 * fuse_session_exit().
 *
 * @param se the session to exit
 * @return 0 on success, -1 on failure
 *
 * See also:
 * fuse_remove_signal_handlers()
 */
int fuse_set_signal_handlers(struct fuse_session *se);

/**
 * Restore default signal handlers
 *
 * Resets global session.  After this fuse_set_signal_handlers() may
 * be called again.
 *
 * @param se the same session as given in fuse_set_signal_handlers()
 *
 * See also:
 * fuse_set_signal_handlers()
 */
void fuse_remove_signal_handlers(struct fuse_session *se);

/*
 * Compatibility stuff
 */

#if !defined(FUSE_USE_VERSION) || FUSE_USE_VERSION < 30
#error only API version 30 or greater is supported
#endif


/*
 * This interface uses 64 bit off_t.
 *
 * On 32bit systems please add -D_FILE_OFFSET_BITS=64 to your compile flags!
 */
QEMU_BUILD_BUG_ON(sizeof(off_t) != 8);

#endif /* FUSE_COMMON_H_ */
