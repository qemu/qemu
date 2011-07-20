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

#include "qemu-thread.h"
#include "console.h"
#include "pflib.h"

#define NUM_MEMSLOTS 8
#define MEMSLOT_GENERATION_BITS 8
#define MEMSLOT_SLOT_BITS 8

#define MEMSLOT_GROUP_HOST  0
#define MEMSLOT_GROUP_GUEST 1
#define NUM_MEMSLOTS_GROUPS 2

#define NUM_SURFACES 1024

typedef struct SimpleSpiceDisplay SimpleSpiceDisplay;
typedef struct SimpleSpiceUpdate SimpleSpiceUpdate;

struct SimpleSpiceDisplay {
    DisplayState *ds;
    void *buf;
    int bufsize;
    QXLWorker *worker;
    QXLInstance qxl;
    uint32_t unique;
    QemuPfConv *conv;

    QXLRect dirty;
    int notify;
    int running;

    /*
     * All struct members below this comment can be accessed from
     * both spice server and qemu (iothread) context and any access
     * to them must be protected by the lock.
     */
    QemuMutex lock;
    SimpleSpiceUpdate *update;
    QEMUCursor *cursor;
    int mouse_x, mouse_y;
};

struct SimpleSpiceUpdate {
    QXLDrawable drawable;
    QXLImage image;
    QXLCommandExt ext;
    uint8_t *bitmap;
};

int qemu_spice_rect_is_empty(const QXLRect* r);
void qemu_spice_rect_union(QXLRect *dest, const QXLRect *r);

void qemu_spice_destroy_update(SimpleSpiceDisplay *sdpy, SimpleSpiceUpdate *update);
void qemu_spice_create_host_memslot(SimpleSpiceDisplay *ssd);
void qemu_spice_create_host_primary(SimpleSpiceDisplay *ssd);
void qemu_spice_destroy_host_primary(SimpleSpiceDisplay *ssd);
void qemu_spice_vm_change_state_handler(void *opaque, int running, int reason);
void qemu_spice_display_init_common(SimpleSpiceDisplay *ssd, DisplayState *ds);

void qemu_spice_display_update(SimpleSpiceDisplay *ssd,
                               int x, int y, int w, int h);
void qemu_spice_display_resize(SimpleSpiceDisplay *ssd);
void qemu_spice_display_refresh(SimpleSpiceDisplay *ssd);

void qemu_spice_update_area(SimpleSpiceDisplay *ssd, uint32_t surface_id,
                            struct QXLRect *area, struct QXLRect *dirty_rects,
                            uint32_t num_dirty_rects,
                            uint32_t clear_dirty_region);
void qemu_spice_add_memslot(SimpleSpiceDisplay *ssd, QXLDevMemSlot *memslot);
void qemu_spice_del_memslot(SimpleSpiceDisplay *ssd, uint32_t gid,
                            uint32_t sid);
void qemu_spice_create_primary_surface(SimpleSpiceDisplay *ssd, uint32_t id,
                                       QXLDevSurfaceCreate *surface);
void qemu_spice_destroy_primary_surface(SimpleSpiceDisplay *ssd, uint32_t id);
void qemu_spice_destroy_surface_wait(SimpleSpiceDisplay *ssd, uint32_t id);
void qemu_spice_loadvm_commands(SimpleSpiceDisplay *ssd,
                                struct QXLCommandExt *ext, uint32_t count);
void qemu_spice_wakeup(SimpleSpiceDisplay *ssd);
void qemu_spice_oom(SimpleSpiceDisplay *ssd);
void qemu_spice_start(SimpleSpiceDisplay *ssd);
void qemu_spice_stop(SimpleSpiceDisplay *ssd);
void qemu_spice_reset_memslots(SimpleSpiceDisplay *ssd);
void qemu_spice_destroy_surfaces(SimpleSpiceDisplay *ssd);
void qemu_spice_reset_image_cache(SimpleSpiceDisplay *ssd);
void qemu_spice_reset_cursor(SimpleSpiceDisplay *ssd);
