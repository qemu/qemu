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
#include "hw/arm/boot.h"
#include "hw/arm/aspeed.h"
#include "hw/arm/aspeed_soc.h"
#include "hw/arm/machines-qom.h"
#include "hw/block/flash.h"
#include "hw/gpio/pca9552.h"
#include "hw/gpio/pca9554.h"
#include "system/block-backend.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "hw/qdev-clock.h"
#include "system/system.h"

static struct arm_boot_info aspeed_board_binfo = {
    .board_id = -1, /* device-tree-only board */
};

#define AST_SMP_MAILBOX_BASE            0x1e6e2180
#define AST_SMP_MBOX_FIELD_ENTRY        (AST_SMP_MAILBOX_BASE + 0x0)
#define AST_SMP_MBOX_FIELD_GOSIGN       (AST_SMP_MAILBOX_BASE + 0x4)
#define AST_SMP_MBOX_FIELD_READY        (AST_SMP_MAILBOX_BASE + 0x8)
#define AST_SMP_MBOX_FIELD_POLLINSN     (AST_SMP_MAILBOX_BASE + 0xc)
#define AST_SMP_MBOX_CODE               (AST_SMP_MAILBOX_BASE + 0x10)
#define AST_SMP_MBOX_GOSIGN             0xabbaab00

static void aspeed_write_smpboot(ARMCPU *cpu,
                                 const struct arm_boot_info *info)
{
    AddressSpace *as = arm_boot_address_space(cpu, info);
    static const ARMInsnFixup poll_mailbox_ready[] = {
        /*
         * r2 = per-cpu go sign value
         * r1 = AST_SMP_MBOX_FIELD_ENTRY
         * r0 = AST_SMP_MBOX_FIELD_GOSIGN
         */
        { 0xee100fb0 },  /* mrc     p15, 0, r0, c0, c0, 5 */
        { 0xe21000ff },  /* ands    r0, r0, #255          */
        { 0xe59f201c },  /* ldr     r2, [pc, #28]         */
        { 0xe1822000 },  /* orr     r2, r2, r0            */

        { 0xe59f1018 },  /* ldr     r1, [pc, #24]         */
        { 0xe59f0018 },  /* ldr     r0, [pc, #24]         */

        { 0xe320f002 },  /* wfe                           */
        { 0xe5904000 },  /* ldr     r4, [r0]              */
        { 0xe1520004 },  /* cmp     r2, r4                */
        { 0x1afffffb },  /* bne     <wfe>                 */
        { 0xe591f000 },  /* ldr     pc, [r1]              */
        { AST_SMP_MBOX_GOSIGN },
        { AST_SMP_MBOX_FIELD_ENTRY },
        { AST_SMP_MBOX_FIELD_GOSIGN },
        { 0, FIXUP_TERMINATOR }
    };
    static const uint32_t fixupcontext[FIXUP_MAX] = { 0 };

    arm_write_bootloader("aspeed.smpboot", as, info->smp_loader_start,
                         poll_mailbox_ready, fixupcontext);
}

static void aspeed_reset_secondary(ARMCPU *cpu,
                                   const struct arm_boot_info *info)
{
    AddressSpace *as = arm_boot_address_space(cpu, info);
    CPUState *cs = CPU(cpu);

    /* info->smp_bootreg_addr */
    address_space_stl_notdirty(as, AST_SMP_MBOX_FIELD_GOSIGN, 0,
                               MEMTXATTRS_UNSPECIFIED, NULL);
    cpu_set_pc(cs, info->smp_loader_start);
}

static void sdhci_attach_drive(SDHCIState *sdhci, DriveInfo *dinfo, bool emmc,
                               bool boot_emmc)
{
        DeviceState *card;

        if (!dinfo) {
            return;
        }
        card = qdev_new(emmc ? TYPE_EMMC : TYPE_SD_CARD);

        /*
         * Force the boot properties of the eMMC device only when the
         * machine is strapped to boot from eMMC. Without these
         * settings, the machine would not boot.
         *
         * This also allows the machine to use an eMMC device without
         * boot areas when booting from the flash device (or -kernel)
         * Ideally, the device and its properties should be defined on
         * the command line.
         */
        if (emmc && boot_emmc) {
            qdev_prop_set_uint64(card, "boot-partition-size", 1 * MiB);
            qdev_prop_set_uint8(card, "boot-config", 0x1 << 3);
        }
        qdev_prop_set_drive_err(card, "drive", blk_by_legacy_dinfo(dinfo),
                                &error_fatal);
        qdev_realize_and_unref(card,
                               qdev_get_child_bus(DEVICE(sdhci), "sd-bus"),
                               &error_fatal);
}

