/*
 * QEMU Mixing engine
 *
 * Copyright (c) 2004-2005 Vassili Karpov (malc)
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
 * Tusen tack till Mike Nordell
 * dec++'ified by Dscho
 */

#ifndef SIGNED
#define HALF (IN_MAX >> 1)
#endif

#define ET glue (ENDIAN_CONVERSION, glue (_, IN_T))

#ifdef FLOAT_MIXENG
static mixeng_real inline glue (conv_, ET) (IN_T v)
{
    IN_T nv = ENDIAN_CONVERT (v);

#ifdef RECIPROCAL
#ifdef SIGNED
    return nv * (1.f / (mixeng_real) (IN_MAX - IN_MIN));
#else
    return (nv - HALF) * (1.f / (mixeng_real) IN_MAX);
#endif
#else  /* !RECIPROCAL */
#ifdef SIGNED
    return nv / (mixeng_real) ((mixeng_real) IN_MAX - IN_MIN);
#else
    return (nv - HALF) / (mixeng_real) IN_MAX;
#endif
#endif
}

static IN_T inline glue (clip_, ET) (mixeng_real v)
{
    if (v >= 0.5) {
        return IN_MAX;
    }
    else if (v < -0.5) {
        return IN_MIN;
    }

#ifdef SIGNED
    return ENDIAN_CONVERT ((IN_T) (v * ((mixeng_real) IN_MAX - IN_MIN)));
#else
    return ENDIAN_CONVERT ((IN_T) ((v * IN_MAX) + HALF));
#endif
}

#else  /* !FLOAT_MIXENG */

static inline int64_t glue (conv_, ET) (IN_T v)
{
    IN_T nv = ENDIAN_CONVERT (v);
#ifdef SIGNED
    return ((int64_t) nv) << (32 - SHIFT);
#else
    return ((int64_t) nv - HALF) << (32 - SHIFT);
#endif
}

static inline IN_T glue (clip_, ET) (int64_t v)
{
    if (v >= 0x7f000000) {
        return IN_MAX;
    }
    else if (v < -2147483648LL) {
        return IN_MIN;
    }

#ifdef SIGNED
    return ENDIAN_CONVERT ((IN_T) (v >> (32 - SHIFT)));
#else
    return ENDIAN_CONVERT ((IN_T) ((v >> (32 - SHIFT)) + HALF));
#endif
}
#endif

static void glue (glue (conv_, ET), _to_stereo)
    (struct st_sample *dst, const void *src, int samples)
{
    struct st_sample *out = dst;
    IN_T *in = (IN_T *) src;

    while (samples--) {
        out->l = glue (conv_, ET) (*in++);
        out->r = glue (conv_, ET) (*in++);
        out += 1;
    }
}

static void glue (glue (conv_, ET), _to_mono)
    (struct st_sample *dst, const void *src, int samples)
{
    struct st_sample *out = dst;
    IN_T *in = (IN_T *) src;

    while (samples--) {
        out->l = glue (conv_, ET) (in[0]);
        out->r = out->l;
        out += 1;
        in += 1;
    }
}

static void glue (glue (clip_, ET), _from_stereo)
    (void *dst, const struct st_sample *src, int samples)
{
    const struct st_sample *in = src;
    IN_T *out = (IN_T *) dst;
    while (samples--) {
        *out++ = glue (clip_, ET) (in->l);
        *out++ = glue (clip_, ET) (in->r);
        in += 1;
    }
}

static void glue (glue (clip_, ET), _from_mono)
    (void *dst, const struct st_sample *src, int samples)
{
    const struct st_sample *in = src;
    IN_T *out = (IN_T *) dst;
    while (samples--) {
        *out++ = glue (clip_, ET) (in->l + in->r);
        in += 1;
    }
}

#undef ET
#undef HALF
