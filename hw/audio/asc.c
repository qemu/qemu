/*
 * QEMU Apple Sound Chip emulation
 *
 * Apple Sound Chip (ASC) 344S0063
 * Enhanced Apple Sound Chip (EASC) 343S1063
 *
 * Copyright (c) 2012-2018 Laurent Vivier <laurent@vivier.eu>
 * Copyright (c) 2022 Mark Cave-Ayland <mark.cave-ayland@ilande.co.uk>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "audio/audio.h"
#include "hw/audio/asc.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "trace.h"

/*
 * Linux doesn't provide information about ASC, see arch/m68k/mac/macboing.c
 * and arch/m68k/include/asm/mac_asc.h
 *
 * best information is coming from MAME:
 *   https://github.com/mamedev/mame/blob/master/src/devices/sound/asc.h
 *   https://github.com/mamedev/mame/blob/master/src/devices/sound/asc.cpp
 *   Emulation by R. Belmont
 * or MESS:
 *   http://mess.redump.net/mess/driver_info/easc
 *
 *     0x800: VERSION
 *     0x801: MODE
 *            1=FIFO mode,
 *            2=wavetable mode
 *     0x802: CONTROL
 *            bit 0=analog or PWM output,
 *                1=stereo/mono,
 *                7=processing time exceeded
 *     0x803: FIFO MODE
 *            bit 7=clear FIFO,
 *            bit 1="non-ROM companding",
 *            bit 0="ROM companding")
 *     0x804: FIFO IRQ STATUS
 *            bit 0=ch A 1/2 full,
 *                1=ch A full,
 *                2=ch B 1/2 full,
 *                3=ch B full)
 *     0x805: WAVETABLE CONTROL
 *            bits 0-3 wavetables 0-3 start
 *     0x806: VOLUME
 *            bits 2-4 = 3 bit internal ASC volume,
 *            bits 5-7 = volume control sent to Sony sound chip
 *     0x807: CLOCK RATE
 *            0 = Mac 22257 Hz,
 *            1 = undefined,
 *            2 = 22050 Hz,
 *            3 = 44100 Hz
 *     0x80a: PLAY REC A
 *     0x80f: TEST
 *            bits 6-7 = digital test,
 *            bits 4-5 = analog test
 *     0x810: WAVETABLE 0 PHASE
 *            big-endian 9.15 fixed-point, only 24 bits valid
 *     0x814: WAVETABLE 0 INCREMENT
 *            big-endian 9.15 fixed-point, only 24 bits valid
 *     0x818: WAVETABLE 1 PHASE
 *     0x81C: WAVETABLE 1 INCREMENT
 *     0x820: WAVETABLE 2 PHASE
 *     0x824: WAVETABLE 2 INCREMENT
 *     0x828: WAVETABLE 3 PHASE
 *     0x82C: WAVETABLE 3 INCREMENT
 *     0x830: UNKNOWN START
 *            NetBSD writes Wavetable data here (are there more
 *            wavetables/channels than we know about?)
 *     0x857: UNKNOWN END
 */

#define ASC_SIZE           0x2000

enum {
    ASC_VERSION     = 0x00,
    ASC_MODE        = 0x01,
    ASC_CONTROL     = 0x02,
    ASC_FIFOMODE    = 0x03,
    ASC_FIFOIRQ     = 0x04,
    ASC_WAVECTRL    = 0x05,
    ASC_VOLUME      = 0x06,
    ASC_CLOCK       = 0x07,
    ASC_PLAYRECA    = 0x0a,
    ASC_TEST        = 0x0f,
    ASC_WAVETABLE   = 0x10
};

#define ASC_FIFO_STATUS_HALF_FULL      1
#define ASC_FIFO_STATUS_FULL_EMPTY     2

#define ASC_EXTREGS_FIFOCTRL           0x8
#define ASC_EXTREGS_INTCTRL            0x9
#define ASC_EXTREGS_CDXA_DECOMP_FILT   0x10

#define ASC_FIFO_CYCLE_TIME            ((NANOSECONDS_PER_SECOND / ASC_FREQ) * \
                                        0x400)

static void asc_raise_irq(ASCState *s)
{
    qemu_set_irq(s->irq, 1);
}

static void asc_lower_irq(ASCState *s)
{
    qemu_set_irq(s->irq, 0);
}

