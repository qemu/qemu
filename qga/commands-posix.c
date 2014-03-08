/*
 * QEMU Guest Agent POSIX-specific command implementations
 *
 * Copyright IBM Corp. 2011
 *
 * Authors:
 *  Michael Roth      <mdroth@linux.vnet.ibm.com>
 *  Michal Privoznik  <mprivozn@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <glib.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <inttypes.h>
#include "qga/guest-agent-core.h"
#include "qga-qmp-commands.h"
#include "qapi/qmp/qerror.h"
#include "qemu/queue.h"
#include "qemu/host-utils.h"

#ifndef CONFIG_HAS_ENVIRON
#ifdef __APPLE__
#include <crt_externs.h>
#define environ (*_NSGetEnviron())
#else
extern char **environ;
#endif
#endif

#if defined(__linux__)
#include <mntent.h>
#include <linux/fs.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <net/if.h>

#ifdef FIFREEZE
#define CONFIG_FSFREEZE
#endif
#ifdef FITRIM
#define CONFIG_FSTRIM
#endif
#endif

static void ga_wait_child(pid_t pid, int *status, Error **err)
{
    pid_t rpid;

    *status = 0;

    do {
        rpid = waitpid(pid, status, 0);
    } while (rpid == -1 && errno == EINTR);

    if (rpid == -1) {
        error_setg_errno(err, errno, "failed to wait for child (pid: %d)", pid);
        return;
    }

    g_assert(rpid == pid);
}

void qmp_guest_shutdown(bool has_mode, const char *mode, Error **err)
{
    const char *shutdown_flag;
    Error *local_err = NULL;
    pid_t pid;
    int status;

    slog("guest-shutdown called, mode: %s", mode);
    if (!has_mode || strcmp(mode, "powerdown") == 0) {
        shutdown_flag = "-P";
    } else if (strcmp(mode, "halt") == 0) {
        shutdown_flag = "-H";
    } else if (strcmp(mode, "reboot") == 0) {
        shutdown_flag = "-r";
    } else {
        error_setg(err,
                   "mode is invalid (valid values are: halt|powerdown|reboot");
        return;
    }

    pid = fork();
    if (pid == 0) {
        /* child, start the shutdown */
        setsid();
        reopen_fd_to_null(0);
        reopen_fd_to_null(1);
        reopen_fd_to_null(2);

        execle("/sbin/shutdown", "shutdown", "-h", shutdown_flag, "+0",
               "hypervisor initiated shutdown", (char*)NULL, environ);
        _exit(EXIT_FAILURE);
    } else if (pid < 0) {
        error_setg_errno(err, errno, "failed to create child process");
        return;
    }

    ga_wait_child(pid, &status, &local_err);
    if (local_err) {
        error_propagate(err, local_err);
        return;
    }

    if (!WIFEXITED(status)) {
        error_setg(err, "child process has terminated abnormally");
        return;
    }

    if (WEXITSTATUS(status)) {
        error_setg(err, "child process has failed to shutdown");
        return;
    }

    /* succeeded */
}

int64_t qmp_guest_get_time(Error **errp)
{
   int ret;
   qemu_timeval tq;
   int64_t time_ns;

   ret = qemu_gettimeofday(&tq);
   if (ret < 0) {
       error_setg_errno(errp, errno, "Failed to get time");
       return -1;
   }

   time_ns = tq.tv_sec * 1000000000LL + tq.tv_usec * 1000;
   return time_ns;
}

