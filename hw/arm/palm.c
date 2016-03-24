/*
 * PalmOne's (TM) PDAs.
 *
 * Copyright (C) 2006-2007 Andrzej Zaborowski  <balrog@zabor.org>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "audio/audio.h"
#include "sysemu/sysemu.h"
#include "sysemu/qtest.h"
#include "ui/console.h"
#include "hw/arm/omap.h"
#include "hw/boards.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"
#include "hw/loader.h"
#include "exec/address-spaces.h"

static uint32_t static_readb(void *opaque, hwaddr offset)
{
    uint32_t *val = (uint32_t *) opaque;
    return *val >> ((offset & 3) << 3);
}

static uint32_t static_readh(void *opaque, hwaddr offset)
{
    uint32_t *val = (uint32_t *) opaque;
    return *val >> ((offset & 1) << 3);
}

static uint32_t static_readw(void *opaque, hwaddr offset)
{
    uint32_t *val = (uint32_t *) opaque;
    return *val >> ((offset & 0) << 3);
}

static void static_write(void *opaque, hwaddr offset,
                uint32_t value)
{
#ifdef SPY
    printf("%s: value %08lx written at " PA_FMT "\n",
                    __FUNCTION__, value, offset);
#endif
}

static const MemoryRegionOps static_ops = {
    .old_mmio = {
        .read = { static_readb, static_readh, static_readw, },
        .write = { static_write, static_write, static_write, },
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/* Palm Tunsgten|E support */

/* Shared GPIOs */
#define PALMTE_USBDETECT_GPIO	0
#define PALMTE_USB_OR_DC_GPIO	1
#define PALMTE_TSC_GPIO		4
#define PALMTE_PINTDAV_GPIO	6
#define PALMTE_MMC_WP_GPIO	8
#define PALMTE_MMC_POWER_GPIO	9
#define PALMTE_HDQ_GPIO		11
#define PALMTE_HEADPHONES_GPIO	14
#define PALMTE_SPEAKER_GPIO	15
/* MPU private GPIOs */
#define PALMTE_DC_GPIO		2
#define PALMTE_MMC_SWITCH_GPIO	4
#define PALMTE_MMC1_GPIO	6
#define PALMTE_MMC2_GPIO	7
#define PALMTE_MMC3_GPIO	11

static MouseTransformInfo palmte_pointercal = {
    .x = 320,
    .y = 320,
    .a = { -5909, 8, 22465308, 104, 7644, -1219972, 65536 },
};

static void palmte_microwire_setup(struct omap_mpu_state_s *cpu)
{
    uWireSlave *tsc;

    tsc = tsc2102_init(qdev_get_gpio_in(cpu->gpio, PALMTE_PINTDAV_GPIO));

    omap_uwire_attach(cpu->microwire, tsc, 0);
    omap_mcbsp_i2s_attach(cpu->mcbsp1, tsc210x_codec(tsc));

    tsc210x_set_transform(tsc, &palmte_pointercal);
}

static struct {
    int row;
    int column;
} palmte_keymap[0x80] = {
    [0 ... 0x7f] = { -1, -1 },
    [0x3b] = { 0, 0 },	/* F1	-> Calendar */
    [0x3c] = { 1, 0 },	/* F2	-> Contacts */
    [0x3d] = { 2, 0 },	/* F3	-> Tasks List */
    [0x3e] = { 3, 0 },	/* F4	-> Note Pad */
    [0x01] = { 4, 0 },	/* Esc	-> Power */
    [0x4b] = { 0, 1 },	/* 	   Left */
    [0x50] = { 1, 1 },	/* 	   Down */
    [0x48] = { 2, 1 },	/*	   Up */
    [0x4d] = { 3, 1 },	/*	   Right */
    [0x4c] = { 4, 1 },	/* 	   Centre */
    [0x39] = { 4, 1 },	/* Spc	-> Centre */
};

static void palmte_button_event(void *opaque, int keycode)
{
    struct omap_mpu_state_s *cpu = (struct omap_mpu_state_s *) opaque;

    if (palmte_keymap[keycode & 0x7f].row != -1)
        omap_mpuio_key(cpu->mpuio,
                        palmte_keymap[keycode & 0x7f].row,
                        palmte_keymap[keycode & 0x7f].column,
                        !(keycode & 0x80));
}

static void palmte_onoff_gpios(void *opaque, int line, int level)
{
    switch (line) {
    case 0:
        printf("%s: current to MMC/SD card %sabled.\n",
                        __FUNCTION__, level ? "dis" : "en");
        break;
    case 1:
        printf("%s: internal speaker amplifier %s.\n",
                        __FUNCTION__, level ? "down" : "on");
        break;

    /* These LCD & Audio output signals have not been identified yet.  */
    case 2:
    case 3:
    case 4:
        printf("%s: LCD GPIO%i %s.\n",
                        __FUNCTION__, line - 1, level ? "high" : "low");
        break;
    case 5:
    case 6:
        printf("%s: Audio GPIO%i %s.\n",
                        __FUNCTION__, line - 4, level ? "high" : "low");
        break;
    }
}

