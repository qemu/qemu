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
#include <dirent.h>
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
     * hardware clock (RTC). */
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
    if (handle < 0) {
        return -1;
    }

    gfh = g_malloc0(sizeof(GuestFileHandle));
    gfh->id = handle;
    gfh->fh = fh;
    QTAILQ_INSERT_TAIL(&guest_file_state.filehandles, gfh, next);

    return handle;
}

static GuestFileHandle *guest_file_handle_find(int64_t id, Error **errp)
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
    int fd;
    int64_t ret = -1, handle;

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
    fd = fileno(fh);
    ret = fcntl(fd, F_GETFL);
    ret = fcntl(fd, F_SETFL, ret | O_NONBLOCK);
    if (ret == -1) {
        error_setg_errno(errp, errno, "failed to make file '%s' non-blocking",
                         path);
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

struct GuestFileRead *qmp_guest_file_read(int64_t handle, bool has_count,
                                          int64_t count, Error **errp)
{
    GuestFileHandle *gfh = guest_file_handle_find(handle, errp);
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
        error_setg(errp, "value '%" PRId64 "' is invalid for argument count",
                   count);
        return NULL;
    }

    fh = gfh->fh;
    buf = g_malloc0(count+1);
    read_count = fread(buf, 1, count, fh);
    if (ferror(fh)) {
        error_setg_errno(errp, errno, "failed to read file");
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
    buf = g_base64_decode(buf_b64, &buf_len);

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
        write_data = g_malloc0(sizeof(GuestFileWrite));
        write_data->count = write_count;
        write_data->eof = feof(fh);
    }
    g_free(buf);
    clearerr(fh);

    return write_data;
}