void qmp_guest_set_time(bool has_time, int64_t time_ns, Error **errp)
{
    int ret;
    int status;
    pid_t pid;
    Error *local_err = NULL;
    struct timeval tv;

    /* If user has passed a time, validate and set it. */
    if (has_time) {
        /* year-2038 will overflow in case time_t is 32bit */
        if (time_ns / 1000000000 != (time_t)(time_ns / 1000000000)) {
            error_setg(errp, "Time %" PRId64 " is too large", time_ns);
            return;
        }

        tv.tv_sec = time_ns / 1000000000;
        tv.tv_usec = (time_ns % 1000000000) / 1000;

        ret = settimeofday(&tv, NULL);
        if (ret < 0) {
            error_setg_errno(errp, errno, "Failed to set time to guest");
            return;
        }
    }

    /* Now, if user has passed a time to set and the system time is set, we
     * just need to synchronize the hardware clock. However, if no time was
     * passed, user is requesting the opposite: set the system time from the
     * hardware clock. */
    pid = fork();
    if (pid == 0) {
        setsid();
        reopen_fd_to_null(0);
        reopen_fd_to_null(1);
        reopen_fd_to_null(2);

        /* Use '/sbin/hwclock -w' to set RTC from the system time,
         * or '/sbin/hwclock -s' to set the system time from RTC. */
        execle("/sbin/hwclock", "hwclock", has_time ? "-w" : "-s",
               NULL, environ);
        _exit(EXIT_FAILURE);
    } else if (pid < 0) {
        error_setg_errno(errp, errno, "failed to create child process");
        return;
    }

    ga_wait_child(pid, &status, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (!WIFEXITED(status)) {
        error_setg(errp, "child process has terminated abnormally");
        return;
    }

    if (WEXITSTATUS(status)) {
        error_setg(errp, "hwclock failed to set hardware clock to system time");
        return;
    }
}

typedef struct GuestFileHandle {
    uint64_t id;
    FILE *fh;
    QTAILQ_ENTRY(GuestFileHandle) next;
} GuestFileHandle;

static struct {
    QTAILQ_HEAD(, GuestFileHandle) filehandles;
} guest_file_state;

static int64_t guest_file_handle_add(FILE *fh, Error **errp)
{
    GuestFileHandle *gfh;
    int64_t handle;

    handle = ga_get_fd_handle(ga_state, errp);
    if (error_is_set(errp)) {
        return 0;
    }

    gfh = g_malloc0(sizeof(GuestFileHandle));
    gfh->id = handle;
    gfh->fh = fh;
    QTAILQ_INSERT_TAIL(&guest_file_state.filehandles, gfh, next);

    return handle;
}

static GuestFileHandle *guest_file_handle_find(int64_t id, Error **err)
{
    GuestFileHandle *gfh;

    QTAILQ_FOREACH(gfh, &guest_file_state.filehandles, next)
    {
        if (gfh->id == id) {
            return gfh;
        }
    }

    error_setg(err, "handle '%" PRId64 "' has not been found", id);
    return NULL;
}

typedef const char * const ccpc;

#ifndef O_BINARY
#define O_BINARY 0
#endif

/* http://pubs.opengroup.org/onlinepubs/9699919799/functions/fopen.html */
static const struct {
    ccpc *forms;
    int oflag_base;
} guest_file_open_modes[] = {
    { (ccpc[]){ "r",          NULL }, O_RDONLY                                 },
    { (ccpc[]){ "rb",         NULL }, O_RDONLY                      | O_BINARY },
    { (ccpc[]){ "w",          NULL }, O_WRONLY | O_CREAT | O_TRUNC             },
    { (ccpc[]){ "wb",         NULL }, O_WRONLY | O_CREAT | O_TRUNC  | O_BINARY },
    { (ccpc[]){ "a",          NULL }, O_WRONLY | O_CREAT | O_APPEND            },
    { (ccpc[]){ "ab",         NULL }, O_WRONLY | O_CREAT | O_APPEND | O_BINARY },
    { (ccpc[]){ "r+",         NULL }, O_RDWR                                   },
    { (ccpc[]){ "rb+", "r+b", NULL }, O_RDWR                        | O_BINARY },
    { (ccpc[]){ "w+",         NULL }, O_RDWR   | O_CREAT | O_TRUNC             },
    { (ccpc[]){ "wb+", "w+b", NULL }, O_RDWR   | O_CREAT | O_TRUNC  | O_BINARY },
    { (ccpc[]){ "a+",         NULL }, O_RDWR   | O_CREAT | O_APPEND            },
    { (ccpc[]){ "ab+", "a+b", NULL }, O_RDWR   | O_CREAT | O_APPEND | O_BINARY }
};

static int
find_open_flag(const char *mode_str, Error **err)
{
    unsigned mode;

    for (mode = 0; mode < ARRAY_SIZE(guest_file_open_modes); ++mode) {
        ccpc *form;

        form = guest_file_open_modes[mode].forms;
        while (*form != NULL && strcmp(*form, mode_str) != 0) {
            ++form;
        }
        if (*form != NULL) {
            break;
        }
    }

    if (mode == ARRAY_SIZE(guest_file_open_modes)) {
        error_setg(err, "invalid file open mode '%s'", mode_str);
        return -1;
    }
    return guest_file_open_modes[mode].oflag_base | O_NOCTTY | O_NONBLOCK;
}

#define DEFAULT_NEW_FILE_MODE (S_IRUSR | S_IWUSR | \
                               S_IRGRP | S_IWGRP | \
                               S_IROTH | S_IWOTH)

static FILE *
safe_open_or_create(const char *path, const char *mode, Error **err)
{
    Error *local_err = NULL;
    int oflag;

    oflag = find_open_flag(mode, &local_err);
    if (local_err == NULL) {
        int fd;

        /* If the caller wants / allows creation of a new file, we implement it
         * with a two step process: open() + (open() / fchmod()).
         *
         * First we insist on creating the file exclusively as a new file. If
         * that succeeds, we're free to set any file-mode bits on it. (The
         * motivation is that we want to set those file-mode bits independently
         * of the current umask.)
         *
         * If the exclusive creation fails because the file already exists
         * (EEXIST is not possible for any other reason), we just attempt to
         * open the file, but in this case we won't be allowed to change the
         * file-mode bits on the preexistent file.
         *
         * The pathname should never disappear between the two open()s in
         * practice. If it happens, then someone very likely tried to race us.
         * In this case just go ahead and report the ENOENT from the second
         * open() to the caller.
         *
         * If the caller wants to open a preexistent file, then the first
         * open() is decisive and its third argument is ignored, and the second
         * open() and the fchmod() are never called.
         */
        fd = open(path, oflag | ((oflag & O_CREAT) ? O_EXCL : 0), 0);
        if (fd == -1 && errno == EEXIST) {
            oflag &= ~(unsigned)O_CREAT;
            fd = open(path, oflag);
        }

        if (fd == -1) {
            error_setg_errno(&local_err, errno, "failed to open file '%s' "
                             "(mode: '%s')", path, mode);
        } else {
            qemu_set_cloexec(fd);

            if ((oflag & O_CREAT) && fchmod(fd, DEFAULT_NEW_FILE_MODE) == -1) {
                error_setg_errno(&local_err, errno, "failed to set permission "
                                 "0%03o on new file '%s' (mode: '%s')",
                                 (unsigned)DEFAULT_NEW_FILE_MODE, path, mode);
            } else {
                FILE *f;

                f = fdopen(fd, mode);
                if (f == NULL) {
                    error_setg_errno(&local_err, errno, "failed to associate "
                                     "stdio stream with file descriptor %d, "
                                     "file '%s' (mode: '%s')", fd, path, mode);
                } else {
                    return f;
                }
            }

            close(fd);
            if (oflag & O_CREAT) {
                unlink(path);
            }
        }
    }

    error_propagate(err, local_err);
    return NULL;
}

