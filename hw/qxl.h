#include "qemu-common.h"

#include "console.h"
#include "hw.h"
#include "pci.h"
#include "vga_int.h"

#include "ui/qemu-spice.h"
#include "ui/spice-display.h"

enum qxl_mode {
    QXL_MODE_UNDEFINED,
    QXL_MODE_VGA,
    QXL_MODE_COMPAT, /* spice 0.4.x */
    QXL_MODE_NATIVE,
};

typedef struct PCIQXLDevice {
    PCIDevice          pci;
    SimpleSpiceDisplay ssd;
    int                id;
    uint32_t           debug;
    uint32_t           guestdebug;
    uint32_t           cmdlog;
    enum qxl_mode      mode;
    uint32_t           cmdflags;
    int                generation;
    uint32_t           revision;

    int32_t            num_memslots;
    int32_t            num_surfaces;

    struct guest_slots {
        QXLMemSlot     slot;
        void           *ptr;
        uint64_t       size;
        uint64_t       delta;
        uint32_t       active;
    } guest_slots[NUM_MEMSLOTS];

    struct guest_primary {
        QXLSurfaceCreate surface;
        uint32_t       commands;
        uint32_t       resized;
        int32_t        stride;
        uint32_t       bits_pp;
        uint32_t       bytes_pp;
        uint8_t        *data, *flipped;
    } guest_primary;

    struct surfaces {
        QXLPHYSICAL    cmds[NUM_SURFACES];
        uint32_t       count;
        uint32_t       max;
    } guest_surfaces;
    QXLPHYSICAL        guest_cursor;

    QemuMutex          track_lock;

    /* thread signaling */
    pthread_t          main;
    int                pipe[2];

    /* ram pci bar */
    QXLRam             *ram;
    VGACommonState     vga;
    uint32_t           num_free_res;
    QXLReleaseInfo     *last_release;
    uint32_t           last_release_offset;
    uint32_t           oom_running;

    /* rom pci bar */
    QXLRom             shadow_rom;
    QXLRom             *rom;
    QXLModes           *modes;
    uint32_t           rom_size;
    uint64_t           rom_offset;

    /* vram pci bar */
    uint32_t           vram_size;
    uint64_t           vram_offset;

    /* io bar */
    uint32_t           io_base;
} PCIQXLDevice;

#define PANIC_ON(x) if ((x)) {                         \
    printf("%s: PANIC %s failed\n", __FUNCTION__, #x); \
    exit(-1);                                          \
}

#define dprint(_qxl, _level, _fmt, ...)                                 \
    do {                                                                \
        if (_qxl->debug >= _level) {                                    \
            fprintf(stderr, "qxl-%d: ", _qxl->id);                      \
            fprintf(stderr, _fmt, ## __VA_ARGS__);                      \
        }                                                               \
    } while (0)

/* qxl.c */
void *qxl_phys2virt(PCIQXLDevice *qxl, QXLPHYSICAL phys, int group_id);

void qxl_spice_update_area(PCIQXLDevice *qxl, uint32_t surface_id,
                           struct QXLRect *area, struct QXLRect *dirty_rects,
                           uint32_t num_dirty_rects,
                           uint32_t clear_dirty_region);
void qxl_spice_destroy_surface_wait(PCIQXLDevice *qxl, uint32_t id);
void qxl_spice_loadvm_commands(PCIQXLDevice *qxl, struct QXLCommandExt *ext,
                               uint32_t count);
void qxl_spice_oom(PCIQXLDevice *qxl);
void qxl_spice_reset_memslots(PCIQXLDevice *qxl);
void qxl_spice_destroy_surfaces(PCIQXLDevice *qxl);
void qxl_spice_reset_image_cache(PCIQXLDevice *qxl);
void qxl_spice_reset_cursor(PCIQXLDevice *qxl);

/* qxl-logger.c */
void qxl_log_cmd_cursor(PCIQXLDevice *qxl, QXLCursorCmd *cmd, int group_id);
void qxl_log_command(PCIQXLDevice *qxl, const char *ring, QXLCommandExt *ext);

/* qxl-render.c */
void qxl_render_resize(PCIQXLDevice *qxl);
void qxl_render_update(PCIQXLDevice *qxl);
void qxl_render_cursor(PCIQXLDevice *qxl, QXLCommandExt *ext);
