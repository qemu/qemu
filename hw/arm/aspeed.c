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
#include "hw/i2c/i2c_mux_pca954x.h"
#include "hw/i2c/smbus_eeprom.h"
#include "hw/misc/pca9552.h"
#include "hw/sensor/tmp105.h"
#include "hw/misc/led.h"
#include "hw/qdev-properties.h"
#include "sysemu/block-backend.h"
#include "sysemu/reset.h"
#include "hw/loader.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "hw/qdev-clock.h"
#include "sysemu/sysemu.h"

static struct arm_boot_info aspeed_board_binfo = {
    .board_id = -1, /* device-tree-only board */
};

struct AspeedMachineState {
    /* Private */
    MachineState parent_obj;
    /* Public */

    AspeedSoCState soc;
    bool mmio_exec;
    char *fmc_model;
    char *spi_model;
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

/* TODO: Find the actual hardware value */
#define SUPERMICROX11_BMC_HW_STRAP1 (                                   \
        SCU_AST2400_HW_STRAP_DRAM_SIZE(DRAM_SIZE_128MB) |               \
        SCU_AST2400_HW_STRAP_DRAM_CONFIG(2) |                           \
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

/* Sonorapass hardware value: 0xF100D216 */
#define SONORAPASS_BMC_HW_STRAP1 (                                      \
        SCU_AST2500_HW_STRAP_SPI_AUTOFETCH_ENABLE |                     \
        SCU_AST2500_HW_STRAP_GPIO_STRAP_ENABLE |                        \
        SCU_AST2500_HW_STRAP_UART_DEBUG |                               \
        SCU_AST2500_HW_STRAP_RESERVED28 |                               \
        SCU_AST2500_HW_STRAP_DDR4_ENABLE |                              \
        SCU_HW_STRAP_VGA_CLASS_CODE |                                   \
        SCU_HW_STRAP_LPC_RESET_PIN |                                    \
        SCU_HW_STRAP_SPI_MODE(SCU_HW_STRAP_SPI_MASTER) |                \
        SCU_AST2500_HW_STRAP_SET_AXI_AHB_RATIO(AXI_AHB_RATIO_2_1) |     \
        SCU_HW_STRAP_VGA_BIOS_ROM |                                     \
        SCU_HW_STRAP_VGA_SIZE_SET(VGA_16M_DRAM) |                       \
        SCU_AST2500_HW_STRAP_RESERVED1)

#define G220A_BMC_HW_STRAP1 (                                      \
        SCU_AST2500_HW_STRAP_SPI_AUTOFETCH_ENABLE |                     \
        SCU_AST2500_HW_STRAP_GPIO_STRAP_ENABLE |                        \
        SCU_AST2500_HW_STRAP_UART_DEBUG |                               \
        SCU_AST2500_HW_STRAP_RESERVED28 |                               \
        SCU_AST2500_HW_STRAP_DDR4_ENABLE |                              \
        SCU_HW_STRAP_2ND_BOOT_WDT |                                     \
        SCU_HW_STRAP_VGA_CLASS_CODE |                                   \
        SCU_HW_STRAP_LPC_RESET_PIN |                                    \
        SCU_HW_STRAP_SPI_MODE(SCU_HW_STRAP_SPI_MASTER) |                \
        SCU_AST2500_HW_STRAP_SET_AXI_AHB_RATIO(AXI_AHB_RATIO_2_1) |     \
        SCU_HW_STRAP_VGA_SIZE_SET(VGA_64M_DRAM) |                       \
        SCU_AST2500_HW_STRAP_RESERVED1)

/* FP5280G2 hardware value: 0XF100D286 */
#define FP5280G2_BMC_HW_STRAP1 (                                      \
        SCU_AST2500_HW_STRAP_SPI_AUTOFETCH_ENABLE |                     \
        SCU_AST2500_HW_STRAP_GPIO_STRAP_ENABLE |                        \
        SCU_AST2500_HW_STRAP_UART_DEBUG |                               \
        SCU_AST2500_HW_STRAP_RESERVED28 |                               \
        SCU_AST2500_HW_STRAP_DDR4_ENABLE |                              \
        SCU_HW_STRAP_VGA_CLASS_CODE |                                   \
        SCU_HW_STRAP_LPC_RESET_PIN |                                    \
        SCU_HW_STRAP_SPI_MODE(SCU_HW_STRAP_SPI_MASTER) |                \
        SCU_AST2500_HW_STRAP_SET_AXI_AHB_RATIO(AXI_AHB_RATIO_2_1) |     \
        SCU_HW_STRAP_MAC1_RGMII |                                       \
        SCU_HW_STRAP_VGA_SIZE_SET(VGA_16M_DRAM) |                       \
        SCU_AST2500_HW_STRAP_RESERVED1)

/* Witherspoon hardware value: 0xF10AD216 (but use romulus definition) */
#define WITHERSPOON_BMC_HW_STRAP1 ROMULUS_BMC_HW_STRAP1

/* Quanta-Q71l hardware value */
#define QUANTA_Q71L_BMC_HW_STRAP1 (                                     \
        SCU_AST2400_HW_STRAP_DRAM_SIZE(DRAM_SIZE_128MB) |               \
        SCU_AST2400_HW_STRAP_DRAM_CONFIG(2/* DDR3 with CL=6, CWL=5 */) | \
        SCU_AST2400_HW_STRAP_ACPI_DIS |                                 \
        SCU_AST2400_HW_STRAP_SET_CLK_SOURCE(AST2400_CLK_24M_IN) |       \
        SCU_HW_STRAP_VGA_CLASS_CODE |                                   \
        SCU_HW_STRAP_SPI_MODE(SCU_HW_STRAP_SPI_PASS_THROUGH) |          \
        SCU_AST2400_HW_STRAP_SET_CPU_AHB_RATIO(AST2400_CPU_AHB_RATIO_2_1) | \
        SCU_HW_STRAP_SPI_WIDTH |                                        \
        SCU_HW_STRAP_VGA_SIZE_SET(VGA_8M_DRAM) |                        \
        SCU_AST2400_HW_STRAP_BOOT_MODE(AST2400_SPI_BOOT))

/* AST2600 evb hardware value */
#define AST2600_EVB_HW_STRAP1 0x000000C0
#define AST2600_EVB_HW_STRAP2 0x00000003

/* Tacoma hardware value */
#define TACOMA_BMC_HW_STRAP1  0x00000000
#define TACOMA_BMC_HW_STRAP2  0x00000040

/* Rainier hardware value: (QEMU prototype) */
#define RAINIER_BMC_HW_STRAP1 0x00422016
#define RAINIER_BMC_HW_STRAP2 0x80000848

/* Fuji hardware value */
#define FUJI_BMC_HW_STRAP1    0x00000000
#define FUJI_BMC_HW_STRAP2    0x00000000

/* Bletchley hardware value */
/* TODO: Leave same as EVB for now. */
#define BLETCHLEY_BMC_HW_STRAP1 AST2600_EVB_HW_STRAP1
#define BLETCHLEY_BMC_HW_STRAP2 AST2600_EVB_HW_STRAP2

