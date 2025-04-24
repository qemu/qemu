/*
 * QEMU Apple ParavirtualizedGraphics.framework device
 *
 * Copyright © 2023 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * ParavirtualizedGraphics.framework is a set of libraries that macOS provides
 * which implements 3d graphics passthrough to the host as well as a
 * proprietary guest communication channel to drive it. This device model
 * implements support to drive that library from within QEMU.
 */

#include "qemu/osdep.h"
#include "qemu/lockable.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "qapi/visitor.h"
#include "qapi/error.h"
#include "block/aio-wait.h"
#include "system/address-spaces.h"
#include "system/dma.h"
#include "migration/blocker.h"
#include "ui/console.h"
#include "apple-gfx.h"
#include "trace.h"

#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <dispatch/dispatch.h>

#import <ParavirtualizedGraphics/ParavirtualizedGraphics.h>

static const AppleGFXDisplayMode apple_gfx_default_modes[] = {
    { 1920, 1080, 60 },
    { 1440, 1080, 60 },
    { 1280, 1024, 60 },
};

static Error *apple_gfx_mig_blocker;
static uint32_t next_pgdisplay_serial_num = 1;

static dispatch_queue_t get_background_queue(void)
{
    return dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
}

/* ------ PGTask and task operations: new/destroy/map/unmap ------ */

/*
 * This implements the type declared in <ParavirtualizedGraphics/PGDevice.h>
 * which is opaque from the framework's point of view. It is used in callbacks
 * in the form of its typedef PGTask_t, which also already exists in the
 * framework headers.
 *
 * A "task" in PVG terminology represents a host-virtual contiguous address
 * range which is reserved in a large chunk on task creation. The mapMemory
 * callback then requests ranges of guest system memory (identified by their
 * GPA) to be mapped into subranges of this reserved address space.
 * This type of operation isn't well-supported by QEMU's memory subsystem,
 * but it is fortunately trivial to achieve with Darwin's mach_vm_remap() call,
 * which allows us to refer to the same backing memory via multiple virtual
 * address ranges. The Mach VM APIs are therefore used throughout for managing
 * task memory.
 */
struct PGTask_s {
    QTAILQ_ENTRY(PGTask_s) node;
    AppleGFXState *s;
    mach_vm_address_t address;
    uint64_t len;
    /*
     * All unique MemoryRegions for which a mapping has been created in in this
     * task, and on which we have thus called memory_region_ref(). There are
     * usually very few regions of system RAM in total, so we expect this array
     * to be very short. Therefore, no need for sorting or fancy search
     * algorithms, linear search will do.
     * Protected by AppleGFXState's task_mutex.
     */
    GPtrArray *mapped_regions;
};

static PGTask_t *apple_gfx_new_task(AppleGFXState *s, uint64_t len)
{
    mach_vm_address_t task_mem;
    PGTask_t *task;
    kern_return_t r;

    r = mach_vm_allocate(mach_task_self(), &task_mem, len, VM_FLAGS_ANYWHERE);
    if (r != KERN_SUCCESS) {
        return NULL;
    }

    task = g_new0(PGTask_t, 1);
    task->s = s;
    task->address = task_mem;
    task->len = len;
    task->mapped_regions = g_ptr_array_sized_new(2 /* Usually enough */);

    QEMU_LOCK_GUARD(&s->task_mutex);
    QTAILQ_INSERT_TAIL(&s->tasks, task, node);

    return task;
}

static void apple_gfx_destroy_task(AppleGFXState *s, PGTask_t *task)
{
    GPtrArray *regions = task->mapped_regions;
    MemoryRegion *region;
    size_t i;

    for (i = 0; i < regions->len; ++i) {
        region = g_ptr_array_index(regions, i);
        memory_region_unref(region);
    }
    g_ptr_array_unref(regions);

    mach_vm_deallocate(mach_task_self(), task->address, task->len);

    QEMU_LOCK_GUARD(&s->task_mutex);
    QTAILQ_REMOVE(&s->tasks, task, node);
    g_free(task);
}

