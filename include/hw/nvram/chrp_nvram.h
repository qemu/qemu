/*
 * Common Hardware Reference Platform NVRAM functions.
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CHRP_NVRAM_H
#define CHRP_NVRAM_H

/* OpenBIOS NVRAM partition */
typedef struct {
    uint8_t signature;
    uint8_t checksum;
    uint16_t len;       /* Big endian, length divided by 16 */
    char name[12];
} ChrpNvramPartHdr;

#define CHRP_NVPART_SYSTEM 0x70
#define CHRP_NVPART_FREE 0x7f

static inline void
chrp_nvram_finish_partition(ChrpNvramPartHdr *header, uint32_t size)
{
    unsigned int i, sum;
    uint8_t *tmpptr;

    /* Length divided by 16 */
    header->len = cpu_to_be16(size >> 4);

    /* Checksum */
    tmpptr = (uint8_t *)header;
    sum = *tmpptr;
    for (i = 0; i < 14; i++) {
        sum += tmpptr[2 + i];
        sum = (sum + ((sum & 0xff00) >> 8)) & 0xff;
    }
    header->checksum = sum & 0xff;
}

int chrp_nvram_create_system_partition(uint8_t *data, int min_len);
int chrp_nvram_create_free_partition(uint8_t *data, int len);

#endif