/* Qualcomm DC-SCM hardware value */
#define QCOM_DC_SCM_V1_BMC_HW_STRAP1  0x00000000
#define QCOM_DC_SCM_V1_BMC_HW_STRAP2  0x00000041

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
    static const uint32_t poll_mailbox_ready[] = {
        /*
         * r2 = per-cpu go sign value
         * r1 = AST_SMP_MBOX_FIELD_ENTRY
         * r0 = AST_SMP_MBOX_FIELD_GOSIGN
         */
        0xee100fb0,  /* mrc     p15, 0, r0, c0, c0, 5 */
        0xe21000ff,  /* ands    r0, r0, #255          */
        0xe59f201c,  /* ldr     r2, [pc, #28]         */
        0xe1822000,  /* orr     r2, r2, r0            */

        0xe59f1018,  /* ldr     r1, [pc, #24]         */
        0xe59f0018,  /* ldr     r0, [pc, #24]         */

        0xe320f002,  /* wfe                           */
        0xe5904000,  /* ldr     r4, [r0]              */
        0xe1520004,  /* cmp     r2, r4                */
        0x1afffffb,  /* bne     <wfe>                 */
        0xe591f000,  /* ldr     pc, [r1]              */
        AST_SMP_MBOX_GOSIGN,
        AST_SMP_MBOX_FIELD_ENTRY,
        AST_SMP_MBOX_FIELD_GOSIGN,
    };

    rom_add_blob_fixed("aspeed.smpboot", poll_mailbox_ready,
                       sizeof(poll_mailbox_ready),
                       info->smp_loader_start);
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

#define FIRMWARE_ADDR 0x0

static void write_boot_rom(DriveInfo *dinfo, hwaddr addr, size_t rom_size,
                           Error **errp)
{
    BlockBackend *blk = blk_by_legacy_dinfo(dinfo);
    g_autofree void *storage = NULL;
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

    storage = g_malloc0(rom_size);
    if (blk_pread(blk, 0, rom_size, storage, 0) < 0) {
        error_setg(errp, "failed to read the initial flash content");
        return;
    }

    rom_add_blob_fixed("aspeed.boot_rom", storage, rom_size, addr);
}

void aspeed_board_init_flashes(AspeedSMCState *s, const char *flashtype,
                                      unsigned int count, int unit0)
{
    int i;

    if (!flashtype) {
        return;
    }

    for (i = 0; i < count; ++i) {
        DriveInfo *dinfo = drive_get(IF_MTD, 0, unit0 + i);
        qemu_irq cs_line;
        DeviceState *dev;

        dev = qdev_new(flashtype);
        if (dinfo) {
            qdev_prop_set_drive(dev, "drive", blk_by_legacy_dinfo(dinfo));
        }
        qdev_realize_and_unref(dev, BUS(s->spi), &error_fatal);

        cs_line = qdev_get_gpio_in_named(dev, SSI_GPIO_CS, 0);
        sysbus_connect_irq(SYS_BUS_DEVICE(s), i + 1, cs_line);
    }
}

static void sdhci_attach_drive(SDHCIState *sdhci, DriveInfo *dinfo)
{
        DeviceState *card;

        if (!dinfo) {
            return;
        }
        card = qdev_new(TYPE_SD_CARD);
        qdev_prop_set_drive_err(card, "drive", blk_by_legacy_dinfo(dinfo),
                                &error_fatal);
        qdev_realize_and_unref(card,
                               qdev_get_child_bus(DEVICE(sdhci), "sd-bus"),
                               &error_fatal);
}

static void connect_serial_hds_to_uarts(AspeedMachineState *bmc)
{
    AspeedMachineClass *amc = ASPEED_MACHINE_GET_CLASS(bmc);
    AspeedSoCState *s = &bmc->soc;
    AspeedSoCClass *sc = ASPEED_SOC_GET_CLASS(s);

    aspeed_soc_uart_set_chr(s, amc->uart_default, serial_hd(0));
    for (int i = 1, uart = ASPEED_DEV_UART1; i < sc->uarts_num; i++, uart++) {
        if (uart == amc->uart_default) {
            continue;
        }
        aspeed_soc_uart_set_chr(s, uart, serial_hd(i));
    }
}

static void aspeed_machine_init(MachineState *machine)
{
    AspeedMachineState *bmc = ASPEED_MACHINE(machine);
    AspeedMachineClass *amc = ASPEED_MACHINE_GET_CLASS(machine);
    AspeedSoCClass *sc;
    DriveInfo *drive0 = drive_get(IF_MTD, 0, 0);
    int i;
    NICInfo *nd = &nd_table[0];

    object_initialize_child(OBJECT(machine), "soc", &bmc->soc, amc->soc_name);

    sc = ASPEED_SOC_GET_CLASS(&bmc->soc);

    /*
     * This will error out if the RAM size is not supported by the
     * memory controller of the SoC.
     */
    object_property_set_uint(OBJECT(&bmc->soc), "ram-size", machine->ram_size,
                             &error_fatal);

    for (i = 0; i < sc->macs_num; i++) {
        if ((amc->macs_mask & (1 << i)) && nd->used) {
            qemu_check_nic_model(nd, TYPE_FTGMAC100);
            qdev_set_nic_properties(DEVICE(&bmc->soc.ftgmac100[i]), nd);
            nd++;
        }
    }

    object_property_set_int(OBJECT(&bmc->soc), "hw-strap1", amc->hw_strap1,
                            &error_abort);
    object_property_set_int(OBJECT(&bmc->soc), "hw-strap2", amc->hw_strap2,
                            &error_abort);
    object_property_set_link(OBJECT(&bmc->soc), "memory",
                             OBJECT(get_system_memory()), &error_abort);
    object_property_set_link(OBJECT(&bmc->soc), "dram",
                             OBJECT(machine->ram), &error_abort);
    if (machine->kernel_filename) {
        /*
         * When booting with a -kernel command line there is no u-boot
         * that runs to unlock the SCU. In this case set the default to
         * be unlocked as the kernel expects
         */
        object_property_set_int(OBJECT(&bmc->soc), "hw-prot-key",
                                ASPEED_SCU_PROT_KEY, &error_abort);
    }
    connect_serial_hds_to_uarts(bmc);
    qdev_realize(DEVICE(&bmc->soc), NULL, &error_abort);

    aspeed_board_init_flashes(&bmc->soc.fmc,
                              bmc->fmc_model ? bmc->fmc_model : amc->fmc_model,
                              amc->num_cs, 0);
    aspeed_board_init_flashes(&bmc->soc.spi[0],
                              bmc->spi_model ? bmc->spi_model : amc->spi_model,
                              1, amc->num_cs);

    /* Install first FMC flash content as a boot rom. */
    if (drive0) {
        AspeedSMCFlash *fl = &bmc->soc.fmc.flashes[0];
        MemoryRegion *boot_rom = g_new(MemoryRegion, 1);
        uint64_t size = memory_region_size(&fl->mmio);

        /*
         * create a ROM region using the default mapping window size of
         * the flash module. The window size is 64MB for the AST2400
         * SoC and 128MB for the AST2500 SoC, which is twice as big as
         * needed by the flash modules of the Aspeed machines.
         */
        if (ASPEED_MACHINE(machine)->mmio_exec) {
            memory_region_init_alias(boot_rom, NULL, "aspeed.boot_rom",
                                     &fl->mmio, 0, size);
            memory_region_add_subregion(get_system_memory(), FIRMWARE_ADDR,
                                        boot_rom);
        } else {
            memory_region_init_rom(boot_rom, NULL, "aspeed.boot_rom",
                                   size, &error_abort);
            memory_region_add_subregion(get_system_memory(), FIRMWARE_ADDR,
                                        boot_rom);
            write_boot_rom(drive0, FIRMWARE_ADDR, size, &error_abort);
        }
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

    for (i = 0; i < bmc->soc.sdhci.num_slots; i++) {
        sdhci_attach_drive(&bmc->soc.sdhci.slots[i],
                           drive_get(IF_SD, 0, i));
    }

    if (bmc->soc.emmc.num_slots) {
        sdhci_attach_drive(&bmc->soc.emmc.slots[0],
                           drive_get(IF_SD, 0, bmc->soc.sdhci.num_slots));
    }

    arm_load_kernel(ARM_CPU(first_cpu), machine, &aspeed_board_binfo);
}

static void at24c_eeprom_init(I2CBus *bus, uint8_t addr, uint32_t rsize)
{
    I2CSlave *i2c_dev = i2c_slave_new("at24c-eeprom", addr);
    DeviceState *dev = DEVICE(i2c_dev);

    qdev_prop_set_uint32(dev, "rom-size", rsize);
    i2c_slave_realize_and_unref(i2c_dev, bus, &error_abort);
}

static void palmetto_bmc_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = &bmc->soc;
    DeviceState *dev;
    uint8_t *eeprom_buf = g_malloc0(32 * 1024);

    /* The palmetto platform expects a ds3231 RTC but a ds1338 is
     * enough to provide basic RTC features. Alarms will be missing */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 0), "ds1338", 0x68);

    smbus_eeprom_init_one(aspeed_i2c_get_bus(&soc->i2c, 0), 0x50,
                          eeprom_buf);

    /* add a TMP423 temperature sensor */
    dev = DEVICE(i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 2),
                                         "tmp423", 0x4c));
    object_property_set_int(OBJECT(dev), "temperature0", 31000, &error_abort);
    object_property_set_int(OBJECT(dev), "temperature1", 28000, &error_abort);
    object_property_set_int(OBJECT(dev), "temperature2", 20000, &error_abort);
    object_property_set_int(OBJECT(dev), "temperature3", 110000, &error_abort);
}

