/*
 * Helper for QEMU Proxy FS Driver
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 * M. Mohan Kumar <mohan@in.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See
 * the COPYING file in the top-level directory.
 */
#include <stdio.h>
#include <sys/socket.h>
#include <string.h>
#include <sys/un.h>
#include <limits.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <getopt.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/capability.h>
#include <sys/fsuid.h>
#include <stdarg.h>
#include <stdbool.h>
#include "qemu-common.h"
#include "virtio-9p-marshal.h"
#include "hw/9pfs/virtio-9p-proxy.h"
#include "fsdev/virtio-9p-marshal.h"

#define PROGNAME "virtfs-proxy-helper"

static struct option helper_opts[] = {
    {"fd", required_argument, NULL, 'f'},
    {"path", required_argument, NULL, 'p'},
    {"nodaemon", no_argument, NULL, 'n'},
};

static bool is_daemon;

static void do_log(int loglevel, const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    if (is_daemon) {
        vsyslog(LOG_CRIT, format, ap);
    } else {
        vfprintf(stderr, format, ap);
    }
    va_end(ap);
}

static void do_perror(const char *string)
{
    if (is_daemon) {
        syslog(LOG_CRIT, "%s:%s", string, strerror(errno));
    } else {
        fprintf(stderr, "%s:%s\n", string, strerror(errno));
    }
}

static int do_cap_set(cap_value_t *cap_value, int size, int reset)
{
    cap_t caps;
    if (reset) {
        /*
         * Start with an empty set and set permitted and effective
         */
        caps = cap_init();
        if (caps == NULL) {
            do_perror("cap_init");
            return -1;
        }
        if (cap_set_flag(caps, CAP_PERMITTED, size, cap_value, CAP_SET) < 0) {
            do_perror("cap_set_flag");
            goto error;
        }
    } else {
        caps = cap_get_proc();
        if (!caps) {
            do_perror("cap_get_proc");
            return -1;
        }
    }
    if (cap_set_flag(caps, CAP_EFFECTIVE, size, cap_value, CAP_SET) < 0) {
        do_perror("cap_set_flag");
        goto error;
    }
    if (cap_set_proc(caps) < 0) {
        do_perror("cap_set_proc");
        goto error;
    }
    cap_free(caps);
    return 0;

error:
    cap_free(caps);
    return -1;
}

static int init_capabilities(void)
{
    /* helper needs following capbabilities only */
    cap_value_t cap_list[] = {
        CAP_CHOWN,
        CAP_DAC_OVERRIDE,
        CAP_FOWNER,
        CAP_FSETID,
        CAP_SETGID,
        CAP_MKNOD,
        CAP_SETUID,
    };
    return do_cap_set(cap_list, ARRAY_SIZE(cap_list), 1);
}

static int socket_read(int sockfd, void *buff, ssize_t size)
{
    ssize_t retval, total = 0;

    while (size) {
        retval = read(sockfd, buff, size);
        if (retval == 0) {
            return -EIO;
        }
        if (retval < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -errno;
        }
        size -= retval;
        buff += retval;
        total += retval;
    }
    return total;
}

static int socket_write(int sockfd, void *buff, ssize_t size)
{
    ssize_t retval, total = 0;

    while (size) {
        retval = write(sockfd, buff, size);
        if (retval < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -errno;
        }
        size -= retval;
        buff += retval;
        total += retval;
    }
    return total;
}

static int read_request(int sockfd, struct iovec *iovec, ProxyHeader *header)
{
    int retval;

    /*
     * read the request header.
     */
    iovec->iov_len = 0;
    retval = socket_read(sockfd, iovec->iov_base, PROXY_HDR_SZ);
    if (retval < 0) {
        return retval;
    }
    iovec->iov_len = PROXY_HDR_SZ;
    retval = proxy_unmarshal(iovec, 0, "dd", &header->type, &header->size);
    if (retval < 0) {
        return retval;
    }
    /*
     * We can't process message.size > PROXY_MAX_IO_SZ.
     * Treat it as fatal error
     */
    if (header->size > PROXY_MAX_IO_SZ) {
        return -ENOBUFS;
    }
    retval = socket_read(sockfd, iovec->iov_base + PROXY_HDR_SZ, header->size);
    if (retval < 0) {
        return retval;
    }
    iovec->iov_len += header->size;
    return 0;
}

static int send_fd(int sockfd, int fd)
{
    struct msghdr msg;
    struct iovec iov;
    int retval, data;
    struct cmsghdr *cmsg;
    union MsgControl msg_control;

    iov.iov_base = &data;
    iov.iov_len = sizeof(data);

    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    /* No ancillary data on error */
    if (fd < 0) {
        /* fd is really negative errno if the request failed  */
        data = fd;
    } else {
        data = V9FS_FD_VALID;
        msg.msg_control = &msg_control;
        msg.msg_controllen = sizeof(msg_control);

        cmsg = &msg_control.cmsg;
        cmsg->cmsg_len = CMSG_LEN(sizeof(fd));
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        memcpy(CMSG_DATA(cmsg), &fd, sizeof(fd));
    }

    do {
        retval = sendmsg(sockfd, &msg, 0);
    } while (retval < 0 && errno == EINTR);
    if (fd >= 0) {
        close(fd);
    }
    if (retval < 0) {
        return retval;
    }
    return 0;
}

