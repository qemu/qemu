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
#ifndef QEMU_AUDIO_INT_H
#define QEMU_AUDIO_INT_H

#include "vl.h"

struct pcm_ops;

typedef struct HWVoice {
    int active;
    int enabled;
    int pending_disable;
    int valid;
    int freq;

    f_sample *clip;
    audfmt_e fmt;
    int nchannels;

    int align;
    int shift;

    int rpos;
    int bufsize;

    int bytes_per_second;
    st_sample_t *mix_buf;

    int samples;
    int64_t old_ticks;
    int nb_voices;
    struct SWVoice **pvoice;
    struct pcm_ops *pcm_ops;
} HWVoice;

extern struct pcm_ops no_pcm_ops;
extern struct audio_output_driver no_output_driver;

extern struct pcm_ops oss_pcm_ops;
extern struct audio_output_driver oss_output_driver;

extern struct pcm_ops sdl_pcm_ops;
extern struct audio_output_driver sdl_output_driver;

extern struct pcm_ops wav_pcm_ops;
extern struct audio_output_driver wav_output_driver;

extern struct pcm_ops fmod_pcm_ops;
extern struct audio_output_driver fmod_output_driver;

struct audio_output_driver {
    const char *name;
    void *(*init) (void);
    void (*fini) (void *);
    struct pcm_ops *pcm_ops;
    int can_be_default;
    int max_voices;
    int voice_size;
};

typedef struct AudioState {
    int fixed_format;
    int fixed_freq;
    int fixed_channels;
    int fixed_fmt;
    int nb_hw_voices;
    int voice_size;
    int64_t ticks_threshold;
    int freq_threshold;
    void *opaque;
    struct audio_output_driver *drv;
} AudioState;
extern AudioState audio_state;

struct SWVoice {
    int freq;
    audfmt_e fmt;
    int nchannels;

    int shift;
    int align;

    t_sample *conv;

    int left;
    int pos;
    int bytes_per_second;
    int64_t ratio;
    st_sample_t *buf;
    void *rate;

    int wpos;
    int live;
    int active;
    int64_t old_ticks;
    HWVoice *hw;
    char *name;
};

struct pcm_ops {
    int  (*init)  (HWVoice *hw, int freq, int nchannels, audfmt_e fmt);
    void (*fini)  (HWVoice *hw);
    void (*run)   (HWVoice *hw);
    int  (*write) (SWVoice *sw, void *buf, int size);
    int  (*ctl)   (HWVoice *hw, int cmd, ...);
};

void      pcm_sw_free_resources (SWVoice *sw);
int       pcm_sw_alloc_resources (SWVoice *sw);
void      pcm_sw_fini (SWVoice *sw);
int       pcm_sw_init (SWVoice *sw, HWVoice *hw, int freq,
                       int nchannels, audfmt_e fmt);

void      pcm_hw_clear (HWVoice *hw, void *buf, int len);
HWVoice * pcm_hw_find_any (HWVoice *hw);
HWVoice * pcm_hw_find_any_active (HWVoice *hw);
HWVoice * pcm_hw_find_any_passive (HWVoice *hw);
HWVoice * pcm_hw_find_specific (HWVoice *hw, int freq,
                                int nchannels, audfmt_e fmt);
HWVoice * pcm_hw_add (int freq, int nchannels, audfmt_e fmt);
int       pcm_hw_add_sw (HWVoice *hw, SWVoice *sw);
int       pcm_hw_del_sw (HWVoice *hw, SWVoice *sw);
SWVoice * pcm_create_voice_pair (int freq, int nchannels, audfmt_e fmt);

void      pcm_hw_free_resources (HWVoice *hw);
int       pcm_hw_alloc_resources (HWVoice *hw);
void      pcm_hw_fini (HWVoice *hw);
void      pcm_hw_gc (HWVoice *hw);
int       pcm_hw_get_live (HWVoice *hw);
int       pcm_hw_get_live2 (HWVoice *hw, int *nb_active);
void      pcm_hw_dec_live (HWVoice *hw, int decr);
int       pcm_hw_write (SWVoice *sw, void *buf, int len);

int         audio_get_conf_int (const char *key, int defval);
const char *audio_get_conf_str (const char *key, const char *defval);

struct audio_output_driver;

#define VOICE_ENABLE 1
#define VOICE_DISABLE 2

#endif /* audio_int.h */
