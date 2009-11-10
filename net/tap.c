/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2009 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "net/tap.h"

#include "config-host.h"

#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <net/if.h>

#include "net.h"
#include "sysemu.h"
#include "qemu-char.h"
#include "qemu-common.h"

#include "net/tap-linux.h"

/* Maximum GSO packet size (64k) plus plenty of room for
 * the ethernet and virtio_net headers
 */
#define TAP_BUFSIZE (4096 + 65536)

typedef struct TAPState {
    VLANClientState *vc;
    int fd;
    char down_script[1024];
    char down_script_arg[128];
    uint8_t buf[TAP_BUFSIZE];
    unsigned int read_poll : 1;
    unsigned int write_poll : 1;
    unsigned int has_vnet_hdr : 1;
    unsigned int using_vnet_hdr : 1;
    unsigned int has_ufo: 1;
} TAPState;

static int launch_script(const char *setup_script, const char *ifname, int fd);

static int tap_can_send(void *opaque);
static void tap_send(void *opaque);
static void tap_writable(void *opaque);

static void tap_update_fd_handler(TAPState *s)
{
    qemu_set_fd_handler2(s->fd,
                         s->read_poll  ? tap_can_send : NULL,
                         s->read_poll  ? tap_send     : NULL,
                         s->write_poll ? tap_writable : NULL,
                         s);
}

static void tap_read_poll(TAPState *s, int enable)
{
    s->read_poll = !!enable;
    tap_update_fd_handler(s);
}

static void tap_write_poll(TAPState *s, int enable)
{
    s->write_poll = !!enable;
    tap_update_fd_handler(s);
}

static void tap_writable(void *opaque)
{
    TAPState *s = opaque;

    tap_write_poll(s, 0);

    qemu_flush_queued_packets(s->vc);
}

static ssize_t tap_write_packet(TAPState *s, const struct iovec *iov, int iovcnt)
{
    ssize_t len;

    do {
        len = writev(s->fd, iov, iovcnt);
    } while (len == -1 && errno == EINTR);

    if (len == -1 && errno == EAGAIN) {
        tap_write_poll(s, 1);
        return 0;
    }

    return len;
}

static ssize_t tap_receive_iov(VLANClientState *vc, const struct iovec *iov,
                               int iovcnt)
{
    TAPState *s = vc->opaque;
    const struct iovec *iovp = iov;
    struct iovec iov_copy[iovcnt + 1];
    struct virtio_net_hdr hdr = { 0, };

    if (s->has_vnet_hdr && !s->using_vnet_hdr) {
        iov_copy[0].iov_base = &hdr;
        iov_copy[0].iov_len =  sizeof(hdr);
        memcpy(&iov_copy[1], iov, iovcnt * sizeof(*iov));
        iovp = iov_copy;
        iovcnt++;
    }

    return tap_write_packet(s, iovp, iovcnt);
}

static ssize_t tap_receive_raw(VLANClientState *vc, const uint8_t *buf, size_t size)
{
    TAPState *s = vc->opaque;
    struct iovec iov[2];
    int iovcnt = 0;
    struct virtio_net_hdr hdr = { 0, };

    if (s->has_vnet_hdr) {
        iov[iovcnt].iov_base = &hdr;
        iov[iovcnt].iov_len  = sizeof(hdr);
        iovcnt++;
    }

    iov[iovcnt].iov_base = (char *)buf;
    iov[iovcnt].iov_len  = size;
    iovcnt++;

    return tap_write_packet(s, iov, iovcnt);
}

static ssize_t tap_receive(VLANClientState *vc, const uint8_t *buf, size_t size)
{
    TAPState *s = vc->opaque;
    struct iovec iov[1];

    if (s->has_vnet_hdr && !s->using_vnet_hdr) {
        return tap_receive_raw(vc, buf, size);
    }

    iov[0].iov_base = (char *)buf;
    iov[0].iov_len  = size;

    return tap_write_packet(s, iov, 1);
}

