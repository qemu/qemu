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
#include "qapi-types.h"

int net_init_dump(const NetClientOptions *opts, const char *name,
                  NetClientState *peer);

#ifdef CONFIG_SLIRP
int net_init_slirp(const NetClientOptions *opts, const char *name,
                   NetClientState *peer);
#endif

int net_init_hubport(const NetClientOptions *opts, const char *name,
                     NetClientState *peer);

int net_init_socket(const NetClientOptions *opts, const char *name,
                    NetClientState *peer);

int net_init_tap(const NetClientOptions *opts, const char *name,
                 NetClientState *peer);

int net_init_bridge(const NetClientOptions *opts, const char *name,
                    NetClientState *peer);

#ifdef CONFIG_VDE
int net_init_vde(const NetClientOptions *opts, const char *name,
                 NetClientState *peer);
#endif

#endif /* QEMU_NET_CLIENTS_H */
