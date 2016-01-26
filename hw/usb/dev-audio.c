/*
 * QEMU USB audio device
 *
 * written by:
 *  H. Peter Anvin <hpa@linux.intel.com>
 *  Gerd Hoffmann <kraxel@redhat.com>
 *
 * lousely based on usb net device code which is:
 *
 * Copyright (c) 2006 Thomas Sailer
 * Copyright (c) 2008 Andrzej Zaborowski
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
#include "qemu-common.h"
#include "hw/usb.h"
#include "hw/usb/desc.h"
#include "hw/hw.h"
#include "audio/audio.h"

#define USBAUDIO_VENDOR_NUM     0x46f4 /* CRC16() of "QEMU" */
#define USBAUDIO_PRODUCT_NUM    0x0002

#define DEV_CONFIG_VALUE        1 /* The one and only */

/* Descriptor subtypes for AC interfaces */
#define DST_AC_HEADER           1
#define DST_AC_INPUT_TERMINAL   2
#define DST_AC_OUTPUT_TERMINAL  3
#define DST_AC_FEATURE_UNIT     6
/* Descriptor subtypes for AS interfaces */
#define DST_AS_GENERAL          1
#define DST_AS_FORMAT_TYPE      2
/* Descriptor subtypes for endpoints */
#define DST_EP_GENERAL          1

enum usb_audio_strings {
    STRING_NULL,
    STRING_MANUFACTURER,
    STRING_PRODUCT,
    STRING_SERIALNUMBER,
    STRING_CONFIG,
    STRING_USBAUDIO_CONTROL,
    STRING_INPUT_TERMINAL,
    STRING_FEATURE_UNIT,
    STRING_OUTPUT_TERMINAL,
    STRING_NULL_STREAM,
    STRING_REAL_STREAM,
};

static const USBDescStrings usb_audio_stringtable = {
    [STRING_MANUFACTURER]       = "QEMU",
    [STRING_PRODUCT]            = "QEMU USB Audio",
    [STRING_SERIALNUMBER]       = "1",
    [STRING_CONFIG]             = "Audio Configuration",
    [STRING_USBAUDIO_CONTROL]   = "Audio Device",
    [STRING_INPUT_TERMINAL]     = "Audio Output Pipe",
    [STRING_FEATURE_UNIT]       = "Audio Output Volume Control",
    [STRING_OUTPUT_TERMINAL]    = "Audio Output Terminal",
    [STRING_NULL_STREAM]        = "Audio Output - Disabled",
    [STRING_REAL_STREAM]        = "Audio Output - 48 kHz Stereo",
};

#define U16(x) ((x) & 0xff), (((x) >> 8) & 0xff)
#define U24(x) U16(x), (((x) >> 16) & 0xff)
#define U32(x) U24(x), (((x) >> 24) & 0xff)

/*
 * A Basic Audio Device uses these specific values
 */
#define USBAUDIO_PACKET_SIZE     192
#define USBAUDIO_SAMPLE_RATE     48000
#define USBAUDIO_PACKET_INTERVAL 1

