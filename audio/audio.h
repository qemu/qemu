/*
 * QEMU Audio subsystem header
 * 
 * Copyright (c) 2003-2004 Vassili Karpov (malc)
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
#ifndef QEMU_AUDIO_H
#define QEMU_AUDIO_H

#include "mixeng.h"

typedef enum {
  AUD_FMT_U8,
  AUD_FMT_S8,
  AUD_FMT_U16,
  AUD_FMT_S16
} audfmt_e;

typedef struct SWVoice SWVoice;

SWVoice * AUD_open (SWVoice *sw, const char *name, int freq,
                    int nchannels, audfmt_e fmt);
void   AUD_init (void);
void   AUD_log (const char *cap, const char *fmt, ...)
    __attribute__ ((__format__ (__printf__, 2, 3)));;
void   AUD_close (SWVoice *sw);
int    AUD_write (SWVoice *sw, void *pcm_buf, int size);
void   AUD_adjust (SWVoice *sw, int leftover);
void   AUD_reset (SWVoice *sw);
int    AUD_get_free (SWVoice *sw);
int    AUD_get_buffer_size (SWVoice *sw);
void   AUD_run (void);
void   AUD_enable (SWVoice *sw, int on);
int    AUD_calc_elapsed (SWVoice *sw);

static inline void *advance (void *p, int incr)
{
    uint8_t *d = p;
    return (d + incr);
}

uint32_t popcount (uint32_t u);
inline uint32_t lsbindex (uint32_t u);

#define audio_MIN(a, b) ((a)>(b)?(b):(a))
#define audio_MAX(a, b) ((a)<(b)?(b):(a))

#endif  /* audio.h */
