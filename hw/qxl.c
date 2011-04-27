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

#include <pthread.h>

#include "qemu-common.h"
#include "qemu-timer.h"
#include "qemu-queue.h"
#include "monitor.h"
#include "sysemu.h"

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
static void qxl_destroy_primary(PCIQXLDevice *d);
static void qxl_reset_memslots(PCIQXLDevice *d);
static void qxl_reset_surfaces(PCIQXLDevice *d);
static void qxl_ring_set_dirty(PCIQXLDevice *qxl);

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
    QXLRom *rom = qemu_get_ram_ptr(d->rom_offset);
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
    SPICE_RING_INIT(&d->ram->cmd_ring);
    SPICE_RING_INIT(&d->ram->cursor_ring);
    SPICE_RING_INIT(&d->ram->release_ring);
    SPICE_RING_PROD_ITEM(&d->ram->release_ring, item);
    *item = 0;
    qxl_ring_set_dirty(d);
}

/* can be called from spice server thread context */
static void qxl_set_dirty(ram_addr_t addr, ram_addr_t end)
{
    while (addr < end) {
        cpu_physical_memory_set_dirty(addr);
        addr += TARGET_PAGE_SIZE;
    }
}

static void qxl_rom_set_dirty(PCIQXLDevice *qxl)
{
    ram_addr_t addr = qxl->rom_offset;
    qxl_set_dirty(addr, addr + qxl->rom_size);
}

/* called from spice server thread context only */
static void qxl_ram_set_dirty(PCIQXLDevice *qxl, void *ptr)
{
    ram_addr_t addr = qxl->vga.vram_offset;
    void *base = qxl->vga.vram_ptr;
    intptr_t offset;

    offset = ptr - base;
    offset &= ~(TARGET_PAGE_SIZE-1);
    assert(offset < qxl->vga.vram_size);
    qxl_set_dirty(addr + offset, addr + offset + TARGET_PAGE_SIZE);
}

/* can be called from spice server thread context */
static void qxl_ring_set_dirty(PCIQXLDevice *qxl)
{
    ram_addr_t addr = qxl->vga.vram_offset + qxl->shadow_rom.ram_header_offset;
    ram_addr_t end  = qxl->vga.vram_offset + qxl->vga.vram_size;
    qxl_set_dirty(addr, end);
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
        break;
    }
    case QXL_CMD_CURSOR:
    {
        QXLCursorCmd *cmd = qxl_phys2virt(qxl, ext->cmd.data, ext->group_id);
        if (cmd->type == QXL_CURSOR_SET) {
            qxl->guest_cursor = ext->cmd.data;
        }
        break;
    }
    }
}

/* spice display interface callbacks */

static void interface_attach_worker(QXLInstance *sin, QXLWorker *qxl_worker)
{
    PCIQXLDevice *qxl = container_of(sin, PCIQXLDevice, ssd.qxl);

    dprint(qxl, 1, "%s:\n", __FUNCTION__);
    qxl->ssd.worker = qxl_worker;
}

static void interface_set_compression_level(QXLInstance *sin, int level)
{
    PCIQXLDevice *qxl = container_of(sin, PCIQXLDevice, ssd.qxl);

    dprint(qxl, 1, "%s: %d\n", __FUNCTION__, level);
    qxl->shadow_rom.compression_level = cpu_to_le32(level);
    qxl->rom->compression_level = cpu_to_le32(level);
    qxl_rom_set_dirty(qxl);
}

static void interface_set_mm_time(QXLInstance *sin, uint32_t mm_time)
{
    PCIQXLDevice *qxl = container_of(sin, PCIQXLDevice, ssd.qxl);

    qxl->shadow_rom.mm_clock = cpu_to_le32(mm_time);
    qxl->rom->mm_clock = cpu_to_le32(mm_time);
    qxl_rom_set_dirty(qxl);
}

static void interface_get_init_info(QXLInstance *sin, QXLDevInitInfo *info)
{
    PCIQXLDevice *qxl = container_of(sin, PCIQXLDevice, ssd.qxl);

    dprint(qxl, 1, "%s:\n", __FUNCTION__);
    info->memslot_gen_bits = MEMSLOT_GENERATION_BITS;
    info->memslot_id_bits = MEMSLOT_SLOT_BITS;
    info->num_memslots = NUM_MEMSLOTS;
    info->num_memslots_groups = NUM_MEMSLOTS_GROUPS;
    info->internal_groupslot_id = 0;
    info->qxl_ram_size = le32_to_cpu(qxl->shadow_rom.num_pages) << TARGET_PAGE_BITS;
    info->n_surfaces = NUM_SURFACES;
}