int64_t qmp_guest_file_open(const char *path, bool has_mode, const char *mode, Error **err)
{
    FILE *fh;
    Error *local_err = NULL;
    int fd;
    int64_t ret = -1, handle;

    if (!has_mode) {
        mode = "r";
    }
    slog("guest-file-open called, filepath: %s, mode: %s", path, mode);
    fh = safe_open_or_create(path, mode, &local_err);
    if (local_err != NULL) {
        error_propagate(err, local_err);
        return -1;
    }

    /* set fd non-blocking to avoid common use cases (like reading from a
     * named pipe) from hanging the agent
     */
    fd = fileno(fh);
    ret = fcntl(fd, F_GETFL);
    ret = fcntl(fd, F_SETFL, ret | O_NONBLOCK);
    if (ret == -1) {
        error_setg_errno(err, errno, "failed to make file '%s' non-blocking",
                         path);
        fclose(fh);
        return -1;
    }

    handle = guest_file_handle_add(fh, err);
    if (error_is_set(err)) {
        fclose(fh);
        return -1;
    }

    slog("guest-file-open, handle: %" PRId64, handle);
    return handle;
}

void qmp_guest_file_close(int64_t handle, Error **err)
{
    GuestFileHandle *gfh = guest_file_handle_find(handle, err);
    int ret;

    slog("guest-file-close called, handle: %" PRId64, handle);
    if (!gfh) {
        return;
    }

    ret = fclose(gfh->fh);
    if (ret == EOF) {
        error_setg_errno(err, errno, "failed to close handle");
        return;
    }

    QTAILQ_REMOVE(&guest_file_state.filehandles, gfh, next);
    g_free(gfh);
}

struct GuestFileRead *qmp_guest_file_read(int64_t handle, bool has_count,
                                          int64_t count, Error **err)
{
    GuestFileHandle *gfh = guest_file_handle_find(handle, err);
    GuestFileRead *read_data = NULL;
    guchar *buf;
    FILE *fh;
    size_t read_count;

    if (!gfh) {
        return NULL;
    }

    if (!has_count) {
        count = QGA_READ_COUNT_DEFAULT;
    } else if (count < 0) {
        error_setg(err, "value '%" PRId64 "' is invalid for argument count",
                   count);
        return NULL;
    }

    fh = gfh->fh;
    buf = g_malloc0(count+1);
    read_count = fread(buf, 1, count, fh);
    if (ferror(fh)) {
        error_setg_errno(err, errno, "failed to read file");
        slog("guest-file-read failed, handle: %" PRId64, handle);
    } else {
        buf[read_count] = 0;
        read_data = g_malloc0(sizeof(GuestFileRead));
        read_data->count = read_count;
        read_data->eof = feof(fh);
        if (read_count) {
            read_data->buf_b64 = g_base64_encode(buf, read_count);
        }
    }
    g_free(buf);
    clearerr(fh);

    return read_data;
}

GuestFileWrite *qmp_guest_file_write(int64_t handle, const char *buf_b64,
                                     bool has_count, int64_t count, Error **err)
{
    GuestFileWrite *write_data = NULL;
    guchar *buf;
    gsize buf_len;
    int write_count;
    GuestFileHandle *gfh = guest_file_handle_find(handle, err);
    FILE *fh;

    if (!gfh) {
        return NULL;
    }

    fh = gfh->fh;
    buf = g_base64_decode(buf_b64, &buf_len);

    if (!has_count) {
        count = buf_len;
    } else if (count < 0 || count > buf_len) {
        error_setg(err, "value '%" PRId64 "' is invalid for argument count",
                   count);
        g_free(buf);
        return NULL;
    }

    write_count = fwrite(buf, 1, count, fh);
    if (ferror(fh)) {
        error_setg_errno(err, errno, "failed to write to file");
        slog("guest-file-write failed, handle: %" PRId64, handle);
    } else {
        write_data = g_malloc0(sizeof(GuestFileWrite));
        write_data->count = write_count;
        write_data->eof = feof(fh);
    }
    g_free(buf);
    clearerr(fh);

    return write_data;
}

struct GuestFileSeek *qmp_guest_file_seek(int64_t handle, int64_t offset,
                                          int64_t whence, Error **err)
{
    GuestFileHandle *gfh = guest_file_handle_find(handle, err);
    GuestFileSeek *seek_data = NULL;
    FILE *fh;
    int ret;

    if (!gfh) {
        return NULL;
    }

    fh = gfh->fh;
    ret = fseek(fh, offset, whence);
    if (ret == -1) {
        error_setg_errno(err, errno, "failed to seek file");
    } else {
        seek_data = g_new0(GuestFileSeek, 1);
        seek_data->position = ftell(fh);
        seek_data->eof = feof(fh);
    }
    clearerr(fh);

    return seek_data;
}

