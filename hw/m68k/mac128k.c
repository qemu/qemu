/*
 * Macintosh 128K system emulation.
 *
 * This code is licensed under the GPL
 */

#include "hw/hw.h"
#include "mac128k.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "elf.h"
#include "exec/address-spaces.h"
#include "exec/ram_addr.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"
#include "sysemu/qtest.h"
#include "ui/console.h"
#include "ui/pixel_ops.h"

#define ROM_LOAD_ADDR 0x400000
#define MAX_ROM_SIZE  0x20000
#define IWM_BASE_ADDR 0xDFE1FF // dBase
#define VIA_BASE_ADDR 0xEFE1FE // vBase
#define SCREEN_WIDTH  512
#define SCREEN_HEIGHT 342

typedef struct {
    QemuConsole *con;
    int invalidate;
} mac_display;

/* Display controller */

typedef void (*drawfn)(uint8_t *, const uint8_t *, int);

#define DEPTH 8
#include "hw/display/mac_display_template.h"
#define DEPTH 15
#include "hw/display/mac_display_template.h"
#define DEPTH 16
#include "hw/display/mac_display_template.h"
#define DEPTH 32
#include "hw/display/mac_display_template.h"

static drawfn draw_line_table[33] = {
    [0 ... 32]	= NULL,
    [8]		= draw_line_8,
    [15]	= draw_line_15,
    [16]	= draw_line_16,
    [32]	= draw_line_32,
};

static void mac_update_display(void *opaque)
{
    mac_display *s = (mac_display*)opaque;
    DisplaySurface *surface = qemu_console_surface(s->con);
    uint8_t *dest;
    uint8_t *src;
    int line;

    if (!surface_bits_per_pixel(surface)) {
        return;
    }

    drawfn draw_line = draw_line_table[surface_bits_per_pixel(surface)];
    dest = surface_data(surface);
    src = qemu_get_ram_ptr(0x1a700);
    for (line = 0 ; line < SCREEN_HEIGHT ; ++line) {
        draw_line(dest, src, SCREEN_WIDTH);
        dest += surface_stride(surface);
        src += SCREEN_WIDTH / 8;
    }
    dpy_gfx_update(s->con, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    s->invalidate = 0;
}

static void mac_invalidate_display(void *opaque) {
    mac_display *s = opaque;
    s->invalidate = 1;
}


static const GraphicHwOps mac_display_ops = {
    .invalidate  = mac_invalidate_display,
    .gfx_update  = mac_update_display,
};

/* Board init.  */

static void mac128k_init(MachineState *machine)
{
    ram_addr_t ram_size = 0x20000;//machine->ram_size;
    const char *cpu_model = machine->cpu_model;
    const char *kernel_filename = machine->kernel_filename;
    M68kCPU *cpu;
    int kernel_size;
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    MemoryRegion *rom = g_new(MemoryRegion, 1);
    mac_display *display = (mac_display *)g_malloc0(sizeof(mac_display));

    if (!cpu_model) {
        cpu_model = "m68000";
    }

    cpu = cpu_m68k_init(cpu_model);
    if (!cpu) {
        hw_error("Unable to find m68k CPU definition\n");
    }

    /* RAM at address zero */
    memory_region_allocate_system_memory(ram, NULL, "mac128k.ram", ram_size);
    memory_region_add_subregion(address_space_mem, 0, ram);

    /* ROM */
    memory_region_init_ram(rom, NULL, "mac128k.rom", MAX_ROM_SIZE, &error_abort);
    memory_region_add_subregion(address_space_mem, ROM_LOAD_ADDR, rom);
    memory_region_set_readonly(rom, true);

    iwm_init(address_space_mem, IWM_BASE_ADDR, cpu);
    sy6522_init(rom, ram, VIA_BASE_ADDR, cpu);

    /* Display */
    display->con = graphic_console_init(NULL, 0, &mac_display_ops, display);
    qemu_console_resize(display->con, SCREEN_WIDTH, SCREEN_HEIGHT);

    /* Load kernel.  */
    if (kernel_filename) {
        kernel_size = load_image_targphys(kernel_filename,
                                          ROM_LOAD_ADDR,
                                          MAX_ROM_SIZE);
        if (kernel_size < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n",
                    kernel_filename);
            exit(1);
        }
    }
}

static QEMUMachine mac128k_machine = {
    .name = "mac128k",
    .desc = "Macintosh 128K",
    .init = mac128k_init,
};

static void mac128k_machine_init(void)
{
    qemu_register_machine(&mac128k_machine);
}

machine_init(mac128k_machine_init);
