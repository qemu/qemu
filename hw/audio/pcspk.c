/*
 * QEMU PC speaker emulation
 *
 * Copyright (c) 2006 Joachim Henke
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

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/isa/isa.h"
#include "hw/audio/audio.h"
#include "audio/audio.h"
#include "qemu/timer.h"
#include "hw/timer/i8254.h"
#include "hw/audio/pcspk.h"

#define PCSPK_BUF_LEN 1792
#define PCSPK_SAMPLE_RATE 32000
#define PCSPK_MAX_FREQ (PCSPK_SAMPLE_RATE >> 1)
#define PCSPK_MIN_COUNT DIV_ROUND_UP(PIT_FREQ, PCSPK_MAX_FREQ)

#define PC_SPEAKER(obj) OBJECT_CHECK(PCSpkState, (obj), TYPE_PC_SPEAKER)

typedef struct {
    ISADevice parent_obj;

    MemoryRegion ioport;
    uint32_t iobase;
    uint8_t sample_buf[PCSPK_BUF_LEN];
    QEMUSoundCard card;
    SWVoiceOut *voice;
    void *pit;
    unsigned int pit_count;
    unsigned int samples;
    unsigned int play_pos;
    int data_on;
    int dummy_refresh_clock;
} PCSpkState;

static const char *s_spk = "pcspk";
static PCSpkState *pcspk_state;

static inline void generate_samples(PCSpkState *s)
{
    unsigned int i;

    if (s->pit_count) {
        const uint32_t m = PCSPK_SAMPLE_RATE * s->pit_count;
        const uint32_t n = ((uint64_t)PIT_FREQ << 32) / m;

        /* multiple of wavelength for gapless looping */
        s->samples = (PCSPK_BUF_LEN * PIT_FREQ / m * m / (PIT_FREQ >> 1) + 1) >> 1;
        for (i = 0; i < s->samples; ++i)
            s->sample_buf[i] = (64 & (n * i >> 25)) - 32;
    } else {
        s->samples = PCSPK_BUF_LEN;
        for (i = 0; i < PCSPK_BUF_LEN; ++i)
            s->sample_buf[i] = 128; /* silence */
    }
}

static void pcspk_callback(void *opaque, int free)
{
    PCSpkState *s = opaque;
    PITChannelInfo ch;
    unsigned int n;

    pit_get_channel_info(s->pit, 2, &ch);

    if (ch.mode != 3) {
        return;
    }

    n = ch.initial_count;
    /* avoid frequencies that are not reproducible with sample rate */
    if (n < PCSPK_MIN_COUNT)
        n = 0;

    if (s->pit_count != n) {
        s->pit_count = n;
        s->play_pos = 0;
        generate_samples(s);
    }

    while (free > 0) {
        n = audio_MIN(s->samples - s->play_pos, (unsigned int)free);
        n = AUD_write(s->voice, &s->sample_buf[s->play_pos], n);
        if (!n)
            break;
        s->play_pos = (s->play_pos + n) % s->samples;
        free -= n;
    }
}

static int pcspk_audio_init(ISABus *bus)
{
    PCSpkState *s = pcspk_state;
    struct audsettings as = {PCSPK_SAMPLE_RATE, 1, AUD_FMT_U8, 0};

    AUD_register_card(s_spk, &s->card);

    s->voice = AUD_open_out(&s->card, s->voice, s_spk, s, pcspk_callback, &as);
    if (!s->voice) {
        AUD_log(s_spk, "Could not open voice\n");
        return -1;
    }

    return 0;
}

static uint64_t pcspk_io_read(void *opaque, hwaddr addr,
                              unsigned size)
{
    PCSpkState *s = opaque;
    PITChannelInfo ch;

    pit_get_channel_info(s->pit, 2, &ch);

    s->dummy_refresh_clock ^= (1 << 4);

    return ch.gate | (s->data_on << 1) | s->dummy_refresh_clock |
       (ch.out << 5);
}

static void pcspk_io_write(void *opaque, hwaddr addr, uint64_t val,
                           unsigned size)
{
    PCSpkState *s = opaque;
    const int gate = val & 1;

    s->data_on = (val >> 1) & 1;
    pit_set_gate(s->pit, 2, gate);
    if (s->voice) {
        if (gate) /* restart */
            s->play_pos = 0;
        AUD_set_active_out(s->voice, gate & s->data_on);
    }
}

static const MemoryRegionOps pcspk_io_ops = {
    .read = pcspk_io_read,
    .write = pcspk_io_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void pcspk_initfn(Object *obj)
{
    PCSpkState *s = PC_SPEAKER(obj);

    memory_region_init_io(&s->ioport, OBJECT(s), &pcspk_io_ops, s, "pcspk", 1);
}

static void pcspk_realizefn(DeviceState *dev, Error **errp)
{
    ISADevice *isadev = ISA_DEVICE(dev);
    PCSpkState *s = PC_SPEAKER(dev);

    isa_register_ioport(isadev, &s->ioport, s->iobase);

    pcspk_state = s;
}

static Property pcspk_properties[] = {
    DEFINE_PROP_UINT32("iobase", PCSpkState, iobase,  -1),
    DEFINE_PROP_PTR("pit", PCSpkState, pit),
    DEFINE_PROP_END_OF_LIST(),
};

static void pcspk_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pcspk_realizefn;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    dc->props = pcspk_properties;
    /* Reason: pointer property "pit", realize sets global pcspk_state */
    dc->cannot_instantiate_with_device_add_yet = true;
}

static const TypeInfo pcspk_info = {
    .name           = TYPE_PC_SPEAKER,
    .parent         = TYPE_ISA_DEVICE,
    .instance_size  = sizeof(PCSpkState),
    .instance_init  = pcspk_initfn,
    .class_init     = pcspk_class_initfn,
};

static void pcspk_register(void)
{
    type_register_static(&pcspk_info);
    isa_register_soundhw("pcspk", "PC speaker", pcspk_audio_init);
}
type_init(pcspk_register)
