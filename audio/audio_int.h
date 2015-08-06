/*
 * QEMU Audio subsystem header
 *
 * Copyright (c) 2003-2005 Vassili Karpov (malc)
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

#ifdef CONFIG_COREAUDIO
#define FLOAT_MIXENG
/* #define RECIPROCAL */
#endif
#include "mixeng.h"

struct audio_pcm_ops;

typedef enum {
    AUD_OPT_INT,
    AUD_OPT_FMT,
    AUD_OPT_STR,
    AUD_OPT_BOOL
} audio_option_tag_e;

struct audio_option {
    const char *name;
    audio_option_tag_e tag;
    void *valp;
    const char *descr;
    int *overriddenp;
    int overridden;
};

struct audio_callback {
    void *opaque;
    audio_callback_fn fn;
};

struct audio_pcm_info {
    int bits;
    int sign;
    int freq;
    int nchannels;
    int align;
    int shift;
    int bytes_per_second;
    int swap_endianness;
};

typedef struct SWVoiceCap SWVoiceCap;

typedef struct HWVoiceOut {
    int enabled;
    int poll_mode;
    int pending_disable;
    struct audio_pcm_info info;

    f_sample *clip;

    int rpos;
    uint64_t ts_helper;

    struct st_sample *mix_buf;

    int samples;
    QLIST_HEAD (sw_out_listhead, SWVoiceOut) sw_head;
    QLIST_HEAD (sw_cap_listhead, SWVoiceCap) cap_head;
    int ctl_caps;
    struct audio_pcm_ops *pcm_ops;
    QLIST_ENTRY (HWVoiceOut) entries;
} HWVoiceOut;

typedef struct HWVoiceIn {
    int enabled;
    int poll_mode;
    struct audio_pcm_info info;

    t_sample *conv;

    int wpos;
    int total_samples_captured;
    uint64_t ts_helper;

    struct st_sample *conv_buf;

    int samples;
    QLIST_HEAD (sw_in_listhead, SWVoiceIn) sw_head;
    int ctl_caps;
    struct audio_pcm_ops *pcm_ops;
    QLIST_ENTRY (HWVoiceIn) entries;
} HWVoiceIn;

struct SWVoiceOut {
    QEMUSoundCard *card;
    struct audio_pcm_info info;
    t_sample *conv;
    int64_t ratio;
    struct st_sample *buf;
    void *rate;
    int total_hw_samples_mixed;
    int active;
    int empty;
    HWVoiceOut *hw;
    char *name;
    struct mixeng_volume vol;
    struct audio_callback callback;
    QLIST_ENTRY (SWVoiceOut) entries;
};

struct SWVoiceIn {
    QEMUSoundCard *card;
    int active;
    struct audio_pcm_info info;
    int64_t ratio;
    void *rate;
    int total_hw_samples_acquired;
    struct st_sample *buf;
    f_sample *clip;
    HWVoiceIn *hw;
    char *name;
    struct mixeng_volume vol;
    struct audio_callback callback;
    QLIST_ENTRY (SWVoiceIn) entries;
};

struct audio_driver {
    const char *name;
    const char *descr;
    struct audio_option *options;
    void *(*init) (void);
    void (*fini) (void *);
    struct audio_pcm_ops *pcm_ops;
    int can_be_default;
    int max_voices_out;
    int max_voices_in;
    int voice_size_out;
    int voice_size_in;
    int ctl_caps;
};

struct audio_pcm_ops {
    int  (*init_out)(HWVoiceOut *hw, struct audsettings *as, void *drv_opaque);
    void (*fini_out)(HWVoiceOut *hw);
    int  (*run_out) (HWVoiceOut *hw, int live);
    int  (*write)   (SWVoiceOut *sw, void *buf, int size);
    int  (*ctl_out) (HWVoiceOut *hw, int cmd, ...);

    int  (*init_in) (HWVoiceIn *hw, struct audsettings *as, void *drv_opaque);
    void (*fini_in) (HWVoiceIn *hw);
    int  (*run_in)  (HWVoiceIn *hw);
    int  (*read)    (SWVoiceIn *sw, void *buf, int size);
    int  (*ctl_in)  (HWVoiceIn *hw, int cmd, ...);
};

struct capture_callback {
    struct audio_capture_ops ops;
    void *opaque;
    QLIST_ENTRY (capture_callback) entries;
};

struct CaptureVoiceOut {
    HWVoiceOut hw;
    void *buf;
    QLIST_HEAD (cb_listhead, capture_callback) cb_head;
    QLIST_ENTRY (CaptureVoiceOut) entries;
};

struct SWVoiceCap {
    SWVoiceOut sw;
    CaptureVoiceOut *cap;
    QLIST_ENTRY (SWVoiceCap) entries;
};

struct AudioState {
    struct audio_driver *drv;
    void *drv_opaque;

    QEMUTimer *ts;
    QLIST_HEAD (card_listhead, QEMUSoundCard) card_head;
    QLIST_HEAD (hw_in_listhead, HWVoiceIn) hw_head_in;
    QLIST_HEAD (hw_out_listhead, HWVoiceOut) hw_head_out;
    QLIST_HEAD (cap_listhead, CaptureVoiceOut) cap_head;
    int nb_hw_voices_out;
    int nb_hw_voices_in;
    int vm_running;
};

extern struct audio_driver no_audio_driver;
extern struct audio_driver oss_audio_driver;
extern struct audio_driver sdl_audio_driver;
extern struct audio_driver wav_audio_driver;
extern struct audio_driver alsa_audio_driver;
extern struct audio_driver coreaudio_audio_driver;
extern struct audio_driver dsound_audio_driver;
extern struct audio_driver pa_audio_driver;
extern struct audio_driver spice_audio_driver;
extern const struct mixeng_volume nominal_volume;

void audio_pcm_init_info (struct audio_pcm_info *info, struct audsettings *as);
void audio_pcm_info_clear_buf (struct audio_pcm_info *info, void *buf, int len);

int  audio_pcm_sw_write (SWVoiceOut *sw, void *buf, int len);
int  audio_pcm_hw_get_live_in (HWVoiceIn *hw);

int  audio_pcm_sw_read (SWVoiceIn *sw, void *buf, int len);

int audio_pcm_hw_clip_out (HWVoiceOut *hw, void *pcm_buf,
                           int live, int pending);

int audio_bug (const char *funcname, int cond);
void *audio_calloc (const char *funcname, int nmemb, size_t size);

void audio_run (const char *msg);

#define VOICE_ENABLE 1
#define VOICE_DISABLE 2
#define VOICE_VOLUME 3

#define VOICE_VOLUME_CAP (1 << VOICE_VOLUME)

static inline int audio_ring_dist (int dst, int src, int len)
{
    return (dst >= src) ? (dst - src) : (len - src + dst);
}

#define dolog(fmt, ...) AUD_log(AUDIO_CAP, fmt, ## __VA_ARGS__)

#ifdef DEBUG
#define ldebug(fmt, ...) AUD_log(AUDIO_CAP, fmt, ## __VA_ARGS__)
#else
#define ldebug(fmt, ...) (void)0
#endif

#define AUDIO_STRINGIFY_(n) #n
#define AUDIO_STRINGIFY(n) AUDIO_STRINGIFY_(n)

#if defined _MSC_VER || defined __GNUC__
#define AUDIO_FUNC __FUNCTION__
#else
#define AUDIO_FUNC __FILE__ ":" AUDIO_STRINGIFY (__LINE__)
#endif

#endif /* audio_int.h */
