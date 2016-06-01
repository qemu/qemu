/*
 *  IP checksumming functions.
 *  (c) 2008 Gerd Hoffmann <kraxel@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 2 or later of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "net/checksum.h"

#define PROTO_TCP  6
#define PROTO_UDP 17

uint32_t net_checksum_add_cont(int len, uint8_t *buf, int seq)
{
    uint32_t sum = 0;
    int i;

    for (i = seq; i < seq + len; i++) {
        if (i & 1) {
            sum += (uint32_t)buf[i - seq];
        } else {
            sum += (uint32_t)buf[i - seq] << 8;
        }
    }
    return sum;
}

uint16_t net_checksum_finish(uint32_t sum)
{
    while (sum>>16)
	sum = (sum & 0xFFFF)+(sum >> 16);
    return ~sum;
}

uint16_t net_checksum_tcpudp(uint16_t length, uint16_t proto,
                             uint8_t *addrs, uint8_t *buf)
{
    uint32_t sum = 0;

    sum += net_checksum_add(length, buf);         // payload
    sum += net_checksum_add(8, addrs);            // src + dst address
    sum += proto + length;                        // protocol & length
    return net_checksum_finish(sum);
}

void net_checksum_calculate(uint8_t *data, int length)
{
    int hlen, plen, proto, csum_offset;
    uint16_t csum;

    /* Ensure data has complete L2 & L3 headers. */
    if (length < 14 + 20) {
        return;
    }

    if ((data[14] & 0xf0) != 0x40)
	return; /* not IPv4 */
    hlen  = (data[14] & 0x0f) * 4;
    plen  = (data[16] << 8 | data[17]) - hlen;
    proto = data[23];

    switch (proto) {
    case PROTO_TCP:
	csum_offset = 16;
	break;
    case PROTO_UDP:
	csum_offset = 6;
	break;
    default:
	return;
    }

    if (plen < csum_offset + 2 || 14 + hlen + plen > length) {
        return;
    }

    data[14+hlen+csum_offset]   = 0;
    data[14+hlen+csum_offset+1] = 0;
    csum = net_checksum_tcpudp(plen, proto, data+14+12, data+14+hlen);
    data[14+hlen+csum_offset]   = csum >> 8;
    data[14+hlen+csum_offset+1] = csum & 0xff;
}

uint32_t
net_checksum_add_iov(const struct iovec *iov, const unsigned int iov_cnt,
                     uint32_t iov_off, uint32_t size, uint32_t csum_offset)
{
    size_t iovec_off, buf_off;
    unsigned int i;
    uint32_t res = 0;

    iovec_off = 0;
    buf_off = 0;
    for (i = 0; i < iov_cnt && size; i++) {
        if (iov_off < (iovec_off + iov[i].iov_len)) {
            size_t len = MIN((iovec_off + iov[i].iov_len) - iov_off , size);
            void *chunk_buf = iov[i].iov_base + (iov_off - iovec_off);

            res += net_checksum_add_cont(len, chunk_buf, csum_offset);
            csum_offset += len;

            buf_off += len;
            iov_off += len;
            size -= len;
        }
        iovec_off += iov[i].iov_len;
    }
    return res;
}