void qmp_guest_file_flush(int64_t handle, Error **err)
{
    GuestFileHandle *gfh = guest_file_handle_find(handle, err);
    FILE *fh;
    int ret;

    if (!gfh) {
        return;
    }

    fh = gfh->fh;
    ret = fflush(fh);
    if (ret == EOF) {
        error_setg_errno(err, errno, "failed to flush file");
    }
}

static void guest_file_init(void)
{
    QTAILQ_INIT(&guest_file_state.filehandles);
}

/* linux-specific implementations. avoid this if at all possible. */
#if defined(__linux__)

#if defined(CONFIG_FSFREEZE) || defined(CONFIG_FSTRIM)
typedef struct FsMount {
    char *dirname;
    char *devtype;
    QTAILQ_ENTRY(FsMount) next;
} FsMount;

typedef QTAILQ_HEAD(FsMountList, FsMount) FsMountList;

static void free_fs_mount_list(FsMountList *mounts)
{
     FsMount *mount, *temp;

     if (!mounts) {
         return;
     }

     QTAILQ_FOREACH_SAFE(mount, mounts, next, temp) {
         QTAILQ_REMOVE(mounts, mount, next);
         g_free(mount->dirname);
         g_free(mount->devtype);
         g_free(mount);
     }
}

/*
 * Walk the mount table and build a list of local file systems
 */
static void build_fs_mount_list(FsMountList *mounts, Error **err)
{
    struct mntent *ment;
    FsMount *mount;
    char const *mtab = "/proc/self/mounts";
    FILE *fp;

    fp = setmntent(mtab, "r");
    if (!fp) {
        error_setg(err, "failed to open mtab file: '%s'", mtab);
        return;
    }

    while ((ment = getmntent(fp))) {
        /*
         * An entry which device name doesn't start with a '/' is
         * either a dummy file system or a network file system.
         * Add special handling for smbfs and cifs as is done by
         * coreutils as well.
         */
        if ((ment->mnt_fsname[0] != '/') ||
            (strcmp(ment->mnt_type, "smbfs") == 0) ||
            (strcmp(ment->mnt_type, "cifs") == 0)) {
            continue;
        }

        mount = g_malloc0(sizeof(FsMount));
        mount->dirname = g_strdup(ment->mnt_dir);
        mount->devtype = g_strdup(ment->mnt_type);

        QTAILQ_INSERT_TAIL(mounts, mount, next);
    }

    endmntent(fp);
}
#endif

#if defined(CONFIG_FSFREEZE)

typedef enum {
    FSFREEZE_HOOK_THAW = 0,
    FSFREEZE_HOOK_FREEZE,
} FsfreezeHookArg;

const char *fsfreeze_hook_arg_string[] = {
    "thaw",
    "freeze",
};

static void execute_fsfreeze_hook(FsfreezeHookArg arg, Error **err)
{
    int status;
    pid_t pid;
    const char *hook;
    const char *arg_str = fsfreeze_hook_arg_string[arg];
    Error *local_err = NULL;

    hook = ga_fsfreeze_hook(ga_state);
    if (!hook) {
        return;
    }
    if (access(hook, X_OK) != 0) {
        error_setg_errno(err, errno, "can't access fsfreeze hook '%s'", hook);
        return;
    }

    slog("executing fsfreeze hook with arg '%s'", arg_str);
    pid = fork();
    if (pid == 0) {
        setsid();
        reopen_fd_to_null(0);
        reopen_fd_to_null(1);
        reopen_fd_to_null(2);

        execle(hook, hook, arg_str, NULL, environ);
        _exit(EXIT_FAILURE);
    } else if (pid < 0) {
        error_setg_errno(err, errno, "failed to create child process");
        return;
    }

    ga_wait_child(pid, &status, &local_err);
    if (local_err) {
        error_propagate(err, local_err);
        return;
    }

    if (!WIFEXITED(status)) {
        error_setg(err, "fsfreeze hook has terminated abnormally");
        return;
    }

    status = WEXITSTATUS(status);
    if (status) {
        error_setg(err, "fsfreeze hook has failed with status %d", status);
        return;
    }
}

/*
 * Return status of freeze/thaw
 */
GuestFsfreezeStatus qmp_guest_fsfreeze_status(Error **err)
{
    if (ga_is_frozen(ga_state)) {
        return GUEST_FSFREEZE_STATUS_FROZEN;
    }

    return GUEST_FSFREEZE_STATUS_THAWED;
}

/*
 * Walk list of mounted file systems in the guest, and freeze the ones which
 * are real local file systems.
 */
int64_t qmp_guest_fsfreeze_freeze(Error **err)
{
    int ret = 0, i = 0;
    FsMountList mounts;
    struct FsMount *mount;
    Error *local_err = NULL;
    int fd;

    slog("guest-fsfreeze called");

    execute_fsfreeze_hook(FSFREEZE_HOOK_FREEZE, &local_err);
    if (local_err) {
        error_propagate(err, local_err);
        return -1;
    }

    QTAILQ_INIT(&mounts);
    build_fs_mount_list(&mounts, &local_err);
    if (local_err) {
        error_propagate(err, local_err);
        return -1;
    }

    /* cannot risk guest agent blocking itself on a write in this state */
    ga_set_frozen(ga_state);

    QTAILQ_FOREACH_REVERSE(mount, &mounts, FsMountList, next) {
        fd = qemu_open(mount->dirname, O_RDONLY);
        if (fd == -1) {
            error_setg_errno(err, errno, "failed to open %s", mount->dirname);
            goto error;
        }

        /* we try to cull filesytems we know won't work in advance, but other
         * filesytems may not implement fsfreeze for less obvious reasons.
         * these will report EOPNOTSUPP. we simply ignore these when tallying
         * the number of frozen filesystems.
         *
         * any other error means a failure to freeze a filesystem we
         * expect to be freezable, so return an error in those cases
         * and return system to thawed state.
         */
        ret = ioctl(fd, FIFREEZE);
        if (ret == -1) {
            if (errno != EOPNOTSUPP) {
                error_setg_errno(err, errno, "failed to freeze %s",
                                 mount->dirname);
                close(fd);
                goto error;
            }
        } else {
            i++;
        }
        close(fd);
    }

    free_fs_mount_list(&mounts);
    return i;

error:
    free_fs_mount_list(&mounts);
    qmp_guest_fsfreeze_thaw(NULL);
    return 0;
}

