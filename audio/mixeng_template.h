/*
 * QEMU Mixing engine
 * 
 * Copyright (c) 2004 Vassili Karpov (malc)
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

#ifdef SIGNED
#define HALFT IN_MAX
#define HALF IN_MAX
#else
#define HALFT ((IN_MAX)>>1)
#define HALF HALFT
#endif

static int64_t inline glue(conv_,IN_T) (IN_T v)
{
#ifdef SIGNED
    return (INT_MAX*(int64_t)v)/HALF;
#else
    return (INT_MAX*((int64_t)v-HALFT))/HALF;
#endif
}

static IN_T inline glue(clip_,IN_T) (int64_t v)
{
    if (v >= INT_MAX)
        return IN_MAX;
    else if (v < -INT_MAX)
        return IN_MIN;

#ifdef SIGNED
    return (IN_T) (v*HALF/INT_MAX);
#else
    return (IN_T) (v+INT_MAX/2)*HALF/INT_MAX;
#endif
}

static void glue(glue(conv_,IN_T),_to_stereo) (void *dst, const void *src,
                                               int samples)
{
    st_sample_t *out = (st_sample_t *) dst;
    IN_T *in = (IN_T *) src;
    while (samples--) {
        out->l = glue(conv_,IN_T) (*in++);
        out->r = glue(conv_,IN_T) (*in++);
        out += 1;
    }
}

static void glue(glue(conv_,IN_T),_to_mono) (void *dst, const void *src,
                                             int samples)
{
    st_sample_t *out = (st_sample_t *) dst;
    IN_T *in = (IN_T *) src;
    while (samples--) {
        out->l = glue(conv_,IN_T) (in[0]);
        out->r = out->l;
        out += 1;
        in += 1;
    }
}

static void glue(glue(clip_,IN_T),_from_stereo) (void *dst, const void *src,
                                                 int samples)
{
    st_sample_t *in = (st_sample_t *) src;
    IN_T *out = (IN_T *) dst;
    while (samples--) {
        *out++ = glue(clip_,IN_T) (in->l);
        *out++ = glue(clip_,IN_T) (in->r);
        in += 1;
    }
}

static void glue(glue(clip_,IN_T),_from_mono) (void *dst, const void *src,
                                               int samples)
{
    st_sample_t *in = (st_sample_t *) src;
    IN_T *out = (IN_T *) dst;
    while (samples--) {
        *out++ = glue(clip_,IN_T) (in->l + in->r);
        in += 1;
    }
}

#undef HALF
#undef HALFT

