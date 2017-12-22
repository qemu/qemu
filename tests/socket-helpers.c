/*
 * Helper functions for tests using sockets
 *
 * Copyright 2015-2018 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/sockets.h"
#include "socket-helpers.h"

#ifndef AI_ADDRCONFIG
# define AI_ADDRCONFIG 0
#endif
#ifndef EAI_ADDRFAMILY
# define EAI_ADDRFAMILY 0
#endif

int socket_can_bind(const char *hostname)
{
    int fd = -1;
    struct addrinfo ai, *res = NULL;
    int rc;
    int ret = -1;

    memset(&ai, 0, sizeof(ai));
    ai.ai_flags = AI_CANONNAME | AI_ADDRCONFIG;
    ai.ai_family = AF_UNSPEC;
    ai.ai_socktype = SOCK_STREAM;

    /* lookup */
    rc = getaddrinfo(hostname, NULL, &ai, &res);
    if (rc != 0) {
        if (rc == EAI_ADDRFAMILY ||
            rc == EAI_FAMILY) {
            errno = EADDRNOTAVAIL;
        } else {
            errno = EINVAL;
        }
        goto cleanup;
    }

    fd = qemu_socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        goto cleanup;
    }

    if (bind(fd, res->ai_addr, res->ai_addrlen) < 0) {
        goto cleanup;
    }

    ret = 0;

 cleanup:
    if (fd != -1) {
        close(fd);
    }
    if (res) {
        freeaddrinfo(res);
    }
    return ret;
}


int socket_check_protocol_support(bool *has_ipv4, bool *has_ipv6)
{
    *has_ipv4 = *has_ipv6 = false;

    if (socket_can_bind("127.0.0.1") < 0) {
        if (errno != EADDRNOTAVAIL) {
            return -1;
        }
    } else {
        *has_ipv4 = true;
    }

    if (socket_can_bind("::1") < 0) {
        if (errno != EADDRNOTAVAIL) {
            return -1;
        }
    } else {
        *has_ipv6 = true;
    }

    return 0;
}
