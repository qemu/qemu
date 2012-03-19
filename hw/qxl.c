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

#include "qemu-common.h"
#include "qemu-timer.h"
#include "qemu-queue.h"
#include "monitor.h"
#include "sysemu.h"
#include "trace.h"

#include "qxl.h"

#undef SPICE_RING_PROD_ITEM
#define SPICE_RING_PROD_ITEM(r, ret) {                                  \
        typeof(r) start = r;                                            \
        typeof(r) end = r + 1;                                          \
        uint32_t prod = (r)->prod & SPICE_RING_INDEX_MASK(r);           \
        typeof(&(r)->items[prod]) m_item = &(r)->items[prod];           \
        if (!((uint8_t*)m_item >= (uint8_t*)(start) && (uint8_t*)(m_item + 1) <= (uint8_t*)(end))) { \
            abort();                                                    \
        }                                                               \
        ret = &m_item->el;                                              \
    }

#undef SPICE_RING_CONS_ITEM
#define SPICE_RING_CONS_ITEM(r, ret) {                                  \
        typeof(r) start = r;                                            \
        typeof(r) end = r + 1;                                          \
        uint32_t cons = (r)->cons & SPICE_RING_INDEX_MASK(r);           \
        typeof(&(r)->items[cons]) m_item = &(r)->items[cons];           \
        if (!((uint8_t*)m_item >= (uint8_t*)(start) && (uint8_t*)(m_item + 1) <= (uint8_t*)(end))) { \
            abort();                                                    \
        }                                                               \
        ret = &m_item->el;                                              \
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
    QXL_MODE_16_32(y_res, x_res, 1),              \
    QXL_MODE_16_32(x_res, y_res, 2),              \
    QXL_MODE_16_32(y_res, x_res, 3)

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
#if VGA_RAM_SIZE >= (16 * 1024 * 1024)
    /* these modes need more than 8 MB video memory */
    QXL_MODE_EX(1920, 1200),
    QXL_MODE_EX(1920, 1440),
    QXL_MODE_EX(2048, 1536),
    QXL_MODE_EX(2560, 1440),
    QXL_MODE_EX(2560, 1600),
#endif
#if VGA_RAM_SIZE >= (32 * 1024 * 1024)
    /* these modes need more than 16 MB video memory */
    QXL_MODE_EX(2560, 2048),
    QXL_MODE_EX(2800, 2100),
    QXL_MODE_EX(3200, 2400),
#endif
};

static PCIQXLDevice *qxl0;

static void qxl_send_events(PCIQXLDevice *d, uint32_t events);
static int qxl_destroy_primary(PCIQXLDevice *d, qxl_async_io async);
static void qxl_reset_memslots(PCIQXLDevice *d);
static void qxl_reset_surfaces(PCIQXLDevice *d);
static void qxl_ring_set_dirty(PCIQXLDevice *qxl);

void qxl_guest_bug(PCIQXLDevice *qxl, const char *msg, ...)
{
    qxl_send_events(qxl, QXL_INTERRUPT_ERROR);
    if (qxl->guestdebug) {
        va_list ap;
        va_start(ap, msg);
        fprintf(stderr, "qxl-%d: guest bug: ", qxl->id);
        vfprintf(stderr, msg, ap);
        fprintf(stderr, "\n");
        va_end(ap);
    }
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
        qxl->ssd.worker->update_area(qxl->ssd.worker, surface_id, area,
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
        qxl->ssd.worker->destroy_surface_wait(qxl->ssd.worker, id);
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
    qxl->ssd.worker->loadvm_commands(qxl->ssd.worker, ext, count);
}

void qxl_spice_oom(PCIQXLDevice *qxl)
{
    trace_qxl_spice_oom(qxl->id);
    qxl->ssd.worker->oom(qxl->ssd.worker);
}

void qxl_spice_reset_memslots(PCIQXLDevice *qxl)
{
    trace_qxl_spice_reset_memslots(qxl->id);
    qxl->ssd.worker->reset_memslots(qxl->ssd.worker);
}

static void qxl_spice_destroy_surfaces_complete(PCIQXLDevice *qxl)
{
    trace_qxl_spice_destroy_surfaces_complete(qxl->id);
    qemu_mutex_lock(&qxl->track_lock);
    memset(&qxl->guest_surfaces.cmds, 0, sizeof(qxl->guest_surfaces.cmds));
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
        qxl->ssd.worker->destroy_surfaces(qxl->ssd.worker);
        qxl_spice_destroy_surfaces_complete(qxl);
    }
}

void qxl_spice_reset_image_cache(PCIQXLDevice *qxl)
{
    trace_qxl_spice_reset_image_cache(qxl->id);
    qxl->ssd.worker->reset_image_cache(qxl->ssd.worker);
}

void qxl_spice_reset_cursor(PCIQXLDevice *qxl)
{
    trace_qxl_spice_reset_cursor(qxl->id);
    qxl->ssd.worker->reset_cursor(qxl->ssd.worker);
    qemu_mutex_lock(&qxl->track_lock);
    qxl->guest_cursor = 0;
    qemu_mutex_unlock(&qxl->track_lock);
}


static inline uint32_t msb_mask(uint32_t val)
{
    uint32_t mask;

    do {
        mask = ~(val - 1) & val;
        val &= ~mask;
    } while (mask < val);

    return mask;
}

static ram_addr_t qxl_rom_size(void)
{
    uint32_t rom_size = sizeof(QXLRom) + sizeof(QXLModes) + sizeof(qxl_modes);
    rom_size = MAX(rom_size, TARGET_PAGE_SIZE);
    rom_size = msb_mask(rom_size * 2 - 1);
    return rom_size;
}

