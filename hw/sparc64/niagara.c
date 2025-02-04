/*
 * QEMU Sun4v/Niagara System Emulator
 *
 * Copyright (c) 2016 Artyom Tarasenko
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
#include "block/block_int-common.h"
#include "qemu/units.h"
#include "cpu.h"
#include "hw/boards.h"
#include "hw/char/serial-mm.h"
#include "hw/misc/unimp.h"
#include "hw/loader.h"
#include "hw/sparc/sparc64.h"
#include "hw/rtc/sun4v-rtc.h"
#include "system/block-backend.h"
#include "qemu/error-report.h"
#include "system/qtest.h"
#include "system/system.h"
#include "qapi/error.h"

typedef struct NiagaraBoardState {
    MemoryRegion hv_ram;
    MemoryRegion nvram;
    MemoryRegion md_rom;
    MemoryRegion hv_rom;
    MemoryRegion vdisk_ram;
    MemoryRegion prom;
} NiagaraBoardState;

#define NIAGARA_HV_RAM_BASE 0x100000ULL
#define NIAGARA_HV_RAM_SIZE 0x3f00000ULL /* 63 MiB */

#define NIAGARA_PARTITION_RAM_BASE 0x80000000ULL

#define NIAGARA_UART_BASE   0x1f10000000ULL

#define NIAGARA_NVRAM_BASE  0x1f11000000ULL
#define NIAGARA_NVRAM_SIZE  0x2000

#define NIAGARA_MD_ROM_BASE 0x1f12000000ULL
#define NIAGARA_MD_ROM_SIZE 0x2000

#define NIAGARA_HV_ROM_BASE 0x1f12080000ULL
#define NIAGARA_HV_ROM_SIZE 0x2000

#define NIAGARA_IOBBASE     0x9800000000ULL
#define NIAGARA_IOBSIZE     0x0100000000ULL

#define NIAGARA_VDISK_BASE  0x1f40000000ULL
#define NIAGARA_RTC_BASE    0xfff0c1fff8ULL

/* Firmware layout
 *
 * |------------------|
 * |   openboot.bin   |
 * |------------------| PROM_ADDR + OBP_OFFSET
 * |      q.bin       |
 * |------------------| PROM_ADDR + Q_OFFSET
 * |     reset.bin    |
 * |------------------| PROM_ADDR
 */
#define NIAGARA_PROM_BASE   0xfff0000000ULL
#define NIAGARA_Q_OFFSET    0x10000ULL
#define NIAGARA_OBP_OFFSET  0x80000ULL
#define PROM_SIZE_MAX       (4 * MiB)

static void add_rom_or_fail(const char *file, const hwaddr addr)
{
    /* XXX remove qtest_enabled() check once firmware files are
     * in the qemu tree
     */
    if (!qtest_enabled() && rom_add_file_fixed(file, addr, -1)) {
        error_report("Unable to load a firmware for -M niagara");
        exit(1);
    }

}
/* Niagara hardware initialisation */
static void niagara_init(MachineState *machine)
{
    NiagaraBoardState *s = g_new(NiagaraBoardState, 1);
    DriveInfo *dinfo = drive_get(IF_PFLASH, 0, 0);
    MemoryRegion *sysmem = get_system_memory();

    /* init CPUs */
    sparc64_cpu_devinit(machine->cpu_type, NIAGARA_PROM_BASE);
    /* set up devices */
    memory_region_init_ram(&s->hv_ram, NULL, "sun4v-hv.ram",
                           NIAGARA_HV_RAM_SIZE, &error_fatal);
    memory_region_add_subregion(sysmem, NIAGARA_HV_RAM_BASE, &s->hv_ram);

    memory_region_add_subregion(sysmem, NIAGARA_PARTITION_RAM_BASE,
                                machine->ram);

    memory_region_init_ram(&s->nvram, NULL, "sun4v.nvram", NIAGARA_NVRAM_SIZE,
                           &error_fatal);
    memory_region_add_subregion(sysmem, NIAGARA_NVRAM_BASE, &s->nvram);
    memory_region_init_ram(&s->md_rom, NULL, "sun4v-md.rom",
                           NIAGARA_MD_ROM_SIZE, &error_fatal);
    memory_region_add_subregion(sysmem, NIAGARA_MD_ROM_BASE, &s->md_rom);
    memory_region_init_ram(&s->hv_rom, NULL, "sun4v-hv.rom",
                           NIAGARA_HV_ROM_SIZE, &error_fatal);
    memory_region_add_subregion(sysmem, NIAGARA_HV_ROM_BASE, &s->hv_rom);
    memory_region_init_ram(&s->prom, NULL, "sun4v.prom", PROM_SIZE_MAX,
                           &error_fatal);
    memory_region_add_subregion(sysmem, NIAGARA_PROM_BASE, &s->prom);

    add_rom_or_fail("nvram1", NIAGARA_NVRAM_BASE);
    add_rom_or_fail("1up-md.bin", NIAGARA_MD_ROM_BASE);
    add_rom_or_fail("1up-hv.bin", NIAGARA_HV_ROM_BASE);

    add_rom_or_fail("reset.bin", NIAGARA_PROM_BASE);
    add_rom_or_fail("q.bin", NIAGARA_PROM_BASE + NIAGARA_Q_OFFSET);
    add_rom_or_fail("openboot.bin", NIAGARA_PROM_BASE + NIAGARA_OBP_OFFSET);

    /* the virtual ramdisk is kind of initrd, but it resides
       outside of the partition RAM */
    if (dinfo) {
        BlockBackend *blk = blk_by_legacy_dinfo(dinfo);
        int size = blk_getlength(blk);
        if (size > 0) {
            memory_region_init_ram(&s->vdisk_ram, NULL, "sun4v_vdisk.ram", size,
                                   &error_fatal);
            memory_region_add_subregion(get_system_memory(),
                                        NIAGARA_VDISK_BASE, &s->vdisk_ram);
            dinfo->is_default = 1;
            rom_add_file_fixed(blk_bs(blk)->filename, NIAGARA_VDISK_BASE, -1);
        } else {
            error_report("could not load ram disk '%s'",
                         blk_bs(blk)->filename);
            exit(1);
        }
    }
    serial_mm_init(sysmem, NIAGARA_UART_BASE, 0, NULL,
                   115200, serial_hd(0), DEVICE_BIG_ENDIAN);
    create_unimplemented_device("sun4v-iob", NIAGARA_IOBBASE, NIAGARA_IOBSIZE);
    sun4v_rtc_init(NIAGARA_RTC_BASE);
}

static void niagara_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Sun4v platform, Niagara";
    mc->init = niagara_init;
    mc->max_cpus = 1; /* XXX for now */
    mc->default_boot_order = "c";
    mc->default_cpu_type = SPARC_CPU_TYPE_NAME("Sun-UltraSparc-T1");
    mc->default_ram_id = "sun4v-partition.ram";
}

static const TypeInfo niagara_type = {
    .name = MACHINE_TYPE_NAME("niagara"),
    .parent = TYPE_MACHINE,
    .class_init = niagara_class_init,
};

static void niagara_register_types(void)
{
    type_register_static(&niagara_type);
}

type_init(niagara_register_types)