static const USBDescIface desc_iface[] = {
    {
        .bInterfaceNumber              = 0,
        .bNumEndpoints                 = 0,
        .bInterfaceClass               = USB_CLASS_AUDIO,
        .bInterfaceSubClass            = USB_SUBCLASS_AUDIO_CONTROL,
        .bInterfaceProtocol            = 0x04,
        .iInterface                    = STRING_USBAUDIO_CONTROL,
        .ndesc                         = 4,
        .descs = (USBDescOther[]) {
            {
                /* Headphone Class-Specific AC Interface Header Descriptor */
                .data = (uint8_t[]) {
                    0x09,                       /*  u8  bLength */
                    USB_DT_CS_INTERFACE,        /*  u8  bDescriptorType */
                    DST_AC_HEADER,              /*  u8  bDescriptorSubtype */
                    U16(0x0100),                /* u16  bcdADC */
                    U16(0x2b),                  /* u16  wTotalLength */
                    0x01,                       /*  u8  bInCollection */
                    0x01,                       /*  u8  baInterfaceNr */
                }
            },{
                /* Generic Stereo Input Terminal ID1 Descriptor */
                .data = (uint8_t[]) {
                    0x0c,                       /*  u8  bLength */
                    USB_DT_CS_INTERFACE,        /*  u8  bDescriptorType */
                    DST_AC_INPUT_TERMINAL,      /*  u8  bDescriptorSubtype */
                    0x01,                       /*  u8  bTerminalID */
                    U16(0x0101),                /* u16  wTerminalType */
                    0x00,                       /*  u8  bAssocTerminal */
                    0x02,                       /* u16  bNrChannels */
                    U16(0x0003),                /* u16  wChannelConfig */
                    0x00,                       /*  u8  iChannelNames */
                    STRING_INPUT_TERMINAL,      /*  u8  iTerminal */
                }
            },{
                /* Generic Stereo Feature Unit ID2 Descriptor */
                .data = (uint8_t[]) {
                    0x0d,                       /*  u8  bLength */
                    USB_DT_CS_INTERFACE,        /*  u8  bDescriptorType */
                    DST_AC_FEATURE_UNIT,        /*  u8  bDescriptorSubtype */
                    0x02,                       /*  u8  bUnitID */
                    0x01,                       /*  u8  bSourceID */
                    0x02,                       /*  u8  bControlSize */
                    U16(0x0001),                /* u16  bmaControls(0) */
                    U16(0x0002),                /* u16  bmaControls(1) */
                    U16(0x0002),                /* u16  bmaControls(2) */
                    STRING_FEATURE_UNIT,        /*  u8  iFeature */
                }
            },{
                /* Headphone Ouptut Terminal ID3 Descriptor */
                .data = (uint8_t[]) {
                    0x09,                       /*  u8  bLength */
                    USB_DT_CS_INTERFACE,        /*  u8  bDescriptorType */
                    DST_AC_OUTPUT_TERMINAL,     /*  u8  bDescriptorSubtype */
                    0x03,                       /*  u8  bUnitID */
                    U16(0x0301),                /* u16  wTerminalType (SPK) */
                    0x00,                       /*  u8  bAssocTerminal */
                    0x02,                       /*  u8  bSourceID */
                    STRING_OUTPUT_TERMINAL,     /*  u8  iTerminal */
                }
            }
        },
    },{
        .bInterfaceNumber              = 1,
        .bAlternateSetting             = 0,
        .bNumEndpoints                 = 0,
        .bInterfaceClass               = USB_CLASS_AUDIO,
        .bInterfaceSubClass            = USB_SUBCLASS_AUDIO_STREAMING,
        .iInterface                    = STRING_NULL_STREAM,
    },{
        .bInterfaceNumber              = 1,
        .bAlternateSetting             = 1,
        .bNumEndpoints                 = 1,
        .bInterfaceClass               = USB_CLASS_AUDIO,
        .bInterfaceSubClass            = USB_SUBCLASS_AUDIO_STREAMING,
        .iInterface                    = STRING_REAL_STREAM,
        .ndesc                         = 2,
        .descs = (USBDescOther[]) {
            {
                /* Headphone Class-specific AS General Interface Descriptor */
                .data = (uint8_t[]) {
                    0x07,                       /*  u8  bLength */
                    USB_DT_CS_INTERFACE,        /*  u8  bDescriptorType */
                    DST_AS_GENERAL,             /*  u8  bDescriptorSubtype */
                    0x01,                       /*  u8  bTerminalLink */
                    0x00,                       /*  u8  bDelay */
                    0x01, 0x00,                 /* u16  wFormatTag */
                }
            },{
                /* Headphone Type I Format Type Descriptor */
                .data = (uint8_t[]) {
                    0x0b,                       /*  u8  bLength */
                    USB_DT_CS_INTERFACE,        /*  u8  bDescriptorType */
                    DST_AS_FORMAT_TYPE,         /*  u8  bDescriptorSubtype */
                    0x01,                       /*  u8  bFormatType */
                    0x02,                       /*  u8  bNrChannels */
                    0x02,                       /*  u8  bSubFrameSize */
                    0x10,                       /*  u8  bBitResolution */
                    0x01,                       /*  u8  bSamFreqType */
                    U24(USBAUDIO_SAMPLE_RATE),  /* u24  tSamFreq */
                }
            }
        },
        .eps = (USBDescEndpoint[]) {
            {
                .bEndpointAddress      = USB_DIR_OUT | 0x01,
                .bmAttributes          = 0x0d,
                .wMaxPacketSize        = USBAUDIO_PACKET_SIZE,
                .bInterval             = 1,
                .is_audio              = 1,
                /* Stereo Headphone Class-specific
                   AS Audio Data Endpoint Descriptor */
                .extra = (uint8_t[]) {
                    0x07,                       /*  u8  bLength */
                    USB_DT_CS_ENDPOINT,         /*  u8  bDescriptorType */
                    DST_EP_GENERAL,             /*  u8  bDescriptorSubtype */
                    0x00,                       /*  u8  bmAttributes */
                    0x00,                       /*  u8  bLockDelayUnits */
                    U16(0x0000),                /* u16  wLockDelay */
                },
            },
        }
    }
};