/* called from spice server thread context only */
static int interface_get_command(QXLInstance *sin, struct QXLCommandExt *ext)
{
    PCIQXLDevice *qxl = container_of(sin, PCIQXLDevice, ssd.qxl);
    SimpleSpiceUpdate *update;
    QXLCommandRing *ring;
    QXLCommand *cmd;
    int notify, ret;

    switch (qxl->mode) {
    case QXL_MODE_VGA:
        dprint(qxl, 2, "%s: vga\n", __FUNCTION__);
        ret = false;
        qemu_mutex_lock(&qxl->ssd.lock);
        if (qxl->ssd.update != NULL) {
            update = qxl->ssd.update;
            qxl->ssd.update = NULL;
            *ext = update->ext;
            ret = true;
        }
        qemu_mutex_unlock(&qxl->ssd.lock);
        qxl_log_command(qxl, "vga", ext);
        return ret;
    case QXL_MODE_COMPAT:
    case QXL_MODE_NATIVE:
    case QXL_MODE_UNDEFINED:
        dprint(qxl, 2, "%s: %s\n", __FUNCTION__,
               qxl->cmdflags ? "compat" : "native");
        ring = &qxl->ram->cmd_ring;
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
    dprint(d, 2, "free: push %d items, notify %s, ring %d/%d [%d,%d]\n",
           d->num_free_res, notify ? "yes" : "no",
           ring->prod - ring->cons, ring->num_items,
           ring->prod, ring->cons);
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
        qemu_spice_destroy_update(&qxl->ssd, (void*)ext.info->id);
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
    dprint(qxl, 3, "%4d\r", qxl->num_free_res);
    qxl_push_free_res(qxl, 0);
}

/* called from spice server thread context only */
static int interface_get_cursor_command(QXLInstance *sin, struct QXLCommandExt *ext)
{
    PCIQXLDevice *qxl = container_of(sin, PCIQXLDevice, ssd.qxl);
    QXLCursorRing *ring;
    QXLCommand *cmd;
    int notify;

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

    dprint(qxl, 1, "free: guest flush (have %d)\n", qxl->num_free_res);
    ret = qxl->num_free_res;
    if (ret) {
        qxl_push_free_res(qxl, 1);
    }
    return ret;
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
};

static void qxl_enter_vga_mode(PCIQXLDevice *d)
{
    if (d->mode == QXL_MODE_VGA) {
        return;
    }
    dprint(d, 1, "%s\n", __FUNCTION__);
    qemu_spice_create_host_primary(&d->ssd);
    d->mode = QXL_MODE_VGA;
    memset(&d->ssd.dirty, 0, sizeof(d->ssd.dirty));
}

static void qxl_exit_vga_mode(PCIQXLDevice *d)
{
    if (d->mode != QXL_MODE_VGA) {
        return;
    }
    dprint(d, 1, "%s\n", __FUNCTION__);
    qxl_destroy_primary(d);
}

static void qxl_set_irq(PCIQXLDevice *d)
{
    uint32_t pending = le32_to_cpu(d->ram->int_pending);
    uint32_t mask    = le32_to_cpu(d->ram->int_mask);
    int level = !!(pending & mask);
    qemu_set_irq(d->pci.irq[0], level);
    qxl_ring_set_dirty(d);
}

static void qxl_write_config(PCIDevice *d, uint32_t address,
                             uint32_t val, int len)
{
    PCIQXLDevice *qxl = DO_UPCAST(PCIQXLDevice, pci, d);
    VGACommonState *vga = &qxl->vga;

    vga_dirty_log_stop(vga);
    pci_default_write_config(d, address, val, len);
    if (vga->map_addr && qxl->pci.io_regions[0].addr == -1) {
        vga->map_addr = 0;
    }
    vga_dirty_log_start(vga);
}

static void qxl_check_state(PCIQXLDevice *d)
{
    QXLRam *ram = d->ram;

    assert(SPICE_RING_IS_EMPTY(&ram->cmd_ring));
    assert(SPICE_RING_IS_EMPTY(&ram->cursor_ring));
}

static void qxl_reset_state(PCIQXLDevice *d)
{
    QXLRam *ram = d->ram;
    QXLRom *rom = d->rom;

    assert(SPICE_RING_IS_EMPTY(&ram->cmd_ring));
    assert(SPICE_RING_IS_EMPTY(&ram->cursor_ring));
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
    dprint(d, 1, "%s:\n", __FUNCTION__);
    qxl_check_state(d);

    if (d->id == 0) {
        qxl_enter_vga_mode(d);
    } else {
        d->mode = QXL_MODE_UNDEFINED;
    }
}

static void qxl_hard_reset(PCIQXLDevice *d, int loadvm)
{
    dprint(d, 1, "%s: start%s\n", __FUNCTION__,
           loadvm ? " (loadvm)" : "");

    d->ssd.worker->reset_cursor(d->ssd.worker);
    d->ssd.worker->reset_image_cache(d->ssd.worker);
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

    dprint(d, 1, "%s: done\n", __FUNCTION__);
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

    if (qxl->mode != QXL_MODE_VGA) {
        dprint(qxl, 1, "%s\n", __FUNCTION__);
        qxl_destroy_primary(qxl);
        qxl_soft_reset(qxl);
    }
    vga_ioport_write(opaque, addr, val);
}

static void qxl_add_memslot(PCIQXLDevice *d, uint32_t slot_id, uint64_t delta)
{
    static const int regions[] = {
        QXL_RAM_RANGE_INDEX,
        QXL_VRAM_RANGE_INDEX,
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

    dprint(d, 1, "%s: slot %d: guest phys 0x%" PRIx64 " - 0x%" PRIx64 "\n",
           __FUNCTION__, slot_id,
           guest_start, guest_end);

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
        virt_start = (intptr_t)qemu_get_ram_ptr(d->vga.vram_offset);
        break;
    case QXL_VRAM_RANGE_INDEX:
        virt_start = (intptr_t)qemu_get_ram_ptr(d->vram_offset);
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

    dprint(d, 1, "%s: slot %d: host virt 0x%" PRIx64 " - 0x%" PRIx64 "\n",
           __FUNCTION__, memslot.slot_id,
           memslot.virt_start, memslot.virt_end);

    d->ssd.worker->add_memslot(d->ssd.worker, &memslot);
    d->guest_slots[slot_id].ptr = (void*)memslot.virt_start;
    d->guest_slots[slot_id].size = memslot.virt_end - memslot.virt_start;
    d->guest_slots[slot_id].delta = delta;
    d->guest_slots[slot_id].active = 1;
}

static void qxl_del_memslot(PCIQXLDevice *d, uint32_t slot_id)
{
    dprint(d, 1, "%s: slot %d\n", __FUNCTION__, slot_id);
    d->ssd.worker->del_memslot(d->ssd.worker, MEMSLOT_GROUP_HOST, slot_id);
    d->guest_slots[slot_id].active = 0;
}

static void qxl_reset_memslots(PCIQXLDevice *d)
{
    dprint(d, 1, "%s:\n", __FUNCTION__);
    d->ssd.worker->reset_memslots(d->ssd.worker);
    memset(&d->guest_slots, 0, sizeof(d->guest_slots));
}

static void qxl_reset_surfaces(PCIQXLDevice *d)
{
    dprint(d, 1, "%s:\n", __FUNCTION__);
    d->mode = QXL_MODE_UNDEFINED;
    d->ssd.worker->destroy_surfaces(d->ssd.worker);
    memset(&d->guest_surfaces.cmds, 0, sizeof(d->guest_surfaces.cmds));
}

/* called from spice server thread context only */
void *qxl_phys2virt(PCIQXLDevice *qxl, QXLPHYSICAL pqxl, int group_id)
{
    uint64_t phys   = le64_to_cpu(pqxl);
    uint32_t slot   = (phys >> (64 -  8)) & 0xff;
    uint64_t offset = phys & 0xffffffffffff;

    switch (group_id) {
    case MEMSLOT_GROUP_HOST:
        return (void*)offset;
    case MEMSLOT_GROUP_GUEST:
        PANIC_ON(slot > NUM_MEMSLOTS);
        PANIC_ON(!qxl->guest_slots[slot].active);
        PANIC_ON(offset < qxl->guest_slots[slot].delta);
        offset -= qxl->guest_slots[slot].delta;
        PANIC_ON(offset > qxl->guest_slots[slot].size)
        return qxl->guest_slots[slot].ptr + offset;
    default:
        PANIC_ON(1);
    }
}

static void qxl_create_guest_primary(PCIQXLDevice *qxl, int loadvm)
{
    QXLDevSurfaceCreate surface;
    QXLSurfaceCreate *sc = &qxl->guest_primary.surface;

    assert(qxl->mode != QXL_MODE_NATIVE);
    qxl_exit_vga_mode(qxl);

    dprint(qxl, 1, "%s: %dx%d\n", __FUNCTION__,
           le32_to_cpu(sc->width), le32_to_cpu(sc->height));

    surface.format     = le32_to_cpu(sc->format);
    surface.height     = le32_to_cpu(sc->height);
    surface.mem        = le64_to_cpu(sc->mem);
    surface.position   = le32_to_cpu(sc->position);
    surface.stride     = le32_to_cpu(sc->stride);
    surface.width      = le32_to_cpu(sc->width);
    surface.type       = le32_to_cpu(sc->type);
    surface.flags      = le32_to_cpu(sc->flags);

    surface.mouse_mode = true;
    surface.group_id   = MEMSLOT_GROUP_GUEST;
    if (loadvm) {
        surface.flags |= QXL_SURF_FLAG_KEEP_DATA;
    }

    qxl->mode = QXL_MODE_NATIVE;
    qxl->cmdflags = 0;
    qxl->ssd.worker->create_primary_surface(qxl->ssd.worker, 0, &surface);

    /* for local rendering */
    qxl_render_resize(qxl);
}

static void qxl_destroy_primary(PCIQXLDevice *d)
{
    if (d->mode == QXL_MODE_UNDEFINED) {
        return;
    }

    dprint(d, 1, "%s\n", __FUNCTION__);

    d->mode = QXL_MODE_UNDEFINED;
    d->ssd.worker->destroy_primary_surface(d->ssd.worker, 0);
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

    dprint(d, 1, "%s: mode %d  [ %d x %d @ %d bpp devmem 0x%lx ]\n", __FUNCTION__,
           modenr, mode->x_res, mode->y_res, mode->bits, devmem);
    if (!loadvm) {
        qxl_hard_reset(d, 0);
    }

    d->guest_slots[0].slot = slot;
    qxl_add_memslot(d, 0, devmem);

    d->guest_primary.surface = surface;
    qxl_create_guest_primary(d, 0);

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

static void ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    PCIQXLDevice *d = opaque;
    uint32_t io_port = addr - d->io_base;

    switch (io_port) {
    case QXL_IO_RESET:
    case QXL_IO_SET_MODE:
    case QXL_IO_MEMSLOT_ADD:
    case QXL_IO_MEMSLOT_DEL:
    case QXL_IO_CREATE_PRIMARY:
        break;
    default:
        if (d->mode == QXL_MODE_NATIVE || d->mode == QXL_MODE_COMPAT)
            break;
        dprint(d, 1, "%s: unexpected port 0x%x in vga mode\n", __FUNCTION__, io_port);
        return;
    }

    switch (io_port) {
    case QXL_IO_UPDATE_AREA:
    {
        QXLRect update = d->ram->update_area;
        d->ssd.worker->update_area(d->ssd.worker, d->ram->update_surface,
                                   &update, NULL, 0, 0);
        break;
    }
    case QXL_IO_NOTIFY_CMD:
        d->ssd.worker->wakeup(d->ssd.worker);
        break;
    case QXL_IO_NOTIFY_CURSOR:
        d->ssd.worker->wakeup(d->ssd.worker);
        break;
    case QXL_IO_UPDATE_IRQ:
        qxl_set_irq(d);
        break;
    case QXL_IO_NOTIFY_OOM:
        if (!SPICE_RING_IS_EMPTY(&d->ram->release_ring)) {
            break;
        }
        pthread_yield();
        if (!SPICE_RING_IS_EMPTY(&d->ram->release_ring)) {
            break;
        }
        d->oom_running = 1;
        d->ssd.worker->oom(d->ssd.worker);
        d->oom_running = 0;
        break;
    case QXL_IO_SET_MODE:
        dprint(d, 1, "QXL_SET_MODE %d\n", val);
        qxl_set_mode(d, val, 0);
        break;
    case QXL_IO_LOG:
        if (d->guestdebug) {
            fprintf(stderr, "qxl/guest: %s", d->ram->log_buf);
        }
        break;
    case QXL_IO_RESET:
        dprint(d, 1, "QXL_IO_RESET\n");
        qxl_hard_reset(d, 0);
        break;
    case QXL_IO_MEMSLOT_ADD:
        PANIC_ON(val >= NUM_MEMSLOTS);
        PANIC_ON(d->guest_slots[val].active);
        d->guest_slots[val].slot = d->ram->mem_slot;
        qxl_add_memslot(d, val, 0);
        break;
    case QXL_IO_MEMSLOT_DEL:
        qxl_del_memslot(d, val);
        break;
    case QXL_IO_CREATE_PRIMARY:
        PANIC_ON(val != 0);
        dprint(d, 1, "QXL_IO_CREATE_PRIMARY\n");
        d->guest_primary.surface = d->ram->create_surface;
        qxl_create_guest_primary(d, 0);
        break;
    case QXL_IO_DESTROY_PRIMARY:
        PANIC_ON(val != 0);
        dprint(d, 1, "QXL_IO_DESTROY_PRIMARY\n");
        qxl_destroy_primary(d);
        break;
    case QXL_IO_DESTROY_SURFACE_WAIT:
        d->ssd.worker->destroy_surface_wait(d->ssd.worker, val);
        break;
    case QXL_IO_DESTROY_ALL_SURFACES:
        d->ssd.worker->destroy_surfaces(d->ssd.worker);
        break;
    default:
        fprintf(stderr, "%s: ioport=0x%x, abort()\n", __FUNCTION__, io_port);
        abort();
    }
}

static uint32_t ioport_read(void *opaque, uint32_t addr)
{
    PCIQXLDevice *d = opaque;

    dprint(d, 1, "%s: unexpected\n", __FUNCTION__);
    return 0xff;
}

static void qxl_map(PCIDevice *pci, int region_num,
                    pcibus_t addr, pcibus_t size, int type)
{
    static const char *names[] = {
        [ QXL_IO_RANGE_INDEX ]   = "ioports",
        [ QXL_RAM_RANGE_INDEX ]  = "devram",
        [ QXL_ROM_RANGE_INDEX ]  = "rom",
        [ QXL_VRAM_RANGE_INDEX ] = "vram",
    };
    PCIQXLDevice *qxl = DO_UPCAST(PCIQXLDevice, pci, pci);

    dprint(qxl, 1, "%s: bar %d [%s] addr 0x%lx size 0x%lx\n", __FUNCTION__,
            region_num, names[region_num], addr, size);

    switch (region_num) {
    case QXL_IO_RANGE_INDEX:
        register_ioport_write(addr, size, 1, ioport_write, pci);
        register_ioport_read(addr, size, 1, ioport_read, pci);
        qxl->io_base = addr;
        break;
    case QXL_RAM_RANGE_INDEX:
        cpu_register_physical_memory(addr, size, qxl->vga.vram_offset | IO_MEM_RAM);
        qxl->vga.map_addr = addr;
        qxl->vga.map_end = addr + size;
        if (qxl->id == 0) {
            vga_dirty_log_start(&qxl->vga);
        }
        break;
    case QXL_ROM_RANGE_INDEX:
        cpu_register_physical_memory(addr, size, qxl->rom_offset | IO_MEM_ROM);
        break;
    case QXL_VRAM_RANGE_INDEX:
        cpu_register_physical_memory(addr, size, qxl->vram_offset | IO_MEM_RAM);
        break;
    }
}

static void pipe_read(void *opaque)
{
    PCIQXLDevice *d = opaque;
    char dummy;
    int len;

    do {
        len = read(d->pipe[0], &dummy, sizeof(dummy));
    } while (len == sizeof(dummy));
    qxl_set_irq(d);
}

/* called from spice server thread context only */
static void qxl_send_events(PCIQXLDevice *d, uint32_t events)
{
    uint32_t old_pending;
    uint32_t le_events = cpu_to_le32(events);

    assert(d->ssd.running);
    old_pending = __sync_fetch_and_or(&d->ram->int_pending, le_events);
    if ((old_pending & le_events) == le_events) {
        return;
    }
    if (pthread_self() == d->main) {
        qxl_set_irq(d);
    } else {
        if (write(d->pipe[1], d, 1) != 1) {
            dprint(d, 1, "%s: write to pipe failed\n", __FUNCTION__);
        }
    }
}

static void init_pipe_signaling(PCIQXLDevice *d)
{
   if (pipe(d->pipe) < 0) {
       dprint(d, 1, "%s: pipe creation failed\n", __FUNCTION__);
       return;
   }
#ifdef CONFIG_IOTHREAD
   fcntl(d->pipe[0], F_SETFL, O_NONBLOCK);
#else
   fcntl(d->pipe[0], F_SETFL, O_NONBLOCK /* | O_ASYNC */);
#endif
   fcntl(d->pipe[1], F_SETFL, O_NONBLOCK);
   fcntl(d->pipe[0], F_SETOWN, getpid());

   d->main = pthread_self();
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

static void qxl_hw_screen_dump(void *opaque, const char *filename)
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
        vga->screen_dump(vga, filename);
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

static void qxl_vm_change_state_handler(void *opaque, int running, int reason)
{
    PCIQXLDevice *qxl = opaque;
    qemu_spice_vm_change_state_handler(&qxl->ssd, running, reason);

    if (!running && qxl->mode == QXL_MODE_NATIVE) {
        /* dirty all vram (which holds surfaces) to make sure it is saved */
        /* FIXME #1: should go out during "live" stage */
        /* FIXME #2: we only need to save the areas which are actually used */
        ram_addr_t addr = qxl->vram_offset;
        qxl_set_dirty(addr, addr + qxl->vram_size);
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
    }
}

static DisplayChangeListener display_listener = {
    .dpy_update  = display_update,
    .dpy_resize  = display_resize,
    .dpy_refresh = display_refresh,
};

static int qxl_init_common(PCIQXLDevice *qxl)
{
    uint8_t* config = qxl->pci.config;
    uint32_t pci_device_id;
    uint32_t pci_device_rev;
    uint32_t io_size;

    qxl->mode = QXL_MODE_UNDEFINED;
    qxl->generation = 1;
    qxl->num_memslots = NUM_MEMSLOTS;
    qxl->num_surfaces = NUM_SURFACES;

    switch (qxl->revision) {
    case 1: /* spice 0.4 -- qxl-1 */
        pci_device_id  = QXL_DEVICE_ID_STABLE;
        pci_device_rev = QXL_REVISION_STABLE_V04;
        break;
    case 2: /* spice 0.6 -- qxl-2 */
        pci_device_id  = QXL_DEVICE_ID_STABLE;
        pci_device_rev = QXL_REVISION_STABLE_V06;
        break;
    default: /* experimental */
        pci_device_id  = QXL_DEVICE_ID_DEVEL;
        pci_device_rev = 1;
        break;
    }

    pci_config_set_vendor_id(config, REDHAT_PCI_VENDOR_ID);
    pci_config_set_device_id(config, pci_device_id);
    pci_set_byte(&config[PCI_REVISION_ID], pci_device_rev);
    pci_set_byte(&config[PCI_INTERRUPT_PIN], 1);

    qxl->rom_size = qxl_rom_size();
    qxl->rom_offset = qemu_ram_alloc(&qxl->pci.qdev, "qxl.vrom", qxl->rom_size);
    init_qxl_rom(qxl);
    init_qxl_ram(qxl);

    if (qxl->vram_size < 16 * 1024 * 1024) {
        qxl->vram_size = 16 * 1024 * 1024;
    }
    if (qxl->revision == 1) {
        qxl->vram_size = 4096;
    }
    qxl->vram_size = msb_mask(qxl->vram_size * 2 - 1);
    qxl->vram_offset = qemu_ram_alloc(&qxl->pci.qdev, "qxl.vram", qxl->vram_size);

    io_size = msb_mask(QXL_IO_RANGE_SIZE * 2 - 1);
    if (qxl->revision == 1) {
        io_size = 8;
    }

    pci_register_bar(&qxl->pci, QXL_IO_RANGE_INDEX,
                     io_size, PCI_BASE_ADDRESS_SPACE_IO, qxl_map);

    pci_register_bar(&qxl->pci, QXL_ROM_RANGE_INDEX,
                     qxl->rom_size, PCI_BASE_ADDRESS_SPACE_MEMORY,
                     qxl_map);

    pci_register_bar(&qxl->pci, QXL_RAM_RANGE_INDEX,
                     qxl->vga.vram_size, PCI_BASE_ADDRESS_SPACE_MEMORY,
                     qxl_map);

    pci_register_bar(&qxl->pci, QXL_VRAM_RANGE_INDEX, qxl->vram_size,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, qxl_map);

    qxl->ssd.qxl.base.sif = &qxl_interface.base;
    qxl->ssd.qxl.id = qxl->id;
    qemu_spice_add_interface(&qxl->ssd.qxl.base);
    qemu_add_vm_change_state_handler(qxl_vm_change_state_handler, qxl);

    init_pipe_signaling(qxl);
    qxl_reset_state(qxl);

    return 0;
}

static int qxl_init_primary(PCIDevice *dev)
{
    PCIQXLDevice *qxl = DO_UPCAST(PCIQXLDevice, pci, dev);
    VGACommonState *vga = &qxl->vga;
    ram_addr_t ram_size = msb_mask(qxl->vga.vram_size * 2 - 1);

    qxl->id = 0;

    if (ram_size < 32 * 1024 * 1024) {
        ram_size = 32 * 1024 * 1024;
    }
    vga_common_init(vga, ram_size);
    vga_init(vga);
    register_ioport_write(0x3c0, 16, 1, qxl_vga_ioport_write, vga);
    register_ioport_write(0x3b4,  2, 1, qxl_vga_ioport_write, vga);
    register_ioport_write(0x3d4,  2, 1, qxl_vga_ioport_write, vga);
    register_ioport_write(0x3ba,  1, 1, qxl_vga_ioport_write, vga);
    register_ioport_write(0x3da,  1, 1, qxl_vga_ioport_write, vga);

    vga->ds = graphic_console_init(qxl_hw_update, qxl_hw_invalidate,
                                   qxl_hw_screen_dump, qxl_hw_text_update, qxl);
    qxl->ssd.ds = vga->ds;
    qemu_mutex_init(&qxl->ssd.lock);
    qxl->ssd.mouse_x = -1;
    qxl->ssd.mouse_y = -1;
    qxl->ssd.bufsize = (16 * 1024 * 1024);
    qxl->ssd.buf = qemu_malloc(qxl->ssd.bufsize);

    qxl0 = qxl;
    register_displaychangelistener(vga->ds, &display_listener);

    pci_config_set_class(dev->config, PCI_CLASS_DISPLAY_VGA);
    return qxl_init_common(qxl);
}

static int qxl_init_secondary(PCIDevice *dev)
{
    static int device_id = 1;
    PCIQXLDevice *qxl = DO_UPCAST(PCIQXLDevice, pci, dev);
    ram_addr_t ram_size = msb_mask(qxl->vga.vram_size * 2 - 1);

    qxl->id = device_id++;

    if (ram_size < 16 * 1024 * 1024) {
        ram_size = 16 * 1024 * 1024;
    }
    qxl->vga.vram_size = ram_size;
    qxl->vga.vram_offset = qemu_ram_alloc(&qxl->pci.qdev, "qxl.vgavram",
                                          qxl->vga.vram_size);
    qxl->vga.vram_ptr = qemu_get_ram_ptr(qxl->vga.vram_offset);

    pci_config_set_class(dev->config, PCI_CLASS_DISPLAY_OTHER);
    return qxl_init_common(qxl);
}

static void qxl_pre_save(void *opaque)
{
    PCIQXLDevice* d = opaque;
    uint8_t *ram_start = d->vga.vram_ptr;

    dprint(d, 1, "%s:\n", __FUNCTION__);
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

    dprint(d, 1, "%s: start\n", __FUNCTION__);
    qxl_hard_reset(d, 1);
    qxl_exit_vga_mode(d);
    dprint(d, 1, "%s: done\n", __FUNCTION__);
    return 0;
}

static int qxl_post_load(void *opaque, int version)
{
    PCIQXLDevice* d = opaque;
    uint8_t *ram_start = d->vga.vram_ptr;
    QXLCommandExt *cmds;
    int in, out, i, newmode;

    dprint(d, 1, "%s: start\n", __FUNCTION__);

    assert(d->last_release_offset < d->vga.vram_size);
    if (d->last_release_offset == 0) {
        d->last_release = NULL;
    } else {
        d->last_release = (QXLReleaseInfo *)(ram_start + d->last_release_offset);
    }

    d->modes = (QXLModes*)((uint8_t*)d->rom + d->rom->modes_offset);

    dprint(d, 1, "%s: restore mode\n", __FUNCTION__);
    newmode = d->mode;
    d->mode = QXL_MODE_UNDEFINED;
    switch (newmode) {
    case QXL_MODE_UNDEFINED:
        break;
    case QXL_MODE_VGA:
        qxl_enter_vga_mode(d);
        break;
    case QXL_MODE_NATIVE:
        for (i = 0; i < NUM_MEMSLOTS; i++) {
            if (!d->guest_slots[i].active) {
                continue;
            }
            qxl_add_memslot(d, i, 0);
        }
        qxl_create_guest_primary(d, 1);

        /* replay surface-create and cursor-set commands */
        cmds = qemu_mallocz(sizeof(QXLCommandExt) * (NUM_SURFACES + 1));
        for (in = 0, out = 0; in < NUM_SURFACES; in++) {
            if (d->guest_surfaces.cmds[in] == 0) {
                continue;
            }
            cmds[out].cmd.data = d->guest_surfaces.cmds[in];
            cmds[out].cmd.type = QXL_CMD_SURFACE;
            cmds[out].group_id = MEMSLOT_GROUP_GUEST;
            out++;
        }
        cmds[out].cmd.data = d->guest_cursor;
        cmds[out].cmd.type = QXL_CMD_CURSOR;
        cmds[out].group_id = MEMSLOT_GROUP_GUEST;
        out++;
        d->ssd.worker->loadvm_commands(d->ssd.worker, cmds, out);
        qemu_free(cmds);

        break;
    case QXL_MODE_COMPAT:
        qxl_set_mode(d, d->shadow_rom.mode, 1);
        break;
    }
    dprint(d, 1, "%s: done\n", __FUNCTION__);

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

static PCIDeviceInfo qxl_info_primary = {
    .qdev.name    = "qxl-vga",
    .qdev.desc    = "Spice QXL GPU (primary, vga compatible)",
    .qdev.size    = sizeof(PCIQXLDevice),
    .qdev.reset   = qxl_reset_handler,
    .qdev.vmsd    = &qxl_vmstate,
    .no_hotplug   = 1,
    .init         = qxl_init_primary,
    .config_write = qxl_write_config,
    .romfile      = "vgabios-qxl.bin",
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("ram_size", PCIQXLDevice, vga.vram_size, 64 * 1024 * 1024),
        DEFINE_PROP_UINT32("vram_size", PCIQXLDevice, vram_size, 64 * 1024 * 1024),
        DEFINE_PROP_UINT32("revision", PCIQXLDevice, revision, 2),
        DEFINE_PROP_UINT32("debug", PCIQXLDevice, debug, 0),
        DEFINE_PROP_UINT32("guestdebug", PCIQXLDevice, guestdebug, 0),
        DEFINE_PROP_UINT32("cmdlog", PCIQXLDevice, cmdlog, 0),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static PCIDeviceInfo qxl_info_secondary = {
    .qdev.name    = "qxl",
    .qdev.desc    = "Spice QXL GPU (secondary)",
    .qdev.size    = sizeof(PCIQXLDevice),
    .qdev.reset   = qxl_reset_handler,
    .qdev.vmsd    = &qxl_vmstate,
    .init         = qxl_init_secondary,
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("ram_size", PCIQXLDevice, vga.vram_size, 64 * 1024 * 1024),
        DEFINE_PROP_UINT32("vram_size", PCIQXLDevice, vram_size, 64 * 1024 * 1024),
        DEFINE_PROP_UINT32("revision", PCIQXLDevice, revision, 2),
        DEFINE_PROP_UINT32("debug", PCIQXLDevice, debug, 0),
        DEFINE_PROP_UINT32("guestdebug", PCIQXLDevice, guestdebug, 0),
        DEFINE_PROP_UINT32("cmdlog", PCIQXLDevice, cmdlog, 0),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void qxl_register(void)
{
    pci_qdev_register(&qxl_info_primary);
    pci_qdev_register(&qxl_info_secondary);
}

device_init(qxl_register);