static int send_status(int sockfd, struct iovec *iovec, int status)
{
    ProxyHeader header;
    int retval, msg_size;;

    if (status < 0) {
        header.type = T_ERROR;
    } else {
        header.type = T_SUCCESS;
    }
    header.size = sizeof(status);
    /*
     * marshal the return status. We don't check error.
     * because we are sure we have enough space for the status
     */
    msg_size = proxy_marshal(iovec, 0, "ddd", header.type,
                             header.size, status);
    retval = socket_write(sockfd, iovec->iov_base, msg_size);
    if (retval < 0) {
        return retval;
    }
    return 0;
}

/*
 * from man 7 capabilities, section
 * Effect of User ID Changes on Capabilities:
 * 4. If the file system user ID is changed from 0 to nonzero (see setfsuid(2))
 * then the following capabilities are cleared from the effective set:
 * CAP_CHOWN, CAP_DAC_OVERRIDE, CAP_DAC_READ_SEARCH,  CAP_FOWNER, CAP_FSETID,
 * CAP_LINUX_IMMUTABLE  (since  Linux 2.2.30), CAP_MAC_OVERRIDE, and CAP_MKNOD
 * (since Linux 2.2.30). If the file system UID is changed from nonzero to 0,
 * then any of these capabilities that are enabled in the permitted set
 * are enabled in the effective set.
 */
static int setfsugid(int uid, int gid)
{
    /*
     * We still need DAC_OVERRIDE because  we don't change
     * supplementary group ids, and hence may be subjected DAC rules
     */
    cap_value_t cap_list[] = {
        CAP_DAC_OVERRIDE,
    };

    setfsgid(gid);
    setfsuid(uid);

    if (uid != 0 || gid != 0) {
        return do_cap_set(cap_list, ARRAY_SIZE(cap_list), 0);
    }
    return 0;
}

/*
 * create other filesystem objects and send 0 on success
 * return -errno on error
 */
static int do_create_others(int type, struct iovec *iovec)
{
    dev_t rdev;
    int retval = 0;
    int offset = PROXY_HDR_SZ;
    V9fsString oldpath, path;
    int mode, uid, gid, cur_uid, cur_gid;

    v9fs_string_init(&path);
    v9fs_string_init(&oldpath);
    cur_uid = geteuid();
    cur_gid = getegid();

    retval = proxy_unmarshal(iovec, offset, "dd", &uid, &gid);
    if (retval < 0) {
        return retval;
    }
    offset += retval;
    retval = setfsugid(uid, gid);
    if (retval < 0) {
        retval = -errno;
        goto err_out;
    }
    switch (type) {
    case T_MKNOD:
        retval = proxy_unmarshal(iovec, offset, "sdq", &path, &mode, &rdev);
        if (retval < 0) {
            goto err_out;
        }
        retval = mknod(path.data, mode, rdev);
        break;
    case T_MKDIR:
        retval = proxy_unmarshal(iovec, offset, "sd", &path, &mode);
        if (retval < 0) {
            goto err_out;
        }
        retval = mkdir(path.data, mode);
        break;
    case T_SYMLINK:
        retval = proxy_unmarshal(iovec, offset, "ss", &oldpath, &path);
        if (retval < 0) {
            goto err_out;
        }
        retval = symlink(oldpath.data, path.data);
        break;
    }
    if (retval < 0) {
        retval = -errno;
    }

err_out:
    v9fs_string_free(&path);
    v9fs_string_free(&oldpath);
    setfsugid(cur_uid, cur_gid);
    return retval;
}

/*
 * create a file and send fd on success
 * return -errno on error
 */
static int do_create(struct iovec *iovec)
{
    int ret;
    V9fsString path;
    int flags, mode, uid, gid, cur_uid, cur_gid;

    v9fs_string_init(&path);
    ret = proxy_unmarshal(iovec, PROXY_HDR_SZ, "sdddd",
                          &path, &flags, &mode, &uid, &gid);
    if (ret < 0) {
        goto unmarshal_err_out;
    }
    cur_uid = geteuid();
    cur_gid = getegid();
    ret = setfsugid(uid, gid);
    if (ret < 0) {
        /*
         * On failure reset back to the
         * old uid/gid
         */
        ret = -errno;
        goto err_out;
    }
    ret = open(path.data, flags, mode);
    if (ret < 0) {
        ret = -errno;
    }

err_out:
    setfsugid(cur_uid, cur_gid);
unmarshal_err_out:
    v9fs_string_free(&path);
    return ret;
}

/*
 * open a file and send fd on success
 * return -errno on error
 */
