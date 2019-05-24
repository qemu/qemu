#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/display/ramfb.h"

void ramfb_display_update(QemuConsole *con, RAMFBState *s)
{
}

RAMFBState *ramfb_setup(DeviceState* dev, Error **errp)
{
    error_setg(errp, "ramfb support not available");
    return NULL;
}