static const USBDescDevice desc_device = {
    .bcdUSB                        = 0x0100,
    .bMaxPacketSize0               = 64,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 2,
            .bConfigurationValue   = DEV_CONFIG_VALUE,
            .iConfiguration        = STRING_CONFIG,
            .bmAttributes          = USB_CFG_ATT_ONE | USB_CFG_ATT_SELFPOWER,
            .bMaxPower             = 0x32,
            .nif = ARRAY_SIZE(desc_iface),
            .ifs = desc_iface,
        },
    },
};

static const USBDesc desc_audio = {
    .id = {
        .idVendor          = USBAUDIO_VENDOR_NUM,
        .idProduct         = USBAUDIO_PRODUCT_NUM,
        .bcdDevice         = 0,
        .iManufacturer     = STRING_MANUFACTURER,
        .iProduct          = STRING_PRODUCT,
        .iSerialNumber     = STRING_SERIALNUMBER,
    },
    .full = &desc_device,
    .str  = usb_audio_stringtable,
};

/*
 * A USB audio device supports an arbitrary number of alternate
 * interface settings for each interface.  Each corresponds to a block
 * diagram of parameterized blocks.  This can thus refer to things like
 * number of channels, data rates, or in fact completely different
 * block diagrams.  Alternative setting 0 is always the null block diagram,
 * which is used by a disabled device.
 */
enum usb_audio_altset {
    ALTSET_OFF  = 0x00,         /* No endpoint */
    ALTSET_ON   = 0x01,         /* Single endpoint */
};

/*
 * Class-specific control requests
 */
#define CR_SET_CUR      0x01
#define CR_GET_CUR      0x81
#define CR_SET_MIN      0x02
#define CR_GET_MIN      0x82
#define CR_SET_MAX      0x03
#define CR_GET_MAX      0x83
#define CR_SET_RES      0x04
#define CR_GET_RES      0x84
#define CR_SET_MEM      0x05
#define CR_GET_MEM      0x85
#define CR_GET_STAT     0xff

/*
 * Feature Unit Control Selectors
 */
#define MUTE_CONTROL                    0x01
#define VOLUME_CONTROL                  0x02
#define BASS_CONTROL                    0x03
#define MID_CONTROL                     0x04
#define TREBLE_CONTROL                  0x05
#define GRAPHIC_EQUALIZER_CONTROL       0x06
#define AUTOMATIC_GAIN_CONTROL          0x07
#define DELAY_CONTROL                   0x08
#define BASS_BOOST_CONTROL              0x09
#define LOUDNESS_CONTROL                0x0a

/*
 * buffering
 */

struct streambuf {
    uint8_t *data;
    uint32_t size;
    uint32_t prod;
    uint32_t cons;
};

static void streambuf_init(struct streambuf *buf, uint32_t size)
{
    g_free(buf->data);
    buf->size = size - (size % USBAUDIO_PACKET_SIZE);
    buf->data = g_malloc(buf->size);
    buf->prod = 0;
    buf->cons = 0;
}

static void streambuf_fini(struct streambuf *buf)
{
    g_free(buf->data);
    buf->data = NULL;
}

