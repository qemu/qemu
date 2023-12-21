/*
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * written by Yaniv Kamay, Izik Eidus, Gerd Hoffmann
 * maintained by Gerd Hoffmann <kraxel@redhat.com>
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

#include "qemu/osdep.h"
#include "qemu/units.h"
#include <zlib.h>

#include "qapi/error.h"
#include "qemu/timer.h"
#include "qemu/queue.h"
#include "qemu/atomic.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "hw/qdev-properties.h"
#include "sysemu/runstate.h"
#include "migration/vmstate.h"
#include "trace.h"

#include "qxl.h"

#undef SPICE_RING_CONS_ITEM
#define SPICE_RING_CONS_ITEM(qxl, r, ret) {                             \
        uint32_t cons = (r)->cons & SPICE_RING_INDEX_MASK(r);           \
        if (cons >= ARRAY_SIZE((r)->items)) {                           \
            qxl_set_guest_bug(qxl, "SPICE_RING_CONS_ITEM indices mismatch " \
                          "%u >= %zu", cons, ARRAY_SIZE((r)->items));   \
            ret = NULL;                                                 \
        } else {                                                        \
            ret = &(r)->items[cons].el;                                 \
        }                                                               \
    }

#undef ALIGN
#define ALIGN(a, b) (((a) + ((b) - 1)) & ~((b) - 1))

#define PIXEL_SIZE 0.2936875 //1280x1024 is 14.8" x 11.9" 

#define QXL_MODE(_x, _y, _b, _o)                  \
    {   .x_res = _x,                              \
        .y_res = _y,                              \
        .bits  = _b,                              \
        .stride = (_x) * (_b) / 8,                \
        .x_mili = PIXEL_SIZE * (_x),              \
        .y_mili = PIXEL_SIZE * (_y),              \
        .orientation = _o,                        \
    }

#define QXL_MODE_16_32(x_res, y_res, orientation) \
    QXL_MODE(x_res, y_res, 16, orientation),      \
    QXL_MODE(x_res, y_res, 32, orientation)

#define QXL_MODE_EX(x_res, y_res)                 \
    QXL_MODE_16_32(x_res, y_res, 0),              \
    QXL_MODE_16_32(x_res, y_res, 1)

static QXLMode qxl_modes[] = {
    QXL_MODE_EX(640, 480),
    QXL_MODE_EX(800, 480),
    QXL_MODE_EX(800, 600),
    QXL_MODE_EX(832, 624),
    QXL_MODE_EX(960, 640),
    QXL_MODE_EX(1024, 600),
    QXL_MODE_EX(1024, 768),
    QXL_MODE_EX(1152, 864),
    QXL_MODE_EX(1152, 870),
    QXL_MODE_EX(1280, 720),
    QXL_MODE_EX(1280, 760),
    QXL_MODE_EX(1280, 768),
    QXL_MODE_EX(1280, 800),
    QXL_MODE_EX(1280, 960),
    QXL_MODE_EX(1280, 1024),
    QXL_MODE_EX(1360, 768),
    QXL_MODE_EX(1366, 768),
    QXL_MODE_EX(1400, 1050),
    QXL_MODE_EX(1440, 900),
    QXL_MODE_EX(1600, 900),
    QXL_MODE_EX(1600, 1200),
    QXL_MODE_EX(1680, 1050),
    QXL_MODE_EX(1920, 1080),
    /* these modes need more than 8 MB video memory */
    QXL_MODE_EX(1920, 1200),
    QXL_MODE_EX(1920, 1440),
    QXL_MODE_EX(2000, 2000),
    QXL_MODE_EX(2048, 1536),
    QXL_MODE_EX(2048, 2048),
    QXL_MODE_EX(2560, 1440),
    QXL_MODE_EX(2560, 1600),
    /* these modes need more than 16 MB video memory */
    QXL_MODE_EX(2560, 2048),
    QXL_MODE_EX(2800, 2100),
    QXL_MODE_EX(3200, 2400),
    /* these modes need more than 32 MB video memory */
    QXL_MODE_EX(3840, 2160), /* 4k mainstream */
    QXL_MODE_EX(4096, 2160), /* 4k            */
    /* these modes need more than 64 MB video memory */
    QXL_MODE_EX(7680, 4320), /* 8k mainstream */
    /* these modes need more than 128 MB video memory */
    QXL_MODE_EX(8192, 4320), /* 8k            */
};

static void qxl_send_events(PCIQXLDevice *d, uint32_t events);
static int qxl_destroy_primary(PCIQXLDevice *d, qxl_async_io async);
static void qxl_reset_memslots(PCIQXLDevice *d);
static void qxl_reset_surfaces(PCIQXLDevice *d);
static void qxl_ring_set_dirty(PCIQXLDevice *qxl);

static void qxl_hw_update(void *opaque);

void qxl_set_guest_bug(PCIQXLDevice *qxl, const char *msg, ...)
{
    trace_qxl_set_guest_bug(qxl->id);
    qxl_send_events(qxl, QXL_INTERRUPT_ERROR);
    qxl->guest_bug = 1;
    if (qxl->guestdebug) {
        va_list ap;
        va_start(ap, msg);
        fprintf(stderr, "qxl-%d: guest bug: ", qxl->id);
        vfprintf(stderr, msg, ap);
        fprintf(stderr, "\n");
        va_end(ap);
    }
}

static void qxl_clear_guest_bug(PCIQXLDevice *qxl)
{
    qxl->guest_bug = 0;
}

void qxl_spice_update_area(PCIQXLDevice *qxl, uint32_t surface_id,
                           struct QXLRect *area, struct QXLRect *dirty_rects,
                           uint32_t num_dirty_rects,
                           uint32_t clear_dirty_region,
                           qxl_async_io async, struct QXLCookie *cookie)
{
    trace_qxl_spice_update_area(qxl->id, surface_id, area->left, area->right,
                                area->top, area->bottom);
    trace_qxl_spice_update_area_rest(qxl->id, num_dirty_rects,
                                     clear_dirty_region);
    if (async == QXL_SYNC) {
        spice_qxl_update_area(&qxl->ssd.qxl, surface_id, area,
                        dirty_rects, num_dirty_rects, clear_dirty_region);
    } else {
        assert(cookie != NULL);
        spice_qxl_update_area_async(&qxl->ssd.qxl, surface_id, area,
                                    clear_dirty_region, (uintptr_t)cookie);
    }
}

static void qxl_spice_destroy_surface_wait_complete(PCIQXLDevice *qxl,
                                                    uint32_t id)
{
    trace_qxl_spice_destroy_surface_wait_complete(qxl->id, id);
    qemu_mutex_lock(&qxl->track_lock);
    qxl->guest_surfaces.cmds[id] = 0;
    qxl->guest_surfaces.count--;
    qemu_mutex_unlock(&qxl->track_lock);
}

static void qxl_spice_destroy_surface_wait(PCIQXLDevice *qxl, uint32_t id,
                                           qxl_async_io async)
{
    QXLCookie *cookie;

    trace_qxl_spice_destroy_surface_wait(qxl->id, id, async);
    if (async) {
        cookie = qxl_cookie_new(QXL_COOKIE_TYPE_IO,
                                QXL_IO_DESTROY_SURFACE_ASYNC);
        cookie->u.surface_id = id;
        spice_qxl_destroy_surface_async(&qxl->ssd.qxl, id, (uintptr_t)cookie);
    } else {
        spice_qxl_destroy_surface_wait(&qxl->ssd.qxl, id);
        qxl_spice_destroy_surface_wait_complete(qxl, id);
    }
}

static void qxl_spice_flush_surfaces_async(PCIQXLDevice *qxl)
{
    trace_qxl_spice_flush_surfaces_async(qxl->id, qxl->guest_surfaces.count,
                                         qxl->num_free_res);
    spice_qxl_flush_surfaces_async(&qxl->ssd.qxl,
        (uintptr_t)qxl_cookie_new(QXL_COOKIE_TYPE_IO,
                                  QXL_IO_FLUSH_SURFACES_ASYNC));
}

void qxl_spice_loadvm_commands(PCIQXLDevice *qxl, struct QXLCommandExt *ext,
                               uint32_t count)
{
    trace_qxl_spice_loadvm_commands(qxl->id, ext, count);
    spice_qxl_loadvm_commands(&qxl->ssd.qxl, ext, count);
}

void qxl_spice_oom(PCIQXLDevice *qxl)
{
    trace_qxl_spice_oom(qxl->id);
    spice_qxl_oom(&qxl->ssd.qxl);
}

void qxl_spice_reset_memslots(PCIQXLDevice *qxl)
{
    trace_qxl_spice_reset_memslots(qxl->id);
    spice_qxl_reset_memslots(&qxl->ssd.qxl);
}

static void qxl_spice_destroy_surfaces_complete(PCIQXLDevice *qxl)
{
    trace_qxl_spice_destroy_surfaces_complete(qxl->id);
    qemu_mutex_lock(&qxl->track_lock);
    memset(qxl->guest_surfaces.cmds, 0,
           sizeof(qxl->guest_surfaces.cmds[0]) * qxl->ssd.num_surfaces);
    qxl->guest_surfaces.count = 0;
    qemu_mutex_unlock(&qxl->track_lock);
}

static void qxl_spice_destroy_surfaces(PCIQXLDevice *qxl, qxl_async_io async)
{
    trace_qxl_spice_destroy_surfaces(qxl->id, async);
    if (async) {
        spice_qxl_destroy_surfaces_async(&qxl->ssd.qxl,
                (uintptr_t)qxl_cookie_new(QXL_COOKIE_TYPE_IO,
                                          QXL_IO_DESTROY_ALL_SURFACES_ASYNC));
    } else {
        spice_qxl_destroy_surfaces(&qxl->ssd.qxl);
        qxl_spice_destroy_surfaces_complete(qxl);
    }
}

static void qxl_spice_monitors_config_async(PCIQXLDevice *qxl, int replay)
{
    QXLMonitorsConfig *cfg;

    trace_qxl_spice_monitors_config(qxl->id);
    if (replay) {
        /*
         * don't use QXL_COOKIE_TYPE_IO:
         *  - we are not running yet (post_load), we will assert
         *    in send_events
         *  - this is not a guest io, but a reply, so async_io isn't set.
         */
        spice_qxl_monitors_config_async(&qxl->ssd.qxl,
                qxl->guest_monitors_config,
                MEMSLOT_GROUP_GUEST,
                (uintptr_t)qxl_cookie_new(
                    QXL_COOKIE_TYPE_POST_LOAD_MONITORS_CONFIG,
                    0));
    } else {
#if SPICE_SERVER_VERSION < 0x000e02 /* release 0.14.2 */
        if (qxl->max_outputs) {
            spice_qxl_set_max_monitors(&qxl->ssd.qxl, qxl->max_outputs);
        }
#endif
        qxl->guest_monitors_config = qxl->ram->monitors_config;
        spice_qxl_monitors_config_async(&qxl->ssd.qxl,
                qxl->ram->monitors_config,
                MEMSLOT_GROUP_GUEST,
                (uintptr_t)qxl_cookie_new(QXL_COOKIE_TYPE_IO,
                                          QXL_IO_MONITORS_CONFIG_ASYNC));
    }

    cfg = qxl_phys2virt(qxl, qxl->guest_monitors_config, MEMSLOT_GROUP_GUEST,
                        sizeof(QXLMonitorsConfig));
    if (cfg != NULL && cfg->count == 1) {
        qxl->guest_primary.resized = 1;
        qxl->guest_head0_width  = cfg->heads[0].width;
        qxl->guest_head0_height = cfg->heads[0].height;
    } else {
        qxl->guest_head0_width  = 0;
        qxl->guest_head0_height = 0;
    }
}

void qxl_spice_reset_image_cache(PCIQXLDevice *qxl)
{
    trace_qxl_spice_reset_image_cache(qxl->id);
    spice_qxl_reset_image_cache(&qxl->ssd.qxl);
}

void qxl_spice_reset_cursor(PCIQXLDevice *qxl)
{
    trace_qxl_spice_reset_cursor(qxl->id);
    spice_qxl_reset_cursor(&qxl->ssd.qxl);
    qemu_mutex_lock(&qxl->track_lock);
    qxl->guest_cursor = 0;
    qemu_mutex_unlock(&qxl->track_lock);
    if (qxl->ssd.cursor) {
        cursor_unref(qxl->ssd.cursor);
    }
    qxl->ssd.cursor = cursor_builtin_hidden();
}

