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

#ifndef QEMU_NET_TAP_H
#define QEMU_NET_TAP_H

#include "qemu-common.h"
#include "qemu-option.h"

#define DEFAULT_NETWORK_SCRIPT "/etc/qemu-ifup"
#define DEFAULT_NETWORK_DOWN_SCRIPT "/etc/qemu-ifdown"

int net_init_tap(QemuOpts *opts, Monitor *mon, const char *name, VLANState *vlan);

int tap_open(char *ifname, int ifname_size, int *vnet_hdr, int vnet_hdr_required);

ssize_t tap_read_packet(int tapfd, uint8_t *buf, int maxlen);

int tap_has_ufo(VLANClientState *vc);
int tap_has_vnet_hdr(VLANClientState *vc);
int tap_has_vnet_hdr_len(VLANClientState *vc, int len);
void tap_using_vnet_hdr(VLANClientState *vc, int using_vnet_hdr);
void tap_set_offload(VLANClientState *vc, int csum, int tso4, int tso6, int ecn, int ufo);
void tap_set_vnet_hdr_len(VLANClientState *vc, int len);

int tap_set_sndbuf(int fd, QemuOpts *opts);
int tap_probe_vnet_hdr(int fd);
int tap_probe_vnet_hdr_len(int fd, int len);
int tap_probe_has_ufo(int fd);
void tap_fd_set_offload(int fd, int csum, int tso4, int tso6, int ecn, int ufo);
void tap_fd_set_vnet_hdr_len(int fd, int len);

int tap_get_fd(VLANClientState *vc);

struct vhost_net;
struct vhost_net *tap_get_vhost_net(VLANClientState *vc);

int net_init_bridge(QemuOpts *opts, Monitor *mon, const char *name,
                    VLANState *vlan);

#endif /* QEMU_NET_TAP_H */