static void quanta_q71l_bmc_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = &bmc->soc;

    /*
     * The quanta-q71l platform expects tmp75s which are compatible with
     * tmp105s.
     */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 1), "tmp105", 0x4c);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 1), "tmp105", 0x4e);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 1), "tmp105", 0x4f);

    /* TODO: i2c-1: Add baseboard FRU eeprom@54 24c64 */
    /* TODO: i2c-1: Add Frontpanel FRU eeprom@57 24c64 */
    /* TODO: Add Memory Riser i2c mux and eeproms. */

    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 2), "pca9546", 0x74);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 2), "pca9548", 0x77);

    /* TODO: i2c-3: Add BIOS FRU eeprom@56 24c64 */

    /* i2c-7 */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 7), "pca9546", 0x70);
    /*        - i2c@0: pmbus@59 */
    /*        - i2c@1: pmbus@58 */
    /*        - i2c@2: pmbus@58 */
    /*        - i2c@3: pmbus@59 */

    /* TODO: i2c-7: Add PDB FRU eeprom@52 */
    /* TODO: i2c-8: Add BMC FRU eeprom@50 */
}

static void ast2500_evb_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = &bmc->soc;
    uint8_t *eeprom_buf = g_malloc0(8 * 1024);

    smbus_eeprom_init_one(aspeed_i2c_get_bus(&soc->i2c, 3), 0x50,
                          eeprom_buf);

    /* The AST2500 EVB expects a LM75 but a TMP105 is compatible */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 7),
                     TYPE_TMP105, 0x4d);
}

static void ast2600_evb_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = &bmc->soc;
    uint8_t *eeprom_buf = g_malloc0(8 * 1024);

    smbus_eeprom_init_one(aspeed_i2c_get_bus(&soc->i2c, 7), 0x50,
                          eeprom_buf);

    /* LM75 is compatible with TMP105 driver */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 8),
                     TYPE_TMP105, 0x4d);
}

static void romulus_bmc_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = &bmc->soc;

    /* The romulus board expects Epson RX8900 I2C RTC but a ds1338 is
     * good enough */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 11), "ds1338", 0x32);
}

static void create_pca9552(AspeedSoCState *soc, int bus_id, int addr)
{
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, bus_id),
                            TYPE_PCA9552, addr);
}

static void sonorapass_bmc_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = &bmc->soc;

    /* bus 2 : */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 2), "tmp105", 0x48);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 2), "tmp105", 0x49);
    /* bus 2 : pca9546 @ 0x73 */

    /* bus 3 : pca9548 @ 0x70 */

    /* bus 4 : */
    uint8_t *eeprom4_54 = g_malloc0(8 * 1024);
    smbus_eeprom_init_one(aspeed_i2c_get_bus(&soc->i2c, 4), 0x54,
                          eeprom4_54);
    /* PCA9539 @ 0x76, but PCA9552 is compatible */
    create_pca9552(soc, 4, 0x76);
    /* PCA9539 @ 0x77, but PCA9552 is compatible */
    create_pca9552(soc, 4, 0x77);

    /* bus 6 : */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 6), "tmp105", 0x48);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 6), "tmp105", 0x49);
    /* bus 6 : pca9546 @ 0x73 */

    /* bus 8 : */
    uint8_t *eeprom8_56 = g_malloc0(8 * 1024);
    smbus_eeprom_init_one(aspeed_i2c_get_bus(&soc->i2c, 8), 0x56,
                          eeprom8_56);
    create_pca9552(soc, 8, 0x60);
    create_pca9552(soc, 8, 0x61);
    /* bus 8 : adc128d818 @ 0x1d */
    /* bus 8 : adc128d818 @ 0x1f */

    /*
     * bus 13 : pca9548 @ 0x71
     *      - channel 3:
     *          - tmm421 @ 0x4c
     *          - tmp421 @ 0x4e
     *          - tmp421 @ 0x4f
     */

}

static void witherspoon_bmc_i2c_init(AspeedMachineState *bmc)
{
    static const struct {
        unsigned gpio_id;
        LEDColor color;
        const char *description;
        bool gpio_polarity;
    } pca1_leds[] = {
        {13, LED_COLOR_GREEN, "front-fault-4",  GPIO_POLARITY_ACTIVE_LOW},
        {14, LED_COLOR_GREEN, "front-power-3",  GPIO_POLARITY_ACTIVE_LOW},
        {15, LED_COLOR_GREEN, "front-id-5",     GPIO_POLARITY_ACTIVE_LOW},
    };
    AspeedSoCState *soc = &bmc->soc;
    uint8_t *eeprom_buf = g_malloc0(8 * 1024);
    DeviceState *dev;
    LEDState *led;

    /* Bus 3: TODO bmp280@77 */
    dev = DEVICE(i2c_slave_new(TYPE_PCA9552, 0x60));
    qdev_prop_set_string(dev, "description", "pca1");
    i2c_slave_realize_and_unref(I2C_SLAVE(dev),
                                aspeed_i2c_get_bus(&soc->i2c, 3),
                                &error_fatal);

    for (size_t i = 0; i < ARRAY_SIZE(pca1_leds); i++) {
        led = led_create_simple(OBJECT(bmc),
                                pca1_leds[i].gpio_polarity,
                                pca1_leds[i].color,
                                pca1_leds[i].description);
        qdev_connect_gpio_out(dev, pca1_leds[i].gpio_id,
                              qdev_get_gpio_in(DEVICE(led), 0));
    }
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 3), "dps310", 0x76);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 3), "max31785", 0x52);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 4), "tmp423", 0x4c);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 5), "tmp423", 0x4c);

    /* The Witherspoon expects a TMP275 but a TMP105 is compatible */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 9), TYPE_TMP105,
                     0x4a);

    /* The witherspoon board expects Epson RX8900 I2C RTC but a ds1338 is
     * good enough */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 11), "ds1338", 0x32);

    smbus_eeprom_init_one(aspeed_i2c_get_bus(&soc->i2c, 11), 0x51,
                          eeprom_buf);
    dev = DEVICE(i2c_slave_new(TYPE_PCA9552, 0x60));
    qdev_prop_set_string(dev, "description", "pca0");
    i2c_slave_realize_and_unref(I2C_SLAVE(dev),
                                aspeed_i2c_get_bus(&soc->i2c, 11),
                                &error_fatal);
    /* Bus 11: TODO ucd90160@64 */
}

