#ifndef CHARDEV_SPICE_H
#define CHARDEV_SPICE_H

#include <spice.h>
#include "chardev/char-fe.h"

typedef struct SpiceChardev {
    Chardev               parent;

    SpiceCharDeviceInstance sin;
    bool                  active;
    bool                  blocked;
    const uint8_t         *datapos;
    int                   datalen;
    QLIST_ENTRY(SpiceChardev) next;
} SpiceChardev;

#define TYPE_CHARDEV_SPICE "chardev-spice"
#define TYPE_CHARDEV_SPICEVMC "chardev-spicevmc"
#define TYPE_CHARDEV_SPICEPORT "chardev-spiceport"

#define SPICE_CHARDEV(obj) OBJECT_CHECK(SpiceChardev, (obj), TYPE_CHARDEV_SPICE)

void qemu_chr_open_spice_port(Chardev *chr, ChardevBackend *backend,
                              bool *be_opened, Error **errp);

#endif
