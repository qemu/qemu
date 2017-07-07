/*
 * OpenPOWER Palmetto BMC
 *
 * Andrew Jeffery <andrew@aj.id.au>
 *
 * Copyright 2016 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "exec/address-spaces.h"
#include "hw/arm/arm.h"
#include "hw/arm/aspeed_soc.h"
#include "hw/boards.h"
#include "qemu/log.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"
#include "hw/loader.h"
#include "qemu/error-report.h"

static struct arm_boot_info aspeed_board_binfo = {
    .board_id = -1, /* device-tree-only board */
    .nb_cpus = 1,
};

typedef struct AspeedBoardState {
    AspeedSoCState soc;
    MemoryRegion ram;
} AspeedBoardState;

typedef struct AspeedBoardConfig {
    const char *soc_name;
    uint32_t hw_strap1;
    const char *fmc_model;
    const char *spi_model;
    uint32_t num_cs;
    void (*i2c_init)(AspeedBoardState *bmc);
} AspeedBoardConfig;

enum {
    PALMETTO_BMC,
    AST2500_EVB,
    ROMULUS_BMC,
};

/* Palmetto hardware value: 0x120CE416 */
#define PALMETTO_BMC_HW_STRAP1 (                                        \
        SCU_AST2400_HW_STRAP_DRAM_SIZE(DRAM_SIZE_256MB) |               \
        SCU_AST2400_HW_STRAP_DRAM_CONFIG(2 /* DDR3 with CL=6, CWL=5 */) | \
        SCU_AST2400_HW_STRAP_ACPI_DIS |                                 \
        SCU_AST2400_HW_STRAP_SET_CLK_SOURCE(AST2400_CLK_48M_IN) |       \
        SCU_HW_STRAP_VGA_CLASS_CODE |                                   \
        SCU_HW_STRAP_LPC_RESET_PIN |                                    \
        SCU_HW_STRAP_SPI_MODE(SCU_HW_STRAP_SPI_M_S_EN) |                \
        SCU_AST2400_HW_STRAP_SET_CPU_AHB_RATIO(AST2400_CPU_AHB_RATIO_2_1) | \
        SCU_HW_STRAP_SPI_WIDTH |                                        \
        SCU_HW_STRAP_VGA_SIZE_SET(VGA_16M_DRAM) |                       \
        SCU_AST2400_HW_STRAP_BOOT_MODE(AST2400_SPI_BOOT))

/* AST2500 evb hardware value: 0xF100C2E6 */
#define AST2500_EVB_HW_STRAP1 ((                                        \
        AST2500_HW_STRAP1_DEFAULTS |                                    \
        SCU_AST2500_HW_STRAP_SPI_AUTOFETCH_ENABLE |                     \
        SCU_AST2500_HW_STRAP_GPIO_STRAP_ENABLE |                        \
        SCU_AST2500_HW_STRAP_UART_DEBUG |                               \
        SCU_AST2500_HW_STRAP_DDR4_ENABLE |                              \
        SCU_HW_STRAP_MAC1_RGMII |                                       \
        SCU_HW_STRAP_MAC0_RGMII) &                                      \
        ~SCU_HW_STRAP_2ND_BOOT_WDT)

/* Romulus hardware value: 0xF10AD206 */
#define ROMULUS_BMC_HW_STRAP1 (                                         \
        AST2500_HW_STRAP1_DEFAULTS |                                    \
        SCU_AST2500_HW_STRAP_SPI_AUTOFETCH_ENABLE |                     \
        SCU_AST2500_HW_STRAP_GPIO_STRAP_ENABLE |                        \
        SCU_AST2500_HW_STRAP_UART_DEBUG |                               \
        SCU_AST2500_HW_STRAP_DDR4_ENABLE |                              \
        SCU_AST2500_HW_STRAP_ACPI_ENABLE |                              \
        SCU_HW_STRAP_SPI_MODE(SCU_HW_STRAP_SPI_MASTER))

static void palmetto_bmc_i2c_init(AspeedBoardState *bmc);
static void ast2500_evb_i2c_init(AspeedBoardState *bmc);