static uint8_t asc_fifo_get(ASCFIFOState *fs)
{
    ASCState *s = container_of(fs, ASCState, fifos[fs->index]);
    bool fifo_half_irq_enabled = fs->extregs[ASC_EXTREGS_INTCTRL] & 1;
    uint8_t val;

    assert(fs->cnt);

    val = fs->fifo[fs->rptr];
    trace_asc_fifo_get('A' + fs->index, fs->rptr, fs->cnt, val);

    fs->rptr++;
    fs->rptr &= 0x3ff;
    fs->cnt--;

    if (fs->cnt <= 0x1ff) {
        /* FIFO less than half full */
        fs->int_status |= ASC_FIFO_STATUS_HALF_FULL;
    } else {
        /* FIFO more than half full */
        fs->int_status &= ~ASC_FIFO_STATUS_HALF_FULL;
    }

    if (fs->cnt == 0x1ff && fifo_half_irq_enabled) {
        /* Raise FIFO half full IRQ */
        asc_raise_irq(s);
    }

    if (fs->cnt == 0) {
        /* Raise FIFO empty IRQ */
        fs->int_status |= ASC_FIFO_STATUS_FULL_EMPTY;
        asc_raise_irq(s);
    }

    return val;
}

static int generate_fifo(ASCState *s, int maxsamples)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint8_t *buf = s->mixbuf;
    int i, wcount = 0;

    while (wcount < maxsamples) {
        uint8_t val;
        int16_t d, f0, f1;
        int32_t t;
        int shift, filter;
        bool hasdata = false;

        for (i = 0; i < 2; i++) {
            ASCFIFOState *fs = &s->fifos[i];

            switch (fs->extregs[ASC_EXTREGS_FIFOCTRL] & 0x83) {
            case 0x82:
                /*
                 * CD-XA BRR mode: decompress 15 bytes into 28 16-bit
                 * samples
                 */
                if (!fs->cnt) {
                    val = 0x80;
                    break;
                }

                if (fs->xa_cnt == -1) {
                    /* Start of packet, get flags */
                    fs->xa_flags = asc_fifo_get(fs);
                    fs->xa_cnt = 0;
                }

                shift = fs->xa_flags & 0xf;
                filter = fs->xa_flags >> 4;
                f0 = (int8_t)fs->extregs[ASC_EXTREGS_CDXA_DECOMP_FILT +
                                 (filter << 1) + 1];
                f1 = (int8_t)fs->extregs[ASC_EXTREGS_CDXA_DECOMP_FILT +
                                 (filter << 1)];

                if ((fs->xa_cnt & 1) == 0) {
                    if (!fs->cnt) {
                        val = 0x80;
                        break;
                    }

                    fs->xa_val = asc_fifo_get(fs);
                    d = (fs->xa_val & 0xf) << 12;
                } else {
                    d = (fs->xa_val & 0xf0) << 8;
                }
                t = (d >> shift) + (((fs->xa_last[0] * f0) +
                                     (fs->xa_last[1] * f1) + 32) >> 6);
                if (t < -32768) {
                    t = -32768;
                } else if (t > 32767) {
                    t = 32767;
                }

                /*
                 * CD-XA BRR generates 16-bit signed output, so convert to
                 * 8-bit before writing to buffer. Does real hardware do the
                 * same?
                 */
                val = (uint8_t)(t / 256) ^ 0x80;
                hasdata = true;
                fs->xa_cnt++;

                fs->xa_last[1] = fs->xa_last[0];
                fs->xa_last[0] = (int16_t)t;

                if (fs->xa_cnt == 28) {
                    /* End of packet */
                    fs->xa_cnt = -1;
                }
                break;

            default:
                /* fallthrough */
            case 0x80:
                /* Raw mode */
                if (fs->cnt) {
                    val = asc_fifo_get(fs);
                    hasdata = true;
                } else {
                    val = 0x80;
                }
                break;
            }

            buf[wcount * 2 + i] = val;
        }

        if (!hasdata) {
            break;
        }

        wcount++;
    }

    /*
     * MacOS (un)helpfully leaves the FIFO engine running even when it has
     * finished writing out samples, but still expects the FIFO empty
     * interrupts to be generated for each FIFO cycle (without these interrupts
     * MacOS will freeze)
     */
    if (s->fifos[0].cnt == 0 && s->fifos[1].cnt == 0) {
        if (!s->fifo_empty_ns) {
            /* FIFO has completed first empty cycle */
            s->fifo_empty_ns = now;
        } else if (now > (s->fifo_empty_ns + ASC_FIFO_CYCLE_TIME)) {
            /* FIFO has completed entire cycle with no data */
            s->fifos[0].int_status |= ASC_FIFO_STATUS_HALF_FULL |
                                      ASC_FIFO_STATUS_FULL_EMPTY;
            s->fifos[1].int_status |= ASC_FIFO_STATUS_HALF_FULL |
                                      ASC_FIFO_STATUS_FULL_EMPTY;
            s->fifo_empty_ns = now;
            asc_raise_irq(s);
        }
    } else {
        /* FIFO contains data, reset empty time */
        s->fifo_empty_ns = 0;
    }

    return wcount;
}

