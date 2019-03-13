/*
 * Vhost User library
 *
 * Copyright (c) 2016 Nutanix Inc. All rights reserved.
 * Copyright (c) 2017 Red Hat, Inc.
 *
 * Authors:
 *  Marc-André Lureau <mlureau@redhat.com>
 *  Felipe Franciosi <felipe@nutanix.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "libvhost-user-glib.h"

/* glib event loop integration for libvhost-user and misc callbacks */

G_STATIC_ASSERT((int)G_IO_IN == (int)VU_WATCH_IN);
G_STATIC_ASSERT((int)G_IO_OUT == (int)VU_WATCH_OUT);
G_STATIC_ASSERT((int)G_IO_PRI == (int)VU_WATCH_PRI);
G_STATIC_ASSERT((int)G_IO_ERR == (int)VU_WATCH_ERR);
G_STATIC_ASSERT((int)G_IO_HUP == (int)VU_WATCH_HUP);

typedef struct VugSrc {
    GSource parent;
    VuDev *dev;
    GPollFD gfd;
} VugSrc;

static gboolean
vug_src_prepare(GSource *gsrc, gint *timeout)
{
    g_assert(timeout);

    *timeout = -1;
    return FALSE;
}

static gboolean
vug_src_check(GSource *gsrc)
{
    VugSrc *src = (VugSrc *)gsrc;

    g_assert(src);

    return src->gfd.revents & src->gfd.events;
}

static gboolean
vug_src_dispatch(GSource *gsrc, GSourceFunc cb, gpointer data)
{
    VugSrc *src = (VugSrc *)gsrc;

    g_assert(src);

    ((vu_watch_cb)cb)(src->dev, src->gfd.revents, data);

    return G_SOURCE_CONTINUE;
}

static GSourceFuncs vug_src_funcs = {
    vug_src_prepare,
    vug_src_check,
    vug_src_dispatch,
    NULL
};

GSource *
vug_source_new(VugDev *gdev, int fd, GIOCondition cond,
               vu_watch_cb vu_cb, gpointer data)
{
    VuDev *dev = &gdev->parent;
    GSource *gsrc;
    VugSrc *src;
    guint id;

    g_assert(gdev);
    g_assert(fd >= 0);
    g_assert(vu_cb);

    gsrc = g_source_new(&vug_src_funcs, sizeof(VugSrc));
    g_source_set_callback(gsrc, (GSourceFunc)vu_cb, data, NULL);
    src = (VugSrc *)gsrc;
    src->dev = dev;
    src->gfd.fd = fd;
    src->gfd.events = cond;

    g_source_add_poll(gsrc, &src->gfd);
    id = g_source_attach(gsrc, NULL);
    g_assert(id);
    g_source_unref(gsrc);

    return gsrc;
}

static void
set_watch(VuDev *vu_dev, int fd, int vu_evt, vu_watch_cb cb, void *pvt)
{
    GSource *src;
    VugDev *dev;

    g_assert(vu_dev);
    g_assert(fd >= 0);
    g_assert(cb);

    dev = container_of(vu_dev, VugDev, parent);
    src = vug_source_new(dev, fd, vu_evt, cb, pvt);
    g_hash_table_replace(dev->fdmap, GINT_TO_POINTER(fd), src);
}

static void
remove_watch(VuDev *vu_dev, int fd)
{
    VugDev *dev;

    g_assert(vu_dev);
    g_assert(fd >= 0);

    dev = container_of(vu_dev, VugDev, parent);
    g_hash_table_remove(dev->fdmap, GINT_TO_POINTER(fd));
}


static void vug_watch(VuDev *dev, int condition, void *data)
{
    if (!vu_dispatch(dev) != 0) {
        dev->panic(dev, "Error processing vhost message");
    }
}

void
vug_init(VugDev *dev, int socket,
         vu_panic_cb panic, const VuDevIface *iface)
{
    g_assert(dev);
    g_assert(iface);

    vu_init(&dev->parent, socket, panic, set_watch, remove_watch, iface);
    dev->fdmap = g_hash_table_new_full(NULL, NULL, NULL,
                                       (GDestroyNotify) g_source_destroy);

    dev->src = vug_source_new(dev, socket, G_IO_IN, vug_watch, NULL);
}

void
vug_deinit(VugDev *dev)
{
    g_assert(dev);

    g_hash_table_unref(dev->fdmap);
    g_source_unref(dev->src);
}
