/*
 * SCM_RIGHTS with unix socket help program for test
 *
 * Copyright IBM, Inc. 2013
 *
 * Authors:
 *  Wenchao Xia    <xiawenc@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/* #define SOCKET_SCM_DEBUG */

/*
 * @fd and @fd_to_send will not be checked for validation in this function,
 * a blank will be sent as iov data to notify qemu.
 */
static int send_fd(int fd, int fd_to_send)
{
    struct msghdr msg;
    struct iovec iov[1];
    int ret;
    char control[CMSG_SPACE(sizeof(int))];
    struct cmsghdr *cmsg;

    memset(&msg, 0, sizeof(msg));
    memset(control, 0, sizeof(control));

    /* Send a blank to notify qemu */
    iov[0].iov_base = (void *)" ";
    iov[0].iov_len = 1;

    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    cmsg = CMSG_FIRSTHDR(&msg);

    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));

    do {
        ret = sendmsg(fd, &msg, 0);
    } while (ret < 0 && errno == EINTR);

    if (ret < 0) {
        fprintf(stderr, "Failed to send msg, reason: %s\n", strerror(errno));
    }

    return ret;
}

/* Convert string to fd number. */
static int get_fd_num(const char *fd_str)
{
    int sock;
    char *err;

    errno = 0;
    sock = strtol(fd_str, &err, 10);
    if (errno) {
        fprintf(stderr, "Failed in strtol for socket fd, reason: %s\n",
                strerror(errno));
        return -1;
    }
    if (!*fd_str || *err || sock < 0) {
        fprintf(stderr, "bad numerical value for socket fd '%s'\n", fd_str);
        return -1;
    }

    return sock;
}

/*
 * To make things simple, the caller needs to specify:
 * 1. socket fd.
 * 2. path of the file to be sent.
 */
int main(int argc, char **argv, char **envp)
{
    int sock, fd, ret;

#ifdef SOCKET_SCM_DEBUG
    int i;
    for (i = 0; i < argc; i++) {
        fprintf(stderr, "Parameter %d: %s\n", i, argv[i]);
    }
#endif

    if (argc != 3) {
        fprintf(stderr,
                "Usage: %s < socket-fd > < file-path >\n",
                argv[0]);
        return EXIT_FAILURE;
    }


    sock = get_fd_num(argv[1]);
    if (sock < 0) {
        return EXIT_FAILURE;
    }

    /* Now only open a file in readonly mode for test purpose. If more precise
       control is needed, use python script in file operation, which is
       supposed to fork and exec this program. */
    fd = open(argv[2], O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Failed to open file '%s'\n", argv[2]);
        return EXIT_FAILURE;
    }

    ret = send_fd(sock, fd);
    if (ret < 0) {
        close(fd);
        return EXIT_FAILURE;
    }

    close(fd);
    return EXIT_SUCCESS;
}
