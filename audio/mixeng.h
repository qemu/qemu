/*
 * QEMU Mixing engine header
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
#ifndef QEMU_MIXENG_H
#define QEMU_MIXENG_H

typedef void (t_sample) (void *dst, const void *src, int samples);
typedef void (f_sample) (void *dst, const void *src, int samples);
typedef struct { int64_t l; int64_t r; } st_sample_t;

extern t_sample *mixeng_conv[2][2][2];
extern f_sample *mixeng_clip[2][2][2];

void *st_rate_start (int inrate, int outrate);
void st_rate_flow (void *opaque, st_sample_t *ibuf, st_sample_t *obuf,
                   int *isamp, int *osamp);
void st_rate_stop (void *opaque);

#endif  /* mixeng.h */