static uint32_t qxl_crc32(const uint8_t *p, unsigned len)
{
    /*
     * zlib xors the seed with 0xffffffff, and xors the result
     * again with 0xffffffff; Both are not done with linux's crc32,
     * which we want to be compatible with, so undo that.
     */
    return crc32(0xffffffff, p, len) ^ 0xffffffff;
}

static ram_addr_t qxl_rom_size(void)
{
#define QXL_REQUIRED_SZ (sizeof(QXLRom) + sizeof(QXLModes) + sizeof(qxl_modes))
#define QXL_ROM_SZ 8192

    QEMU_BUILD_BUG_ON(QXL_REQUIRED_SZ > QXL_ROM_SZ);
    return QEMU_ALIGN_UP(QXL_REQUIRED_SZ, qemu_real_host_page_size());
}

static void init_qxl_rom(PCIQXLDevice *d)
{
    QXLRom *rom = memory_region_get_ram_ptr(&d->rom_bar);
    QXLModes *modes = (QXLModes *)(rom + 1);
    uint32_t ram_header_size;
    uint32_t surface0_area_size;
    uint32_t num_pages;
    uint32_t fb;
    int i, n;

    memset(rom, 0, d->rom_size);

    rom->magic         = cpu_to_le32(QXL_ROM_MAGIC);
    rom->id            = cpu_to_le32(d->id);
    rom->log_level     = cpu_to_le32(d->guestdebug);
    rom->modes_offset  = cpu_to_le32(sizeof(QXLRom));

    rom->slot_gen_bits = MEMSLOT_GENERATION_BITS;
    rom->slot_id_bits  = MEMSLOT_SLOT_BITS;
    rom->slots_start   = 1;
    rom->slots_end     = NUM_MEMSLOTS - 1;
    rom->n_surfaces    = cpu_to_le32(d->ssd.num_surfaces);

    for (i = 0, n = 0; i < ARRAY_SIZE(qxl_modes); i++) {
        fb = qxl_modes[i].y_res * qxl_modes[i].stride;
        if (fb > d->vgamem_size) {
            continue;
        }
        modes->modes[n].id          = cpu_to_le32(i);
        modes->modes[n].x_res       = cpu_to_le32(qxl_modes[i].x_res);
        modes->modes[n].y_res       = cpu_to_le32(qxl_modes[i].y_res);
        modes->modes[n].bits        = cpu_to_le32(qxl_modes[i].bits);
        modes->modes[n].stride      = cpu_to_le32(qxl_modes[i].stride);
        modes->modes[n].x_mili      = cpu_to_le32(qxl_modes[i].x_mili);
        modes->modes[n].y_mili      = cpu_to_le32(qxl_modes[i].y_mili);
        modes->modes[n].orientation = cpu_to_le32(qxl_modes[i].orientation);
        n++;
    }
    modes->n_modes     = cpu_to_le32(n);

    ram_header_size    = ALIGN(sizeof(QXLRam), 4096);
    surface0_area_size = ALIGN(d->vgamem_size, 4096);
    num_pages          = d->vga.vram_size;
    num_pages         -= ram_header_size;
    num_pages         -= surface0_area_size;
    num_pages          = num_pages / QXL_PAGE_SIZE;

    assert(ram_header_size + surface0_area_size <= d->vga.vram_size);

    rom->draw_area_offset   = cpu_to_le32(0);
    rom->surface0_area_size = cpu_to_le32(surface0_area_size);
    rom->pages_offset       = cpu_to_le32(surface0_area_size);
    rom->num_pages          = cpu_to_le32(num_pages);
    rom->ram_header_offset  = cpu_to_le32(d->vga.vram_size - ram_header_size);

    if (d->xres && d->yres) {
        /* needs linux kernel 4.12+ to work */
        rom->client_monitors_config.count = 1;
        rom->client_monitors_config.heads[0].left = 0;
        rom->client_monitors_config.heads[0].top = 0;
        rom->client_monitors_config.heads[0].right = cpu_to_le32(d->xres);
        rom->client_monitors_config.heads[0].bottom = cpu_to_le32(d->yres);
        rom->client_monitors_config_crc = qxl_crc32(
            (const uint8_t *)&rom->client_monitors_config,
            sizeof(rom->client_monitors_config));
    }

    d->shadow_rom = *rom;
    d->rom        = rom;
    d->modes      = modes;
}

static void init_qxl_ram(PCIQXLDevice *d)
{
    uint8_t *buf;
    uint32_t prod;
    QXLReleaseRing *ring;

    buf = d->vga.vram_ptr;
    d->ram = (QXLRam *)(buf + le32_to_cpu(d->shadow_rom.ram_header_offset));
    d->ram->magic       = cpu_to_le32(QXL_RAM_MAGIC);
    d->ram->int_pending = cpu_to_le32(0);
    d->ram->int_mask    = cpu_to_le32(0);
    d->ram->update_surface = 0;
    d->ram->monitors_config = 0;
    SPICE_RING_INIT(&d->ram->cmd_ring);
    SPICE_RING_INIT(&d->ram->cursor_ring);
    SPICE_RING_INIT(&d->ram->release_ring);

    ring = &d->ram->release_ring;
    prod = ring->prod & SPICE_RING_INDEX_MASK(ring);
    assert(prod < ARRAY_SIZE(ring->items));
    ring->items[prod].el = 0;

    qxl_ring_set_dirty(d);
}

/* can be called from spice server thread context */
static void qxl_set_dirty(MemoryRegion *mr, ram_addr_t addr, ram_addr_t end)
{
    memory_region_set_dirty(mr, addr, end - addr);
}

static void qxl_rom_set_dirty(PCIQXLDevice *qxl)
{
    qxl_set_dirty(&qxl->rom_bar, 0, qxl->rom_size);
}

/* called from spice server thread context only */
static void qxl_ram_set_dirty(PCIQXLDevice *qxl, void *ptr)
{
    void *base = qxl->vga.vram_ptr;
    intptr_t offset;

    offset = ptr - base;
    assert(offset < qxl->vga.vram_size);
    qxl_set_dirty(&qxl->vga.vram, offset, offset + 3);
}

/* can be called from spice server thread context */
static void qxl_ring_set_dirty(PCIQXLDevice *qxl)
{
    ram_addr_t addr = qxl->shadow_rom.ram_header_offset;
    ram_addr_t end  = qxl->vga.vram_size;
    qxl_set_dirty(&qxl->vga.vram, addr, end);
}

/*
 * keep track of some command state, for savevm/loadvm.
 * called from spice server thread context only
 */
static int qxl_track_command(PCIQXLDevice *qxl, struct QXLCommandExt *ext)
{
    switch (le32_to_cpu(ext->cmd.type)) {
    case QXL_CMD_SURFACE:
    {
        QXLSurfaceCmd *cmd = qxl_phys2virt(qxl, ext->cmd.data, ext->group_id,
                                           sizeof(QXLSurfaceCmd));

        if (!cmd) {
            return 1;
        }
        uint32_t id = le32_to_cpu(cmd->surface_id);

        if (id >= qxl->ssd.num_surfaces) {
            qxl_set_guest_bug(qxl, "QXL_CMD_SURFACE id %d >= %d", id,
                              qxl->ssd.num_surfaces);
            return 1;
        }
        if (cmd->type == QXL_SURFACE_CMD_CREATE &&
            (cmd->u.surface_create.stride & 0x03) != 0) {
            qxl_set_guest_bug(qxl, "QXL_CMD_SURFACE stride = %d %% 4 != 0\n",
                              cmd->u.surface_create.stride);
            return 1;
        }
        WITH_QEMU_LOCK_GUARD(&qxl->track_lock) {
            if (cmd->type == QXL_SURFACE_CMD_CREATE) {
                qxl->guest_surfaces.cmds[id] = ext->cmd.data;
                qxl->guest_surfaces.count++;
                if (qxl->guest_surfaces.max < qxl->guest_surfaces.count) {
                    qxl->guest_surfaces.max = qxl->guest_surfaces.count;
                }
            }
            if (cmd->type == QXL_SURFACE_CMD_DESTROY) {
                qxl->guest_surfaces.cmds[id] = 0;
                qxl->guest_surfaces.count--;
            }
        }
        break;
    }
    case QXL_CMD_CURSOR:
    {
        QXLCursorCmd *cmd = qxl_phys2virt(qxl, ext->cmd.data, ext->group_id,
                                          sizeof(QXLCursorCmd));

        if (!cmd) {
            return 1;
        }
        if (cmd->type == QXL_CURSOR_SET) {
            qemu_mutex_lock(&qxl->track_lock);
            qxl->guest_cursor = ext->cmd.data;
            qemu_mutex_unlock(&qxl->track_lock);
        }
        if (cmd->type == QXL_CURSOR_HIDE) {
            qemu_mutex_lock(&qxl->track_lock);
            qxl->guest_cursor = 0;
            qemu_mutex_unlock(&qxl->track_lock);
        }
        break;
    }
    }
    return 0;
}

/* spice display interface callbacks */

static void interface_attached_worker(QXLInstance *sin)
{
    PCIQXLDevice *qxl = container_of(sin, PCIQXLDevice, ssd.qxl);

    trace_qxl_interface_attach_worker(qxl->id);
}

#if !(SPICE_HAS_ATTACHED_WORKER)
static void interface_attach_worker(QXLInstance *sin, QXLWorker *qxl_worker)
{
    interface_attached_worker(sin);
}
#endif

static void interface_set_compression_level(QXLInstance *sin, int level)
{
    PCIQXLDevice *qxl = container_of(sin, PCIQXLDevice, ssd.qxl);

    trace_qxl_interface_set_compression_level(qxl->id, level);
    qxl->shadow_rom.compression_level = cpu_to_le32(level);
    qxl->rom->compression_level = cpu_to_le32(level);
    qxl_rom_set_dirty(qxl);
}

static void interface_get_init_info(QXLInstance *sin, QXLDevInitInfo *info)
{
    PCIQXLDevice *qxl = container_of(sin, PCIQXLDevice, ssd.qxl);

    trace_qxl_interface_get_init_info(qxl->id);
    info->memslot_gen_bits = MEMSLOT_GENERATION_BITS;
    info->memslot_id_bits = MEMSLOT_SLOT_BITS;
    info->num_memslots = NUM_MEMSLOTS;
    info->num_memslots_groups = NUM_MEMSLOTS_GROUPS;
    info->internal_groupslot_id = 0;
    info->qxl_ram_size =
        le32_to_cpu(qxl->shadow_rom.num_pages) << QXL_PAGE_BITS;
    info->n_surfaces = qxl->ssd.num_surfaces;
}

static const char *qxl_mode_to_string(int mode)
{
    switch (mode) {
    case QXL_MODE_COMPAT:
        return "compat";
    case QXL_MODE_NATIVE:
        return "native";
    case QXL_MODE_UNDEFINED:
        return "undefined";
    case QXL_MODE_VGA:
        return "vga";
    }
    return "INVALID";
}

static const char *io_port_to_string(uint32_t io_port)
{
    if (io_port >= QXL_IO_RANGE_SIZE) {
        return "out of range";
    }
    static const char *io_port_to_string[QXL_IO_RANGE_SIZE + 1] = {
        [QXL_IO_NOTIFY_CMD]             = "QXL_IO_NOTIFY_CMD",
        [QXL_IO_NOTIFY_CURSOR]          = "QXL_IO_NOTIFY_CURSOR",
        [QXL_IO_UPDATE_AREA]            = "QXL_IO_UPDATE_AREA",
        [QXL_IO_UPDATE_IRQ]             = "QXL_IO_UPDATE_IRQ",
        [QXL_IO_NOTIFY_OOM]             = "QXL_IO_NOTIFY_OOM",
        [QXL_IO_RESET]                  = "QXL_IO_RESET",
        [QXL_IO_SET_MODE]               = "QXL_IO_SET_MODE",
        [QXL_IO_LOG]                    = "QXL_IO_LOG",
        [QXL_IO_MEMSLOT_ADD]            = "QXL_IO_MEMSLOT_ADD",
        [QXL_IO_MEMSLOT_DEL]            = "QXL_IO_MEMSLOT_DEL",
        [QXL_IO_DETACH_PRIMARY]         = "QXL_IO_DETACH_PRIMARY",
        [QXL_IO_ATTACH_PRIMARY]         = "QXL_IO_ATTACH_PRIMARY",
        [QXL_IO_CREATE_PRIMARY]         = "QXL_IO_CREATE_PRIMARY",
        [QXL_IO_DESTROY_PRIMARY]        = "QXL_IO_DESTROY_PRIMARY",
        [QXL_IO_DESTROY_SURFACE_WAIT]   = "QXL_IO_DESTROY_SURFACE_WAIT",
        [QXL_IO_DESTROY_ALL_SURFACES]   = "QXL_IO_DESTROY_ALL_SURFACES",
        [QXL_IO_UPDATE_AREA_ASYNC]      = "QXL_IO_UPDATE_AREA_ASYNC",
        [QXL_IO_MEMSLOT_ADD_ASYNC]      = "QXL_IO_MEMSLOT_ADD_ASYNC",
        [QXL_IO_CREATE_PRIMARY_ASYNC]   = "QXL_IO_CREATE_PRIMARY_ASYNC",
        [QXL_IO_DESTROY_PRIMARY_ASYNC]  = "QXL_IO_DESTROY_PRIMARY_ASYNC",
        [QXL_IO_DESTROY_SURFACE_ASYNC]  = "QXL_IO_DESTROY_SURFACE_ASYNC",
        [QXL_IO_DESTROY_ALL_SURFACES_ASYNC]
                                        = "QXL_IO_DESTROY_ALL_SURFACES_ASYNC",
        [QXL_IO_FLUSH_SURFACES_ASYNC]   = "QXL_IO_FLUSH_SURFACES_ASYNC",
        [QXL_IO_FLUSH_RELEASE]          = "QXL_IO_FLUSH_RELEASE",
        [QXL_IO_MONITORS_CONFIG_ASYNC]  = "QXL_IO_MONITORS_CONFIG_ASYNC",
    };
    return io_port_to_string[io_port];
}

