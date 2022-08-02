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
 */

#ifndef TESTS_SOCKET_HELPERS_H
#define TESTS_SOCKET_HELPERS_H

/*
 * @has_ipv4: set to true on return if IPv4 is available
 * @has_ipv6: set to true on return if IPv6 is available
 *
 * Check whether IPv4 and/or IPv6 are available for use.
 * On success, @has_ipv4 and @has_ipv6 will be set to
 * indicate whether the respective protocols are available.
 *
 * Returns 0 on success, -1 on fatal error
 */
int socket_check_protocol_support(bool *has_ipv4, bool *has_ipv6);

/*
 * @has_afunix: set to true on return if unix socket support is available
 *
 * Check whether unix domain socket support is available for use.
 * On success, @has_afunix will be set to indicate whether AF_UNIX protocol
 * is available.
 */
void socket_check_afunix_support(bool *has_afunix);

#endif
