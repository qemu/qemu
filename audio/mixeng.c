/*
 * QEMU Mixing engine
 *
 * Copyright (c) 2004-2005 Vassili Karpov (malc)
 * Copyright (c) 1998 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/error-report.h"
#include "audio.h"

#define AUDIO_CAP "mixeng"
#include "audio_int.h"

/* 8 bit */
#define ENDIAN_CONVERSION natural
#define ENDIAN_CONVERT(v) (v)

/* Signed 8 bit */
#define BSIZE 8
#define ITYPE int
#define IN_MIN SCHAR_MIN
#define IN_MAX SCHAR_MAX
#define SIGNED
#define SHIFT 8
#include "mixeng_template.h"
#undef SIGNED
#undef IN_MAX
#undef IN_MIN
#undef BSIZE
#undef ITYPE
#undef SHIFT

/* Unsigned 8 bit */
#define BSIZE 8
#define ITYPE uint
#define IN_MIN 0
#define IN_MAX UCHAR_MAX
#define SHIFT 8
#include "mixeng_template.h"
#undef IN_MAX
#undef IN_MIN
#undef BSIZE
#undef ITYPE
#undef SHIFT

#undef ENDIAN_CONVERT
#undef ENDIAN_CONVERSION

/* Signed 16 bit */
#define BSIZE 16
#define ITYPE int
#define IN_MIN SHRT_MIN
#define IN_MAX SHRT_MAX
#define SIGNED
#define SHIFT 16
#define ENDIAN_CONVERSION natural
#define ENDIAN_CONVERT(v) (v)
#include "mixeng_template.h"
#undef ENDIAN_CONVERT
#undef ENDIAN_CONVERSION
#define ENDIAN_CONVERSION swap
#define ENDIAN_CONVERT(v) bswap16 (v)
#include "mixeng_template.h"
#undef ENDIAN_CONVERT
#undef ENDIAN_CONVERSION
#undef SIGNED
#undef IN_MAX
#undef IN_MIN
#undef BSIZE
#undef ITYPE
#undef SHIFT

/* Unsigned 16 bit */
#define BSIZE 16
#define ITYPE uint
#define IN_MIN 0
#define IN_MAX USHRT_MAX
#define SHIFT 16
#define ENDIAN_CONVERSION natural
#define ENDIAN_CONVERT(v) (v)
#include "mixeng_template.h"
#undef ENDIAN_CONVERT
#undef ENDIAN_CONVERSION
#define ENDIAN_CONVERSION swap
#define ENDIAN_CONVERT(v) bswap16 (v)
#include "mixeng_template.h"
#undef ENDIAN_CONVERT
#undef ENDIAN_CONVERSION
#undef IN_MAX
#undef IN_MIN
#undef BSIZE
#undef ITYPE
#undef SHIFT

/* Signed 32 bit */
#define BSIZE 32
#define ITYPE int
#define IN_MIN INT32_MIN
#define IN_MAX INT32_MAX
#define SIGNED
#define SHIFT 32
#define ENDIAN_CONVERSION natural
#define ENDIAN_CONVERT(v) (v)
#include "mixeng_template.h"
#undef ENDIAN_CONVERT
#undef ENDIAN_CONVERSION
#define ENDIAN_CONVERSION swap
#define ENDIAN_CONVERT(v) bswap32 (v)
#include "mixeng_template.h"
#undef ENDIAN_CONVERT
#undef ENDIAN_CONVERSION
#undef SIGNED
#undef IN_MAX
#undef IN_MIN
#undef BSIZE
#undef ITYPE
#undef SHIFT

/* Unsigned 32 bit */
#define BSIZE 32
#define ITYPE uint
#define IN_MIN 0
#define IN_MAX UINT32_MAX
#define SHIFT 32
#define ENDIAN_CONVERSION natural
#define ENDIAN_CONVERT(v) (v)
#include "mixeng_template.h"
#undef ENDIAN_CONVERT
#undef ENDIAN_CONVERSION
#define ENDIAN_CONVERSION swap
#define ENDIAN_CONVERT(v) bswap32 (v)
#include "mixeng_template.h"
#undef ENDIAN_CONVERT
#undef ENDIAN_CONVERSION
#undef IN_MAX
#undef IN_MIN
#undef BSIZE
#undef ITYPE
#undef SHIFT

