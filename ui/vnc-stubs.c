#include "qemu/osdep.h"
#include "ui/console.h"
#include "qapi/error.h"

int vnc_display_password(const char *id, const char *password, Error **errp)
{
    g_assert_not_reached();
}
int vnc_display_pw_expire(const char *id, time_t expires)
{
    g_assert_not_reached();
};
