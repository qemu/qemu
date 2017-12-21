/*
 * Tests for util/qemu-sockets.c
 *
 * Copyright 2018 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/sockets.h"
#include "qapi/error.h"
#include "socket-helpers.h"

static void test_fd_is_socket_bad(void)
{
    char *tmp = g_strdup("qemu-test-util-sockets-XXXXXX");
    int fd = mkstemp(tmp);
    if (fd != 0) {
        unlink(tmp);
    }
    g_free(tmp);

    g_assert(fd >= 0);

    g_assert(!fd_is_socket(fd));
    close(fd);
}

static void test_fd_is_socket_good(void)
{
    int fd = qemu_socket(PF_INET, SOCK_STREAM, 0);

    g_assert(fd >= 0);

    g_assert(fd_is_socket(fd));
    close(fd);
}

int main(int argc, char **argv)
{
    bool has_ipv4, has_ipv6;

    socket_init();

    g_test_init(&argc, &argv, NULL);

    /* We're creating actual IPv4/6 sockets, so we should
     * check if the host running tests actually supports
     * each protocol to avoid breaking tests on machines
     * with either IPv4 or IPv6 disabled.
     */
    if (socket_check_protocol_support(&has_ipv4, &has_ipv6) < 0) {
        return 1;
    }

    if (has_ipv4) {
        g_test_add_func("/util/socket/is-socket/bad",
                        test_fd_is_socket_bad);
        g_test_add_func("/util/socket/is-socket/good",
                        test_fd_is_socket_good);
    }

    return g_test_run();
}
