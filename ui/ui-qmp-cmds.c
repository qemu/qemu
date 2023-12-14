/*
 * QMP commands related to UI
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"

#include "io/channel-file.h"
#include "monitor/qmp-helpers.h"
#include "qapi/qapi-commands-ui.h"
#include "qapi/qmp/qerror.h"
#include "qemu/coroutine.h"
#include "qemu/cutils.h"
#include "trace.h"
#include "ui/console.h"
#include "ui/dbus-display.h"
#include "ui/qemu-spice.h"
#ifdef CONFIG_PNG
#include <png.h>
#endif

void qmp_set_password(SetPasswordOptions *opts, Error **errp)
{
    int rc;

    if (opts->protocol == DISPLAY_PROTOCOL_SPICE) {
        if (!qemu_using_spice(errp)) {
            return;
        }
        rc = qemu_spice.set_passwd(opts->password,
                opts->connected == SET_PASSWORD_ACTION_FAIL,
                opts->connected == SET_PASSWORD_ACTION_DISCONNECT);
    } else {
        assert(opts->protocol == DISPLAY_PROTOCOL_VNC);
        if (opts->connected != SET_PASSWORD_ACTION_KEEP) {
            /* vnc supports "connected=keep" only */
            error_setg(errp, "parameter 'connected' must be 'keep'"
                       " when 'protocol' is 'vnc'");
            return;
        }
        /*
         * Note that setting an empty password will not disable login
         * through this interface.
         */
        rc = vnc_display_password(opts->u.vnc.display, opts->password);
    }

    if (rc != 0) {
        error_setg(errp, "Could not set password");
    }
}

void qmp_expire_password(ExpirePasswordOptions *opts, Error **errp)
{
    time_t when;
    int rc;
    const char *whenstr = opts->time;
    const char *numstr = NULL;
    uint64_t num;

    if (strcmp(whenstr, "now") == 0) {
        when = 0;
    } else if (strcmp(whenstr, "never") == 0) {
        when = TIME_MAX;
    } else if (whenstr[0] == '+') {
        when = time(NULL);
        numstr = whenstr + 1;
    } else {
        when = 0;
        numstr = whenstr;
    }

    if (numstr) {
        if (qemu_strtou64(numstr, NULL, 10, &num) < 0) {
            error_setg(errp, "Parameter 'time' doesn't take value '%s'",
                       whenstr);
            return;
        }
        when += num;
    }

    if (opts->protocol == DISPLAY_PROTOCOL_SPICE) {
        if (!qemu_using_spice(errp)) {
            return;
        }
        rc = qemu_spice.set_pw_expire(when);
    } else {
        assert(opts->protocol == DISPLAY_PROTOCOL_VNC);
        rc = vnc_display_pw_expire(opts->u.vnc.display, when);
    }

    if (rc != 0) {
        error_setg(errp, "Could not set password expire time");
    }
}

#ifdef CONFIG_VNC
void qmp_change_vnc_password(const char *password, Error **errp)
{
    if (vnc_display_password(NULL, password) < 0) {
        error_setg(errp, "Could not set password");
    }
}
#endif

bool qmp_add_client_spice(int fd, bool has_skipauth, bool skipauth,
                          bool has_tls, bool tls, Error **errp)
{
    if (!qemu_using_spice(errp)) {
        return false;
    }
    skipauth = has_skipauth ? skipauth : false;
    tls = has_tls ? tls : false;
    if (qemu_spice.display_add_client(fd, skipauth, tls) < 0) {
        error_setg(errp, "spice failed to add client");
        return false;
    }
    return true;
}

#ifdef CONFIG_VNC
bool qmp_add_client_vnc(int fd, bool has_skipauth, bool skipauth,
                        bool has_tls, bool tls, Error **errp)
{
    skipauth = has_skipauth ? skipauth : false;
    vnc_display_add_client(NULL, fd, skipauth);
    return true;
}
#endif

#ifdef CONFIG_DBUS_DISPLAY
bool qmp_add_client_dbus_display(int fd, bool has_skipauth, bool skipauth,
                                 bool has_tls, bool tls, Error **errp)
{
    if (!qemu_using_dbus_display(errp)) {
        return false;
    }
    if (!qemu_dbus_display.add_client(fd, errp)) {
        return false;
    }
    return true;
}
#endif

void qmp_display_reload(DisplayReloadOptions *arg, Error **errp)
{
    switch (arg->type) {
    case DISPLAY_RELOAD_TYPE_VNC:
#ifdef CONFIG_VNC
        if (arg->u.vnc.has_tls_certs && arg->u.vnc.tls_certs) {
            vnc_display_reload_certs(NULL, errp);
        }
#else
        error_setg(errp, "vnc is invalid, missing 'CONFIG_VNC'");
#endif
        break;
    default:
        abort();
    }
}

void qmp_display_update(DisplayUpdateOptions *arg, Error **errp)
{
    switch (arg->type) {
    case DISPLAY_UPDATE_TYPE_VNC:
#ifdef CONFIG_VNC
        vnc_display_update(&arg->u.vnc, errp);
#else
        error_setg(errp, "vnc is invalid, missing 'CONFIG_VNC'");
#endif
        break;
    default:
        abort();
    }
}

void qmp_client_migrate_info(const char *protocol, const char *hostname,
                             bool has_port, int64_t port,
                             bool has_tls_port, int64_t tls_port,
                             const char *cert_subject,
                             Error **errp)
{
    if (strcmp(protocol, "spice") == 0) {
        if (!qemu_using_spice(errp)) {
            return;
        }

        if (!has_port && !has_tls_port) {
            error_setg(errp, "parameter 'port' or 'tls-port' is required");
            return;
        }

        if (qemu_spice.migrate_info(hostname,
                                    has_port ? port : -1,
                                    has_tls_port ? tls_port : -1,
                                    cert_subject)) {
            error_setg(errp, "Could not set up display for migration");
            return;
        }
        return;
    }

    error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "protocol", "'spice'");
}

