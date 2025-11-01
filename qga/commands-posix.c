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
#include "qemu/host-utils.h"
#include "qemu/sockets.h"
#include "qemu/base64.h"
#include "qemu/cutils.h"
#include "commands-common.h"
#include "cutils.h"

#ifdef HAVE_UTMPX
#include <utmpx.h>
#endif

#ifdef HAVE_GETIFADDRS
#include <arpa/inet.h>
#include <sys/socket.h>
#include <net/if.h>
#if defined(__NetBSD__) || defined(__OpenBSD__) || defined(CONFIG_SOLARIS)
#include <net/if_arp.h>
#include <netinet/if_ether.h>
#if !defined(ETHER_ADDR_LEN) && defined(ETHERADDRL)
#define ETHER_ADDR_LEN ETHERADDRL
#endif
#else
#include <net/ethernet.h>
#endif
#ifdef CONFIG_SOLARIS
#ifdef CONFIG_GETLOADAVG
#include <sys/loadavg.h>
#endif
#include <sys/sockio.h>
#endif
#endif

static bool ga_wait_child(pid_t pid, int *status, Error **errp)
{
    pid_t rpid;

    *status = 0;

    rpid = RETRY_ON_EINTR(waitpid(pid, status, 0));

    if (rpid == -1) {
        error_setg_errno(errp, errno, "failed to wait for child (pid: %d)",
                         pid);
        return false;
    }

    g_assert(rpid == pid);
    return true;
}

static ssize_t ga_pipe_read_str(int fd[2], char **str)
{
    ssize_t n, len = 0;
    char buf[1024];

    close(fd[1]);
    fd[1] = -1;
    while ((n = read(fd[0], buf, sizeof(buf))) != 0) {
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            } else {
                len = -errno;
                break;
            }
        }
        *str = g_realloc(*str, len + n + 1);
        memcpy(*str + len, buf, n);
        len += n;
        (*str)[len] = '\0';
    }
    close(fd[0]);
    fd[0] = -1;

    return len;
}

/*
 * Helper to run command with input/output redirection,
 * sending string to stdin and taking error message from
 * stdout/err.
 */
static int ga_run_command(const char *argv[], const char *in_str,
                          const char *action, Error **errp)
{
    pid_t pid;
    int status;
    int retcode = -1;
    int infd[2] = { -1, -1 };
    int outfd[2] = { -1, -1 };
    char *str = NULL;
    ssize_t len = 0;

    if ((in_str && !g_unix_open_pipe(infd, FD_CLOEXEC, NULL)) ||
        !g_unix_open_pipe(outfd, FD_CLOEXEC, NULL)) {
        error_setg(errp, "cannot create pipe FDs");
        goto out;
    }

    pid = fork();
    if (pid == 0) {
        char *cherr = NULL;

        setsid();

        if (in_str) {
            /* Redirect stdin to infd. */
            close(infd[1]);
            dup2(infd[0], 0);
            close(infd[0]);
        } else {
            reopen_fd_to_null(0);
        }

        /* Redirect stdout/stderr to outfd. */
        close(outfd[0]);
        dup2(outfd[1], 1);
        dup2(outfd[1], 2);
        close(outfd[1]);

        execvp(argv[0], (char *const *)argv);

        /* Write the cause of failed exec to pipe for the parent to read it. */
        cherr = g_strdup_printf("failed to exec '%s'", argv[0]);
        perror(cherr);
        g_free(cherr);
        _exit(EXIT_FAILURE);
    } else if (pid < 0) {
        error_setg_errno(errp, errno, "failed to create child process");
        goto out;
    }

    if (in_str) {
        close(infd[0]);
        infd[0] = -1;
        if (qemu_write_full(infd[1], in_str, strlen(in_str)) !=
                strlen(in_str)) {
            error_setg_errno(errp, errno, "%s: cannot write to stdin pipe",
                             action);
            goto out;
        }
        close(infd[1]);
        infd[1] = -1;
    }

    len = ga_pipe_read_str(outfd, &str);
    if (len < 0) {
        error_setg_errno(errp, -len, "%s: cannot read from stdout/stderr pipe",
                         action);
        goto out;
    }

    if (!ga_wait_child(pid, &status, errp)) {
        goto out;
    }

    if (!WIFEXITED(status)) {
        if (len) {
            error_setg(errp, "child process has terminated abnormally: %s",
                       str);
        } else {
            error_setg(errp, "child process has terminated abnormally");
        }
        goto out;
    }

    retcode = WEXITSTATUS(status);

    if (WEXITSTATUS(status)) {
        if (len) {
            error_setg(errp, "child process has failed to %s: %s",
                       action, str);
        } else {
            error_setg(errp, "child process has failed to %s: exit status %d",
                       action, WEXITSTATUS(status));
        }
        goto out;
    }

out:
    g_free(str);

    if (infd[0] != -1) {
        close(infd[0]);
    }
    if (infd[1] != -1) {
        close(infd[1]);
    }
    if (outfd[0] != -1) {
        close(outfd[0]);
    }
    if (outfd[1] != -1) {
        close(outfd[1]);
    }

    return retcode;
}

#define POWEROFF_CMD_PATH "/sbin/poweroff"
#define HALT_CMD_PATH "/sbin/halt"
#define REBOOT_CMD_PATH "/sbin/reboot"

