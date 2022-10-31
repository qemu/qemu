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

#include "qemu/osdep.h"
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <dirent.h>
#include "qga-qapi-commands.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/host-utils.h"
#include "qemu/sockets.h"
#include "qemu/base64.h"
#include "qemu/cutils.h"
#include "commands-common.h"
#include "block/nvme.h"
#include "cutils.h"

#ifdef HAVE_UTMPX
#include <utmpx.h>
#endif

#if defined(__linux__)
#include <mntent.h>
#include <sys/statvfs.h>
#include <linux/nvme_ioctl.h>

#ifdef CONFIG_LIBUDEV
#include <libudev.h>
#endif
#endif

#ifdef HAVE_GETIFADDRS
#include <arpa/inet.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <sys/types.h>
#ifdef CONFIG_SOLARIS
#include <sys/sockio.h>
#endif
#endif

static void ga_wait_child(pid_t pid, int *status, Error **errp)
{
    pid_t rpid;

    *status = 0;

    do {
        rpid = waitpid(pid, status, 0);
    } while (rpid == -1 && errno == EINTR);

    if (rpid == -1) {
        error_setg_errno(errp, errno, "failed to wait for child (pid: %d)",
                         pid);
        return;
    }

    g_assert(rpid == pid);
}

void qmp_guest_shutdown(bool has_mode, const char *mode, Error **errp)
{
    const char *shutdown_flag;
    Error *local_err = NULL;
    pid_t pid;
    int status;

#ifdef CONFIG_SOLARIS
    const char *powerdown_flag = "-i5";
    const char *halt_flag = "-i0";
    const char *reboot_flag = "-i6";
#elif defined(CONFIG_BSD)
    const char *powerdown_flag = "-p";
    const char *halt_flag = "-h";
    const char *reboot_flag = "-r";
#else
    const char *powerdown_flag = "-P";
    const char *halt_flag = "-H";
    const char *reboot_flag = "-r";
#endif

    slog("guest-shutdown called, mode: %s", mode);
    if (!has_mode || strcmp(mode, "powerdown") == 0) {
        shutdown_flag = powerdown_flag;
    } else if (strcmp(mode, "halt") == 0) {
        shutdown_flag = halt_flag;
    } else if (strcmp(mode, "reboot") == 0) {
        shutdown_flag = reboot_flag;
    } else {
        error_setg(errp,
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

#ifdef CONFIG_SOLARIS
        execl("/sbin/shutdown", "shutdown", shutdown_flag, "-g0", "-y",
              "hypervisor initiated shutdown", (char *)NULL);
#elif defined(CONFIG_BSD)
        execl("/sbin/shutdown", "shutdown", shutdown_flag, "+0",
               "hypervisor initiated shutdown", (char *)NULL);
#else
        execl("/sbin/shutdown", "shutdown", "-h", shutdown_flag, "+0",
               "hypervisor initiated shutdown", (char *)NULL);
#endif
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
        error_setg(errp, "child process has failed to shutdown");
        return;
    }

    /* succeeded */
}

void qmp_guest_set_time(bool has_time, int64_t time_ns, Error **errp)
{
    int ret;
    int status;
    pid_t pid;
    Error *local_err = NULL;
    struct timeval tv;
    static const char hwclock_path[] = "/sbin/hwclock";
    static int hwclock_available = -1;

    if (hwclock_available < 0) {
        hwclock_available = (access(hwclock_path, X_OK) == 0);
    }

    if (!hwclock_available) {
        error_setg(errp, QERR_UNSUPPORTED);
        return;
    }

    /* If user has passed a time, validate and set it. */
    if (has_time) {
        GDate date = { 0, };

        /* year-2038 will overflow in case time_t is 32bit */
        if (time_ns / 1000000000 != (time_t)(time_ns / 1000000000)) {
            error_setg(errp, "Time %" PRId64 " is too large", time_ns);
            return;
        }

        tv.tv_sec = time_ns / 1000000000;
        tv.tv_usec = (time_ns % 1000000000) / 1000;
        g_date_set_time_t(&date, tv.tv_sec);
        if (date.year < 1970 || date.year >= 2070) {
            error_setg_errno(errp, errno, "Invalid time");
            return;
        }

        ret = settimeofday(&tv, NULL);
        if (ret < 0) {
            error_setg_errno(errp, errno, "Failed to set time to guest");
            return;
        }
    }

    /* Now, if user has passed a time to set and the system time is set, we
     * just need to synchronize the hardware clock. However, if no time was
     * passed, user is requesting the opposite: set the system time from the
     * hardware clock (RTC). */
    pid = fork();
    if (pid == 0) {
        setsid();
        reopen_fd_to_null(0);
        reopen_fd_to_null(1);
        reopen_fd_to_null(2);

        /* Use '/sbin/hwclock -w' to set RTC from the system time,
         * or '/sbin/hwclock -s' to set the system time from RTC. */
        execl(hwclock_path, "hwclock", has_time ? "-w" : "-s", NULL);
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

typedef enum {
    RW_STATE_NEW,
    RW_STATE_READING,
    RW_STATE_WRITING,
} RwState;

struct GuestFileHandle {
    uint64_t id;
    FILE *fh;
    RwState state;
    QTAILQ_ENTRY(GuestFileHandle) next;
};

static struct {
    QTAILQ_HEAD(, GuestFileHandle) filehandles;
} guest_file_state = {
    .filehandles = QTAILQ_HEAD_INITIALIZER(guest_file_state.filehandles),
};

static int64_t guest_file_handle_add(FILE *fh, Error **errp)
{
    GuestFileHandle *gfh;
    int64_t handle;

    handle = ga_get_fd_handle(ga_state, errp);
    if (handle < 0) {
        return -1;
    }

    gfh = g_new0(GuestFileHandle, 1);
    gfh->id = handle;
    gfh->fh = fh;
    QTAILQ_INSERT_TAIL(&guest_file_state.filehandles, gfh, next);

    return handle;
}

GuestFileHandle *guest_file_handle_find(int64_t id, Error **errp)
{
    GuestFileHandle *gfh;

    QTAILQ_FOREACH(gfh, &guest_file_state.filehandles, next)
    {
        if (gfh->id == id) {
            return gfh;
        }
    }

    error_setg(errp, "handle '%" PRId64 "' has not been found", id);
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
find_open_flag(const char *mode_str, Error **errp)
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
        error_setg(errp, "invalid file open mode '%s'", mode_str);
        return -1;
    }
    return guest_file_open_modes[mode].oflag_base | O_NOCTTY | O_NONBLOCK;
}

#define DEFAULT_NEW_FILE_MODE (S_IRUSR | S_IWUSR | \
                               S_IRGRP | S_IWGRP | \
                               S_IROTH | S_IWOTH)

static FILE *
safe_open_or_create(const char *path, const char *mode, Error **errp)
{
    int oflag;
    int fd = -1;
    FILE *f = NULL;

    oflag = find_open_flag(mode, errp);
    if (oflag < 0) {
        goto end;
    }

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
    fd = qga_open_cloexec(path, oflag | ((oflag & O_CREAT) ? O_EXCL : 0), 0);
    if (fd == -1 && errno == EEXIST) {
        oflag &= ~(unsigned)O_CREAT;
        fd = qga_open_cloexec(path, oflag, 0);
    }
    if (fd == -1) {
        error_setg_errno(errp, errno,
                         "failed to open file '%s' (mode: '%s')",
                         path, mode);
        goto end;
    }

    if ((oflag & O_CREAT) && fchmod(fd, DEFAULT_NEW_FILE_MODE) == -1) {
        error_setg_errno(errp, errno, "failed to set permission "
                         "0%03o on new file '%s' (mode: '%s')",
                         (unsigned)DEFAULT_NEW_FILE_MODE, path, mode);
        goto end;
    }

    f = fdopen(fd, mode);
    if (f == NULL) {
        error_setg_errno(errp, errno, "failed to associate stdio stream with "
                         "file descriptor %d, file '%s' (mode: '%s')",
                         fd, path, mode);
    }

end:
    if (f == NULL && fd != -1) {
        close(fd);
        if (oflag & O_CREAT) {
            unlink(path);
        }
    }
    return f;
}

int64_t qmp_guest_file_open(const char *path, bool has_mode, const char *mode,
                            Error **errp)
{
    FILE *fh;
    Error *local_err = NULL;
    int64_t handle;

    if (!has_mode) {
        mode = "r";
    }
    slog("guest-file-open called, filepath: %s, mode: %s", path, mode);
    fh = safe_open_or_create(path, mode, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return -1;
    }

    /* set fd non-blocking to avoid common use cases (like reading from a
     * named pipe) from hanging the agent
     */
    if (!g_unix_set_fd_nonblocking(fileno(fh), true, NULL)) {
        fclose(fh);
        error_setg_errno(errp, errno, "Failed to set FD nonblocking");
        return -1;
    }

    handle = guest_file_handle_add(fh, errp);
    if (handle < 0) {
        fclose(fh);
        return -1;
    }

    slog("guest-file-open, handle: %" PRId64, handle);
    return handle;
}

void qmp_guest_file_close(int64_t handle, Error **errp)
{
    GuestFileHandle *gfh = guest_file_handle_find(handle, errp);
    int ret;

    slog("guest-file-close called, handle: %" PRId64, handle);
    if (!gfh) {
        return;
    }

    ret = fclose(gfh->fh);
    if (ret == EOF) {
        error_setg_errno(errp, errno, "failed to close handle");
        return;
    }

    QTAILQ_REMOVE(&guest_file_state.filehandles, gfh, next);
    g_free(gfh);
}

GuestFileRead *guest_file_read_unsafe(GuestFileHandle *gfh,
                                      int64_t count, Error **errp)
{
    GuestFileRead *read_data = NULL;
    guchar *buf;
    FILE *fh = gfh->fh;
    size_t read_count;

    /* explicitly flush when switching from writing to reading */
    if (gfh->state == RW_STATE_WRITING) {
        int ret = fflush(fh);
        if (ret == EOF) {
            error_setg_errno(errp, errno, "failed to flush file");
            return NULL;
        }
        gfh->state = RW_STATE_NEW;
    }

    buf = g_malloc0(count + 1);
    read_count = fread(buf, 1, count, fh);
    if (ferror(fh)) {
        error_setg_errno(errp, errno, "failed to read file");
    } else {
        buf[read_count] = 0;
        read_data = g_new0(GuestFileRead, 1);
        read_data->count = read_count;
        read_data->eof = feof(fh);
        if (read_count) {
            read_data->buf_b64 = g_base64_encode(buf, read_count);
        }
        gfh->state = RW_STATE_READING;
    }
    g_free(buf);
    clearerr(fh);

    return read_data;
}

GuestFileWrite *qmp_guest_file_write(int64_t handle, const char *buf_b64,
                                     bool has_count, int64_t count,
                                     Error **errp)
{
    GuestFileWrite *write_data = NULL;
    guchar *buf;
    gsize buf_len;
    int write_count;
    GuestFileHandle *gfh = guest_file_handle_find(handle, errp);
    FILE *fh;

    if (!gfh) {
        return NULL;
    }

    fh = gfh->fh;

    if (gfh->state == RW_STATE_READING) {
        int ret = fseek(fh, 0, SEEK_CUR);
        if (ret == -1) {
            error_setg_errno(errp, errno, "failed to seek file");
            return NULL;
        }
        gfh->state = RW_STATE_NEW;
    }

    buf = qbase64_decode(buf_b64, -1, &buf_len, errp);
    if (!buf) {
        return NULL;
    }

    if (!has_count) {
        count = buf_len;
    } else if (count < 0 || count > buf_len) {
        error_setg(errp, "value '%" PRId64 "' is invalid for argument count",
                   count);
        g_free(buf);
        return NULL;
    }

    write_count = fwrite(buf, 1, count, fh);
    if (ferror(fh)) {
        error_setg_errno(errp, errno, "failed to write to file");
        slog("guest-file-write failed, handle: %" PRId64, handle);
    } else {
        write_data = g_new0(GuestFileWrite, 1);
        write_data->count = write_count;
        write_data->eof = feof(fh);
        gfh->state = RW_STATE_WRITING;
    }
    g_free(buf);
    clearerr(fh);

    return write_data;
}

struct GuestFileSeek *qmp_guest_file_seek(int64_t handle, int64_t offset,
                                          GuestFileWhence *whence_code,
                                          Error **errp)
{
    GuestFileHandle *gfh = guest_file_handle_find(handle, errp);
    GuestFileSeek *seek_data = NULL;
    FILE *fh;
    int ret;
    int whence;
    Error *err = NULL;

    if (!gfh) {
        return NULL;
    }

    /* We stupidly exposed 'whence':'int' in our qapi */
    whence = ga_parse_whence(whence_code, &err);
    if (err) {
        error_propagate(errp, err);
        return NULL;
    }

    fh = gfh->fh;
    ret = fseek(fh, offset, whence);
    if (ret == -1) {
        error_setg_errno(errp, errno, "failed to seek file");
        if (errno == ESPIPE) {
            /* file is non-seekable, stdio shouldn't be buffering anyways */
            gfh->state = RW_STATE_NEW;
        }
    } else {
        seek_data = g_new0(GuestFileSeek, 1);
        seek_data->position = ftell(fh);
        seek_data->eof = feof(fh);
        gfh->state = RW_STATE_NEW;
    }
    clearerr(fh);

    return seek_data;
}

void qmp_guest_file_flush(int64_t handle, Error **errp)
{
    GuestFileHandle *gfh = guest_file_handle_find(handle, errp);
    FILE *fh;
    int ret;

    if (!gfh) {
        return;
    }

    fh = gfh->fh;
    ret = fflush(fh);
    if (ret == EOF) {
        error_setg_errno(errp, errno, "failed to flush file");
    } else {
        gfh->state = RW_STATE_NEW;
    }
}

#if defined(CONFIG_FSFREEZE) || defined(CONFIG_FSTRIM)
void free_fs_mount_list(FsMountList *mounts)
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
#endif

#if defined(CONFIG_FSFREEZE)
typedef enum {
    FSFREEZE_HOOK_THAW = 0,
    FSFREEZE_HOOK_FREEZE,
} FsfreezeHookArg;

static const char *fsfreeze_hook_arg_string[] = {
    "thaw",
    "freeze",
};

static void execute_fsfreeze_hook(FsfreezeHookArg arg, Error **errp)
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
        error_setg_errno(errp, errno, "can't access fsfreeze hook '%s'", hook);
        return;
    }

    slog("executing fsfreeze hook with arg '%s'", arg_str);
    pid = fork();
    if (pid == 0) {
        setsid();
        reopen_fd_to_null(0);
        reopen_fd_to_null(1);
        reopen_fd_to_null(2);

        execl(hook, hook, arg_str, NULL);
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
        error_setg(errp, "fsfreeze hook has terminated abnormally");
        return;
    }

    status = WEXITSTATUS(status);
    if (status) {
        error_setg(errp, "fsfreeze hook has failed with status %d", status);
        return;
    }
}