void aspeed_connect_serial_hds_to_uarts(AspeedMachineState *bmc)
{
    AspeedMachineClass *amc = ASPEED_MACHINE_GET_CLASS(bmc);
    AspeedSoCState *s = bmc->soc;
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);
    int uart_chosen = bmc->uart_chosen ? bmc->uart_chosen : amc->uart_default;

    aspeed_soc_uart_set_chr(s->uart, uart_chosen, sc->uarts_base,
                            sc->uarts_num, serial_hd(0));
    for (int i = 1, uart = sc->uarts_base; i < sc->uarts_num; uart++) {
        if (uart == uart_chosen) {
            continue;
        }
        aspeed_soc_uart_set_chr(s->uart, uart, sc->uarts_base, sc->uarts_num,
                                serial_hd(i++));
    }
}

static void aspeed_machine_init(MachineState *machine)
{
    AspeedMachineState *bmc = ASPEED_MACHINE(machine);
    AspeedMachineClass *amc = ASPEED_MACHINE_GET_CLASS(machine);
    AspeedSoCClass *sc;
    int i;
    const char *bios_name = NULL;
    DriveInfo *emmc0 = NULL;
    bool boot_emmc;

    bmc->soc = ASPEED_SOC(object_new(amc->soc_name));
    object_property_add_child(OBJECT(machine), "soc", OBJECT(bmc->soc));
    object_unref(OBJECT(bmc->soc));
    sc = ASPEED_SOC_GET_CLASS(bmc->soc);

    /*
     * This will error out if the RAM size is not supported by the
     * memory controller of the SoC.
     */
    object_property_set_uint(OBJECT(bmc->soc), "ram-size", machine->ram_size,
                             &error_fatal);

    for (i = 0; i < sc->macs_num; i++) {
        if ((amc->macs_mask & (1 << i)) &&
            !qemu_configure_nic_device(DEVICE(&bmc->soc->ftgmac100[i]),
                                       true, NULL)) {
            break; /* No configs left; stop asking */
        }
    }

    object_property_set_int(OBJECT(bmc->soc), "hw-strap1", bmc->hw_strap1,
                            &error_abort);
    object_property_set_int(OBJECT(bmc->soc), "hw-strap2", amc->hw_strap2,
                            &error_abort);
    object_property_set_link(OBJECT(bmc->soc), "memory",
                             OBJECT(get_system_memory()), &error_abort);
    object_property_set_link(OBJECT(bmc->soc), "dram",
                             OBJECT(machine->ram), &error_abort);
    if (amc->sdhci_wp_inverted) {
        for (i = 0; i < bmc->soc->sdhci.num_slots; i++) {
            object_property_set_bool(OBJECT(&bmc->soc->sdhci.slots[i]),
                                     "wp-inverted", true, &error_abort);
        }
    }
    if (machine->kernel_filename) {
        /*
         * When booting with a -kernel command line there is no u-boot
         * that runs to unlock the SCU. In this case set the default to
         * be unlocked as the kernel expects
         */
        object_property_set_int(OBJECT(bmc->soc), "hw-prot-key",
                                ASPEED_SCU_PROT_KEY, &error_abort);
    }
    aspeed_connect_serial_hds_to_uarts(bmc);
    qdev_realize(DEVICE(bmc->soc), NULL, &error_abort);

    if (defaults_enabled()) {
        aspeed_board_init_flashes(&bmc->soc->fmc,
                              bmc->fmc_model ? bmc->fmc_model : amc->fmc_model,
                              amc->num_cs, 0);
        aspeed_board_init_flashes(&bmc->soc->spi[0],
                              bmc->spi_model ? bmc->spi_model : amc->spi_model,
                              1, amc->num_cs);
        aspeed_board_init_flashes(&bmc->soc->spi[1],
                                  amc->spi2_model, 1, amc->num_cs2);
    }

    if (machine->kernel_filename && sc->num_cpus > 1) {
        /* With no u-boot we must set up a boot stub for the secondary CPU */
        MemoryRegion *smpboot = g_new(MemoryRegion, 1);
        memory_region_init_ram(smpboot, NULL, "aspeed.smpboot",
                               0x80, &error_abort);
        memory_region_add_subregion(get_system_memory(),
                                    AST_SMP_MAILBOX_BASE, smpboot);

        aspeed_board_binfo.write_secondary_boot = aspeed_write_smpboot;
        aspeed_board_binfo.secondary_cpu_reset_hook = aspeed_reset_secondary;
        aspeed_board_binfo.smp_loader_start = AST_SMP_MBOX_CODE;
    }

    aspeed_board_binfo.ram_size = machine->ram_size;
    aspeed_board_binfo.loader_start = sc->memmap[ASPEED_DEV_SDRAM];

    if (amc->i2c_init) {
        amc->i2c_init(bmc);
    }

    for (i = 0; i < bmc->soc->sdhci.num_slots && defaults_enabled(); i++) {
        sdhci_attach_drive(&bmc->soc->sdhci.slots[i],
                           drive_get(IF_SD, 0, i), false, false);
    }

    boot_emmc = sc->boot_from_emmc(bmc->soc);

    if (bmc->soc->emmc.num_slots && defaults_enabled()) {
        emmc0 = drive_get(IF_SD, 0, bmc->soc->sdhci.num_slots);
        sdhci_attach_drive(&bmc->soc->emmc.slots[0], emmc0, true, boot_emmc);
    }

    if (!bmc->mmio_exec) {
        DeviceState *dev = ssi_get_cs(bmc->soc->fmc.spi, 0);
        BlockBackend *fmc0 = dev ? m25p80_get_blk(dev) : NULL;

        if (fmc0 && !boot_emmc) {
            uint64_t rom_size = memory_region_size(&bmc->soc->spi_boot);
            aspeed_install_boot_rom(bmc->soc, fmc0, &bmc->boot_rom, rom_size);
        } else if (emmc0) {
            aspeed_install_boot_rom(bmc->soc, blk_by_legacy_dinfo(emmc0),
                                    &bmc->boot_rom, 64 * KiB);
        }
    }

    if (amc->vbootrom) {
        bios_name = machine->firmware ?: VBOOTROM_FILE_NAME;
        aspeed_load_vbootrom(bmc->soc, bios_name, &error_abort);
    }

    arm_load_kernel(ARM_CPU(first_cpu), machine, &aspeed_board_binfo);
}