static int streambuf_put(struct streambuf *buf, USBPacket *p)
{
    uint32_t free = buf->size - (buf->prod - buf->cons);

    if (!free) {
        return 0;
    }
    assert(free >= USBAUDIO_PACKET_SIZE);
    usb_packet_copy(p, buf->data + (buf->prod % buf->size),
                    USBAUDIO_PACKET_SIZE);
    buf->prod += USBAUDIO_PACKET_SIZE;
    return USBAUDIO_PACKET_SIZE;
}

static uint8_t *streambuf_get(struct streambuf *buf)
{
    uint32_t used = buf->prod - buf->cons;
    uint8_t *data;

    if (!used) {
        return NULL;
    }
    assert(used >= USBAUDIO_PACKET_SIZE);
    data = buf->data + (buf->cons % buf->size);
    buf->cons += USBAUDIO_PACKET_SIZE;
    return data;
}

typedef struct USBAudioState {
    /* qemu interfaces */
    USBDevice dev;
    QEMUSoundCard card;

    /* state */
    struct {
        enum usb_audio_altset altset;
        struct audsettings as;
        SWVoiceOut *voice;
        bool mute;
        uint8_t vol[2];
        struct streambuf buf;
    } out;

    /* properties */
    uint32_t debug;
    uint32_t buffer;
} USBAudioState;

#define TYPE_USB_AUDIO "usb-audio"
#define USB_AUDIO(obj) OBJECT_CHECK(USBAudioState, (obj), TYPE_USB_AUDIO)

static void output_callback(void *opaque, int avail)
{
    USBAudioState *s = opaque;
    uint8_t *data;

    for (;;) {
        if (avail < USBAUDIO_PACKET_SIZE) {
            return;
        }
        data = streambuf_get(&s->out.buf);
        if (!data) {
            return;
        }
        AUD_write(s->out.voice, data, USBAUDIO_PACKET_SIZE);
        avail -= USBAUDIO_PACKET_SIZE;
    }
}

static int usb_audio_set_output_altset(USBAudioState *s, int altset)
{
    switch (altset) {
    case ALTSET_OFF:
        streambuf_init(&s->out.buf, s->buffer);
        AUD_set_active_out(s->out.voice, false);
        break;
    case ALTSET_ON:
        AUD_set_active_out(s->out.voice, true);
        break;
    default:
        return -1;
    }

    if (s->debug) {
        fprintf(stderr, "usb-audio: set interface %d\n", altset);
    }
    s->out.altset = altset;
    return 0;
}

/*
 * Note: we arbitrarily map the volume control range onto -inf..+8 dB
 */
#define ATTRIB_ID(cs, attrib, idif)     \
    (((cs) << 24) | ((attrib) << 16) | (idif))