static int tap_can_send(void *opaque)
{
    TAPState *s = opaque;

    return qemu_can_send_packet(s->vc);
}

#ifndef __sun__
ssize_t tap_read_packet(int tapfd, uint8_t *buf, int maxlen)
{
    return read(tapfd, buf, maxlen);
}
#endif

static void tap_send_completed(VLANClientState *vc, ssize_t len)
{
    TAPState *s = vc->opaque;
    tap_read_poll(s, 1);
}

static void tap_send(void *opaque)
{
    TAPState *s = opaque;
    int size;

    do {
        uint8_t *buf = s->buf;

        size = tap_read_packet(s->fd, s->buf, sizeof(s->buf));
        if (size <= 0) {
            break;
        }

        if (s->has_vnet_hdr && !s->using_vnet_hdr) {
            buf  += sizeof(struct virtio_net_hdr);
            size -= sizeof(struct virtio_net_hdr);
        }

        size = qemu_send_packet_async(s->vc, buf, size, tap_send_completed);
        if (size == 0) {
            tap_read_poll(s, 0);
        }
    } while (size > 0 && qemu_can_send_packet(s->vc));
}

int tap_has_ufo(VLANClientState *vc)
{
    TAPState *s = vc->opaque;

    assert(vc->type == NET_CLIENT_TYPE_TAP);

    return s->has_ufo;
}

int tap_has_vnet_hdr(VLANClientState *vc)
{
    TAPState *s = vc->opaque;

    assert(vc->type == NET_CLIENT_TYPE_TAP);

    return s->has_vnet_hdr;
}

void tap_using_vnet_hdr(VLANClientState *vc, int using_vnet_hdr)
{
    TAPState *s = vc->opaque;

    using_vnet_hdr = using_vnet_hdr != 0;

    assert(vc->type == NET_CLIENT_TYPE_TAP);
    assert(s->has_vnet_hdr == using_vnet_hdr);

    s->using_vnet_hdr = using_vnet_hdr;
}

void tap_set_offload(VLANClientState *vc, int csum, int tso4,
                     int tso6, int ecn, int ufo)
{
    TAPState *s = vc->opaque;

    return tap_fd_set_offload(s->fd, csum, tso4, tso6, ecn, ufo);
}

static void tap_cleanup(VLANClientState *vc)
{
    TAPState *s = vc->opaque;

    qemu_purge_queued_packets(vc);

    if (s->down_script[0])
        launch_script(s->down_script, s->down_script_arg, s->fd);

    tap_read_poll(s, 0);
    tap_write_poll(s, 0);
    close(s->fd);
    qemu_free(s);
}

/* fd support */

static TAPState *net_tap_fd_init(VLANState *vlan,
                                 const char *model,
                                 const char *name,
                                 int fd,
                                 int vnet_hdr)
{
    TAPState *s;

    s = qemu_mallocz(sizeof(TAPState));
    s->fd = fd;
    s->has_vnet_hdr = vnet_hdr != 0;
    s->using_vnet_hdr = 0;
    s->vc = qemu_new_vlan_client(NET_CLIENT_TYPE_TAP,
                                 vlan, NULL, model, name, NULL,
                                 tap_receive, tap_receive_raw,
                                 tap_receive_iov, tap_cleanup, s);
    s->has_ufo = tap_probe_has_ufo(s->fd);
    tap_set_offload(s->vc, 0, 0, 0, 0, 0);
    tap_read_poll(s, 1);
    return s;
}

static int launch_script(const char *setup_script, const char *ifname, int fd)
{
    sigset_t oldmask, mask;
    int pid, status;
    char *args[3];
    char **parg;

    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);

    /* try to launch network script */
    pid = fork();
    if (pid == 0) {
        int open_max = sysconf(_SC_OPEN_MAX), i;

        for (i = 0; i < open_max; i++) {
            if (i != STDIN_FILENO &&
                i != STDOUT_FILENO &&
                i != STDERR_FILENO &&
                i != fd) {
                close(i);
            }
        }
        parg = args;
        *parg++ = (char *)setup_script;
        *parg++ = (char *)ifname;
        *parg++ = NULL;
        execv(setup_script, args);
        _exit(1);
    } else if (pid > 0) {
        while (waitpid(pid, &status, 0) != pid) {
            /* loop */
        }
        sigprocmask(SIG_SETMASK, &oldmask, NULL);

        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            return 0;
        }
    }
    fprintf(stderr, "%s: could not launch network script\n", setup_script);
    return -1;
}

