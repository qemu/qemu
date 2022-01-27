/*
 * SABRELITE Board System emulation.
 *
 * Copyright (c) 2015 Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * This code is licensed under the GPL, version 2 or later.
 * See the file `COPYING' in the top level directory.
 *
 * It (partially) emulates a sabrelite board, with a Freescale
 * i.MX6 SoC
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/fsl-imx6.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "qemu/error-report.h"
#include "sysemu/qtest.h"

static struct arm_boot_info sabrelite_binfo = {
    /* DDR memory start */
    .loader_start = FSL_IMX6_MMDC_ADDR,
    /* No board ID, we boot from DT tree */
    .board_id = -1,
};

/* No need to do any particular setup for secondary boot */
static void sabrelite_write_secondary(ARMCPU *cpu,
                                      const struct arm_boot_info *info)
{
}

/* Secondary cores are reset through SRC device */
static void sabrelite_reset_secondary(ARMCPU *cpu,
                                      const struct arm_boot_info *info)
{
}

static void sabrelite_init(MachineState *machine)
{
    FslIMX6State *s;

    /* Check the amount of memory is compatible with the SOC */
    if (machine->ram_size > FSL_IMX6_MMDC_SIZE) {
        error_report("RAM size " RAM_ADDR_FMT " above max supported (%08x)",
                     machine->ram_size, FSL_IMX6_MMDC_SIZE);
        exit(1);
    }

    s = FSL_IMX6(object_new(TYPE_FSL_IMX6));
    object_property_add_child(OBJECT(machine), "soc", OBJECT(s));

    /* Ethernet PHY address is 6 */
    object_property_set_int(OBJECT(s), "fec-phy-num", 6, &error_fatal);

    qdev_realize(DEVICE(s), NULL, &error_fatal);

    memory_region_add_subregion(get_system_memory(), FSL_IMX6_MMDC_ADDR,
                                machine->ram);

    {
        /*
         * TODO: Ideally we would expose the chip select and spi bus on the
         * SoC object using alias properties; then we would not need to
         * directly access the underlying spi device object.
         */
        /* Add the sst25vf016b NOR FLASH memory to first SPI */
        Object *spi_dev;

        spi_dev = object_resolve_path_component(OBJECT(s), "spi1");
        if (spi_dev) {
            SSIBus *spi_bus;

            spi_bus = (SSIBus *)qdev_get_child_bus(DEVICE(spi_dev), "spi");
            if (spi_bus) {
                DeviceState *flash_dev;
                qemu_irq cs_line;
                DriveInfo *dinfo = drive_get(IF_MTD, 0, 0);

                flash_dev = qdev_new("sst25vf016b");
                if (dinfo) {
                    qdev_prop_set_drive_err(flash_dev, "drive",
                                            blk_by_legacy_dinfo(dinfo),
                                            &error_fatal);
                }
                qdev_realize_and_unref(flash_dev, BUS(spi_bus), &error_fatal);

                cs_line = qdev_get_gpio_in_named(flash_dev, SSI_GPIO_CS, 0);
                qdev_connect_gpio_out(DEVICE(&s->gpio[2]), 19, cs_line);
            }
        }
    }

    sabrelite_binfo.ram_size = machine->ram_size;
    sabrelite_binfo.secure_boot = true;
    sabrelite_binfo.write_secondary_boot = sabrelite_write_secondary;
    sabrelite_binfo.secondary_cpu_reset_hook = sabrelite_reset_secondary;

    if (!qtest_enabled()) {
        arm_load_kernel(&s->cpu[0], machine, &sabrelite_binfo);
    }
}

static void sabrelite_machine_init(MachineClass *mc)
{
    mc->desc = "Freescale i.MX6 Quad SABRE Lite Board (Cortex-A9)";
    mc->init = sabrelite_init;
    mc->max_cpus = FSL_IMX6_NUM_CPUS;
    mc->ignore_memory_transaction_failures = true;
    mc->default_ram_id = "sabrelite.ram";
}

DEFINE_MACHINE("sabrelite", sabrelite_machine_init)
