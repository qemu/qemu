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

#include "qemu/bswap.h"
struct iovec;

uint32_t net_checksum_add_cont(int len, uint8_t *buf, int seq);
uint16_t net_checksum_finish(uint32_t sum);
uint16_t net_checksum_tcpudp(uint16_t length, uint16_t proto,
                             uint8_t *addrs, uint8_t *buf);
void net_checksum_calculate(uint8_t *data, int length);

static inline uint32_t
net_checksum_add(int len, uint8_t *buf)
{
    return net_checksum_add_cont(len, buf, 0);
}

static inline uint16_t
net_raw_checksum(uint8_t *data, int length)
{
    return net_checksum_finish(net_checksum_add(length, data));
}

/**
 * net_checksum_add_iov: scatter-gather vector checksumming
 *
 * @iov: input scatter-gather array
 * @iov_cnt: number of array elements
 * @iov_off: starting iov offset for checksumming
 * @size: length of data to be checksummed
 */
uint32_t net_checksum_add_iov(const struct iovec *iov,
                              const unsigned int iov_cnt,
                              uint32_t iov_off, uint32_t size);

typedef struct toeplitz_key_st {
    uint32_t leftmost_32_bits;
    uint8_t *next_byte;
} net_toeplitz_key;

static inline
void net_toeplitz_key_init(net_toeplitz_key *key, uint8_t *key_bytes)
{
    key->leftmost_32_bits = be32_to_cpu(*(uint32_t *)key_bytes);
    key->next_byte = key_bytes + sizeof(uint32_t);
}

static inline
void net_toeplitz_add(uint32_t *result,
                      uint8_t *input,
                      uint32_t len,
                      net_toeplitz_key *key)
{
    register uint32_t accumulator = *result;
    register uint32_t leftmost_32_bits = key->leftmost_32_bits;
    register uint32_t byte;

    for (byte = 0; byte < len; byte++) {
        register uint8_t input_byte = input[byte];
        register uint8_t key_byte = *(key->next_byte++);
        register uint8_t bit;

        for (bit = 0; bit < 8; bit++) {
            if (input_byte & (1 << 7)) {
                accumulator ^= leftmost_32_bits;
            }

            leftmost_32_bits =
                (leftmost_32_bits << 1) | ((key_byte & (1 << 7)) >> 7);

            input_byte <<= 1;
            key_byte <<= 1;
        }
    }

    key->leftmost_32_bits = leftmost_32_bits;
    *result = accumulator;
}

#endif /* QEMU_NET_CHECKSUM_H */