void *apple_gfx_host_ptr_for_gpa_range(uint64_t guest_physical,
                                       uint64_t length, bool read_only,
                                       MemoryRegion **mapping_in_region)
{
    MemoryRegion *ram_region;
    char *host_ptr;
    hwaddr ram_region_offset = 0;
    hwaddr ram_region_length = length;

    ram_region = address_space_translate(&address_space_memory,
                                         guest_physical,
                                         &ram_region_offset,
                                         &ram_region_length, !read_only,
                                         MEMTXATTRS_UNSPECIFIED);

    if (!ram_region || ram_region_length < length ||
        !memory_access_is_direct(ram_region, !read_only,
				 MEMTXATTRS_UNSPECIFIED)) {
        return NULL;
    }

    host_ptr = memory_region_get_ram_ptr(ram_region);
    if (!host_ptr) {
        return NULL;
    }
    host_ptr += ram_region_offset;
    *mapping_in_region = ram_region;
    return host_ptr;
}

static bool apple_gfx_task_map_memory(AppleGFXState *s, PGTask_t *task,
                                      uint64_t virtual_offset,
                                      PGPhysicalMemoryRange_t *ranges,
                                      uint32_t range_count, bool read_only)
{
    kern_return_t r;
    void *source_ptr;
    mach_vm_address_t target;
    vm_prot_t cur_protection, max_protection;
    bool success = true;
    MemoryRegion *region;

    RCU_READ_LOCK_GUARD();
    QEMU_LOCK_GUARD(&s->task_mutex);

    trace_apple_gfx_map_memory(task, range_count, virtual_offset, read_only);
    for (int i = 0; i < range_count; i++) {
        PGPhysicalMemoryRange_t *range = &ranges[i];

        target = task->address + virtual_offset;
        virtual_offset += range->physicalLength;

        trace_apple_gfx_map_memory_range(i, range->physicalAddress,
                                         range->physicalLength);

        region = NULL;
        source_ptr = apple_gfx_host_ptr_for_gpa_range(range->physicalAddress,
                                                      range->physicalLength,
                                                      read_only, &region);
        if (!source_ptr) {
            success = false;
            continue;
        }

        if (!g_ptr_array_find(task->mapped_regions, region, NULL)) {
            g_ptr_array_add(task->mapped_regions, region);
            memory_region_ref(region);
        }

        cur_protection = 0;
        max_protection = 0;
        /* Map guest RAM at range->physicalAddress into PG task memory range */
        r = mach_vm_remap(mach_task_self(),
                          &target, range->physicalLength, vm_page_size - 1,
                          VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE,
                          mach_task_self(), (mach_vm_address_t)source_ptr,
                          false /* shared mapping, no copy */,
                          &cur_protection, &max_protection,
                          VM_INHERIT_COPY);
        trace_apple_gfx_remap(r, source_ptr, target);
        g_assert(r == KERN_SUCCESS);
    }

    return success;
}

static void apple_gfx_task_unmap_memory(AppleGFXState *s, PGTask_t *task,
                                        uint64_t virtual_offset, uint64_t length)
{
    kern_return_t r;
    mach_vm_address_t range_address;

    trace_apple_gfx_unmap_memory(task, virtual_offset, length);

    /*
     * Replace task memory range with fresh 0 pages, undoing the mapping
     * from guest RAM.
     */
    range_address = task->address + virtual_offset;
    r = mach_vm_allocate(mach_task_self(), &range_address, length,
                         VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE);
    g_assert(r == KERN_SUCCESS);
}

/* ------ Rendering and frame management ------ */

static void apple_gfx_render_frame_completed_bh(void *opaque);