/*
 * Walk list of frozen file systems in the guest, and thaw them.
 */
int64_t qmp_guest_fsfreeze_thaw(Error **err)
{
    int ret;
    FsMountList mounts;
    FsMount *mount;
    int fd, i = 0, logged;
    Error *local_err = NULL;

    QTAILQ_INIT(&mounts);
    build_fs_mount_list(&mounts, &local_err);
    if (local_err) {
        error_propagate(err, local_err);
        return 0;
    }

    QTAILQ_FOREACH(mount, &mounts, next) {
        logged = false;
        fd = qemu_open(mount->dirname, O_RDONLY);
        if (fd == -1) {
            continue;
        }
        /* we have no way of knowing whether a filesystem was actually unfrozen
         * as a result of a successful call to FITHAW, only that if an error
         * was returned the filesystem was *not* unfrozen by that particular
         * call.
         *
         * since multiple preceding FIFREEZEs require multiple calls to FITHAW
         * to unfreeze, continuing issuing FITHAW until an error is returned,
         * in which case either the filesystem is in an unfreezable state, or,
         * more likely, it was thawed previously (and remains so afterward).
         *
         * also, since the most recent successful call is the one that did
         * the actual unfreeze, we can use this to provide an accurate count
         * of the number of filesystems unfrozen by guest-fsfreeze-thaw, which
         * may * be useful for determining whether a filesystem was unfrozen
         * during the freeze/thaw phase by a process other than qemu-ga.
         */
        do {
            ret = ioctl(fd, FITHAW);
            if (ret == 0 && !logged) {
                i++;
                logged = true;
            }
        } while (ret == 0);
        close(fd);
    }

    ga_unset_frozen(ga_state);
    free_fs_mount_list(&mounts);

    execute_fsfreeze_hook(FSFREEZE_HOOK_THAW, err);

    return i;
}

static void guest_fsfreeze_cleanup(void)
{
    Error *err = NULL;

    if (ga_is_frozen(ga_state) == GUEST_FSFREEZE_STATUS_FROZEN) {
        qmp_guest_fsfreeze_thaw(&err);
        if (err) {
            slog("failed to clean up frozen filesystems: %s",
                 error_get_pretty(err));
            error_free(err);
        }
    }
}
#endif /* CONFIG_FSFREEZE */

#if defined(CONFIG_FSTRIM)
/*
 * Walk list of mounted file systems in the guest, and trim them.
 */
void qmp_guest_fstrim(bool has_minimum, int64_t minimum, Error **err)
{
    int ret = 0;
    FsMountList mounts;
    struct FsMount *mount;
    int fd;
    Error *local_err = NULL;
    struct fstrim_range r = {
        .start = 0,
        .len = -1,
        .minlen = has_minimum ? minimum : 0,
    };

    slog("guest-fstrim called");

    QTAILQ_INIT(&mounts);
    build_fs_mount_list(&mounts, &local_err);
    if (local_err) {
        error_propagate(err, local_err);
        return;
    }

    QTAILQ_FOREACH(mount, &mounts, next) {
        fd = qemu_open(mount->dirname, O_RDONLY);
        if (fd == -1) {
            error_setg_errno(err, errno, "failed to open %s", mount->dirname);
            goto error;
        }

        /* We try to cull filesytems we know won't work in advance, but other
         * filesytems may not implement fstrim for less obvious reasons.  These
         * will report EOPNOTSUPP; we simply ignore these errors.  Any other
         * error means an unexpected error, so return it in those cases.  In
         * some other cases ENOTTY will be reported (e.g. CD-ROMs).
         */
        ret = ioctl(fd, FITRIM, &r);
        if (ret == -1) {
            if (errno != ENOTTY && errno != EOPNOTSUPP) {
                error_setg_errno(err, errno, "failed to trim %s",
                                 mount->dirname);
                close(fd);
                goto error;
            }
        }
        close(fd);
    }

error:
    free_fs_mount_list(&mounts);
}
#endif /* CONFIG_FSTRIM */


#define LINUX_SYS_STATE_FILE "/sys/power/state"
#define SUSPEND_SUPPORTED 0
#define SUSPEND_NOT_SUPPORTED 1