/* called from spice server thread context only */
static int interface_get_command(QXLInstance *sin, struct QXLCommandExt *ext)
{
    PCIQXLDevice *qxl = container_of(sin, PCIQXLDevice, ssd.qxl);
    SimpleSpiceUpdate *update;
    QXLCommandRing *ring;
    QXLCommand *cmd;
    int notify, ret;

    trace_qxl_ring_command_check(qxl->id, qxl_mode_to_string(qxl->mode));

    switch (qxl->mode) {
    case QXL_MODE_VGA:
        ret = false;
        qemu_mutex_lock(&qxl->ssd.lock);
        update = QTAILQ_FIRST(&qxl->ssd.updates);
        if (update != NULL) {
            QTAILQ_REMOVE(&qxl->ssd.updates, update, next);
            *ext = update->ext;
            ret = true;
        }
        qemu_mutex_unlock(&qxl->ssd.lock);
        if (ret) {
            trace_qxl_ring_command_get(qxl->id, qxl_mode_to_string(qxl->mode));
            qxl_log_command(qxl, "vga", ext);
        }
        return ret;
    case QXL_MODE_COMPAT:
    case QXL_MODE_NATIVE:
    case QXL_MODE_UNDEFINED:
        ring = &qxl->ram->cmd_ring;
        if (qxl->guest_bug || SPICE_RING_IS_EMPTY(ring)) {
            return false;
        }
        SPICE_RING_CONS_ITEM(qxl, ring, cmd);
        if (!cmd) {
            return false;
        }
        ext->cmd      = *cmd;
        ext->group_id = MEMSLOT_GROUP_GUEST;
        ext->flags    = qxl->cmdflags;
        SPICE_RING_POP(ring, notify);
        qxl_ring_set_dirty(qxl);
        if (notify) {
            qxl_send_events(qxl, QXL_INTERRUPT_DISPLAY);
        }
        qxl->guest_primary.commands++;
        qxl_track_command(qxl, ext);
        qxl_log_command(qxl, "cmd", ext);
        trace_qxl_ring_command_get(qxl->id, qxl_mode_to_string(qxl->mode));
        return true;
    default:
        return false;
    }
}

/* called from spice server thread context only */
static int interface_req_cmd_notification(QXLInstance *sin)
{
    PCIQXLDevice *qxl = container_of(sin, PCIQXLDevice, ssd.qxl);
    int wait = 1;

    trace_qxl_ring_command_req_notification(qxl->id);
    switch (qxl->mode) {
    case QXL_MODE_COMPAT:
    case QXL_MODE_NATIVE:
    case QXL_MODE_UNDEFINED:
        SPICE_RING_CONS_WAIT(&qxl->ram->cmd_ring, wait);
        qxl_ring_set_dirty(qxl);
        break;
    default:
        /* nothing */
        break;
    }
    return wait;
}

/* called from spice server thread context only */
static inline void qxl_push_free_res(PCIQXLDevice *d, int flush)
{
    QXLReleaseRing *ring = &d->ram->release_ring;
    uint32_t prod;
    int notify;

#define QXL_FREE_BUNCH_SIZE 32

    if (ring->prod - ring->cons + 1 == ring->num_items) {
        /* ring full -- can't push */
        return;
    }
    if (!flush && d->oom_running) {
        /* collect everything from oom handler before pushing */
        return;
    }
    if (!flush && d->num_free_res < QXL_FREE_BUNCH_SIZE) {
        /* collect a bit more before pushing */
        return;
    }

    SPICE_RING_PUSH(ring, notify);
    trace_qxl_ring_res_push(d->id, qxl_mode_to_string(d->mode),
           d->guest_surfaces.count, d->num_free_res,
           d->last_release, notify ? "yes" : "no");
    trace_qxl_ring_res_push_rest(d->id, ring->prod - ring->cons,
           ring->num_items, ring->prod, ring->cons);
    if (notify) {
        qxl_send_events(d, QXL_INTERRUPT_DISPLAY);
    }

    ring = &d->ram->release_ring;
    prod = ring->prod & SPICE_RING_INDEX_MASK(ring);
    if (prod >= ARRAY_SIZE(ring->items)) {
        qxl_set_guest_bug(d, "SPICE_RING_PROD_ITEM indices mismatch "
                          "%u >= %zu", prod, ARRAY_SIZE(ring->items));
        return;
    }
    ring->items[prod].el = 0;
    d->num_free_res = 0;
    d->last_release = NULL;
    qxl_ring_set_dirty(d);
}

/* called from spice server thread context only */
static void interface_release_resource(QXLInstance *sin,
                                       QXLReleaseInfoExt ext)
{
    PCIQXLDevice *qxl = container_of(sin, PCIQXLDevice, ssd.qxl);
    QXLReleaseRing *ring;
    uint32_t prod;
    uint64_t id;

    if (!ext.info) {
        return;
    }
    if (ext.group_id == MEMSLOT_GROUP_HOST) {
        /* host group -> vga mode update request */
        QXLCommandExt *cmdext = (void *)(intptr_t)(ext.info->id);
        SimpleSpiceUpdate *update;
        g_assert(cmdext->cmd.type == QXL_CMD_DRAW);
        update = container_of(cmdext, SimpleSpiceUpdate, ext);
        qemu_spice_destroy_update(&qxl->ssd, update);
        return;
    }

    /*
     * ext->info points into guest-visible memory
     * pci bar 0, $command.release_info
     */
    ring = &qxl->ram->release_ring;
    prod = ring->prod & SPICE_RING_INDEX_MASK(ring);
    if (prod >= ARRAY_SIZE(ring->items)) {
        qxl_set_guest_bug(qxl, "SPICE_RING_PROD_ITEM indices mismatch "
                          "%u >= %zu", prod, ARRAY_SIZE(ring->items));
        return;
    }
    if (ring->items[prod].el == 0) {
        /* stick head into the ring */
        id = ext.info->id;
        ext.info->next = 0;
        qxl_ram_set_dirty(qxl, &ext.info->next);
        ring->items[prod].el = id;
        qxl_ring_set_dirty(qxl);
    } else {
        /* append item to the list */
        qxl->last_release->next = ext.info->id;
        qxl_ram_set_dirty(qxl, &qxl->last_release->next);
        ext.info->next = 0;
        qxl_ram_set_dirty(qxl, &ext.info->next);
    }
    qxl->last_release = ext.info;
    qxl->num_free_res++;
    trace_qxl_ring_res_put(qxl->id, qxl->num_free_res);
    qxl_push_free_res(qxl, 0);
}

/* called from spice server thread context only */
static int interface_get_cursor_command(QXLInstance *sin, struct QXLCommandExt *ext)
{
    PCIQXLDevice *qxl = container_of(sin, PCIQXLDevice, ssd.qxl);
    QXLCursorRing *ring;
    QXLCommand *cmd;
    int notify;

    trace_qxl_ring_cursor_check(qxl->id, qxl_mode_to_string(qxl->mode));

    switch (qxl->mode) {
    case QXL_MODE_COMPAT:
    case QXL_MODE_NATIVE:
    case QXL_MODE_UNDEFINED:
        ring = &qxl->ram->cursor_ring;
        if (SPICE_RING_IS_EMPTY(ring)) {
            return false;
        }
        SPICE_RING_CONS_ITEM(qxl, ring, cmd);
        if (!cmd) {
            return false;
        }
        ext->cmd      = *cmd;
        ext->group_id = MEMSLOT_GROUP_GUEST;
        ext->flags    = qxl->cmdflags;
        SPICE_RING_POP(ring, notify);
        qxl_ring_set_dirty(qxl);
        if (notify) {
            qxl_send_events(qxl, QXL_INTERRUPT_CURSOR);
        }
        qxl->guest_primary.commands++;
        qxl_track_command(qxl, ext);
        qxl_log_command(qxl, "csr", ext);
        if (qxl->have_vga) {
            qxl_render_cursor(qxl, ext);
        }
        trace_qxl_ring_cursor_get(qxl->id, qxl_mode_to_string(qxl->mode));
        return true;
    default:
        return false;
    }
}

/* called from spice server thread context only */
static int interface_req_cursor_notification(QXLInstance *sin)
{
    PCIQXLDevice *qxl = container_of(sin, PCIQXLDevice, ssd.qxl);
    int wait = 1;

    trace_qxl_ring_cursor_req_notification(qxl->id);
    switch (qxl->mode) {
    case QXL_MODE_COMPAT:
    case QXL_MODE_NATIVE:
    case QXL_MODE_UNDEFINED:
        SPICE_RING_CONS_WAIT(&qxl->ram->cursor_ring, wait);
        qxl_ring_set_dirty(qxl);
        break;
    default:
        /* nothing */
        break;
    }
    return wait;
}

/* called from spice server thread context */
static void interface_notify_update(QXLInstance *sin, uint32_t update_id)
{
    /*
     * Called by spice-server as a result of a QXL_CMD_UPDATE which is not in
     * use by xf86-video-qxl and is defined out in the qxl windows driver.
     * Probably was at some earlier version that is prior to git start (2009),
     * and is still guest trigerrable.
     */
    fprintf(stderr, "%s: deprecated\n", __func__);
}

/* called from spice server thread context only */
static int interface_flush_resources(QXLInstance *sin)
{
    PCIQXLDevice *qxl = container_of(sin, PCIQXLDevice, ssd.qxl);
    int ret;

    ret = qxl->num_free_res;
    if (ret) {
        qxl_push_free_res(qxl, 1);
    }
    return ret;
}

static void qxl_create_guest_primary_complete(PCIQXLDevice *d);

/* called from spice server thread context only */
static void interface_async_complete_io(PCIQXLDevice *qxl, QXLCookie *cookie)
{
    uint32_t current_async;

    qemu_mutex_lock(&qxl->async_lock);
    current_async = qxl->current_async;
    qxl->current_async = QXL_UNDEFINED_IO;
    qemu_mutex_unlock(&qxl->async_lock);

    trace_qxl_interface_async_complete_io(qxl->id, current_async, cookie);
    if (!cookie) {
        fprintf(stderr, "qxl: %s: error, cookie is NULL\n", __func__);
        return;
    }
    if (cookie && current_async != cookie->io) {
        fprintf(stderr,
                "qxl: %s: error: current_async = %d != %"
                PRId64 " = cookie->io\n", __func__, current_async, cookie->io);
    }
    switch (current_async) {
    case QXL_IO_MEMSLOT_ADD_ASYNC:
    case QXL_IO_DESTROY_PRIMARY_ASYNC:
    case QXL_IO_UPDATE_AREA_ASYNC:
    case QXL_IO_FLUSH_SURFACES_ASYNC:
    case QXL_IO_MONITORS_CONFIG_ASYNC:
        break;
    case QXL_IO_CREATE_PRIMARY_ASYNC:
        qxl_create_guest_primary_complete(qxl);
        break;
    case QXL_IO_DESTROY_ALL_SURFACES_ASYNC:
        qxl_spice_destroy_surfaces_complete(qxl);
        break;
    case QXL_IO_DESTROY_SURFACE_ASYNC:
        qxl_spice_destroy_surface_wait_complete(qxl, cookie->u.surface_id);
        break;
    default:
        fprintf(stderr, "qxl: %s: unexpected current_async %u\n", __func__,
                current_async);
    }
    qxl_send_events(qxl, QXL_INTERRUPT_IO_CMD);
}

