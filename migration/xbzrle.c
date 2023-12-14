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
#include "qemu/host-utils.h"
#include "xbzrle.h"

#if defined(CONFIG_AVX512BW_OPT)
#include <immintrin.h>
#include "host/cpuinfo.h"

static int __attribute__((target("avx512bw")))
xbzrle_encode_buffer_avx512(uint8_t *old_buf, uint8_t *new_buf, int slen,
                            uint8_t *dst, int dlen)
{
    uint32_t zrun_len = 0, nzrun_len = 0;
    int d = 0, i = 0, num = 0;
    uint8_t *nzrun_start = NULL;
    /* add 1 to include residual part in main loop */
    uint32_t count512s = (slen >> 6) + 1;
    /* countResidual is tail of data, i.e., countResidual = slen % 64 */
    uint32_t count_residual = slen & 0b111111;
    bool never_same = true;
    uint64_t mask_residual = 1;
    mask_residual <<= count_residual;
    mask_residual -= 1;
    __m512i r = _mm512_set1_epi32(0);

    while (count512s) {
        int bytes_to_check = 64;
        uint64_t mask = 0xffffffffffffffff;
        if (count512s == 1) {
            bytes_to_check = count_residual;
            mask = mask_residual;
        }
        __m512i old_data = _mm512_mask_loadu_epi8(r,
                                                  mask, old_buf + i);
        __m512i new_data = _mm512_mask_loadu_epi8(r,
                                                  mask, new_buf + i);
        uint64_t comp = _mm512_cmpeq_epi8_mask(old_data, new_data);
        count512s--;

        bool is_same = (comp & 0x1);
        while (bytes_to_check) {
            if (d + 2 > dlen) {
                return -1;
            }
            if (is_same) {
                if (nzrun_len) {
                    d += uleb128_encode_small(dst + d, nzrun_len);
                    if (d + nzrun_len > dlen) {
                        return -1;
                    }
                    nzrun_start = new_buf + i - nzrun_len;
                    memcpy(dst + d, nzrun_start, nzrun_len);
                    d += nzrun_len;
                    nzrun_len = 0;
                }
                /* 64 data at a time for speed */
                if (count512s && (comp == 0xffffffffffffffff)) {
                    i += 64;
                    zrun_len += 64;
                    break;
                }
                never_same = false;
                num = ctz64(~comp);
                num = (num < bytes_to_check) ? num : bytes_to_check;
                zrun_len += num;
                bytes_to_check -= num;
                comp >>= num;
                i += num;
                if (bytes_to_check) {
                    /* still has different data after same data */
                    d += uleb128_encode_small(dst + d, zrun_len);
                    zrun_len = 0;
                } else {
                    break;
                }
            }
            if (never_same || zrun_len) {
                /*
                 * never_same only acts if
                 * data begins with diff in first count512s
                 */
                d += uleb128_encode_small(dst + d, zrun_len);
                zrun_len = 0;
                never_same = false;
            }
            /* has diff, 64 data at a time for speed */
            if ((bytes_to_check == 64) && (comp == 0x0)) {
                i += 64;
                nzrun_len += 64;
                break;
            }
            num = ctz64(comp);
            num = (num < bytes_to_check) ? num : bytes_to_check;
            nzrun_len += num;
            bytes_to_check -= num;
            comp >>= num;
            i += num;
            if (bytes_to_check) {
                /* mask like 111000 */
                d += uleb128_encode_small(dst + d, nzrun_len);
                /* overflow */
                if (d + nzrun_len > dlen) {
                    return -1;
                }
                nzrun_start = new_buf + i - nzrun_len;
                memcpy(dst + d, nzrun_start, nzrun_len);
                d += nzrun_len;
                nzrun_len = 0;
                is_same = true;
            }
        }
    }

    if (nzrun_len != 0) {
        d += uleb128_encode_small(dst + d, nzrun_len);
        /* overflow */
        if (d + nzrun_len > dlen) {
            return -1;
        }
        nzrun_start = new_buf + i - nzrun_len;
        memcpy(dst + d, nzrun_start, nzrun_len);
        d += nzrun_len;
    }
    return d;
}

static int xbzrle_encode_buffer_int(uint8_t *old_buf, uint8_t *new_buf,
                                    int slen, uint8_t *dst, int dlen);

static int (*accel_func)(uint8_t *, uint8_t *, int, uint8_t *, int);

static void __attribute__((constructor)) init_accel(void)
{
    unsigned info = cpuinfo_init();
    if (info & CPUINFO_AVX512BW) {
        accel_func = xbzrle_encode_buffer_avx512;
    } else {
        accel_func = xbzrle_encode_buffer_int;
    }
}

int xbzrle_encode_buffer(uint8_t *old_buf, uint8_t *new_buf, int slen,
                         uint8_t *dst, int dlen)
{
    return accel_func(old_buf, new_buf, slen, dst, dlen);
}

#define xbzrle_encode_buffer xbzrle_encode_buffer_int
#endif

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
