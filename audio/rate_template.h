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

/*
 * Processed signed long samples from ibuf to obuf.
 * Return number of samples processed.
 */
void NAME (void *opaque, struct st_sample *ibuf, struct st_sample *obuf,
           size_t *isamp, size_t *osamp)
{
    struct rate *rate = opaque;
    struct st_sample *istart, *iend;
    struct st_sample *ostart, *oend;
    struct st_sample ilast, icur, out;
#ifdef FLOAT_MIXENG
    mixeng_real t;
#else
    int64_t t;
#endif

    istart = ibuf;
    iend = ibuf + *isamp;

    ostart = obuf;
    oend = obuf + *osamp;

    if (rate->opos_inc == (1ULL + UINT_MAX)) {
        int i, n = *isamp > *osamp ? *osamp : *isamp;
        for (i = 0; i < n; i++) {
            OP (obuf[i].l, ibuf[i].l);
            OP (obuf[i].r, ibuf[i].r);
        }
        *isamp = n;
        *osamp = n;
        return;
    }

    /* without input samples, there's nothing to do */
    if (ibuf >= iend) {
        *osamp = 0;
        return;
    }

    ilast = rate->ilast;

    while (true) {

        /* read as many input samples so that ipos > opos */
        while (rate->ipos <= (rate->opos >> 32)) {
            ilast = *ibuf++;
            rate->ipos++;

            /* See if we finished the input buffer yet */
            if (ibuf >= iend) {
                goto the_end;
            }
        }

        /* make sure that the next output sample can be written */
        if (obuf >= oend) {
            break;
        }

        icur = *ibuf;

        /* wrap ipos and opos around long before they overflow */
        if (rate->ipos >= 0x10001) {
            rate->ipos = 1;
            rate->opos &= 0xffffffff;
        }

        /* interpolate */
#ifdef FLOAT_MIXENG
#ifdef RECIPROCAL
        t = (rate->opos & UINT_MAX) * (1.f / UINT_MAX);
#else
        t = (rate->opos & UINT_MAX) / (mixeng_real) UINT_MAX;
#endif
        out.l = (ilast.l * (1.0 - t)) + icur.l * t;
        out.r = (ilast.r * (1.0 - t)) + icur.r * t;
#else
        t = rate->opos & 0xffffffff;
        out.l = (ilast.l * ((int64_t) UINT_MAX - t) + icur.l * t) >> 32;
        out.r = (ilast.r * ((int64_t) UINT_MAX - t) + icur.r * t) >> 32;
#endif

        /* output sample & increment position */
        OP (obuf->l, out.l);
        OP (obuf->r, out.r);
        obuf += 1;
        rate->opos += rate->opos_inc;
    }

the_end:
    *isamp = ibuf - istart;
    *osamp = obuf - ostart;
    rate->ilast = ilast;
}

#undef NAME
#undef OP
