/*
 * QEMU Mixing engine header
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
#ifndef QEMU_MIXENG_H
#define QEMU_MIXENG_H

#ifdef FLOAT_MIXENG
typedef float mixeng_real;
struct mixeng_volume { int mute; mixeng_real r; mixeng_real l; };
struct st_sample { mixeng_real l; mixeng_real r; };
#else
struct mixeng_volume { int mute; int64_t r; int64_t l; };
struct st_sample { int64_t l; int64_t r; };
#endif

typedef void (t_sample) (struct st_sample *dst, const void *src,
                         int samples, struct mixeng_volume *vol);
typedef void (f_sample) (void *dst, const struct st_sample *src, int samples);

extern t_sample *mixeng_conv[2][2][2][3];
extern f_sample *mixeng_clip[2][2][2][3];

void *st_rate_start (int inrate, int outrate);
void st_rate_flow (void *opaque, struct st_sample *ibuf, struct st_sample *obuf,
                   int *isamp, int *osamp);
void st_rate_flow_mix (void *opaque, struct st_sample *ibuf, struct st_sample *obuf,
                       int *isamp, int *osamp);
void st_rate_stop (void *opaque);
void mixeng_clear (struct st_sample *buf, int len);

#endif  /* mixeng.h */