/*
 * Return status of freeze/thaw
 */
GuestFsfreezeStatus qmp_guest_fsfreeze_status(Error **errp)
{
    if (ga_is_frozen(ga_state)) {
        return GUEST_FSFREEZE_STATUS_FROZEN;
    }

    return GUEST_FSFREEZE_STATUS_THAWED;
}

int64_t qmp_guest_fsfreeze_freeze(Error **errp)
{
    return qmp_guest_fsfreeze_freeze_list(false, NULL, errp);
}

int64_t qmp_guest_fsfreeze_freeze_list(bool has_mountpoints,
                                       strList *mountpoints,
                                       Error **errp)
{
    int ret;
    FsMountList mounts;
    Error *local_err = NULL;

    slog("guest-fsfreeze called");

    execute_fsfreeze_hook(FSFREEZE_HOOK_FREEZE, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return -1;
    }

    QTAILQ_INIT(&mounts);
    if (!build_fs_mount_list(&mounts, &local_err)) {
        error_propagate(errp, local_err);
        return -1;
    }

    /* cannot risk guest agent blocking itself on a write in this state */
    ga_set_frozen(ga_state);

    ret = qmp_guest_fsfreeze_do_freeze_list(has_mountpoints, mountpoints,
                                            mounts, errp);

    free_fs_mount_list(&mounts);
    /* We may not issue any FIFREEZE here.
     * Just unset ga_state here and ready for the next call.
     */
    if (ret == 0) {
        ga_unset_frozen(ga_state);
    } else if (ret < 0) {
        qmp_guest_fsfreeze_thaw(NULL);
    }
    return ret;
}

