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
#include "qemu-common.h"
#include "guest-agent-core.h"
#include "qga-qapi-commands.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/queue.h"
#include "qemu/host-utils.h"
#include "qemu/sockets.h"
#include "qemu/base64.h"
#include "qemu/cutils.h"
#include "commands-common.h"

#ifdef HAVE_UTMPX
#include <utmpx.h>
#endif

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
#include <sys/statvfs.h>

#ifdef CONFIG_LIBUDEV
#include <libudev.h>
#endif

#ifdef FIFREEZE
#define CONFIG_FSFREEZE
#endif
#ifdef FITRIM
#define CONFIG_FSTRIM
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

    slog("guest-shutdown called, mode: %s", mode);
    if (!has_mode || strcmp(mode, "powerdown") == 0) {
        shutdown_flag = "-P";
    } else if (strcmp(mode, "halt") == 0) {
        shutdown_flag = "-H";
    } else if (strcmp(mode, "reboot") == 0) {
        shutdown_flag = "-r";
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

        execle("/sbin/shutdown", "shutdown", "-h", shutdown_flag, "+0",
               "hypervisor initiated shutdown", (char*)NULL, environ);
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

int64_t qmp_guest_get_time(Error **errp)
{
   int ret;
   qemu_timeval tq;

   ret = qemu_gettimeofday(&tq);
   if (ret < 0) {
       error_setg_errno(errp, errno, "Failed to get time");
       return -1;
   }

   return tq.tv_sec * 1000000000LL + tq.tv_usec * 1000;
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
        execle(hwclock_path, "hwclock", has_time ? "-w" : "-s",
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

    error_propagate(errp, local_err);
    return NULL;
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
    qemu_set_nonblock(fileno(fh));

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

    buf = g_malloc0(count+1);
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

/* linux-specific implementations. avoid this if at all possible. */
#if defined(__linux__)

#if defined(CONFIG_FSFREEZE) || defined(CONFIG_FSTRIM)
typedef struct FsMount {
    char *dirname;
    char *devtype;
    unsigned int devmajor, devminor;
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

static int dev_major_minor(const char *devpath,
                           unsigned int *devmajor, unsigned int *devminor)
{
    struct stat st;

    *devmajor = 0;
    *devminor = 0;

    if (stat(devpath, &st) < 0) {
        slog("failed to stat device file '%s': %s", devpath, strerror(errno));
        return -1;
    }
    if (S_ISDIR(st.st_mode)) {
        /* It is bind mount */
        return -2;
    }
    if (S_ISBLK(st.st_mode)) {
        *devmajor = major(st.st_rdev);
        *devminor = minor(st.st_rdev);
        return 0;
    }
    return -1;
}

/*
 * Walk the mount table and build a list of local file systems
 */
static void build_fs_mount_list_from_mtab(FsMountList *mounts, Error **errp)
{
    struct mntent *ment;
    FsMount *mount;
    char const *mtab = "/proc/self/mounts";
    FILE *fp;
    unsigned int devmajor, devminor;

    fp = setmntent(mtab, "r");
    if (!fp) {
        error_setg(errp, "failed to open mtab file: '%s'", mtab);
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
        if (dev_major_minor(ment->mnt_fsname, &devmajor, &devminor) == -2) {
            /* Skip bind mounts */
            continue;
        }

        mount = g_new0(FsMount, 1);
        mount->dirname = g_strdup(ment->mnt_dir);
        mount->devtype = g_strdup(ment->mnt_type);
        mount->devmajor = devmajor;
        mount->devminor = devminor;

        QTAILQ_INSERT_TAIL(mounts, mount, next);
    }

    endmntent(fp);
}

static void decode_mntname(char *name, int len)
{
    int i, j = 0;
    for (i = 0; i <= len; i++) {
        if (name[i] != '\\') {
            name[j++] = name[i];
        } else if (name[i + 1] == '\\') {
            name[j++] = '\\';
            i++;
        } else if (name[i + 1] >= '0' && name[i + 1] <= '3' &&
                   name[i + 2] >= '0' && name[i + 2] <= '7' &&
                   name[i + 3] >= '0' && name[i + 3] <= '7') {
            name[j++] = (name[i + 1] - '0') * 64 +
                        (name[i + 2] - '0') * 8 +
                        (name[i + 3] - '0');
            i += 3;
        } else {
            name[j++] = name[i];
        }
    }
}

static void build_fs_mount_list(FsMountList *mounts, Error **errp)
{
    FsMount *mount;
    char const *mountinfo = "/proc/self/mountinfo";
    FILE *fp;
    char *line = NULL, *dash;
    size_t n;
    char check;
    unsigned int devmajor, devminor;
    int ret, dir_s, dir_e, type_s, type_e, dev_s, dev_e;

    fp = fopen(mountinfo, "r");
    if (!fp) {
        build_fs_mount_list_from_mtab(mounts, errp);
        return;
    }

    while (getline(&line, &n, fp) != -1) {
        ret = sscanf(line, "%*u %*u %u:%u %*s %n%*s%n%c",
                     &devmajor, &devminor, &dir_s, &dir_e, &check);
        if (ret < 3) {
            continue;
        }
        dash = strstr(line + dir_e, " - ");
        if (!dash) {
            continue;
        }
        ret = sscanf(dash, " - %n%*s%n %n%*s%n%c",
                     &type_s, &type_e, &dev_s, &dev_e, &check);
        if (ret < 1) {
            continue;
        }
        line[dir_e] = 0;
        dash[type_e] = 0;
        dash[dev_e] = 0;
        decode_mntname(line + dir_s, dir_e - dir_s);
        decode_mntname(dash + dev_s, dev_e - dev_s);
        if (devmajor == 0) {
            /* btrfs reports major number = 0 */
            if (strcmp("btrfs", dash + type_s) != 0 ||
                dev_major_minor(dash + dev_s, &devmajor, &devminor) < 0) {
                continue;
            }
        }

        mount = g_new0(FsMount, 1);
        mount->dirname = g_strdup(line + dir_s);
        mount->devtype = g_strdup(dash + type_s);
        mount->devmajor = devmajor;
        mount->devminor = devminor;

        QTAILQ_INSERT_TAIL(mounts, mount, next);
    }
    free(line);

    fclose(fp);
}
#endif

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

/* Store disk device info specified by @sysfs into @fs */
static void build_guest_fsinfo_for_real_device(char const *syspath,
                                               GuestFilesystemInfo *fs,
                                               Error **errp)
{
    unsigned int pci[4], host, hosts[8], tgt[3];
    int i, nhosts = 0, pcilen;
    GuestDiskAddress *disk;
    GuestPCIAddress *pciaddr;
    GuestDiskAddressList *list = NULL;
    bool has_ata = false, has_host = false, has_tgt = false;
    char *p, *q, *driver = NULL;
#ifdef CONFIG_LIBUDEV
    struct udev *udev = NULL;
    struct udev_device *udevice = NULL;
#endif

    p = strstr(syspath, "/devices/pci");
    if (!p || sscanf(p + 12, "%*x:%*x/%x:%x:%x.%x%n",
                     pci, pci + 1, pci + 2, pci + 3, &pcilen) < 4) {
        g_debug("only pci device is supported: sysfs path '%s'", syspath);
        return;
    }

    p += 12 + pcilen;
    while (true) {
        driver = get_pci_driver(syspath, p - syspath, errp);
        if (driver && (g_str_equal(driver, "ata_piix") ||
                       g_str_equal(driver, "sym53c8xx") ||
                       g_str_equal(driver, "virtio-pci") ||
                       g_str_equal(driver, "ahci"))) {
            break;
        }

        g_free(driver);
        if (sscanf(p, "/%x:%x:%x.%x%n",
                          pci, pci + 1, pci + 2, pci + 3, &pcilen) == 4) {
            p += pcilen;
            continue;
        }

        g_debug("unsupported driver or sysfs path '%s'", syspath);
        return;
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

    pciaddr = g_malloc0(sizeof(*pciaddr));
    pciaddr->domain = pci[0];
    pciaddr->bus = pci[1];
    pciaddr->slot = pci[2];
    pciaddr->function = pci[3];

    disk = g_malloc0(sizeof(*disk));
    disk->pci_controller = pciaddr;

    list = g_malloc0(sizeof(*list));
    list->value = disk;

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
#endif

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
    } else {
        g_debug("unknown driver '%s' (sysfs path '%s')", driver, syspath);
        goto cleanup;
    }

    list->next = fs->disk;
    fs->disk = list;
    goto out;

cleanup:
    if (list) {
        qapi_free_GuestDiskAddressList(list);
    }
out:
    g_free(driver);
#ifdef CONFIG_LIBUDEV
    udev_unref(udev);
    udev_device_unref(udevice);
#endif
    return;
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

/* Dispatch to functions for virtual/real device */
static void build_guest_fsinfo_for_device(char const *devpath,
                                          GuestFilesystemInfo *fs,
                                          Error **errp)
{
    char *syspath = realpath(devpath, NULL);

    if (!syspath) {
        error_setg_errno(errp, errno, "realpath(\"%s\")", devpath);
        return;
    }

    if (!fs->name) {
        fs->name = g_path_get_basename(syspath);
    }

    g_debug("  parse sysfs path '%s'", syspath);

    if (strstr(syspath, "/devices/virtual/block/")) {
        build_guest_fsinfo_for_virtual_device(syspath, fs, errp);
    } else {
        build_guest_fsinfo_for_real_device(syspath, fs, errp);
    }

    free(syspath);
}

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
    GuestFilesystemInfoList *new, *ret = NULL;
    Error *local_err = NULL;

    QTAILQ_INIT(&mounts);
    build_fs_mount_list(&mounts, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return NULL;
    }

    QTAILQ_FOREACH(mount, &mounts, next) {
        g_debug("Building guest fsinfo for '%s'", mount->dirname);

        new = g_malloc0(sizeof(*ret));
        new->value = build_guest_fsinfo(mount, &local_err);
        new->next = ret;
        ret = new;
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

        execle(hook, hook, arg_str, NULL, environ);
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

/*
 * Walk list of mounted file systems in the guest, and freeze the ones which
 * are real local file systems.
 */
int64_t qmp_guest_fsfreeze_freeze_list(bool has_mountpoints,
                                       strList *mountpoints,
                                       Error **errp)
{
    int ret = 0, i = 0;
    strList *list;
    FsMountList mounts;
    struct FsMount *mount;
    Error *local_err = NULL;
    int fd;

    slog("guest-fsfreeze called");

    execute_fsfreeze_hook(FSFREEZE_HOOK_FREEZE, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return -1;
    }

    QTAILQ_INIT(&mounts);
    build_fs_mount_list(&mounts, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return -1;
    }

    /* cannot risk guest agent blocking itself on a write in this state */
    ga_set_frozen(ga_state);

    QTAILQ_FOREACH_REVERSE(mount, &mounts, next) {
        /* To issue fsfreeze in the reverse order of mounts, check if the
         * mount is listed in the list here */
        if (has_mountpoints) {
            for (list = mountpoints; list; list = list->next) {
                if (strcmp(list->value, mount->dirname) == 0) {
                    break;
                }
            }
            if (!list) {
                continue;
            }
        }

        fd = qemu_open(mount->dirname, O_RDONLY);
        if (fd == -1) {
            error_setg_errno(errp, errno, "failed to open %s", mount->dirname);
            goto error;
        }

        /* we try to cull filesystems we know won't work in advance, but other
         * filesystems may not implement fsfreeze for less obvious reasons.
         * these will report EOPNOTSUPP. we simply ignore these when tallying
         * the number of frozen filesystems.
         * if a filesystem is mounted more than once (aka bind mount) a
         * consecutive attempt to freeze an already frozen filesystem will
         * return EBUSY.
         *
         * any other error means a failure to freeze a filesystem we
         * expect to be freezable, so return an error in those cases
         * and return system to thawed state.
         */
        ret = ioctl(fd, FIFREEZE);
        if (ret == -1) {
            if (errno != EOPNOTSUPP && errno != EBUSY) {
                error_setg_errno(errp, errno, "failed to freeze %s",
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
    /* We may not issue any FIFREEZE here.
     * Just unset ga_state here and ready for the next call.
     */
    if (i == 0) {
        ga_unset_frozen(ga_state);
    }
    return i;

error:
    free_fs_mount_list(&mounts);
    qmp_guest_fsfreeze_thaw(NULL);
    return 0;
}

/*
 * Walk list of frozen file systems in the guest, and thaw them.
 */
int64_t qmp_guest_fsfreeze_thaw(Error **errp)
{
    int ret;
    FsMountList mounts;
    FsMount *mount;
    int fd, i = 0, logged;
    Error *local_err = NULL;

    QTAILQ_INIT(&mounts);
    build_fs_mount_list(&mounts, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
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

    execute_fsfreeze_hook(FSFREEZE_HOOK_THAW, errp);

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
GuestFilesystemTrimResponse *
qmp_guest_fstrim(bool has_minimum, int64_t minimum, Error **errp)
{
    GuestFilesystemTrimResponse *response;
    GuestFilesystemTrimResultList *list;
    GuestFilesystemTrimResult *result;
    int ret = 0;
    FsMountList mounts;
    struct FsMount *mount;
    int fd;
    Error *local_err = NULL;
    struct fstrim_range r;

    slog("guest-fstrim called");

    QTAILQ_INIT(&mounts);
    build_fs_mount_list(&mounts, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return NULL;
    }

    response = g_malloc0(sizeof(*response));

    QTAILQ_FOREACH(mount, &mounts, next) {
        result = g_malloc0(sizeof(*result));
        result->path = g_strdup(mount->dirname);

        list = g_malloc0(sizeof(*list));
        list->value = result;
        list->next = response->paths;
        response->paths = list;

        fd = qemu_open(mount->dirname, O_RDONLY);
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

    success =  g_spawn_sync(NULL, (char **)command, environ, spawn_flag,
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
    Error *local_err = NULL;
    const char *systemctl_args[3] = {"systemd-hibernate", "systemd-suspend",
                                     "systemd-hybrid-sleep"};
    const char *cmd[4] = {"systemctl", "status", systemctl_args[mode], NULL};
    int status;

    status = run_process_child(cmd, &local_err);

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

    error_propagate(errp, local_err);
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

static int guest_get_network_stats(const char *name,
                       GuestNetworkInterfaceStat *stats)
{
    int name_len;
    char const *devinfo = "/proc/net/dev";
    FILE *fp;
    char *line = NULL, *colon;
    size_t n = 0;
    fp = fopen(devinfo, "r");
    if (!fp) {
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
    return -1;
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
        GuestNetworkInterfaceStat  *interface_stat = NULL;
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

        if (!info->value->has_statistics) {
            interface_stat = g_malloc0(sizeof(*interface_stat));
            if (guest_get_network_stats(info->value->name,
                interface_stat) == -1) {
                info->value->has_statistics = false;
                g_free(interface_stat);
            } else {
                info->value->statistics = interface_stat;
                info->value->has_statistics = true;
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

#define SYSCONF_EXACT(name, errp) sysconf_exact((name), #name, (errp))

static long sysconf_exact(int name, const char *name_str, Error **errp)
{
    long ret;

    errno = 0;
    ret = sysconf(name);
    if (ret == -1) {
        if (errno == 0) {
            error_setg(errp, "sysconf(%s): value indefinite", name_str);
        } else {
            error_setg_errno(errp, errno, "sysconf(%s)", name_str);
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
        int64_t id = current++;
        char *path = g_strdup_printf("/sys/devices/system/cpu/cpu%" PRId64 "/",
                                     id);

        if (g_file_test(path, G_FILE_TEST_EXISTS)) {
            vcpu = g_malloc0(sizeof *vcpu);
            vcpu->logical_id = id;
            vcpu->has_can_offline = true; /* lolspeak ftw */
            transfer_vcpu(vcpu, true, path, &local_err);
            entry = g_malloc0(sizeof *entry);
            entry->value = vcpu;
            *link = entry;
            link = &entry->next;
        }
        g_free(path);
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

    chpasswddata = g_strdup_printf("%s:%s\n", username, rawpasswddata);
    chpasswdlen = strlen(chpasswddata);

    passwd_path = g_find_program_in_path("chpasswd");

    if (!passwd_path) {
        error_setg(errp, "cannot find 'passwd' program in PATH");
        goto out;
    }

    if (pipe(datafd) < 0) {
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

        if (crypted) {
            execle(passwd_path, "chpasswd", "-e", NULL, environ);
        } else {
            execle(passwd_path, "chpasswd", NULL, environ);
        }
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
    GuestMemoryBlockList *head, **link;
    Error *local_err = NULL;
    struct dirent *de;
    DIR *dp;

    head = NULL;
    link = &head;

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
        GuestMemoryBlockList *entry;

        if ((strncmp(de->d_name, "memory", 6) != 0) ||
            !(de->d_type & DT_DIR)) {
            continue;
        }

        mem_blk = g_malloc0(sizeof *mem_blk);
        /* The d_name is "memoryXXX",  phys_index is block id, same as XXX */
        mem_blk->phys_index = strtoul(&de->d_name[6], NULL, 10);
        mem_blk->has_can_offline = true; /* lolspeak ftw */
        transfer_memory_block(mem_blk, true, NULL, &local_err);

        entry = g_malloc0(sizeof *entry);
        entry->value = mem_blk;

        *link = entry;
        link = &entry->next;
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
    GuestMemoryBlockResponseList *head, **link;
    Error *local_err = NULL;

    head = NULL;
    link = &head;

    while (mem_blks != NULL) {
        GuestMemoryBlockResponse *result;
        GuestMemoryBlockResponseList *entry;
        GuestMemoryBlock *current_mem_blk = mem_blks->value;

        result = g_malloc0(sizeof(*result));
        result->phys_index = current_mem_blk->phys_index;
        transfer_memory_block(current_mem_blk, false, result, &local_err);
        if (local_err) { /* should never happen */
            goto err;
        }
        entry = g_malloc0(sizeof *entry);
        entry->value = result;

        *link = entry;
        link = &entry->next;
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

GuestNetworkInterfaceList *qmp_guest_network_get_interfaces(Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
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

void qmp_guest_set_user_password(const char *username,
                                 const char *password,
                                 bool crypted,
                                 Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
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
#endif /* CONFIG_FSFREEZE */

#if !defined(CONFIG_FSTRIM)
GuestFilesystemTrimResponse *
qmp_guest_fstrim(bool has_minimum, int64_t minimum, Error **errp)
{
    error_setg(errp, QERR_UNSUPPORTED);
    return NULL;
}
#endif

/* add unsupported commands to the blacklist */
GList *ga_command_blacklist_init(GList *blacklist)
{
#if !defined(__linux__)
    {
        const char *list[] = {
            "guest-suspend-disk", "guest-suspend-ram",
            "guest-suspend-hybrid", "guest-network-get-interfaces",
            "guest-get-vcpus", "guest-set-vcpus",
            "guest-get-memory-blocks", "guest-set-memory-blocks",
            "guest-get-memory-block-size", "guest-get-memory-block-info",
            NULL};
        char **p = (char **)list;

        while (*p) {
            blacklist = g_list_append(blacklist, g_strdup(*p++));
        }
    }
#endif

#if !defined(CONFIG_FSFREEZE)
    {
        const char *list[] = {
            "guest-get-fsinfo", "guest-fsfreeze-status",
            "guest-fsfreeze-freeze", "guest-fsfreeze-freeze-list",
            "guest-fsfreeze-thaw", "guest-get-fsinfo", NULL};
        char **p = (char **)list;

        while (*p) {
            blacklist = g_list_append(blacklist, g_strdup(*p++));
        }
    }
#endif

#if !defined(CONFIG_FSTRIM)
    blacklist = g_list_append(blacklist, g_strdup("guest-fstrim"));
#endif

    return blacklist;
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
    GuestUserList *head = NULL, *cur_item = NULL;
    struct utmpx *user_info = NULL;
    gpointer value = NULL;
    GuestUser *user = NULL;
    GuestUserList *item = NULL;
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

        item = g_new0(GuestUserList, 1);
        item->value = g_new0(GuestUser, 1);
        item->value->user = g_strdup(user_info->ut_user);
        item->value->login_time = ga_get_login_time(user_info);

        g_hash_table_insert(cache, item->value->user, item->value);

        if (!cur_item) {
            head = cur_item = item;
        } else {
            cur_item->next = item;
            cur_item = item;
        }
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