static void g220a_bmc_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = &bmc->soc;
    DeviceState *dev;

    dev = DEVICE(i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 3),
                                         "emc1413", 0x4c));
    object_property_set_int(OBJECT(dev), "temperature0", 31000, &error_abort);
    object_property_set_int(OBJECT(dev), "temperature1", 28000, &error_abort);
    object_property_set_int(OBJECT(dev), "temperature2", 20000, &error_abort);

    dev = DEVICE(i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 12),
                                         "emc1413", 0x4c));
    object_property_set_int(OBJECT(dev), "temperature0", 31000, &error_abort);
    object_property_set_int(OBJECT(dev), "temperature1", 28000, &error_abort);
    object_property_set_int(OBJECT(dev), "temperature2", 20000, &error_abort);

    dev = DEVICE(i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 13),
                                         "emc1413", 0x4c));
    object_property_set_int(OBJECT(dev), "temperature0", 31000, &error_abort);
    object_property_set_int(OBJECT(dev), "temperature1", 28000, &error_abort);
    object_property_set_int(OBJECT(dev), "temperature2", 20000, &error_abort);

    static uint8_t eeprom_buf[2 * 1024] = {
            0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0xfe,
            0x01, 0x06, 0x00, 0xc9, 0x42, 0x79, 0x74, 0x65,
            0x64, 0x61, 0x6e, 0x63, 0x65, 0xc5, 0x47, 0x32,
            0x32, 0x30, 0x41, 0xc4, 0x41, 0x41, 0x42, 0x42,
            0xc4, 0x43, 0x43, 0x44, 0x44, 0xc4, 0x45, 0x45,
            0x46, 0x46, 0xc4, 0x48, 0x48, 0x47, 0x47, 0xc1,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa7,
    };
    smbus_eeprom_init_one(aspeed_i2c_get_bus(&soc->i2c, 4), 0x57,
                          eeprom_buf);
}

static void aspeed_eeprom_init(I2CBus *bus, uint8_t addr, uint32_t rsize)
{
    I2CSlave *i2c_dev = i2c_slave_new("at24c-eeprom", addr);
    DeviceState *dev = DEVICE(i2c_dev);

    qdev_prop_set_uint32(dev, "rom-size", rsize);
    i2c_slave_realize_and_unref(i2c_dev, bus, &error_abort);
}

static void fp5280g2_bmc_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = &bmc->soc;
    I2CSlave *i2c_mux;

    /* The at24c256 */
    at24c_eeprom_init(aspeed_i2c_get_bus(&soc->i2c, 1), 0x50, 32768);

    /* The fp5280g2 expects a TMP112 but a TMP105 is compatible */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 2), TYPE_TMP105,
                     0x48);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 2), TYPE_TMP105,
                     0x49);

    i2c_mux = i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 2),
                     "pca9546", 0x70);
    /* It expects a TMP112 but a TMP105 is compatible */
    i2c_slave_create_simple(pca954x_i2c_get_bus(i2c_mux, 0), TYPE_TMP105,
                     0x4a);

    /* It expects a ds3232 but a ds1338 is good enough */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 4), "ds1338", 0x68);

    /* It expects a pca9555 but a pca9552 is compatible */
    create_pca9552(soc, 8, 0x30);
}

static void rainier_bmc_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = &bmc->soc;
    I2CSlave *i2c_mux;

    aspeed_eeprom_init(aspeed_i2c_get_bus(&soc->i2c, 0), 0x51, 32 * KiB);

    create_pca9552(soc, 3, 0x61);

    /* The rainier expects a TMP275 but a TMP105 is compatible */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 4), TYPE_TMP105,
                     0x48);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 4), TYPE_TMP105,
                     0x49);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 4), TYPE_TMP105,
                     0x4a);
    i2c_mux = i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 4),
                                      "pca9546", 0x70);
    aspeed_eeprom_init(pca954x_i2c_get_bus(i2c_mux, 0), 0x50, 64 * KiB);
    aspeed_eeprom_init(pca954x_i2c_get_bus(i2c_mux, 1), 0x51, 64 * KiB);
    aspeed_eeprom_init(pca954x_i2c_get_bus(i2c_mux, 2), 0x52, 64 * KiB);
    create_pca9552(soc, 4, 0x60);

    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 5), TYPE_TMP105,
                     0x48);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 5), TYPE_TMP105,
                     0x49);
    create_pca9552(soc, 5, 0x60);
    create_pca9552(soc, 5, 0x61);
    i2c_mux = i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 5),
                                      "pca9546", 0x70);
    aspeed_eeprom_init(pca954x_i2c_get_bus(i2c_mux, 0), 0x50, 64 * KiB);
    aspeed_eeprom_init(pca954x_i2c_get_bus(i2c_mux, 1), 0x51, 64 * KiB);

    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 6), TYPE_TMP105,
                     0x48);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 6), TYPE_TMP105,
                     0x4a);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 6), TYPE_TMP105,
                     0x4b);
    i2c_mux = i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 6),
                                      "pca9546", 0x70);
    aspeed_eeprom_init(pca954x_i2c_get_bus(i2c_mux, 0), 0x50, 64 * KiB);
    aspeed_eeprom_init(pca954x_i2c_get_bus(i2c_mux, 1), 0x51, 64 * KiB);
    aspeed_eeprom_init(pca954x_i2c_get_bus(i2c_mux, 2), 0x50, 64 * KiB);
    aspeed_eeprom_init(pca954x_i2c_get_bus(i2c_mux, 3), 0x51, 64 * KiB);

    create_pca9552(soc, 7, 0x30);
    create_pca9552(soc, 7, 0x31);
    create_pca9552(soc, 7, 0x32);
    create_pca9552(soc, 7, 0x33);
    create_pca9552(soc, 7, 0x60);
    create_pca9552(soc, 7, 0x61);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 7), "dps310", 0x76);
    /* Bus 7: TODO si7021-a20@20 */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 7), TYPE_TMP105,
                     0x48);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 7), "max31785", 0x52);
    aspeed_eeprom_init(aspeed_i2c_get_bus(&soc->i2c, 7), 0x50, 64 * KiB);
    aspeed_eeprom_init(aspeed_i2c_get_bus(&soc->i2c, 7), 0x51, 64 * KiB);

    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 8), TYPE_TMP105,
                     0x48);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 8), TYPE_TMP105,
                     0x4a);
    aspeed_eeprom_init(aspeed_i2c_get_bus(&soc->i2c, 8), 0x50, 64 * KiB);
    aspeed_eeprom_init(aspeed_i2c_get_bus(&soc->i2c, 8), 0x51, 64 * KiB);
    create_pca9552(soc, 8, 0x60);
    create_pca9552(soc, 8, 0x61);
    /* Bus 8: ucd90320@11 */
    /* Bus 8: ucd90320@b */
    /* Bus 8: ucd90320@c */

    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 9), "tmp423", 0x4c);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 9), "tmp423", 0x4d);
    aspeed_eeprom_init(aspeed_i2c_get_bus(&soc->i2c, 9), 0x50, 128 * KiB);

    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 10), "tmp423", 0x4c);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 10), "tmp423", 0x4d);
    aspeed_eeprom_init(aspeed_i2c_get_bus(&soc->i2c, 10), 0x50, 128 * KiB);

    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 11), TYPE_TMP105,
                     0x48);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 11), TYPE_TMP105,
                     0x49);
    i2c_mux = i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 11),
                                      "pca9546", 0x70);
    aspeed_eeprom_init(pca954x_i2c_get_bus(i2c_mux, 0), 0x50, 64 * KiB);
    aspeed_eeprom_init(pca954x_i2c_get_bus(i2c_mux, 1), 0x51, 64 * KiB);
    create_pca9552(soc, 11, 0x60);


    aspeed_eeprom_init(aspeed_i2c_get_bus(&soc->i2c, 13), 0x50, 64 * KiB);
    create_pca9552(soc, 13, 0x60);

    aspeed_eeprom_init(aspeed_i2c_get_bus(&soc->i2c, 14), 0x50, 64 * KiB);
    create_pca9552(soc, 14, 0x60);

    aspeed_eeprom_init(aspeed_i2c_get_bus(&soc->i2c, 15), 0x50, 64 * KiB);
    create_pca9552(soc, 15, 0x60);
}