static void bios_supports_mode(const char *pmutils_bin, const char *pmutils_arg,
                               const char *sysfile_str, Error **err)
{
    Error *local_err = NULL;
    char *pmutils_path;
    pid_t pid;
    int status;

    pmutils_path = g_find_program_in_path(pmutils_bin);

    pid = fork();
    if (!pid) {
        char buf[32]; /* hopefully big enough */
        ssize_t ret;
        int fd;

        setsid();
        reopen_fd_to_null(0);
        reopen_fd_to_null(1);
        reopen_fd_to_null(2);

        if (pmutils_path) {
            execle(pmutils_path, pmutils_bin, pmutils_arg, NULL, environ);
        }

        /*
         * If we get here either pm-utils is not installed or execle() has
         * failed. Let's try the manual method if the caller wants it.
         */

        if (!sysfile_str) {
            _exit(SUSPEND_NOT_SUPPORTED);
        }

        fd = open(LINUX_SYS_STATE_FILE, O_RDONLY);
        if (fd < 0) {
            _exit(SUSPEND_NOT_SUPPORTED);
        }

        ret = read(fd, buf, sizeof(buf)-1);
        if (ret <= 0) {
            _exit(SUSPEND_NOT_SUPPORTED);
        }
        buf[ret] = '\0';

        if (strstr(buf, sysfile_str)) {
            _exit(SUSPEND_SUPPORTED);
        }

        _exit(SUSPEND_NOT_SUPPORTED);
    } else if (pid < 0) {
        error_setg_errno(err, errno, "failed to create child process");
        goto out;
    }

    ga_wait_child(pid, &status, &local_err);
    if (local_err) {
        error_propagate(err, local_err);
        goto out;
    }

    if (!WIFEXITED(status)) {
        error_setg(err, "child process has terminated abnormally");
        goto out;
    }

    switch (WEXITSTATUS(status)) {
    case SUSPEND_SUPPORTED:
        goto out;
    case SUSPEND_NOT_SUPPORTED:
        error_setg(err,
                   "the requested suspend mode is not supported by the guest");
        goto out;
    default:
        error_setg(err,
                   "the helper program '%s' returned an unexpected exit status"
                   " code (%d)", pmutils_path, WEXITSTATUS(status));
        goto out;
    }

out:
    g_free(pmutils_path);
}

static void guest_suspend(const char *pmutils_bin, const char *sysfile_str,
                          Error **err)
{
    Error *local_err = NULL;
    char *pmutils_path;
    pid_t pid;
    int status;

    pmutils_path = g_find_program_in_path(pmutils_bin);

    pid = fork();
    if (pid == 0) {
        /* child */
        int fd;

        setsid();
        reopen_fd_to_null(0);
        reopen_fd_to_null(1);
        reopen_fd_to_null(2);

        if (pmutils_path) {
            execle(pmutils_path, pmutils_bin, NULL, environ);
        }

        /*
         * If we get here either pm-utils is not installed or execle() has
         * failed. Let's try the manual method if the caller wants it.
         */

        if (!sysfile_str) {
            _exit(EXIT_FAILURE);
        }

        fd = open(LINUX_SYS_STATE_FILE, O_WRONLY);
        if (fd < 0) {
            _exit(EXIT_FAILURE);
        }

        if (write(fd, sysfile_str, strlen(sysfile_str)) < 0) {
            _exit(EXIT_FAILURE);
        }

        _exit(EXIT_SUCCESS);
    } else if (pid < 0) {
        error_setg_errno(err, errno, "failed to create child process");
        goto out;
    }

    ga_wait_child(pid, &status, &local_err);
    if (local_err) {
        error_propagate(err, local_err);
        goto out;
    }

    if (!WIFEXITED(status)) {
        error_setg(err, "child process has terminated abnormally");
        goto out;
    }

    if (WEXITSTATUS(status)) {
        error_setg(err, "child process has failed to suspend");
        goto out;
    }

out:
    g_free(pmutils_path);
}

void qmp_guest_suspend_disk(Error **err)
{
    bios_supports_mode("pm-is-supported", "--hibernate", "disk", err);
    if (error_is_set(err)) {
        return;
    }

    guest_suspend("pm-hibernate", "disk", err);
}

void qmp_guest_suspend_ram(Error **err)
{
    bios_supports_mode("pm-is-supported", "--suspend", "mem", err);
    if (error_is_set(err)) {
        return;
    }

    guest_suspend("pm-suspend", "mem", err);
}

void qmp_guest_suspend_hybrid(Error **err)
{
    bios_supports_mode("pm-is-supported", "--suspend-hybrid", NULL, err);
    if (error_is_set(err)) {
        return;
    }

    guest_suspend("pm-suspend-hybrid", NULL, err);
}

static GuestNetworkInterfaceList *
guest_find_interface(GuestNetworkInterfaceList *head,
                     const char *name)
{
    for (; head; head = head->next) {
        if (strcmp(head->value->name, name) == 0) {
            break;
        }
    }

    return head;
}

/*
 * Build information about guest interfaces
 */
