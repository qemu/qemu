/*
 * QEMU PC System Firmware
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 * Copyright (c) 2011-2012 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "sysemu/blockdev.h"
#include "qemu/error-report.h"
#include "hw/sysbus.h"
#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "sysemu/sysemu.h"
#include "hw/block/flash.h"
#include "sysemu/kvm.h"

#define BIOS_FILENAME "bios.bin"

typedef struct PcSysFwDevice {
    SysBusDevice busdev;
    uint8_t rom_only;
} PcSysFwDevice;

static void pc_isa_bios_init(MemoryRegion *rom_memory,
                             MemoryRegion *flash_mem,
                             int ram_size)
{
    int isa_bios_size;
    MemoryRegion *isa_bios;
    uint64_t flash_size;
    void *flash_ptr, *isa_bios_ptr;

    flash_size = memory_region_size(flash_mem);

    /* map the last 128KB of the BIOS in ISA space */
    isa_bios_size = flash_size;
    if (isa_bios_size > (128 * 1024)) {
        isa_bios_size = 128 * 1024;
    }
    isa_bios = g_malloc(sizeof(*isa_bios));
    memory_region_init_ram(isa_bios, "isa-bios", isa_bios_size);
    vmstate_register_ram_global(isa_bios);
    memory_region_add_subregion_overlap(rom_memory,
                                        0x100000 - isa_bios_size,
                                        isa_bios,
                                        1);

    /* copy ISA rom image from top of flash memory */
    flash_ptr = memory_region_get_ram_ptr(flash_mem);
    isa_bios_ptr = memory_region_get_ram_ptr(isa_bios);
    memcpy(isa_bios_ptr,
           ((uint8_t*)flash_ptr) + (flash_size - isa_bios_size),
           isa_bios_size);

    memory_region_set_readonly(isa_bios, true);
}

static void pc_fw_add_pflash_drv(void)
{
    QemuOpts *opts;
    QEMUMachine *machine;
    char *filename;

    if (bios_name == NULL) {
        bios_name = BIOS_FILENAME;
    }
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
    if (!filename) {
        error_report("Can't open BIOS image %s", bios_name);
        exit(1);
    }

    opts = drive_add(IF_PFLASH, -1, filename, "readonly=on");

    g_free(filename);

    if (opts == NULL) {
      return;
    }

    machine = find_default_machine();
    if (machine == NULL) {
      return;
    }

    if (!drive_init(opts, machine->block_default_type)) {
        qemu_opts_del(opts);
    }
}

static void pc_system_flash_init(MemoryRegion *rom_memory,
                                 DriveInfo *pflash_drv)
{
    BlockDriverState *bdrv;
    int64_t size;
    hwaddr phys_addr;
    int sector_bits, sector_size;
    pflash_t *system_flash;
    MemoryRegion *flash_mem;

    bdrv = pflash_drv->bdrv;
    size = bdrv_getlength(pflash_drv->bdrv);
    sector_bits = 12;
    sector_size = 1 << sector_bits;

    if ((size % sector_size) != 0) {
        fprintf(stderr,
                "qemu: PC system firmware (pflash) must be a multiple of 0x%x\n",
                sector_size);
        exit(1);
    }

    phys_addr = 0x100000000ULL - size;
    system_flash = pflash_cfi01_register(phys_addr, NULL, "system.flash", size,
                                         bdrv, sector_size, size >> sector_bits,
                                         1, 0x0000, 0x0000, 0x0000, 0x0000, 0);
    flash_mem = pflash_cfi01_get_memory(system_flash);

    pc_isa_bios_init(rom_memory, flash_mem, size);
}