static void get_pca9548_channels(I2CBus *bus, uint8_t mux_addr,
                                 I2CBus **channels)
{
    I2CSlave *mux = i2c_slave_create_simple(bus, "pca9548", mux_addr);
    for (int i = 0; i < 8; i++) {
        channels[i] = pca954x_i2c_get_bus(mux, i);
    }
}

#define TYPE_LM75 TYPE_TMP105
#define TYPE_TMP75 TYPE_TMP105
#define TYPE_TMP422 "tmp422"

static void fuji_bmc_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = &bmc->soc;
    I2CBus *i2c[144] = {};

    for (int i = 0; i < 16; i++) {
        i2c[i] = aspeed_i2c_get_bus(&soc->i2c, i);
    }
    I2CBus *i2c180 = i2c[2];
    I2CBus *i2c480 = i2c[8];
    I2CBus *i2c600 = i2c[11];

    get_pca9548_channels(i2c180, 0x70, &i2c[16]);
    get_pca9548_channels(i2c480, 0x70, &i2c[24]);
    /* NOTE: The device tree skips [32, 40) in the alias numbering */
    get_pca9548_channels(i2c600, 0x77, &i2c[40]);
    get_pca9548_channels(i2c[24], 0x71, &i2c[48]);
    get_pca9548_channels(i2c[25], 0x72, &i2c[56]);
    get_pca9548_channels(i2c[26], 0x76, &i2c[64]);
    get_pca9548_channels(i2c[27], 0x76, &i2c[72]);
    for (int i = 0; i < 8; i++) {
        get_pca9548_channels(i2c[40 + i], 0x76, &i2c[80 + i * 8]);
    }

    i2c_slave_create_simple(i2c[17], TYPE_LM75, 0x4c);
    i2c_slave_create_simple(i2c[17], TYPE_LM75, 0x4d);

    aspeed_eeprom_init(i2c[19], 0x52, 64 * KiB);
    aspeed_eeprom_init(i2c[20], 0x50, 2 * KiB);
    aspeed_eeprom_init(i2c[22], 0x52, 2 * KiB);

    i2c_slave_create_simple(i2c[3], TYPE_LM75, 0x48);
    i2c_slave_create_simple(i2c[3], TYPE_LM75, 0x49);
    i2c_slave_create_simple(i2c[3], TYPE_LM75, 0x4a);
    i2c_slave_create_simple(i2c[3], TYPE_TMP422, 0x4c);

    aspeed_eeprom_init(i2c[8], 0x51, 64 * KiB);
    i2c_slave_create_simple(i2c[8], TYPE_LM75, 0x4a);

    i2c_slave_create_simple(i2c[50], TYPE_LM75, 0x4c);
    aspeed_eeprom_init(i2c[50], 0x52, 64 * KiB);
    i2c_slave_create_simple(i2c[51], TYPE_TMP75, 0x48);
    i2c_slave_create_simple(i2c[52], TYPE_TMP75, 0x49);

    i2c_slave_create_simple(i2c[59], TYPE_TMP75, 0x48);
    i2c_slave_create_simple(i2c[60], TYPE_TMP75, 0x49);

    aspeed_eeprom_init(i2c[65], 0x53, 64 * KiB);
    i2c_slave_create_simple(i2c[66], TYPE_TMP75, 0x49);
    i2c_slave_create_simple(i2c[66], TYPE_TMP75, 0x48);
    aspeed_eeprom_init(i2c[68], 0x52, 64 * KiB);
    aspeed_eeprom_init(i2c[69], 0x52, 64 * KiB);
    aspeed_eeprom_init(i2c[70], 0x52, 64 * KiB);
    aspeed_eeprom_init(i2c[71], 0x52, 64 * KiB);

    aspeed_eeprom_init(i2c[73], 0x53, 64 * KiB);
    i2c_slave_create_simple(i2c[74], TYPE_TMP75, 0x49);
    i2c_slave_create_simple(i2c[74], TYPE_TMP75, 0x48);
    aspeed_eeprom_init(i2c[76], 0x52, 64 * KiB);
    aspeed_eeprom_init(i2c[77], 0x52, 64 * KiB);
    aspeed_eeprom_init(i2c[78], 0x52, 64 * KiB);
    aspeed_eeprom_init(i2c[79], 0x52, 64 * KiB);
    aspeed_eeprom_init(i2c[28], 0x50, 2 * KiB);

    for (int i = 0; i < 8; i++) {
        aspeed_eeprom_init(i2c[81 + i * 8], 0x56, 64 * KiB);
        i2c_slave_create_simple(i2c[82 + i * 8], TYPE_TMP75, 0x48);
        i2c_slave_create_simple(i2c[83 + i * 8], TYPE_TMP75, 0x4b);
        i2c_slave_create_simple(i2c[84 + i * 8], TYPE_TMP75, 0x4a);
    }
}

#define TYPE_TMP421 "tmp421"

static void bletchley_bmc_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = &bmc->soc;
    I2CBus *i2c[13] = {};
    for (int i = 0; i < 13; i++) {
        if ((i == 8) || (i == 11)) {
            continue;
        }
        i2c[i] = aspeed_i2c_get_bus(&soc->i2c, i);
    }

    /* Bus 0 - 5 all have the same config. */
    for (int i = 0; i < 6; i++) {
        /* Missing model: ti,ina230 @ 0x45 */
        /* Missing model: mps,mp5023 @ 0x40 */
        i2c_slave_create_simple(i2c[i], TYPE_TMP421, 0x4f);
        /* Missing model: nxp,pca9539 @ 0x76, but PCA9552 works enough */
        i2c_slave_create_simple(i2c[i], TYPE_PCA9552, 0x76);
        i2c_slave_create_simple(i2c[i], TYPE_PCA9552, 0x67);
        /* Missing model: fsc,fusb302 @ 0x22 */
    }

    /* Bus 6 */
    at24c_eeprom_init(i2c[6], 0x56, 65536);
    /* Missing model: nxp,pcf85263 @ 0x51 , but ds1338 works enough */
    i2c_slave_create_simple(i2c[6], "ds1338", 0x51);


    /* Bus 7 */
    at24c_eeprom_init(i2c[7], 0x54, 65536);

    /* Bus 9 */
    i2c_slave_create_simple(i2c[9], TYPE_TMP421, 0x4f);

    /* Bus 10 */
    i2c_slave_create_simple(i2c[10], TYPE_TMP421, 0x4f);
    /* Missing model: ti,hdc1080 @ 0x40 */
    i2c_slave_create_simple(i2c[10], TYPE_PCA9552, 0x67);

    /* Bus 12 */
    /* Missing model: adi,adm1278 @ 0x11 */
    i2c_slave_create_simple(i2c[12], TYPE_TMP421, 0x4c);
    i2c_slave_create_simple(i2c[12], TYPE_TMP421, 0x4d);
    i2c_slave_create_simple(i2c[12], TYPE_PCA9552, 0x67);
}

static void fby35_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = &bmc->soc;
    I2CBus *i2c[16];

    for (int i = 0; i < 16; i++) {
        i2c[i] = aspeed_i2c_get_bus(&soc->i2c, i);
    }

    i2c_slave_create_simple(i2c[2], TYPE_LM75, 0x4f);
    i2c_slave_create_simple(i2c[8], TYPE_TMP421, 0x1f);
    /* Hotswap controller is actually supposed to be mp5920 or ltc4282. */
    i2c_slave_create_simple(i2c[11], "adm1272", 0x44);
    i2c_slave_create_simple(i2c[12], TYPE_LM75, 0x4e);
    i2c_slave_create_simple(i2c[12], TYPE_LM75, 0x4f);

    aspeed_eeprom_init(i2c[4], 0x51, 128 * KiB);
    aspeed_eeprom_init(i2c[6], 0x51, 128 * KiB);
    aspeed_eeprom_init(i2c[8], 0x50, 32 * KiB);
    aspeed_eeprom_init(i2c[11], 0x51, 128 * KiB);
    aspeed_eeprom_init(i2c[11], 0x54, 128 * KiB);

    /*
     * TODO: There is a multi-master i2c connection to an AST1030 MiniBMC on
     * buses 0, 1, 2, 3, and 9. Source address 0x10, target address 0x20 on
     * each.
     */
}

