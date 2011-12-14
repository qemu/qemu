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

static void usage(char *prog)
{
    fprintf(stderr, "usage: %s\n"
            " -p|--path <path> 9p path to export\n"
            " {-f|--fd <socket-descriptor>} socket file descriptor to be used\n"
            " [-n|--nodaemon] Run as a normal program\n",
            basename(prog));
}

static int process_requests(int sock)
{
    int retval;
    ProxyHeader header;
    struct iovec in_iovec;

    in_iovec.iov_base = g_malloc(PROXY_MAX_IO_SZ + PROXY_HDR_SZ);
    in_iovec.iov_len  = PROXY_MAX_IO_SZ + PROXY_HDR_SZ;
    while (1) {
        retval = read_request(sock, &in_iovec, &header);
        if (retval < 0) {
            goto error;
        }
    }
    (void)socket_write;
error:
    g_free(in_iovec.iov_base);
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
