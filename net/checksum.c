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
#include "net/eth.h"

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
    int mac_hdr_len, ip_len;
    struct ip_header *ip;

    /*
     * Note: We cannot assume "data" is aligned, so the all code uses
     * some macros that take care of possible unaligned access for
     * struct members (just in case).
     */

    /* Ensure we have at least an Eth header */
    if (length < sizeof(struct eth_header)) {
        return;
    }

    /* Handle the optionnal VLAN headers */
    switch (lduw_be_p(&PKT_GET_ETH_HDR(data)->h_proto)) {
    case ETH_P_VLAN:
        mac_hdr_len = sizeof(struct eth_header) +
                     sizeof(struct vlan_header);
        break;
    case ETH_P_DVLAN:
        if (lduw_be_p(&PKT_GET_VLAN_HDR(data)->h_proto) == ETH_P_VLAN) {
            mac_hdr_len = sizeof(struct eth_header) +
                         2 * sizeof(struct vlan_header);
        } else {
            mac_hdr_len = sizeof(struct eth_header) +
                         sizeof(struct vlan_header);
        }
        break;
    default:
        mac_hdr_len = sizeof(struct eth_header);
        break;
    }

    length -= mac_hdr_len;

    /* Now check we have an IP header (with an optionnal VLAN header) */
    if (length < sizeof(struct ip_header)) {
        return;
    }

    ip = (struct ip_header *)(data + mac_hdr_len);

    if (IP_HEADER_VERSION(ip) != IP_HEADER_VERSION_4) {
        return; /* not IPv4 */
    }

    ip_len = lduw_be_p(&ip->ip_len);

    /* Last, check that we have enough data for the all IP frame */
    if (length < ip_len) {
        return;
    }

    ip_len -= IP_HDR_GET_LEN(ip);

    switch (ip->ip_p) {
    case IP_PROTO_TCP:
    {
        uint16_t csum;
        tcp_header *tcp = (tcp_header *)(ip + 1);

        if (ip_len < sizeof(tcp_header)) {
            return;
        }

        /* Set csum to 0 */
        stw_he_p(&tcp->th_sum, 0);

        csum = net_checksum_tcpudp(ip_len, ip->ip_p,
                                   (uint8_t *)&ip->ip_src,
                                   (uint8_t *)tcp);

        /* Store computed csum */
        stw_be_p(&tcp->th_sum, csum);

        break;
    }
    case IP_PROTO_UDP:
    {
        uint16_t csum;
        udp_header *udp = (udp_header *)(ip + 1);

        if (ip_len < sizeof(udp_header)) {
            return;
        }

        /* Set csum to 0 */
        stw_he_p(&udp->uh_sum, 0);

        csum = net_checksum_tcpudp(ip_len, ip->ip_p,
                                   (uint8_t *)&ip->ip_src,
                                   (uint8_t *)udp);

        /* Store computed csum */
        stw_be_p(&udp->uh_sum, csum);

        break;
    }
    default:
        /* Can't handle any other protocol */
        break;
    }
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