static void qcom_dc_scm_bmc_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = &bmc->soc;

    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 15), "tmp105", 0x4d);
}

static void qcom_dc_scm_firework_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = &bmc->soc;
    I2CSlave *therm_mux, *cpuvr_mux;

    /* Create the generic DC-SCM hardware */
    qcom_dc_scm_bmc_i2c_init(bmc);

    /* Now create the Firework specific hardware */

    /* I2C7 CPUVR MUX */
    cpuvr_mux = i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 7),
                                        "pca9546", 0x70);
    i2c_slave_create_simple(pca954x_i2c_get_bus(cpuvr_mux, 0), "pca9548", 0x72);
    i2c_slave_create_simple(pca954x_i2c_get_bus(cpuvr_mux, 1), "pca9548", 0x72);
    i2c_slave_create_simple(pca954x_i2c_get_bus(cpuvr_mux, 2), "pca9548", 0x72);
    i2c_slave_create_simple(pca954x_i2c_get_bus(cpuvr_mux, 3), "pca9548", 0x72);

    /* I2C8 Thermal Diodes*/
    therm_mux = i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 8),
                                        "pca9548", 0x70);
    i2c_slave_create_simple(pca954x_i2c_get_bus(therm_mux, 0), TYPE_LM75, 0x4C);
    i2c_slave_create_simple(pca954x_i2c_get_bus(therm_mux, 1), TYPE_LM75, 0x4C);
    i2c_slave_create_simple(pca954x_i2c_get_bus(therm_mux, 2), TYPE_LM75, 0x48);
    i2c_slave_create_simple(pca954x_i2c_get_bus(therm_mux, 3), TYPE_LM75, 0x48);
    i2c_slave_create_simple(pca954x_i2c_get_bus(therm_mux, 4), TYPE_LM75, 0x48);

    /* I2C9 Fan Controller (MAX31785) */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 9), "max31785", 0x52);
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 9), "max31785", 0x54);
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
    ASPEED_MACHINE(obj)->mmio_exec = false;
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

static void aspeed_machine_class_props_init(ObjectClass *oc)
{
    object_class_property_add_bool(oc, "execute-in-place",
                                   aspeed_get_mmio_exec,
                                   aspeed_set_mmio_exec);
    object_class_property_set_description(oc, "execute-in-place",
                           "boot directly from CE0 flash device");

    object_class_property_add_str(oc, "fmc-model", aspeed_get_fmc_model,
                                   aspeed_set_fmc_model);
    object_class_property_set_description(oc, "fmc-model",
                                          "Change the FMC Flash model");
    object_class_property_add_str(oc, "spi-model", aspeed_get_spi_model,
                                   aspeed_set_spi_model);
    object_class_property_set_description(oc, "spi-model",
                                          "Change the SPI Flash model");
}

static int aspeed_soc_num_cpus(const char *soc_name)
{
   AspeedSoCClass *sc = ASPEED_SOC_CLASS(object_class_by_name(soc_name));
   return sc->num_cpus;
}

static void aspeed_machine_class_init(ObjectClass *oc, void *data)
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

static void aspeed_machine_palmetto_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "OpenPOWER Palmetto BMC (ARM926EJ-S)";
    amc->soc_name  = "ast2400-a1";
    amc->hw_strap1 = PALMETTO_BMC_HW_STRAP1;
    amc->fmc_model = "n25q256a";
    amc->spi_model = "mx25l25635f";
    amc->num_cs    = 1;
    amc->i2c_init  = palmetto_bmc_i2c_init;
    mc->default_ram_size       = 256 * MiB;
    mc->default_cpus = mc->min_cpus = mc->max_cpus =
        aspeed_soc_num_cpus(amc->soc_name);
};

static void aspeed_machine_quanta_q71l_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "Quanta-Q71l BMC (ARM926EJ-S)";
    amc->soc_name  = "ast2400-a1";
    amc->hw_strap1 = QUANTA_Q71L_BMC_HW_STRAP1;
    amc->fmc_model = "n25q256a";
    amc->spi_model = "mx25l25635e";
    amc->num_cs    = 1;
    amc->i2c_init  = quanta_q71l_bmc_i2c_init;
    mc->default_ram_size       = 128 * MiB;
    mc->default_cpus = mc->min_cpus = mc->max_cpus =
        aspeed_soc_num_cpus(amc->soc_name);
}

static void aspeed_machine_supermicrox11_bmc_class_init(ObjectClass *oc,
                                                        void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "Supermicro X11 BMC (ARM926EJ-S)";
    amc->soc_name  = "ast2400-a1";
    amc->hw_strap1 = SUPERMICROX11_BMC_HW_STRAP1;
    amc->fmc_model = "mx25l25635e";
    amc->spi_model = "mx25l25635e";
    amc->num_cs    = 1;
    amc->macs_mask = ASPEED_MAC0_ON | ASPEED_MAC1_ON;
    amc->i2c_init  = palmetto_bmc_i2c_init;
    mc->default_ram_size = 256 * MiB;
}

static void aspeed_machine_ast2500_evb_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "Aspeed AST2500 EVB (ARM1176)";
    amc->soc_name  = "ast2500-a1";
    amc->hw_strap1 = AST2500_EVB_HW_STRAP1;
    amc->fmc_model = "mx25l25635e";
    amc->spi_model = "mx25l25635f";
    amc->num_cs    = 1;
    amc->i2c_init  = ast2500_evb_i2c_init;
    mc->default_ram_size       = 512 * MiB;
    mc->default_cpus = mc->min_cpus = mc->max_cpus =
        aspeed_soc_num_cpus(amc->soc_name);
};

static void aspeed_machine_romulus_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "OpenPOWER Romulus BMC (ARM1176)";
    amc->soc_name  = "ast2500-a1";
    amc->hw_strap1 = ROMULUS_BMC_HW_STRAP1;
    amc->fmc_model = "n25q256a";
    amc->spi_model = "mx66l1g45g";
    amc->num_cs    = 2;
    amc->i2c_init  = romulus_bmc_i2c_init;
    mc->default_ram_size       = 512 * MiB;
    mc->default_cpus = mc->min_cpus = mc->max_cpus =
        aspeed_soc_num_cpus(amc->soc_name);
};

static void aspeed_machine_sonorapass_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "OCP SonoraPass BMC (ARM1176)";
    amc->soc_name  = "ast2500-a1";
    amc->hw_strap1 = SONORAPASS_BMC_HW_STRAP1;
    amc->fmc_model = "mx66l1g45g";
    amc->spi_model = "mx66l1g45g";
    amc->num_cs    = 2;
    amc->i2c_init  = sonorapass_bmc_i2c_init;
    mc->default_ram_size       = 512 * MiB;
    mc->default_cpus = mc->min_cpus = mc->max_cpus =
        aspeed_soc_num_cpus(amc->soc_name);
};

static void aspeed_machine_witherspoon_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "OpenPOWER Witherspoon BMC (ARM1176)";
    amc->soc_name  = "ast2500-a1";
    amc->hw_strap1 = WITHERSPOON_BMC_HW_STRAP1;
    amc->fmc_model = "mx25l25635f";
    amc->spi_model = "mx66l1g45g";
    amc->num_cs    = 2;
    amc->i2c_init  = witherspoon_bmc_i2c_init;
    mc->default_ram_size = 512 * MiB;
    mc->default_cpus = mc->min_cpus = mc->max_cpus =
        aspeed_soc_num_cpus(amc->soc_name);
};