static void apple_gfx_render_new_frame(AppleGFXState *s)
{
    bool managed_texture = s->using_managed_texture_storage;
    uint32_t width = surface_width(s->surface);
    uint32_t height = surface_height(s->surface);
    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    id<MTLCommandBuffer> command_buffer = [s->mtl_queue commandBuffer];
    id<MTLTexture> texture = s->texture;

    assert(bql_locked());
    [texture retain];
    [command_buffer retain];

    s->rendering_frame_width = width;
    s->rendering_frame_height = height;

    dispatch_async(get_background_queue(), ^{
        /*
         * This is not safe to call from the BQL/BH due to PVG-internal locks
         * causing deadlocks.
         */
        bool r = [s->pgdisp encodeCurrentFrameToCommandBuffer:command_buffer
                                                 texture:texture
                                                  region:region];
        if (!r) {
            [texture release];
            [command_buffer release];
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: encodeCurrentFrameToCommandBuffer:texture:region: "
                          "failed\n", __func__);
            bql_lock();
            --s->pending_frames;
            if (s->pending_frames > 0) {
                apple_gfx_render_new_frame(s);
            }
            bql_unlock();
            return;
        }

        if (managed_texture) {
            /* "Managed" textures exist in both VRAM and RAM and must be synced. */
            id<MTLBlitCommandEncoder> blit = [command_buffer blitCommandEncoder];
            [blit synchronizeResource:texture];
            [blit endEncoding];
        }
        [texture release];
        [command_buffer addCompletedHandler:
            ^(id<MTLCommandBuffer> cb)
            {
                aio_bh_schedule_oneshot(qemu_get_aio_context(),
                                        apple_gfx_render_frame_completed_bh, s);
            }];
        [command_buffer commit];
        [command_buffer release];
    });
}

static void copy_mtl_texture_to_surface_mem(id<MTLTexture> texture, void *vram)
{
    /*
     * TODO: Skip this entirely on a pure Metal or headless/guest-only
     * rendering path, else use a blit command encoder? Needs careful
     * (double?) buffering design.
     */
    size_t width = texture.width, height = texture.height;
    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    [texture getBytes:vram
          bytesPerRow:(width * 4)
        bytesPerImage:(width * height * 4)
           fromRegion:region
          mipmapLevel:0
                slice:0];
}

static void apple_gfx_render_frame_completed_bh(void *opaque)
{
    AppleGFXState *s = opaque;

    @autoreleasepool {
        --s->pending_frames;
        assert(s->pending_frames >= 0);

        /* Only update display if mode hasn't changed since we started rendering. */
        if (s->rendering_frame_width == surface_width(s->surface) &&
            s->rendering_frame_height == surface_height(s->surface)) {
            copy_mtl_texture_to_surface_mem(s->texture, surface_data(s->surface));
            if (s->gfx_update_requested) {
                s->gfx_update_requested = false;
                dpy_gfx_update_full(s->con);
                graphic_hw_update_done(s->con);
                s->new_frame_ready = false;
            } else {
                s->new_frame_ready = true;
            }
        }
        if (s->pending_frames > 0) {
            apple_gfx_render_new_frame(s);
        }
    }
}

static void apple_gfx_fb_update_display(void *opaque)
{
    AppleGFXState *s = opaque;

    assert(bql_locked());
    if (s->new_frame_ready) {
        dpy_gfx_update_full(s->con);
        s->new_frame_ready = false;
        graphic_hw_update_done(s->con);
    } else if (s->pending_frames > 0) {
        s->gfx_update_requested = true;
    } else {
        graphic_hw_update_done(s->con);
    }
}

static const GraphicHwOps apple_gfx_fb_ops = {
    .gfx_update = apple_gfx_fb_update_display,
    .gfx_update_async = true,
};

/* ------ Mouse cursor and display mode setting ------ */

static void set_mode(AppleGFXState *s, uint32_t width, uint32_t height)
{
    MTLTextureDescriptor *textureDescriptor;

    if (s->surface &&
        width == surface_width(s->surface) &&
        height == surface_height(s->surface)) {
        return;
    }

    [s->texture release];

    s->surface = qemu_create_displaysurface(width, height);

    @autoreleasepool {
        textureDescriptor =
            [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                             width:width
                                            height:height
                                         mipmapped:NO];
        textureDescriptor.usage = s->pgdisp.minimumTextureUsage;
        s->texture = [s->mtl newTextureWithDescriptor:textureDescriptor];
        s->using_managed_texture_storage =
            (s->texture.storageMode == MTLStorageModeManaged);
    }

    dpy_gfx_replace_surface(s->con, s->surface);
}

static void update_cursor(AppleGFXState *s)
{
    assert(bql_locked());
    dpy_mouse_set(s->con, s->pgdisp.cursorPosition.x,
                  s->pgdisp.cursorPosition.y, qatomic_read(&s->cursor_show));
}