/* called from spice server thread context only */
static void interface_update_area_complete(QXLInstance *sin,
        uint32_t surface_id,
        QXLRect *dirty, uint32_t num_updated_rects)
{
    PCIQXLDevice *qxl = container_of(sin, PCIQXLDevice, ssd.qxl);
    int i;
    int qxl_i;

    QEMU_LOCK_GUARD(&qxl->ssd.lock);
    if (surface_id != 0 || !num_updated_rects ||
        !qxl->render_update_cookie_num) {
        return;
    }
    trace_qxl_interface_update_area_complete(qxl->id, surface_id, dirty->left,
            dirty->right, dirty->top, dirty->bottom);
    trace_qxl_interface_update_area_complete_rest(qxl->id, num_updated_rects);
    if (qxl->num_dirty_rects + num_updated_rects > QXL_NUM_DIRTY_RECTS) {
        /*
         * overflow - treat this as a full update. Not expected to be common.
         */
        trace_qxl_interface_update_area_complete_overflow(qxl->id,
                                                          QXL_NUM_DIRTY_RECTS);
        qxl->guest_primary.resized = 1;
    }
    if (qxl->guest_primary.resized) {
        /*
         * Don't bother copying or scheduling the bh since we will flip
         * the whole area anyway on completion of the update_area async call
         */
        return;
    }
    qxl_i = qxl->num_dirty_rects;
    for (i = 0; i < num_updated_rects; i++) {
        qxl->dirty[qxl_i++] = dirty[i];
    }
    qxl->num_dirty_rects += num_updated_rects;
    trace_qxl_interface_update_area_complete_schedule_bh(qxl->id,
                                                         qxl->num_dirty_rects);
    qemu_bh_schedule(qxl->update_area_bh);
}

/* called from spice server thread context only */
static void interface_async_complete(QXLInstance *sin, uint64_t cookie_token)
{
    PCIQXLDevice *qxl = container_of(sin, PCIQXLDevice, ssd.qxl);
    QXLCookie *cookie = (QXLCookie *)(uintptr_t)cookie_token;

    switch (cookie->type) {
    case QXL_COOKIE_TYPE_IO:
        interface_async_complete_io(qxl, cookie);
        g_free(cookie);
        break;
    case QXL_COOKIE_TYPE_RENDER_UPDATE_AREA:
        qxl_render_update_area_done(qxl, cookie);
        break;
    case QXL_COOKIE_TYPE_POST_LOAD_MONITORS_CONFIG:
        break;
    default:
        fprintf(stderr, "qxl: %s: unexpected cookie type %d\n",
                __func__, cookie->type);
        g_free(cookie);
    }
}

/* called from spice server thread context only */
static void interface_set_client_capabilities(QXLInstance *sin,
                                              uint8_t client_present,
                                              uint8_t caps[58])
{
    PCIQXLDevice *qxl = container_of(sin, PCIQXLDevice, ssd.qxl);

    if (qxl->revision < 4) {
        trace_qxl_set_client_capabilities_unsupported_by_revision(qxl->id,
                                                              qxl->revision);
        return;
    }

    if (runstate_check(RUN_STATE_INMIGRATE) ||
        runstate_check(RUN_STATE_POSTMIGRATE)) {
        return;
    }

    qxl->shadow_rom.client_present = client_present;
    memcpy(qxl->shadow_rom.client_capabilities, caps,
           sizeof(qxl->shadow_rom.client_capabilities));
    qxl->rom->client_present = client_present;
    memcpy(qxl->rom->client_capabilities, caps,
           sizeof(qxl->rom->client_capabilities));
    qxl_rom_set_dirty(qxl);

    qxl_send_events(qxl, QXL_INTERRUPT_CLIENT);
}

static bool qxl_rom_monitors_config_changed(QXLRom *rom,
        VDAgentMonitorsConfig *monitors_config,
        unsigned int max_outputs)
{
    int i;
    unsigned int monitors_count;

    monitors_count = MIN(monitors_config->num_of_monitors, max_outputs);

    if (rom->client_monitors_config.count != monitors_count) {
        return true;
    }

    for (i = 0 ; i < rom->client_monitors_config.count ; ++i) {
        VDAgentMonConfig *monitor = &monitors_config->monitors[i];
        QXLURect *rect = &rom->client_monitors_config.heads[i];
        /* monitor->depth ignored */
        if ((rect->left != monitor->x) ||
            (rect->top != monitor->y)  ||
            (rect->right != monitor->x + monitor->width) ||
            (rect->bottom != monitor->y + monitor->height)) {
            return true;
        }
    }

    return false;
}

/* called from main context only */
static int interface_client_monitors_config(QXLInstance *sin,
                                        VDAgentMonitorsConfig *monitors_config)
{
    PCIQXLDevice *qxl = container_of(sin, PCIQXLDevice, ssd.qxl);
    QXLRom *rom = memory_region_get_ram_ptr(&qxl->rom_bar);
    int i;
    unsigned max_outputs = ARRAY_SIZE(rom->client_monitors_config.heads);
    bool config_changed = false;

    if (qxl->revision < 4) {
        trace_qxl_client_monitors_config_unsupported_by_device(qxl->id,
                                                               qxl->revision);
        return 0;
    }
    /*
     * Older windows drivers set int_mask to 0 when their ISR is called,
     * then later set it to ~0. So it doesn't relate to the actual interrupts
     * handled. However, they are old, so clearly they don't support this
     * interrupt
     */
    if (qxl->ram->int_mask == 0 || qxl->ram->int_mask == ~0 ||
        !(qxl->ram->int_mask & QXL_INTERRUPT_CLIENT_MONITORS_CONFIG)) {
        trace_qxl_client_monitors_config_unsupported_by_guest(qxl->id,
                                                            qxl->ram->int_mask,
                                                            monitors_config);
        return 0;
    }
    if (!monitors_config) {
        return 1;
    }

    /* limit number of outputs based on setting limit */
    if (qxl->max_outputs && qxl->max_outputs <= max_outputs) {
        max_outputs = qxl->max_outputs;
    }

    config_changed = qxl_rom_monitors_config_changed(rom,
                                                     monitors_config,
                                                     max_outputs);

    memset(&rom->client_monitors_config, 0,
           sizeof(rom->client_monitors_config));
    rom->client_monitors_config.count = monitors_config->num_of_monitors;
    /* monitors_config->flags ignored */
    if (rom->client_monitors_config.count >= max_outputs) {
        trace_qxl_client_monitors_config_capped(qxl->id,
                                monitors_config->num_of_monitors,
                                max_outputs);
        rom->client_monitors_config.count = max_outputs;
    }
    for (i = 0 ; i < rom->client_monitors_config.count ; ++i) {
        VDAgentMonConfig *monitor = &monitors_config->monitors[i];
        QXLURect *rect = &rom->client_monitors_config.heads[i];
        /* monitor->depth ignored */
        rect->left = monitor->x;
        rect->top = monitor->y;
        rect->right = monitor->x + monitor->width;
        rect->bottom = monitor->y + monitor->height;
    }
    rom->client_monitors_config_crc = qxl_crc32(
            (const uint8_t *)&rom->client_monitors_config,
            sizeof(rom->client_monitors_config));
    trace_qxl_client_monitors_config_crc(qxl->id,
            sizeof(rom->client_monitors_config),
            rom->client_monitors_config_crc);

    trace_qxl_interrupt_client_monitors_config(qxl->id,
                        rom->client_monitors_config.count,
                        rom->client_monitors_config.heads);
    if (config_changed) {
        qxl_send_events(qxl, QXL_INTERRUPT_CLIENT_MONITORS_CONFIG);
    }
    return 1;
}

static const QXLInterface qxl_interface = {
    .base.type               = SPICE_INTERFACE_QXL,
    .base.description        = "qxl gpu",
    .base.major_version      = SPICE_INTERFACE_QXL_MAJOR,
    .base.minor_version      = SPICE_INTERFACE_QXL_MINOR,

#if SPICE_HAS_ATTACHED_WORKER
    .attached_worker         = interface_attached_worker,
#else
    .attache_worker          = interface_attach_worker,
#endif

    .set_compression_level   = interface_set_compression_level,
    .get_init_info           = interface_get_init_info,

    /* the callbacks below are called from spice server thread context */
    .get_command             = interface_get_command,
    .req_cmd_notification    = interface_req_cmd_notification,
    .release_resource        = interface_release_resource,
    .get_cursor_command      = interface_get_cursor_command,
    .req_cursor_notification = interface_req_cursor_notification,
    .notify_update           = interface_notify_update,
    .flush_resources         = interface_flush_resources,
    .async_complete          = interface_async_complete,
    .update_area_complete    = interface_update_area_complete,
    .set_client_capabilities = interface_set_client_capabilities,
    .client_monitors_config = interface_client_monitors_config,
};

static const GraphicHwOps qxl_ops = {
    .gfx_update  = qxl_hw_update,
    .gfx_update_async = true,
};

static void qxl_enter_vga_mode(PCIQXLDevice *d)
{
    if (d->mode == QXL_MODE_VGA) {
        return;
    }
    trace_qxl_enter_vga_mode(d->id);
    spice_qxl_driver_unload(&d->ssd.qxl);
    graphic_console_set_hwops(d->ssd.dcl.con, d->vga.hw_ops, &d->vga);
    update_displaychangelistener(&d->ssd.dcl, GUI_REFRESH_INTERVAL_DEFAULT);
    qemu_spice_create_host_primary(&d->ssd);
    d->mode = QXL_MODE_VGA;
    qemu_spice_display_switch(&d->ssd, d->ssd.ds);
    vga_dirty_log_start(&d->vga);
    graphic_hw_update(d->vga.con);
}

static void qxl_exit_vga_mode(PCIQXLDevice *d)
{
    if (d->mode != QXL_MODE_VGA) {
        return;
    }
    trace_qxl_exit_vga_mode(d->id);
    graphic_console_set_hwops(d->ssd.dcl.con, &qxl_ops, d);
    update_displaychangelistener(&d->ssd.dcl, GUI_REFRESH_INTERVAL_IDLE);
    vga_dirty_log_stop(&d->vga);
    qxl_destroy_primary(d, QXL_SYNC);
}

static void qxl_update_irq(PCIQXLDevice *d)
{
    uint32_t pending = le32_to_cpu(d->ram->int_pending);
    uint32_t mask    = le32_to_cpu(d->ram->int_mask);
    int level = !!(pending & mask);
    pci_set_irq(&d->pci, level);
    qxl_ring_set_dirty(d);
}

static void qxl_check_state(PCIQXLDevice *d)
{
    QXLRam *ram = d->ram;
    int spice_display_running = qemu_spice_display_is_running(&d->ssd);

    assert(!spice_display_running || SPICE_RING_IS_EMPTY(&ram->cmd_ring));
    assert(!spice_display_running || SPICE_RING_IS_EMPTY(&ram->cursor_ring));
}

static void qxl_reset_state(PCIQXLDevice *d)
{
    QXLRom *rom = d->rom;

    qxl_check_state(d);
    d->shadow_rom.update_id = cpu_to_le32(0);
    *rom = d->shadow_rom;
    qxl_rom_set_dirty(d);
    init_qxl_ram(d);
    d->num_free_res = 0;
    d->last_release = NULL;
    memset(&d->ssd.dirty, 0, sizeof(d->ssd.dirty));
    qxl_update_irq(d);
}

static void qxl_soft_reset(PCIQXLDevice *d)
{
    trace_qxl_soft_reset(d->id);
    qxl_check_state(d);
    qxl_clear_guest_bug(d);
    qemu_mutex_lock(&d->async_lock);
    d->current_async = QXL_UNDEFINED_IO;
    qemu_mutex_unlock(&d->async_lock);

    if (d->have_vga) {
        qxl_enter_vga_mode(d);
    } else {
        d->mode = QXL_MODE_UNDEFINED;
    }
}

