/*
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <spice/ipc_ring.h>
#include <spice/enums.h>
#include <spice/qxl_dev.h>

#include "qemu/thread.h"
#include "ui/qemu-pixman.h"
#include "ui/console.h"
#include "sysemu/sysemu.h"

#define NUM_MEMSLOTS 8
#define MEMSLOT_GENERATION_BITS 8
#define MEMSLOT_SLOT_BITS 8

#define MEMSLOT_GROUP_HOST  0
#define MEMSLOT_GROUP_GUEST 1
#define NUM_MEMSLOTS_GROUPS 2

/*
 * Internal enum to differenciate between options for
 * io calls that have a sync (old) version and an _async (new)
 * version:
 *  QXL_SYNC: use the old version
 *  QXL_ASYNC: use the new version and make sure there are no two
 *   happening at the same time. This is used for guest initiated
 *   calls
 */
typedef enum qxl_async_io {
    QXL_SYNC,
    QXL_ASYNC,
} qxl_async_io;

enum {
    QXL_COOKIE_TYPE_IO,
    QXL_COOKIE_TYPE_RENDER_UPDATE_AREA,
    QXL_COOKIE_TYPE_POST_LOAD_MONITORS_CONFIG,
};

typedef struct QXLCookie {
    int      type;
    uint64_t io;
    union {
        uint32_t surface_id;
        QXLRect area;
        struct {
            QXLRect area;
            int redraw;
        } render;
    } u;
} QXLCookie;

QXLCookie *qxl_cookie_new(int type, uint64_t io);

typedef struct SimpleSpiceDisplay SimpleSpiceDisplay;
typedef struct SimpleSpiceUpdate SimpleSpiceUpdate;

struct SimpleSpiceDisplay {
    DisplaySurface *ds;
    DisplayChangeListener dcl;
    void *buf;
    int bufsize;
    QXLWorker *worker;
    QXLInstance qxl;
    uint32_t unique;
    pixman_image_t *surface;
    pixman_image_t *mirror;
    int32_t num_surfaces;

    QXLRect dirty;
    int notify;

    /*
     * All struct members below this comment can be accessed from
     * both spice server and qemu (iothread) context and any access
     * to them must be protected by the lock.
     */
    QemuMutex lock;
    QTAILQ_HEAD(, SimpleSpiceUpdate) updates;
    QEMUCursor *cursor;
    int mouse_x, mouse_y;
};

struct SimpleSpiceUpdate {
    QXLDrawable drawable;
    QXLImage image;
    QXLCommandExt ext;
    uint8_t *bitmap;
    QTAILQ_ENTRY(SimpleSpiceUpdate) next;
};

int qemu_spice_rect_is_empty(const QXLRect* r);
void qemu_spice_rect_union(QXLRect *dest, const QXLRect *r);

void qemu_spice_destroy_update(SimpleSpiceDisplay *sdpy, SimpleSpiceUpdate *update);
void qemu_spice_create_host_memslot(SimpleSpiceDisplay *ssd);
void qemu_spice_create_host_primary(SimpleSpiceDisplay *ssd);
void qemu_spice_destroy_host_primary(SimpleSpiceDisplay *ssd);
void qemu_spice_vm_change_state_handler(void *opaque, int running,
                                        RunState state);
void qemu_spice_display_init_common(SimpleSpiceDisplay *ssd);

void qemu_spice_display_update(SimpleSpiceDisplay *ssd,
                               int x, int y, int w, int h);
void qemu_spice_display_switch(SimpleSpiceDisplay *ssd,
                               DisplaySurface *surface);
void qemu_spice_display_refresh(SimpleSpiceDisplay *ssd);
void qemu_spice_cursor_refresh_unlocked(SimpleSpiceDisplay *ssd);

void qemu_spice_add_memslot(SimpleSpiceDisplay *ssd, QXLDevMemSlot *memslot,
                            qxl_async_io async);
void qemu_spice_del_memslot(SimpleSpiceDisplay *ssd, uint32_t gid,
                            uint32_t sid);
void qemu_spice_create_primary_surface(SimpleSpiceDisplay *ssd, uint32_t id,
                                       QXLDevSurfaceCreate *surface,
                                       qxl_async_io async);
void qemu_spice_destroy_primary_surface(SimpleSpiceDisplay *ssd,
                                        uint32_t id, qxl_async_io async);
void qemu_spice_wakeup(SimpleSpiceDisplay *ssd);
void qemu_spice_display_start(void);
void qemu_spice_display_stop(void);
int qemu_spice_display_is_running(SimpleSpiceDisplay *ssd);
