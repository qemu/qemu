#include <glib.h>
#include <termios.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include "qemu/osdep.h"
#include "qemu/sockets.h"
#include "qga/channel.h"

#ifdef CONFIG_SOLARIS
#include <stropts.h>
#endif

#define GA_CHANNEL_BAUDRATE_DEFAULT B38400 /* for isa-serial channels */

struct GAChannel {
    GIOChannel *listen_channel;
    GIOChannel *client_channel;
    GAChannelMethod method;
    GAChannelCallback event_cb;
    gpointer user_data;
};

static int ga_channel_client_add(GAChannel *c, int fd);

static gboolean ga_channel_listen_accept(GIOChannel *channel,
                                         GIOCondition condition, gpointer data)
{
    GAChannel *c = data;
    int ret, client_fd;
    bool accepted = false;
    struct sockaddr_un addr;
    socklen_t addrlen = sizeof(addr);

    g_assert(channel != NULL);

    client_fd = qemu_accept(g_io_channel_unix_get_fd(channel),
                            (struct sockaddr *)&addr, &addrlen);
    if (client_fd == -1) {
        g_warning("error converting fd to gsocket: %s", strerror(errno));
        goto out;
    }
    qemu_set_nonblock(client_fd);
    ret = ga_channel_client_add(c, client_fd);
    if (ret) {
        g_warning("error setting up connection");
        close(client_fd);
        goto out;
    }
    accepted = true;

out:
    /* only accept 1 connection at a time */
    return !accepted;
}

/* start polling for readable events on listen fd, new==true
 * indicates we should use the existing s->listen_channel
 */
static void ga_channel_listen_add(GAChannel *c, int listen_fd, bool create)
{
    if (create) {
        c->listen_channel = g_io_channel_unix_new(listen_fd);
    }
    g_io_add_watch(c->listen_channel, G_IO_IN, ga_channel_listen_accept, c);
}

static void ga_channel_listen_close(GAChannel *c)
{
    g_assert(c->method == GA_CHANNEL_UNIX_LISTEN);
    g_assert(c->listen_channel);
    g_io_channel_shutdown(c->listen_channel, true, NULL);
    g_io_channel_unref(c->listen_channel);
    c->listen_channel = NULL;
}

/* cleanup state for closed connection/session, start accepting new
 * connections if we're in listening mode
 */
static void ga_channel_client_close(GAChannel *c)
{
    g_assert(c->client_channel);
    g_io_channel_shutdown(c->client_channel, true, NULL);
    g_io_channel_unref(c->client_channel);
    c->client_channel = NULL;
    if (c->method == GA_CHANNEL_UNIX_LISTEN && c->listen_channel) {
        ga_channel_listen_add(c, 0, false);
    }
}

static gboolean ga_channel_client_event(GIOChannel *channel,
                                        GIOCondition condition, gpointer data)
{
    GAChannel *c = data;
    gboolean client_cont;

    g_assert(c);
    if (c->event_cb) {
        client_cont = c->event_cb(condition, c->user_data);
        if (!client_cont) {
            ga_channel_client_close(c);
            return false;
        }
    }
    return true;
}

static int ga_channel_client_add(GAChannel *c, int fd)
{
    GIOChannel *client_channel;
    GError *err = NULL;

    g_assert(c && !c->client_channel);
    client_channel = g_io_channel_unix_new(fd);
    g_assert(client_channel);
    g_io_channel_set_encoding(client_channel, NULL, &err);
    if (err != NULL) {
        g_warning("error setting channel encoding to binary");
        g_error_free(err);
        return -1;
    }
    g_io_add_watch(client_channel, G_IO_IN | G_IO_HUP,
                   ga_channel_client_event, c);
    c->client_channel = client_channel;
    return 0;
}