static int net_tap_init(QemuOpts *opts, int *vnet_hdr)
{
    int fd, vnet_hdr_required;
    char ifname[128] = {0,};
    const char *setup_script;

    if (qemu_opt_get(opts, "ifname")) {
        pstrcpy(ifname, sizeof(ifname), qemu_opt_get(opts, "ifname"));
    }

    *vnet_hdr = qemu_opt_get_bool(opts, "vnet_hdr", 1);
    if (qemu_opt_get(opts, "vnet_hdr")) {
        vnet_hdr_required = *vnet_hdr;
    } else {
        vnet_hdr_required = 0;
    }

    TFR(fd = tap_open(ifname, sizeof(ifname), vnet_hdr, vnet_hdr_required));
    if (fd < 0) {
        return -1;
    }

    setup_script = qemu_opt_get(opts, "script");
    if (setup_script &&
        setup_script[0] != '\0' &&
        strcmp(setup_script, "no") != 0 &&
        launch_script(setup_script, ifname, fd)) {
        close(fd);
        return -1;
    }

    qemu_opt_set(opts, "ifname", ifname);

    return fd;
}

int net_init_tap(QemuOpts *opts, Monitor *mon, const char *name, VLANState *vlan)
{
    TAPState *s;
    int fd, vnet_hdr;

    if (qemu_opt_get(opts, "fd")) {
        if (qemu_opt_get(opts, "ifname") ||
            qemu_opt_get(opts, "script") ||
            qemu_opt_get(opts, "downscript") ||
            qemu_opt_get(opts, "vnet_hdr")) {
            qemu_error("ifname=, script=, downscript= and vnet_hdr= is invalid with fd=\n");
            return -1;
        }

        fd = net_handle_fd_param(mon, qemu_opt_get(opts, "fd"));
        if (fd == -1) {
            return -1;
        }

        fcntl(fd, F_SETFL, O_NONBLOCK);

        vnet_hdr = tap_probe_vnet_hdr(fd);
    } else {
        if (!qemu_opt_get(opts, "script")) {
            qemu_opt_set(opts, "script", DEFAULT_NETWORK_SCRIPT);
        }

        if (!qemu_opt_get(opts, "downscript")) {
            qemu_opt_set(opts, "downscript", DEFAULT_NETWORK_DOWN_SCRIPT);
        }

        fd = net_tap_init(opts, &vnet_hdr);
    }

    s = net_tap_fd_init(vlan, "tap", name, fd, vnet_hdr);
    if (!s) {
        close(fd);
        return -1;
    }

    if (tap_set_sndbuf(s->fd, opts) < 0) {
        return -1;
    }

    if (qemu_opt_get(opts, "fd")) {
        snprintf(s->vc->info_str, sizeof(s->vc->info_str), "fd=%d", fd);
    } else {
        const char *ifname, *script, *downscript;

        ifname     = qemu_opt_get(opts, "ifname");
        script     = qemu_opt_get(opts, "script");
        downscript = qemu_opt_get(opts, "downscript");

        snprintf(s->vc->info_str, sizeof(s->vc->info_str),
                 "ifname=%s,script=%s,downscript=%s",
                 ifname, script, downscript);

        if (strcmp(downscript, "no") != 0) {
            snprintf(s->down_script, sizeof(s->down_script), "%s", downscript);
            snprintf(s->down_script_arg, sizeof(s->down_script_arg), "%s", ifname);
        }
    }

    if (vlan) {
        vlan->nb_host_devs++;
    }

    return 0;
}