static int generate_wavetable(ASCState *s, int maxsamples)
{
    uint8_t *buf = s->mixbuf;
    int channel, count = 0;

    while (count < maxsamples) {
        uint32_t left = 0, right = 0;
        uint8_t sample;

        for (channel = 0; channel < 4; channel++) {
            ASCFIFOState *fs = &s->fifos[channel >> 1];
            int chanreg = ASC_WAVETABLE + (channel << 3);
            uint32_t phase, incr, offset;

            phase = ldl_be_p(&s->regs[chanreg]);
            incr = ldl_be_p(&s->regs[chanreg + sizeof(uint32_t)]);

            phase += incr;
            offset = (phase >> 15) & 0x1ff;
            sample = fs->fifo[0x200 * (channel >> 1) + offset];

            stl_be_p(&s->regs[chanreg], phase);

            left += sample;
            right += sample;
        }

        buf[count * 2] = left >> 2;
        buf[count * 2 + 1] = right >> 2;

        count++;
    }

    return count;
}

static void asc_out_cb(void *opaque, int free_b)
{
    ASCState *s = opaque;
    int samples, generated;

    if (free_b == 0) {
        return;
    }

    samples = MIN(s->samples, free_b >> s->shift);

    switch (s->regs[ASC_MODE] & 3) {
    default:
        /* Off */
        generated = 0;
        break;
    case 1:
        /* FIFO mode */
        generated = generate_fifo(s, samples);
        break;
    case 2:
        /* Wave table mode */
        generated = generate_wavetable(s, samples);
        break;
    }

    if (!generated) {
        /* Workaround for audio underflow bug on Windows dsound backend */
        int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        int silent_samples = muldiv64(now - s->fifo_empty_ns,
                                      NANOSECONDS_PER_SECOND, ASC_FREQ);

        if (silent_samples > ASC_FIFO_CYCLE_TIME / 2) {
            /*
             * No new FIFO data within half a cycle time (~23ms) so fill the
             * entire available buffer with silence. This prevents an issue
             * with the Windows dsound backend whereby the sound appears to
             * loop because the FIFO has run out of data, and the driver
             * reuses the stale content in its circular audio buffer.
             */
            AUD_write(s->voice, s->silentbuf, samples << s->shift);
        }
        return;
    }

    AUD_write(s->voice, s->mixbuf, generated << s->shift);
}

static uint64_t asc_fifo_read(void *opaque, hwaddr addr,
                              unsigned size)
{
    ASCFIFOState *fs = opaque;

    trace_asc_read_fifo('A' + fs->index, addr, size, fs->fifo[addr]);
    return fs->fifo[addr];
}

static void asc_fifo_write(void *opaque, hwaddr addr, uint64_t value,
                           unsigned size)
{
    ASCFIFOState *fs = opaque;
    ASCState *s = container_of(fs, ASCState, fifos[fs->index]);
    bool fifo_half_irq_enabled = fs->extregs[ASC_EXTREGS_INTCTRL] & 1;

    trace_asc_write_fifo('A' + fs->index, addr, size, fs->wptr, fs->cnt, value);

    if (s->regs[ASC_MODE] == 1) {
        fs->fifo[fs->wptr++] = value;
        fs->wptr &= 0x3ff;
        fs->cnt++;

        if (fs->cnt <= 0x1ff) {
            /* FIFO less than half full */
            fs->int_status |= ASC_FIFO_STATUS_HALF_FULL;
        } else {
            /* FIFO at least half full */
            fs->int_status &= ~ASC_FIFO_STATUS_HALF_FULL;
        }

        if (fs->cnt == 0x200 && fifo_half_irq_enabled) {
            /* Raise FIFO half full interrupt */
            asc_raise_irq(s);
        }

        if (fs->cnt == 0x3ff) {
            /* Raise FIFO full interrupt */
            fs->int_status |= ASC_FIFO_STATUS_FULL_EMPTY;
            asc_raise_irq(s);
        }
    } else {
        fs->fifo[addr] = value;
    }
}

static const MemoryRegionOps asc_fifo_ops = {
    .read = asc_fifo_read,
    .write = asc_fifo_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .endianness = DEVICE_BIG_ENDIAN,
};