static void aspeed_machine_ast2600_evb_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "Aspeed AST2600 EVB (Cortex-A7)";
    amc->soc_name  = "ast2600-a3";
    amc->hw_strap1 = AST2600_EVB_HW_STRAP1;
    amc->hw_strap2 = AST2600_EVB_HW_STRAP2;
    amc->fmc_model = "mx66u51235f";
    amc->spi_model = "mx66u51235f";
    amc->num_cs    = 1;
    amc->macs_mask = ASPEED_MAC0_ON | ASPEED_MAC1_ON | ASPEED_MAC2_ON |
                     ASPEED_MAC3_ON;
    amc->i2c_init  = ast2600_evb_i2c_init;
    mc->default_ram_size = 1 * GiB;
    mc->default_cpus = mc->min_cpus = mc->max_cpus =
        aspeed_soc_num_cpus(amc->soc_name);
};

static void aspeed_machine_tacoma_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "OpenPOWER Tacoma BMC (Cortex-A7)";
    amc->soc_name  = "ast2600-a3";
    amc->hw_strap1 = TACOMA_BMC_HW_STRAP1;
    amc->hw_strap2 = TACOMA_BMC_HW_STRAP2;
    amc->fmc_model = "mx66l1g45g";
    amc->spi_model = "mx66l1g45g";
    amc->num_cs    = 2;
    amc->macs_mask  = ASPEED_MAC2_ON;
    amc->i2c_init  = witherspoon_bmc_i2c_init; /* Same board layout */
    mc->default_ram_size = 1 * GiB;
    mc->default_cpus = mc->min_cpus = mc->max_cpus =
        aspeed_soc_num_cpus(amc->soc_name);
};

static void aspeed_machine_g220a_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "Bytedance G220A BMC (ARM1176)";
    amc->soc_name  = "ast2500-a1";
    amc->hw_strap1 = G220A_BMC_HW_STRAP1;
    amc->fmc_model = "n25q512a";
    amc->spi_model = "mx25l25635e";
    amc->num_cs    = 2;
    amc->macs_mask  = ASPEED_MAC0_ON | ASPEED_MAC1_ON;
    amc->i2c_init  = g220a_bmc_i2c_init;
    mc->default_ram_size = 1024 * MiB;
    mc->default_cpus = mc->min_cpus = mc->max_cpus =
        aspeed_soc_num_cpus(amc->soc_name);
};

static void aspeed_machine_fp5280g2_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "Inspur FP5280G2 BMC (ARM1176)";
    amc->soc_name  = "ast2500-a1";
    amc->hw_strap1 = FP5280G2_BMC_HW_STRAP1;
    amc->fmc_model = "n25q512a";
    amc->spi_model = "mx25l25635e";
    amc->num_cs    = 2;
    amc->macs_mask  = ASPEED_MAC0_ON | ASPEED_MAC1_ON;
    amc->i2c_init  = fp5280g2_bmc_i2c_init;
    mc->default_ram_size = 512 * MiB;
    mc->default_cpus = mc->min_cpus = mc->max_cpus =
        aspeed_soc_num_cpus(amc->soc_name);
};

static void aspeed_machine_rainier_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "IBM Rainier BMC (Cortex-A7)";
    amc->soc_name  = "ast2600-a3";
    amc->hw_strap1 = RAINIER_BMC_HW_STRAP1;
    amc->hw_strap2 = RAINIER_BMC_HW_STRAP2;
    amc->fmc_model = "mx66l1g45g";
    amc->spi_model = "mx66l1g45g";
    amc->num_cs    = 2;
    amc->macs_mask  = ASPEED_MAC2_ON | ASPEED_MAC3_ON;
    amc->i2c_init  = rainier_bmc_i2c_init;
    mc->default_ram_size = 1 * GiB;
    mc->default_cpus = mc->min_cpus = mc->max_cpus =
        aspeed_soc_num_cpus(amc->soc_name);
};

/* On 32-bit hosts, lower RAM to 1G because of the 2047 MB limit */
#if HOST_LONG_BITS == 32
#define FUJI_BMC_RAM_SIZE (1 * GiB)
#else
#define FUJI_BMC_RAM_SIZE (2 * GiB)
#endif

static void aspeed_machine_fuji_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc = "Facebook Fuji BMC (Cortex-A7)";
    amc->soc_name = "ast2600-a3";
    amc->hw_strap1 = FUJI_BMC_HW_STRAP1;
    amc->hw_strap2 = FUJI_BMC_HW_STRAP2;
    amc->fmc_model = "mx66l1g45g";
    amc->spi_model = "mx66l1g45g";
    amc->num_cs = 2;
    amc->macs_mask = ASPEED_MAC3_ON;
    amc->i2c_init = fuji_bmc_i2c_init;
    amc->uart_default = ASPEED_DEV_UART1;
    mc->default_ram_size = FUJI_BMC_RAM_SIZE;
    mc->default_cpus = mc->min_cpus = mc->max_cpus =
        aspeed_soc_num_cpus(amc->soc_name);
};

/* On 32-bit hosts, lower RAM to 1G because of the 2047 MB limit */
#if HOST_LONG_BITS == 32
#define BLETCHLEY_BMC_RAM_SIZE (1 * GiB)
#else
#define BLETCHLEY_BMC_RAM_SIZE (2 * GiB)
#endif

static void aspeed_machine_bletchley_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "Facebook Bletchley BMC (Cortex-A7)";
    amc->soc_name  = "ast2600-a3";
    amc->hw_strap1 = BLETCHLEY_BMC_HW_STRAP1;
    amc->hw_strap2 = BLETCHLEY_BMC_HW_STRAP2;
    amc->fmc_model = "w25q01jvq";
    amc->spi_model = NULL;
    amc->num_cs    = 2;
    amc->macs_mask = ASPEED_MAC2_ON;
    amc->i2c_init  = bletchley_bmc_i2c_init;
    mc->default_ram_size = BLETCHLEY_BMC_RAM_SIZE;
    mc->default_cpus = mc->min_cpus = mc->max_cpus =
        aspeed_soc_num_cpus(amc->soc_name);
}

static void fby35_reset(MachineState *state, ShutdownCause reason)
{
    AspeedMachineState *bmc = ASPEED_MACHINE(state);
    AspeedGPIOState *gpio = &bmc->soc.gpio;

    qemu_devices_reset(reason);

    /* Board ID: 7 (Class-1, 4 slots) */
    object_property_set_bool(OBJECT(gpio), "gpioV4", true, &error_fatal);
    object_property_set_bool(OBJECT(gpio), "gpioV5", true, &error_fatal);
    object_property_set_bool(OBJECT(gpio), "gpioV6", true, &error_fatal);
    object_property_set_bool(OBJECT(gpio), "gpioV7", false, &error_fatal);

    /* Slot presence pins, inverse polarity. (False means present) */
    object_property_set_bool(OBJECT(gpio), "gpioH4", false, &error_fatal);
    object_property_set_bool(OBJECT(gpio), "gpioH5", true, &error_fatal);
    object_property_set_bool(OBJECT(gpio), "gpioH6", true, &error_fatal);
    object_property_set_bool(OBJECT(gpio), "gpioH7", true, &error_fatal);

    /* Slot 12v power pins, normal polarity. (True means powered-on) */
    object_property_set_bool(OBJECT(gpio), "gpioB2", true, &error_fatal);
    object_property_set_bool(OBJECT(gpio), "gpioB3", false, &error_fatal);
    object_property_set_bool(OBJECT(gpio), "gpioB4", false, &error_fatal);
    object_property_set_bool(OBJECT(gpio), "gpioB5", false, &error_fatal);
}