static void qxl_hard_reset(PCIQXLDevice *d, int loadvm)
{
    bool startstop = qemu_spice_display_is_running(&d->ssd);

    trace_qxl_hard_reset(d->id, loadvm);

    if (startstop) {
        qemu_spice_display_stop();
    }

    qxl_spice_reset_cursor(d);
    qxl_spice_reset_image_cache(d);
    qxl_reset_surfaces(d);
    qxl_reset_memslots(d);

    /* pre loadvm reset must not touch QXLRam.  This lives in
     * device memory, is migrated together with RAM and thus
     * already loaded at this point */
    if (!loadvm) {
        qxl_reset_state(d);
    }
    qemu_spice_create_host_memslot(&d->ssd);
    qxl_soft_reset(d);

    if (startstop) {
        qemu_spice_display_start();
    }
}

static void qxl_reset_handler(DeviceState *dev)
{
    PCIQXLDevice *d = PCI_QXL(PCI_DEVICE(dev));

    qxl_hard_reset(d, 0);
}

static void qxl_vga_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    VGACommonState *vga = opaque;
    PCIQXLDevice *qxl = container_of(vga, PCIQXLDevice, vga);

    trace_qxl_io_write_vga(qxl->id, qxl_mode_to_string(qxl->mode), addr, val);
    if (qxl->mode != QXL_MODE_VGA &&
        qxl->revision <= QXL_REVISION_STABLE_V12) {
        qxl_destroy_primary(qxl, QXL_SYNC);
        qxl_soft_reset(qxl);
    }
    vga_ioport_write(opaque, addr, val);
}

static const MemoryRegionPortio qxl_vga_portio_list[] = {
    { 0x04,  2, 1, .read  = vga_ioport_read,
                   .write = qxl_vga_ioport_write }, /* 3b4 */
    { 0x0a,  1, 1, .read  = vga_ioport_read,
                   .write = qxl_vga_ioport_write }, /* 3ba */
    { 0x10, 16, 1, .read  = vga_ioport_read,
                   .write = qxl_vga_ioport_write }, /* 3c0 */
    { 0x24,  2, 1, .read  = vga_ioport_read,
                   .write = qxl_vga_ioport_write }, /* 3d4 */
    { 0x2a,  1, 1, .read  = vga_ioport_read,
                   .write = qxl_vga_ioport_write }, /* 3da */
    PORTIO_END_OF_LIST(),
};

static int qxl_add_memslot(PCIQXLDevice *d, uint32_t slot_id, uint64_t delta,
                           qxl_async_io async)
{
    static const int regions[] = {
        QXL_RAM_RANGE_INDEX,
        QXL_VRAM_RANGE_INDEX,
        QXL_VRAM64_RANGE_INDEX,
    };
    uint64_t guest_start;
    uint64_t guest_end;
    int pci_region;
    pcibus_t pci_start;
    pcibus_t pci_end;
    MemoryRegion *mr;
    intptr_t virt_start;
    QXLDevMemSlot memslot;
    int i;

    guest_start = le64_to_cpu(d->guest_slots[slot_id].slot.mem_start);
    guest_end   = le64_to_cpu(d->guest_slots[slot_id].slot.mem_end);

    trace_qxl_memslot_add_guest(d->id, slot_id, guest_start, guest_end);

    if (slot_id >= NUM_MEMSLOTS) {
        qxl_set_guest_bug(d, "%s: slot_id >= NUM_MEMSLOTS %d >= %d", __func__,
                      slot_id, NUM_MEMSLOTS);
        return 1;
    }
    if (guest_start > guest_end) {
        qxl_set_guest_bug(d, "%s: guest_start > guest_end 0x%" PRIx64
                         " > 0x%" PRIx64, __func__, guest_start, guest_end);
        return 1;
    }

    for (i = 0; i < ARRAY_SIZE(regions); i++) {
        pci_region = regions[i];
        pci_start = d->pci.io_regions[pci_region].addr;
        pci_end = pci_start + d->pci.io_regions[pci_region].size;
        /* mapped? */
        if (pci_start == -1) {
            continue;
        }
        /* start address in range ? */
        if (guest_start < pci_start || guest_start > pci_end) {
            continue;
        }
        /* end address in range ? */
        if (guest_end > pci_end) {
            continue;
        }
        /* passed */
        break;
    }
    if (i == ARRAY_SIZE(regions)) {
        qxl_set_guest_bug(d, "%s: finished loop without match", __func__);
        return 1;
    }

    switch (pci_region) {
    case QXL_RAM_RANGE_INDEX:
        mr = &d->vga.vram;
        break;
    case QXL_VRAM_RANGE_INDEX:
    case 4 /* vram 64bit */:
        mr = &d->vram_bar;
        break;
    default:
        /* should not happen */
        qxl_set_guest_bug(d, "%s: pci_region = %d", __func__, pci_region);
        return 1;
    }
    assert(guest_end - pci_start <= memory_region_size(mr));

    virt_start = (intptr_t)memory_region_get_ram_ptr(mr);
    memslot.slot_id = slot_id;
    memslot.slot_group_id = MEMSLOT_GROUP_GUEST; /* guest group */
    memslot.virt_start = virt_start + (guest_start - pci_start);
    memslot.virt_end   = virt_start + (guest_end   - pci_start);
    memslot.addr_delta = memslot.virt_start - delta;
    memslot.generation = d->rom->slot_generation = 0;
    qxl_rom_set_dirty(d);

    qemu_spice_add_memslot(&d->ssd, &memslot, async);
    d->guest_slots[slot_id].mr = mr;
    d->guest_slots[slot_id].offset = memslot.virt_start - virt_start;
    d->guest_slots[slot_id].size = memslot.virt_end - memslot.virt_start;
    d->guest_slots[slot_id].delta = delta;
    d->guest_slots[slot_id].active = 1;
    return 0;
}

static void qxl_del_memslot(PCIQXLDevice *d, uint32_t slot_id)
{
    qemu_spice_del_memslot(&d->ssd, MEMSLOT_GROUP_HOST, slot_id);
    d->guest_slots[slot_id].active = 0;
}

static void qxl_reset_memslots(PCIQXLDevice *d)
{
    qxl_spice_reset_memslots(d);
    memset(&d->guest_slots, 0, sizeof(d->guest_slots));
}

static void qxl_reset_surfaces(PCIQXLDevice *d)
{
    trace_qxl_reset_surfaces(d->id);
    d->mode = QXL_MODE_UNDEFINED;
    qxl_spice_destroy_surfaces(d, QXL_SYNC);
}

/* can be also called from spice server thread context */
static bool qxl_get_check_slot_offset(PCIQXLDevice *qxl, QXLPHYSICAL pqxl,
                                      uint32_t *s, uint64_t *o,
                                      size_t size_requested)
{
    uint64_t phys   = le64_to_cpu(pqxl);
    uint32_t slot   = (phys >> (64 -  8)) & 0xff;
    uint64_t offset = phys & 0xffffffffffff;
    uint64_t size_available;

    if (slot >= NUM_MEMSLOTS) {
        qxl_set_guest_bug(qxl, "slot too large %d >= %d", slot,
                          NUM_MEMSLOTS);
        return false;
    }
    if (!qxl->guest_slots[slot].active) {
        qxl_set_guest_bug(qxl, "inactive slot %d\n", slot);
        return false;
    }
    if (offset < qxl->guest_slots[slot].delta) {
        qxl_set_guest_bug(qxl,
                          "slot %d offset %"PRIu64" < delta %"PRIu64"\n",
                          slot, offset, qxl->guest_slots[slot].delta);
        return false;
    }
    offset -= qxl->guest_slots[slot].delta;
    if (offset > qxl->guest_slots[slot].size) {
        qxl_set_guest_bug(qxl,
                          "slot %d offset %"PRIu64" > size %"PRIu64"\n",
                          slot, offset, qxl->guest_slots[slot].size);
        return false;
    }
    size_available = memory_region_size(qxl->guest_slots[slot].mr);
    if (qxl->guest_slots[slot].offset + offset >= size_available) {
        qxl_set_guest_bug(qxl,
                          "slot %d offset %"PRIu64" > region size %"PRIu64"\n",
                          slot, qxl->guest_slots[slot].offset + offset,
                          size_available);
        return false;
    }
    size_available -= qxl->guest_slots[slot].offset + offset;
    if (size_requested > size_available) {
        qxl_set_guest_bug(qxl,
                          "slot %d offset %"PRIu64" size %zu: "
                          "overrun by %"PRIu64" bytes\n",
                          slot, offset, size_requested,
                          size_requested - size_available);
        return false;
    }

    *s = slot;
    *o = offset;
    return true;
}

/* can be also called from spice server thread context */
void *qxl_phys2virt(PCIQXLDevice *qxl, QXLPHYSICAL pqxl, int group_id,
                    size_t size)
{
    uint64_t offset;
    uint32_t slot;
    void *ptr;

    switch (group_id) {
    case MEMSLOT_GROUP_HOST:
        offset = le64_to_cpu(pqxl) & 0xffffffffffff;
        return (void *)(intptr_t)offset;
    case MEMSLOT_GROUP_GUEST:
        if (!qxl_get_check_slot_offset(qxl, pqxl, &slot, &offset, size)) {
            return NULL;
        }
        ptr = memory_region_get_ram_ptr(qxl->guest_slots[slot].mr);
        ptr += qxl->guest_slots[slot].offset;
        ptr += offset;
        return ptr;
    }
    return NULL;
}

static void qxl_create_guest_primary_complete(PCIQXLDevice *qxl)
{
    /* for local rendering */
    qxl_render_resize(qxl);
}

static void qxl_create_guest_primary(PCIQXLDevice *qxl, int loadvm,
                                     qxl_async_io async)
{
    QXLDevSurfaceCreate surface;
    QXLSurfaceCreate *sc = &qxl->guest_primary.surface;
    uint32_t requested_height = le32_to_cpu(sc->height);
    int requested_stride = le32_to_cpu(sc->stride);

    if (requested_stride == INT32_MIN ||
        abs(requested_stride) * (uint64_t)requested_height
                                        > qxl->vgamem_size) {
        qxl_set_guest_bug(qxl, "%s: requested primary larger than framebuffer"
                               " stride %d x height %" PRIu32 " > %" PRIu32,
                               __func__, requested_stride, requested_height,
                               qxl->vgamem_size);
        return;
    }

    if (qxl->mode == QXL_MODE_NATIVE) {
        qxl_set_guest_bug(qxl, "%s: nop since already in QXL_MODE_NATIVE",
                      __func__);
    }
    qxl_exit_vga_mode(qxl);

    surface.format     = le32_to_cpu(sc->format);
    surface.height     = le32_to_cpu(sc->height);
    surface.mem        = le64_to_cpu(sc->mem);
    surface.position   = le32_to_cpu(sc->position);
    surface.stride     = le32_to_cpu(sc->stride);
    surface.width      = le32_to_cpu(sc->width);
    surface.type       = le32_to_cpu(sc->type);
    surface.flags      = le32_to_cpu(sc->flags);
    trace_qxl_create_guest_primary(qxl->id, sc->width, sc->height, sc->mem,
                                   sc->format, sc->position);
    trace_qxl_create_guest_primary_rest(qxl->id, sc->stride, sc->type,
                                        sc->flags);

    if ((surface.stride & 0x3) != 0) {
        qxl_set_guest_bug(qxl, "primary surface stride = %d %% 4 != 0",
                          surface.stride);
        return;
    }

    surface.mouse_mode = true;
    surface.group_id   = MEMSLOT_GROUP_GUEST;
    if (loadvm) {
        surface.flags |= QXL_SURF_FLAG_KEEP_DATA;
    }

    qxl->mode = QXL_MODE_NATIVE;
    qxl->cmdflags = 0;
    qemu_spice_create_primary_surface(&qxl->ssd, 0, &surface, async);

    if (async == QXL_SYNC) {
        qxl_create_guest_primary_complete(qxl);
    }
}

/* return 1 if surface destroy was initiated (in QXL_ASYNC case) or
 * done (in QXL_SYNC case), 0 otherwise. */
static int qxl_destroy_primary(PCIQXLDevice *d, qxl_async_io async)
{
    if (d->mode == QXL_MODE_UNDEFINED) {
        return 0;
    }
    trace_qxl_destroy_primary(d->id);
    d->mode = QXL_MODE_UNDEFINED;
    qemu_spice_destroy_primary_surface(&d->ssd, 0, async);
    qxl_spice_reset_cursor(d);
    return 1;
}

