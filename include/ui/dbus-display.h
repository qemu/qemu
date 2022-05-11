#ifndef DBUS_DISPLAY_H
#define DBUS_DISPLAY_H

#include "qapi/error.h"
#include "ui/dbus-module.h"

static inline bool qemu_using_dbus_display(Error **errp)
{
    if (!using_dbus_display) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_ACTIVE,
                  "D-Bus display is not in use");
        return false;
    }
    return true;
}

#endif /* DBUS_DISPLAY_H */
