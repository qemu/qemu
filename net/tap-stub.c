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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "tap_int.h"

int tap_open(char *ifname, int ifname_size, int *vnet_hdr,
             int vnet_hdr_required, int mq_required, Error **errp)
{
    error_setg(errp, "tap is not supported in this build");
    return -1;
}

void tap_set_sndbuf(int fd, const NetdevTapOptions *tap, Error **errp)
{
}

int tap_probe_vnet_hdr(int fd, Error **errp)
{
    return 0;
}

int tap_probe_has_ufo(int fd)
{
    return 0;
}

int tap_probe_vnet_hdr_len(int fd, int len)
{
    return 0;
}

void tap_fd_set_vnet_hdr_len(int fd, int len)
{
}

int tap_fd_set_vnet_le(int fd, int is_le)
{
    return -EINVAL;
}

int tap_fd_set_vnet_be(int fd, int is_be)
{
    return -EINVAL;
}

void tap_fd_set_offload(int fd, int csum, int tso4,
                        int tso6, int ecn, int ufo)
{
}

int tap_fd_enable(int fd)
{
    return -1;
}

int tap_fd_disable(int fd)
{
    return -1;
}

int tap_fd_get_ifname(int fd, char *ifname)
{
    return -1;
}
