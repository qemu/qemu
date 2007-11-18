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
#include "qemu-common.h"
#include "audio.h"

#define AUDIO_CAP "mixeng"
#include "audio_int.h"

#define NOVOL

/* 8 bit */
#define ENDIAN_CONVERSION natural
#define ENDIAN_CONVERT(v) (v)

/* Signed 8 bit */
#define IN_T int8_t
#define IN_MIN SCHAR_MIN
#define IN_MAX SCHAR_MAX
#define SIGNED
#define SHIFT 8
#include "mixeng_template.h"
#undef SIGNED
#undef IN_MAX
#undef IN_MIN
#undef IN_T
#undef SHIFT

/* Unsigned 8 bit */
#define IN_T uint8_t
#define IN_MIN 0
#define IN_MAX UCHAR_MAX
#define SHIFT 8
#include "mixeng_template.h"
#undef IN_MAX
#undef IN_MIN
#undef IN_T
#undef SHIFT

#undef ENDIAN_CONVERT
#undef ENDIAN_CONVERSION

/* Signed 16 bit */
#define IN_T int16_t
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
#undef IN_T
#undef SHIFT

/* Unsigned 16 bit */
#define IN_T uint16_t
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
#undef IN_T
#undef SHIFT

/* Signed 32 bit */
#define IN_T int32_t
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
#undef IN_T
#undef SHIFT

/* Unsigned 16 bit */
#define IN_T uint32_t
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
#undef IN_T
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

/*
 * August 21, 1998
 * Copyright 1998 Fabrice Bellard.
 *
 * [Rewrote completly the code of Lance Norskog And Sundry
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
    st_sample_t ilast;          /* last sample in the input stream */
};

/*
 * Prepare processing.
 */
void *st_rate_start (int inrate, int outrate)
{
    struct rate *rate = audio_calloc (AUDIO_FUNC, 1, sizeof (*rate));

    if (!rate) {
        dolog ("Could not allocate resampler (%zu bytes)\n", sizeof (*rate));
        return NULL;
    }

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
    qemu_free (opaque);
}

void mixeng_clear (st_sample_t *buf, int len)
{
    memset (buf, 0, len * sizeof (st_sample_t));
}