static int usb_audio_get_control(USBAudioState *s, uint8_t attrib,
                                 uint16_t cscn, uint16_t idif,
                                 int length, uint8_t *data)
{
    uint8_t cs = cscn >> 8;
    uint8_t cn = cscn - 1;      /* -1 for the non-present master control */
    uint32_t aid = ATTRIB_ID(cs, attrib, idif);
    int ret = USB_RET_STALL;

    switch (aid) {
    case ATTRIB_ID(MUTE_CONTROL, CR_GET_CUR, 0x0200):
        data[0] = s->out.mute;
        ret = 1;
        break;
    case ATTRIB_ID(VOLUME_CONTROL, CR_GET_CUR, 0x0200):
        if (cn < 2) {
            uint16_t vol = (s->out.vol[cn] * 0x8800 + 127) / 255 + 0x8000;
            data[0] = vol;
            data[1] = vol >> 8;
            ret = 2;
        }
        break;
    case ATTRIB_ID(VOLUME_CONTROL, CR_GET_MIN, 0x0200):
        if (cn < 2) {
            data[0] = 0x01;
            data[1] = 0x80;
            ret = 2;
        }
        break;
    case ATTRIB_ID(VOLUME_CONTROL, CR_GET_MAX, 0x0200):
        if (cn < 2) {
            data[0] = 0x00;
            data[1] = 0x08;
            ret = 2;
        }
        break;
    case ATTRIB_ID(VOLUME_CONTROL, CR_GET_RES, 0x0200):
        if (cn < 2) {
            data[0] = 0x88;
            data[1] = 0x00;
            ret = 2;
        }
        break;
    }

    return ret;
}
static int usb_audio_set_control(USBAudioState *s, uint8_t attrib,
                                 uint16_t cscn, uint16_t idif,
                                 int length, uint8_t *data)
{
    uint8_t cs = cscn >> 8;
    uint8_t cn = cscn - 1;      /* -1 for the non-present master control */
    uint32_t aid = ATTRIB_ID(cs, attrib, idif);
    int ret = USB_RET_STALL;
    bool set_vol = false;

    switch (aid) {
    case ATTRIB_ID(MUTE_CONTROL, CR_SET_CUR, 0x0200):
        s->out.mute = data[0] & 1;
        set_vol = true;
        ret = 0;
        break;
    case ATTRIB_ID(VOLUME_CONTROL, CR_SET_CUR, 0x0200):
        if (cn < 2) {
            uint16_t vol = data[0] + (data[1] << 8);

            if (s->debug) {
                fprintf(stderr, "usb-audio: vol %04x\n", (uint16_t)vol);
            }

            vol -= 0x8000;
            vol = (vol * 255 + 0x4400) / 0x8800;
            if (vol > 255) {
                vol = 255;
            }

            s->out.vol[cn] = vol;
            set_vol = true;
            ret = 0;
        }
        break;
    }

    if (set_vol) {
        if (s->debug) {
            fprintf(stderr, "usb-audio: mute %d, lvol %3d, rvol %3d\n",
                    s->out.mute, s->out.vol[0], s->out.vol[1]);
        }
        AUD_set_volume_out(s->out.voice, s->out.mute,
                           s->out.vol[0], s->out.vol[1]);
    }

    return ret;
}

static void usb_audio_handle_control(USBDevice *dev, USBPacket *p,
                                    int request, int value, int index,
                                    int length, uint8_t *data)
{
    USBAudioState *s = USB_AUDIO(dev);
    int ret = 0;

    if (s->debug) {
        fprintf(stderr, "usb-audio: control transaction: "
                "request 0x%04x value 0x%04x index 0x%04x length 0x%04x\n",
                request, value, index, length);
    }

    ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
    if (ret >= 0) {
        return;
    }

    switch (request) {
    case ClassInterfaceRequest | CR_GET_CUR:
    case ClassInterfaceRequest | CR_GET_MIN:
    case ClassInterfaceRequest | CR_GET_MAX:
    case ClassInterfaceRequest | CR_GET_RES:
        ret = usb_audio_get_control(s, request & 0xff, value, index,
                                    length, data);
        if (ret < 0) {
            if (s->debug) {
                fprintf(stderr, "usb-audio: fail: get control\n");
            }
            goto fail;
        }
        p->actual_length = ret;
        break;

    case ClassInterfaceOutRequest | CR_SET_CUR:
    case ClassInterfaceOutRequest | CR_SET_MIN:
    case ClassInterfaceOutRequest | CR_SET_MAX:
    case ClassInterfaceOutRequest | CR_SET_RES:
        ret = usb_audio_set_control(s, request & 0xff, value, index,
                                    length, data);
        if (ret < 0) {
            if (s->debug) {
                fprintf(stderr, "usb-audio: fail: set control\n");
            }
            goto fail;
        }
        break;

    default:
fail:
        if (s->debug) {
            fprintf(stderr, "usb-audio: failed control transaction: "
                    "request 0x%04x value 0x%04x index 0x%04x length 0x%04x\n",
                    request, value, index, length);
        }
        p->status = USB_RET_STALL;
        break;
    }
}

static void usb_audio_set_interface(USBDevice *dev, int iface,
                                    int old, int value)
{
    USBAudioState *s = USB_AUDIO(dev);

    if (iface == 1) {
        usb_audio_set_output_altset(s, value);
    }
}

static void usb_audio_handle_reset(USBDevice *dev)
{
    USBAudioState *s = USB_AUDIO(dev);

    if (s->debug) {
        fprintf(stderr, "usb-audio: reset\n");
    }
    usb_audio_set_output_altset(s, ALTSET_OFF);
}

