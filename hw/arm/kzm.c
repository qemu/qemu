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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/arm/fsl-imx31.h"
#include "hw/boards.h"
#include "qemu/error-report.h"
#include "exec/address-spaces.h"
#include "net/net.h"
#include "hw/devices.h"
#include "hw/char/serial.h"
#include "sysemu/qtest.h"

/* Memory map for Kzm Emulation Baseboard:
 * 0x00000000-0x7fffffff See i.MX31 SOC for support
 * 0x80000000-0x8fffffff RAM                  EMULATED
 * 0x90000000-0x9fffffff RAM                  EMULATED
 * 0xa0000000-0xafffffff Flash                IGNORED
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

typedef struct IMX31KZM {
    FslIMX31State soc;
    MemoryRegion ram;
    MemoryRegion ram_alias;
} IMX31KZM;

#define KZM_RAM_ADDR            (FSL_IMX31_SDRAM0_ADDR)
#define KZM_FPGA_ADDR           (FSL_IMX31_CS4_ADDR + 0x1040)
#define KZM_LAN9118_ADDR        (FSL_IMX31_CS5_ADDR)

static struct arm_boot_info kzm_binfo = {
    .loader_start = KZM_RAM_ADDR,
    .board_id = 1722,
};

static void kzm_init(MachineState *machine)
{
    IMX31KZM *s = g_new0(IMX31KZM, 1);
    unsigned int ram_size;
    unsigned int alias_offset;
    unsigned int i;

    object_initialize(&s->soc, sizeof(s->soc), TYPE_FSL_IMX31);
    object_property_add_child(OBJECT(machine), "soc", OBJECT(&s->soc),
                              &error_abort);

    object_property_set_bool(OBJECT(&s->soc), true, "realized", &error_fatal);

    /* Check the amount of memory is compatible with the SOC */
    if (machine->ram_size > (FSL_IMX31_SDRAM0_SIZE + FSL_IMX31_SDRAM1_SIZE)) {
        error_report("WARNING: RAM size " RAM_ADDR_FMT " above max supported, "
                     "reduced to %x", machine->ram_size,
                     FSL_IMX31_SDRAM0_SIZE + FSL_IMX31_SDRAM1_SIZE);
        machine->ram_size = FSL_IMX31_SDRAM0_SIZE + FSL_IMX31_SDRAM1_SIZE;
    }

    memory_region_allocate_system_memory(&s->ram, NULL, "kzm.ram",
                                         machine->ram_size);
    memory_region_add_subregion(get_system_memory(), FSL_IMX31_SDRAM0_ADDR,
                                &s->ram);

    /* initialize the alias memory if any */
    for (i = 0, ram_size = machine->ram_size, alias_offset = 0;
         (i < 2) && ram_size; i++) {
        unsigned int size;
        static const struct {
            hwaddr addr;
            unsigned int size;
        } ram[2] = {
            { FSL_IMX31_SDRAM0_ADDR, FSL_IMX31_SDRAM0_SIZE },
            { FSL_IMX31_SDRAM1_ADDR, FSL_IMX31_SDRAM1_SIZE },
        };

        size = MIN(ram_size, ram[i].size);

        ram_size -= size;

        if (size < ram[i].size) {
            memory_region_init_alias(&s->ram_alias, NULL, "ram.alias",
                                     &s->ram, alias_offset, ram[i].size - size);
            memory_region_add_subregion(get_system_memory(),
                                        ram[i].addr + size, &s->ram_alias);
        }

        alias_offset += ram[i].size;
    }

    if (nd_table[0].used) {
        lan9118_init(&nd_table[0], KZM_LAN9118_ADDR,
                     qdev_get_gpio_in(DEVICE(&s->soc.avic), 52));
    }

    if (serial_hds[2]) { /* touchscreen */
        serial_mm_init(get_system_memory(), KZM_FPGA_ADDR+0x10, 0,
                       qdev_get_gpio_in(DEVICE(&s->soc.avic), 52),
                       14745600, serial_hds[2], DEVICE_NATIVE_ENDIAN);
    }

    kzm_binfo.ram_size = machine->ram_size;
    kzm_binfo.kernel_filename = machine->kernel_filename;
    kzm_binfo.kernel_cmdline = machine->kernel_cmdline;
    kzm_binfo.initrd_filename = machine->initrd_filename;
    kzm_binfo.nb_cpus = 1;

    if (!qtest_enabled()) {
        arm_load_kernel(&s->soc.cpu, &kzm_binfo);
    }
}

static void kzm_machine_init(MachineClass *mc)
{
    mc->desc = "ARM KZM Emulation Baseboard (ARM1136)";
    mc->init = kzm_init;
}

DEFINE_MACHINE("kzm", kzm_machine_init)