static gboolean ga_channel_open(GAChannel *c, const gchar *path, GAChannelMethod method)
{
    int ret;
    c->method = method;

    switch (c->method) {
    case GA_CHANNEL_VIRTIO_SERIAL: {
        int fd = qemu_open(path, O_RDWR | O_NONBLOCK
#ifndef CONFIG_SOLARIS
                           | O_ASYNC
#endif
                           );
        if (fd == -1) {
            g_critical("error opening channel: %s", strerror(errno));
            return false;
        }
#ifdef CONFIG_SOLARIS
        ret = ioctl(fd, I_SETSIG, S_OUTPUT | S_INPUT | S_HIPRI);
        if (ret == -1) {
            g_critical("error setting event mask for channel: %s",
                       strerror(errno));
            close(fd);
            return false;
        }
#endif
        ret = ga_channel_client_add(c, fd);
        if (ret) {
            g_critical("error adding channel to main loop");
            close(fd);
            return false;
        }
        break;
    }
    case GA_CHANNEL_ISA_SERIAL: {
        struct termios tio;
        int fd = qemu_open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd == -1) {
            g_critical("error opening channel: %s", strerror(errno));
            return false;
        }
        tcgetattr(fd, &tio);
        /* set up serial port for non-canonical, dumb byte streaming */
        tio.c_iflag &= ~(IGNBRK | BRKINT | IGNPAR | PARMRK | INPCK | ISTRIP |
                         INLCR | IGNCR | ICRNL | IXON | IXOFF | IXANY |
                         IMAXBEL);
        tio.c_oflag = 0;
        tio.c_lflag = 0;
        tio.c_cflag |= GA_CHANNEL_BAUDRATE_DEFAULT;
        /* 1 available byte min or reads will block (we'll set non-blocking
         * elsewhere, else we have to deal with read()=0 instead)
         */
        tio.c_cc[VMIN] = 1;
        tio.c_cc[VTIME] = 0;
        /* flush everything waiting for read/xmit, it's garbage at this point */
        tcflush(fd, TCIFLUSH);
        tcsetattr(fd, TCSANOW, &tio);
        ret = ga_channel_client_add(c, fd);
        if (ret) {
            g_critical("error adding channel to main loop");
            close(fd);
            return false;
        }
        break;
    }
    case GA_CHANNEL_UNIX_LISTEN: {
        Error *local_err = NULL;
        int fd = unix_listen(path, NULL, strlen(path), &local_err);
        if (local_err != NULL) {
            g_critical("%s", error_get_pretty(local_err));
            error_free(local_err);
            return false;
        }
        ga_channel_listen_add(c, fd, true);
        break;
    }
    default:
        g_critical("error binding/listening to specified socket");
        return false;
    }

    return true;
}

GIOStatus ga_channel_write_all(GAChannel *c, const gchar *buf, gsize size)
{
    GError *err = NULL;
    gsize written = 0;
    GIOStatus status = G_IO_STATUS_NORMAL;

    while (size) {
        status = g_io_channel_write_chars(c->client_channel, buf, size,
                                          &written, &err);
        g_debug("sending data, count: %d", (int)size);
        if (err != NULL) {
            g_warning("error writing to channel: %s", err->message);
            return G_IO_STATUS_ERROR;
        }
        if (status != G_IO_STATUS_NORMAL) {
            break;
        }
        size -= written;
    }

    if (status == G_IO_STATUS_NORMAL) {
        status = g_io_channel_flush(c->client_channel, &err);
        if (err != NULL) {
            g_warning("error flushing channel: %s", err->message);
            return G_IO_STATUS_ERROR;
        }
    }

    return status;
}

GIOStatus ga_channel_read(GAChannel *c, gchar *buf, gsize size, gsize *count)
{
    return g_io_channel_read_chars(c->client_channel, buf, size, count, NULL);
}

GAChannel *ga_channel_new(GAChannelMethod method, const gchar *path,
                          GAChannelCallback cb, gpointer opaque)
{
    GAChannel *c = g_malloc0(sizeof(GAChannel));
    c->event_cb = cb;
    c->user_data = opaque;

    if (!ga_channel_open(c, path, method)) {
        g_critical("error opening channel");
        ga_channel_free(c);
        return NULL;
    }

    return c;
}

void ga_channel_free(GAChannel *c)
{
    if (c->method == GA_CHANNEL_UNIX_LISTEN
        && c->listen_channel) {
        ga_channel_listen_close(c);
    }
    if (c->client_channel) {
        ga_channel_client_close(c);
    }
    g_free(c);
}