void aspeed_create_pca9552(AspeedSoCState *soc, int bus_id, int addr)
{
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, bus_id),
                            TYPE_PCA9552, addr);
}

I2CSlave *aspeed_create_pca9554(AspeedSoCState *soc, int bus_id, int addr)
{
    return i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, bus_id),
                            TYPE_PCA9554, addr);
}

static bool aspeed_get_mmio_exec(Object *obj, Error **errp)
{
    return ASPEED_MACHINE(obj)->mmio_exec;
}

static void aspeed_set_mmio_exec(Object *obj, bool value, Error **errp)
{
    ASPEED_MACHINE(obj)->mmio_exec = value;
}

static void aspeed_machine_instance_init(Object *obj)
{
    AspeedMachineClass *amc = ASPEED_MACHINE_GET_CLASS(obj);

    ASPEED_MACHINE(obj)->mmio_exec = false;
    ASPEED_MACHINE(obj)->hw_strap1 = amc->hw_strap1;
}

static char *aspeed_get_fmc_model(Object *obj, Error **errp)
{
    AspeedMachineState *bmc = ASPEED_MACHINE(obj);
    return g_strdup(bmc->fmc_model);
}

static void aspeed_set_fmc_model(Object *obj, const char *value, Error **errp)
{
    AspeedMachineState *bmc = ASPEED_MACHINE(obj);

    g_free(bmc->fmc_model);
    bmc->fmc_model = g_strdup(value);
}

static char *aspeed_get_spi_model(Object *obj, Error **errp)
{
    AspeedMachineState *bmc = ASPEED_MACHINE(obj);
    return g_strdup(bmc->spi_model);
}

static void aspeed_set_spi_model(Object *obj, const char *value, Error **errp)
{
    AspeedMachineState *bmc = ASPEED_MACHINE(obj);

    g_free(bmc->spi_model);
    bmc->spi_model = g_strdup(value);
}

static char *aspeed_get_bmc_console(Object *obj, Error **errp)
{
    AspeedMachineState *bmc = ASPEED_MACHINE(obj);
    AspeedMachineClass *amc = ASPEED_MACHINE_GET_CLASS(bmc);
    int uart_chosen = bmc->uart_chosen ? bmc->uart_chosen : amc->uart_default;

    return g_strdup_printf("uart%d", aspeed_uart_index(uart_chosen));
}

