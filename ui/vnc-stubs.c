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