static int do_open(struct iovec *iovec)
{
    int flags, ret;
    V9fsString path;

    v9fs_string_init(&path);
    ret = proxy_unmarshal(iovec, PROXY_HDR_SZ, "sd", &path, &flags);
    if (ret < 0) {
        goto err_out;
    }
    ret = open(path.data, flags);
    if (ret < 0) {
        ret = -errno;
    }
err_out:
    v9fs_string_free(&path);
    return ret;
}

static void usage(char *prog)
{
    fprintf(stderr, "usage: %s\n"
            " -p|--path <path> 9p path to export\n"
            " {-f|--fd <socket-descriptor>} socket file descriptor to be used\n"
            " [-n|--nodaemon] Run as a normal program\n",
            basename(prog));
}

static int process_reply(int sock, int type,
                         struct iovec *out_iovec, int retval)
{
    switch (type) {
    case T_OPEN:
    case T_CREATE:
        if (send_fd(sock, retval) < 0) {
            return -1;
        }
        break;
    case T_MKNOD:
    case T_MKDIR:
    case T_SYMLINK:
    case T_LINK:
        if (send_status(sock, out_iovec, retval) < 0) {
            return -1;
        }
        break;
    default:
        return -1;
        break;
    }
    return 0;
}

static int process_requests(int sock)
{
    int retval = 0;
    ProxyHeader header;
    V9fsString oldpath, path;
    struct iovec in_iovec, out_iovec;

    in_iovec.iov_base  = g_malloc(PROXY_MAX_IO_SZ + PROXY_HDR_SZ);
    in_iovec.iov_len   = PROXY_MAX_IO_SZ + PROXY_HDR_SZ;
    out_iovec.iov_base = g_malloc(PROXY_MAX_IO_SZ + PROXY_HDR_SZ);
    out_iovec.iov_len  = PROXY_MAX_IO_SZ + PROXY_HDR_SZ;

    while (1) {
        /*
         * initialize the header type, so that we send
         * response to proper request type.
         */
        header.type = 0;
        retval = read_request(sock, &in_iovec, &header);
        if (retval < 0) {
            goto err_out;
        }

        switch (header.type) {
        case T_OPEN:
            retval = do_open(&in_iovec);
            break;
        case T_CREATE:
            retval = do_create(&in_iovec);
            break;
        case T_MKNOD:
        case T_MKDIR:
        case T_SYMLINK:
            retval = do_create_others(header.type, &in_iovec);
            break;
        case T_LINK:
            v9fs_string_init(&path);
            v9fs_string_init(&oldpath);
            retval = proxy_unmarshal(&in_iovec, PROXY_HDR_SZ,
                                     "ss", &oldpath, &path);
            if (retval > 0) {
                retval = link(oldpath.data, path.data);
                if (retval < 0) {
                    retval = -errno;
                }
            }
            v9fs_string_free(&oldpath);
            v9fs_string_free(&path);
            break;
        default:
            goto err_out;
            break;
        }

        if (process_reply(sock, header.type, &out_iovec, retval) < 0) {
            goto err_out;
        }
    }
err_out:
    g_free(in_iovec.iov_base);
    g_free(out_iovec.iov_base);
    return -1;
}

int main(int argc, char **argv)
{
    int sock;
    char *rpath = NULL;
    struct stat stbuf;
    int c, option_index;

    is_daemon = true;
    sock = -1;
    while (1) {
        option_index = 0;
        c = getopt_long(argc, argv, "p:nh?f:", helper_opts,
                        &option_index);
        if (c == -1) {
            break;
        }
        switch (c) {
        case 'p':
            rpath = strdup(optarg);
            break;
        case 'n':
            is_daemon = false;
            break;
        case 'f':
            sock = atoi(optarg);
            break;
        case '?':
        case 'h':
        default:
            usage(argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    /* Parameter validation */
    if (sock == -1 || rpath == NULL) {
        fprintf(stderr, "socket descriptor or path not specified\n");
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    if (lstat(rpath, &stbuf) < 0) {
        fprintf(stderr, "invalid path \"%s\" specified, %s\n",
                rpath, strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (!S_ISDIR(stbuf.st_mode)) {
        fprintf(stderr, "specified path \"%s\" is not directory\n", rpath);
        exit(EXIT_FAILURE);
    }

    if (is_daemon) {
        if (daemon(0, 0) < 0) {
            fprintf(stderr, "daemon call failed\n");
            exit(EXIT_FAILURE);
        }
        openlog(PROGNAME, LOG_PID, LOG_DAEMON);
    }

    do_log(LOG_INFO, "Started\n");

    if (chdir("/") < 0) {
        do_perror("chdir");
        goto error;
    }
    if (chroot(rpath) < 0) {
        do_perror("chroot");
        goto error;
    }
    umask(0);

    if (init_capabilities() < 0) {
        goto error;
    }

    process_requests(sock);
error:
    do_log(LOG_INFO, "Done\n");
    closelog();
    return 0;
}
