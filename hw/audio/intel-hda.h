#ifndef HW_INTEL_HDA_H
#define HW_INTEL_HDA_H

#include "hw/qdev.h"

/* --------------------------------------------------------------------- */
/* hda bus                                                               */

#define TYPE_HDA_CODEC_DEVICE "hda-codec"
#define HDA_CODEC_DEVICE(obj) \
     OBJECT_CHECK(HDACodecDevice, (obj), TYPE_HDA_CODEC_DEVICE)
#define HDA_CODEC_DEVICE_CLASS(klass) \
     OBJECT_CLASS_CHECK(HDACodecDeviceClass, (klass), TYPE_HDA_CODEC_DEVICE)
#define HDA_CODEC_DEVICE_GET_CLASS(obj) \
     OBJECT_GET_CLASS(HDACodecDeviceClass, (obj), TYPE_HDA_CODEC_DEVICE)

#define TYPE_HDA_BUS "HDA"
#define HDA_BUS(obj) OBJECT_CHECK(HDACodecBus, (obj), TYPE_HDA_BUS)

typedef struct HDACodecBus HDACodecBus;
typedef struct HDACodecDevice HDACodecDevice;

typedef void (*hda_codec_response_func)(HDACodecDevice *dev,
                                        bool solicited, uint32_t response);
typedef bool (*hda_codec_xfer_func)(HDACodecDevice *dev,
                                    uint32_t stnr, bool output,
                                    uint8_t *buf, uint32_t len);

struct HDACodecBus {
    BusState qbus;
    uint32_t next_cad;
    hda_codec_response_func response;
    hda_codec_xfer_func xfer;
};

typedef struct HDACodecDeviceClass
{
    DeviceClass parent_class;

    int (*init)(HDACodecDevice *dev);
    int (*exit)(HDACodecDevice *dev);
    void (*command)(HDACodecDevice *dev, uint32_t nid, uint32_t data);
    void (*stream)(HDACodecDevice *dev, uint32_t stnr, bool running, bool output);
} HDACodecDeviceClass;

struct HDACodecDevice {
    DeviceState         qdev;
    uint32_t            cad;    /* codec address */
};

void hda_codec_bus_init(DeviceState *dev, HDACodecBus *bus,
                        hda_codec_response_func response,
                        hda_codec_xfer_func xfer);
HDACodecDevice *hda_codec_find(HDACodecBus *bus, uint32_t cad);

void hda_codec_response(HDACodecDevice *dev, bool solicited, uint32_t response);
bool hda_codec_xfer(HDACodecDevice *dev, uint32_t stnr, bool output,
                    uint8_t *buf, uint32_t len);

/* --------------------------------------------------------------------- */

#define dprint(_dev, _level, _fmt, ...)                                 \
    do {                                                                \
        if (_dev->debug >= _level) {                                    \
            fprintf(stderr, "%s: ", _dev->name);                        \
            fprintf(stderr, _fmt, ## __VA_ARGS__);                      \
        }                                                               \
    } while (0)

/* --------------------------------------------------------------------- */

#endif