int64_t qmp_guest_fsfreeze_thaw(Error **errp)
{
    int ret;

    ret = qmp_guest_fsfreeze_do_thaw(errp);
    if (ret >= 0) {
        ga_unset_frozen(ga_state);
        execute_fsfreeze_hook(FSFREEZE_HOOK_THAW, errp);
    } else {
        ret = 0;
    }

    return ret;
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
#endif

/* linux-specific implementations. avoid this if at all possible. */
#if defined(__linux__)
#if defined(CONFIG_FSFREEZE)

static char *get_pci_driver(char const *syspath, int pathlen, Error **errp)
{
    char *path;
    char *dpath;
    char *driver = NULL;
    char buf[PATH_MAX];
    ssize_t len;

    path = g_strndup(syspath, pathlen);
    dpath = g_strdup_printf("%s/driver", path);
    len = readlink(dpath, buf, sizeof(buf) - 1);
    if (len != -1) {
        buf[len] = 0;
        driver = g_path_get_basename(buf);
    }
    g_free(dpath);
    g_free(path);
    return driver;
}

static int compare_uint(const void *_a, const void *_b)
{
    unsigned int a = *(unsigned int *)_a;
    unsigned int b = *(unsigned int *)_b;

    return a < b ? -1 : a > b ? 1 : 0;
}

/* Walk the specified sysfs and build a sorted list of host or ata numbers */
static int build_hosts(char const *syspath, char const *host, bool ata,
                       unsigned int *hosts, int hosts_max, Error **errp)
{
    char *path;
    DIR *dir;
    struct dirent *entry;
    int i = 0;

    path = g_strndup(syspath, host - syspath);
    dir = opendir(path);
    if (!dir) {
        error_setg_errno(errp, errno, "opendir(\"%s\")", path);
        g_free(path);
        return -1;
    }

    while (i < hosts_max) {
        entry = readdir(dir);
        if (!entry) {
            break;
        }
        if (ata && sscanf(entry->d_name, "ata%d", hosts + i) == 1) {
            ++i;
        } else if (!ata && sscanf(entry->d_name, "host%d", hosts + i) == 1) {
            ++i;
        }
    }

    qsort(hosts, i, sizeof(hosts[0]), compare_uint);

    g_free(path);
    closedir(dir);
    return i;
}

/*
 * Store disk device info for devices on the PCI bus.
 * Returns true if information has been stored, or false for failure.
 */
static bool build_guest_fsinfo_for_pci_dev(char const *syspath,
                                           GuestDiskAddress *disk,
                                           Error **errp)
{
    unsigned int pci[4], host, hosts[8], tgt[3];
    int i, nhosts = 0, pcilen;
    GuestPCIAddress *pciaddr = disk->pci_controller;
    bool has_ata = false, has_host = false, has_tgt = false;
    char *p, *q, *driver = NULL;
    bool ret = false;

    p = strstr(syspath, "/devices/pci");
    if (!p || sscanf(p + 12, "%*x:%*x/%x:%x:%x.%x%n",
                     pci, pci + 1, pci + 2, pci + 3, &pcilen) < 4) {
        g_debug("only pci device is supported: sysfs path '%s'", syspath);
        return false;
    }

    p += 12 + pcilen;
    while (true) {
        driver = get_pci_driver(syspath, p - syspath, errp);
        if (driver && (g_str_equal(driver, "ata_piix") ||
                       g_str_equal(driver, "sym53c8xx") ||
                       g_str_equal(driver, "virtio-pci") ||
                       g_str_equal(driver, "ahci") ||
                       g_str_equal(driver, "nvme"))) {
            break;
        }

        g_free(driver);
        if (sscanf(p, "/%x:%x:%x.%x%n",
                          pci, pci + 1, pci + 2, pci + 3, &pcilen) == 4) {
            p += pcilen;
            continue;
        }

        g_debug("unsupported driver or sysfs path '%s'", syspath);
        return false;
    }

    p = strstr(syspath, "/target");
    if (p && sscanf(p + 7, "%*u:%*u:%*u/%*u:%u:%u:%u",
                    tgt, tgt + 1, tgt + 2) == 3) {
        has_tgt = true;
    }

    p = strstr(syspath, "/ata");
    if (p) {
        q = p + 4;
        has_ata = true;
    } else {
        p = strstr(syspath, "/host");
        q = p + 5;
    }
    if (p && sscanf(q, "%u", &host) == 1) {
        has_host = true;
        nhosts = build_hosts(syspath, p, has_ata, hosts,
                             ARRAY_SIZE(hosts), errp);
        if (nhosts < 0) {
            goto cleanup;
        }
    }

    pciaddr->domain = pci[0];
    pciaddr->bus = pci[1];
    pciaddr->slot = pci[2];
    pciaddr->function = pci[3];

    if (strcmp(driver, "ata_piix") == 0) {
        /* a host per ide bus, target*:0:<unit>:0 */
        if (!has_host || !has_tgt) {
            g_debug("invalid sysfs path '%s' (driver '%s')", syspath, driver);
            goto cleanup;
        }
        for (i = 0; i < nhosts; i++) {
            if (host == hosts[i]) {
                disk->bus_type = GUEST_DISK_BUS_TYPE_IDE;
                disk->bus = i;
                disk->unit = tgt[1];
                break;
            }
        }
        if (i >= nhosts) {
            g_debug("no host for '%s' (driver '%s')", syspath, driver);
            goto cleanup;
        }
    } else if (strcmp(driver, "sym53c8xx") == 0) {
        /* scsi(LSI Logic): target*:0:<unit>:0 */
        if (!has_tgt) {
            g_debug("invalid sysfs path '%s' (driver '%s')", syspath, driver);
            goto cleanup;
        }
        disk->bus_type = GUEST_DISK_BUS_TYPE_SCSI;
        disk->unit = tgt[1];
    } else if (strcmp(driver, "virtio-pci") == 0) {
        if (has_tgt) {
            /* virtio-scsi: target*:0:0:<unit> */
            disk->bus_type = GUEST_DISK_BUS_TYPE_SCSI;
            disk->unit = tgt[2];
        } else {
            /* virtio-blk: 1 disk per 1 device */
            disk->bus_type = GUEST_DISK_BUS_TYPE_VIRTIO;
        }
    } else if (strcmp(driver, "ahci") == 0) {
        /* ahci: 1 host per 1 unit */
        if (!has_host || !has_tgt) {
            g_debug("invalid sysfs path '%s' (driver '%s')", syspath, driver);
            goto cleanup;
        }
        for (i = 0; i < nhosts; i++) {
            if (host == hosts[i]) {
                disk->unit = i;
                disk->bus_type = GUEST_DISK_BUS_TYPE_SATA;
                break;
            }
        }
        if (i >= nhosts) {
            g_debug("no host for '%s' (driver '%s')", syspath, driver);
            goto cleanup;
        }
    } else if (strcmp(driver, "nvme") == 0) {
        disk->bus_type = GUEST_DISK_BUS_TYPE_NVME;
    } else {
        g_debug("unknown driver '%s' (sysfs path '%s')", driver, syspath);
        goto cleanup;
    }

    ret = true;

cleanup:
    g_free(driver);
    return ret;
}

/*
 * Store disk device info for non-PCI virtio devices (for example s390x
 * channel I/O devices). Returns true if information has been stored, or
 * false for failure.
 */
static bool build_guest_fsinfo_for_nonpci_virtio(char const *syspath,
                                                 GuestDiskAddress *disk,
                                                 Error **errp)
{
    unsigned int tgt[3];
    char *p;

    if (!strstr(syspath, "/virtio") || !strstr(syspath, "/block")) {
        g_debug("Unsupported virtio device '%s'", syspath);
        return false;
    }

    p = strstr(syspath, "/target");
    if (p && sscanf(p + 7, "%*u:%*u:%*u/%*u:%u:%u:%u",
                    &tgt[0], &tgt[1], &tgt[2]) == 3) {
        /* virtio-scsi: target*:0:<target>:<unit> */
        disk->bus_type = GUEST_DISK_BUS_TYPE_SCSI;
        disk->bus = tgt[0];
        disk->target = tgt[1];
        disk->unit = tgt[2];
    } else {
        /* virtio-blk: 1 disk per 1 device */
        disk->bus_type = GUEST_DISK_BUS_TYPE_VIRTIO;
    }

    return true;
}

/*
 * Store disk device info for CCW devices (s390x channel I/O devices).
 * Returns true if information has been stored, or false for failure.
 */
static bool build_guest_fsinfo_for_ccw_dev(char const *syspath,
                                           GuestDiskAddress *disk,
                                           Error **errp)
{
    unsigned int cssid, ssid, subchno, devno;
    char *p;

    p = strstr(syspath, "/devices/css");
    if (!p || sscanf(p + 12, "%*x/%x.%x.%x/%*x.%*x.%x/",
                     &cssid, &ssid, &subchno, &devno) < 4) {
        g_debug("could not parse ccw device sysfs path: %s", syspath);
        return false;
    }

    disk->has_ccw_address = true;
    disk->ccw_address = g_new0(GuestCCWAddress, 1);
    disk->ccw_address->cssid = cssid;
    disk->ccw_address->ssid = ssid;
    disk->ccw_address->subchno = subchno;
    disk->ccw_address->devno = devno;

    if (strstr(p, "/virtio")) {
        build_guest_fsinfo_for_nonpci_virtio(syspath, disk, errp);
    }

    return true;
}

/* Store disk device info specified by @sysfs into @fs */
static void build_guest_fsinfo_for_real_device(char const *syspath,
                                               GuestFilesystemInfo *fs,
                                               Error **errp)
{
    GuestDiskAddress *disk;
    GuestPCIAddress *pciaddr;
    bool has_hwinf;
#ifdef CONFIG_LIBUDEV
    struct udev *udev = NULL;
    struct udev_device *udevice = NULL;
#endif

    pciaddr = g_new0(GuestPCIAddress, 1);
    pciaddr->domain = -1;                       /* -1 means field is invalid */
    pciaddr->bus = -1;
    pciaddr->slot = -1;
    pciaddr->function = -1;

    disk = g_new0(GuestDiskAddress, 1);
    disk->pci_controller = pciaddr;
    disk->bus_type = GUEST_DISK_BUS_TYPE_UNKNOWN;

#ifdef CONFIG_LIBUDEV
    udev = udev_new();
    udevice = udev_device_new_from_syspath(udev, syspath);
    if (udev == NULL || udevice == NULL) {
        g_debug("failed to query udev");
    } else {
        const char *devnode, *serial;
        devnode = udev_device_get_devnode(udevice);
        if (devnode != NULL) {
            disk->dev = g_strdup(devnode);
            disk->has_dev = true;
        }
        serial = udev_device_get_property_value(udevice, "ID_SERIAL");
        if (serial != NULL && *serial != 0) {
            disk->serial = g_strdup(serial);
            disk->has_serial = true;
        }
    }

    udev_unref(udev);
    udev_device_unref(udevice);
#endif

    if (strstr(syspath, "/devices/pci")) {
        has_hwinf = build_guest_fsinfo_for_pci_dev(syspath, disk, errp);
    } else if (strstr(syspath, "/devices/css")) {
        has_hwinf = build_guest_fsinfo_for_ccw_dev(syspath, disk, errp);
    } else if (strstr(syspath, "/virtio")) {
        has_hwinf = build_guest_fsinfo_for_nonpci_virtio(syspath, disk, errp);
    } else {
        g_debug("Unsupported device type for '%s'", syspath);
        has_hwinf = false;
    }

    if (has_hwinf || disk->has_dev || disk->has_serial) {
        QAPI_LIST_PREPEND(fs->disk, disk);
    } else {
        qapi_free_GuestDiskAddress(disk);
    }
}

static void build_guest_fsinfo_for_device(char const *devpath,
                                          GuestFilesystemInfo *fs,
                                          Error **errp);

/* Store a list of slave devices of virtual volume specified by @syspath into
 * @fs */
static void build_guest_fsinfo_for_virtual_device(char const *syspath,
                                                  GuestFilesystemInfo *fs,
                                                  Error **errp)
{
    Error *err = NULL;
    DIR *dir;
    char *dirpath;
    struct dirent *entry;

    dirpath = g_strdup_printf("%s/slaves", syspath);
    dir = opendir(dirpath);
    if (!dir) {
        if (errno != ENOENT) {
            error_setg_errno(errp, errno, "opendir(\"%s\")", dirpath);
        }
        g_free(dirpath);
        return;
    }

    for (;;) {
        errno = 0;
        entry = readdir(dir);
        if (entry == NULL) {
            if (errno) {
                error_setg_errno(errp, errno, "readdir(\"%s\")", dirpath);
            }
            break;
        }

        if (entry->d_type == DT_LNK) {
            char *path;

            g_debug(" slave device '%s'", entry->d_name);
            path = g_strdup_printf("%s/slaves/%s", syspath, entry->d_name);
            build_guest_fsinfo_for_device(path, fs, &err);
            g_free(path);

            if (err) {
                error_propagate(errp, err);
                break;
            }
        }
    }

    g_free(dirpath);
    closedir(dir);
}

static bool is_disk_virtual(const char *devpath, Error **errp)
{
    g_autofree char *syspath = realpath(devpath, NULL);

    if (!syspath) {
        error_setg_errno(errp, errno, "realpath(\"%s\")", devpath);
        return false;
    }
    return strstr(syspath, "/devices/virtual/block/") != NULL;
}

/* Dispatch to functions for virtual/real device */
static void build_guest_fsinfo_for_device(char const *devpath,
                                          GuestFilesystemInfo *fs,
                                          Error **errp)
{
    ERRP_GUARD();
    g_autofree char *syspath = NULL;
    bool is_virtual = false;

    syspath = realpath(devpath, NULL);
    if (!syspath) {
        if (errno != ENOENT) {
            error_setg_errno(errp, errno, "realpath(\"%s\")", devpath);
            return;
        }

        /* ENOENT: This devpath may not exist because of container config */
        if (!fs->name) {
            fs->name = g_path_get_basename(devpath);
        }
        return;
    }

    if (!fs->name) {
        fs->name = g_path_get_basename(syspath);
    }

    g_debug("  parse sysfs path '%s'", syspath);
    is_virtual = is_disk_virtual(syspath, errp);
    if (*errp != NULL) {
        return;
    }
    if (is_virtual) {
        build_guest_fsinfo_for_virtual_device(syspath, fs, errp);
    } else {
        build_guest_fsinfo_for_real_device(syspath, fs, errp);
    }
}

#ifdef CONFIG_LIBUDEV

/*
 * Wrapper around build_guest_fsinfo_for_device() for getting just
 * the disk address.
 */
static GuestDiskAddress *get_disk_address(const char *syspath, Error **errp)
{
    g_autoptr(GuestFilesystemInfo) fs = NULL;

    fs = g_new0(GuestFilesystemInfo, 1);
    build_guest_fsinfo_for_device(syspath, fs, errp);
    if (fs->disk != NULL) {
        return g_steal_pointer(&fs->disk->value);
    }
    return NULL;
}

static char *get_alias_for_syspath(const char *syspath)
{
    struct udev *udev = NULL;
    struct udev_device *udevice = NULL;
    char *ret = NULL;

    udev = udev_new();
    if (udev == NULL) {
        g_debug("failed to query udev");
        goto out;
    }
    udevice = udev_device_new_from_syspath(udev, syspath);
    if (udevice == NULL) {
        g_debug("failed to query udev for path: %s", syspath);
        goto out;
    } else {
        const char *alias = udev_device_get_property_value(
            udevice, "DM_NAME");
        /*
         * NULL means there was an error and empty string means there is no
         * alias. In case of no alias we return NULL instead of empty string.
         */
        if (alias == NULL) {
            g_debug("failed to query udev for device alias for: %s",
                syspath);
        } else if (*alias != 0) {
            ret = g_strdup(alias);
        }
    }

out:
    udev_unref(udev);
    udev_device_unref(udevice);
    return ret;
}

static char *get_device_for_syspath(const char *syspath)
{
    struct udev *udev = NULL;
    struct udev_device *udevice = NULL;
    char *ret = NULL;

    udev = udev_new();
    if (udev == NULL) {
        g_debug("failed to query udev");
        goto out;
    }
    udevice = udev_device_new_from_syspath(udev, syspath);
    if (udevice == NULL) {
        g_debug("failed to query udev for path: %s", syspath);
        goto out;
    } else {
        ret = g_strdup(udev_device_get_devnode(udevice));
    }

out:
    udev_unref(udev);
    udev_device_unref(udevice);
    return ret;
}

static void get_disk_deps(const char *disk_dir, GuestDiskInfo *disk)
{
    g_autofree char *deps_dir = NULL;
    const gchar *dep;
    GDir *dp_deps = NULL;

    /* List dependent disks */
    deps_dir = g_strdup_printf("%s/slaves", disk_dir);
    g_debug("  listing entries in: %s", deps_dir);
    dp_deps = g_dir_open(deps_dir, 0, NULL);
    if (dp_deps == NULL) {
        g_debug("failed to list entries in %s", deps_dir);
        return;
    }
    disk->has_dependencies = true;
    while ((dep = g_dir_read_name(dp_deps)) != NULL) {
        g_autofree char *dep_dir = NULL;
        char *dev_name;

        /* Add dependent disks */
        dep_dir = g_strdup_printf("%s/%s", deps_dir, dep);
        dev_name = get_device_for_syspath(dep_dir);
        if (dev_name != NULL) {
            g_debug("  adding dependent device: %s", dev_name);
            QAPI_LIST_PREPEND(disk->dependencies, dev_name);
        }
    }
    g_dir_close(dp_deps);
}

/*
 * Detect partitions subdirectory, name is "<disk_name><number>" or
 * "<disk_name>p<number>"
 *
 * @disk_name -- last component of /sys path (e.g. sda)
 * @disk_dir -- sys path of the disk (e.g. /sys/block/sda)
 * @disk_dev -- device node of the disk (e.g. /dev/sda)
 */
static GuestDiskInfoList *get_disk_partitions(
    GuestDiskInfoList *list,
    const char *disk_name, const char *disk_dir,
    const char *disk_dev)
{
    GuestDiskInfoList *ret = list;
    struct dirent *de_disk;
    DIR *dp_disk = NULL;
    size_t len = strlen(disk_name);

    dp_disk = opendir(disk_dir);
    while ((de_disk = readdir(dp_disk)) != NULL) {
        g_autofree char *partition_dir = NULL;
        char *dev_name;
        GuestDiskInfo *partition;

        if (!(de_disk->d_type & DT_DIR)) {
            continue;
        }

        if (!(strncmp(disk_name, de_disk->d_name, len) == 0 &&
            ((*(de_disk->d_name + len) == 'p' &&
            isdigit(*(de_disk->d_name + len + 1))) ||
                isdigit(*(de_disk->d_name + len))))) {
            continue;
        }

        partition_dir = g_strdup_printf("%s/%s",
            disk_dir, de_disk->d_name);
        dev_name = get_device_for_syspath(partition_dir);
        if (dev_name == NULL) {
            g_debug("Failed to get device name for syspath: %s",
                disk_dir);
            continue;
        }
        partition = g_new0(GuestDiskInfo, 1);
        partition->name = dev_name;
        partition->partition = true;
        partition->has_dependencies = true;
        /* Add parent disk as dependent for easier tracking of hierarchy */
        QAPI_LIST_PREPEND(partition->dependencies, g_strdup(disk_dev));

        QAPI_LIST_PREPEND(ret, partition);
    }
    closedir(dp_disk);

    return ret;
}

static void get_nvme_smart(GuestDiskInfo *disk)
{
    int fd;
    GuestNVMeSmart *smart;
    NvmeSmartLog log = {0};
    struct nvme_admin_cmd cmd = {
        .opcode = NVME_ADM_CMD_GET_LOG_PAGE,
        .nsid = NVME_NSID_BROADCAST,
        .addr = (uintptr_t)&log,
        .data_len = sizeof(log),
        .cdw10 = NVME_LOG_SMART_INFO | (1 << 15) /* RAE bit */
                 | (((sizeof(log) >> 2) - 1) << 16)
    };

    fd = qga_open_cloexec(disk->name, O_RDONLY, 0);
    if (fd == -1) {
        g_debug("Failed to open device: %s: %s", disk->name, g_strerror(errno));
        return;
    }

    if (ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd)) {
        g_debug("Failed to get smart: %s: %s", disk->name, g_strerror(errno));
        close(fd);
        return;
    }

    disk->has_smart = true;
    disk->smart = g_new0(GuestDiskSmart, 1);
    disk->smart->type = GUEST_DISK_BUS_TYPE_NVME;

    smart = &disk->smart->u.nvme;
    smart->critical_warning = log.critical_warning;
    smart->temperature = lduw_le_p(&log.temperature); /* unaligned field */
    smart->available_spare = log.available_spare;
    smart->available_spare_threshold = log.available_spare_threshold;
    smart->percentage_used = log.percentage_used;
    smart->data_units_read_lo = le64_to_cpu(log.data_units_read[0]);
    smart->data_units_read_hi = le64_to_cpu(log.data_units_read[1]);
    smart->data_units_written_lo = le64_to_cpu(log.data_units_written[0]);
    smart->data_units_written_hi = le64_to_cpu(log.data_units_written[1]);
    smart->host_read_commands_lo = le64_to_cpu(log.host_read_commands[0]);
    smart->host_read_commands_hi = le64_to_cpu(log.host_read_commands[1]);
    smart->host_write_commands_lo = le64_to_cpu(log.host_write_commands[0]);
    smart->host_write_commands_hi = le64_to_cpu(log.host_write_commands[1]);
    smart->controller_busy_time_lo = le64_to_cpu(log.controller_busy_time[0]);
    smart->controller_busy_time_hi = le64_to_cpu(log.controller_busy_time[1]);
    smart->power_cycles_lo = le64_to_cpu(log.power_cycles[0]);
    smart->power_cycles_hi = le64_to_cpu(log.power_cycles[1]);
    smart->power_on_hours_lo = le64_to_cpu(log.power_on_hours[0]);
    smart->power_on_hours_hi = le64_to_cpu(log.power_on_hours[1]);
    smart->unsafe_shutdowns_lo = le64_to_cpu(log.unsafe_shutdowns[0]);
    smart->unsafe_shutdowns_hi = le64_to_cpu(log.unsafe_shutdowns[1]);
    smart->media_errors_lo = le64_to_cpu(log.media_errors[0]);
    smart->media_errors_hi = le64_to_cpu(log.media_errors[1]);
    smart->number_of_error_log_entries_lo =
        le64_to_cpu(log.number_of_error_log_entries[0]);
    smart->number_of_error_log_entries_hi =
        le64_to_cpu(log.number_of_error_log_entries[1]);

    close(fd);
}

