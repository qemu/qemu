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

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "sysemu/block-backend.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "qemu/units.h"
#include "hw/sysbus.h"
#include "hw/i386/x86.h"
#include "hw/i386/pc.h"
#include "hw/loader.h"
#include "hw/qdev-properties.h"
#include "sysemu/sysemu.h"
#include "hw/block/flash.h"
#include "sysemu/kvm.h"

/*
 * We don't have a theoretically justifiable exact lower bound on the base
 * address of any flash mapping. In practice, the IO-APIC MMIO range is
 * [0xFEE00000..0xFEE01000] -- see IO_APIC_DEFAULT_ADDRESS --, leaving free
 * only 18MB-4KB below 4G. For now, restrict the cumulative mapping to 8MB in
 * size.
 */
#define FLASH_SIZE_LIMIT (8 * MiB)

#define FLASH_SECTOR_SIZE 4096

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
    isa_bios_size = MIN(flash_size, 128 * KiB);
    isa_bios = g_malloc(sizeof(*isa_bios));
    memory_region_init_ram(isa_bios, NULL, "isa-bios", isa_bios_size,
                           &error_fatal);
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

static PFlashCFI01 *pc_pflash_create(PCMachineState *pcms,
                                     const char *name,
                                     const char *alias_prop_name)
{
    DeviceState *dev = qdev_new(TYPE_PFLASH_CFI01);

    qdev_prop_set_uint64(dev, "sector-length", FLASH_SECTOR_SIZE);
    qdev_prop_set_uint8(dev, "width", 1);
    qdev_prop_set_string(dev, "name", name);
    object_property_add_child(OBJECT(pcms), name, OBJECT(dev));
    object_property_add_alias(OBJECT(pcms), alias_prop_name,
                              OBJECT(dev), "drive");
    /*
     * The returned reference is tied to the child property and
     * will be removed with object_unparent.
     */
    object_unref(OBJECT(dev));
    return PFLASH_CFI01(dev);
}

void pc_system_flash_create(PCMachineState *pcms)
{
    PCMachineClass *pcmc = PC_MACHINE_GET_CLASS(pcms);

    if (pcmc->pci_enabled) {
        pcms->flash[0] = pc_pflash_create(pcms, "system.flash0",
                                          "pflash0");
        pcms->flash[1] = pc_pflash_create(pcms, "system.flash1",
                                          "pflash1");
    }
}

void pc_system_flash_cleanup_unused(PCMachineState *pcms)
{
    char *prop_name;
    int i;
    Object *dev_obj;

    assert(PC_MACHINE_GET_CLASS(pcms)->pci_enabled);

    for (i = 0; i < ARRAY_SIZE(pcms->flash); i++) {
        dev_obj = OBJECT(pcms->flash[i]);
        if (!object_property_get_bool(dev_obj, "realized", &error_abort)) {
            prop_name = g_strdup_printf("pflash%d", i);
            object_property_del(OBJECT(pcms), prop_name);
            g_free(prop_name);
            object_unparent(dev_obj);
            pcms->flash[i] = NULL;
        }
    }
}

/*
 * Map the pcms->flash[] from 4GiB downward, and realize.
 * Map them in descending order, i.e. pcms->flash[0] at the top,
 * without gaps.
 * Stop at the first pcms->flash[0] lacking a block backend.
 * Set each flash's size from its block backend.  Fatal error if the
 * size isn't a non-zero multiple of 4KiB, or the total size exceeds
 * FLASH_SIZE_LIMIT.
 *
 * If pcms->flash[0] has a block backend, its memory is passed to
 * pc_isa_bios_init().  Merging several flash devices for isa-bios is
 * not supported.
 */
static void pc_system_flash_map(PCMachineState *pcms,
                                MemoryRegion *rom_memory)
{
    hwaddr total_size = 0;
    int i;
    BlockBackend *blk;
    int64_t size;
    PFlashCFI01 *system_flash;
    MemoryRegion *flash_mem;
    void *flash_ptr;
    int ret, flash_size;

    assert(PC_MACHINE_GET_CLASS(pcms)->pci_enabled);

    for (i = 0; i < ARRAY_SIZE(pcms->flash); i++) {
        system_flash = pcms->flash[i];
        blk = pflash_cfi01_get_blk(system_flash);
        if (!blk) {
            break;
        }
        size = blk_getlength(blk);
        if (size < 0) {
            error_report("can't get size of block device %s: %s",
                         blk_name(blk), strerror(-size));
            exit(1);
        }
        if (size == 0 || !QEMU_IS_ALIGNED(size, FLASH_SECTOR_SIZE)) {
            error_report("system firmware block device %s has invalid size "
                         "%" PRId64,
                         blk_name(blk), size);
            info_report("its size must be a non-zero multiple of 0x%x",
                        FLASH_SECTOR_SIZE);
            exit(1);
        }
        if ((hwaddr)size != size
            || total_size > HWADDR_MAX - size
            || total_size + size > FLASH_SIZE_LIMIT) {
            error_report("combined size of system firmware exceeds "
                         "%" PRIu64 " bytes",
                         FLASH_SIZE_LIMIT);
            exit(1);
        }

        total_size += size;
        qdev_prop_set_uint32(DEVICE(system_flash), "num-blocks",
                             size / FLASH_SECTOR_SIZE);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(system_flash), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(system_flash), 0,
                        0x100000000ULL - total_size);

        if (i == 0) {
            flash_mem = pflash_cfi01_get_memory(system_flash);
            pc_isa_bios_init(rom_memory, flash_mem, size);

            /* Encrypt the pflash boot ROM */
            if (kvm_memcrypt_enabled()) {
                flash_ptr = memory_region_get_ram_ptr(flash_mem);
                flash_size = memory_region_size(flash_mem);
                ret = kvm_memcrypt_encrypt_data(flash_ptr, flash_size);
                if (ret) {
                    error_report("failed to encrypt pflash rom");
                    exit(1);
                }
            }
        }
    }
}

void pc_system_firmware_init(PCMachineState *pcms,
                             MemoryRegion *rom_memory)
{
    PCMachineClass *pcmc = PC_MACHINE_GET_CLASS(pcms);
    int i;
    BlockBackend *pflash_blk[ARRAY_SIZE(pcms->flash)];

    if (!pcmc->pci_enabled) {
        x86_bios_rom_init(rom_memory, true);
        return;
    }

    /* Map legacy -drive if=pflash to machine properties */
    for (i = 0; i < ARRAY_SIZE(pcms->flash); i++) {
        pflash_cfi01_legacy_drive(pcms->flash[i],
                                  drive_get(IF_PFLASH, 0, i));
        pflash_blk[i] = pflash_cfi01_get_blk(pcms->flash[i]);
    }

    /* Reject gaps */
    for (i = 1; i < ARRAY_SIZE(pcms->flash); i++) {
        if (pflash_blk[i] && !pflash_blk[i - 1]) {
            error_report("pflash%d requires pflash%d", i, i - 1);
            exit(1);
        }
    }

    if (!pflash_blk[0]) {
        /* Machine property pflash0 not set, use ROM mode */
        x86_bios_rom_init(rom_memory, false);
    } else {
        if (kvm_enabled() && !kvm_readonly_mem_enabled()) {
            /*
             * Older KVM cannot execute from device memory. So, flash
             * memory cannot be used unless the readonly memory kvm
             * capability is present.
             */
            error_report("pflash with kvm requires KVM readonly memory support");
            exit(1);
        }

        pc_system_flash_map(pcms, rom_memory);
    }

    pc_system_flash_cleanup_unused(pcms);
}
