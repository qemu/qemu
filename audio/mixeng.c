/*
 * QEMU Mixing engine
 *
 * Copyright (c) 2004 Vassili Karpov (malc)
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
#include "vl.h"
//#define DEBUG_FP
#include "audio/mixeng.h"

#define IN_T int8_t
#define IN_MIN CHAR_MIN
#define IN_MAX CHAR_MAX
#define SIGNED
#include "mixeng_template.h"
#undef SIGNED
#undef IN_MAX
#undef IN_MIN
#undef IN_T

#define IN_T uint8_t
#define IN_MIN 0
#define IN_MAX UCHAR_MAX
#include "mixeng_template.h"
#undef IN_MAX
#undef IN_MIN
#undef IN_T

#define IN_T int16_t
#define IN_MIN SHRT_MIN
#define IN_MAX SHRT_MAX
#define SIGNED
#include "mixeng_template.h"
#undef SIGNED
#undef IN_MAX
#undef IN_MIN
#undef IN_T

#define IN_T uint16_t
#define IN_MIN 0
#define IN_MAX USHRT_MAX
#include "mixeng_template.h"
#undef IN_MAX
#undef IN_MIN
#undef IN_T

t_sample *mixeng_conv[2][2][2] = {
    {
        {
            conv_uint8_t_to_mono,
            conv_uint16_t_to_mono
        },
        {
            conv_int8_t_to_mono,
            conv_int16_t_to_mono
        }
    },
    {
        {
            conv_uint8_t_to_stereo,
            conv_uint16_t_to_stereo
        },
        {
            conv_int8_t_to_stereo,
            conv_int16_t_to_stereo
        }
    }
};

f_sample *mixeng_clip[2][2][2] = {
    {
        {
            clip_uint8_t_from_mono,
            clip_uint16_t_from_mono
        },
        {
            clip_int8_t_from_mono,
            clip_int16_t_from_mono
        }
    },
    {
        {
            clip_uint8_t_from_stereo,
            clip_uint16_t_from_stereo
        },
        {
            clip_int8_t_from_stereo,
            clip_int16_t_from_stereo
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
typedef struct ratestuff {
    uint64_t opos;
    uint64_t opos_inc;
    uint32_t ipos;              /* position in the input stream (integer) */
    st_sample_t ilast;          /* last sample in the input stream */
} *rate_t;

/*
 * Prepare processing.
 */
void *st_rate_start (int inrate, int outrate)
{
    rate_t rate = (rate_t) qemu_mallocz (sizeof (struct ratestuff));

    if (!rate) {
        exit (EXIT_FAILURE);
    }

    if (inrate == outrate) {
        // exit (EXIT_FAILURE);
    }

    if (inrate >= 65535 || outrate >= 65535) {
        // exit (EXIT_FAILURE);
    }

    rate->opos = 0;

    /* increment */
    rate->opos_inc = (inrate * ((int64_t) UINT_MAX)) / outrate;

    rate->ipos = 0;
    rate->ilast.l = 0;
    rate->ilast.r = 0;
    return rate;
}

/*
 * Processed signed long samples from ibuf to obuf.
 * Return number of samples processed.
 */
void st_rate_flow (void *opaque, st_sample_t *ibuf, st_sample_t *obuf,
                   int *isamp, int *osamp)
{
    rate_t rate = (rate_t) opaque;
    st_sample_t *istart, *iend;
    st_sample_t *ostart, *oend;
    st_sample_t ilast, icur, out;
    int64_t t;

    ilast = rate->ilast;

    istart = ibuf;
    iend = ibuf + *isamp;

    ostart = obuf;
    oend = obuf + *osamp;

    if (rate->opos_inc == 1ULL << 32) {
        int i, n = *isamp > *osamp ? *osamp : *isamp;
        for (i = 0; i < n; i++) {
            obuf[i].l += ibuf[i].r;
            obuf[i].r += ibuf[i].r;
        }
        *isamp = n;
        *osamp = n;
        return;
    }

    while (obuf < oend) {

        /* Safety catch to make sure we have input samples.  */
        if (ibuf >= iend)
            break;

        /* read as many input samples so that ipos > opos */

        while (rate->ipos <= (rate->opos >> 32)) {
            ilast = *ibuf++;
            rate->ipos++;
            /* See if we finished the input buffer yet */
            if (ibuf >= iend) goto the_end;
        }

        icur = *ibuf;

        /* interpolate */
        t = rate->opos & 0xffffffff;
        out.l = (ilast.l * (INT_MAX - t) + icur.l * t) / INT_MAX;
        out.r = (ilast.r * (INT_MAX - t) + icur.r * t) / INT_MAX;

        /* output sample & increment position */
#if 0
        *obuf++ = out;
#else
        obuf->l += out.l;
        obuf->r += out.r;
        obuf += 1;
#endif
        rate->opos += rate->opos_inc;
    }

the_end:
    *isamp = ibuf - istart;
    *osamp = obuf - ostart;
    rate->ilast = ilast;
}

void st_rate_stop (void *opaque)
{
    qemu_free (opaque);
}
