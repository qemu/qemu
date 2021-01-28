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
void vnc_parse(const char *str)
{
    if (strcmp(str, "none") == 0) {
        return;
    }
    error_setg(&error_fatal, "VNC support is disabled");
}
int vnc_init_func(void *opaque, QemuOpts *opts, Error **errp)
{
    error_setg(errp, "VNC support is disabled");
    return -1;
}
