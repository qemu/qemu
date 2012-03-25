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
#include "qga/guest-agent-core.h"
#include "qga-qmp-commands.h"
#include "qerror.h"
#include "qemu-queue.h"
#include "host-utils.h"

#if defined(__linux__)
#include <mntent.h>
#include <linux/fs.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <net/if.h>
#include <sys/wait.h>

#if defined(__linux__) && defined(FIFREEZE)
#define CONFIG_FSFREEZE
#endif
#endif

#if defined(__linux__)
/* TODO: use this in place of all post-fork() fclose(std*) callers */
static void reopen_fd_to_null(int fd)
{
    int nullfd;

    nullfd = open("/dev/null", O_RDWR);
    if (nullfd < 0) {
        return;
    }

    dup2(nullfd, fd);

    if (nullfd != fd) {
        close(nullfd);
    }
}
#endif /* defined(__linux__) */

void qmp_guest_shutdown(bool has_mode, const char *mode, Error **err)
{
    int ret;
    const char *shutdown_flag;

    slog("guest-shutdown called, mode: %s", mode);
    if (!has_mode || strcmp(mode, "powerdown") == 0) {
        shutdown_flag = "-P";
    } else if (strcmp(mode, "halt") == 0) {
        shutdown_flag = "-H";
    } else if (strcmp(mode, "reboot") == 0) {
        shutdown_flag = "-r";
    } else {
        error_set(err, QERR_INVALID_PARAMETER_VALUE, "mode",
                  "halt|powerdown|reboot");
        return;
    }

    ret = fork();
    if (ret == 0) {
        /* child, start the shutdown */
        setsid();
        fclose(stdin);
        fclose(stdout);
        fclose(stderr);

        ret = execl("/sbin/shutdown", "shutdown", shutdown_flag, "+0",
                    "hypervisor initiated shutdown", (char*)NULL);
        if (ret) {
            slog("guest-shutdown failed: %s", strerror(errno));
        }
        exit(!!ret);
    } else if (ret < 0) {
        error_set(err, QERR_UNDEFINED_ERROR);
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

static void guest_file_handle_add(FILE *fh)
{
    GuestFileHandle *gfh;

    gfh = g_malloc0(sizeof(GuestFileHandle));
    gfh->id = fileno(fh);
    gfh->fh = fh;
    QTAILQ_INSERT_TAIL(&guest_file_state.filehandles, gfh, next);
}

static GuestFileHandle *guest_file_handle_find(int64_t id)
{
    GuestFileHandle *gfh;

    QTAILQ_FOREACH(gfh, &guest_file_state.filehandles, next)
    {
        if (gfh->id == id) {
            return gfh;
        }
    }

    return NULL;
}

int64_t qmp_guest_file_open(const char *path, bool has_mode, const char *mode, Error **err)
{
    FILE *fh;
    int fd;
    int64_t ret = -1;

    if (!has_mode) {
        mode = "r";
    }
    slog("guest-file-open called, filepath: %s, mode: %s", path, mode);
    fh = fopen(path, mode);
    if (!fh) {
        error_set(err, QERR_OPEN_FILE_FAILED, path);
        return -1;
    }

    /* set fd non-blocking to avoid common use cases (like reading from a
     * named pipe) from hanging the agent
     */
    fd = fileno(fh);
    ret = fcntl(fd, F_GETFL);
    ret = fcntl(fd, F_SETFL, ret | O_NONBLOCK);
    if (ret == -1) {
        error_set(err, QERR_QGA_COMMAND_FAILED, "fcntl() failed");
        fclose(fh);
        return -1;
    }

    guest_file_handle_add(fh);
    slog("guest-file-open, handle: %d", fd);
    return fd;
}

void qmp_guest_file_close(int64_t handle, Error **err)
{
    GuestFileHandle *gfh = guest_file_handle_find(handle);
    int ret;

    slog("guest-file-close called, handle: %ld", handle);
    if (!gfh) {
        error_set(err, QERR_FD_NOT_FOUND, "handle");
        return;
    }

    ret = fclose(gfh->fh);
    if (ret == -1) {
        error_set(err, QERR_QGA_COMMAND_FAILED, "fclose() failed");
        return;
    }

    QTAILQ_REMOVE(&guest_file_state.filehandles, gfh, next);
    g_free(gfh);
}

struct GuestFileRead *qmp_guest_file_read(int64_t handle, bool has_count,
                                          int64_t count, Error **err)
{
    GuestFileHandle *gfh = guest_file_handle_find(handle);
    GuestFileRead *read_data = NULL;
    guchar *buf;
    FILE *fh;
    size_t read_count;

    if (!gfh) {
        error_set(err, QERR_FD_NOT_FOUND, "handle");
        return NULL;
    }

    if (!has_count) {
        count = QGA_READ_COUNT_DEFAULT;
    } else if (count < 0) {
        error_set(err, QERR_INVALID_PARAMETER, "count");
        return NULL;
    }

    fh = gfh->fh;
    buf = g_malloc0(count+1);
    read_count = fread(buf, 1, count, fh);
    if (ferror(fh)) {
        slog("guest-file-read failed, handle: %ld", handle);
        error_set(err, QERR_QGA_COMMAND_FAILED, "fread() failed");
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
    GuestFileHandle *gfh = guest_file_handle_find(handle);
    FILE *fh;

    if (!gfh) {
        error_set(err, QERR_FD_NOT_FOUND, "handle");
        return NULL;
    }

    fh = gfh->fh;
    buf = g_base64_decode(buf_b64, &buf_len);

    if (!has_count) {
        count = buf_len;
    } else if (count < 0 || count > buf_len) {
        g_free(buf);
        error_set(err, QERR_INVALID_PARAMETER, "count");
        return NULL;
    }

    write_count = fwrite(buf, 1, count, fh);
    if (ferror(fh)) {
        slog("guest-file-write failed, handle: %ld", handle);
        error_set(err, QERR_QGA_COMMAND_FAILED, "fwrite() error");
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
    GuestFileHandle *gfh = guest_file_handle_find(handle);
    GuestFileSeek *seek_data = NULL;
    FILE *fh;
    int ret;

    if (!gfh) {
        error_set(err, QERR_FD_NOT_FOUND, "handle");
        return NULL;
    }

    fh = gfh->fh;
    ret = fseek(fh, offset, whence);
    if (ret == -1) {
        error_set(err, QERR_QGA_COMMAND_FAILED, strerror(errno));
    } else {
        seek_data = g_malloc0(sizeof(GuestFileRead));
        seek_data->position = ftell(fh);
        seek_data->eof = feof(fh);
    }
    clearerr(fh);

    return seek_data;
}

void qmp_guest_file_flush(int64_t handle, Error **err)
{
    GuestFileHandle *gfh = guest_file_handle_find(handle);
    FILE *fh;
    int ret;

    if (!gfh) {
        error_set(err, QERR_FD_NOT_FOUND, "handle");
        return;
    }

    fh = gfh->fh;
    ret = fflush(fh);
    if (ret == EOF) {
        error_set(err, QERR_QGA_COMMAND_FAILED, strerror(errno));
    }
}

static void guest_file_init(void)
{
    QTAILQ_INIT(&guest_file_state.filehandles);
}

/* linux-specific implementations. avoid this if at all possible. */
#if defined(__linux__)

#if defined(CONFIG_FSFREEZE)

static void disable_logging(void)
{
    ga_disable_logging(ga_state);
}

static void enable_logging(void)
{
    ga_enable_logging(ga_state);
}

typedef struct GuestFsfreezeMount {
    char *dirname;
    char *devtype;
    QTAILQ_ENTRY(GuestFsfreezeMount) next;
} GuestFsfreezeMount;

struct {
    GuestFsfreezeStatus status;
    QTAILQ_HEAD(, GuestFsfreezeMount) mount_list;
} guest_fsfreeze_state;

/*
 * Walk the mount table and build a list of local file systems
 */
static int guest_fsfreeze_build_mount_list(void)
{
    struct mntent *ment;
    GuestFsfreezeMount *mount, *temp;
    char const *mtab = MOUNTED;
    FILE *fp;

    QTAILQ_FOREACH_SAFE(mount, &guest_fsfreeze_state.mount_list, next, temp) {
        QTAILQ_REMOVE(&guest_fsfreeze_state.mount_list, mount, next);
        g_free(mount->dirname);
        g_free(mount->devtype);
        g_free(mount);
    }

    fp = setmntent(mtab, "r");
    if (!fp) {
        g_warning("fsfreeze: unable to read mtab");
        return -1;
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

        mount = g_malloc0(sizeof(GuestFsfreezeMount));
        mount->dirname = g_strdup(ment->mnt_dir);
        mount->devtype = g_strdup(ment->mnt_type);

        QTAILQ_INSERT_TAIL(&guest_fsfreeze_state.mount_list, mount, next);
    }

    endmntent(fp);

    return 0;
}

/*
 * Return status of freeze/thaw
 */
GuestFsfreezeStatus qmp_guest_fsfreeze_status(Error **err)
{
    return guest_fsfreeze_state.status;
}

/*
 * Walk list of mounted file systems in the guest, and freeze the ones which
 * are real local file systems.
 */
int64_t qmp_guest_fsfreeze_freeze(Error **err)
{
    int ret = 0, i = 0;
    struct GuestFsfreezeMount *mount, *temp;
    int fd;
    char err_msg[512];

    slog("guest-fsfreeze called");

    if (guest_fsfreeze_state.status == GUEST_FSFREEZE_STATUS_FROZEN) {
        return 0;
    }

    ret = guest_fsfreeze_build_mount_list();
    if (ret < 0) {
        return ret;
    }

    /* cannot risk guest agent blocking itself on a write in this state */
    disable_logging();

    QTAILQ_FOREACH_SAFE(mount, &guest_fsfreeze_state.mount_list, next, temp) {
        fd = qemu_open(mount->dirname, O_RDONLY);
        if (fd == -1) {
            sprintf(err_msg, "failed to open %s, %s", mount->dirname, strerror(errno));
            error_set(err, QERR_QGA_COMMAND_FAILED, err_msg);
            goto error;
        }

        /* we try to cull filesytems we know won't work in advance, but other
         * filesytems may not implement fsfreeze for less obvious reasons.
         * these will report EOPNOTSUPP, so we simply ignore them. when
         * thawing, these filesystems will return an EINVAL instead, due to
         * not being in a frozen state. Other filesystem-specific
         * errors may result in EINVAL, however, so the user should check the
         * number * of filesystems returned here against those returned by the
         * thaw operation to determine whether everything completed
         * successfully
         */
        ret = ioctl(fd, FIFREEZE);
        if (ret < 0 && errno != EOPNOTSUPP) {
            sprintf(err_msg, "failed to freeze %s, %s", mount->dirname, strerror(errno));
            error_set(err, QERR_QGA_COMMAND_FAILED, err_msg);
            close(fd);
            goto error;
        }
        close(fd);

        i++;
    }

    guest_fsfreeze_state.status = GUEST_FSFREEZE_STATUS_FROZEN;
    return i;

error:
    if (i > 0) {
        qmp_guest_fsfreeze_thaw(NULL);
    }
    return 0;
}

/*
 * Walk list of frozen file systems in the guest, and thaw them.
 */
int64_t qmp_guest_fsfreeze_thaw(Error **err)
{
    int ret;
    GuestFsfreezeMount *mount, *temp;
    int fd, i = 0;
    bool has_error = false;

    QTAILQ_FOREACH_SAFE(mount, &guest_fsfreeze_state.mount_list, next, temp) {
        fd = qemu_open(mount->dirname, O_RDONLY);
        if (fd == -1) {
            has_error = true;
            continue;
        }
        ret = ioctl(fd, FITHAW);
        if (ret < 0 && errno != EOPNOTSUPP && errno != EINVAL) {
            has_error = true;
            close(fd);
            continue;
        }
        close(fd);
        i++;
    }

    if (has_error) {
        guest_fsfreeze_state.status = GUEST_FSFREEZE_STATUS_ERROR;
    } else {
        guest_fsfreeze_state.status = GUEST_FSFREEZE_STATUS_THAWED;
    }
    enable_logging();
    return i;
}

static void guest_fsfreeze_init(void)
{
    guest_fsfreeze_state.status = GUEST_FSFREEZE_STATUS_THAWED;
    QTAILQ_INIT(&guest_fsfreeze_state.mount_list);
}

static void guest_fsfreeze_cleanup(void)
{
    int64_t ret;
    Error *err = NULL;

    if (guest_fsfreeze_state.status == GUEST_FSFREEZE_STATUS_FROZEN) {
        ret = qmp_guest_fsfreeze_thaw(&err);
        if (ret < 0 || err) {
            slog("failed to clean up frozen filesystems");
        }
    }
}
#endif /* CONFIG_FSFREEZE */

#define LINUX_SYS_STATE_FILE "/sys/power/state"
#define SUSPEND_SUPPORTED 0
#define SUSPEND_NOT_SUPPORTED 1

/**
 * This function forks twice and the information about the mode support
 * status is passed to the qemu-ga process via a pipe.
 *
 * This approach allows us to keep the way we reap terminated children
 * in qemu-ga quite simple.
 */
static void bios_supports_mode(const char *pmutils_bin, const char *pmutils_arg,
                               const char *sysfile_str, Error **err)
{
    pid_t pid;
    ssize_t ret;
    char *pmutils_path;
    int status, pipefds[2];

    if (pipe(pipefds) < 0) {
        error_set(err, QERR_UNDEFINED_ERROR);
        return;
    }

    pmutils_path = g_find_program_in_path(pmutils_bin);

    pid = fork();
    if (!pid) {
        struct sigaction act;

        memset(&act, 0, sizeof(act));
        act.sa_handler = SIG_DFL;
        sigaction(SIGCHLD, &act, NULL);

        setsid();
        close(pipefds[0]);
        reopen_fd_to_null(0);
        reopen_fd_to_null(1);
        reopen_fd_to_null(2);

        pid = fork();
        if (!pid) {
            int fd;
            char buf[32]; /* hopefully big enough */

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
        }

        if (pid > 0) {
            wait(&status);
        } else {
            status = SUSPEND_NOT_SUPPORTED;
        }

        ret = write(pipefds[1], &status, sizeof(status));
        if (ret != sizeof(status)) {
            _exit(EXIT_FAILURE);
        }

        _exit(EXIT_SUCCESS);
    }

    close(pipefds[1]);
    g_free(pmutils_path);

    if (pid < 0) {
        error_set(err, QERR_UNDEFINED_ERROR);
        goto out;
    }

    ret = read(pipefds[0], &status, sizeof(status));
    if (ret == sizeof(status) && WIFEXITED(status) &&
        WEXITSTATUS(status) == SUSPEND_SUPPORTED) {
            goto out;
    }

    error_set(err, QERR_UNSUPPORTED);

out:
    close(pipefds[0]);
}

static void guest_suspend(const char *pmutils_bin, const char *sysfile_str,
                          Error **err)
{
    pid_t pid;
    char *pmutils_path;

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
    }

    g_free(pmutils_path);

    if (pid < 0) {
        error_set(err, QERR_UNDEFINED_ERROR);
        return;
    }
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
    char err_msg[512];

    if (getifaddrs(&ifap) < 0) {
        snprintf(err_msg, sizeof(err_msg),
                 "getifaddrs failed: %s", strerror(errno));
        error_set(errp, QERR_QGA_COMMAND_FAILED, err_msg);
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
                snprintf(err_msg, sizeof(err_msg),
                         "failed to create socket: %s", strerror(errno));
                error_set(errp, QERR_QGA_COMMAND_FAILED, err_msg);
                goto error;
            }

            memset(&ifr, 0, sizeof(ifr));
            strncpy(ifr.ifr_name,  info->value->name, IF_NAMESIZE);
            if (ioctl(sock, SIOCGIFHWADDR, &ifr) == -1) {
                snprintf(err_msg, sizeof(err_msg),
                         "failed to get MAC addres of %s: %s",
                         ifa->ifa_name,
                         strerror(errno));
                error_set(errp, QERR_QGA_COMMAND_FAILED, err_msg);
                goto error;
            }

            mac_addr = (unsigned char *) &ifr.ifr_hwaddr.sa_data;

            if (asprintf(&info->value->hardware_address,
                         "%02x:%02x:%02x:%02x:%02x:%02x",
                         (int) mac_addr[0], (int) mac_addr[1],
                         (int) mac_addr[2], (int) mac_addr[3],
                         (int) mac_addr[4], (int) mac_addr[5]) == -1) {
                snprintf(err_msg, sizeof(err_msg),
                         "failed to format MAC: %s", strerror(errno));
                error_set(errp, QERR_QGA_COMMAND_FAILED, err_msg);
                goto error;
            }

            info->value->has_hardware_address = true;
            close(sock);
        }

        if (ifa->ifa_addr &&
            ifa->ifa_addr->sa_family == AF_INET) {
            /* interface with IPv4 address */
            address_item = g_malloc0(sizeof(*address_item));
            address_item->value = g_malloc0(sizeof(*address_item->value));
            p = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
            if (!inet_ntop(AF_INET, p, addr4, sizeof(addr4))) {
                snprintf(err_msg, sizeof(err_msg),
                         "inet_ntop failed : %s", strerror(errno));
                error_set(errp, QERR_QGA_COMMAND_FAILED, err_msg);
                goto error;
            }

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
            address_item = g_malloc0(sizeof(*address_item));
            address_item->value = g_malloc0(sizeof(*address_item->value));
            p = &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
            if (!inet_ntop(AF_INET6, p, addr6, sizeof(addr6))) {
                snprintf(err_msg, sizeof(err_msg),
                         "inet_ntop failed : %s", strerror(errno));
                error_set(errp, QERR_QGA_COMMAND_FAILED, err_msg);
                goto error;
            }

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

#else /* defined(__linux__) */

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

#endif

/* register init/cleanup routines for stateful command groups */
void ga_command_state_init(GAState *s, GACommandState *cs)
{
#if defined(CONFIG_FSFREEZE)
    ga_command_state_add(cs, guest_fsfreeze_init, guest_fsfreeze_cleanup);
#endif
    ga_command_state_add(cs, guest_file_init, NULL);
}