static void update_cursor_bh(void *opaque)
{
    AppleGFXState *s = opaque;
    update_cursor(s);
}

typedef struct AppleGFXSetCursorGlyphJob {
    AppleGFXState *s;
    NSBitmapImageRep *glyph;
    PGDisplayCoord_t hotspot;
} AppleGFXSetCursorGlyphJob;

static void set_cursor_glyph(void *opaque)
{
    AppleGFXSetCursorGlyphJob *job = opaque;
    AppleGFXState *s = job->s;
    NSBitmapImageRep *glyph = job->glyph;
    uint32_t bpp = glyph.bitsPerPixel;
    size_t width = glyph.pixelsWide;
    size_t height = glyph.pixelsHigh;
    size_t padding_bytes_per_row = glyph.bytesPerRow - width * 4;
    const uint8_t* px_data = glyph.bitmapData;

    trace_apple_gfx_cursor_set(bpp, width, height);

    if (s->cursor) {
        cursor_unref(s->cursor);
        s->cursor = NULL;
    }

    if (bpp == 32) { /* Shouldn't be anything else, but just to be safe... */
        s->cursor = cursor_alloc(width, height);
        s->cursor->hot_x = job->hotspot.x;
        s->cursor->hot_y = job->hotspot.y;

        uint32_t *dest_px = s->cursor->data;

        for (size_t y = 0; y < height; ++y) {
            for (size_t x = 0; x < width; ++x) {
                /*
                 * NSBitmapImageRep's red & blue channels are swapped
                 * compared to QEMUCursor's.
                 */
                *dest_px =
                    (px_data[0] << 16u) |
                    (px_data[1] <<  8u) |
                    (px_data[2] <<  0u) |
                    (px_data[3] << 24u);
                ++dest_px;
                px_data += 4;
            }
            px_data += padding_bytes_per_row;
        }
        dpy_cursor_define(s->con, s->cursor);
        update_cursor(s);
    }
    [glyph release];

    g_free(job);
}

/* ------ DMA (device reading system memory) ------ */

typedef struct AppleGFXReadMemoryJob {
    QemuSemaphore sem;
    hwaddr physical_address;
    uint64_t length;
    void *dst;
    bool success;
} AppleGFXReadMemoryJob;

static void apple_gfx_do_read_memory(void *opaque)
{
    AppleGFXReadMemoryJob *job = opaque;
    MemTxResult r;

    r = dma_memory_read(&address_space_memory, job->physical_address,
                        job->dst, job->length, MEMTXATTRS_UNSPECIFIED);
    job->success = (r == MEMTX_OK);

    qemu_sem_post(&job->sem);
}

static bool apple_gfx_read_memory(AppleGFXState *s, hwaddr physical_address,
                                  uint64_t length, void *dst)
{
    AppleGFXReadMemoryJob job = {
        .physical_address = physical_address, .length = length, .dst = dst
    };

    trace_apple_gfx_read_memory(physical_address, length, dst);

    /* Performing DMA requires BQL, so do it in a BH. */
    qemu_sem_init(&job.sem, 0);
    aio_bh_schedule_oneshot(qemu_get_aio_context(),
                            apple_gfx_do_read_memory, &job);
    qemu_sem_wait(&job.sem);
    qemu_sem_destroy(&job.sem);
    return job.success;
}

/* ------ Memory-mapped device I/O operations ------ */

typedef struct AppleGFXIOJob {
    AppleGFXState *state;
    uint64_t offset;
    uint64_t value;
    bool completed;
} AppleGFXIOJob;

static void apple_gfx_do_read(void *opaque)
{
    AppleGFXIOJob *job = opaque;
    job->value = [job->state->pgdev mmioReadAtOffset:job->offset];
    qatomic_set(&job->completed, true);
    aio_wait_kick();
}

static uint64_t apple_gfx_read(void *opaque, hwaddr offset, unsigned size)
{
    AppleGFXIOJob job = {
        .state = opaque,
        .offset = offset,
        .completed = false,
    };
    dispatch_queue_t queue = get_background_queue();

    dispatch_async_f(queue, &job, apple_gfx_do_read);
    AIO_WAIT_WHILE(NULL, !qatomic_read(&job.completed));

    trace_apple_gfx_read(offset, job.value);
    return job.value;
}

