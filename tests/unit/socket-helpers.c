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
#include "qemu/sockets.h"
#include "socket-helpers.h"

#ifndef AI_ADDRCONFIG
# define AI_ADDRCONFIG 0
#endif
#ifndef EAI_ADDRFAMILY
# define EAI_ADDRFAMILY 0
#endif

/*
 * @hostname: a DNS name or numeric IP address
 *
 * Check whether it is possible to bind & connect to ports
 * on the DNS name or IP address @hostname. If an IP address
 * is used, it must not be a wildcard address.
 *
 * Returns 0 on success, -1 on error with errno set
 */
static int socket_can_bind_connect(const char *hostname, int family)
{
    int lfd = -1, cfd = -1, afd = -1;
    struct addrinfo ai, *res = NULL;
    struct sockaddr_storage ss;
    socklen_t sslen = sizeof(ss);
    int soerr;
    socklen_t soerrlen = sizeof(soerr);
    bool check_soerr = false;
    int rc;
    int ret = -1;

    memset(&ai, 0, sizeof(ai));
    ai.ai_flags = AI_CANONNAME | AI_ADDRCONFIG;
    ai.ai_family = family;
    ai.ai_socktype = SOCK_STREAM;

    /* lookup */
    rc = getaddrinfo(hostname, NULL, &ai, &res);
    if (rc != 0) {
        if (rc == EAI_ADDRFAMILY || rc == EAI_FAMILY || rc == EAI_NONAME) {
            errno = EADDRNOTAVAIL;
        } else {
            errno = EINVAL;
        }
        goto cleanup;
    }

    lfd = qemu_socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (lfd < 0) {
        goto cleanup;
    }

    cfd = qemu_socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (cfd < 0) {
        goto cleanup;
    }

    if (bind(lfd, res->ai_addr, res->ai_addrlen) < 0) {
        goto cleanup;
    }

    if (listen(lfd, 1) < 0) {
        goto cleanup;
    }

    if (getsockname(lfd, (struct sockaddr *)&ss, &sslen) < 0) {
        goto cleanup;
    }

    qemu_socket_set_nonblock(cfd);
    if (connect(cfd, (struct sockaddr *)&ss, sslen) < 0) {
        if (errno == EINPROGRESS) {
            check_soerr = true;
        } else {
            goto cleanup;
        }
    }

    sslen = sizeof(ss);
    afd = accept(lfd,  (struct sockaddr *)&ss, &sslen);
    if (afd < 0) {
        goto cleanup;
    }

    if (check_soerr) {
        if (getsockopt(cfd, SOL_SOCKET, SO_ERROR, &soerr, &soerrlen) < 0) {
            goto cleanup;
        }
        if (soerr) {
            errno = soerr;
            goto cleanup;
        }
    }

    ret = 0;

 cleanup:
    if (afd != -1) {
        close(afd);
    }
    if (cfd != -1) {
        close(cfd);
    }
    if (lfd != -1) {
        close(lfd);
    }
    if (res) {
        freeaddrinfo(res);
    }
    return ret;
}


int socket_check_protocol_support(bool *has_ipv4, bool *has_ipv6)
{
    *has_ipv4 = *has_ipv6 = false;

    if (socket_can_bind_connect("127.0.0.1", PF_INET) < 0) {
        if (errno != EADDRNOTAVAIL) {
            return -1;
        }
    } else {
        *has_ipv4 = true;
    }

    if (socket_can_bind_connect("::1", PF_INET6) < 0) {
        if (errno != EADDRNOTAVAIL) {
            return -1;
        }
    } else {
        *has_ipv6 = true;
    }

    return 0;
}

void socket_check_afunix_support(bool *has_afunix)
{
    int fd;

    fd = socket(PF_UNIX, SOCK_STREAM, 0);
    closesocket(fd);

#ifdef _WIN32
    *has_afunix = (fd != (int)INVALID_SOCKET);
#else
    *has_afunix = (fd >= 0);
#endif

    return;
}