static void asc_fifo_reset(ASCFIFOState *fs);

static uint64_t asc_read(void *opaque, hwaddr addr,
                         unsigned size)
{
    ASCState *s = opaque;
    uint64_t prev, value;

    switch (addr) {
    case ASC_VERSION:
        switch (s->type) {
        default:
        case ASC_TYPE_ASC:
            value = 0;
            break;
        case ASC_TYPE_EASC:
            value = 0xb0;
            break;
        }
        break;
    case ASC_FIFOIRQ:
        prev = (s->fifos[0].int_status & 0x3) |
                (s->fifos[1].int_status & 0x3) << 2;

        s->fifos[0].int_status = 0;
        s->fifos[1].int_status = 0;
        asc_lower_irq(s);
        value = prev;
        break;
    default:
        value = s->regs[addr];
        break;
    }

    trace_asc_read_reg(addr, size, value);
    return value;
}

static void asc_write(void *opaque, hwaddr addr, uint64_t value,
                      unsigned size)
{
    ASCState *s = opaque;

    switch (addr) {
    case ASC_MODE:
        value &= 3;
        if (value != s->regs[ASC_MODE]) {
            asc_fifo_reset(&s->fifos[0]);
            asc_fifo_reset(&s->fifos[1]);
            asc_lower_irq(s);
            if (value != 0) {
                AUD_set_active_out(s->voice, 1);
            } else {
                AUD_set_active_out(s->voice, 0);
            }
        }
        break;
    case ASC_FIFOMODE:
        if (value & 0x80) {
            asc_fifo_reset(&s->fifos[0]);
            asc_fifo_reset(&s->fifos[1]);
            asc_lower_irq(s);
        }
        break;
    case ASC_WAVECTRL:
        break;
    case ASC_VOLUME:
        {
            int vol = (value & 0xe0);

            AUD_set_volume_out(s->voice, 0, vol, vol);
            break;
        }
    }

    trace_asc_write_reg(addr, size, value);
    s->regs[addr] = value;
}

static const MemoryRegionOps asc_regs_ops = {
    .read = asc_read,
    .write = asc_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    }
};

static uint64_t asc_ext_read(void *opaque, hwaddr addr,
                             unsigned size)
{
    ASCFIFOState *fs = opaque;
    uint64_t value;

    value = fs->extregs[addr];

    trace_asc_read_extreg('A' + fs->index, addr, size, value);
    return value;
}

static void asc_ext_write(void *opaque, hwaddr addr, uint64_t value,
                          unsigned size)
{
    ASCFIFOState *fs = opaque;

    trace_asc_write_extreg('A' + fs->index, addr, size, value);

    fs->extregs[addr] = value;
}

static const MemoryRegionOps asc_extregs_ops = {
    .read = asc_ext_read,
    .write = asc_ext_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
    .endianness = DEVICE_BIG_ENDIAN,
};

static int asc_post_load(void *opaque, int version)
{
    ASCState *s = ASC(opaque);

    if (s->regs[ASC_MODE] != 0) {
        AUD_set_active_out(s->voice, 1);
    }

    return 0;
}

static const VMStateDescription vmstate_asc_fifo = {
    .name = "apple-sound-chip.fifo",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(fifo, ASCFIFOState, ASC_FIFO_SIZE),
        VMSTATE_UINT8(int_status, ASCFIFOState),
        VMSTATE_INT32(cnt, ASCFIFOState),
        VMSTATE_INT32(wptr, ASCFIFOState),
        VMSTATE_INT32(rptr, ASCFIFOState),
        VMSTATE_UINT8_ARRAY(extregs, ASCFIFOState, ASC_EXTREG_SIZE),
        VMSTATE_INT32(xa_cnt, ASCFIFOState),
        VMSTATE_UINT8(xa_val, ASCFIFOState),
        VMSTATE_UINT8(xa_flags, ASCFIFOState),
        VMSTATE_INT16_ARRAY(xa_last, ASCFIFOState, 2),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_asc = {
    .name = "apple-sound-chip",
    .version_id = 0,
    .minimum_version_id = 0,
    .post_load = asc_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(fifos, ASCState, 2, 0, vmstate_asc_fifo,
                             ASCFIFOState),
        VMSTATE_UINT8_ARRAY(regs, ASCState, ASC_REG_SIZE),
        VMSTATE_INT64(fifo_empty_ns, ASCState),
        VMSTATE_END_OF_LIST()
    }
};

static void asc_fifo_reset(ASCFIFOState *fs)
{
    fs->wptr = 0;
    fs->rptr = 0;
    fs->cnt = 0;
    fs->xa_cnt = -1;
    fs->int_status = 0;
}

