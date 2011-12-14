/*
 * LM4549 Audio Codec Interface
 *
 * Copyright (c) 2011
 * Written by Mathieu Sonet - www.elasticsheep.com
 *
 * This code is licensed under the GPL.
 *
 * *****************************************************************
 */

#ifndef HW_LM4549_H
#define HW_LM4549_H

#include "audio/audio.h"

typedef void (*lm4549_callback)(void *opaque);

#define LM4549_BUFFER_SIZE (512 * 2) /* 512 16-bit stereo samples */


typedef struct {
    QEMUSoundCard card;
    SWVoiceOut *voice;
    uint32_t voice_is_active;

    uint16_t regfile[128];
    lm4549_callback data_req_cb;
    void *opaque;

    uint16_t buffer[LM4549_BUFFER_SIZE];
    uint32_t buffer_level;
} lm4549_state;

extern const VMStateDescription vmstate_lm4549_state;


void lm4549_init(lm4549_state *s, lm4549_callback data_req, void *opaque);
uint32_t lm4549_read(lm4549_state *s, target_phys_addr_t offset);
void lm4549_write(lm4549_state *s, target_phys_addr_t offset, uint32_t value);
uint32_t lm4549_write_samples(lm4549_state *s, uint32_t left, uint32_t right);

#endif /* #ifndef HW_LM4549_H */