static void aspeed_set_bmc_console(Object *obj, const char *value, Error **errp)
{
    AspeedMachineState *bmc = ASPEED_MACHINE(obj);
    AspeedMachineClass *amc = ASPEED_MACHINE_GET_CLASS(bmc);
    AspeedSoCClass *sc = ASPEED_SOC_CLASS(object_class_by_name(amc->soc_name));
    int val;
    int uart_first = aspeed_uart_first(sc->uarts_base);
    int uart_last = aspeed_uart_last(sc->uarts_base, sc->uarts_num);

    if (sscanf(value, "uart%u", &val) != 1) {
        error_setg(errp, "Bad value for \"uart\" property");
        return;
    }

    /* The number of UART depends on the SoC */
    if (val < uart_first || val > uart_last) {
        error_setg(errp, "\"uart\" should be in range [%d - %d]",
                   uart_first, uart_last);
        return;
    }
    bmc->uart_chosen = val + ASPEED_DEV_UART0;
}

static void aspeed_machine_class_props_init(ObjectClass *oc)
{
    object_class_property_add_bool(oc, "execute-in-place",
                                   aspeed_get_mmio_exec,
                                   aspeed_set_mmio_exec);
    object_class_property_set_description(oc, "execute-in-place",
                           "boot directly from CE0 flash device");

    object_class_property_add_str(oc, "bmc-console", aspeed_get_bmc_console,
                                  aspeed_set_bmc_console);
    object_class_property_set_description(oc, "bmc-console",
                           "Change the default UART to \"uartX\"");

    object_class_property_add_str(oc, "fmc-model", aspeed_get_fmc_model,
                                   aspeed_set_fmc_model);
    object_class_property_set_description(oc, "fmc-model",
                                          "Change the FMC Flash model");
    object_class_property_add_str(oc, "spi-model", aspeed_get_spi_model,
                                   aspeed_set_spi_model);
    object_class_property_set_description(oc, "spi-model",
                                          "Change the SPI Flash model");
}

void aspeed_machine_class_init_cpus_defaults(MachineClass *mc)
{
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(mc);
    AspeedSoCClass *sc = ASPEED_SOC_CLASS(object_class_by_name(amc->soc_name));

    mc->default_cpus = sc->num_cpus;
    mc->min_cpus = sc->num_cpus;
    mc->max_cpus = sc->num_cpus;
    mc->valid_cpu_types = sc->valid_cpu_types;
}

static bool aspeed_machine_ast2600_get_boot_from_emmc(Object *obj, Error **errp)
{
    AspeedMachineState *bmc = ASPEED_MACHINE(obj);

    return !!(bmc->hw_strap1 & SCU_AST2600_HW_STRAP_BOOT_SRC_EMMC);
}

static void aspeed_machine_ast2600_set_boot_from_emmc(Object *obj, bool value,
                                                      Error **errp)
{
    AspeedMachineState *bmc = ASPEED_MACHINE(obj);

    if (value) {
        bmc->hw_strap1 |= SCU_AST2600_HW_STRAP_BOOT_SRC_EMMC;
    } else {
        bmc->hw_strap1 &= ~SCU_AST2600_HW_STRAP_BOOT_SRC_EMMC;
    }
}

void aspeed_machine_ast2600_class_emmc_init(ObjectClass *oc)
{
    object_class_property_add_bool(oc, "boot-emmc",
                                   aspeed_machine_ast2600_get_boot_from_emmc,
                                   aspeed_machine_ast2600_set_boot_from_emmc);
    object_class_property_set_description(oc, "boot-emmc",
                                          "Set or unset boot from EMMC");
}

static void aspeed_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->init = aspeed_machine_init;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_parallel = 1;
    mc->default_ram_id = "ram";
    amc->macs_mask = ASPEED_MAC0_ON;
    amc->uart_default = ASPEED_DEV_UART5;

    aspeed_machine_class_props_init(oc);
}

static const TypeInfo aspeed_machine_types[] = {
    {
        .name          = TYPE_ASPEED_MACHINE,
        .parent        = TYPE_MACHINE,
        .instance_size = sizeof(AspeedMachineState),
        .instance_init = aspeed_machine_instance_init,
        .class_size    = sizeof(AspeedMachineClass),
        .class_init    = aspeed_machine_class_init,
        .abstract      = true,
    }
};

DEFINE_TYPES(aspeed_machine_types)