t_sample *mixeng_conv[2][2][2][3] = {
    {
        {
            {
                conv_natural_uint8_t_to_mono,
                conv_natural_uint16_t_to_mono,
                conv_natural_uint32_t_to_mono
            },
            {
                conv_natural_uint8_t_to_mono,
                conv_swap_uint16_t_to_mono,
                conv_swap_uint32_t_to_mono,
            }
        },
        {
            {
                conv_natural_int8_t_to_mono,
                conv_natural_int16_t_to_mono,
                conv_natural_int32_t_to_mono
            },
            {
                conv_natural_int8_t_to_mono,
                conv_swap_int16_t_to_mono,
                conv_swap_int32_t_to_mono
            }
        }
    },
    {
        {
            {
                conv_natural_uint8_t_to_stereo,
                conv_natural_uint16_t_to_stereo,
                conv_natural_uint32_t_to_stereo
            },
            {
                conv_natural_uint8_t_to_stereo,
                conv_swap_uint16_t_to_stereo,
                conv_swap_uint32_t_to_stereo
            }
        },
        {
            {
                conv_natural_int8_t_to_stereo,
                conv_natural_int16_t_to_stereo,
                conv_natural_int32_t_to_stereo
            },
            {
                conv_natural_int8_t_to_stereo,
                conv_swap_int16_t_to_stereo,
                conv_swap_int32_t_to_stereo,
            }
        }
    }
};

f_sample *mixeng_clip[2][2][2][3] = {
    {
        {
            {
                clip_natural_uint8_t_from_mono,
                clip_natural_uint16_t_from_mono,
                clip_natural_uint32_t_from_mono
            },
            {
                clip_natural_uint8_t_from_mono,
                clip_swap_uint16_t_from_mono,
                clip_swap_uint32_t_from_mono
            }
        },
        {
            {
                clip_natural_int8_t_from_mono,
                clip_natural_int16_t_from_mono,
                clip_natural_int32_t_from_mono
            },
            {
                clip_natural_int8_t_from_mono,
                clip_swap_int16_t_from_mono,
                clip_swap_int32_t_from_mono
            }
        }
    },
    {
        {
            {
                clip_natural_uint8_t_from_stereo,
                clip_natural_uint16_t_from_stereo,
                clip_natural_uint32_t_from_stereo
            },
            {
                clip_natural_uint8_t_from_stereo,
                clip_swap_uint16_t_from_stereo,
                clip_swap_uint32_t_from_stereo
            }
        },
        {
            {
                clip_natural_int8_t_from_stereo,
                clip_natural_int16_t_from_stereo,
                clip_natural_int32_t_from_stereo
            },
            {
                clip_natural_int8_t_from_stereo,
                clip_swap_int16_t_from_stereo,
                clip_swap_int32_t_from_stereo
            }
        }
    }
};

#ifdef FLOAT_MIXENG
#define CONV_NATURAL_FLOAT(x) (x)
#define CLIP_NATURAL_FLOAT(x) (x)
#else
/* macros to map [-1.f, 1.f] <-> [INT32_MIN, INT32_MAX + 1] */
static const float float_scale = (int64_t)INT32_MAX + 1;
#define CONV_NATURAL_FLOAT(x) ((x) * float_scale)

#ifdef RECIPROCAL
static const float float_scale_reciprocal = 1.f / ((int64_t)INT32_MAX + 1);
#define CLIP_NATURAL_FLOAT(x) ((x) * float_scale_reciprocal)
#else
#define CLIP_NATURAL_FLOAT(x) ((x) / float_scale)
#endif
#endif

static void conv_natural_float_to_mono(struct st_sample *dst, const void *src,
                                       int samples)
{
    float *in = (float *)src;

    while (samples--) {
        dst->r = dst->l = CONV_NATURAL_FLOAT(*in++);
        dst++;
    }
}

static void conv_natural_float_to_stereo(struct st_sample *dst, const void *src,
                                         int samples)
{
    float *in = (float *)src;

    while (samples--) {
        dst->l = CONV_NATURAL_FLOAT(*in++);
        dst->r = CONV_NATURAL_FLOAT(*in++);
        dst++;
    }
}