GuestNetworkInterfaceList *qmp_guest_network_get_interfaces(Error **errp)
{
    GuestNetworkInterfaceList *head = NULL, *cur_item = NULL;
    struct ifaddrs *ifap, *ifa;

    if (getifaddrs(&ifap) < 0) {
        error_setg_errno(errp, errno, "getifaddrs failed");
        goto error;
    }

    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        GuestNetworkInterfaceList *info;
        GuestIpAddressList **address_list = NULL, *address_item = NULL;
        char addr4[INET_ADDRSTRLEN];
        char addr6[INET6_ADDRSTRLEN];
        int sock;
        struct ifreq ifr;
        unsigned char *mac_addr;
        void *p;

        g_debug("Processing %s interface", ifa->ifa_name);

        info = guest_find_interface(head, ifa->ifa_name);

        if (!info) {
            info = g_malloc0(sizeof(*info));
            info->value = g_malloc0(sizeof(*info->value));
            info->value->name = g_strdup(ifa->ifa_name);

            if (!cur_item) {
                head = cur_item = info;
            } else {
                cur_item->next = info;
                cur_item = info;
            }
        }

        if (!info->value->has_hardware_address &&
            ifa->ifa_flags & SIOCGIFHWADDR) {
            /* we haven't obtained HW address yet */
            sock = socket(PF_INET, SOCK_STREAM, 0);
            if (sock == -1) {
                error_setg_errno(errp, errno, "failed to create socket");
                goto error;
            }

            memset(&ifr, 0, sizeof(ifr));
            pstrcpy(ifr.ifr_name, IF_NAMESIZE, info->value->name);
            if (ioctl(sock, SIOCGIFHWADDR, &ifr) == -1) {
                error_setg_errno(errp, errno,
                                 "failed to get MAC address of %s",
                                 ifa->ifa_name);
                close(sock);
                goto error;
            }

            close(sock);
            mac_addr = (unsigned char *) &ifr.ifr_hwaddr.sa_data;

            info->value->hardware_address =
                g_strdup_printf("%02x:%02x:%02x:%02x:%02x:%02x",
                                (int) mac_addr[0], (int) mac_addr[1],
                                (int) mac_addr[2], (int) mac_addr[3],
                                (int) mac_addr[4], (int) mac_addr[5]);

            info->value->has_hardware_address = true;
        }

        if (ifa->ifa_addr &&
            ifa->ifa_addr->sa_family == AF_INET) {
            /* interface with IPv4 address */
            p = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
            if (!inet_ntop(AF_INET, p, addr4, sizeof(addr4))) {
                error_setg_errno(errp, errno, "inet_ntop failed");
                goto error;
            }

            address_item = g_malloc0(sizeof(*address_item));
            address_item->value = g_malloc0(sizeof(*address_item->value));
            address_item->value->ip_address = g_strdup(addr4);
            address_item->value->ip_address_type = GUEST_IP_ADDRESS_TYPE_IPV4;

            if (ifa->ifa_netmask) {
                /* Count the number of set bits in netmask.
                 * This is safe as '1' and '0' cannot be shuffled in netmask. */
                p = &((struct sockaddr_in *)ifa->ifa_netmask)->sin_addr;
                address_item->value->prefix = ctpop32(((uint32_t *) p)[0]);
            }
        } else if (ifa->ifa_addr &&
                   ifa->ifa_addr->sa_family == AF_INET6) {
            /* interface with IPv6 address */
            p = &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
            if (!inet_ntop(AF_INET6, p, addr6, sizeof(addr6))) {
                error_setg_errno(errp, errno, "inet_ntop failed");
                goto error;
            }

            address_item = g_malloc0(sizeof(*address_item));
            address_item->value = g_malloc0(sizeof(*address_item->value));
            address_item->value->ip_address = g_strdup(addr6);
            address_item->value->ip_address_type = GUEST_IP_ADDRESS_TYPE_IPV6;

            if (ifa->ifa_netmask) {
                /* Count the number of set bits in netmask.
                 * This is safe as '1' and '0' cannot be shuffled in netmask. */
                p = &((struct sockaddr_in6 *)ifa->ifa_netmask)->sin6_addr;
                address_item->value->prefix =
                    ctpop32(((uint32_t *) p)[0]) +
                    ctpop32(((uint32_t *) p)[1]) +
                    ctpop32(((uint32_t *) p)[2]) +
                    ctpop32(((uint32_t *) p)[3]);
            }
        }

        if (!address_item) {
            continue;
        }

        address_list = &info->value->ip_addresses;

        while (*address_list && (*address_list)->next) {
            address_list = &(*address_list)->next;
        }

        if (!*address_list) {
            *address_list = address_item;
        } else {
            (*address_list)->next = address_item;
        }

        info->value->has_ip_addresses = true;


    }

    freeifaddrs(ifap);
    return head;

error:
    freeifaddrs(ifap);
    qapi_free_GuestNetworkInterfaceList(head);
    return NULL;
}

#define SYSCONF_EXACT(name, err) sysconf_exact((name), #name, (err))

static long sysconf_exact(int name, const char *name_str, Error **err)
{
    long ret;

    errno = 0;
    ret = sysconf(name);
    if (ret == -1) {
        if (errno == 0) {
            error_setg(err, "sysconf(%s): value indefinite", name_str);
        } else {
            error_setg_errno(err, errno, "sysconf(%s)", name_str);
        }
    }
    return ret;
}

/* Transfer online/offline status between @vcpu and the guest system.
 *
 * On input either @errp or *@errp must be NULL.
 *
 * In system-to-@vcpu direction, the following @vcpu fields are accessed:
 * - R: vcpu->logical_id
 * - W: vcpu->online
 * - W: vcpu->can_offline
 *
 * In @vcpu-to-system direction, the following @vcpu fields are accessed:
 * - R: vcpu->logical_id
 * - R: vcpu->online
 *
 * Written members remain unmodified on error.
 */
