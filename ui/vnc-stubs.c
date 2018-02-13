#include "qemu/osdep.h"
#include "ui/console.h"
#include "qapi/error.h"

int vnc_display_password(const char *id, const char *password)
{
    return -ENODEV;
}
int vnc_display_pw_expire(const char *id, time_t expires)
{
    return -ENODEV;
};
QemuOpts *vnc_parse(const char *str, Error **errp)
{
    error_setg(errp, "VNC support is disabled");
    return NULL;
}
int vnc_init_func(void *opaque, QemuOpts *opts, Error **errp)
{
    error_setg(errp, "VNC support is disabled");
    return -1;
}