void qmp_guest_shutdown(const char *mode, Error **errp)
{
    const char *shutdown_flag;
    const char *shutdown_cmd = NULL;
    Error *local_err = NULL;

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
    if (!mode || strcmp(mode, "powerdown") == 0) {
        if (access(POWEROFF_CMD_PATH, X_OK) == 0) {
            shutdown_cmd = POWEROFF_CMD_PATH;
        }
        shutdown_flag = powerdown_flag;
    } else if (strcmp(mode, "halt") == 0) {
        if (access(HALT_CMD_PATH, X_OK) == 0) {
            shutdown_cmd = HALT_CMD_PATH;
        }
        shutdown_flag = halt_flag;
    } else if (strcmp(mode, "reboot") == 0) {
        if (access(REBOOT_CMD_PATH, X_OK) == 0) {
            shutdown_cmd = REBOOT_CMD_PATH;
        }
        shutdown_flag = reboot_flag;
    } else {
        error_setg(errp,
                   "mode is invalid (valid values are: halt|powerdown|reboot");
        return;
    }

    const char *argv[] = {"/sbin/shutdown",
#ifdef CONFIG_SOLARIS
                          shutdown_flag, "-g0", "-y",
#elif defined(CONFIG_BSD)
                          shutdown_flag, "+0",
#else
                          "-h", shutdown_flag, "+0",
#endif
                          "hypervisor initiated shutdown", (char *) NULL};

    /*
     * If the specific command exists (poweroff, halt or reboot), use it instead
     * of /sbin/shutdown.
     */
    if (shutdown_cmd != NULL) {
        argv[0] = shutdown_cmd;
        argv[1] = NULL;
    }

    ga_run_command(argv, NULL, "shutdown", &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* succeeded */
}

void qmp_guest_set_time(bool has_time, int64_t time_ns, Error **errp)
{
    int ret;
    Error *local_err = NULL;
    struct timeval tv;
    const char *argv[] = {"/sbin/hwclock", has_time ? "-w" : "-s", NULL};

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
    ga_run_command(argv, NULL, "set hardware clock to system time",
                   &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
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

int64_t qmp_guest_file_open(const char *path, const char *mode,
                            Error **errp)
{
    FILE *fh;
    Error *local_err = NULL;
    int64_t handle;

    if (!mode) {
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
    if (!qemu_set_blocking(fileno(fh), false, errp)) {
        fclose(fh);
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
    const char *hook;
    const char *arg_str = fsfreeze_hook_arg_string[arg];
    Error *local_err = NULL;

    hook = ga_fsfreeze_hook(ga_state);
    if (!hook) {
        return;
    }

    const char *argv[] = {hook, arg_str, NULL};

    slog("executing fsfreeze hook with arg '%s'", arg_str);
    ga_run_command(argv, NULL, "execute fsfreeze hook", &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
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
        slog("guest-fsthaw called");
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

#if defined(__linux__) || defined(__FreeBSD__)
void qmp_guest_set_user_password(const char *username,
                                 const char *password,
                                 bool crypted,
                                 Error **errp)
{
    Error *local_err = NULL;
    g_autofree char *rawpasswddata = NULL;
    size_t rawpasswdlen;

    rawpasswddata = (char *)qbase64_decode(password, -1, &rawpasswdlen, errp);
    if (!rawpasswddata) {
        return;
    }
    rawpasswddata = g_renew(char, rawpasswddata, rawpasswdlen + 1);
    rawpasswddata[rawpasswdlen] = '\0';

    if (strchr(rawpasswddata, '\n')) {
        error_setg(errp, "forbidden characters in raw password");
        return;
    }

    if (strchr(username, '\n') ||
        strchr(username, ':')) {
        error_setg(errp, "forbidden characters in username");
        return;
    }

#ifdef __FreeBSD__
    g_autofree char *chpasswddata = g_strdup(rawpasswddata);
    const char *crypt_flag = crypted ? "-H" : "-h";
    const char *argv[] = {"pw", "usermod", "-n", username,
                          crypt_flag, "0", NULL};
#else
    g_autofree char *chpasswddata = g_strdup_printf("%s:%s\n", username,
                                                    rawpasswddata);
    const char *crypt_flag = crypted ? "-e" : NULL;
    const char *argv[] = {"chpasswd", crypt_flag, NULL};
#endif

    ga_run_command(argv, chpasswddata, "set user password", &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
}
#endif /* __linux__ || __FreeBSD__ */

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

#ifndef CONFIG_BSD
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
#endif /* CONFIG_BSD */

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

        if (!info->hardware_address) {
            if (!guest_get_hw_addr(ifa, mac_addr, &obtained, errp)) {
                goto error;
            }
            if (obtained) {
                info->hardware_address =
                    g_strdup_printf("%02x:%02x:%02x:%02x:%02x:%02x",
                                    (int) mac_addr[0], (int) mac_addr[1],
                                    (int) mac_addr[2], (int) mac_addr[3],
                                    (int) mac_addr[4], (int) mac_addr[5]);
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

        if (!info->statistics) {
            interface_stat = g_malloc0(sizeof(*interface_stat));
            if (guest_get_network_stats(info->name, interface_stat) == -1) {
                g_free(interface_stat);
            } else {
                info->statistics = interface_stat;
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

#endif /* HAVE_GETIFADDRS */

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

#endif /* HAVE_UTMPX */

/* Replace escaped special characters with their real values. The replacement
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
        info->kernel_version = g_strdup(kinfo.version);
        info->kernel_release = g_strdup(kinfo.release);
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

#ifdef CONFIG_GETLOADAVG
GuestLoadAverage *qmp_guest_get_load(Error **errp)
{
    double loadavg[3];
    GuestLoadAverage *ret = NULL;

    if (getloadavg(loadavg, G_N_ELEMENTS(loadavg)) < 0) {
        error_setg_errno(errp, errno,
                         "cannot query load average");
        return NULL;
    }

    ret = g_new0(GuestLoadAverage, 1);
    ret->load1m = loadavg[0];
    ret->load5m = loadavg[1];
    ret->load15m = loadavg[2];
    return ret;
}
#endif