struct GuestFileSeek *qmp_guest_file_seek(int64_t handle, int64_t offset,
                                          int64_t whence, Error **errp)
{
    GuestFileHandle *gfh = guest_file_handle_find(handle, errp);
    GuestFileSeek *seek_data = NULL;
    FILE *fh;
    int ret;

    if (!gfh) {
        return NULL;
    }

    fh = gfh->fh;
    ret = fseek(fh, offset, whence);
    if (ret == -1) {
        error_setg_errno(errp, errno, "failed to seek file");
    } else {
        seek_data = g_new0(GuestFileSeek, 1);
        seek_data->position = ftell(fh);
        seek_data->eof = feof(fh);
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

        mount = g_malloc0(sizeof(FsMount));
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

        mount = g_malloc0(sizeof(FsMount));
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
        driver = g_strdup(basename(buf));
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

    p = strstr(syspath, "/devices/pci");
    if (!p || sscanf(p + 12, "%*x:%*x/%x:%x:%x.%x%n",
                     pci, pci + 1, pci + 2, pci + 3, &pcilen) < 4) {
        g_debug("only pci device is supported: sysfs path \"%s\"", syspath);
        return;
    }

    driver = get_pci_driver(syspath, (p + 12 + pcilen) - syspath, errp);
    if (!driver) {
        goto cleanup;
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
                             sizeof(hosts) / sizeof(hosts[0]), errp);
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
    g_free(driver);
    return;

cleanup:
    if (list) {
        qapi_free_GuestDiskAddressList(list);
    }
    g_free(driver);
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
    DIR *dir;
    char *dirpath;
    struct dirent *entry;

    dirpath = g_strdup_printf("%s/slaves", syspath);
    dir = opendir(dirpath);
    if (!dir) {
        error_setg_errno(errp, errno, "opendir(\"%s\")", dirpath);
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
            build_guest_fsinfo_for_device(path, fs, errp);
            g_free(path);

            if (*errp) {
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
        fs->name = g_strdup(basename(syspath));
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
    char *devpath = g_strdup_printf("/sys/dev/block/%u:%u",
                                    mount->devmajor, mount->devminor);

    fs->mountpoint = g_strdup(mount->dirname);
    fs->type = g_strdup(mount->devtype);
    build_guest_fsinfo_for_device(devpath, fs, errp);

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

    QTAILQ_FOREACH_REVERSE(mount, &mounts, FsMountList, next) {
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
void qmp_guest_fstrim(bool has_minimum, int64_t minimum, Error **errp)
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
        error_propagate(errp, local_err);
        return;
    }

    QTAILQ_FOREACH(mount, &mounts, next) {
        fd = qemu_open(mount->dirname, O_RDONLY);
        if (fd == -1) {
            error_setg_errno(errp, errno, "failed to open %s", mount->dirname);
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
                error_setg_errno(errp, errno, "failed to trim %s",
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
                               const char *sysfile_str, Error **errp)
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
        error_setg_errno(errp, errno, "failed to create child process");
        goto out;
    }

    ga_wait_child(pid, &status, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto out;
    }

    if (!WIFEXITED(status)) {
        error_setg(errp, "child process has terminated abnormally");
        goto out;
    }

    switch (WEXITSTATUS(status)) {
    case SUSPEND_SUPPORTED:
        goto out;
    case SUSPEND_NOT_SUPPORTED:
        error_setg(errp,
                   "the requested suspend mode is not supported by the guest");
        goto out;
    default:
        error_setg(errp,
                   "the helper program '%s' returned an unexpected exit status"
                   " code (%d)", pmutils_path, WEXITSTATUS(status));
        goto out;
    }

out:
    g_free(pmutils_path);
}

static void guest_suspend(const char *pmutils_bin, const char *sysfile_str,
                          Error **errp)
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
        error_setg_errno(errp, errno, "failed to create child process");
        goto out;
    }

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
        error_setg(errp, "child process has failed to suspend");
        goto out;
    }

out:
    g_free(pmutils_path);
}

void qmp_guest_suspend_disk(Error **errp)
{
    Error *local_err = NULL;

    bios_supports_mode("pm-is-supported", "--hibernate", "disk", &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    guest_suspend("pm-hibernate", "disk", errp);
}

void qmp_guest_suspend_ram(Error **errp)
{
    Error *local_err = NULL;

    bios_supports_mode("pm-is-supported", "--suspend", "mem", &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    guest_suspend("pm-suspend", "mem", errp);
}

void qmp_guest_suspend_hybrid(Error **errp)
{
    Error *local_err = NULL;

    bios_supports_mode("pm-is-supported", "--suspend-hybrid", NULL,
                       &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    guest_suspend("pm-suspend-hybrid", NULL, errp);
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

void qmp_guest_suspend_disk(Error **errp)
{
    error_set(errp, QERR_UNSUPPORTED);
}

void qmp_guest_suspend_ram(Error **errp)
{
    error_set(errp, QERR_UNSUPPORTED);
}

void qmp_guest_suspend_hybrid(Error **errp)
{
    error_set(errp, QERR_UNSUPPORTED);
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

GuestFilesystemInfoList *qmp_guest_get_fsinfo(Error **errp)
{
    error_set(errp, QERR_UNSUPPORTED);
    return NULL;
}

GuestFsfreezeStatus qmp_guest_fsfreeze_status(Error **errp)
{
    error_set(errp, QERR_UNSUPPORTED);

    return 0;
}

int64_t qmp_guest_fsfreeze_freeze(Error **errp)
{
    error_set(errp, QERR_UNSUPPORTED);

    return 0;
}

int64_t qmp_guest_fsfreeze_freeze_list(bool has_mountpoints,
                                       strList *mountpoints,
                                       Error **errp)
{
    error_set(errp, QERR_UNSUPPORTED);

    return 0;
}

int64_t qmp_guest_fsfreeze_thaw(Error **errp)
{
    error_set(errp, QERR_UNSUPPORTED);

    return 0;
}
#endif /* CONFIG_FSFREEZE */

#if !defined(CONFIG_FSTRIM)
void qmp_guest_fstrim(bool has_minimum, int64_t minimum, Error **errp)
{
    error_set(errp, QERR_UNSUPPORTED);
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
            "guest-get-vcpus", "guest-set-vcpus", NULL};
        char **p = (char **)list;

        while (*p) {
            blacklist = g_list_append(blacklist, *p++);
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
            blacklist = g_list_append(blacklist, *p++);
        }
    }
#endif

#if !defined(CONFIG_FSTRIM)
    blacklist = g_list_append(blacklist, (char *)"guest-fstrim");
#endif

    return blacklist;
}

/* register init/cleanup routines for stateful command groups */
void ga_command_state_init(GAState *s, GACommandState *cs)
{
#if defined(CONFIG_FSFREEZE)
    ga_command_state_add(cs, NULL, guest_fsfreeze_cleanup);
#endif
    ga_command_state_add(cs, guest_file_init, NULL);
}
