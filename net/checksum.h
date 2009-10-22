/*
 *  IP checksumming functions.
 *  (c) 2008 Gerd Hoffmann <kraxel@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QEMU_NET_CHECKSUM_H
#define QEMU_NET_CHECKSUM_H

#include <stdint.h>

uint32_t net_checksum_add(int len, uint8_t *buf);
uint16_t net_checksum_finish(uint32_t sum);
uint16_t net_checksum_tcpudp(uint16_t length, uint16_t proto,
                             uint8_t *addrs, uint8_t *buf);
void net_checksum_calculate(uint8_t *data, int length);

#endif /* QEMU_NET_CHECKSUM_H */