static void get_disk_smart(GuestDiskInfo *disk)
{
    if (disk->has_address
        && (disk->address->bus_type == GUEST_DISK_BUS_TYPE_NVME)) {
        get_nvme_smart(disk);
    }
}

GuestDiskInfoList *qmp_guest_get_disks(Error **errp)
{
    GuestDiskInfoList *ret = NULL;
    GuestDiskInfo *disk;
    DIR *dp = NULL;
    struct dirent *de = NULL;

    g_debug("listing /sys/block directory");
    dp = opendir("/sys/block");
    if (dp == NULL) {
        error_setg_errno(errp, errno, "Can't open directory \"/sys/block\"");
        return NULL;
    }
    while ((de = readdir(dp)) != NULL) {
        g_autofree char *disk_dir = NULL, *line = NULL,
            *size_path = NULL;
        char *dev_name;
        Error *local_err = NULL;
        if (de->d_type != DT_LNK) {
            g_debug("  skipping entry: %s", de->d_name);
            continue;
        }

        /* Check size and skip zero-sized disks */
        g_debug("  checking disk size");
        size_path = g_strdup_printf("/sys/block/%s/size", de->d_name);
        if (!g_file_get_contents(size_path, &line, NULL, NULL)) {
            g_debug("  failed to read disk size");
            continue;
        }
        if (g_strcmp0(line, "0\n") == 0) {
            g_debug("  skipping zero-sized disk");
            continue;
        }

        g_debug("  adding %s", de->d_name);
        disk_dir = g_strdup_printf("/sys/block/%s", de->d_name);
        dev_name = get_device_for_syspath(disk_dir);
        if (dev_name == NULL) {
            g_debug("Failed to get device name for syspath: %s",
                disk_dir);
            continue;
        }
        disk = g_new0(GuestDiskInfo, 1);
        disk->name = dev_name;
        disk->partition = false;
        disk->alias = get_alias_for_syspath(disk_dir);
        disk->has_alias = (disk->alias != NULL);
        QAPI_LIST_PREPEND(ret, disk);

        /* Get address for non-virtual devices */
        bool is_virtual = is_disk_virtual(disk_dir, &local_err);
        if (local_err != NULL) {
            g_debug("  failed to check disk path, ignoring error: %s",
                error_get_pretty(local_err));
            error_free(local_err);
            local_err = NULL;
            /* Don't try to get the address */
            is_virtual = true;
        }
        if (!is_virtual) {
            disk->address = get_disk_address(disk_dir, &local_err);
            if (local_err != NULL) {
                g_debug("  failed to get device info, ignoring error: %s",
                    error_get_pretty(local_err));
                error_free(local_err);
                local_err = NULL;
            } else if (disk->address != NULL) {
                disk->has_address = true;
            }
        }

        get_disk_deps(disk_dir, disk);
        get_disk_smart(disk);
        ret = get_disk_partitions(ret, de->d_name, disk_dir, dev_name);
    }

    closedir(dp);

    return ret;
}

#else

