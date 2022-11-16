/*
 * QEMU live migration
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_MIGRATION_XBZRLE_H
#define QEMU_MIGRATION_XBZRLE_H

int xbzrle_encode_buffer(uint8_t *old_buf, uint8_t *new_buf, int slen,
                         uint8_t *dst, int dlen);

int xbzrle_decode_buffer(uint8_t *src, int slen, uint8_t *dst, int dlen);
#if defined(CONFIG_AVX512BW_OPT)
int xbzrle_encode_buffer_avx512(uint8_t *old_buf, uint8_t *new_buf, int slen,
                                uint8_t *dst, int dlen);
#endif
#endif