static void asc_fifo_init(ASCFIFOState *fs, int index)
{
    ASCState *s = container_of(fs, ASCState, fifos[index]);
    char *name;

    fs->index = index;
    name = g_strdup_printf("asc.fifo%c", 'A' + index);
    memory_region_init_io(&fs->mem_fifo, OBJECT(s), &asc_fifo_ops, fs,
                          name, ASC_FIFO_SIZE);
    g_free(name);

    name = g_strdup_printf("asc.extregs%c", 'A' + index);
    memory_region_init_io(&fs->mem_extregs, OBJECT(s), &asc_extregs_ops,
                          fs, name, ASC_EXTREG_SIZE);
    g_free(name);
}

static void asc_reset_hold(Object *obj, ResetType type)
{
    ASCState *s = ASC(obj);

    AUD_set_active_out(s->voice, 0);

    memset(s->regs, 0, sizeof(s->regs));
    asc_fifo_reset(&s->fifos[0]);
    asc_fifo_reset(&s->fifos[1]);
    s->fifo_empty_ns = 0;

    if (s->type == ASC_TYPE_ASC) {
        /* FIFO half full IRQs enabled by default */
        s->fifos[0].extregs[ASC_EXTREGS_INTCTRL] = 1;
        s->fifos[1].extregs[ASC_EXTREGS_INTCTRL] = 1;
    }
}

static void asc_unrealize(DeviceState *dev)
{
    ASCState *s = ASC(dev);

    g_free(s->mixbuf);
    g_free(s->silentbuf);

    AUD_remove_card(&s->card);
}

static void asc_realize(DeviceState *dev, Error **errp)
{
    ASCState *s = ASC(dev);
    struct audsettings as;

    if (!AUD_register_card("Apple Sound Chip", &s->card, errp)) {
        return;
    }

    as.freq = ASC_FREQ;
    as.nchannels = 2;
    as.fmt = AUDIO_FORMAT_U8;
    as.endianness = AUDIO_HOST_ENDIANNESS;

    s->voice = AUD_open_out(&s->card, s->voice, "asc.out", s, asc_out_cb,
                            &as);
    s->shift = 1;
    s->samples = AUD_get_buffer_size_out(s->voice) >> s->shift;
    s->mixbuf = g_malloc0(s->samples << s->shift);

    s->silentbuf = g_malloc0(s->samples << s->shift);
    memset(s->silentbuf, 0x80, s->samples << s->shift);

    /* Add easc registers if required */
    if (s->type == ASC_TYPE_EASC) {
        memory_region_add_subregion(&s->asc, ASC_EXTREG_OFFSET,
                                    &s->fifos[0].mem_extregs);
        memory_region_add_subregion(&s->asc,
                                    ASC_EXTREG_OFFSET + ASC_EXTREG_SIZE,
                                    &s->fifos[1].mem_extregs);
    }
}

static void asc_init(Object *obj)
{
    ASCState *s = ASC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init(&s->asc, OBJECT(obj), "asc", ASC_SIZE);

    asc_fifo_init(&s->fifos[0], 0);
    asc_fifo_init(&s->fifos[1], 1);

    memory_region_add_subregion(&s->asc, ASC_FIFO_OFFSET,
                                &s->fifos[0].mem_fifo);
    memory_region_add_subregion(&s->asc,
                                ASC_FIFO_OFFSET + ASC_FIFO_SIZE,
                                &s->fifos[1].mem_fifo);

    memory_region_init_io(&s->mem_regs, OBJECT(obj), &asc_regs_ops, s,
                          "asc.regs", ASC_REG_SIZE);
    memory_region_add_subregion(&s->asc, ASC_REG_OFFSET, &s->mem_regs);

    sysbus_init_irq(sbd, &s->irq);
    sysbus_init_mmio(sbd, &s->asc);
}

static const Property asc_properties[] = {
    DEFINE_AUDIO_PROPERTIES(ASCState, card),
    DEFINE_PROP_UINT8("asctype", ASCState, type, ASC_TYPE_ASC),
};

static void asc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->realize = asc_realize;
    dc->unrealize = asc_unrealize;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    dc->vmsd = &vmstate_asc;
    device_class_set_props(dc, asc_properties);
    rc->phases.hold = asc_reset_hold;
}

static const TypeInfo asc_info_types[] = {
    {
        .name = TYPE_ASC,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(ASCState),
        .instance_init = asc_init,
        .class_init = asc_class_init,
    },
};

DEFINE_TYPES(asc_info_types)
