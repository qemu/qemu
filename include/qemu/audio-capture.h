/*
 * QEMU Audio subsystem
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef QEMU_AUDIO_CAPTURE_H
#define QEMU_AUDIO_CAPTURE_H

#include "audio.h"

struct capture_ops {
    void (*info) (void *opaque);
    void (*destroy) (void *opaque);
};

typedef struct CaptureState {
    void *opaque;
    struct capture_ops ops;
    QLIST_ENTRY(CaptureState) entries;
} CaptureState;

CaptureVoiceOut *audio_be_add_capture(
    AudioBackend *be,
    const struct audsettings *as,
    const struct audio_capture_ops *ops,
    void *opaque);

void audio_be_del_capture(
    AudioBackend *be,
    CaptureVoiceOut *cap,
    void *cb_opaque);

#endif /* QEMU_AUDIO_CAPTURE_H */