static const AspeedBoardConfig aspeed_boards[] = {
    [PALMETTO_BMC] = {
        .soc_name  = "ast2400-a1",
        .hw_strap1 = PALMETTO_BMC_HW_STRAP1,
        .fmc_model = "n25q256a",
        .spi_model = "mx25l25635e",
        .num_cs    = 1,
        .i2c_init  = palmetto_bmc_i2c_init,
    },
    [AST2500_EVB]  = {
        .soc_name  = "ast2500-a1",
        .hw_strap1 = AST2500_EVB_HW_STRAP1,
        .fmc_model = "n25q256a",
        .spi_model = "mx25l25635e",
        .num_cs    = 1,
        .i2c_init  = ast2500_evb_i2c_init,
    },
    [ROMULUS_BMC]  = {
        .soc_name  = "ast2500-a1",
        .hw_strap1 = ROMULUS_BMC_HW_STRAP1,
        .fmc_model = "n25q256a",
        .spi_model = "mx66l1g45g",
        .num_cs    = 2,
    },
};

#define FIRMWARE_ADDR 0x0

static void write_boot_rom(DriveInfo *dinfo, hwaddr addr, size_t rom_size,
                           Error **errp)
{
    BlockBackend *blk = blk_by_legacy_dinfo(dinfo);
    uint8_t *storage;
    int64_t size;

    /* The block backend size should have already been 'validated' by
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

    storage = g_new0(uint8_t, rom_size);
    if (blk_pread(blk, 0, storage, rom_size) < 0) {
        error_setg(errp, "failed to read the initial flash content");
        return;
    }

    rom_add_blob_fixed("aspeed.boot_rom", storage, rom_size, addr);
    g_free(storage);
}

static void aspeed_board_init_flashes(AspeedSMCState *s, const char *flashtype,
                                      Error **errp)
{
    int i ;

    for (i = 0; i < s->num_cs; ++i) {
        AspeedSMCFlash *fl = &s->flashes[i];
        DriveInfo *dinfo = drive_get_next(IF_MTD);
        qemu_irq cs_line;

        fl->flash = ssi_create_slave_no_init(s->spi, flashtype);
        if (dinfo) {
            qdev_prop_set_drive(fl->flash, "drive", blk_by_legacy_dinfo(dinfo),
                                errp);
        }
        qdev_init_nofail(fl->flash);

        cs_line = qdev_get_gpio_in_named(fl->flash, SSI_GPIO_CS, 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(s), i + 1, cs_line);
    }
}

static void aspeed_board_init(MachineState *machine,
                              const AspeedBoardConfig *cfg)
{
    AspeedBoardState *bmc;
    AspeedSoCClass *sc;
    DriveInfo *drive0 = drive_get(IF_MTD, 0, 0);

    bmc = g_new0(AspeedBoardState, 1);
    object_initialize(&bmc->soc, (sizeof(bmc->soc)), cfg->soc_name);
    object_property_add_child(OBJECT(machine), "soc", OBJECT(&bmc->soc),
                              &error_abort);

    sc = ASPEED_SOC_GET_CLASS(&bmc->soc);

    object_property_set_uint(OBJECT(&bmc->soc), ram_size, "ram-size",
                             &error_abort);
    object_property_set_int(OBJECT(&bmc->soc), cfg->hw_strap1, "hw-strap1",
                            &error_abort);
    object_property_set_int(OBJECT(&bmc->soc), cfg->num_cs, "num-cs",
                            &error_abort);
    object_property_set_bool(OBJECT(&bmc->soc), true, "realized",
                             &error_abort);

    /*
     * Allocate RAM after the memory controller has checked the size
     * was valid. If not, a default value is used.
     */
    ram_size = object_property_get_uint(OBJECT(&bmc->soc), "ram-size",
                                        &error_abort);

    memory_region_allocate_system_memory(&bmc->ram, NULL, "ram", ram_size);
    memory_region_add_subregion(get_system_memory(), sc->info->sdram_base,
                                &bmc->ram);
    object_property_add_const_link(OBJECT(&bmc->soc), "ram", OBJECT(&bmc->ram),
                                   &error_abort);

    aspeed_board_init_flashes(&bmc->soc.fmc, cfg->fmc_model, &error_abort);
    aspeed_board_init_flashes(&bmc->soc.spi[0], cfg->spi_model, &error_abort);

    /* Install first FMC flash content as a boot rom. */
    if (drive0) {
        AspeedSMCFlash *fl = &bmc->soc.fmc.flashes[0];
        MemoryRegion *boot_rom = g_new(MemoryRegion, 1);

        /*
         * create a ROM region using the default mapping window size of
         * the flash module. The window size is 64MB for the AST2400
         * SoC and 128MB for the AST2500 SoC, which is twice as big as
         * needed by the flash modules of the Aspeed machines.
         */
        memory_region_init_rom_nomigrate(boot_rom, OBJECT(bmc), "aspeed.boot_rom",
                               fl->size, &error_abort);
        memory_region_add_subregion(get_system_memory(), FIRMWARE_ADDR,
                                    boot_rom);
        write_boot_rom(drive0, FIRMWARE_ADDR, fl->size, &error_abort);
    }

    aspeed_board_binfo.kernel_filename = machine->kernel_filename;
    aspeed_board_binfo.initrd_filename = machine->initrd_filename;
    aspeed_board_binfo.kernel_cmdline = machine->kernel_cmdline;
    aspeed_board_binfo.ram_size = ram_size;
    aspeed_board_binfo.loader_start = sc->info->sdram_base;

    if (cfg->i2c_init) {
        cfg->i2c_init(bmc);
    }

    arm_load_kernel(ARM_CPU(first_cpu), &aspeed_board_binfo);
}