#ifdef CONFIG_PIXMAN
#ifdef CONFIG_PNG
/**
 * png_save: Take a screenshot as PNG
 *
 * Saves screendump as a PNG file
 *
 * Returns true for success or false for error.
 *
 * @fd: File descriptor for PNG file.
 * @image: Image data in pixman format.
 * @errp: Pointer to an error.
 */
static bool png_save(int fd, pixman_image_t *image, Error **errp)
{
    int width = pixman_image_get_width(image);
    int height = pixman_image_get_height(image);
    png_struct *png_ptr;
    png_info *info_ptr;
    g_autoptr(pixman_image_t) linebuf =
        qemu_pixman_linebuf_create(PIXMAN_BE_r8g8b8, width);
    uint8_t *buf = (uint8_t *)pixman_image_get_data(linebuf);
    FILE *f = fdopen(fd, "wb");
    int y;
    if (!f) {
        error_setg_errno(errp, errno,
                         "Failed to create file from file descriptor");
        return false;
    }

    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL,
                                      NULL, NULL);
    if (!png_ptr) {
        error_setg(errp, "PNG creation failed. Unable to write struct");
        fclose(f);
        return false;
    }

    info_ptr = png_create_info_struct(png_ptr);

    if (!info_ptr) {
        error_setg(errp, "PNG creation failed. Unable to write info");
        fclose(f);
        png_destroy_write_struct(&png_ptr, &info_ptr);
        return false;
    }

    png_init_io(png_ptr, f);

    png_set_IHDR(png_ptr, info_ptr, width, height, 8,
                 PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    png_write_info(png_ptr, info_ptr);

    for (y = 0; y < height; ++y) {
        qemu_pixman_linebuf_fill(linebuf, image, width, 0, y);
        png_write_row(png_ptr, buf);
    }

    png_write_end(png_ptr, NULL);

    png_destroy_write_struct(&png_ptr, &info_ptr);

    if (fclose(f) != 0) {
        error_setg_errno(errp, errno,
                         "PNG creation failed. Unable to close file");
        return false;
    }

    return true;
}

#else /* no png support */

static bool png_save(int fd, pixman_image_t *image, Error **errp)
{
    error_setg(errp, "Enable PNG support with libpng for screendump");
    return false;
}

#endif /* CONFIG_PNG */

static bool ppm_save(int fd, pixman_image_t *image, Error **errp)
{
    int width = pixman_image_get_width(image);
    int height = pixman_image_get_height(image);
    g_autoptr(Object) ioc = OBJECT(qio_channel_file_new_fd(fd));
    g_autofree char *header = NULL;
    g_autoptr(pixman_image_t) linebuf = NULL;
    int y;

    trace_ppm_save(fd, image);

    header = g_strdup_printf("P6\n%d %d\n%d\n", width, height, 255);
    if (qio_channel_write_all(QIO_CHANNEL(ioc),
                              header, strlen(header), errp) < 0) {
        return false;
    }

    linebuf = qemu_pixman_linebuf_create(PIXMAN_BE_r8g8b8, width);
    for (y = 0; y < height; y++) {
        qemu_pixman_linebuf_fill(linebuf, image, width, 0, y);
        if (qio_channel_write_all(QIO_CHANNEL(ioc),
                                  (char *)pixman_image_get_data(linebuf),
                                  pixman_image_get_stride(linebuf), errp) < 0) {
            return false;
        }
    }

    return true;
}

/* Safety: coroutine-only, concurrent-coroutine safe, main thread only */
void coroutine_fn
qmp_screendump(const char *filename, const char *device,
               bool has_head, int64_t head,
               bool has_format, ImageFormat format, Error **errp)
{
    g_autoptr(pixman_image_t) image = NULL;
    QemuConsole *con;
    DisplaySurface *surface;
    int fd;

    if (device) {
        con = qemu_console_lookup_by_device_name(device, has_head ? head : 0,
                                                 errp);
        if (!con) {
            return;
        }
    } else {
        if (has_head) {
            error_setg(errp, "'head' must be specified together with 'device'");
            return;
        }
        con = qemu_console_lookup_by_index(0);
        if (!con) {
            error_setg(errp, "There is no console to take a screendump from");
            return;
        }
    }

    qemu_console_co_wait_update(con);

    /*
     * All pending coroutines are woken up, while the BQL is held.  No
     * further graphic update are possible until it is released.  Take
     * an image ref before that.
     */
    surface = qemu_console_surface(con);
    if (!surface) {
        error_setg(errp, "no surface");
        return;
    }
    image = pixman_image_ref(surface->image);

    fd = qemu_open_old(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);
    if (fd == -1) {
        error_setg(errp, "failed to open file '%s': %s", filename,
                   strerror(errno));
        return;
    }

    /*
     * The image content could potentially be updated as the coroutine
     * yields and releases the BQL. It could produce corrupted dump, but
     * it should be otherwise safe.
     */
    if (has_format && format == IMAGE_FORMAT_PNG) {
        /* PNG format specified for screendump */
        if (!png_save(fd, image, errp)) {
            qemu_unlink(filename);
        }
    } else {
        /* PPM format specified/default for screendump */
        if (!ppm_save(fd, image, errp)) {
            qemu_unlink(filename);
        }
    }
}
#endif /* CONFIG_PIXMAN */
