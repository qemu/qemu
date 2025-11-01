/*
 * QEMU Audio subsystem
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef QEMU_AUDIO_CAPTURE_H
#define QEMU_AUDIO_CAPTURE_H

#include "audio.h"

typedef struct CaptureVoiceOut CaptureVoiceOut;

typedef enum {
    AUD_CNOTIFY_ENABLE,
    AUD_CNOTIFY_DISABLE
} audcnotification_e;

struct audio_capture_ops {
    void (*notify) (void *opaque, audcnotification_e cmd);
    void (*capture) (void *opaque, const void *buf, int size);
    void (*destroy) (void *opaque);
};

struct capture_ops {
    void (*info) (void *opaque);
    void (*destroy) (void *opaque);
};

typedef struct CaptureState {
    void *opaque;
    struct capture_ops ops;
    QLIST_ENTRY(CaptureState) entries;
} CaptureState;

CaptureVoiceOut *AUD_add_capture(
    AudioBackend *be,
    struct audsettings *as,
    struct audio_capture_ops *ops,
    void *opaque
    );
void AUD_del_capture (CaptureVoiceOut *cap, void *cb_opaque);

#endif /* QEMU_AUDIO_CAPTURE_H */