static void apple_gfx_do_write(void *opaque)
{
    AppleGFXIOJob *job = opaque;
    [job->state->pgdev mmioWriteAtOffset:job->offset value:job->value];
    qatomic_set(&job->completed, true);
    aio_wait_kick();
}

static void apple_gfx_write(void *opaque, hwaddr offset, uint64_t val,
                            unsigned size)
{
    /*
     * The methods mmioReadAtOffset: and especially mmioWriteAtOffset: can
     * trigger synchronous operations on other dispatch queues, which in turn
     * may call back out on one or more of the callback blocks. For this reason,
     * and as we are holding the BQL, we invoke the I/O methods on a pool
     * thread and handle AIO tasks while we wait. Any work in the callbacks
     * requiring the BQL will in turn schedule BHs which this thread will
     * process while waiting.
     */
    AppleGFXIOJob job = {
        .state = opaque,
        .offset = offset,
        .value = val,
        .completed = false,
    };
    dispatch_queue_t queue = get_background_queue();

    dispatch_async_f(queue, &job, apple_gfx_do_write);
    AIO_WAIT_WHILE(NULL, !qatomic_read(&job.completed));

    trace_apple_gfx_write(offset, val);
}

static const MemoryRegionOps apple_gfx_ops = {
    .read = apple_gfx_read,
    .write = apple_gfx_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static size_t apple_gfx_get_default_mmio_range_size(void)
{
    size_t mmio_range_size;
    @autoreleasepool {
        PGDeviceDescriptor *desc = [PGDeviceDescriptor new];
        mmio_range_size = desc.mmioLength;
        [desc release];
    }
    return mmio_range_size;
}

/* ------ Initialisation and startup ------ */

void apple_gfx_common_init(Object *obj, AppleGFXState *s, const char* obj_name)
{
    size_t mmio_range_size = apple_gfx_get_default_mmio_range_size();

    trace_apple_gfx_common_init(obj_name, mmio_range_size);
    memory_region_init_io(&s->iomem_gfx, obj, &apple_gfx_ops, s, obj_name,
                          mmio_range_size);

    /* TODO: PVG framework supports serialising device state: integrate it! */
}

static void apple_gfx_register_task_mapping_handlers(AppleGFXState *s,
                                                     PGDeviceDescriptor *desc)
{
    desc.createTask = ^(uint64_t vmSize, void * _Nullable * _Nonnull baseAddress) {
        PGTask_t *task = apple_gfx_new_task(s, vmSize);
        *baseAddress = (void *)task->address;
        trace_apple_gfx_create_task(vmSize, *baseAddress);
        return task;
    };

    desc.destroyTask = ^(PGTask_t * _Nonnull task) {
        trace_apple_gfx_destroy_task(task, task->mapped_regions->len);

        apple_gfx_destroy_task(s, task);
    };

    desc.mapMemory = ^bool(PGTask_t * _Nonnull task, uint32_t range_count,
                           uint64_t virtual_offset, bool read_only,
                           PGPhysicalMemoryRange_t * _Nonnull ranges) {
        return apple_gfx_task_map_memory(s, task, virtual_offset,
                                         ranges, range_count, read_only);
    };

    desc.unmapMemory = ^bool(PGTask_t * _Nonnull task, uint64_t virtual_offset,
                             uint64_t length) {
        apple_gfx_task_unmap_memory(s, task, virtual_offset, length);
        return true;
    };

    desc.readMemory = ^bool(uint64_t physical_address, uint64_t length,
                            void * _Nonnull dst) {
        return apple_gfx_read_memory(s, physical_address, length, dst);
    };
}

static void new_frame_handler_bh(void *opaque)
{
    AppleGFXState *s = opaque;

    /* Drop frames if guest gets too far ahead. */
    if (s->pending_frames >= 2) {
        return;
    }
    ++s->pending_frames;
    if (s->pending_frames > 1) {
        return;
    }

    @autoreleasepool {
        apple_gfx_render_new_frame(s);
    }
}

static PGDisplayDescriptor *apple_gfx_prepare_display_descriptor(AppleGFXState *s)
{
    PGDisplayDescriptor *disp_desc = [PGDisplayDescriptor new];

    disp_desc.name = @"QEMU display";
    disp_desc.sizeInMillimeters = NSMakeSize(400., 300.); /* A 20" display */
    disp_desc.queue = dispatch_get_main_queue();
    disp_desc.newFrameEventHandler = ^(void) {
        trace_apple_gfx_new_frame();
        aio_bh_schedule_oneshot(qemu_get_aio_context(), new_frame_handler_bh, s);
    };
    disp_desc.modeChangeHandler = ^(PGDisplayCoord_t sizeInPixels,
                                    OSType pixelFormat) {
        trace_apple_gfx_mode_change(sizeInPixels.x, sizeInPixels.y);

        BQL_LOCK_GUARD();
        set_mode(s, sizeInPixels.x, sizeInPixels.y);
    };
    disp_desc.cursorGlyphHandler = ^(NSBitmapImageRep *glyph,
                                     PGDisplayCoord_t hotspot) {
        AppleGFXSetCursorGlyphJob *job = g_malloc0(sizeof(*job));
        job->s = s;
        job->glyph = glyph;
        job->hotspot = hotspot;
        [glyph retain];
        aio_bh_schedule_oneshot(qemu_get_aio_context(),
                                set_cursor_glyph, job);
    };
    disp_desc.cursorShowHandler = ^(BOOL show) {
        trace_apple_gfx_cursor_show(show);
        qatomic_set(&s->cursor_show, show);
        aio_bh_schedule_oneshot(qemu_get_aio_context(),
                                update_cursor_bh, s);
    };
    disp_desc.cursorMoveHandler = ^(void) {
        trace_apple_gfx_cursor_move();
        aio_bh_schedule_oneshot(qemu_get_aio_context(),
                                update_cursor_bh, s);
    };

    return disp_desc;
}

static NSArray<PGDisplayMode *> *apple_gfx_create_display_mode_array(
    const AppleGFXDisplayMode display_modes[], uint32_t display_mode_count)
{
    PGDisplayMode *mode_obj;
    NSMutableArray<PGDisplayMode *> *mode_array =
        [[NSMutableArray alloc] initWithCapacity:display_mode_count];

    for (unsigned i = 0; i < display_mode_count; i++) {
        const AppleGFXDisplayMode *mode = &display_modes[i];
        trace_apple_gfx_display_mode(i, mode->width_px, mode->height_px);
        PGDisplayCoord_t mode_size = { mode->width_px, mode->height_px };

        mode_obj =
            [[PGDisplayMode alloc] initWithSizeInPixels:mode_size
                                        refreshRateInHz:mode->refresh_rate_hz];
        [mode_array addObject:mode_obj];
        [mode_obj release];
    }

    return mode_array;
}

static id<MTLDevice> copy_suitable_metal_device(void)
{
    id<MTLDevice> dev = nil;
    NSArray<id<MTLDevice>> *devs = MTLCopyAllDevices();

    /* Prefer a unified memory GPU. Failing that, pick a non-removable GPU. */
    for (size_t i = 0; i < devs.count; ++i) {
        if (devs[i].hasUnifiedMemory) {
            dev = devs[i];
            break;
        }
        if (!devs[i].removable) {
            dev = devs[i];
        }
    }

    if (dev != nil) {
        [dev retain];
    } else {
        dev = MTLCreateSystemDefaultDevice();
    }
    [devs release];

    return dev;
}

bool apple_gfx_common_realize(AppleGFXState *s, DeviceState *dev,
                              PGDeviceDescriptor *desc, Error **errp)
{
    PGDisplayDescriptor *disp_desc;
    const AppleGFXDisplayMode *display_modes = apple_gfx_default_modes;
    uint32_t num_display_modes = ARRAY_SIZE(apple_gfx_default_modes);
    NSArray<PGDisplayMode *> *mode_array;

    if (apple_gfx_mig_blocker == NULL) {
        error_setg(&apple_gfx_mig_blocker,
                  "Migration state blocked by apple-gfx display device");
        if (migrate_add_blocker(&apple_gfx_mig_blocker, errp) < 0) {
            return false;
        }
    }

    qemu_mutex_init(&s->task_mutex);
    QTAILQ_INIT(&s->tasks);
    s->mtl = copy_suitable_metal_device();
    s->mtl_queue = [s->mtl newCommandQueue];

    desc.device = s->mtl;

    apple_gfx_register_task_mapping_handlers(s, desc);

    s->cursor_show = true;

    s->pgdev = PGNewDeviceWithDescriptor(desc);

    disp_desc = apple_gfx_prepare_display_descriptor(s);
    /*
     * Although the framework does, this integration currently does not support
     * multiple virtual displays connected to a single PV graphics device.
     * It is however possible to create
     * more than one instance of the device, each with one display. The macOS
     * guest will ignore these displays if they share the same serial number,
     * so ensure each instance gets a unique one.
     */
    s->pgdisp = [s->pgdev newDisplayWithDescriptor:disp_desc
                                              port:0
                                         serialNum:next_pgdisplay_serial_num++];
    [disp_desc release];

    if (s->display_modes != NULL && s->num_display_modes > 0) {
        trace_apple_gfx_common_realize_modes_property(s->num_display_modes);
        display_modes = s->display_modes;
        num_display_modes = s->num_display_modes;
    }
    s->pgdisp.modeList = mode_array =
        apple_gfx_create_display_mode_array(display_modes, num_display_modes);
    [mode_array release];

    s->con = graphic_console_init(dev, 0, &apple_gfx_fb_ops, s);
    return true;
}

/* ------ Display mode list device property ------ */

static void apple_gfx_get_display_mode(Object *obj, Visitor *v,
                                       const char *name, void *opaque,
                                       Error **errp)
{
    Property *prop = opaque;
    AppleGFXDisplayMode *mode = object_field_prop_ptr(obj, prop);
    /* 3 uint16s (max 5 digits) + 2 separator characters + nul. */
    char buffer[5 * 3 + 2 + 1];
    char *pos = buffer;

    int rc = snprintf(buffer, sizeof(buffer),
                      "%"PRIu16"x%"PRIu16"@%"PRIu16,
                      mode->width_px, mode->height_px,
                      mode->refresh_rate_hz);
    assert(rc < sizeof(buffer));

    visit_type_str(v, name, &pos, errp);
}

static void apple_gfx_set_display_mode(Object *obj, Visitor *v,
                                       const char *name, void *opaque,
                                       Error **errp)
{
    Property *prop = opaque;
    AppleGFXDisplayMode *mode = object_field_prop_ptr(obj, prop);
    const char *endptr;
    g_autofree char *str = NULL;
    int ret;
    int val;

    if (!visit_type_str(v, name, &str, errp)) {
        return;
    }

    endptr = str;

    ret = qemu_strtoi(endptr, &endptr, 10, &val);
    if (ret || val > UINT16_MAX || val <= 0) {
        error_setg(errp, "width in '%s' must be a decimal integer number"
                         " of pixels in the range 1..65535", name);
        return;
    }
    mode->width_px = val;
    if (*endptr != 'x') {
        goto separator_error;
    }

    ret = qemu_strtoi(endptr + 1, &endptr, 10, &val);
    if (ret || val > UINT16_MAX || val <= 0) {
        error_setg(errp, "height in '%s' must be a decimal integer number"
                         " of pixels in the range 1..65535", name);
        return;
    }
    mode->height_px = val;
    if (*endptr != '@') {
        goto separator_error;
    }

    ret = qemu_strtoi(endptr + 1, &endptr, 10, &val);
    if (ret || val > UINT16_MAX || val <= 0) {
        error_setg(errp, "refresh rate in '%s'"
                         " must be a positive decimal integer (Hertz)", name);
        return;
    }
    mode->refresh_rate_hz = val;
    return;

separator_error:
    error_setg(errp,
               "Each display mode takes the format '<width>x<height>@<rate>'");
}

const PropertyInfo qdev_prop_apple_gfx_display_mode = {
    .type  = "display_mode",
    .description =
        "Display mode in pixels and Hertz, as <width>x<height>@<refresh-rate> "
        "Example: 3840x2160@60",
    .get   = apple_gfx_get_display_mode,
    .set   = apple_gfx_set_display_mode,
};
