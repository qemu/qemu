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
#include "qemu/cutils.h"
#include "xbzrle.h"

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

#if defined(__x86_64__) && defined (__AVX512BW__)
#include <immintrin.h>
#include <math.h>
#define SET_ZERO512(r) r = _mm512_set1_epi32(0)
int xbzrle_encode_buffer_512(uint8_t *old_buf, uint8_t *new_buf, int slen,
                             uint8_t *dst, int dlen)
{
    uint32_t zrun_len = 0, nzrun_len = 0;
    int d = 0, i = 0, num = 0;
    uint8_t *nzrun_start = NULL;
    int count512s = (slen >> 6);
    int res = slen % 64;
    bool never_same = true;
    while (count512s--) {
        if (d+2 > dlen)
            return -1;
        __m512i old_data = _mm512_mask_loadu_epi8(old_data, 0xffffffffffffffff, old_buf+i);
        __m512i new_data = _mm512_mask_loadu_epi8(new_data, 0xffffffffffffffff, new_buf+i);
        // in mask bit 1 for same, 0 for diff
        __mmask64  comp = _mm512_cmpeq_epi8_mask(old_data, new_data);

        int bytesToCheck = 64;
        bool is_same = (comp & 0x1);
        while (bytesToCheck) {
            if (is_same) {
                if (nzrun_len) {
                    d += uleb128_encode_small(dst + d, nzrun_len);
                    if (d + nzrun_len > dlen)
                        return -1;
                    nzrun_start = new_buf+i-nzrun_len;
                    memcpy(dst + d, nzrun_start, nzrun_len);
                    d += nzrun_len;
                    nzrun_len = 0;
                }
                if (comp == 0xffffffffffffffff) {
                    i += 64;
                    zrun_len += 64;
                    break;
                }
                never_same = false;
                num = __builtin_ctzl(~comp);
                num = (num < bytesToCheck) ? num : bytesToCheck;
                zrun_len += num;
                bytesToCheck -= num;
                comp >>= num;
                i += num;
                if (bytesToCheck) {
                    // still has different data after same data
                    d += uleb128_encode_small(dst + d, zrun_len);
                    zrun_len=0;
                }
                else {
                    break;
                }
            }
            if (never_same || zrun_len) {
                // never_same only happend first block's first part of data in 512 group is diffrent
                d += uleb128_encode_small(dst + d, zrun_len);
                zrun_len = 0;
                never_same = false;
            }
            // has diff
            if ((bytesToCheck == 64) && (comp == 0x0)) {
                i += 64;
                nzrun_len += 64;
                break;
            }
            num = __builtin_ctzl(comp);
            num = (num < bytesToCheck) ? num : bytesToCheck;
            nzrun_len += num;
            bytesToCheck -= num;
            comp >>= num;
            i += num;
            if (bytesToCheck) {
                // mask like 111000
                d += uleb128_encode_small(dst + d, nzrun_len);
                // overflow
                if (d + nzrun_len > dlen)
                    return -1;
                nzrun_start = new_buf+i-nzrun_len;
                memcpy(dst + d, nzrun_start, nzrun_len);
                d += nzrun_len;
                nzrun_len = 0;
                is_same = true;
            }
        }
    }
    if (res) {
        /* the number of data is less than 64 */
        unsigned long long mask = pow(2,res);
        mask -= 1;
        __m512i r = SET_ZERO512(r);
        __m512i old_data = _mm512_mask_loadu_epi8(r, mask, old_buf+i);
        __m512i new_data = _mm512_mask_loadu_epi8(r, mask, new_buf+i);
        __mmask64 comp = _mm512_cmpeq_epi8_mask(old_data, new_data);

        int bytesToCheck = res;
        bool is_same = (comp & 0x1);
        while (bytesToCheck) {
            if (is_same) {
                if (nzrun_len) {
                    d += uleb128_encode_small(dst + d, nzrun_len);
                    if ( d + nzrun_len > dlen )
                        return -1;
                    nzrun_start = new_buf+i-nzrun_len;
                    memcpy(dst + d, nzrun_start, nzrun_len);
                    d += nzrun_len;
                    nzrun_len = 0;
                }
                never_same = false;
                num = __builtin_ctzl(~comp);
                num = (num < bytesToCheck) ? num : bytesToCheck;
                zrun_len += num;
                bytesToCheck -= num;
                comp >>= num;
                i += num;
                if (bytesToCheck) {
                    // diff after same
                    d += uleb128_encode_small(dst + d, zrun_len);
                    zrun_len=0;
                }
                else {
                    break;
                }
            }

            if (never_same || zrun_len) {
                // this only happend first block's first part of data in res is diffrent
                d += uleb128_encode_small(dst + d, zrun_len);
                zrun_len = 0;
                never_same = false;
            }
            // has diff
            num = __builtin_ctzl(comp);
            num = (num < bytesToCheck) ? num : bytesToCheck;
            nzrun_len += num;
            bytesToCheck -= num;
            comp >>= num;
            i += num;
            if (bytesToCheck) {
                // mask like 111000
                d += uleb128_encode_small(dst + d, nzrun_len);
                // overflow
                if( d + nzrun_len > dlen )
                    return -1;
                nzrun_start = new_buf+i-nzrun_len;
                memcpy(dst + d, nzrun_start, nzrun_len);
                d += nzrun_len;
                nzrun_len = 0;
                is_same = true;
            }
        }
    }

    if (zrun_len)
        return (zrun_len == slen) ? 0 : d;

    if (nzrun_len != 0) {
        d += uleb128_encode_small(dst + d, nzrun_len);
        // overflow
        if ( d + nzrun_len > dlen )
            return -1;
        nzrun_start = new_buf+i-nzrun_len;
        memcpy(dst + d, nzrun_start, nzrun_len);
        d += nzrun_len;
    }
    return d;
}
#endif
