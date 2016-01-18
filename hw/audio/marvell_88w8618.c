/*
 * Marvell 88w8618 audio emulation extracted from
 * Marvell MV88w8618 / Freecom MusicPal emulation.
 *
 * Copyright (c) 2008 Jan Kiszka
 *
 * This code is licensed under the GNU GPL v2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/hw.h"
#include "hw/i2c/i2c.h"
#include "audio/audio.h"

#define MP_AUDIO_SIZE           0x00001000

/* Audio register offsets */
#define MP_AUDIO_PLAYBACK_MODE  0x00
#define MP_AUDIO_CLOCK_DIV      0x18
#define MP_AUDIO_IRQ_STATUS     0x20
#define MP_AUDIO_IRQ_ENABLE     0x24
#define MP_AUDIO_TX_START_LO    0x28
#define MP_AUDIO_TX_THRESHOLD   0x2C
#define MP_AUDIO_TX_STATUS      0x38
#define MP_AUDIO_TX_START_HI    0x40

/* Status register and IRQ enable bits */
#define MP_AUDIO_TX_HALF        (1 << 6)
#define MP_AUDIO_TX_FULL        (1 << 7)

/* Playback mode bits */
#define MP_AUDIO_16BIT_SAMPLE   (1 << 0)
#define MP_AUDIO_PLAYBACK_EN    (1 << 7)
#define MP_AUDIO_CLOCK_24MHZ    (1 << 9)
#define MP_AUDIO_MONO           (1 << 14)

#define TYPE_MV88W8618_AUDIO "mv88w8618_audio"
#define MV88W8618_AUDIO(obj) \
    OBJECT_CHECK(mv88w8618_audio_state, (obj), TYPE_MV88W8618_AUDIO)

typedef struct mv88w8618_audio_state {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    uint32_t playback_mode;
    uint32_t status;
    uint32_t irq_enable;
    uint32_t phys_buf;
    uint32_t target_buffer;
    uint32_t threshold;
    uint32_t play_pos;
    uint32_t last_free;
    uint32_t clock_div;
    void *wm;
} mv88w8618_audio_state;

static void mv88w8618_audio_callback(void *opaque, int free_out, int free_in)
{
    mv88w8618_audio_state *s = opaque;
    int16_t *codec_buffer;
    int8_t buf[4096];
    int8_t *mem_buffer;
    int pos, block_size;

    if (!(s->playback_mode & MP_AUDIO_PLAYBACK_EN)) {
        return;
    }
    if (s->playback_mode & MP_AUDIO_16BIT_SAMPLE) {
        free_out <<= 1;
    }
    if (!(s->playback_mode & MP_AUDIO_MONO)) {
        free_out <<= 1;
    }
    block_size = s->threshold / 2;
    if (free_out - s->last_free < block_size) {
        return;
    }
    if (block_size > 4096) {
        return;
    }
    cpu_physical_memory_read(s->target_buffer + s->play_pos, buf, block_size);
    mem_buffer = buf;
    if (s->playback_mode & MP_AUDIO_16BIT_SAMPLE) {
        if (s->playback_mode & MP_AUDIO_MONO) {
            codec_buffer = wm8750_dac_buffer(s->wm, block_size >> 1);
            for (pos = 0; pos < block_size; pos += 2) {
                *codec_buffer++ = *(int16_t *)mem_buffer;
                *codec_buffer++ = *(int16_t *)mem_buffer;
                mem_buffer += 2;
            }
        } else {
            memcpy(wm8750_dac_buffer(s->wm, block_size >> 2),
                   (uint32_t *)mem_buffer, block_size);
        }
    } else {
        if (s->playback_mode & MP_AUDIO_MONO) {
            codec_buffer = wm8750_dac_buffer(s->wm, block_size);
            for (pos = 0; pos < block_size; pos++) {
                *codec_buffer++ = cpu_to_le16(256 * *mem_buffer);
                *codec_buffer++ = cpu_to_le16(256 * *mem_buffer++);
            }
        } else {
            codec_buffer = wm8750_dac_buffer(s->wm, block_size >> 1);
            for (pos = 0; pos < block_size; pos += 2) {
                *codec_buffer++ = cpu_to_le16(256 * *mem_buffer++);
                *codec_buffer++ = cpu_to_le16(256 * *mem_buffer++);
            }
        }
    }
    wm8750_dac_commit(s->wm);

    s->last_free = free_out - block_size;

    if (s->play_pos == 0) {
        s->status |= MP_AUDIO_TX_HALF;
        s->play_pos = block_size;
    } else {
        s->status |= MP_AUDIO_TX_FULL;
        s->play_pos = 0;
    }

    if (s->status & s->irq_enable) {
        qemu_irq_raise(s->irq);
    }
}

static void mv88w8618_audio_clock_update(mv88w8618_audio_state *s)
{
    int rate;

    if (s->playback_mode & MP_AUDIO_CLOCK_24MHZ) {
        rate = 24576000 / 64; /* 24.576MHz */
    } else {
        rate = 11289600 / 64; /* 11.2896MHz */
    }
    rate /= ((s->clock_div >> 8) & 0xff) + 1;

    wm8750_set_bclk_in(s->wm, rate);
}