static void palmetto_bmc_i2c_init(AspeedBoardState *bmc)
{
    AspeedSoCState *soc = &bmc->soc;
    DeviceState *dev;

    /* The palmetto platform expects a ds3231 RTC but a ds1338 is
     * enough to provide basic RTC features. Alarms will be missing */
    i2c_create_slave(aspeed_i2c_get_bus(DEVICE(&soc->i2c), 0), "ds1338", 0x68);

    /* add a TMP423 temperature sensor */
    dev = i2c_create_slave(aspeed_i2c_get_bus(DEVICE(&soc->i2c), 2),
                           "tmp423", 0x4c);
    object_property_set_int(OBJECT(dev), 31000, "temperature0", &error_abort);
    object_property_set_int(OBJECT(dev), 28000, "temperature1", &error_abort);
    object_property_set_int(OBJECT(dev), 20000, "temperature2", &error_abort);
    object_property_set_int(OBJECT(dev), 110000, "temperature3", &error_abort);
}

static void palmetto_bmc_init(MachineState *machine)
{
    aspeed_board_init(machine, &aspeed_boards[PALMETTO_BMC]);
}

static void palmetto_bmc_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "OpenPOWER Palmetto BMC (ARM926EJ-S)";
    mc->init = palmetto_bmc_init;
    mc->max_cpus = 1;
    mc->no_sdcard = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_parallel = 1;
}

static const TypeInfo palmetto_bmc_type = {
    .name = MACHINE_TYPE_NAME("palmetto-bmc"),
    .parent = TYPE_MACHINE,
    .class_init = palmetto_bmc_class_init,
};

static void ast2500_evb_i2c_init(AspeedBoardState *bmc)
{
    AspeedSoCState *soc = &bmc->soc;

    /* The AST2500 EVB expects a LM75 but a TMP105 is compatible */
    i2c_create_slave(aspeed_i2c_get_bus(DEVICE(&soc->i2c), 7), "tmp105", 0x4d);
}

static void ast2500_evb_init(MachineState *machine)
{
    aspeed_board_init(machine, &aspeed_boards[AST2500_EVB]);
}

static void ast2500_evb_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Aspeed AST2500 EVB (ARM1176)";
    mc->init = ast2500_evb_init;
    mc->max_cpus = 1;
    mc->no_sdcard = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_parallel = 1;
}

static const TypeInfo ast2500_evb_type = {
    .name = MACHINE_TYPE_NAME("ast2500-evb"),
    .parent = TYPE_MACHINE,
    .class_init = ast2500_evb_class_init,
};

static void romulus_bmc_init(MachineState *machine)
{
    aspeed_board_init(machine, &aspeed_boards[ROMULUS_BMC]);
}

static void romulus_bmc_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "OpenPOWER Romulus BMC (ARM1176)";
    mc->init = romulus_bmc_init;
    mc->max_cpus = 1;
    mc->no_sdcard = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_parallel = 1;
}

static const TypeInfo romulus_bmc_type = {
    .name = MACHINE_TYPE_NAME("romulus-bmc"),
    .parent = TYPE_MACHINE,
    .class_init = romulus_bmc_class_init,
};

static void aspeed_machine_init(void)
{
    type_register_static(&palmetto_bmc_type);
    type_register_static(&ast2500_evb_type);
    type_register_static(&romulus_bmc_type);
}

type_init(aspeed_machine_init)
