/*
 * Xen para-virtualization device
 *
 *  (c) 2008 Gerd Hoffmann <kraxel@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "qemu/osdep.h"

#include "hw/xen/xen_backend.h"
#include "hw/xen/xen_pvdev.h"

static int debug;
/* ------------------------------------------------------------- */

int xenstore_write_str(const char *base, const char *node, const char *val)
{
    char abspath[XEN_BUFSIZE];

    snprintf(abspath, sizeof(abspath), "%s/%s", base, node);
    if (!xs_write(xenstore, 0, abspath, val, strlen(val))) {
        return -1;
    }
    return 0;
}

char *xenstore_read_str(const char *base, const char *node)
{
    char abspath[XEN_BUFSIZE];
    unsigned int len;
    char *str, *ret = NULL;

    snprintf(abspath, sizeof(abspath), "%s/%s", base, node);
    str = xs_read(xenstore, 0, abspath, &len);
    if (str != NULL) {
        /* move to qemu-allocated memory to make sure
         * callers can savely g_free() stuff. */
        ret = g_strdup(str);
        free(str);
    }
    return ret;
}

int xenstore_write_int(const char *base, const char *node, int ival)
{
    char val[12];

    snprintf(val, sizeof(val), "%d", ival);
    return xenstore_write_str(base, node, val);
}

int xenstore_write_int64(const char *base, const char *node, int64_t ival)
{
    char val[21];

    snprintf(val, sizeof(val), "%"PRId64, ival);
    return xenstore_write_str(base, node, val);
}

int xenstore_read_int(const char *base, const char *node, int *ival)
{
    char *val;
    int rc = -1;

    val = xenstore_read_str(base, node);
    if (val && 1 == sscanf(val, "%d", ival)) {
        rc = 0;
    }
    g_free(val);
    return rc;
}

int xenstore_read_uint64(const char *base, const char *node, uint64_t *uval)
{
    char *val;
    int rc = -1;

    val = xenstore_read_str(base, node);
    if (val && 1 == sscanf(val, "%"SCNu64, uval)) {
        rc = 0;
    }
    g_free(val);
    return rc;
}

void xenstore_update(void *unused)
{
    char **vec = NULL;
    intptr_t type, ops, ptr;
    unsigned int dom, count;

    vec = xs_read_watch(xenstore, &count);
    if (vec == NULL) {
        goto cleanup;
    }

    if (sscanf(vec[XS_WATCH_TOKEN], "be:%" PRIxPTR ":%d:%" PRIxPTR,
               &type, &dom, &ops) == 3) {
        xenstore_update_be(vec[XS_WATCH_PATH], (void *)type, dom, (void*)ops);
    }
    if (sscanf(vec[XS_WATCH_TOKEN], "fe:%" PRIxPTR, &ptr) == 1) {
        xenstore_update_fe(vec[XS_WATCH_PATH], (void *)ptr);
    }

cleanup:
    free(vec);
}

const char *xenbus_strstate(enum xenbus_state state)
{
    static const char *const name[] = {
        [XenbusStateUnknown]       = "Unknown",
        [XenbusStateInitialising]  = "Initialising",
        [XenbusStateInitWait]      = "InitWait",
        [XenbusStateInitialised]   = "Initialised",
        [XenbusStateConnected]     = "Connected",
        [XenbusStateClosing]       = "Closing",
        [XenbusStateClosed]        = "Closed",
    };
    return (state < ARRAY_SIZE(name)) ? name[state] : "INVALID";
}

/*
 * msg_level:
 *  0 == errors (stderr + logfile).
 *  1 == informative debug messages (logfile only).
 *  2 == noisy debug messages (logfile only).
 *  3 == will flood your log (logfile only).
 */
void xen_be_printf(struct XenDevice *xendev, int msg_level,
                   const char *fmt, ...)
{
    va_list args;

    if (xendev) {
        if (msg_level > xendev->debug) {
            return;
        }
        qemu_log("xen be: %s: ", xendev->name);
        if (msg_level == 0) {
            fprintf(stderr, "xen be: %s: ", xendev->name);
        }
    } else {
        if (msg_level > debug) {
            return;
        }
        qemu_log("xen be core: ");
        if (msg_level == 0) {
            fprintf(stderr, "xen be core: ");
        }
    }
    va_start(args, fmt);
    qemu_log_vprintf(fmt, args);
    va_end(args);
    if (msg_level == 0) {
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        va_end(args);
    }
    qemu_log_flush();
}
