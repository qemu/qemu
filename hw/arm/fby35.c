/*
 * Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)
 *
 * This code is licensed under the GPL version 2 or later. See the COPYING
 * file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "sysemu/sysemu.h"
#include "sysemu/block-backend.h"
#include "hw/boards.h"
#include "hw/qdev-clock.h"
#include "hw/arm/aspeed_soc.h"
#include "hw/arm/boot.h"

#define TYPE_FBY35 MACHINE_TYPE_NAME("fby35")
OBJECT_DECLARE_SIMPLE_TYPE(Fby35State, FBY35);

struct Fby35State {
    MachineState parent_obj;

    MemoryRegion bmc_memory;
    MemoryRegion bmc_dram;
    MemoryRegion bmc_boot_rom;
    MemoryRegion bic_memory;
    Clock *bic_sysclk;

    Aspeed2600SoCState bmc;
    Aspeed10x0SoCState bic;

    bool mmio_exec;
};

#define FBY35_BMC_RAM_SIZE (2 * GiB)
#define FBY35_BMC_FIRMWARE_ADDR 0x0

static void fby35_bmc_write_boot_rom(DriveInfo *dinfo, MemoryRegion *mr,
                                     hwaddr offset, size_t rom_size,
                                     Error **errp)
{
    BlockBackend *blk = blk_by_legacy_dinfo(dinfo);
    g_autofree void *storage = NULL;
    int64_t size;

    /*
     * The block backend size should have already been 'validated' by
     * the creation of the m25p80 object.
     */
    size = blk_getlength(blk);
    if (size <= 0) {
        error_setg(errp, "failed to get flash size");
        return;
    }

    if (rom_size > size) {
        rom_size = size;
    }

    storage = g_malloc0(rom_size);
    if (blk_pread(blk, 0, rom_size, storage, 0) < 0) {
        error_setg(errp, "failed to read the initial flash content");
        return;
    }

    /* TODO: find a better way to install the ROM */
    memcpy(memory_region_get_ram_ptr(mr) + offset, storage, rom_size);
}

static void fby35_bmc_init(Fby35State *s)
{
    AspeedSoCState *soc;

    object_initialize_child(OBJECT(s), "bmc", &s->bmc, "ast2600-a3");
    soc = ASPEED_SOC(&s->bmc);

    memory_region_init(&s->bmc_memory, OBJECT(&s->bmc), "bmc-memory",
                       UINT64_MAX);
    memory_region_init_ram(&s->bmc_dram, OBJECT(&s->bmc), "bmc-dram",
                           FBY35_BMC_RAM_SIZE, &error_abort);

    object_property_set_int(OBJECT(&s->bmc), "ram-size", FBY35_BMC_RAM_SIZE,
                            &error_abort);
    object_property_set_link(OBJECT(&s->bmc), "memory", OBJECT(&s->bmc_memory),
                             &error_abort);
    object_property_set_link(OBJECT(&s->bmc), "dram", OBJECT(&s->bmc_dram),
                             &error_abort);
    object_property_set_int(OBJECT(&s->bmc), "hw-strap1", 0x000000C0,
                            &error_abort);
    object_property_set_int(OBJECT(&s->bmc), "hw-strap2", 0x00000003,
                            &error_abort);
    aspeed_soc_uart_set_chr(soc, ASPEED_DEV_UART5, serial_hd(0));
    qdev_realize(DEVICE(&s->bmc), NULL, &error_abort);

    aspeed_board_init_flashes(&soc->fmc, "n25q00", 2, 0);

    /* Install first FMC flash content as a boot rom. */
    if (!s->mmio_exec) {
        DriveInfo *mtd0 = drive_get(IF_MTD, 0, 0);

        if (mtd0) {
            uint64_t rom_size = memory_region_size(&soc->spi_boot);

            memory_region_init_rom(&s->bmc_boot_rom, NULL, "aspeed.boot_rom",
                                   rom_size, &error_abort);
            memory_region_add_subregion_overlap(&soc->spi_boot_container, 0,
                                                &s->bmc_boot_rom, 1);

            fby35_bmc_write_boot_rom(mtd0, &s->bmc_boot_rom,
                                     FBY35_BMC_FIRMWARE_ADDR,
                                     rom_size, &error_abort);
        }
    }
}

static void fby35_bic_init(Fby35State *s)
{
    AspeedSoCState *soc;

    s->bic_sysclk = clock_new(OBJECT(s), "SYSCLK");
    clock_set_hz(s->bic_sysclk, 200000000ULL);

    object_initialize_child(OBJECT(s), "bic", &s->bic, "ast1030-a1");
    soc = ASPEED_SOC(&s->bic);

    memory_region_init(&s->bic_memory, OBJECT(&s->bic), "bic-memory",
                       UINT64_MAX);

    qdev_connect_clock_in(DEVICE(&s->bic), "sysclk", s->bic_sysclk);
    object_property_set_link(OBJECT(&s->bic), "memory", OBJECT(&s->bic_memory),
                             &error_abort);
    aspeed_soc_uart_set_chr(soc, ASPEED_DEV_UART5, serial_hd(1));
    qdev_realize(DEVICE(&s->bic), NULL, &error_abort);

    aspeed_board_init_flashes(&soc->fmc, "sst25vf032b", 2, 2);
    aspeed_board_init_flashes(&soc->spi[0], "sst25vf032b", 2, 4);
    aspeed_board_init_flashes(&soc->spi[1], "sst25vf032b", 2, 6);
}

static void fby35_init(MachineState *machine)
{
    Fby35State *s = FBY35(machine);

    fby35_bmc_init(s);
    fby35_bic_init(s);
}


static bool fby35_get_mmio_exec(Object *obj, Error **errp)
{
    return FBY35(obj)->mmio_exec;
}

static void fby35_set_mmio_exec(Object *obj, bool value, Error **errp)
{
    FBY35(obj)->mmio_exec = value;
}

static void fby35_instance_init(Object *obj)
{
    FBY35(obj)->mmio_exec = false;
}

static void fby35_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Meta Platforms fby35";
    mc->init = fby35_init;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->min_cpus = mc->max_cpus = mc->default_cpus = 3;

    object_class_property_add_bool(oc, "execute-in-place",
                                   fby35_get_mmio_exec,
                                   fby35_set_mmio_exec);
    object_class_property_set_description(oc, "execute-in-place",
                           "boot directly from CE0 flash device");
}

static const TypeInfo fby35_types[] = {
    {
        .name = MACHINE_TYPE_NAME("fby35"),
        .parent = TYPE_MACHINE,
        .class_init = fby35_class_init,
        .instance_size = sizeof(Fby35State),
        .instance_init = fby35_instance_init,
    },
};

DEFINE_TYPES(fby35_types);