static void qxl_set_mode(PCIQXLDevice *d, unsigned int modenr, int loadvm)
{
    pcibus_t start = d->pci.io_regions[QXL_RAM_RANGE_INDEX].addr;
    pcibus_t end   = d->pci.io_regions[QXL_RAM_RANGE_INDEX].size + start;
    QXLMode *mode = d->modes->modes + modenr;
    uint64_t devmem = d->pci.io_regions[QXL_RAM_RANGE_INDEX].addr;
    QXLMemSlot slot = {
        .mem_start = start,
        .mem_end = end
    };

    if (modenr >= d->modes->n_modes) {
        qxl_set_guest_bug(d, "mode number out of range");
        return;
    }

    QXLSurfaceCreate surface = {
        .width      = mode->x_res,
        .height     = mode->y_res,
        .stride     = -mode->x_res * 4,
        .format     = SPICE_SURFACE_FMT_32_xRGB,
        .flags      = loadvm ? QXL_SURF_FLAG_KEEP_DATA : 0,
        .mouse_mode = true,
        .mem        = devmem + d->shadow_rom.draw_area_offset,
    };

    trace_qxl_set_mode(d->id, modenr, mode->x_res, mode->y_res, mode->bits,
                       devmem);
    if (!loadvm) {
        qxl_hard_reset(d, 0);
    }

    d->guest_slots[0].slot = slot;
    if (qxl_add_memslot(d, 0, devmem, QXL_SYNC) != 0) {
        qxl_set_guest_bug(d, "device isn't initialized yet");
        return;
    }

    d->guest_primary.surface = surface;
    qxl_create_guest_primary(d, 0, QXL_SYNC);

    d->mode = QXL_MODE_COMPAT;
    d->cmdflags = QXL_COMMAND_FLAG_COMPAT;
    if (mode->bits == 16) {
        d->cmdflags |= QXL_COMMAND_FLAG_COMPAT_16BPP;
    }
    d->shadow_rom.mode = cpu_to_le32(modenr);
    d->rom->mode = cpu_to_le32(modenr);
    qxl_rom_set_dirty(d);
}

static void ioport_write(void *opaque, hwaddr addr,
                         uint64_t val, unsigned size)
{
    PCIQXLDevice *d = opaque;
    uint32_t io_port = addr;
    qxl_async_io async = QXL_SYNC;
    uint32_t orig_io_port;

    if (d->guest_bug && io_port != QXL_IO_RESET) {
        return;
    }

    if (d->revision <= QXL_REVISION_STABLE_V10 &&
        io_port > QXL_IO_FLUSH_RELEASE) {
        qxl_set_guest_bug(d, "unsupported io %d for revision %d\n",
            io_port, d->revision);
        return;
    }

    switch (io_port) {
    case QXL_IO_RESET:
    case QXL_IO_SET_MODE:
    case QXL_IO_MEMSLOT_ADD:
    case QXL_IO_MEMSLOT_DEL:
    case QXL_IO_CREATE_PRIMARY:
    case QXL_IO_UPDATE_IRQ:
    case QXL_IO_LOG:
    case QXL_IO_MEMSLOT_ADD_ASYNC:
    case QXL_IO_CREATE_PRIMARY_ASYNC:
        break;
    default:
        if (d->mode != QXL_MODE_VGA) {
            break;
        }
        trace_qxl_io_unexpected_vga_mode(d->id,
            addr, val, io_port_to_string(io_port));
        /* be nice to buggy guest drivers */
        if (io_port >= QXL_IO_UPDATE_AREA_ASYNC &&
            io_port < QXL_IO_RANGE_SIZE) {
            qxl_send_events(d, QXL_INTERRUPT_IO_CMD);
        }
        return;
    }

    /* we change the io_port to avoid ifdeffery in the main switch */
    orig_io_port = io_port;
    switch (io_port) {
    case QXL_IO_UPDATE_AREA_ASYNC:
        io_port = QXL_IO_UPDATE_AREA;
        goto async_common;
    case QXL_IO_MEMSLOT_ADD_ASYNC:
        io_port = QXL_IO_MEMSLOT_ADD;
        goto async_common;
    case QXL_IO_CREATE_PRIMARY_ASYNC:
        io_port = QXL_IO_CREATE_PRIMARY;
        goto async_common;
    case QXL_IO_DESTROY_PRIMARY_ASYNC:
        io_port = QXL_IO_DESTROY_PRIMARY;
        goto async_common;
    case QXL_IO_DESTROY_SURFACE_ASYNC:
        io_port = QXL_IO_DESTROY_SURFACE_WAIT;
        goto async_common;
    case QXL_IO_DESTROY_ALL_SURFACES_ASYNC:
        io_port = QXL_IO_DESTROY_ALL_SURFACES;
        goto async_common;
    case QXL_IO_FLUSH_SURFACES_ASYNC:
    case QXL_IO_MONITORS_CONFIG_ASYNC:
async_common:
        async = QXL_ASYNC;
        WITH_QEMU_LOCK_GUARD(&d->async_lock) {
            if (d->current_async != QXL_UNDEFINED_IO) {
                qxl_set_guest_bug(d, "%d async started before last (%d) complete",
                    io_port, d->current_async);
                return;
            }
            d->current_async = orig_io_port;
        }
        break;
    default:
        break;
    }
    trace_qxl_io_write(d->id, qxl_mode_to_string(d->mode),
                       addr, io_port_to_string(addr),
                       val, size, async);

    switch (io_port) {
    case QXL_IO_UPDATE_AREA:
    {
        QXLCookie *cookie = NULL;
        QXLRect update = d->ram->update_area;

        if (d->ram->update_surface > d->ssd.num_surfaces) {
            qxl_set_guest_bug(d, "QXL_IO_UPDATE_AREA: invalid surface id %d\n",
                              d->ram->update_surface);
            break;
        }
        if (update.left >= update.right || update.top >= update.bottom ||
            update.left < 0 || update.top < 0) {
            qxl_set_guest_bug(d,
                    "QXL_IO_UPDATE_AREA: invalid area (%ux%u)x(%ux%u)\n",
                    update.left, update.top, update.right, update.bottom);
            if (update.left == update.right || update.top == update.bottom) {
                /* old drivers may provide empty area, keep going */
                qxl_clear_guest_bug(d);
                goto cancel_async;
            }
            break;
        }
        if (async == QXL_ASYNC) {
            cookie = qxl_cookie_new(QXL_COOKIE_TYPE_IO,
                                    QXL_IO_UPDATE_AREA_ASYNC);
            cookie->u.area = update;
        }
        qxl_spice_update_area(d, d->ram->update_surface,
                              cookie ? &cookie->u.area : &update,
                              NULL, 0, 0, async, cookie);
        break;
    }
    case QXL_IO_NOTIFY_CMD:
        qemu_spice_wakeup(&d->ssd);
        break;
    case QXL_IO_NOTIFY_CURSOR:
        qemu_spice_wakeup(&d->ssd);
        break;
    case QXL_IO_UPDATE_IRQ:
        qxl_update_irq(d);
        break;
    case QXL_IO_NOTIFY_OOM:
        if (!SPICE_RING_IS_EMPTY(&d->ram->release_ring)) {
            break;
        }
        d->oom_running = 1;
        qxl_spice_oom(d);
        d->oom_running = 0;
        break;
    case QXL_IO_SET_MODE:
        qxl_set_mode(d, val, 0);
        break;
    case QXL_IO_LOG:
#ifdef CONFIG_MODULES
        /*
         * FIXME
         * trace_event_get_state_backends() does not work for modules,
         * it leads to "undefined symbol: qemu_qxl_io_log_semaphore"
         */
        if (true) {
#else
        if (trace_event_get_state_backends(TRACE_QXL_IO_LOG) || d->guestdebug) {
#endif
            /* We cannot trust the guest to NUL terminate d->ram->log_buf */
            char *log_buf = g_strndup((const char *)d->ram->log_buf,
                                      sizeof(d->ram->log_buf));
            trace_qxl_io_log(d->id, log_buf);
            if (d->guestdebug) {
                fprintf(stderr, "qxl/guest-%d: %" PRId64 ": %s", d->id,
                        qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL), log_buf);
            }
            g_free(log_buf);
        }
        break;
    case QXL_IO_RESET:
        qxl_hard_reset(d, 0);
        break;
    case QXL_IO_MEMSLOT_ADD:
        if (val >= NUM_MEMSLOTS) {
            qxl_set_guest_bug(d, "QXL_IO_MEMSLOT_ADD: val out of range");
            break;
        }
        if (d->guest_slots[val].active) {
            qxl_set_guest_bug(d,
                        "QXL_IO_MEMSLOT_ADD: memory slot already active");
            break;
        }
        d->guest_slots[val].slot = d->ram->mem_slot;
        qxl_add_memslot(d, val, 0, async);
        break;
    case QXL_IO_MEMSLOT_DEL:
        if (val >= NUM_MEMSLOTS) {
            qxl_set_guest_bug(d, "QXL_IO_MEMSLOT_DEL: val out of range");
            break;
        }
        qxl_del_memslot(d, val);
        break;
    case QXL_IO_CREATE_PRIMARY:
        if (val != 0) {
            qxl_set_guest_bug(d, "QXL_IO_CREATE_PRIMARY (async=%d): val != 0",
                          async);
            goto cancel_async;
        }
        d->guest_primary.surface = d->ram->create_surface;
        qxl_create_guest_primary(d, 0, async);
        break;
    case QXL_IO_DESTROY_PRIMARY:
        if (val != 0) {
            qxl_set_guest_bug(d, "QXL_IO_DESTROY_PRIMARY (async=%d): val != 0",
                          async);
            goto cancel_async;
        }
        if (!qxl_destroy_primary(d, async)) {
            trace_qxl_io_destroy_primary_ignored(d->id,
                                                 qxl_mode_to_string(d->mode));
            goto cancel_async;
        }
        break;
    case QXL_IO_DESTROY_SURFACE_WAIT:
        if (val >= d->ssd.num_surfaces) {
            qxl_set_guest_bug(d, "QXL_IO_DESTROY_SURFACE (async=%d):"
                             "%" PRIu64 " >= NUM_SURFACES", async, val);
            goto cancel_async;
        }
        qxl_spice_destroy_surface_wait(d, val, async);
        break;
    case QXL_IO_FLUSH_RELEASE: {
        QXLReleaseRing *ring = &d->ram->release_ring;
        if (ring->prod - ring->cons + 1 == ring->num_items) {
            fprintf(stderr,
                "ERROR: no flush, full release ring [p%d,%dc]\n",
                ring->prod, ring->cons);
        }
        qxl_push_free_res(d, 1 /* flush */);
        break;
    }
    case QXL_IO_FLUSH_SURFACES_ASYNC:
        qxl_spice_flush_surfaces_async(d);
        break;
    case QXL_IO_DESTROY_ALL_SURFACES:
        d->mode = QXL_MODE_UNDEFINED;
        qxl_spice_destroy_surfaces(d, async);
        break;
    case QXL_IO_MONITORS_CONFIG_ASYNC:
        qxl_spice_monitors_config_async(d, 0);
        break;
    default:
        qxl_set_guest_bug(d, "%s: unexpected ioport=0x%x\n", __func__, io_port);
    }
    return;
cancel_async:
    if (async) {
        qxl_send_events(d, QXL_INTERRUPT_IO_CMD);
        qemu_mutex_lock(&d->async_lock);
        d->current_async = QXL_UNDEFINED_IO;
        qemu_mutex_unlock(&d->async_lock);
    }
}

static uint64_t ioport_read(void *opaque, hwaddr addr,
                            unsigned size)
{
    PCIQXLDevice *qxl = opaque;

    trace_qxl_io_read_unexpected(qxl->id);
    return 0xff;
}