static void palmte_gpio_setup(struct omap_mpu_state_s *cpu)
{
    qemu_irq *misc_gpio;

    omap_mmc_handlers(cpu->mmc,
                    qdev_get_gpio_in(cpu->gpio, PALMTE_MMC_WP_GPIO),
                    qemu_irq_invert(omap_mpuio_in_get(cpu->mpuio)
                            [PALMTE_MMC_SWITCH_GPIO]));

    misc_gpio = qemu_allocate_irqs(palmte_onoff_gpios, cpu, 7);
    qdev_connect_gpio_out(cpu->gpio, PALMTE_MMC_POWER_GPIO,	misc_gpio[0]);
    qdev_connect_gpio_out(cpu->gpio, PALMTE_SPEAKER_GPIO,	misc_gpio[1]);
    qdev_connect_gpio_out(cpu->gpio, 11,			misc_gpio[2]);
    qdev_connect_gpio_out(cpu->gpio, 12,			misc_gpio[3]);
    qdev_connect_gpio_out(cpu->gpio, 13,			misc_gpio[4]);
    omap_mpuio_out_set(cpu->mpuio, 1,				misc_gpio[5]);
    omap_mpuio_out_set(cpu->mpuio, 3,				misc_gpio[6]);

    /* Reset some inputs to initial state.  */
    qemu_irq_lower(qdev_get_gpio_in(cpu->gpio, PALMTE_USBDETECT_GPIO));
    qemu_irq_lower(qdev_get_gpio_in(cpu->gpio, PALMTE_USB_OR_DC_GPIO));
    qemu_irq_lower(qdev_get_gpio_in(cpu->gpio, 4));
    qemu_irq_lower(qdev_get_gpio_in(cpu->gpio, PALMTE_HEADPHONES_GPIO));
    qemu_irq_lower(omap_mpuio_in_get(cpu->mpuio)[PALMTE_DC_GPIO]);
    qemu_irq_raise(omap_mpuio_in_get(cpu->mpuio)[6]);
    qemu_irq_raise(omap_mpuio_in_get(cpu->mpuio)[7]);
    qemu_irq_raise(omap_mpuio_in_get(cpu->mpuio)[11]);
}

static struct arm_boot_info palmte_binfo = {
    .loader_start = OMAP_EMIFF_BASE,
    .ram_size = 0x02000000,
    .board_id = 0x331,
};

static void palmte_init(MachineState *machine)
{
    const char *cpu_model = machine->cpu_model;
    const char *kernel_filename = machine->kernel_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;
    const char *initrd_filename = machine->initrd_filename;
    MemoryRegion *address_space_mem = get_system_memory();
    struct omap_mpu_state_s *mpu;
    int flash_size = 0x00800000;
    int sdram_size = palmte_binfo.ram_size;
    static uint32_t cs0val = 0xffffffff;
    static uint32_t cs1val = 0x0000e1a0;
    static uint32_t cs2val = 0x0000e1a0;
    static uint32_t cs3val = 0xe1a0e1a0;
    int rom_size, rom_loaded = 0;
    MemoryRegion *flash = g_new(MemoryRegion, 1);
    MemoryRegion *cs = g_new(MemoryRegion, 4);

    mpu = omap310_mpu_init(address_space_mem, sdram_size, cpu_model);

    /* External Flash (EMIFS) */
    memory_region_init_ram(flash, NULL, "palmte.flash", flash_size,
                           &error_fatal);
    vmstate_register_ram_global(flash);
    memory_region_set_readonly(flash, true);
    memory_region_add_subregion(address_space_mem, OMAP_CS0_BASE, flash);

    memory_region_init_io(&cs[0], NULL, &static_ops, &cs0val, "palmte-cs0",
                          OMAP_CS0_SIZE - flash_size);
    memory_region_add_subregion(address_space_mem, OMAP_CS0_BASE + flash_size,
                                &cs[0]);
    memory_region_init_io(&cs[1], NULL, &static_ops, &cs1val, "palmte-cs1",
                          OMAP_CS1_SIZE);
    memory_region_add_subregion(address_space_mem, OMAP_CS1_BASE, &cs[1]);
    memory_region_init_io(&cs[2], NULL, &static_ops, &cs2val, "palmte-cs2",
                          OMAP_CS2_SIZE);
    memory_region_add_subregion(address_space_mem, OMAP_CS2_BASE, &cs[2]);
    memory_region_init_io(&cs[3], NULL, &static_ops, &cs3val, "palmte-cs3",
                          OMAP_CS3_SIZE);
    memory_region_add_subregion(address_space_mem, OMAP_CS3_BASE, &cs[3]);

    palmte_microwire_setup(mpu);

    qemu_add_kbd_event_handler(palmte_button_event, mpu);

    palmte_gpio_setup(mpu);

    /* Setup initial (reset) machine state */
    if (nb_option_roms) {
        rom_size = get_image_size(option_rom[0].name);
        if (rom_size > flash_size) {
            fprintf(stderr, "%s: ROM image too big (%x > %x)\n",
                            __FUNCTION__, rom_size, flash_size);
            rom_size = 0;
        }
        if (rom_size > 0) {
            rom_size = load_image_targphys(option_rom[0].name, OMAP_CS0_BASE,
                                           flash_size);
            rom_loaded = 1;
        }
        if (rom_size < 0) {
            fprintf(stderr, "%s: error loading '%s'\n",
                            __FUNCTION__, option_rom[0].name);
        }
    }

    if (!rom_loaded && !kernel_filename && !qtest_enabled()) {
        fprintf(stderr, "Kernel or ROM image must be specified\n");
        exit(1);
    }

    /* Load the kernel.  */
    palmte_binfo.kernel_filename = kernel_filename;
    palmte_binfo.kernel_cmdline = kernel_cmdline;
    palmte_binfo.initrd_filename = initrd_filename;
    arm_load_kernel(mpu->cpu, &palmte_binfo);
}

static void palmte_machine_init(MachineClass *mc)
{
    mc->desc = "Palm Tungsten|E aka. Cheetah PDA (OMAP310)";
    mc->init = palmte_init;
}

DEFINE_MACHINE("cheetah", palmte_machine_init)