static void init_qxl_rom(PCIQXLDevice *d)
{
    QXLRom *rom = memory_region_get_ram_ptr(&d->rom_bar);
    QXLModes *modes = (QXLModes *)(rom + 1);
    uint32_t ram_header_size;
    uint32_t surface0_area_size;
    uint32_t num_pages;
    uint32_t fb, maxfb = 0;
    int i;

    memset(rom, 0, d->rom_size);

    rom->magic         = cpu_to_le32(QXL_ROM_MAGIC);
    rom->id            = cpu_to_le32(d->id);
    rom->log_level     = cpu_to_le32(d->guestdebug);
    rom->modes_offset  = cpu_to_le32(sizeof(QXLRom));

    rom->slot_gen_bits = MEMSLOT_GENERATION_BITS;
    rom->slot_id_bits  = MEMSLOT_SLOT_BITS;
    rom->slots_start   = 1;
    rom->slots_end     = NUM_MEMSLOTS - 1;
    rom->n_surfaces    = cpu_to_le32(NUM_SURFACES);

    modes->n_modes     = cpu_to_le32(ARRAY_SIZE(qxl_modes));
    for (i = 0; i < modes->n_modes; i++) {
        fb = qxl_modes[i].y_res * qxl_modes[i].stride;
        if (maxfb < fb) {
            maxfb = fb;
        }
        modes->modes[i].id          = cpu_to_le32(i);
        modes->modes[i].x_res       = cpu_to_le32(qxl_modes[i].x_res);
        modes->modes[i].y_res       = cpu_to_le32(qxl_modes[i].y_res);
        modes->modes[i].bits        = cpu_to_le32(qxl_modes[i].bits);
        modes->modes[i].stride      = cpu_to_le32(qxl_modes[i].stride);
        modes->modes[i].x_mili      = cpu_to_le32(qxl_modes[i].x_mili);
        modes->modes[i].y_mili      = cpu_to_le32(qxl_modes[i].y_mili);
        modes->modes[i].orientation = cpu_to_le32(qxl_modes[i].orientation);
    }
    if (maxfb < VGA_RAM_SIZE && d->id == 0)
        maxfb = VGA_RAM_SIZE;

    ram_header_size    = ALIGN(sizeof(QXLRam), 4096);
    surface0_area_size = ALIGN(maxfb, 4096);
    num_pages          = d->vga.vram_size;
    num_pages         -= ram_header_size;
    num_pages         -= surface0_area_size;
    num_pages          = num_pages / TARGET_PAGE_SIZE;

    rom->draw_area_offset   = cpu_to_le32(0);
    rom->surface0_area_size = cpu_to_le32(surface0_area_size);
    rom->pages_offset       = cpu_to_le32(surface0_area_size);
    rom->num_pages          = cpu_to_le32(num_pages);
    rom->ram_header_offset  = cpu_to_le32(d->vga.vram_size - ram_header_size);

    d->shadow_rom = *rom;
    d->rom        = rom;
    d->modes      = modes;
}

