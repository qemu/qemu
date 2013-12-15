/* crc32.h
 * Declaration of CRC-32 routine and table
 *
 * $Id: crc32.h 20485 2007-01-18 18:43:30Z guy $
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * Copied from README.developer
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __CRC32_H_
#define __CRC32_H_

#define guint32 unsigned long
#define guint16 unsigned short
#define guint8 unsigned char
#define guint unsigned int

extern const guint32 crc32_ccitt_table[256];

extern guint32 crc32_ccitt(const guint8 *buf, guint len);
extern guint32 crc32_ccitt_seed(const guint8 *buf, guint len, guint32 seed);


typedef struct {
    const guint8 *ptr;
    int len;
} vec_t;

extern int in_cksum(const vec_t *vec, int veclen);
extern guint16 ip_checksum(const guint8 *ptr, int len);



#endif /* crc32.h */