GuestDiskInfoList *qmp_guest_get_disks(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

#endif

/* Return a list of the disk device(s)' info which @mount lies on */
static GuestFilesystemInfo *build_guest_fsinfo(struct FsMount *mount,
                                               Error **errp)
{
    GuestFilesystemInfo *fs = g_malloc0(sizeof(*fs));
    struct statvfs buf;
    unsigned long used, nonroot_total, fr_size;
    char *devpath = g_strdup_printf("/sys/dev/block/%u:%u",
                                    mount->devmajor, mount->devminor);

    fs->mountpoint = g_strdup(mount->dirname);
    fs->type = g_strdup(mount->devtype);
    build_guest_fsinfo_for_device(devpath, fs, errp);

    if (statvfs(fs->mountpoint, &buf) == 0) {
        fr_size = buf.f_frsize;
        used = buf.f_blocks - buf.f_bfree;
        nonroot_total = used + buf.f_bavail;
        fs->used_bytes = used * fr_size;
        fs->total_bytes = nonroot_total * fr_size;

        fs->has_total_bytes = true;
        fs->has_used_bytes = true;
    }

    g_free(devpath);

    return fs;
}

GuestFilesystemInfoList *qmp_guest_get_fsinfo(Error **errp)
{
    FsMountList mounts;
    struct FsMount *mount;
    GuestFilesystemInfoList *ret = NULL;
    Error *local_err = NULL;

    QTAILQ_INIT(&mounts);
    if (!build_fs_mount_list(&mounts, &local_err)) {
        error_propagate(errp, local_err);
        return NULL;
    }

    QTAILQ_FOREACH(mount, &mounts, next) {
        g_debug("Building guest fsinfo for '%s'", mount->dirname);

        QAPI_LIST_PREPEND(ret, build_guest_fsinfo(mount, &local_err));
        if (local_err) {
            error_propagate(errp, local_err);
            qapi_free_GuestFilesystemInfoList(ret);
            ret = NULL;
            break;
        }
    }

    free_fs_mount_list(&mounts);
    return ret;
}
#endif /* CONFIG_FSFREEZE */

#if defined(CONFIG_FSTRIM)
/*
 * Walk list of mounted file systems in the guest, and trim them.
 */
GuestFilesystemTrimResponse *
qmp_guest_fstrim(bool has_minimum, int64_t minimum, Error **errp)
{
    GuestFilesystemTrimResponse *response;
    GuestFilesystemTrimResult *result;
    int ret = 0;
    FsMountList mounts;
    struct FsMount *mount;
    int fd;
    struct fstrim_range r;

    slog("guest-fstrim called");

    QTAILQ_INIT(&mounts);
    if (!build_fs_mount_list(&mounts, errp)) {
        return NULL;
    }

    response = g_malloc0(sizeof(*response));

    QTAILQ_FOREACH(mount, &mounts, next) {
        result = g_malloc0(sizeof(*result));
        result->path = g_strdup(mount->dirname);

        QAPI_LIST_PREPEND(response->paths, result);

        fd = qga_open_cloexec(mount->dirname, O_RDONLY, 0);
        if (fd == -1) {
            result->error = g_strdup_printf("failed to open: %s",
                                            strerror(errno));
            result->has_error = true;
            continue;
        }

        /* We try to cull filesystems we know won't work in advance, but other
         * filesystems may not implement fstrim for less obvious reasons.
         * These will report EOPNOTSUPP; while in some other cases ENOTTY
         * will be reported (e.g. CD-ROMs).
         * Any other error means an unexpected error.
         */
        r.start = 0;
        r.len = -1;
        r.minlen = has_minimum ? minimum : 0;
        ret = ioctl(fd, FITRIM, &r);
        if (ret == -1) {
            result->has_error = true;
            if (errno == ENOTTY || errno == EOPNOTSUPP) {
                result->error = g_strdup("trim not supported");
            } else {
                result->error = g_strdup_printf("failed to trim: %s",
                                                strerror(errno));
            }
            close(fd);
            continue;
        }

        result->has_minimum = true;
        result->minimum = r.minlen;
        result->has_trimmed = true;
        result->trimmed = r.len;
        close(fd);
    }

    free_fs_mount_list(&mounts);
    return response;
}
#endif /* CONFIG_FSTRIM */


#define LINUX_SYS_STATE_FILE "/sys/power/state"
#define SUSPEND_SUPPORTED 0
#define SUSPEND_NOT_SUPPORTED 1

typedef enum {
    SUSPEND_MODE_DISK = 0,
    SUSPEND_MODE_RAM = 1,
    SUSPEND_MODE_HYBRID = 2,
} SuspendMode;

/*
 * Executes a command in a child process using g_spawn_sync,
 * returning an int >= 0 representing the exit status of the
 * process.
 *
 * If the program wasn't found in path, returns -1.
 *
 * If a problem happened when creating the child process,
 * returns -1 and errp is set.
 */
static int run_process_child(const char *command[], Error **errp)
{
    int exit_status, spawn_flag;
    GError *g_err = NULL;
    bool success;

    spawn_flag = G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL |
                 G_SPAWN_STDERR_TO_DEV_NULL;

    success =  g_spawn_sync(NULL, (char **)command, NULL, spawn_flag,
                            NULL, NULL, NULL, NULL,
                            &exit_status, &g_err);

    if (success) {
        return WEXITSTATUS(exit_status);
    }

    if (g_err && (g_err->code != G_SPAWN_ERROR_NOENT)) {
        error_setg(errp, "failed to create child process, error '%s'",
                   g_err->message);
    }

    g_error_free(g_err);
    return -1;
}

static bool systemd_supports_mode(SuspendMode mode, Error **errp)
{
    const char *systemctl_args[3] = {"systemd-hibernate", "systemd-suspend",
                                     "systemd-hybrid-sleep"};
    const char *cmd[4] = {"systemctl", "status", systemctl_args[mode], NULL};
    int status;

    status = run_process_child(cmd, errp);

    /*
     * systemctl status uses LSB return codes so we can expect
     * status > 0 and be ok. To assert if the guest has support
     * for the selected suspend mode, status should be < 4. 4 is
     * the code for unknown service status, the return value when
     * the service does not exist. A common value is status = 3
     * (program is not running).
     */
    if (status > 0 && status < 4) {
        return true;
    }

    return false;
}

static void systemd_suspend(SuspendMode mode, Error **errp)
{
    Error *local_err = NULL;
    const char *systemctl_args[3] = {"hibernate", "suspend", "hybrid-sleep"};
    const char *cmd[3] = {"systemctl", systemctl_args[mode], NULL};
    int status;

    status = run_process_child(cmd, &local_err);

    if (status == 0) {
        return;
    }

    if ((status == -1) && !local_err) {
        error_setg(errp, "the helper program 'systemctl %s' was not found",
                   systemctl_args[mode]);
        return;
    }

    if (local_err) {
        error_propagate(errp, local_err);
    } else {
        error_setg(errp, "the helper program 'systemctl %s' returned an "
                   "unexpected exit status code (%d)",
                   systemctl_args[mode], status);
    }
}

static bool pmutils_supports_mode(SuspendMode mode, Error **errp)
{
    Error *local_err = NULL;
    const char *pmutils_args[3] = {"--hibernate", "--suspend",
                                   "--suspend-hybrid"};
    const char *cmd[3] = {"pm-is-supported", pmutils_args[mode], NULL};
    int status;

    status = run_process_child(cmd, &local_err);

    if (status == SUSPEND_SUPPORTED) {
        return true;
    }

    if ((status == -1) && !local_err) {
        return false;
    }

    if (local_err) {
        error_propagate(errp, local_err);
    } else {
        error_setg(errp,
                   "the helper program '%s' returned an unexpected exit"
                   " status code (%d)", "pm-is-supported", status);
    }

    return false;
}

static void pmutils_suspend(SuspendMode mode, Error **errp)
{
    Error *local_err = NULL;
    const char *pmutils_binaries[3] = {"pm-hibernate", "pm-suspend",
                                       "pm-suspend-hybrid"};
    const char *cmd[2] = {pmutils_binaries[mode], NULL};
    int status;

    status = run_process_child(cmd, &local_err);

    if (status == 0) {
        return;
    }

    if ((status == -1) && !local_err) {
        error_setg(errp, "the helper program '%s' was not found",
                   pmutils_binaries[mode]);
        return;
    }

    if (local_err) {
        error_propagate(errp, local_err);
    } else {
        error_setg(errp,
                   "the helper program '%s' returned an unexpected exit"
                   " status code (%d)", pmutils_binaries[mode], status);
    }
}

static bool linux_sys_state_supports_mode(SuspendMode mode, Error **errp)
{
    const char *sysfile_strs[3] = {"disk", "mem", NULL};
    const char *sysfile_str = sysfile_strs[mode];
    char buf[32]; /* hopefully big enough */
    int fd;
    ssize_t ret;

    if (!sysfile_str) {
        error_setg(errp, "unknown guest suspend mode");
        return false;
    }

    fd = open(LINUX_SYS_STATE_FILE, O_RDONLY);
    if (fd < 0) {
        return false;
    }

    ret = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (ret <= 0) {
        return false;
    }
    buf[ret] = '\0';

    if (strstr(buf, sysfile_str)) {
        return true;
    }
    return false;
}

static void linux_sys_state_suspend(SuspendMode mode, Error **errp)
{
    Error *local_err = NULL;
    const char *sysfile_strs[3] = {"disk", "mem", NULL};
    const char *sysfile_str = sysfile_strs[mode];
    pid_t pid;
    int status;

    if (!sysfile_str) {
        error_setg(errp, "unknown guest suspend mode");
        return;
    }

    pid = fork();
    if (!pid) {
        /* child */
        int fd;

        setsid();
        reopen_fd_to_null(0);
        reopen_fd_to_null(1);
        reopen_fd_to_null(2);

        fd = open(LINUX_SYS_STATE_FILE, O_WRONLY);
        if (fd < 0) {
            _exit(EXIT_FAILURE);
        }

        if (write(fd, sysfile_str, strlen(sysfile_str)) < 0) {
            _exit(EXIT_FAILURE);
        }

        _exit(EXIT_SUCCESS);
    } else if (pid < 0) {
        error_setg_errno(errp, errno, "failed to create child process");
        return;
    }

    ga_wait_child(pid, &status, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (WEXITSTATUS(status)) {
        error_setg(errp, "child process has failed to suspend");
    }

}

static void guest_suspend(SuspendMode mode, Error **errp)
{
    Error *local_err = NULL;
    bool mode_supported = false;

    if (systemd_supports_mode(mode, &local_err)) {
        mode_supported = true;
        systemd_suspend(mode, &local_err);
    }

    if (!local_err) {
        return;
    }

    error_free(local_err);
    local_err = NULL;

    if (pmutils_supports_mode(mode, &local_err)) {
        mode_supported = true;
        pmutils_suspend(mode, &local_err);
    }

    if (!local_err) {
        return;
    }

    error_free(local_err);
    local_err = NULL;

    if (linux_sys_state_supports_mode(mode, &local_err)) {
        mode_supported = true;
        linux_sys_state_suspend(mode, &local_err);
    }

    if (!mode_supported) {
        error_free(local_err);
        error_setg(errp,
                   "the requested suspend mode is not supported by the guest");
    } else {
        error_propagate(errp, local_err);
    }
}

void qmp_guest_suspend_disk(Error **errp)
{
    guest_suspend(SUSPEND_MODE_DISK, errp);
}

void qmp_guest_suspend_ram(Error **errp)
{
    guest_suspend(SUSPEND_MODE_RAM, errp);
}

void qmp_guest_suspend_hybrid(Error **errp)
{
    guest_suspend(SUSPEND_MODE_HYBRID, errp);
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
                          char *dirpath, Error **errp)
{
    int fd;
    int res;
    int dirfd;
    static const char fn[] = "online";

    dirfd = open(dirpath, O_RDONLY | O_DIRECTORY);
    if (dirfd == -1) {
        error_setg_errno(errp, errno, "open(\"%s\")", dirpath);
        return;
    }

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

GuestLogicalProcessorList *qmp_guest_get_vcpus(Error **errp)
{
    GuestLogicalProcessorList *head, **tail;
    const char *cpu_dir = "/sys/devices/system/cpu";
    const gchar *line;
    g_autoptr(GDir) cpu_gdir = NULL;
    Error *local_err = NULL;

    head = NULL;
    tail = &head;
    cpu_gdir = g_dir_open(cpu_dir, 0, NULL);

    if (cpu_gdir == NULL) {
        error_setg_errno(errp, errno, "failed to list entries: %s", cpu_dir);
        return NULL;
    }

    while (local_err == NULL && (line = g_dir_read_name(cpu_gdir)) != NULL) {
        GuestLogicalProcessor *vcpu;
        int64_t id;
        if (sscanf(line, "cpu%" PRId64, &id)) {
            g_autofree char *path = g_strdup_printf("/sys/devices/system/cpu/"
                                                    "cpu%" PRId64 "/", id);
            vcpu = g_malloc0(sizeof *vcpu);
            vcpu->logical_id = id;
            vcpu->has_can_offline = true; /* lolspeak ftw */
            transfer_vcpu(vcpu, true, path, &local_err);
            QAPI_LIST_APPEND(tail, vcpu);
        }
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
        char *path = g_strdup_printf("/sys/devices/system/cpu/cpu%" PRId64 "/",
                                     vcpus->value->logical_id);

        transfer_vcpu(vcpus->value, false, path, &local_err);
        g_free(path);
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
#endif /* __linux__ */

#if defined(__linux__) || defined(__FreeBSD__)
void qmp_guest_set_user_password(const char *username,
                                 const char *password,
                                 bool crypted,
                                 Error **errp)
{
    Error *local_err = NULL;
    char *passwd_path = NULL;
    pid_t pid;
    int status;
    int datafd[2] = { -1, -1 };
    char *rawpasswddata = NULL;
    size_t rawpasswdlen;
    char *chpasswddata = NULL;
    size_t chpasswdlen;

    rawpasswddata = (char *)qbase64_decode(password, -1, &rawpasswdlen, errp);
    if (!rawpasswddata) {
        return;
    }
    rawpasswddata = g_renew(char, rawpasswddata, rawpasswdlen + 1);
    rawpasswddata[rawpasswdlen] = '\0';

    if (strchr(rawpasswddata, '\n')) {
        error_setg(errp, "forbidden characters in raw password");
        goto out;
    }

    if (strchr(username, '\n') ||
        strchr(username, ':')) {
        error_setg(errp, "forbidden characters in username");
        goto out;
    }

#ifdef __FreeBSD__
    chpasswddata = g_strdup(rawpasswddata);
    passwd_path = g_find_program_in_path("pw");
#else
    chpasswddata = g_strdup_printf("%s:%s\n", username, rawpasswddata);
    passwd_path = g_find_program_in_path("chpasswd");
#endif

    chpasswdlen = strlen(chpasswddata);

    if (!passwd_path) {
        error_setg(errp, "cannot find 'passwd' program in PATH");
        goto out;
    }

    if (!g_unix_open_pipe(datafd, FD_CLOEXEC, NULL)) {
        error_setg(errp, "cannot create pipe FDs");
        goto out;
    }

    pid = fork();
    if (pid == 0) {
        close(datafd[1]);
        /* child */
        setsid();
        dup2(datafd[0], 0);
        reopen_fd_to_null(1);
        reopen_fd_to_null(2);

#ifdef __FreeBSD__
        const char *h_arg;
        h_arg = (crypted) ? "-H" : "-h";
        execl(passwd_path, "pw", "usermod", "-n", username, h_arg, "0", NULL);
#else
        if (crypted) {
            execl(passwd_path, "chpasswd", "-e", NULL);
        } else {
            execl(passwd_path, "chpasswd", NULL);
        }
#endif
        _exit(EXIT_FAILURE);
    } else if (pid < 0) {
        error_setg_errno(errp, errno, "failed to create child process");
        goto out;
    }
    close(datafd[0]);
    datafd[0] = -1;

    if (qemu_write_full(datafd[1], chpasswddata, chpasswdlen) != chpasswdlen) {
        error_setg_errno(errp, errno, "cannot write new account password");
        goto out;
    }
    close(datafd[1]);
    datafd[1] = -1;

    ga_wait_child(pid, &status, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto out;
    }

    if (!WIFEXITED(status)) {
        error_setg(errp, "child process has terminated abnormally");
        goto out;
    }

    if (WEXITSTATUS(status)) {
        error_setg(errp, "child process has failed to set user password");
        goto out;
    }

out:
    g_free(chpasswddata);
    g_free(rawpasswddata);
    g_free(passwd_path);
    if (datafd[0] != -1) {
        close(datafd[0]);
    }
    if (datafd[1] != -1) {
        close(datafd[1]);
    }
}
#else /* __linux__ || __FreeBSD__ */
void qmp_guest_set_user_password(const char *username,
                                 const char *password,
                                 bool crypted,
                                 Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
}
#endif /* __linux__ || __FreeBSD__ */

#ifdef __linux__
static void ga_read_sysfs_file(int dirfd, const char *pathname, char *buf,
                               int size, Error **errp)
{
    int fd;
    int res;

    errno = 0;
    fd = openat(dirfd, pathname, O_RDONLY);
    if (fd == -1) {
        error_setg_errno(errp, errno, "open sysfs file \"%s\"", pathname);
        return;
    }

    res = pread(fd, buf, size, 0);
    if (res == -1) {
        error_setg_errno(errp, errno, "pread sysfs file \"%s\"", pathname);
    } else if (res == 0) {
        error_setg(errp, "pread sysfs file \"%s\": unexpected EOF", pathname);
    }
    close(fd);
}

static void ga_write_sysfs_file(int dirfd, const char *pathname,
                                const char *buf, int size, Error **errp)
{
    int fd;

    errno = 0;
    fd = openat(dirfd, pathname, O_WRONLY);
    if (fd == -1) {
        error_setg_errno(errp, errno, "open sysfs file \"%s\"", pathname);
        return;
    }

    if (pwrite(fd, buf, size, 0) == -1) {
        error_setg_errno(errp, errno, "pwrite sysfs file \"%s\"", pathname);
    }

    close(fd);
}

/* Transfer online/offline status between @mem_blk and the guest system.
 *
 * On input either @errp or *@errp must be NULL.
 *
 * In system-to-@mem_blk direction, the following @mem_blk fields are accessed:
 * - R: mem_blk->phys_index
 * - W: mem_blk->online
 * - W: mem_blk->can_offline
 *
 * In @mem_blk-to-system direction, the following @mem_blk fields are accessed:
 * - R: mem_blk->phys_index
 * - R: mem_blk->online
 *-  R: mem_blk->can_offline
 * Written members remain unmodified on error.
 */
static void transfer_memory_block(GuestMemoryBlock *mem_blk, bool sys2memblk,
                                  GuestMemoryBlockResponse *result,
                                  Error **errp)
{
    char *dirpath;
    int dirfd;
    char *status;
    Error *local_err = NULL;

    if (!sys2memblk) {
        DIR *dp;

        if (!result) {
            error_setg(errp, "Internal error, 'result' should not be NULL");
            return;
        }
        errno = 0;
        dp = opendir("/sys/devices/system/memory/");
         /* if there is no 'memory' directory in sysfs,
         * we think this VM does not support online/offline memory block,
         * any other solution?
         */
        if (!dp) {
            if (errno == ENOENT) {
                result->response =
                    GUEST_MEMORY_BLOCK_RESPONSE_TYPE_OPERATION_NOT_SUPPORTED;
            }
            goto out1;
        }
        closedir(dp);
    }

    dirpath = g_strdup_printf("/sys/devices/system/memory/memory%" PRId64 "/",
                              mem_blk->phys_index);
    dirfd = open(dirpath, O_RDONLY | O_DIRECTORY);
    if (dirfd == -1) {
        if (sys2memblk) {
            error_setg_errno(errp, errno, "open(\"%s\")", dirpath);
        } else {
            if (errno == ENOENT) {
                result->response = GUEST_MEMORY_BLOCK_RESPONSE_TYPE_NOT_FOUND;
            } else {
                result->response =
                    GUEST_MEMORY_BLOCK_RESPONSE_TYPE_OPERATION_FAILED;
            }
        }
        g_free(dirpath);
        goto out1;
    }
    g_free(dirpath);

    status = g_malloc0(10);
    ga_read_sysfs_file(dirfd, "state", status, 10, &local_err);
    if (local_err) {
        /* treat with sysfs file that not exist in old kernel */
        if (errno == ENOENT) {
            error_free(local_err);
            if (sys2memblk) {
                mem_blk->online = true;
                mem_blk->can_offline = false;
            } else if (!mem_blk->online) {
                result->response =
                    GUEST_MEMORY_BLOCK_RESPONSE_TYPE_OPERATION_NOT_SUPPORTED;
            }
        } else {
            if (sys2memblk) {
                error_propagate(errp, local_err);
            } else {
                error_free(local_err);
                result->response =
                    GUEST_MEMORY_BLOCK_RESPONSE_TYPE_OPERATION_FAILED;
            }
        }
        goto out2;
    }

    if (sys2memblk) {
        char removable = '0';

        mem_blk->online = (strncmp(status, "online", 6) == 0);

        ga_read_sysfs_file(dirfd, "removable", &removable, 1, &local_err);
        if (local_err) {
            /* if no 'removable' file, it doesn't support offline mem blk */
            if (errno == ENOENT) {
                error_free(local_err);
                mem_blk->can_offline = false;
            } else {
                error_propagate(errp, local_err);
            }
        } else {
            mem_blk->can_offline = (removable != '0');
        }
    } else {
        if (mem_blk->online != (strncmp(status, "online", 6) == 0)) {
            const char *new_state = mem_blk->online ? "online" : "offline";

            ga_write_sysfs_file(dirfd, "state", new_state, strlen(new_state),
                                &local_err);
            if (local_err) {
                error_free(local_err);
                result->response =
                    GUEST_MEMORY_BLOCK_RESPONSE_TYPE_OPERATION_FAILED;
                goto out2;
            }

            result->response = GUEST_MEMORY_BLOCK_RESPONSE_TYPE_SUCCESS;
            result->has_error_code = false;
        } /* otherwise pretend successful re-(on|off)-lining */
    }
    g_free(status);
    close(dirfd);
    return;

out2:
    g_free(status);
    close(dirfd);
out1:
    if (!sys2memblk) {
        result->has_error_code = true;
        result->error_code = errno;
    }
}

GuestMemoryBlockList *qmp_guest_get_memory_blocks(Error **errp)
{
    GuestMemoryBlockList *head, **tail;
    Error *local_err = NULL;
    struct dirent *de;
    DIR *dp;

    head = NULL;
    tail = &head;

    dp = opendir("/sys/devices/system/memory/");
    if (!dp) {
        /* it's ok if this happens to be a system that doesn't expose
         * memory blocks via sysfs, but otherwise we should report
         * an error
         */
        if (errno != ENOENT) {
            error_setg_errno(errp, errno, "Can't open directory"
                             "\"/sys/devices/system/memory/\"");
        }
        return NULL;
    }

    /* Note: the phys_index of memory block may be discontinuous,
     * this is because a memblk is the unit of the Sparse Memory design, which
     * allows discontinuous memory ranges (ex. NUMA), so here we should
     * traverse the memory block directory.
     */
    while ((de = readdir(dp)) != NULL) {
        GuestMemoryBlock *mem_blk;

        if ((strncmp(de->d_name, "memory", 6) != 0) ||
            !(de->d_type & DT_DIR)) {
            continue;
        }

        mem_blk = g_malloc0(sizeof *mem_blk);
        /* The d_name is "memoryXXX",  phys_index is block id, same as XXX */
        mem_blk->phys_index = strtoul(&de->d_name[6], NULL, 10);
        mem_blk->has_can_offline = true; /* lolspeak ftw */
        transfer_memory_block(mem_blk, true, NULL, &local_err);
        if (local_err) {
            break;
        }

        QAPI_LIST_APPEND(tail, mem_blk);
    }

    closedir(dp);
    if (local_err == NULL) {
        /* there's no guest with zero memory blocks */
        if (head == NULL) {
            error_setg(errp, "guest reported zero memory blocks!");
        }
        return head;
    }

    qapi_free_GuestMemoryBlockList(head);
    error_propagate(errp, local_err);
    return NULL;
}

GuestMemoryBlockResponseList *
qmp_guest_set_memory_blocks(GuestMemoryBlockList *mem_blks, Error **errp)
{
    GuestMemoryBlockResponseList *head, **tail;
    Error *local_err = NULL;

    head = NULL;
    tail = &head;

    while (mem_blks != NULL) {
        GuestMemoryBlockResponse *result;
        GuestMemoryBlock *current_mem_blk = mem_blks->value;

        result = g_malloc0(sizeof(*result));
        result->phys_index = current_mem_blk->phys_index;
        transfer_memory_block(current_mem_blk, false, result, &local_err);
        if (local_err) { /* should never happen */
            goto err;
        }

        QAPI_LIST_APPEND(tail, result);
        mem_blks = mem_blks->next;
    }

    return head;
err:
    qapi_free_GuestMemoryBlockResponseList(head);
    error_propagate(errp, local_err);
    return NULL;
}

GuestMemoryBlockInfo *qmp_guest_get_memory_block_info(Error **errp)
{
    Error *local_err = NULL;
    char *dirpath;
    int dirfd;
    char *buf;
    GuestMemoryBlockInfo *info;

    dirpath = g_strdup_printf("/sys/devices/system/memory/");
    dirfd = open(dirpath, O_RDONLY | O_DIRECTORY);
    if (dirfd == -1) {
        error_setg_errno(errp, errno, "open(\"%s\")", dirpath);
        g_free(dirpath);
        return NULL;
    }
    g_free(dirpath);

    buf = g_malloc0(20);
    ga_read_sysfs_file(dirfd, "block_size_bytes", buf, 20, &local_err);
    close(dirfd);
    if (local_err) {
        g_free(buf);
        error_propagate(errp, local_err);
        return NULL;
    }

    info = g_new0(GuestMemoryBlockInfo, 1);
    info->size = strtol(buf, NULL, 16); /* the unit is bytes */

    g_free(buf);

    return info;
}

#define MAX_NAME_LEN 128
static GuestDiskStatsInfoList *guest_get_diskstats(Error **errp)
{
#ifdef CONFIG_LINUX
    GuestDiskStatsInfoList *head = NULL, **tail = &head;
    const char *diskstats = "/proc/diskstats";
    FILE *fp;
    size_t n;
    char *line = NULL;

    fp = fopen(diskstats, "r");
    if (fp  == NULL) {
        error_setg_errno(errp, errno, "open(\"%s\")", diskstats);
        return NULL;
    }

    while (getline(&line, &n, fp) != -1) {
        g_autofree GuestDiskStatsInfo *diskstatinfo = NULL;
        g_autofree GuestDiskStats *diskstat = NULL;
        char dev_name[MAX_NAME_LEN];
        unsigned int ios_pgr, tot_ticks, rq_ticks, wr_ticks, dc_ticks, fl_ticks;
        unsigned long rd_ios, rd_merges_or_rd_sec, rd_ticks_or_wr_sec, wr_ios;
        unsigned long wr_merges, rd_sec_or_wr_ios, wr_sec;
        unsigned long dc_ios, dc_merges, dc_sec, fl_ios;
        unsigned int major, minor;
        int i;

        i = sscanf(line, "%u %u %s %lu %lu %lu"
                   "%lu %lu %lu %lu %u %u %u %u"
                   "%lu %lu %lu %u %lu %u",
                   &major, &minor, dev_name,
                   &rd_ios, &rd_merges_or_rd_sec, &rd_sec_or_wr_ios,
                   &rd_ticks_or_wr_sec, &wr_ios, &wr_merges, &wr_sec,
                   &wr_ticks, &ios_pgr, &tot_ticks, &rq_ticks,
                   &dc_ios, &dc_merges, &dc_sec, &dc_ticks,
                   &fl_ios, &fl_ticks);

        if (i < 7) {
            continue;
        }

        diskstatinfo = g_new0(GuestDiskStatsInfo, 1);
        diskstatinfo->name = g_strdup(dev_name);
        diskstatinfo->major = major;
        diskstatinfo->minor = minor;

        diskstat = g_new0(GuestDiskStats, 1);
        if (i == 7) {
            diskstat->has_read_ios = true;
            diskstat->read_ios = rd_ios;
            diskstat->has_read_sectors = true;
            diskstat->read_sectors = rd_merges_or_rd_sec;
            diskstat->has_write_ios = true;
            diskstat->write_ios = rd_sec_or_wr_ios;
            diskstat->has_write_sectors = true;
            diskstat->write_sectors = rd_ticks_or_wr_sec;
        }
        if (i >= 14) {
            diskstat->has_read_ios = true;
            diskstat->read_ios = rd_ios;
            diskstat->has_read_sectors = true;
            diskstat->read_sectors = rd_sec_or_wr_ios;
            diskstat->has_read_merges = true;
            diskstat->read_merges = rd_merges_or_rd_sec;
            diskstat->has_read_ticks = true;
            diskstat->read_ticks = rd_ticks_or_wr_sec;
            diskstat->has_write_ios = true;
            diskstat->write_ios = wr_ios;
            diskstat->has_write_sectors = true;
            diskstat->write_sectors = wr_sec;
            diskstat->has_write_merges = true;
            diskstat->write_merges = wr_merges;
            diskstat->has_write_ticks = true;
            diskstat->write_ticks = wr_ticks;
            diskstat->has_ios_pgr = true;
            diskstat->ios_pgr = ios_pgr;
            diskstat->has_total_ticks = true;
            diskstat->total_ticks = tot_ticks;
            diskstat->has_weight_ticks = true;
            diskstat->weight_ticks = rq_ticks;
        }
        if (i >= 18) {
            diskstat->has_discard_ios = true;
            diskstat->discard_ios = dc_ios;
            diskstat->has_discard_merges = true;
            diskstat->discard_merges = dc_merges;
            diskstat->has_discard_sectors = true;
            diskstat->discard_sectors = dc_sec;
            diskstat->has_discard_ticks = true;
            diskstat->discard_ticks = dc_ticks;
        }
        if (i >= 20) {
            diskstat->has_flush_ios = true;
            diskstat->flush_ios = fl_ios;
            diskstat->has_flush_ticks = true;
            diskstat->flush_ticks = fl_ticks;
        }

        diskstatinfo->stats = g_steal_pointer(&diskstat);
        QAPI_LIST_APPEND(tail, diskstatinfo);
        diskstatinfo = NULL;
    }
    free(line);
    fclose(fp);
    return head;
#else
    g_debug("disk stats reporting available only for Linux");
    return NULL;
#endif
}

GuestDiskStatsInfoList *qmp_guest_get_diskstats(Error **errp)
{
    return guest_get_diskstats(errp);
}

GuestCpuStatsList *qmp_guest_get_cpustats(Error **errp)
{
    GuestCpuStatsList *head = NULL, **tail = &head;
    const char *cpustats = "/proc/stat";
    int clk_tck = sysconf(_SC_CLK_TCK);
    FILE *fp;
    size_t n;
    char *line = NULL;

    fp = fopen(cpustats, "r");
    if (fp  == NULL) {
        error_setg_errno(errp, errno, "open(\"%s\")", cpustats);
        return NULL;
    }

    while (getline(&line, &n, fp) != -1) {
        GuestCpuStats *cpustat = NULL;
        GuestLinuxCpuStats *linuxcpustat;
        int i;
        unsigned long user, system, idle, iowait, irq, softirq, steal, guest;
        unsigned long nice, guest_nice;
        char name[64];

        i = sscanf(line, "%s %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
                   name, &user, &nice, &system, &idle, &iowait, &irq, &softirq,
                   &steal, &guest, &guest_nice);

        /* drop "cpu 1 2 3 ...", get "cpuX 1 2 3 ..." only */
        if ((i == EOF) || strncmp(name, "cpu", 3) || (name[3] == '\0')) {
            continue;
        }

        if (i < 5) {
            slog("Parsing cpu stat from %s failed, see \"man proc\"", cpustats);
            break;
        }

        cpustat = g_new0(GuestCpuStats, 1);
        cpustat->type = GUEST_CPU_STATS_TYPE_LINUX;

        linuxcpustat = &cpustat->u.q_linux;
        linuxcpustat->cpu = atoi(&name[3]);
        linuxcpustat->user = user * 1000 / clk_tck;
        linuxcpustat->nice = nice * 1000 / clk_tck;
        linuxcpustat->system = system * 1000 / clk_tck;
        linuxcpustat->idle = idle * 1000 / clk_tck;

        if (i > 5) {
            linuxcpustat->has_iowait = true;
            linuxcpustat->iowait = iowait * 1000 / clk_tck;
        }

        if (i > 6) {
            linuxcpustat->has_irq = true;
            linuxcpustat->irq = irq * 1000 / clk_tck;
            linuxcpustat->has_softirq = true;
            linuxcpustat->softirq = softirq * 1000 / clk_tck;
        }

        if (i > 8) {
            linuxcpustat->has_steal = true;
            linuxcpustat->steal = steal * 1000 / clk_tck;
        }

        if (i > 9) {
            linuxcpustat->has_guest = true;
            linuxcpustat->guest = guest * 1000 / clk_tck;
        }

        if (i > 10) {
            linuxcpustat->has_guest = true;
            linuxcpustat->guest = guest * 1000 / clk_tck;
            linuxcpustat->has_guestnice = true;
            linuxcpustat->guestnice = guest_nice * 1000 / clk_tck;
        }

        QAPI_LIST_APPEND(tail, cpustat);
    }

    free(line);
    fclose(fp);
    return head;
}

#else /* defined(__linux__) */

void qmp_guest_suspend_disk(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
}

void qmp_guest_suspend_ram(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
}

void qmp_guest_suspend_hybrid(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
}

GuestLogicalProcessorList *qmp_guest_get_vcpus(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

int64_t qmp_guest_set_vcpus(GuestLogicalProcessorList *vcpus, Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return -1;
}

GuestMemoryBlockList *qmp_guest_get_memory_blocks(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

GuestMemoryBlockResponseList *
qmp_guest_set_memory_blocks(GuestMemoryBlockList *mem_blks, Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

GuestMemoryBlockInfo *qmp_guest_get_memory_block_info(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

#endif

#ifdef HAVE_GETIFADDRS
static GuestNetworkInterface *
guest_find_interface(GuestNetworkInterfaceList *head,
                     const char *name)
{
    for (; head; head = head->next) {
        if (strcmp(head->value->name, name) == 0) {
            return head->value;
        }
    }

    return NULL;
}

static int guest_get_network_stats(const char *name,
                       GuestNetworkInterfaceStat *stats)
{
#ifdef CONFIG_LINUX
    int name_len;
    char const *devinfo = "/proc/net/dev";
    FILE *fp;
    char *line = NULL, *colon;
    size_t n = 0;
    fp = fopen(devinfo, "r");
    if (!fp) {
        g_debug("failed to open network stats %s: %s", devinfo,
                g_strerror(errno));
        return -1;
    }
    name_len = strlen(name);
    while (getline(&line, &n, fp) != -1) {
        long long dummy;
        long long rx_bytes;
        long long rx_packets;
        long long rx_errs;
        long long rx_dropped;
        long long tx_bytes;
        long long tx_packets;
        long long tx_errs;
        long long tx_dropped;
        char *trim_line;
        trim_line = g_strchug(line);
        if (trim_line[0] == '\0') {
            continue;
        }
        colon = strchr(trim_line, ':');
        if (!colon) {
            continue;
        }
        if (colon - name_len  == trim_line &&
           strncmp(trim_line, name, name_len) == 0) {
            if (sscanf(colon + 1,
                "%lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld",
                  &rx_bytes, &rx_packets, &rx_errs, &rx_dropped,
                  &dummy, &dummy, &dummy, &dummy,
                  &tx_bytes, &tx_packets, &tx_errs, &tx_dropped,
                  &dummy, &dummy, &dummy, &dummy) != 16) {
                continue;
            }
            stats->rx_bytes = rx_bytes;
            stats->rx_packets = rx_packets;
            stats->rx_errs = rx_errs;
            stats->rx_dropped = rx_dropped;
            stats->tx_bytes = tx_bytes;
            stats->tx_packets = tx_packets;
            stats->tx_errs = tx_errs;
            stats->tx_dropped = tx_dropped;
            fclose(fp);
            g_free(line);
            return 0;
        }
    }
    fclose(fp);
    g_free(line);
    g_debug("/proc/net/dev: Interface '%s' not found", name);
#else /* !CONFIG_LINUX */
    g_debug("Network stats reporting available only for Linux");
#endif /* !CONFIG_LINUX */
    return -1;
}

#ifndef __FreeBSD__
/*
 * Fill "buf" with MAC address by ifaddrs. Pointer buf must point to a
 * buffer with ETHER_ADDR_LEN length at least.
 *
 * Returns false in case of an error, otherwise true. "obtained" argument
 * is true if a MAC address was obtained successful, otherwise false.
 */
bool guest_get_hw_addr(struct ifaddrs *ifa, unsigned char *buf,
                       bool *obtained, Error **errp)
{
    struct ifreq ifr;
    int sock;

    *obtained = false;

    /* we haven't obtained HW address yet */
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        error_setg_errno(errp, errno, "failed to create socket");
        return false;
    }

    memset(&ifr, 0, sizeof(ifr));
    pstrcpy(ifr.ifr_name, IF_NAMESIZE, ifa->ifa_name);
    if (ioctl(sock, SIOCGIFHWADDR, &ifr) == -1) {
        /*
         * We can't get the hw addr of this interface, but that's not a
         * fatal error.
         */
        if (errno == EADDRNOTAVAIL) {
            /* The interface doesn't have a hw addr (e.g. loopback). */
            g_debug("failed to get MAC address of %s: %s",
                    ifa->ifa_name, strerror(errno));
        } else{
            g_warning("failed to get MAC address of %s: %s",
                      ifa->ifa_name, strerror(errno));
        }
    } else {
#ifdef CONFIG_SOLARIS
        memcpy(buf, &ifr.ifr_addr.sa_data, ETHER_ADDR_LEN);
#else
        memcpy(buf, &ifr.ifr_hwaddr.sa_data, ETHER_ADDR_LEN);
#endif
        *obtained = true;
    }
    close(sock);
    return true;
}
#endif /* __FreeBSD__ */

/*
 * Build information about guest interfaces
 */
GuestNetworkInterfaceList *qmp_guest_network_get_interfaces(Error **errp)
{
    GuestNetworkInterfaceList *head = NULL, **tail = &head;
    struct ifaddrs *ifap, *ifa;

    if (getifaddrs(&ifap) < 0) {
        error_setg_errno(errp, errno, "getifaddrs failed");
        goto error;
    }

    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        GuestNetworkInterface *info;
        GuestIpAddressList **address_tail;
        GuestIpAddress *address_item = NULL;
        GuestNetworkInterfaceStat *interface_stat = NULL;
        char addr4[INET_ADDRSTRLEN];
        char addr6[INET6_ADDRSTRLEN];
        unsigned char mac_addr[ETHER_ADDR_LEN];
        bool obtained;
        void *p;

        g_debug("Processing %s interface", ifa->ifa_name);

        info = guest_find_interface(head, ifa->ifa_name);

        if (!info) {
            info = g_malloc0(sizeof(*info));
            info->name = g_strdup(ifa->ifa_name);

            QAPI_LIST_APPEND(tail, info);
        }

        if (!info->has_hardware_address) {
            if (!guest_get_hw_addr(ifa, mac_addr, &obtained, errp)) {
                goto error;
            }
            if (obtained) {
                info->hardware_address =
                    g_strdup_printf("%02x:%02x:%02x:%02x:%02x:%02x",
                                    (int) mac_addr[0], (int) mac_addr[1],
                                    (int) mac_addr[2], (int) mac_addr[3],
                                    (int) mac_addr[4], (int) mac_addr[5]);
                info->has_hardware_address = true;
            }
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
            address_item->ip_address = g_strdup(addr4);
            address_item->ip_address_type = GUEST_IP_ADDRESS_TYPE_IPV4;

            if (ifa->ifa_netmask) {
                /* Count the number of set bits in netmask.
                 * This is safe as '1' and '0' cannot be shuffled in netmask. */
                p = &((struct sockaddr_in *)ifa->ifa_netmask)->sin_addr;
                address_item->prefix = ctpop32(((uint32_t *) p)[0]);
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
            address_item->ip_address = g_strdup(addr6);
            address_item->ip_address_type = GUEST_IP_ADDRESS_TYPE_IPV6;

            if (ifa->ifa_netmask) {
                /* Count the number of set bits in netmask.
                 * This is safe as '1' and '0' cannot be shuffled in netmask. */
                p = &((struct sockaddr_in6 *)ifa->ifa_netmask)->sin6_addr;
                address_item->prefix =
                    ctpop32(((uint32_t *) p)[0]) +
                    ctpop32(((uint32_t *) p)[1]) +
                    ctpop32(((uint32_t *) p)[2]) +
                    ctpop32(((uint32_t *) p)[3]);
            }
        }

        if (!address_item) {
            continue;
        }

        address_tail = &info->ip_addresses;
        while (*address_tail) {
            address_tail = &(*address_tail)->next;
        }
        QAPI_LIST_APPEND(address_tail, address_item);

        info->has_ip_addresses = true;

        if (!info->has_statistics) {
            interface_stat = g_malloc0(sizeof(*interface_stat));
            if (guest_get_network_stats(info->name, interface_stat) == -1) {
                info->has_statistics = false;
                g_free(interface_stat);
            } else {
                info->statistics = interface_stat;
                info->has_statistics = true;
            }
        }
    }

    freeifaddrs(ifap);
    return head;

error:
    freeifaddrs(ifap);
    qapi_free_GuestNetworkInterfaceList(head);
    return NULL;
}

#else

GuestNetworkInterfaceList *qmp_guest_network_get_interfaces(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

#endif /* HAVE_GETIFADDRS */

#if !defined(CONFIG_FSFREEZE)

GuestFilesystemInfoList *qmp_guest_get_fsinfo(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

GuestFsfreezeStatus qmp_guest_fsfreeze_status(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);

    return 0;
}

int64_t qmp_guest_fsfreeze_freeze(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);

    return 0;
}

int64_t qmp_guest_fsfreeze_freeze_list(bool has_mountpoints,
                                       strList *mountpoints,
                                       Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);

    return 0;
}

int64_t qmp_guest_fsfreeze_thaw(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);

    return 0;
}

GuestDiskInfoList *qmp_guest_get_disks(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

GuestDiskStatsInfoList *qmp_guest_get_diskstats(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

GuestCpuStatsList *qmp_guest_get_cpustats(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

#endif /* CONFIG_FSFREEZE */

#if !defined(CONFIG_FSTRIM)
GuestFilesystemTrimResponse *
qmp_guest_fstrim(bool has_minimum, int64_t minimum, Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}
#endif

/* add unsupported commands to the list of blocked RPCs */
GList *ga_command_init_blockedrpcs(GList *blockedrpcs)
{
#if !defined(__linux__)
    {
        const char *list[] = {
            "guest-suspend-disk", "guest-suspend-ram",
            "guest-suspend-hybrid", "guest-get-vcpus", "guest-set-vcpus",
            "guest-get-memory-blocks", "guest-set-memory-blocks",
            "guest-get-memory-block-size", "guest-get-memory-block-info",
            NULL};
        char **p = (char **)list;

        while (*p) {
            blockedrpcs = g_list_append(blockedrpcs, g_strdup(*p++));
        }
    }
#endif

#if !defined(HAVE_GETIFADDRS)
    blockedrpcs = g_list_append(blockedrpcs,
                              g_strdup("guest-network-get-interfaces"));
#endif

#if !defined(CONFIG_FSFREEZE)
    {
        const char *list[] = {
            "guest-get-fsinfo", "guest-fsfreeze-status",
            "guest-fsfreeze-freeze", "guest-fsfreeze-freeze-list",
            "guest-fsfreeze-thaw", "guest-get-fsinfo",
            "guest-get-disks", NULL};
        char **p = (char **)list;

        while (*p) {
            blockedrpcs = g_list_append(blockedrpcs, g_strdup(*p++));
        }
    }
#endif

#if !defined(CONFIG_FSTRIM)
    blockedrpcs = g_list_append(blockedrpcs, g_strdup("guest-fstrim"));
#endif

    blockedrpcs = g_list_append(blockedrpcs, g_strdup("guest-get-devices"));

    return blockedrpcs;
}

/* register init/cleanup routines for stateful command groups */
void ga_command_state_init(GAState *s, GACommandState *cs)
{
#if defined(CONFIG_FSFREEZE)
    ga_command_state_add(cs, NULL, guest_fsfreeze_cleanup);
#endif
}

#ifdef HAVE_UTMPX

#define QGA_MICRO_SECOND_TO_SECOND 1000000

static double ga_get_login_time(struct utmpx *user_info)
{
    double seconds = (double)user_info->ut_tv.tv_sec;
    double useconds = (double)user_info->ut_tv.tv_usec;
    useconds /= QGA_MICRO_SECOND_TO_SECOND;
    return seconds + useconds;
}

GuestUserList *qmp_guest_get_users(Error **errp)
{
    GHashTable *cache = NULL;
    GuestUserList *head = NULL, **tail = &head;
    struct utmpx *user_info = NULL;
    gpointer value = NULL;
    GuestUser *user = NULL;
    double login_time = 0;

    cache = g_hash_table_new(g_str_hash, g_str_equal);
    setutxent();

    for (;;) {
        user_info = getutxent();
        if (user_info == NULL) {
            break;
        } else if (user_info->ut_type != USER_PROCESS) {
            continue;
        } else if (g_hash_table_contains(cache, user_info->ut_user)) {
            value = g_hash_table_lookup(cache, user_info->ut_user);
            user = (GuestUser *)value;
            login_time = ga_get_login_time(user_info);
            /* We're ensuring the earliest login time to be sent */
            if (login_time < user->login_time) {
                user->login_time = login_time;
            }
            continue;
        }

        user = g_new0(GuestUser, 1);
        user->user = g_strdup(user_info->ut_user);
        user->login_time = ga_get_login_time(user_info);

        g_hash_table_insert(cache, user->user, user);

        QAPI_LIST_APPEND(tail, user);
    }
    endutxent();
    g_hash_table_destroy(cache);
    return head;
}

#else

GuestUserList *qmp_guest_get_users(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}

#endif

/* Replace escaped special characters with theire real values. The replacement
 * is done in place -- returned value is in the original string.
 */
static void ga_osrelease_replace_special(gchar *value)
{
    gchar *p, *p2, quote;

    /* Trim the string at first space or semicolon if it is not enclosed in
     * single or double quotes. */
    if ((value[0] != '"') || (value[0] == '\'')) {
        p = strchr(value, ' ');
        if (p != NULL) {
            *p = 0;
        }
        p = strchr(value, ';');
        if (p != NULL) {
            *p = 0;
        }
        return;
    }

    quote = value[0];
    p2 = value;
    p = value + 1;
    while (*p != 0) {
        if (*p == '\\') {
            p++;
            switch (*p) {
            case '$':
            case '\'':
            case '"':
            case '\\':
            case '`':
                break;
            default:
                /* Keep literal backslash followed by whatever is there */
                p--;
                break;
            }
        } else if (*p == quote) {
            *p2 = 0;
            break;
        }
        *(p2++) = *(p++);
    }
}

static GKeyFile *ga_parse_osrelease(const char *fname)
{
    gchar *content = NULL;
    gchar *content2 = NULL;
    GError *err = NULL;
    GKeyFile *keys = g_key_file_new();
    const char *group = "[os-release]\n";

    if (!g_file_get_contents(fname, &content, NULL, &err)) {
        slog("failed to read '%s', error: %s", fname, err->message);
        goto fail;
    }

    if (!g_utf8_validate(content, -1, NULL)) {
        slog("file is not utf-8 encoded: %s", fname);
        goto fail;
    }
    content2 = g_strdup_printf("%s%s", group, content);

    if (!g_key_file_load_from_data(keys, content2, -1, G_KEY_FILE_NONE,
                                   &err)) {
        slog("failed to parse file '%s', error: %s", fname, err->message);
        goto fail;
    }

    g_free(content);
    g_free(content2);
    return keys;

fail:
    g_error_free(err);
    g_free(content);
    g_free(content2);
    g_key_file_free(keys);
    return NULL;
}

GuestOSInfo *qmp_guest_get_osinfo(Error **errp)
{
    GuestOSInfo *info = NULL;
    struct utsname kinfo;
    GKeyFile *osrelease = NULL;
    const char *qga_os_release = g_getenv("QGA_OS_RELEASE");

    info = g_new0(GuestOSInfo, 1);

    if (uname(&kinfo) != 0) {
        error_setg_errno(errp, errno, "uname failed");
    } else {
        info->has_kernel_version = true;
        info->kernel_version = g_strdup(kinfo.version);
        info->has_kernel_release = true;
        info->kernel_release = g_strdup(kinfo.release);
        info->has_machine = true;
        info->machine = g_strdup(kinfo.machine);
    }

    if (qga_os_release != NULL) {
        osrelease = ga_parse_osrelease(qga_os_release);
    } else {
        osrelease = ga_parse_osrelease("/etc/os-release");
        if (osrelease == NULL) {
            osrelease = ga_parse_osrelease("/usr/lib/os-release");
        }
    }

    if (osrelease != NULL) {
        char *value;

#define GET_FIELD(field, osfield) do { \
    value = g_key_file_get_value(osrelease, "os-release", osfield, NULL); \
    if (value != NULL) { \
        ga_osrelease_replace_special(value); \
        info->has_ ## field = true; \
        info->field = value; \
    } \
} while (0)
        GET_FIELD(id, "ID");
        GET_FIELD(name, "NAME");
        GET_FIELD(pretty_name, "PRETTY_NAME");
        GET_FIELD(version, "VERSION");
        GET_FIELD(version_id, "VERSION_ID");
        GET_FIELD(variant, "VARIANT");
        GET_FIELD(variant_id, "VARIANT_ID");
#undef GET_FIELD

        g_key_file_free(osrelease);
    }

    return info;
}

GuestDeviceInfoList *qmp_guest_get_devices(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);

    return NULL;
}

#ifndef HOST_NAME_MAX
# ifdef _POSIX_HOST_NAME_MAX
#  define HOST_NAME_MAX _POSIX_HOST_NAME_MAX
# else
#  define HOST_NAME_MAX 255
# endif
#endif

char *qga_get_host_name(Error **errp)
{
    long len = -1;
    g_autofree char *hostname = NULL;

#ifdef _SC_HOST_NAME_MAX
    len = sysconf(_SC_HOST_NAME_MAX);
#endif /* _SC_HOST_NAME_MAX */

    if (len < 0) {
        len = HOST_NAME_MAX;
    }

    /* Unfortunately, gethostname() below does not guarantee a
     * NULL terminated string. Therefore, allocate one byte more
     * to be sure. */
    hostname = g_new0(char, len + 1);

    if (gethostname(hostname, len) < 0) {
        error_setg_errno(errp, errno,
                         "cannot get hostname");
        return NULL;
    }

    return g_steal_pointer(&hostname);
}
