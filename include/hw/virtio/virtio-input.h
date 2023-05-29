#ifndef QEMU_VIRTIO_INPUT_H
#define QEMU_VIRTIO_INPUT_H

#include "ui/input.h"
#include "sysemu/vhost-user-backend.h"

/* ----------------------------------------------------------------- */
/* virtio input protocol                                             */

#include "standard-headers/linux/virtio_ids.h"
#include "standard-headers/linux/virtio_input.h"
#include "qom/object.h"

typedef struct virtio_input_absinfo virtio_input_absinfo;
typedef struct virtio_input_config virtio_input_config;
typedef struct virtio_input_event virtio_input_event;

/* ----------------------------------------------------------------- */
/* qemu internals                                                    */

#define TYPE_VIRTIO_INPUT "virtio-input-device"
OBJECT_DECLARE_TYPE(VirtIOInput, VirtIOInputClass,
                    VIRTIO_INPUT)
#define VIRTIO_INPUT_GET_PARENT_CLASS(obj) \
        OBJECT_GET_PARENT_CLASS(obj, TYPE_VIRTIO_INPUT)

#define TYPE_VIRTIO_INPUT_HID  "virtio-input-hid-device"
#define TYPE_VIRTIO_KEYBOARD   "virtio-keyboard-device"
#define TYPE_VIRTIO_MOUSE      "virtio-mouse-device"
#define TYPE_VIRTIO_TABLET     "virtio-tablet-device"
#define TYPE_VIRTIO_MULTITOUCH "virtio-multitouch-device"

OBJECT_DECLARE_SIMPLE_TYPE(VirtIOInputHID, VIRTIO_INPUT_HID)
#define VIRTIO_INPUT_HID_GET_PARENT_CLASS(obj) \
        OBJECT_GET_PARENT_CLASS(obj, TYPE_VIRTIO_INPUT_HID)

#define TYPE_VIRTIO_INPUT_HOST   "virtio-input-host-device"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIOInputHost, VIRTIO_INPUT_HOST)
#define VIRTIO_INPUT_HOST_GET_PARENT_CLASS(obj) \
        OBJECT_GET_PARENT_CLASS(obj, TYPE_VIRTIO_INPUT_HOST)

#define TYPE_VHOST_USER_INPUT   "vhost-user-input"
OBJECT_DECLARE_SIMPLE_TYPE(VHostUserInput, VHOST_USER_INPUT)
#define VHOST_USER_INPUT_GET_PARENT_CLASS(obj)             \
    OBJECT_GET_PARENT_CLASS(obj, TYPE_VHOST_USER_INPUT)

typedef struct VirtIOInputConfig VirtIOInputConfig;

struct VirtIOInputConfig {
    virtio_input_config               config;
    QTAILQ_ENTRY(VirtIOInputConfig)   node;
};

struct VirtIOInput {
    VirtIODevice                      parent_obj;
    uint8_t                           cfg_select;
    uint8_t                           cfg_subsel;
    uint32_t                          cfg_size;
    QTAILQ_HEAD(, VirtIOInputConfig)  cfg_list;
    VirtQueue                         *evt, *sts;
    char                              *serial;

    struct {
        virtio_input_event event;
        VirtQueueElement *elem;
    }                                 *queue;
    uint32_t                          qindex, qsize;

    bool                              active;
};

struct VirtIOInputClass {
    /*< private >*/
    VirtioDeviceClass parent;
    /*< public >*/

    DeviceRealize realize;
    DeviceUnrealize unrealize;
    void (*change_active)(VirtIOInput *vinput);
    void (*handle_status)(VirtIOInput *vinput, virtio_input_event *event);
};

struct VirtIOInputHID {
    VirtIOInput                       parent_obj;
    char                              *display;
    uint32_t                          head;
    QemuInputHandler                  *handler;
    QemuInputHandlerState             *hs;
    int                               ledstate;
    bool                              wheel_axis;
};

struct VirtIOInputHost {
    VirtIOInput                       parent_obj;
    char                              *evdev;
    int                               fd;
};

struct VHostUserInput {
    VirtIOInput                       parent_obj;

    VhostUserBackend                  *vhost;
};

void virtio_input_send(VirtIOInput *vinput, virtio_input_event *event);
void virtio_input_init_config(VirtIOInput *vinput,
                              virtio_input_config *config);
virtio_input_config *virtio_input_find_config(VirtIOInput *vinput,
                                              uint8_t select,
                                              uint8_t subsel);
void virtio_input_add_config(VirtIOInput *vinput,
                             virtio_input_config *config);
void virtio_input_idstr_config(VirtIOInput *vinput,
                               uint8_t select, const char *string);

#endif /* QEMU_VIRTIO_INPUT_H */