t_sample *mixeng_conv_float[2] = {
    conv_natural_float_to_mono,
    conv_natural_float_to_stereo,
};

static void clip_natural_float_from_mono(void *dst, const struct st_sample *src,
                                         int samples)
{
    float *out = (float *)dst;

    while (samples--) {
        *out++ = CLIP_NATURAL_FLOAT(src->l + src->r);
        src++;
    }
}

static void clip_natural_float_from_stereo(
    void *dst, const struct st_sample *src, int samples)
{
    float *out = (float *)dst;

    while (samples--) {
        *out++ = CLIP_NATURAL_FLOAT(src->l);
        *out++ = CLIP_NATURAL_FLOAT(src->r);
        src++;
    }
}

f_sample *mixeng_clip_float[2] = {
    clip_natural_float_from_mono,
    clip_natural_float_from_stereo,
};

void audio_sample_to_uint64(const void *samples, int pos,
                            uint64_t *left, uint64_t *right)
{
#ifdef FLOAT_MIXENG
    error_report(
        "Coreaudio and floating point samples are not supported by replay yet");
    abort();
#else
    const struct st_sample *sample = samples;
    sample += pos;
    *left = sample->l;
    *right = sample->r;
#endif
}

void audio_sample_from_uint64(void *samples, int pos,
                            uint64_t left, uint64_t right)
{
#ifdef FLOAT_MIXENG
    error_report(
        "Coreaudio and floating point samples are not supported by replay yet");
    abort();
#else
    struct st_sample *sample = samples;
    sample += pos;
    sample->l = left;
    sample->r = right;
#endif
}

/*
 * August 21, 1998
 * Copyright 1998 Fabrice Bellard.
 *
 * [Rewrote completely the code of Lance Norskog And Sundry
 * Contributors with a more efficient algorithm.]
 *
 * This source code is freely redistributable and may be used for
 * any purpose.  This copyright notice must be maintained.
 * Lance Norskog And Sundry Contributors are not responsible for
 * the consequences of using this software.
 */

/*
 * Sound Tools rate change effect file.
 */
/*
 * Linear Interpolation.
 *
 * The use of fractional increment allows us to use no buffer. It
 * avoid the problems at the end of the buffer we had with the old
 * method which stored a possibly big buffer of size
 * lcm(in_rate,out_rate).
 *
 * Limited to 16 bit samples and sampling frequency <= 65535 Hz. If
 * the input & output frequencies are equal, a delay of one sample is
 * introduced.  Limited to processing 32-bit count worth of samples.
 *
 * 1 << FRAC_BITS evaluating to zero in several places.  Changed with
 * an (unsigned long) cast to make it safe.  MarkMLl 2/1/99
 */

/* Private data */
struct rate {
    uint64_t opos;
    uint64_t opos_inc;
    uint32_t ipos;              /* position in the input stream (integer) */
    struct st_sample ilast;          /* last sample in the input stream */
};

/*
 * Prepare processing.
 */
void *st_rate_start (int inrate, int outrate)
{
    struct rate *rate = g_new0(struct rate, 1);

    rate->opos = 0;

    /* increment */
    rate->opos_inc = ((uint64_t) inrate << 32) / outrate;

    rate->ipos = 0;
    rate->ilast.l = 0;
    rate->ilast.r = 0;
    return rate;
}

#define NAME st_rate_flow_mix
#define OP(a, b) a += b
#include "rate_template.h"

#define NAME st_rate_flow
#define OP(a, b) a = b
#include "rate_template.h"

void st_rate_stop (void *opaque)
{
    g_free (opaque);
}

void mixeng_clear (struct st_sample *buf, int len)
{
    memset (buf, 0, len * sizeof (struct st_sample));
}

void mixeng_volume (struct st_sample *buf, int len, struct mixeng_volume *vol)
{
    if (vol->mute) {
        mixeng_clear (buf, len);
        return;
    }

    while (len--) {
#ifdef FLOAT_MIXENG
        buf->l = buf->l * vol->l;
        buf->r = buf->r * vol->r;
#else
        buf->l = (buf->l * vol->l) >> 32;
        buf->r = (buf->r * vol->r) >> 32;
#endif
        buf += 1;
    }
}
