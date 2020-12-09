/*
 * A trivial unit test to check linking without glib. A real test suite should
 * probably based off libvhost-user-glib instead.
 */
#include <assert.h>
#include <stdlib.h>
#include "libvhost-user.h"

static void
panic(VuDev *dev, const char *err)
{
    abort();
}

static void
set_watch(VuDev *dev, int fd, int condition,
          vu_watch_cb cb, void *data)
{
    abort();
}

static void
remove_watch(VuDev *dev, int fd)
{
    abort();
}

static const VuDevIface iface = {
    0,
};

int
main(int argc, const char *argv[])
{
    bool rc;
    uint16_t max_queues = 2;
    int socket = 0;
    VuDev dev = { 0, };

    rc = vu_init(&dev, max_queues, socket, panic, NULL, set_watch, remove_watch, &iface);
    assert(rc == true);
    vu_deinit(&dev);

    return 0;
}