static uint64_t mv88w8618_audio_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    mv88w8618_audio_state *s = opaque;

    switch (offset) {
    case MP_AUDIO_PLAYBACK_MODE:
        return s->playback_mode;

    case MP_AUDIO_CLOCK_DIV:
        return s->clock_div;

    case MP_AUDIO_IRQ_STATUS:
        return s->status;

    case MP_AUDIO_IRQ_ENABLE:
        return s->irq_enable;

    case MP_AUDIO_TX_STATUS:
        return s->play_pos >> 2;

    default:
        return 0;
    }
}

static void mv88w8618_audio_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned size)
{
    mv88w8618_audio_state *s = opaque;

    switch (offset) {
    case MP_AUDIO_PLAYBACK_MODE:
        if (value & MP_AUDIO_PLAYBACK_EN &&
            !(s->playback_mode & MP_AUDIO_PLAYBACK_EN)) {
            s->status = 0;
            s->last_free = 0;
            s->play_pos = 0;
        }
        s->playback_mode = value;
        mv88w8618_audio_clock_update(s);
        break;

    case MP_AUDIO_CLOCK_DIV:
        s->clock_div = value;
        s->last_free = 0;
        s->play_pos = 0;
        mv88w8618_audio_clock_update(s);
        break;

    case MP_AUDIO_IRQ_STATUS:
        s->status &= ~value;
        break;

    case MP_AUDIO_IRQ_ENABLE:
        s->irq_enable = value;
        if (s->status & s->irq_enable) {
            qemu_irq_raise(s->irq);
        }
        break;

    case MP_AUDIO_TX_START_LO:
        s->phys_buf = (s->phys_buf & 0xFFFF0000) | (value & 0xFFFF);
        s->target_buffer = s->phys_buf;
        s->play_pos = 0;
        s->last_free = 0;
        break;

    case MP_AUDIO_TX_THRESHOLD:
        s->threshold = (value + 1) * 4;
        break;

    case MP_AUDIO_TX_START_HI:
        s->phys_buf = (s->phys_buf & 0xFFFF) | (value << 16);
        s->target_buffer = s->phys_buf;
        s->play_pos = 0;
        s->last_free = 0;
        break;
    }
}

static void mv88w8618_audio_reset(DeviceState *d)
{
    mv88w8618_audio_state *s = MV88W8618_AUDIO(d);

    s->playback_mode = 0;
    s->status = 0;
    s->irq_enable = 0;
    s->clock_div = 0;
    s->threshold = 0;
    s->phys_buf = 0;
}

static const MemoryRegionOps mv88w8618_audio_ops = {
    .read = mv88w8618_audio_read,
    .write = mv88w8618_audio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int mv88w8618_audio_init(SysBusDevice *dev)
{
    mv88w8618_audio_state *s = MV88W8618_AUDIO(dev);

    sysbus_init_irq(dev, &s->irq);

    wm8750_data_req_set(s->wm, mv88w8618_audio_callback, s);

    memory_region_init_io(&s->iomem, OBJECT(s), &mv88w8618_audio_ops, s,
                          "audio", MP_AUDIO_SIZE);
    sysbus_init_mmio(dev, &s->iomem);

    return 0;
}

static const VMStateDescription mv88w8618_audio_vmsd = {
    .name = "mv88w8618_audio",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(playback_mode, mv88w8618_audio_state),
        VMSTATE_UINT32(status, mv88w8618_audio_state),
        VMSTATE_UINT32(irq_enable, mv88w8618_audio_state),
        VMSTATE_UINT32(phys_buf, mv88w8618_audio_state),
        VMSTATE_UINT32(target_buffer, mv88w8618_audio_state),
        VMSTATE_UINT32(threshold, mv88w8618_audio_state),
        VMSTATE_UINT32(play_pos, mv88w8618_audio_state),
        VMSTATE_UINT32(last_free, mv88w8618_audio_state),
        VMSTATE_UINT32(clock_div, mv88w8618_audio_state),
        VMSTATE_END_OF_LIST()
    }
};

static Property mv88w8618_audio_properties[] = {
    DEFINE_PROP_PTR("wm8750", mv88w8618_audio_state, wm),
    {/* end of list */},
};

static void mv88w8618_audio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = mv88w8618_audio_init;
    dc->reset = mv88w8618_audio_reset;
    dc->vmsd = &mv88w8618_audio_vmsd;
    dc->props = mv88w8618_audio_properties;
    /* Reason: pointer property "wm8750" */
    dc->cannot_instantiate_with_device_add_yet = true;
}

static const TypeInfo mv88w8618_audio_info = {
    .name          = TYPE_MV88W8618_AUDIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(mv88w8618_audio_state),
    .class_init    = mv88w8618_audio_class_init,
};

static void mv88w8618_register_types(void)
{
    type_register_static(&mv88w8618_audio_info);
}

type_init(mv88w8618_register_types)