static const MemoryRegionOps qxl_io_ops = {
    .read = ioport_read,
    .write = ioport_write,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void qxl_update_irq_bh(void *opaque)
{
    PCIQXLDevice *d = opaque;
    qxl_update_irq(d);
}

static void qxl_send_events(PCIQXLDevice *d, uint32_t events)
{
    uint32_t old_pending;
    uint32_t le_events = cpu_to_le32(events);

    trace_qxl_send_events(d->id, events);
    if (!qemu_spice_display_is_running(&d->ssd)) {
        /* spice-server tracks guest running state and should not do this */
        fprintf(stderr, "%s: spice-server bug: guest stopped, ignoring\n",
                __func__);
        trace_qxl_send_events_vm_stopped(d->id, events);
        return;
    }
    /*
     * Older versions of Spice forgot to define the QXLRam struct
     * with the '__aligned__(4)' attribute. clang 7 and newer will
     * thus warn that qatomic_fetch_or(&d->ram->int_pending, ...)
     * might be a misaligned atomic access, and will generate an
     * out-of-line call for it, which results in a link error since
     * we don't currently link against libatomic.
     *
     * In fact we set up d->ram in init_qxl_ram() so it always starts
     * at a 4K boundary, so we know that &d->ram->int_pending is
     * naturally aligned for a uint32_t. Newer Spice versions
     * (with Spice commit beda5ec7a6848be20c0cac2a9a8ef2a41e8069c1)
     * will fix the bug directly. To deal with older versions,
     * we tell the compiler to assume the address really is aligned.
     * Any compiler which cares about the misalignment will have
     * __builtin_assume_aligned.
     */
#ifdef HAS_ASSUME_ALIGNED
#define ALIGNED_UINT32_PTR(P) ((uint32_t *)__builtin_assume_aligned(P, 4))
#else
#define ALIGNED_UINT32_PTR(P) ((uint32_t *)P)
#endif

    old_pending = qatomic_fetch_or(ALIGNED_UINT32_PTR(&d->ram->int_pending),
                                  le_events);
    if ((old_pending & le_events) == le_events) {
        return;
    }
    qemu_bh_schedule(d->update_irq);
}

/* graphics console */

static void qxl_hw_update(void *opaque)
{
    PCIQXLDevice *qxl = opaque;

    qxl_render_update(qxl);
}

static void qxl_dirty_one_surface(PCIQXLDevice *qxl, QXLPHYSICAL pqxl,
                                  uint32_t height, int32_t stride)
{
    uint64_t offset, size;
    uint32_t slot;
    bool rc;

    size = (uint64_t)height * abs(stride);
    rc = qxl_get_check_slot_offset(qxl, pqxl, &slot, &offset, size);
    assert(rc == true);
    trace_qxl_surfaces_dirty(qxl->id, offset, size);
    qxl_set_dirty(qxl->guest_slots[slot].mr,
                  qxl->guest_slots[slot].offset + offset,
                  qxl->guest_slots[slot].offset + offset + size);
}

static void qxl_dirty_surfaces(PCIQXLDevice *qxl)
{
    int i;

    if (qxl->mode != QXL_MODE_NATIVE && qxl->mode != QXL_MODE_COMPAT) {
        return;
    }

    /* dirty the primary surface */
    qxl_dirty_one_surface(qxl, qxl->guest_primary.surface.mem,
                          qxl->guest_primary.surface.height,
                          qxl->guest_primary.surface.stride);

    /* dirty the off-screen surfaces */
    for (i = 0; i < qxl->ssd.num_surfaces; i++) {
        QXLSurfaceCmd *cmd;

        if (qxl->guest_surfaces.cmds[i] == 0) {
            continue;
        }

        cmd = qxl_phys2virt(qxl, qxl->guest_surfaces.cmds[i],
                            MEMSLOT_GROUP_GUEST, sizeof(QXLSurfaceCmd));
        assert(cmd);
        assert(cmd->type == QXL_SURFACE_CMD_CREATE);
        qxl_dirty_one_surface(qxl, cmd->u.surface_create.data,
                              cmd->u.surface_create.height,
                              cmd->u.surface_create.stride);
    }
}

static void qxl_vm_change_state_handler(void *opaque, bool running,
                                        RunState state)
{
    PCIQXLDevice *qxl = opaque;

    if (running) {
        /*
         * if qxl_send_events was called from spice server context before
         * migration ended, qxl_update_irq for these events might not have been
         * called
         */
         qxl_update_irq(qxl);
    } else {
        /* make sure surfaces are saved before migration */
        qxl_dirty_surfaces(qxl);
    }
}

/* display change listener */

static void display_update(DisplayChangeListener *dcl,
                           int x, int y, int w, int h)
{
    PCIQXLDevice *qxl = container_of(dcl, PCIQXLDevice, ssd.dcl);

    if (qxl->mode == QXL_MODE_VGA) {
        qemu_spice_display_update(&qxl->ssd, x, y, w, h);
    }
}

static void display_switch(DisplayChangeListener *dcl,
                           struct DisplaySurface *surface)
{
    PCIQXLDevice *qxl = container_of(dcl, PCIQXLDevice, ssd.dcl);

    qxl->ssd.ds = surface;
    if (qxl->mode == QXL_MODE_VGA) {
        qemu_spice_display_switch(&qxl->ssd, surface);
    }
}

static void display_refresh(DisplayChangeListener *dcl)
{
    PCIQXLDevice *qxl = container_of(dcl, PCIQXLDevice, ssd.dcl);

    if (qxl->mode == QXL_MODE_VGA) {
        qemu_spice_display_refresh(&qxl->ssd);
    }
}

static DisplayChangeListenerOps display_listener_ops = {
    .dpy_name        = "spice/qxl",
    .dpy_gfx_update  = display_update,
    .dpy_gfx_switch  = display_switch,
    .dpy_refresh     = display_refresh,
};

static void qxl_init_ramsize(PCIQXLDevice *qxl)
{
    /* vga mode framebuffer / primary surface (bar 0, first part) */
    if (qxl->vgamem_size_mb < 8) {
        qxl->vgamem_size_mb = 8;
    }
    /* XXX: we round vgamem_size_mb up to a nearest power of two and it must be
     * less than vga_common_init()'s maximum on qxl->vga.vram_size (512 now).
     */
    if (qxl->vgamem_size_mb > 256) {
        qxl->vgamem_size_mb = 256;
    }
    qxl->vgamem_size = qxl->vgamem_size_mb * MiB;

    /* vga ram (bar 0, total) */
    if (qxl->ram_size_mb != -1) {
        qxl->vga.vram_size = qxl->ram_size_mb * MiB;
    }
    if (qxl->vga.vram_size < qxl->vgamem_size * 2) {
        qxl->vga.vram_size = qxl->vgamem_size * 2;
    }

    /* vram32 (surfaces, 32bit, bar 1) */
    if (qxl->vram32_size_mb != -1) {
        qxl->vram32_size = qxl->vram32_size_mb * MiB;
    }
    if (qxl->vram32_size < 4096) {
        qxl->vram32_size = 4096;
    }

    /* vram (surfaces, 64bit, bar 4+5) */
    if (qxl->vram_size_mb != -1) {
        qxl->vram_size = (uint64_t)qxl->vram_size_mb * MiB;
    }
    if (qxl->vram_size < qxl->vram32_size) {
        qxl->vram_size = qxl->vram32_size;
    }

    if (qxl->revision == 1) {
        qxl->vram32_size = 4096;
        qxl->vram_size = 4096;
    }
    qxl->vgamem_size = pow2ceil(qxl->vgamem_size);
    qxl->vga.vram_size = pow2ceil(qxl->vga.vram_size);
    qxl->vram32_size = pow2ceil(qxl->vram32_size);
    qxl->vram_size = pow2ceil(qxl->vram_size);
}

static void qxl_realize_common(PCIQXLDevice *qxl, Error **errp)
{
    uint8_t* config = qxl->pci.config;
    uint32_t pci_device_rev;
    uint32_t io_size;

    qemu_spice_display_init_common(&qxl->ssd);
    qxl->mode = QXL_MODE_UNDEFINED;
    qxl->num_memslots = NUM_MEMSLOTS;
    qemu_mutex_init(&qxl->track_lock);
    qemu_mutex_init(&qxl->async_lock);
    qxl->current_async = QXL_UNDEFINED_IO;
    qxl->guest_bug = 0;

    switch (qxl->revision) {
    case 1: /* spice 0.4 -- qxl-1 */
        pci_device_rev = QXL_REVISION_STABLE_V04;
        io_size = 8;
        break;
    case 2: /* spice 0.6 -- qxl-2 */
        pci_device_rev = QXL_REVISION_STABLE_V06;
        io_size = 16;
        break;
    case 3: /* qxl-3 */
        pci_device_rev = QXL_REVISION_STABLE_V10;
        io_size = 32; /* PCI region size must be pow2 */
        break;
    case 4: /* qxl-4 */
        pci_device_rev = QXL_REVISION_STABLE_V12;
        io_size = pow2ceil(QXL_IO_RANGE_SIZE);
        break;
    case 5: /* qxl-5 */
        pci_device_rev = QXL_REVISION_STABLE_V12 + 1;
        io_size = pow2ceil(QXL_IO_RANGE_SIZE);
        break;
    default:
        error_setg(errp, "Invalid revision %d for qxl device (max %d)",
                   qxl->revision, QXL_DEFAULT_REVISION);
        return;
    }

    pci_set_byte(&config[PCI_REVISION_ID], pci_device_rev);
    pci_set_byte(&config[PCI_INTERRUPT_PIN], 1);

    qxl->rom_size = qxl_rom_size();
    memory_region_init_rom(&qxl->rom_bar, OBJECT(qxl), "qxl.vrom",
                           qxl->rom_size, &error_fatal);
    init_qxl_rom(qxl);
    init_qxl_ram(qxl);

    qxl->guest_surfaces.cmds = g_new0(QXLPHYSICAL, qxl->ssd.num_surfaces);
    memory_region_init_ram(&qxl->vram_bar, OBJECT(qxl), "qxl.vram",
                           qxl->vram_size, &error_fatal);
    memory_region_init_alias(&qxl->vram32_bar, OBJECT(qxl), "qxl.vram32",
                             &qxl->vram_bar, 0, qxl->vram32_size);

    memory_region_init_io(&qxl->io_bar, OBJECT(qxl), &qxl_io_ops, qxl,
                          "qxl-ioports", io_size);
    if (qxl->have_vga) {
        vga_dirty_log_start(&qxl->vga);
    }
    memory_region_set_flush_coalesced(&qxl->io_bar);


    pci_register_bar(&qxl->pci, QXL_IO_RANGE_INDEX,
                     PCI_BASE_ADDRESS_SPACE_IO, &qxl->io_bar);

    pci_register_bar(&qxl->pci, QXL_ROM_RANGE_INDEX,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, &qxl->rom_bar);

    pci_register_bar(&qxl->pci, QXL_RAM_RANGE_INDEX,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, &qxl->vga.vram);

    pci_register_bar(&qxl->pci, QXL_VRAM_RANGE_INDEX,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, &qxl->vram32_bar);

    if (qxl->vram32_size < qxl->vram_size) {
        /*
         * Make the 64bit vram bar show up only in case it is
         * configured to be larger than the 32bit vram bar.
         */
        pci_register_bar(&qxl->pci, QXL_VRAM64_RANGE_INDEX,
                         PCI_BASE_ADDRESS_SPACE_MEMORY |
                         PCI_BASE_ADDRESS_MEM_TYPE_64 |
                         PCI_BASE_ADDRESS_MEM_PREFETCH,
                         &qxl->vram_bar);
    }

    /* print pci bar details */
    dprint(qxl, 1, "ram/%s: %" PRId64 " MB [region 0]\n",
           qxl->have_vga ? "pri" : "sec", qxl->vga.vram_size / MiB);
    dprint(qxl, 1, "vram/32: %" PRIx64 " MB [region 1]\n",
           qxl->vram32_size / MiB);
    dprint(qxl, 1, "vram/64: %" PRIx64 " MB %s\n",
           qxl->vram_size / MiB,
           qxl->vram32_size < qxl->vram_size ? "[region 4]" : "[unmapped]");

    qxl->ssd.qxl.base.sif = &qxl_interface.base;
    if (qemu_spice_add_display_interface(&qxl->ssd.qxl, qxl->vga.con) != 0) {
        error_setg(errp, "qxl interface %d.%d not supported by spice-server",
                   SPICE_INTERFACE_QXL_MAJOR, SPICE_INTERFACE_QXL_MINOR);
        return;
    }

#if SPICE_SERVER_VERSION >= 0x000e02 /* release 0.14.2 */
    Error *err = NULL;
    char device_address[256] = "";
    if (qemu_console_fill_device_address(qxl->vga.con,
                                         device_address, sizeof(device_address),
                                         &err)) {
        spice_qxl_set_device_info(&qxl->ssd.qxl,
                                  device_address,
                                  0,
                                  qxl->max_outputs);
    } else {
        error_report_err(err);
    }
#endif

    qemu_add_vm_change_state_handler(qxl_vm_change_state_handler, qxl);

    qxl->update_irq = qemu_bh_new_guarded(qxl_update_irq_bh, qxl,
                                          &DEVICE(qxl)->mem_reentrancy_guard);
    qxl_reset_state(qxl);

    qxl->update_area_bh = qemu_bh_new_guarded(qxl_render_update_area_bh, qxl,
                                              &DEVICE(qxl)->mem_reentrancy_guard);
    qxl->ssd.cursor_bh = qemu_bh_new_guarded(qemu_spice_cursor_refresh_bh, &qxl->ssd,
                                             &DEVICE(qxl)->mem_reentrancy_guard);
}

static void qxl_realize_primary(PCIDevice *dev, Error **errp)
{
    PCIQXLDevice *qxl = PCI_QXL(dev);
    VGACommonState *vga = &qxl->vga;
    Error *local_err = NULL;

    qxl_init_ramsize(qxl);
    vga->vbe_size = qxl->vgamem_size;
    vga->vram_size_mb = qxl->vga.vram_size / MiB;
    vga_common_init(vga, OBJECT(dev), &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    vga_init(vga, OBJECT(dev),
             pci_address_space(dev), pci_address_space_io(dev), false);
    portio_list_init(&qxl->vga_port_list, OBJECT(dev), qxl_vga_portio_list,
                     vga, "vga");
    portio_list_set_flush_coalesced(&qxl->vga_port_list);
    portio_list_add(&qxl->vga_port_list, pci_address_space_io(dev), 0x3b0);
    qxl->have_vga = true;

    vga->con = graphic_console_init(DEVICE(dev), 0, &qxl_ops, qxl);
    qxl->id = qemu_console_get_index(vga->con); /* == channel_id */
    if (qxl->id != 0) {
        error_setg(errp, "primary qxl-vga device must be console 0 "
                   "(first display device on the command line)");
        return;
    }

    qxl_realize_common(qxl, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    qxl->ssd.dcl.ops = &display_listener_ops;
    qxl->ssd.dcl.con = vga->con;
    register_displaychangelistener(&qxl->ssd.dcl);
}

static void qxl_realize_secondary(PCIDevice *dev, Error **errp)
{
    PCIQXLDevice *qxl = PCI_QXL(dev);

    qxl_init_ramsize(qxl);
    memory_region_init_ram(&qxl->vga.vram, OBJECT(dev), "qxl.vgavram",
                           qxl->vga.vram_size, &error_fatal);
    qxl->vga.vram_ptr = memory_region_get_ram_ptr(&qxl->vga.vram);
    qxl->vga.con = graphic_console_init(DEVICE(dev), 0, &qxl_ops, qxl);
    qxl->ssd.dcl.con = qxl->vga.con;
    qxl->id = qemu_console_get_index(qxl->vga.con); /* == channel_id */

    qxl_realize_common(qxl, errp);
}

static int qxl_pre_save(void *opaque)
{
    PCIQXLDevice* d = opaque;
    uint8_t *ram_start = d->vga.vram_ptr;

    trace_qxl_pre_save(d->id);
    if (d->last_release == NULL) {
        d->last_release_offset = 0;
    } else {
        d->last_release_offset = (uint8_t *)d->last_release - ram_start;
    }
    if (d->last_release_offset >= d->vga.vram_size) {
        return 1;
    }

    return 0;
}

static int qxl_pre_load(void *opaque)
{
    PCIQXLDevice* d = opaque;

    trace_qxl_pre_load(d->id);
    qxl_hard_reset(d, 1);
    qxl_exit_vga_mode(d);
    return 0;
}

static void qxl_create_memslots(PCIQXLDevice *d)
{
    int i;

    for (i = 0; i < NUM_MEMSLOTS; i++) {
        if (!d->guest_slots[i].active) {
            continue;
        }
        qxl_add_memslot(d, i, 0, QXL_SYNC);
    }
}

static int qxl_post_load(void *opaque, int version)
{
    PCIQXLDevice* d = opaque;
    uint8_t *ram_start = d->vga.vram_ptr;
    QXLCommandExt *cmds;
    int in, out, newmode;

    assert(d->last_release_offset < d->vga.vram_size);
    if (d->last_release_offset == 0) {
        d->last_release = NULL;
    } else {
        d->last_release = (QXLReleaseInfo *)(ram_start + d->last_release_offset);
    }

    d->modes = (QXLModes*)((uint8_t*)d->rom + d->rom->modes_offset);

    trace_qxl_post_load(d->id, qxl_mode_to_string(d->mode));
    newmode = d->mode;
    d->mode = QXL_MODE_UNDEFINED;

    switch (newmode) {
    case QXL_MODE_UNDEFINED:
        qxl_create_memslots(d);
        break;
    case QXL_MODE_VGA:
        qxl_create_memslots(d);
        qxl_enter_vga_mode(d);
        break;
    case QXL_MODE_NATIVE:
        qxl_create_memslots(d);
        qxl_create_guest_primary(d, 1, QXL_SYNC);

        /* replay surface-create and cursor-set commands */
        cmds = g_new0(QXLCommandExt, d->ssd.num_surfaces + 1);
        for (in = 0, out = 0; in < d->ssd.num_surfaces; in++) {
            if (d->guest_surfaces.cmds[in] == 0) {
                continue;
            }
            cmds[out].cmd.data = d->guest_surfaces.cmds[in];
            cmds[out].cmd.type = QXL_CMD_SURFACE;
            cmds[out].group_id = MEMSLOT_GROUP_GUEST;
            out++;
        }
        if (d->guest_cursor) {
            cmds[out].cmd.data = d->guest_cursor;
            cmds[out].cmd.type = QXL_CMD_CURSOR;
            cmds[out].group_id = MEMSLOT_GROUP_GUEST;
            out++;
        }
        qxl_spice_loadvm_commands(d, cmds, out);
        g_free(cmds);
        if (d->guest_monitors_config) {
            qxl_spice_monitors_config_async(d, 1);
        }
        break;
    case QXL_MODE_COMPAT:
        /* note: no need to call qxl_create_memslots, qxl_set_mode
         * creates the mem slot. */
        qxl_set_mode(d, d->shadow_rom.mode, 1);
        break;
    }
    return 0;
}

#define QXL_SAVE_VERSION 21

static bool qxl_monitors_config_needed(void *opaque)
{
    PCIQXLDevice *qxl = opaque;

    return qxl->guest_monitors_config != 0;
}


static const VMStateDescription qxl_memslot = {
    .name               = "qxl-memslot",
    .version_id         = QXL_SAVE_VERSION,
    .minimum_version_id = QXL_SAVE_VERSION,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(slot.mem_start, struct guest_slots),
        VMSTATE_UINT64(slot.mem_end,   struct guest_slots),
        VMSTATE_UINT32(active,         struct guest_slots),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription qxl_surface = {
    .name               = "qxl-surface",
    .version_id         = QXL_SAVE_VERSION,
    .minimum_version_id = QXL_SAVE_VERSION,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(width,      QXLSurfaceCreate),
        VMSTATE_UINT32(height,     QXLSurfaceCreate),
        VMSTATE_INT32(stride,      QXLSurfaceCreate),
        VMSTATE_UINT32(format,     QXLSurfaceCreate),
        VMSTATE_UINT32(position,   QXLSurfaceCreate),
        VMSTATE_UINT32(mouse_mode, QXLSurfaceCreate),
        VMSTATE_UINT32(flags,      QXLSurfaceCreate),
        VMSTATE_UINT32(type,       QXLSurfaceCreate),
        VMSTATE_UINT64(mem,        QXLSurfaceCreate),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription qxl_vmstate_monitors_config = {
    .name               = "qxl/monitors-config",
    .version_id         = 1,
    .minimum_version_id = 1,
    .needed = qxl_monitors_config_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(guest_monitors_config, PCIQXLDevice),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription qxl_vmstate = {
    .name               = "qxl",
    .version_id         = QXL_SAVE_VERSION,
    .minimum_version_id = QXL_SAVE_VERSION,
    .pre_save           = qxl_pre_save,
    .pre_load           = qxl_pre_load,
    .post_load          = qxl_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(pci, PCIQXLDevice),
        VMSTATE_STRUCT(vga, PCIQXLDevice, 0, vmstate_vga_common, VGACommonState),
        VMSTATE_UINT32(shadow_rom.mode, PCIQXLDevice),
        VMSTATE_UINT32(num_free_res, PCIQXLDevice),
        VMSTATE_UINT32(last_release_offset, PCIQXLDevice),
        VMSTATE_UINT32(mode, PCIQXLDevice),
        VMSTATE_UINT32(ssd.unique, PCIQXLDevice),
        VMSTATE_INT32_EQUAL(num_memslots, PCIQXLDevice, NULL),
        VMSTATE_STRUCT_ARRAY(guest_slots, PCIQXLDevice, NUM_MEMSLOTS, 0,
                             qxl_memslot, struct guest_slots),
        VMSTATE_STRUCT(guest_primary.surface, PCIQXLDevice, 0,
                       qxl_surface, QXLSurfaceCreate),
        VMSTATE_INT32_EQUAL(ssd.num_surfaces, PCIQXLDevice, NULL),
        VMSTATE_VARRAY_INT32(guest_surfaces.cmds, PCIQXLDevice,
                             ssd.num_surfaces, 0,
                             vmstate_info_uint64, uint64_t),
        VMSTATE_UINT64(guest_cursor, PCIQXLDevice),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &qxl_vmstate_monitors_config,
        NULL
    }
};

static Property qxl_properties[] = {
        DEFINE_PROP_UINT32("ram_size", PCIQXLDevice, vga.vram_size, 64 * MiB),
        DEFINE_PROP_UINT64("vram_size", PCIQXLDevice, vram32_size, 64 * MiB),
        DEFINE_PROP_UINT32("revision", PCIQXLDevice, revision,
                           QXL_DEFAULT_REVISION),
        DEFINE_PROP_UINT32("debug", PCIQXLDevice, debug, 0),
        DEFINE_PROP_UINT32("guestdebug", PCIQXLDevice, guestdebug, 0),
        DEFINE_PROP_UINT32("cmdlog", PCIQXLDevice, cmdlog, 0),
        DEFINE_PROP_UINT32("ram_size_mb",  PCIQXLDevice, ram_size_mb, -1),
        DEFINE_PROP_UINT32("vram_size_mb", PCIQXLDevice, vram32_size_mb, -1),
        DEFINE_PROP_UINT32("vram64_size_mb", PCIQXLDevice, vram_size_mb, -1),
        DEFINE_PROP_UINT32("vgamem_mb", PCIQXLDevice, vgamem_size_mb, 16),
        DEFINE_PROP_INT32("surfaces", PCIQXLDevice, ssd.num_surfaces, 1024),
        DEFINE_PROP_UINT16("max_outputs", PCIQXLDevice, max_outputs, 0),
        DEFINE_PROP_UINT32("xres", PCIQXLDevice, xres, 0),
        DEFINE_PROP_UINT32("yres", PCIQXLDevice, yres, 0),
        DEFINE_PROP_BOOL("global-vmstate", PCIQXLDevice, vga.global_vmstate, false),
        DEFINE_PROP_END_OF_LIST(),
};

static void qxl_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->vendor_id = REDHAT_PCI_VENDOR_ID;
    k->device_id = QXL_DEVICE_ID_STABLE;
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
    dc->reset = qxl_reset_handler;
    dc->vmsd = &qxl_vmstate;
    device_class_set_props(dc, qxl_properties);
}

static const TypeInfo qxl_pci_type_info = {
    .name = TYPE_PCI_QXL,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIQXLDevice),
    .abstract = true,
    .class_init = qxl_pci_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void qxl_primary_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = qxl_realize_primary;
    k->romfile = "vgabios-qxl.bin";
    k->class_id = PCI_CLASS_DISPLAY_VGA;
    dc->desc = "Spice QXL GPU (primary, vga compatible)";
    dc->hotpluggable = false;
}

static const TypeInfo qxl_primary_info = {
    .name          = "qxl-vga",
    .parent        = TYPE_PCI_QXL,
    .class_init    = qxl_primary_class_init,
};
module_obj("qxl-vga");
module_kconfig(QXL);

static void qxl_secondary_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = qxl_realize_secondary;
    k->class_id = PCI_CLASS_DISPLAY_OTHER;
    dc->desc = "Spice QXL GPU (secondary)";
}

static const TypeInfo qxl_secondary_info = {
    .name          = "qxl",
    .parent        = TYPE_PCI_QXL,
    .class_init    = qxl_secondary_class_init,
};
module_obj("qxl");

static void qxl_register_types(void)
{
    type_register_static(&qxl_pci_type_info);
    type_register_static(&qxl_primary_info);
    type_register_static(&qxl_secondary_info);
}

type_init(qxl_register_types)

module_dep("ui-spice-core");