static void init_qxl_ram(PCIQXLDevice *d)
{
    uint8_t *buf;
    uint64_t *item;

    buf = d->vga.vram_ptr;
    d->ram = (QXLRam *)(buf + le32_to_cpu(d->shadow_rom.ram_header_offset));
    d->ram->magic       = cpu_to_le32(QXL_RAM_MAGIC);
    d->ram->int_pending = cpu_to_le32(0);
    d->ram->int_mask    = cpu_to_le32(0);
    d->ram->update_surface = 0;
    SPICE_RING_INIT(&d->ram->cmd_ring);
    SPICE_RING_INIT(&d->ram->cursor_ring);
    SPICE_RING_INIT(&d->ram->release_ring);
    SPICE_RING_PROD_ITEM(&d->ram->release_ring, item);
    *item = 0;
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
    offset &= ~(TARGET_PAGE_SIZE-1);
    assert(offset < qxl->vga.vram_size);
    qxl_set_dirty(&qxl->vga.vram, offset, offset + TARGET_PAGE_SIZE);
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
static void qxl_track_command(PCIQXLDevice *qxl, struct QXLCommandExt *ext)
{
    switch (le32_to_cpu(ext->cmd.type)) {
    case QXL_CMD_SURFACE:
    {
        QXLSurfaceCmd *cmd = qxl_phys2virt(qxl, ext->cmd.data, ext->group_id);
        uint32_t id = le32_to_cpu(cmd->surface_id);
        PANIC_ON(id >= NUM_SURFACES);
        qemu_mutex_lock(&qxl->track_lock);
        if (cmd->type == QXL_SURFACE_CMD_CREATE) {
            qxl->guest_surfaces.cmds[id] = ext->cmd.data;
            qxl->guest_surfaces.count++;
            if (qxl->guest_surfaces.max < qxl->guest_surfaces.count)
                qxl->guest_surfaces.max = qxl->guest_surfaces.count;
        }
        if (cmd->type == QXL_SURFACE_CMD_DESTROY) {
            qxl->guest_surfaces.cmds[id] = 0;
            qxl->guest_surfaces.count--;
        }
        qemu_mutex_unlock(&qxl->track_lock);
        break;
    }
    case QXL_CMD_CURSOR:
    {
        QXLCursorCmd *cmd = qxl_phys2virt(qxl, ext->cmd.data, ext->group_id);
        if (cmd->type == QXL_CURSOR_SET) {
            qemu_mutex_lock(&qxl->track_lock);
            qxl->guest_cursor = ext->cmd.data;
            qemu_mutex_unlock(&qxl->track_lock);
        }
        break;
    }
    }
}

/* spice display interface callbacks */

static void interface_attach_worker(QXLInstance *sin, QXLWorker *qxl_worker)
{
    PCIQXLDevice *qxl = container_of(sin, PCIQXLDevice, ssd.qxl);

    trace_qxl_interface_attach_worker(qxl->id);
    qxl->ssd.worker = qxl_worker;
}

static void interface_set_compression_level(QXLInstance *sin, int level)
{
    PCIQXLDevice *qxl = container_of(sin, PCIQXLDevice, ssd.qxl);

    trace_qxl_interface_set_compression_level(qxl->id, level);
    qxl->shadow_rom.compression_level = cpu_to_le32(level);
    qxl->rom->compression_level = cpu_to_le32(level);
    qxl_rom_set_dirty(qxl);
}

static void interface_set_mm_time(QXLInstance *sin, uint32_t mm_time)
{
    PCIQXLDevice *qxl = container_of(sin, PCIQXLDevice, ssd.qxl);

    trace_qxl_interface_set_mm_time(qxl->id, mm_time);
    qxl->shadow_rom.mm_clock = cpu_to_le32(mm_time);
    qxl->rom->mm_clock = cpu_to_le32(mm_time);
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
    info->qxl_ram_size = le32_to_cpu(qxl->shadow_rom.num_pages) << TARGET_PAGE_BITS;
    info->n_surfaces = NUM_SURFACES;
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
        if (qxl->ssd.update != NULL) {
            update = qxl->ssd.update;
            qxl->ssd.update = NULL;
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
        if (SPICE_RING_IS_EMPTY(ring)) {
            return false;
        }
        trace_qxl_ring_command_get(qxl->id, qxl_mode_to_string(qxl->mode));
        SPICE_RING_CONS_ITEM(ring, cmd);
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
    uint64_t *item;
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
    SPICE_RING_PROD_ITEM(ring, item);
    *item = 0;
    d->num_free_res = 0;
    d->last_release = NULL;
    qxl_ring_set_dirty(d);
}

/* called from spice server thread context only */
static void interface_release_resource(QXLInstance *sin,
                                       struct QXLReleaseInfoExt ext)
{
    PCIQXLDevice *qxl = container_of(sin, PCIQXLDevice, ssd.qxl);
    QXLReleaseRing *ring;
    uint64_t *item, id;

    if (ext.group_id == MEMSLOT_GROUP_HOST) {
        /* host group -> vga mode update request */
        qemu_spice_destroy_update(&qxl->ssd, (void *)(intptr_t)ext.info->id);
        return;
    }

    /*
     * ext->info points into guest-visible memory
     * pci bar 0, $command.release_info
     */
    ring = &qxl->ram->release_ring;
    SPICE_RING_PROD_ITEM(ring, item);
    if (*item == 0) {
        /* stick head into the ring */
        id = ext.info->id;
        ext.info->next = 0;
        qxl_ram_set_dirty(qxl, &ext.info->next);
        *item = id;
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
        SPICE_RING_CONS_ITEM(ring, cmd);
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
        if (qxl->id == 0) {
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
    fprintf(stderr, "%s: abort()\n", __FUNCTION__);
    abort();
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
                "qxl: %s: error: current_async = %d != %" PRId64 " = cookie->io\n",
                __func__, current_async, cookie->io);
    }
    switch (current_async) {
    case QXL_IO_MEMSLOT_ADD_ASYNC:
    case QXL_IO_DESTROY_PRIMARY_ASYNC:
    case QXL_IO_UPDATE_AREA_ASYNC:
    case QXL_IO_FLUSH_SURFACES_ASYNC:
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
        fprintf(stderr, "qxl: %s: unexpected current_async %d\n", __func__,
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

    qemu_mutex_lock(&qxl->ssd.lock);
    if (surface_id != 0 || !qxl->render_update_cookie_num) {
        qemu_mutex_unlock(&qxl->ssd.lock);
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
        qemu_mutex_unlock(&qxl->ssd.lock);
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
    qemu_mutex_unlock(&qxl->ssd.lock);
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
    default:
        fprintf(stderr, "qxl: %s: unexpected cookie type %d\n",
                __func__, cookie->type);
        g_free(cookie);
    }
}

static const QXLInterface qxl_interface = {
    .base.type               = SPICE_INTERFACE_QXL,
    .base.description        = "qxl gpu",
    .base.major_version      = SPICE_INTERFACE_QXL_MAJOR,
    .base.minor_version      = SPICE_INTERFACE_QXL_MINOR,

    .attache_worker          = interface_attach_worker,
    .set_compression_level   = interface_set_compression_level,
    .set_mm_time             = interface_set_mm_time,
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
};

static void qxl_enter_vga_mode(PCIQXLDevice *d)
{
    if (d->mode == QXL_MODE_VGA) {
        return;
    }
    trace_qxl_enter_vga_mode(d->id);
    qemu_spice_create_host_primary(&d->ssd);
    d->mode = QXL_MODE_VGA;
    memset(&d->ssd.dirty, 0, sizeof(d->ssd.dirty));
}

static void qxl_exit_vga_mode(PCIQXLDevice *d)
{
    if (d->mode != QXL_MODE_VGA) {
        return;
    }
    trace_qxl_exit_vga_mode(d->id);
    qxl_destroy_primary(d, QXL_SYNC);
}

static void qxl_update_irq(PCIQXLDevice *d)
{
    uint32_t pending = le32_to_cpu(d->ram->int_pending);
    uint32_t mask    = le32_to_cpu(d->ram->int_mask);
    int level = !!(pending & mask);
    qemu_set_irq(d->pci.irq[0], level);
    qxl_ring_set_dirty(d);
}

static void qxl_check_state(PCIQXLDevice *d)
{
    QXLRam *ram = d->ram;

    assert(!d->ssd.running || SPICE_RING_IS_EMPTY(&ram->cmd_ring));
    assert(!d->ssd.running || SPICE_RING_IS_EMPTY(&ram->cursor_ring));
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
}

static void qxl_soft_reset(PCIQXLDevice *d)
{
    trace_qxl_soft_reset(d->id);
    qxl_check_state(d);

    if (d->id == 0) {
        qxl_enter_vga_mode(d);
    } else {
        d->mode = QXL_MODE_UNDEFINED;
    }
}

static void qxl_hard_reset(PCIQXLDevice *d, int loadvm)
{
    trace_qxl_hard_reset(d->id, loadvm);

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
}

static void qxl_reset_handler(DeviceState *dev)
{
    PCIQXLDevice *d = DO_UPCAST(PCIQXLDevice, pci.qdev, dev);

    qxl_hard_reset(d, 0);
}

static void qxl_vga_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    VGACommonState *vga = opaque;
    PCIQXLDevice *qxl = container_of(vga, PCIQXLDevice, vga);

    trace_qxl_io_write_vga(qxl->id, qxl_mode_to_string(qxl->mode), addr, val);
    if (qxl->mode != QXL_MODE_VGA) {
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

static void qxl_add_memslot(PCIQXLDevice *d, uint32_t slot_id, uint64_t delta,
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
    intptr_t virt_start;
    QXLDevMemSlot memslot;
    int i;

    guest_start = le64_to_cpu(d->guest_slots[slot_id].slot.mem_start);
    guest_end   = le64_to_cpu(d->guest_slots[slot_id].slot.mem_end);

    trace_qxl_memslot_add_guest(d->id, slot_id, guest_start, guest_end);

    PANIC_ON(slot_id >= NUM_MEMSLOTS);
    PANIC_ON(guest_start > guest_end);

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
    PANIC_ON(i == ARRAY_SIZE(regions)); /* finished loop without match */

    switch (pci_region) {
    case QXL_RAM_RANGE_INDEX:
        virt_start = (intptr_t)memory_region_get_ram_ptr(&d->vga.vram);
        break;
    case QXL_VRAM_RANGE_INDEX:
    case 4 /* vram 64bit */:
        virt_start = (intptr_t)memory_region_get_ram_ptr(&d->vram_bar);
        break;
    default:
        /* should not happen */
        abort();
    }

    memslot.slot_id = slot_id;
    memslot.slot_group_id = MEMSLOT_GROUP_GUEST; /* guest group */
    memslot.virt_start = virt_start + (guest_start - pci_start);
    memslot.virt_end   = virt_start + (guest_end   - pci_start);
    memslot.addr_delta = memslot.virt_start - delta;
    memslot.generation = d->rom->slot_generation = 0;
    qxl_rom_set_dirty(d);

    qemu_spice_add_memslot(&d->ssd, &memslot, async);
    d->guest_slots[slot_id].ptr = (void*)memslot.virt_start;
    d->guest_slots[slot_id].size = memslot.virt_end - memslot.virt_start;
    d->guest_slots[slot_id].delta = delta;
    d->guest_slots[slot_id].active = 1;
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
void *qxl_phys2virt(PCIQXLDevice *qxl, QXLPHYSICAL pqxl, int group_id)
{
    uint64_t phys   = le64_to_cpu(pqxl);
    uint32_t slot   = (phys >> (64 -  8)) & 0xff;
    uint64_t offset = phys & 0xffffffffffff;

    switch (group_id) {
    case MEMSLOT_GROUP_HOST:
        return (void *)(intptr_t)offset;
    case MEMSLOT_GROUP_GUEST:
        PANIC_ON(slot >= NUM_MEMSLOTS);
        PANIC_ON(!qxl->guest_slots[slot].active);
        PANIC_ON(offset < qxl->guest_slots[slot].delta);
        offset -= qxl->guest_slots[slot].delta;
        PANIC_ON(offset > qxl->guest_slots[slot].size)
        return qxl->guest_slots[slot].ptr + offset;
    default:
        PANIC_ON(1);
    }
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

    assert(qxl->mode != QXL_MODE_NATIVE);
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

/* return 1 if surface destoy was initiated (in QXL_ASYNC case) or
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

static void qxl_set_mode(PCIQXLDevice *d, int modenr, int loadvm)
{
    pcibus_t start = d->pci.io_regions[QXL_RAM_RANGE_INDEX].addr;
    pcibus_t end   = d->pci.io_regions[QXL_RAM_RANGE_INDEX].size + start;
    QXLMode *mode = d->modes->modes + modenr;
    uint64_t devmem = d->pci.io_regions[QXL_RAM_RANGE_INDEX].addr;
    QXLMemSlot slot = {
        .mem_start = start,
        .mem_end = end
    };
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
    qxl_add_memslot(d, 0, devmem, QXL_SYNC);

    d->guest_primary.surface = surface;
    qxl_create_guest_primary(d, 0, QXL_SYNC);

    d->mode = QXL_MODE_COMPAT;
    d->cmdflags = QXL_COMMAND_FLAG_COMPAT;
#ifdef QXL_COMMAND_FLAG_COMPAT_16BPP /* new in spice 0.6.1 */
    if (mode->bits == 16) {
        d->cmdflags |= QXL_COMMAND_FLAG_COMPAT_16BPP;
    }
#endif
    d->shadow_rom.mode = cpu_to_le32(modenr);
    d->rom->mode = cpu_to_le32(modenr);
    qxl_rom_set_dirty(d);
}

static void ioport_write(void *opaque, target_phys_addr_t addr,
                         uint64_t val, unsigned size)
{
    PCIQXLDevice *d = opaque;
    uint32_t io_port = addr;
    qxl_async_io async = QXL_SYNC;
    uint32_t orig_io_port = io_port;

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
            io_port, io_port_to_string(io_port));
        /* be nice to buggy guest drivers */
        if (io_port >= QXL_IO_UPDATE_AREA_ASYNC &&
            io_port <= QXL_IO_DESTROY_ALL_SURFACES_ASYNC) {
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
async_common:
        async = QXL_ASYNC;
        qemu_mutex_lock(&d->async_lock);
        if (d->current_async != QXL_UNDEFINED_IO) {
            qxl_guest_bug(d, "%d async started before last (%d) complete",
                io_port, d->current_async);
            qemu_mutex_unlock(&d->async_lock);
            return;
        }
        d->current_async = orig_io_port;
        qemu_mutex_unlock(&d->async_lock);
        break;
    default:
        break;
    }
    trace_qxl_io_write(d->id, qxl_mode_to_string(d->mode), addr, val, size,
                       async);

    switch (io_port) {
    case QXL_IO_UPDATE_AREA:
    {
        QXLCookie *cookie = NULL;
        QXLRect update = d->ram->update_area;

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
        if (d->guestdebug) {
            fprintf(stderr, "qxl/guest-%d: %" PRId64 ": %s", d->id,
                    qemu_get_clock_ns(vm_clock), d->ram->log_buf);
        }
        break;
    case QXL_IO_RESET:
        qxl_hard_reset(d, 0);
        break;
    case QXL_IO_MEMSLOT_ADD:
        if (val >= NUM_MEMSLOTS) {
            qxl_guest_bug(d, "QXL_IO_MEMSLOT_ADD: val out of range");
            break;
        }
        if (d->guest_slots[val].active) {
            qxl_guest_bug(d, "QXL_IO_MEMSLOT_ADD: memory slot already active");
            break;
        }
        d->guest_slots[val].slot = d->ram->mem_slot;
        qxl_add_memslot(d, val, 0, async);
        break;
    case QXL_IO_MEMSLOT_DEL:
        if (val >= NUM_MEMSLOTS) {
            qxl_guest_bug(d, "QXL_IO_MEMSLOT_DEL: val out of range");
            break;
        }
        qxl_del_memslot(d, val);
        break;
    case QXL_IO_CREATE_PRIMARY:
        if (val != 0) {
            qxl_guest_bug(d, "QXL_IO_CREATE_PRIMARY (async=%d): val != 0",
                          async);
            goto cancel_async;
        }
        d->guest_primary.surface = d->ram->create_surface;
        qxl_create_guest_primary(d, 0, async);
        break;
    case QXL_IO_DESTROY_PRIMARY:
        if (val != 0) {
            qxl_guest_bug(d, "QXL_IO_DESTROY_PRIMARY (async=%d): val != 0",
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
        if (val >= NUM_SURFACES) {
            qxl_guest_bug(d, "QXL_IO_DESTROY_SURFACE (async=%d):"
                             "%d >= NUM_SURFACES", async, val);
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
    default:
        fprintf(stderr, "%s: ioport=0x%x, abort()\n", __FUNCTION__, io_port);
        abort();
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

static uint64_t ioport_read(void *opaque, target_phys_addr_t addr,
                            unsigned size)
{
    PCIQXLDevice *d = opaque;

    trace_qxl_io_read_unexpected(d->id);
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

static void pipe_read(void *opaque)
{
    PCIQXLDevice *d = opaque;
    char dummy;
    int len;

    do {
        len = read(d->pipe[0], &dummy, sizeof(dummy));
    } while (len == sizeof(dummy));
    qxl_update_irq(d);
}

static void qxl_send_events(PCIQXLDevice *d, uint32_t events)
{
    uint32_t old_pending;
    uint32_t le_events = cpu_to_le32(events);

    assert(d->ssd.running);
    old_pending = __sync_fetch_and_or(&d->ram->int_pending, le_events);
    if ((old_pending & le_events) == le_events) {
        return;
    }
    if (qemu_thread_is_self(&d->main)) {
        qxl_update_irq(d);
    } else {
        if (write(d->pipe[1], d, 1) != 1) {
            dprint(d, 1, "%s: write to pipe failed\n", __FUNCTION__);
        }
    }
}

static void init_pipe_signaling(PCIQXLDevice *d)
{
    if (pipe(d->pipe) < 0) {
        fprintf(stderr, "%s:%s: qxl pipe creation failed\n",
                __FILE__, __func__);
        exit(1);
    }
    fcntl(d->pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(d->pipe[1], F_SETFL, O_NONBLOCK);
    fcntl(d->pipe[0], F_SETOWN, getpid());

    qemu_thread_get_self(&d->main);
    qemu_set_fd_handler(d->pipe[0], pipe_read, NULL, d);
}

/* graphics console */

static void qxl_hw_update(void *opaque)
{
    PCIQXLDevice *qxl = opaque;
    VGACommonState *vga = &qxl->vga;

    switch (qxl->mode) {
    case QXL_MODE_VGA:
        vga->update(vga);
        break;
    case QXL_MODE_COMPAT:
    case QXL_MODE_NATIVE:
        qxl_render_update(qxl);
        break;
    default:
        break;
    }
}

static void qxl_hw_invalidate(void *opaque)
{
    PCIQXLDevice *qxl = opaque;
    VGACommonState *vga = &qxl->vga;

    vga->invalidate(vga);
}

static void qxl_hw_screen_dump(void *opaque, const char *filename, bool cswitch)
{
    PCIQXLDevice *qxl = opaque;
    VGACommonState *vga = &qxl->vga;

    switch (qxl->mode) {
    case QXL_MODE_COMPAT:
    case QXL_MODE_NATIVE:
        qxl_render_update(qxl);
        ppm_save(filename, qxl->ssd.ds->surface);
        break;
    case QXL_MODE_VGA:
        vga->screen_dump(vga, filename, cswitch);
        break;
    default:
        break;
    }
}

static void qxl_hw_text_update(void *opaque, console_ch_t *chardata)
{
    PCIQXLDevice *qxl = opaque;
    VGACommonState *vga = &qxl->vga;

    if (qxl->mode == QXL_MODE_VGA) {
        vga->text_update(vga, chardata);
        return;
    }
}

static void qxl_dirty_surfaces(PCIQXLDevice *qxl)
{
    intptr_t vram_start;
    int i;

    if (qxl->mode != QXL_MODE_NATIVE && qxl->mode != QXL_MODE_COMPAT) {
        return;
    }

    /* dirty the primary surface */
    qxl_set_dirty(&qxl->vga.vram, qxl->shadow_rom.draw_area_offset,
                  qxl->shadow_rom.surface0_area_size);

    vram_start =  (intptr_t)memory_region_get_ram_ptr(&qxl->vram_bar);

    /* dirty the off-screen surfaces */
    for (i = 0; i < NUM_SURFACES; i++) {
        QXLSurfaceCmd *cmd;
        intptr_t surface_offset;
        int surface_size;

        if (qxl->guest_surfaces.cmds[i] == 0) {
            continue;
        }

        cmd = qxl_phys2virt(qxl, qxl->guest_surfaces.cmds[i],
                            MEMSLOT_GROUP_GUEST);
        assert(cmd->type == QXL_SURFACE_CMD_CREATE);
        surface_offset = (intptr_t)qxl_phys2virt(qxl,
                                                 cmd->u.surface_create.data,
                                                 MEMSLOT_GROUP_GUEST);
        surface_offset -= vram_start;
        surface_size = cmd->u.surface_create.height *
                       abs(cmd->u.surface_create.stride);
        trace_qxl_surfaces_dirty(qxl->id, i, (int)surface_offset, surface_size);
        qxl_set_dirty(&qxl->vram_bar, surface_offset, surface_size);
    }
}

static void qxl_vm_change_state_handler(void *opaque, int running,
                                        RunState state)
{
    PCIQXLDevice *qxl = opaque;
    qemu_spice_vm_change_state_handler(&qxl->ssd, running, state);

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

static void display_update(struct DisplayState *ds, int x, int y, int w, int h)
{
    if (qxl0->mode == QXL_MODE_VGA) {
        qemu_spice_display_update(&qxl0->ssd, x, y, w, h);
    }
}

static void display_resize(struct DisplayState *ds)
{
    if (qxl0->mode == QXL_MODE_VGA) {
        qemu_spice_display_resize(&qxl0->ssd);
    }
}

static void display_refresh(struct DisplayState *ds)
{
    if (qxl0->mode == QXL_MODE_VGA) {
        qemu_spice_display_refresh(&qxl0->ssd);
    } else {
        qemu_mutex_lock(&qxl0->ssd.lock);
        qemu_spice_cursor_refresh_unlocked(&qxl0->ssd);
        qemu_mutex_unlock(&qxl0->ssd.lock);
    }
}

static DisplayChangeListener display_listener = {
    .dpy_update  = display_update,
    .dpy_resize  = display_resize,
    .dpy_refresh = display_refresh,
};

static void qxl_init_ramsize(PCIQXLDevice *qxl, uint32_t ram_min_mb)
{
    /* vga ram (bar 0) */
    if (qxl->ram_size_mb != -1) {
        qxl->vga.vram_size = qxl->ram_size_mb * 1024 * 1024;
    }
    if (qxl->vga.vram_size < ram_min_mb * 1024 * 1024) {
        qxl->vga.vram_size = ram_min_mb * 1024 * 1024;
    }

    /* vram32 (surfaces, 32bit, bar 1) */
    if (qxl->vram32_size_mb != -1) {
        qxl->vram32_size = qxl->vram32_size_mb * 1024 * 1024;
    }
    if (qxl->vram32_size < 4096) {
        qxl->vram32_size = 4096;
    }

    /* vram (surfaces, 64bit, bar 4+5) */
    if (qxl->vram_size_mb != -1) {
        qxl->vram_size = qxl->vram_size_mb * 1024 * 1024;
    }
    if (qxl->vram_size < qxl->vram32_size) {
        qxl->vram_size = qxl->vram32_size;
    }

    if (qxl->revision == 1) {
        qxl->vram32_size = 4096;
        qxl->vram_size = 4096;
    }
    qxl->vga.vram_size = msb_mask(qxl->vga.vram_size * 2 - 1);
    qxl->vram32_size = msb_mask(qxl->vram32_size * 2 - 1);
    qxl->vram_size = msb_mask(qxl->vram_size * 2 - 1);
}

static int qxl_init_common(PCIQXLDevice *qxl)
{
    uint8_t* config = qxl->pci.config;
    uint32_t pci_device_rev;
    uint32_t io_size;

    qxl->mode = QXL_MODE_UNDEFINED;
    qxl->generation = 1;
    qxl->num_memslots = NUM_MEMSLOTS;
    qxl->num_surfaces = NUM_SURFACES;
    qemu_mutex_init(&qxl->track_lock);
    qemu_mutex_init(&qxl->async_lock);
    qxl->current_async = QXL_UNDEFINED_IO;

    switch (qxl->revision) {
    case 1: /* spice 0.4 -- qxl-1 */
        pci_device_rev = QXL_REVISION_STABLE_V04;
        break;
    case 2: /* spice 0.6 -- qxl-2 */
        pci_device_rev = QXL_REVISION_STABLE_V06;
        break;
    case 3: /* qxl-3 */
    default:
        pci_device_rev = QXL_DEFAULT_REVISION;
        break;
    }

    pci_set_byte(&config[PCI_REVISION_ID], pci_device_rev);
    pci_set_byte(&config[PCI_INTERRUPT_PIN], 1);

    qxl->rom_size = qxl_rom_size();
    memory_region_init_ram(&qxl->rom_bar, "qxl.vrom", qxl->rom_size);
    vmstate_register_ram(&qxl->rom_bar, &qxl->pci.qdev);
    init_qxl_rom(qxl);
    init_qxl_ram(qxl);

    memory_region_init_ram(&qxl->vram_bar, "qxl.vram", qxl->vram_size);
    vmstate_register_ram(&qxl->vram_bar, &qxl->pci.qdev);
    memory_region_init_alias(&qxl->vram32_bar, "qxl.vram32", &qxl->vram_bar,
                             0, qxl->vram32_size);

    io_size = msb_mask(QXL_IO_RANGE_SIZE * 2 - 1);
    if (qxl->revision == 1) {
        io_size = 8;
    }

    memory_region_init_io(&qxl->io_bar, &qxl_io_ops, qxl,
                          "qxl-ioports", io_size);
    if (qxl->id == 0) {
        vga_dirty_log_start(&qxl->vga);
    }


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
    dprint(qxl, 1, "ram/%s: %d MB [region 0]\n",
           qxl->id == 0 ? "pri" : "sec",
           qxl->vga.vram_size / (1024*1024));
    dprint(qxl, 1, "vram/32: %d MB [region 1]\n",
           qxl->vram32_size / (1024*1024));
    dprint(qxl, 1, "vram/64: %d MB %s\n",
           qxl->vram_size / (1024*1024),
           qxl->vram32_size < qxl->vram_size ? "[region 4]" : "[unmapped]");

    qxl->ssd.qxl.base.sif = &qxl_interface.base;
    qxl->ssd.qxl.id = qxl->id;
    qemu_spice_add_interface(&qxl->ssd.qxl.base);
    qemu_add_vm_change_state_handler(qxl_vm_change_state_handler, qxl);

    init_pipe_signaling(qxl);
    qxl_reset_state(qxl);

    qxl->update_area_bh = qemu_bh_new(qxl_render_update_area_bh, qxl);

    return 0;
}

static int qxl_init_primary(PCIDevice *dev)
{
    PCIQXLDevice *qxl = DO_UPCAST(PCIQXLDevice, pci, dev);
    VGACommonState *vga = &qxl->vga;
    PortioList *qxl_vga_port_list = g_new(PortioList, 1);

    qxl->id = 0;
    qxl_init_ramsize(qxl, 32);
    vga_common_init(vga, qxl->vga.vram_size);
    vga_init(vga, pci_address_space(dev), pci_address_space_io(dev), false);
    portio_list_init(qxl_vga_port_list, qxl_vga_portio_list, vga, "vga");
    portio_list_add(qxl_vga_port_list, pci_address_space_io(dev), 0x3b0);

    vga->ds = graphic_console_init(qxl_hw_update, qxl_hw_invalidate,
                                   qxl_hw_screen_dump, qxl_hw_text_update, qxl);
    qemu_spice_display_init_common(&qxl->ssd, vga->ds);

    qxl0 = qxl;
    register_displaychangelistener(vga->ds, &display_listener);

    return qxl_init_common(qxl);
}

static int qxl_init_secondary(PCIDevice *dev)
{
    static int device_id = 1;
    PCIQXLDevice *qxl = DO_UPCAST(PCIQXLDevice, pci, dev);

    qxl->id = device_id++;
    qxl_init_ramsize(qxl, 16);
    memory_region_init_ram(&qxl->vga.vram, "qxl.vgavram", qxl->vga.vram_size);
    vmstate_register_ram(&qxl->vga.vram, &qxl->pci.qdev);
    qxl->vga.vram_ptr = memory_region_get_ram_ptr(&qxl->vga.vram);

    return qxl_init_common(qxl);
}

static void qxl_pre_save(void *opaque)
{
    PCIQXLDevice* d = opaque;
    uint8_t *ram_start = d->vga.vram_ptr;

    trace_qxl_pre_save(d->id);
    if (d->last_release == NULL) {
        d->last_release_offset = 0;
    } else {
        d->last_release_offset = (uint8_t *)d->last_release - ram_start;
    }
    assert(d->last_release_offset < d->vga.vram_size);
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
        break;
    case QXL_MODE_VGA:
        qxl_create_memslots(d);
        qxl_enter_vga_mode(d);
        break;
    case QXL_MODE_NATIVE:
        qxl_create_memslots(d);
        qxl_create_guest_primary(d, 1, QXL_SYNC);

        /* replay surface-create and cursor-set commands */
        cmds = g_malloc0(sizeof(QXLCommandExt) * (NUM_SURFACES + 1));
        for (in = 0, out = 0; in < NUM_SURFACES; in++) {
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

static VMStateDescription qxl_memslot = {
    .name               = "qxl-memslot",
    .version_id         = QXL_SAVE_VERSION,
    .minimum_version_id = QXL_SAVE_VERSION,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(slot.mem_start, struct guest_slots),
        VMSTATE_UINT64(slot.mem_end,   struct guest_slots),
        VMSTATE_UINT32(active,         struct guest_slots),
        VMSTATE_END_OF_LIST()
    }
};

static VMStateDescription qxl_surface = {
    .name               = "qxl-surface",
    .version_id         = QXL_SAVE_VERSION,
    .minimum_version_id = QXL_SAVE_VERSION,
    .fields = (VMStateField[]) {
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

static VMStateDescription qxl_vmstate = {
    .name               = "qxl",
    .version_id         = QXL_SAVE_VERSION,
    .minimum_version_id = QXL_SAVE_VERSION,
    .pre_save           = qxl_pre_save,
    .pre_load           = qxl_pre_load,
    .post_load          = qxl_post_load,
    .fields = (VMStateField []) {
        VMSTATE_PCI_DEVICE(pci, PCIQXLDevice),
        VMSTATE_STRUCT(vga, PCIQXLDevice, 0, vmstate_vga_common, VGACommonState),
        VMSTATE_UINT32(shadow_rom.mode, PCIQXLDevice),
        VMSTATE_UINT32(num_free_res, PCIQXLDevice),
        VMSTATE_UINT32(last_release_offset, PCIQXLDevice),
        VMSTATE_UINT32(mode, PCIQXLDevice),
        VMSTATE_UINT32(ssd.unique, PCIQXLDevice),
        VMSTATE_INT32_EQUAL(num_memslots, PCIQXLDevice),
        VMSTATE_STRUCT_ARRAY(guest_slots, PCIQXLDevice, NUM_MEMSLOTS, 0,
                             qxl_memslot, struct guest_slots),
        VMSTATE_STRUCT(guest_primary.surface, PCIQXLDevice, 0,
                       qxl_surface, QXLSurfaceCreate),
        VMSTATE_INT32_EQUAL(num_surfaces, PCIQXLDevice),
        VMSTATE_ARRAY(guest_surfaces.cmds, PCIQXLDevice, NUM_SURFACES, 0,
                      vmstate_info_uint64, uint64_t),
        VMSTATE_UINT64(guest_cursor, PCIQXLDevice),
        VMSTATE_END_OF_LIST()
    },
};

static Property qxl_properties[] = {
        DEFINE_PROP_UINT32("ram_size", PCIQXLDevice, vga.vram_size,
                           64 * 1024 * 1024),
        DEFINE_PROP_UINT32("vram_size", PCIQXLDevice, vram32_size,
                           64 * 1024 * 1024),
        DEFINE_PROP_UINT32("revision", PCIQXLDevice, revision,
                           QXL_DEFAULT_REVISION),
        DEFINE_PROP_UINT32("debug", PCIQXLDevice, debug, 0),
        DEFINE_PROP_UINT32("guestdebug", PCIQXLDevice, guestdebug, 0),
        DEFINE_PROP_UINT32("cmdlog", PCIQXLDevice, cmdlog, 0),
        DEFINE_PROP_UINT32("ram_size_mb",  PCIQXLDevice, ram_size_mb, -1),
        DEFINE_PROP_UINT32("vram_size_mb", PCIQXLDevice, vram32_size_mb, 0),
        DEFINE_PROP_UINT32("vram64_size_mb", PCIQXLDevice, vram_size_mb, 0),
        DEFINE_PROP_END_OF_LIST(),
};

static void qxl_primary_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->no_hotplug = 1;
    k->init = qxl_init_primary;
    k->romfile = "vgabios-qxl.bin";
    k->vendor_id = REDHAT_PCI_VENDOR_ID;
    k->device_id = QXL_DEVICE_ID_STABLE;
    k->class_id = PCI_CLASS_DISPLAY_VGA;
    dc->desc = "Spice QXL GPU (primary, vga compatible)";
    dc->reset = qxl_reset_handler;
    dc->vmsd = &qxl_vmstate;
    dc->props = qxl_properties;
}

static TypeInfo qxl_primary_info = {
    .name          = "qxl-vga",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIQXLDevice),
    .class_init    = qxl_primary_class_init,
};

static void qxl_secondary_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = qxl_init_secondary;
    k->vendor_id = REDHAT_PCI_VENDOR_ID;
    k->device_id = QXL_DEVICE_ID_STABLE;
    k->class_id = PCI_CLASS_DISPLAY_OTHER;
    dc->desc = "Spice QXL GPU (secondary)";
    dc->reset = qxl_reset_handler;
    dc->vmsd = &qxl_vmstate;
    dc->props = qxl_properties;
}

static TypeInfo qxl_secondary_info = {
    .name          = "qxl",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIQXLDevice),
    .class_init    = qxl_secondary_class_init,
};

static void qxl_register_types(void)
{
    type_register_static(&qxl_primary_info);
    type_register_static(&qxl_secondary_info);
}

type_init(qxl_register_types)
