/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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
#ifndef QEMU_NET_CLIENTS_H
#define QEMU_NET_CLIENTS_H

#include "net/net.h"

int net_init_dump(const Netdev *netdev, const char *name,
                  NetClientState *peer, Error **errp);

#ifdef CONFIG_PASST
int net_init_passt(const Netdev *netdev, const char *name,
                   NetClientState *peer, Error **errp);
#endif
#ifdef CONFIG_SLIRP
int net_init_slirp(const Netdev *netdev, const char *name,
                   NetClientState *peer, Error **errp);
#endif

int net_init_hubport(const Netdev *netdev, const char *name,
                     NetClientState *peer, Error **errp);

int net_init_socket(const Netdev *netdev, const char *name,
                    NetClientState *peer, Error **errp);

int net_init_stream(const Netdev *netdev, const char *name,
                    NetClientState *peer, Error **errp);

int net_init_dgram(const Netdev *netdev, const char *name,
                   NetClientState *peer, Error **errp);

int net_init_tap(const Netdev *netdev, const char *name,
                 NetClientState *peer, Error **errp);

int net_init_bridge(const Netdev *netdev, const char *name,
                    NetClientState *peer, Error **errp);

int net_init_l2tpv3(const Netdev *netdev, const char *name,
                    NetClientState *peer, Error **errp);
#ifdef CONFIG_VDE
int net_init_vde(const Netdev *netdev, const char *name,
                 NetClientState *peer, Error **errp);
#endif

#ifdef CONFIG_ZEROTIER
int net_init_zerotier(const Netdev *netdev, const char *name,
                      NetClientState *peer, Error **errp);
#endif

#ifdef CONFIG_NETMAP
int net_init_netmap(const Netdev *netdev, const char *name,
                    NetClientState *peer, Error **errp);
#endif

#ifdef CONFIG_AF_XDP
int net_init_af_xdp(const Netdev *netdev, const char *name,
                    NetClientState *peer, Error **errp);
#endif

int net_init_vhost_user(const Netdev *netdev, const char *name,
                        NetClientState *peer, Error **errp);

int net_init_vhost_vdpa(const Netdev *netdev, const char *name,
                        NetClientState *peer, Error **errp);
#ifdef CONFIG_VMNET
int net_init_vmnet_host(const Netdev *netdev, const char *name,
                          NetClientState *peer, Error **errp);

int net_init_vmnet_shared(const Netdev *netdev, const char *name,
                          NetClientState *peer, Error **errp);

int net_init_vmnet_bridged(const Netdev *netdev, const char *name,
                          NetClientState *peer, Error **errp);
#endif /* CONFIG_VMNET */

#endif /* QEMU_NET_CLIENTS_H */