static void transfer_vcpu(GuestLogicalProcessor *vcpu, bool sys2vcpu,
                          Error **errp)
{
    char *dirpath;
    int dirfd;

    dirpath = g_strdup_printf("/sys/devices/system/cpu/cpu%" PRId64 "/",
                              vcpu->logical_id);
    dirfd = open(dirpath, O_RDONLY | O_DIRECTORY);
    if (dirfd == -1) {
        error_setg_errno(errp, errno, "open(\"%s\")", dirpath);
    } else {
        static const char fn[] = "online";
        int fd;
        int res;

        fd = openat(dirfd, fn, sys2vcpu ? O_RDONLY : O_RDWR);
        if (fd == -1) {
            if (errno != ENOENT) {
                error_setg_errno(errp, errno, "open(\"%s/%s\")", dirpath, fn);
            } else if (sys2vcpu) {
                vcpu->online = true;
                vcpu->can_offline = false;
            } else if (!vcpu->online) {
                error_setg(errp, "logical processor #%" PRId64 " can't be "
                           "offlined", vcpu->logical_id);
            } /* otherwise pretend successful re-onlining */
        } else {
            unsigned char status;

            res = pread(fd, &status, 1, 0);
            if (res == -1) {
                error_setg_errno(errp, errno, "pread(\"%s/%s\")", dirpath, fn);
            } else if (res == 0) {
                error_setg(errp, "pread(\"%s/%s\"): unexpected EOF", dirpath,
                           fn);
            } else if (sys2vcpu) {
                vcpu->online = (status != '0');
                vcpu->can_offline = true;
            } else if (vcpu->online != (status != '0')) {
                status = '0' + vcpu->online;
                if (pwrite(fd, &status, 1, 0) == -1) {
                    error_setg_errno(errp, errno, "pwrite(\"%s/%s\")", dirpath,
                                     fn);
                }
            } /* otherwise pretend successful re-(on|off)-lining */

            res = close(fd);
            g_assert(res == 0);
        }

        res = close(dirfd);
        g_assert(res == 0);
    }

    g_free(dirpath);
}

GuestLogicalProcessorList *qmp_guest_get_vcpus(Error **errp)
{
    int64_t current;
    GuestLogicalProcessorList *head, **link;
    long sc_max;
    Error *local_err = NULL;

    current = 0;
    head = NULL;
    link = &head;
    sc_max = SYSCONF_EXACT(_SC_NPROCESSORS_CONF, &local_err);

    while (local_err == NULL && current < sc_max) {
        GuestLogicalProcessor *vcpu;
        GuestLogicalProcessorList *entry;

        vcpu = g_malloc0(sizeof *vcpu);
        vcpu->logical_id = current++;
        vcpu->has_can_offline = true; /* lolspeak ftw */
        transfer_vcpu(vcpu, true, &local_err);

        entry = g_malloc0(sizeof *entry);
        entry->value = vcpu;

        *link = entry;
        link = &entry->next;
    }

    if (local_err == NULL) {
        /* there's no guest with zero VCPUs */
        g_assert(head != NULL);
        return head;
    }

    qapi_free_GuestLogicalProcessorList(head);
    error_propagate(errp, local_err);
    return NULL;
}

int64_t qmp_guest_set_vcpus(GuestLogicalProcessorList *vcpus, Error **errp)
{
    int64_t processed;
    Error *local_err = NULL;

    processed = 0;
    while (vcpus != NULL) {
        transfer_vcpu(vcpus->value, false, &local_err);
        if (local_err != NULL) {
            break;
        }
        ++processed;
        vcpus = vcpus->next;
    }

    if (local_err != NULL) {
        if (processed == 0) {
            error_propagate(errp, local_err);
        } else {
            error_free(local_err);
        }
    }

    return processed;
}

#else /* defined(__linux__) */

void qmp_guest_suspend_disk(Error **err)
{
    error_set(err, QERR_UNSUPPORTED);
}

void qmp_guest_suspend_ram(Error **err)
{
    error_set(err, QERR_UNSUPPORTED);
}

void qmp_guest_suspend_hybrid(Error **err)
{
    error_set(err, QERR_UNSUPPORTED);
}

GuestNetworkInterfaceList *qmp_guest_network_get_interfaces(Error **errp)
{
    error_set(errp, QERR_UNSUPPORTED);
    return NULL;
}

GuestLogicalProcessorList *qmp_guest_get_vcpus(Error **errp)
{
    error_set(errp, QERR_UNSUPPORTED);
    return NULL;
}

int64_t qmp_guest_set_vcpus(GuestLogicalProcessorList *vcpus, Error **errp)
{
    error_set(errp, QERR_UNSUPPORTED);
    return -1;
}

#endif

#if !defined(CONFIG_FSFREEZE)

GuestFsfreezeStatus qmp_guest_fsfreeze_status(Error **err)
{
    error_set(err, QERR_UNSUPPORTED);

    return 0;
}

int64_t qmp_guest_fsfreeze_freeze(Error **err)
{
    error_set(err, QERR_UNSUPPORTED);

    return 0;
}

int64_t qmp_guest_fsfreeze_thaw(Error **err)
{
    error_set(err, QERR_UNSUPPORTED);

    return 0;
}
#endif /* CONFIG_FSFREEZE */

#if !defined(CONFIG_FSTRIM)
void qmp_guest_fstrim(bool has_minimum, int64_t minimum, Error **err)
{
    error_set(err, QERR_UNSUPPORTED);
}
#endif

/* register init/cleanup routines for stateful command groups */
void ga_command_state_init(GAState *s, GACommandState *cs)
{
#if defined(CONFIG_FSFREEZE)
    ga_command_state_add(cs, NULL, guest_fsfreeze_cleanup);
#endif
    ga_command_state_add(cs, guest_file_init, NULL);
}