static void usb_audio_handle_dataout(USBAudioState *s, USBPacket *p)
{
    if (s->out.altset == ALTSET_OFF) {
        p->status = USB_RET_STALL;
        return;
    }

    streambuf_put(&s->out.buf, p);
    if (p->actual_length < p->iov.size && s->debug > 1) {
        fprintf(stderr, "usb-audio: output overrun (%zd bytes)\n",
                p->iov.size - p->actual_length);
    }
}

static void usb_audio_handle_data(USBDevice *dev, USBPacket *p)
{
    USBAudioState *s = (USBAudioState *) dev;

    if (p->pid == USB_TOKEN_OUT && p->ep->nr == 1) {
        usb_audio_handle_dataout(s, p);
        return;
    }

    p->status = USB_RET_STALL;
    if (s->debug) {
        fprintf(stderr, "usb-audio: failed data transaction: "
                        "pid 0x%x ep 0x%x len 0x%zx\n",
                        p->pid, p->ep->nr, p->iov.size);
    }
}

static void usb_audio_handle_destroy(USBDevice *dev)
{
    USBAudioState *s = USB_AUDIO(dev);

    if (s->debug) {
        fprintf(stderr, "usb-audio: destroy\n");
    }

    usb_audio_set_output_altset(s, ALTSET_OFF);
    AUD_close_out(&s->card, s->out.voice);
    AUD_remove_card(&s->card);

    streambuf_fini(&s->out.buf);
}

static void usb_audio_realize(USBDevice *dev, Error **errp)
{
    USBAudioState *s = USB_AUDIO(dev);

    usb_desc_create_serial(dev);
    usb_desc_init(dev);
    s->dev.opaque = s;
    AUD_register_card(TYPE_USB_AUDIO, &s->card);

    s->out.altset        = ALTSET_OFF;
    s->out.mute          = false;
    s->out.vol[0]        = 240; /* 0 dB */
    s->out.vol[1]        = 240; /* 0 dB */
    s->out.as.freq       = USBAUDIO_SAMPLE_RATE;
    s->out.as.nchannels  = 2;
    s->out.as.fmt        = AUD_FMT_S16;
    s->out.as.endianness = 0;
    streambuf_init(&s->out.buf, s->buffer);

    s->out.voice = AUD_open_out(&s->card, s->out.voice, TYPE_USB_AUDIO,
                                s, output_callback, &s->out.as);
    AUD_set_volume_out(s->out.voice, s->out.mute, s->out.vol[0], s->out.vol[1]);
    AUD_set_active_out(s->out.voice, 0);
}

static const VMStateDescription vmstate_usb_audio = {
    .name = TYPE_USB_AUDIO,
    .unmigratable = 1,
};

static Property usb_audio_properties[] = {
    DEFINE_PROP_UINT32("debug", USBAudioState, debug, 0),
    DEFINE_PROP_UINT32("buffer", USBAudioState, buffer,
                       32 * USBAUDIO_PACKET_SIZE),
    DEFINE_PROP_END_OF_LIST(),
};

static void usb_audio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *k = USB_DEVICE_CLASS(klass);

    dc->vmsd          = &vmstate_usb_audio;
    dc->props         = usb_audio_properties;
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
    k->product_desc   = "QEMU USB Audio Interface";
    k->usb_desc       = &desc_audio;
    k->realize        = usb_audio_realize;
    k->handle_reset   = usb_audio_handle_reset;
    k->handle_control = usb_audio_handle_control;
    k->handle_data    = usb_audio_handle_data;
    k->handle_destroy = usb_audio_handle_destroy;
    k->set_interface  = usb_audio_set_interface;
}

static const TypeInfo usb_audio_info = {
    .name          = TYPE_USB_AUDIO,
    .parent        = TYPE_USB_DEVICE,
    .instance_size = sizeof(USBAudioState),
    .class_init    = usb_audio_class_init,
};

static void usb_audio_register_types(void)
{
    type_register_static(&usb_audio_info);
    usb_legacy_register(TYPE_USB_AUDIO, "audio", NULL);
}

type_init(usb_audio_register_types)
