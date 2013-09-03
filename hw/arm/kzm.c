/*
 * KZM Board System emulation.
 *
 * Copyright (c) 2008 OKL and 2011 NICTA
 * Written by Hans at OK-Labs
 * Updated by Peter Chubb.
 *
 * This code is licensed under the GPL, version 2 or later.
 * See the file `COPYING' in the top level directory.
 *
 * It (partially) emulates a Kyoto Microcomputer
 * KZM-ARM11-01 evaluation board, with a Freescale
 * i.MX31 SoC
 */

#include "hw/sysbus.h"
#include "exec/address-spaces.h"
#include "hw/hw.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"
#include "net/net.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/char/serial.h"
#include "hw/arm/imx.h"

    /* Memory map for Kzm Emulation Baseboard:
     * 0x00000000-0x00003fff 16k secure ROM       IGNORED
     * 0x00004000-0x00407fff Reserved             IGNORED
     * 0x00404000-0x00407fff ROM                  IGNORED
     * 0x00408000-0x0fffffff Reserved             IGNORED
     * 0x10000000-0x1fffbfff RAM aliasing         IGNORED
     * 0x1fffc000-0x1fffffff RAM                  EMULATED
     * 0x20000000-0x2fffffff Reserved             IGNORED
     * 0x30000000-0x7fffffff I.MX31 Internal Register Space
     *   0x43f00000 IO_AREA0
     *   0x43f90000 UART1                         EMULATED
     *   0x43f94000 UART2                         EMULATED
     *   0x68000000 AVIC                          EMULATED
     *   0x53f80000 CCM                           EMULATED
     *   0x53f94000 PIT 1                         EMULATED
     *   0x53f98000 PIT 2                         EMULATED
     *   0x53f90000 GPT                           EMULATED
     * 0x80000000-0x87ffffff RAM                  EMULATED
     * 0x88000000-0x8fffffff RAM Aliasing         EMULATED
     * 0xa0000000-0xafffffff NAND Flash           IGNORED
     * 0xb0000000-0xb3ffffff Unavailable          IGNORED
     * 0xb4000000-0xb4000fff 8-bit free space     IGNORED
     * 0xb4001000-0xb400100f Board control        IGNORED
     *  0xb4001003           DIP switch
     * 0xb4001010-0xb400101f 7-segment LED        IGNORED
     * 0xb4001020-0xb400102f LED                  IGNORED
     * 0xb4001030-0xb400103f LED                  IGNORED
     * 0xb4001040-0xb400104f FPGA, UART           EMULATED
     * 0xb4001050-0xb400105f FPGA, UART           EMULATED
     * 0xb4001060-0xb40fffff FPGA                 IGNORED
     * 0xb6000000-0xb61fffff LAN controller       EMULATED
     * 0xb6200000-0xb62fffff FPGA NAND Controller IGNORED
     * 0xb6300000-0xb7ffffff Free                 IGNORED
     * 0xb8000000-0xb8004fff Memory control registers IGNORED
     * 0xc0000000-0xc3ffffff PCMCIA/CF            IGNORED
     * 0xc4000000-0xffffffff Reserved             IGNORED
     */

#define KZM_RAMADDRESS (0x80000000)
#define KZM_FPGA       (0xb4001040)

static struct arm_boot_info kzm_binfo = {
    .loader_start = KZM_RAMADDRESS,
    .board_id = 1722,
};

static void kzm_init(QEMUMachineInitArgs *args)
{
    ram_addr_t ram_size = args->ram_size;
    const char *cpu_model = args->cpu_model;
    const char *kernel_filename = args->kernel_filename;
    const char *kernel_cmdline = args->kernel_cmdline;
    const char *initrd_filename = args->initrd_filename;
    ARMCPU *cpu;
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    MemoryRegion *sram = g_new(MemoryRegion, 1);
    MemoryRegion *ram_alias = g_new(MemoryRegion, 1);
    DeviceState *dev;
    DeviceState *ccm;

    if (!cpu_model) {
        cpu_model = "arm1136";
    }

    cpu = cpu_arm_init(cpu_model);
    if (!cpu) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }

    /* On a real system, the first 16k is a `secure boot rom' */

    memory_region_init_ram(ram, NULL, "kzm.ram", ram_size);
    vmstate_register_ram_global(ram);
    memory_region_add_subregion(address_space_mem, KZM_RAMADDRESS, ram);

    memory_region_init_alias(ram_alias, NULL, "ram.alias", ram, 0, ram_size);
    memory_region_add_subregion(address_space_mem, 0x88000000, ram_alias);

    memory_region_init_ram(sram, NULL, "kzm.sram", 0x4000);
    memory_region_add_subregion(address_space_mem, 0x1FFFC000, sram);

    dev = sysbus_create_varargs("imx_avic", 0x68000000,
                                qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_IRQ),
                                qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_FIQ),
                                NULL);

    imx_serial_create(0, 0x43f90000, qdev_get_gpio_in(dev, 45));
    imx_serial_create(1, 0x43f94000, qdev_get_gpio_in(dev, 32));

    ccm = sysbus_create_simple("imx_ccm", 0x53f80000, NULL);

    imx_timerp_create(0x53f94000, qdev_get_gpio_in(dev, 28), ccm);
    imx_timerp_create(0x53f98000, qdev_get_gpio_in(dev, 27), ccm);
    imx_timerg_create(0x53f90000, qdev_get_gpio_in(dev, 29), ccm);

    if (nd_table[0].used) {
        lan9118_init(&nd_table[0], 0xb6000000, qdev_get_gpio_in(dev, 52));
    }

    if (serial_hds[2]) { /* touchscreen */
        serial_mm_init(address_space_mem, KZM_FPGA+0x10, 0,
                       qdev_get_gpio_in(dev, 52),
                       14745600, serial_hds[2],
                       DEVICE_NATIVE_ENDIAN);
    }

    kzm_binfo.ram_size = ram_size;
    kzm_binfo.kernel_filename = kernel_filename;
    kzm_binfo.kernel_cmdline = kernel_cmdline;
    kzm_binfo.initrd_filename = initrd_filename;
    kzm_binfo.nb_cpus = 1;
    arm_load_kernel(cpu, &kzm_binfo);
}

static QEMUMachine kzm_machine = {
    .name = "kzm",
    .desc = "ARM KZM Emulation Baseboard (ARM1136)",
    .init = kzm_init,
};

static void kzm_machine_init(void)
{
    qemu_register_machine(&kzm_machine);
}

machine_init(kzm_machine_init)
