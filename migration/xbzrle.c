/*
 * Xor Based Zero Run Length Encoding
 *
 * Copyright 2013 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Orit Wasserman  <owasserm@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "include/migration/migration.h"

/*
  page = zrun nzrun
       | zrun nzrun page

  zrun = length

  nzrun = length byte...

  length = uleb128 encoded integer
 */
int xbzrle_encode_buffer(uint8_t *old_buf, uint8_t *new_buf, int slen,
                         uint8_t *dst, int dlen)
{
    uint32_t zrun_len = 0, nzrun_len = 0;
    int d = 0, i = 0;
    long res;
    uint8_t *nzrun_start = NULL;

    g_assert(!(((uintptr_t)old_buf | (uintptr_t)new_buf | slen) %
               sizeof(long)));

    while (i < slen) {
        /* overflow */
        if (d + 2 > dlen) {
            return -1;
        }

        /* not aligned to sizeof(long) */
        res = (slen - i) % sizeof(long);
        while (res && old_buf[i] == new_buf[i]) {
            zrun_len++;
            i++;
            res--;
        }

        /* word at a time for speed */
        if (!res) {
            while (i < slen &&
                   (*(long *)(old_buf + i)) == (*(long *)(new_buf + i))) {
                i += sizeof(long);
                zrun_len += sizeof(long);
            }

            /* go over the rest */
            while (i < slen && old_buf[i] == new_buf[i]) {
                zrun_len++;
                i++;
            }
        }

        /* buffer unchanged */
        if (zrun_len == slen) {
            return 0;
        }

        /* skip last zero run */
        if (i == slen) {
            return d;
        }

        d += uleb128_encode_small(dst + d, zrun_len);

        zrun_len = 0;
        nzrun_start = new_buf + i;

        /* overflow */
        if (d + 2 > dlen) {
            return -1;
        }
        /* not aligned to sizeof(long) */
        res = (slen - i) % sizeof(long);
        while (res && old_buf[i] != new_buf[i]) {
            i++;
            nzrun_len++;
            res--;
        }

        /* word at a time for speed, use of 32-bit long okay */
        if (!res) {
            /* truncation to 32-bit long okay */
            unsigned long mask = (unsigned long)0x0101010101010101ULL;
            while (i < slen) {
                unsigned long xor;
                xor = *(unsigned long *)(old_buf + i)
                    ^ *(unsigned long *)(new_buf + i);
                if ((xor - mask) & ~xor & (mask << 7)) {
                    /* found the end of an nzrun within the current long */
                    while (old_buf[i] != new_buf[i]) {
                        nzrun_len++;
                        i++;
                    }
                    break;
                } else {
                    i += sizeof(long);
                    nzrun_len += sizeof(long);
                }
            }
        }

        d += uleb128_encode_small(dst + d, nzrun_len);
        /* overflow */
        if (d + nzrun_len > dlen) {
            return -1;
        }
        memcpy(dst + d, nzrun_start, nzrun_len);
        d += nzrun_len;
        nzrun_len = 0;
    }

    return d;
}

int xbzrle_decode_buffer(uint8_t *src, int slen, uint8_t *dst, int dlen)
{
    int i = 0, d = 0;
    int ret;
    uint32_t count = 0;

    while (i < slen) {

        /* zrun */
        if ((slen - i) < 2) {
            return -1;
        }

        ret = uleb128_decode_small(src + i, &count);
        if (ret < 0 || (i && !count)) {
            return -1;
        }
        i += ret;
        d += count;

        /* overflow */
        if (d > dlen) {
            return -1;
        }

        /* nzrun */
        if ((slen - i) < 2) {
            return -1;
        }

        ret = uleb128_decode_small(src + i, &count);
        if (ret < 0 || !count) {
            return -1;
        }
        i += ret;

        /* overflow */
        if (d + count > dlen || i + count > slen) {
            return -1;
        }

        memcpy(dst + d, src + i, count);
        d += count;
        i += count;
    }

    return d;
}