static void aspeed_machine_fby35_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "Facebook fby35 BMC (Cortex-A7)";
    mc->reset      = fby35_reset;
    amc->fmc_model = "mx66l1g45g";
    amc->num_cs    = 2;
    amc->macs_mask = ASPEED_MAC3_ON;
    amc->i2c_init  = fby35_i2c_init;
    /* FIXME: Replace this macro with something more general */
    mc->default_ram_size = FUJI_BMC_RAM_SIZE;
}

#define AST1030_INTERNAL_FLASH_SIZE (1024 * 1024)
/* Main SYSCLK frequency in Hz (200MHz) */
#define SYSCLK_FRQ 200000000ULL

static void aspeed_minibmc_machine_init(MachineState *machine)
{
    AspeedMachineState *bmc = ASPEED_MACHINE(machine);
    AspeedMachineClass *amc = ASPEED_MACHINE_GET_CLASS(machine);
    Clock *sysclk;

    sysclk = clock_new(OBJECT(machine), "SYSCLK");
    clock_set_hz(sysclk, SYSCLK_FRQ);

    object_initialize_child(OBJECT(machine), "soc", &bmc->soc, amc->soc_name);
    qdev_connect_clock_in(DEVICE(&bmc->soc), "sysclk", sysclk);

    object_property_set_link(OBJECT(&bmc->soc), "memory",
                             OBJECT(get_system_memory()), &error_abort);
    connect_serial_hds_to_uarts(bmc);
    qdev_realize(DEVICE(&bmc->soc), NULL, &error_abort);

    aspeed_board_init_flashes(&bmc->soc.fmc,
                              bmc->fmc_model ? bmc->fmc_model : amc->fmc_model,
                              amc->num_cs,
                              0);

    aspeed_board_init_flashes(&bmc->soc.spi[0],
                              bmc->spi_model ? bmc->spi_model : amc->spi_model,
                              amc->num_cs, amc->num_cs);

    aspeed_board_init_flashes(&bmc->soc.spi[1],
                              bmc->spi_model ? bmc->spi_model : amc->spi_model,
                              amc->num_cs, (amc->num_cs * 2));

    if (amc->i2c_init) {
        amc->i2c_init(bmc);
    }

    armv7m_load_kernel(ARM_CPU(first_cpu),
                       machine->kernel_filename,
                       0,
                       AST1030_INTERNAL_FLASH_SIZE);
}

static void ast1030_evb_i2c_init(AspeedMachineState *bmc)
{
    AspeedSoCState *soc = &bmc->soc;

    /* U10 24C08 connects to SDA/SCL Groupt 1 by default */
    uint8_t *eeprom_buf = g_malloc0(32 * 1024);
    smbus_eeprom_init_one(aspeed_i2c_get_bus(&soc->i2c, 0), 0x50, eeprom_buf);

    /* U11 LM75 connects to SDA/SCL Group 2 by default */
    i2c_slave_create_simple(aspeed_i2c_get_bus(&soc->i2c, 1), "tmp105", 0x4d);
}

static void aspeed_minibmc_machine_ast1030_evb_class_init(ObjectClass *oc,
                                                          void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc = "Aspeed AST1030 MiniBMC (Cortex-M4)";
    amc->soc_name = "ast1030-a1";
    amc->hw_strap1 = 0;
    amc->hw_strap2 = 0;
    mc->init = aspeed_minibmc_machine_init;
    amc->i2c_init = ast1030_evb_i2c_init;
    mc->default_ram_size = 0;
    mc->default_cpus = mc->min_cpus = mc->max_cpus = 1;
    amc->fmc_model = "sst25vf032b";
    amc->spi_model = "sst25vf032b";
    amc->num_cs = 2;
    amc->macs_mask = 0;
}

static void aspeed_machine_qcom_dc_scm_v1_class_init(ObjectClass *oc,
                                                     void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "Qualcomm DC-SCM V1 BMC (Cortex A7)";
    amc->soc_name  = "ast2600-a3";
    amc->hw_strap1 = QCOM_DC_SCM_V1_BMC_HW_STRAP1;
    amc->hw_strap2 = QCOM_DC_SCM_V1_BMC_HW_STRAP2;
    amc->fmc_model = "n25q512a";
    amc->spi_model = "n25q512a";
    amc->num_cs    = 2;
    amc->macs_mask = ASPEED_MAC2_ON | ASPEED_MAC3_ON;
    amc->i2c_init  = qcom_dc_scm_bmc_i2c_init;
    mc->default_ram_size = 1 * GiB;
    mc->default_cpus = mc->min_cpus = mc->max_cpus =
        aspeed_soc_num_cpus(amc->soc_name);
};

static void aspeed_machine_qcom_firework_class_init(ObjectClass *oc,
                                                    void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    AspeedMachineClass *amc = ASPEED_MACHINE_CLASS(oc);

    mc->desc       = "Qualcomm DC-SCM V1/Firework BMC (Cortex A7)";
    amc->soc_name  = "ast2600-a3";
    amc->hw_strap1 = QCOM_DC_SCM_V1_BMC_HW_STRAP1;
    amc->hw_strap2 = QCOM_DC_SCM_V1_BMC_HW_STRAP2;
    amc->fmc_model = "n25q512a";
    amc->spi_model = "n25q512a";
    amc->num_cs    = 2;
    amc->macs_mask = ASPEED_MAC2_ON | ASPEED_MAC3_ON;
    amc->i2c_init  = qcom_dc_scm_firework_i2c_init;
    mc->default_ram_size = 1 * GiB;
    mc->default_cpus = mc->min_cpus = mc->max_cpus =
        aspeed_soc_num_cpus(amc->soc_name);
};

static const TypeInfo aspeed_machine_types[] = {
    {
        .name          = MACHINE_TYPE_NAME("palmetto-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_palmetto_class_init,
    }, {
        .name          = MACHINE_TYPE_NAME("supermicrox11-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_supermicrox11_bmc_class_init,
    }, {
        .name          = MACHINE_TYPE_NAME("ast2500-evb"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_ast2500_evb_class_init,
    }, {
        .name          = MACHINE_TYPE_NAME("romulus-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_romulus_class_init,
    }, {
        .name          = MACHINE_TYPE_NAME("sonorapass-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_sonorapass_class_init,
    }, {
        .name          = MACHINE_TYPE_NAME("witherspoon-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_witherspoon_class_init,
    }, {
        .name          = MACHINE_TYPE_NAME("ast2600-evb"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_ast2600_evb_class_init,
    }, {
        .name          = MACHINE_TYPE_NAME("tacoma-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_tacoma_class_init,
    }, {
        .name          = MACHINE_TYPE_NAME("g220a-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_g220a_class_init,
    }, {
        .name          = MACHINE_TYPE_NAME("qcom-dc-scm-v1-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_qcom_dc_scm_v1_class_init,
    }, {
        .name          = MACHINE_TYPE_NAME("qcom-firework-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_qcom_firework_class_init,
    }, {
        .name          = MACHINE_TYPE_NAME("fp5280g2-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_fp5280g2_class_init,
    }, {
        .name          = MACHINE_TYPE_NAME("quanta-q71l-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_quanta_q71l_class_init,
    }, {
        .name          = MACHINE_TYPE_NAME("rainier-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_rainier_class_init,
    }, {
        .name          = MACHINE_TYPE_NAME("fuji-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_fuji_class_init,
    }, {
        .name          = MACHINE_TYPE_NAME("bletchley-bmc"),
        .parent        = TYPE_ASPEED_MACHINE,
        .class_init    = aspeed_machine_bletchley_class_init,
    }, {
        .name          = MACHINE_TYPE_NAME("fby35-bmc"),
        .parent        = MACHINE_TYPE_NAME("ast2600-evb"),
        .class_init    = aspeed_machine_fby35_class_init,
    }, {
        .name           = MACHINE_TYPE_NAME("ast1030-evb"),
        .parent         = TYPE_ASPEED_MACHINE,
        .class_init     = aspeed_minibmc_machine_ast1030_evb_class_init,
    }, {
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