static void old_pc_system_rom_init(MemoryRegion *rom_memory)
{
    char *filename;
    MemoryRegion *bios, *isa_bios;
    int bios_size, isa_bios_size;
    int ret;

    /* BIOS load */
    if (bios_name == NULL) {
        bios_name = BIOS_FILENAME;
    }
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
    if (filename) {
        bios_size = get_image_size(filename);
    } else {
        bios_size = -1;
    }
    if (bios_size <= 0 ||
        (bios_size % 65536) != 0) {
        goto bios_error;
    }
    bios = g_malloc(sizeof(*bios));
    memory_region_init_ram(bios, "pc.bios", bios_size);
    vmstate_register_ram_global(bios);
    memory_region_set_readonly(bios, true);
    ret = rom_add_file_fixed(bios_name, (uint32_t)(-bios_size), -1);
    if (ret != 0) {
    bios_error:
        fprintf(stderr, "qemu: could not load PC BIOS '%s'\n", bios_name);
        exit(1);
    }
    if (filename) {
        g_free(filename);
    }

    /* map the last 128KB of the BIOS in ISA space */
    isa_bios_size = bios_size;
    if (isa_bios_size > (128 * 1024)) {
        isa_bios_size = 128 * 1024;
    }
    isa_bios = g_malloc(sizeof(*isa_bios));
    memory_region_init_alias(isa_bios, "isa-bios", bios,
                             bios_size - isa_bios_size, isa_bios_size);
    memory_region_add_subregion_overlap(rom_memory,
                                        0x100000 - isa_bios_size,
                                        isa_bios,
                                        1);
    memory_region_set_readonly(isa_bios, true);

    /* map all the bios at the top of memory */
    memory_region_add_subregion(rom_memory,
                                (uint32_t)(-bios_size),
                                bios);
}

/*
 * Bug-compatible flash vs. ROM selection enabled?
 * A few older machines enable this.
 */
bool pc_sysfw_flash_vs_rom_bug_compatible;

void pc_system_firmware_init(MemoryRegion *rom_memory)
{
    DriveInfo *pflash_drv;
    PcSysFwDevice *sysfw_dev;

    /*
     * TODO This device exists only so that users can switch between
     * use of flash and ROM for the BIOS.  The ability to switch was
     * created because flash doesn't work with KVM.  Once it does, we
     * should drop this device.
     */
    sysfw_dev = (PcSysFwDevice*) qdev_create(NULL, "pc-sysfw");

    qdev_init_nofail(DEVICE(sysfw_dev));

    if (sysfw_dev->rom_only) {
        old_pc_system_rom_init(rom_memory);
        return;
    }

    pflash_drv = drive_get(IF_PFLASH, 0, 0);

    /* Currently KVM cannot execute from device memory.
       Use old rom based firmware initialization for KVM. */
    /*
     * This is a Bad Idea, because it makes enabling/disabling KVM
     * guest-visible.  Let's fix it for real in QEMU 1.6.
     */
    if (kvm_enabled()) {
        if (pflash_drv != NULL) {
            fprintf(stderr, "qemu: pflash cannot be used with kvm enabled\n");
            exit(1);
        } else {
            sysfw_dev->rom_only = 1;
            old_pc_system_rom_init(rom_memory);
            return;
        }
    }

    /* If a pflash drive is not found, then create one using
       the bios filename. */
    if (pflash_drv == NULL) {
        pc_fw_add_pflash_drv();
        pflash_drv = drive_get(IF_PFLASH, 0, 0);
    }

    if (pflash_drv != NULL) {
        pc_system_flash_init(rom_memory, pflash_drv);
    } else {
        fprintf(stderr, "qemu: PC system firmware (pflash) not available\n");
        exit(1);
    }
}

static Property pcsysfw_properties[] = {
    DEFINE_PROP_UINT8("rom_only", PcSysFwDevice, rom_only, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static int pcsysfw_init(DeviceState *dev)
{
    return 0;
}

static void pcsysfw_class_init (ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS (klass);

    dc->desc = "PC System Firmware";
    dc->init = pcsysfw_init;
    dc->props = pcsysfw_properties;
}

static const TypeInfo pcsysfw_info = {
    .name          = "pc-sysfw",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof (PcSysFwDevice),
    .class_init    = pcsysfw_class_init,
};

static void pcsysfw_register (void)
{
    type_register_static (&pcsysfw_info);
}

type_init (pcsysfw_register);

