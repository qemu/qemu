/*
 * Copyright (c) 2013 Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * PDK Board System emulation.
 *
 * Based on hw/arm/kzm.c
 *
 * Copyright (c) 2008 OKL and 2011 NICTA
 * Written by Hans at OK-Labs
 * Updated by Peter Chubb.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "hw/arm/fsl-imx25.h"
#include "hw/boards.h"
#include "qemu/error-report.h"
#include "exec/address-spaces.h"
#include "sysemu/qtest.h"
#include "hw/i2c/i2c.h"

/* Memory map for PDK Emulation Baseboard:
 * 0x00000000-0x7fffffff See i.MX25 SOC fr support
 * 0x80000000-0x87ffffff RAM + Alias          EMULATED
 * 0x90000000-0x9fffffff RAM + Alias          EMULATED
 * 0xa0000000-0xa7ffffff Flash                IGNORED
 * 0xa8000000-0xafffffff Flash                IGNORED
 * 0xb0000000-0xb1ffffff SRAM                 IGNORED
 * 0xb2000000-0xb3ffffff SRAM                 IGNORED
 * 0xb4000000-0xb5ffffff CS4                  IGNORED
 * 0xb6000000-0xb8000fff Reserved             IGNORED
 * 0xb8001000-0xb8001fff SDRAM CTRL reg       IGNORED
 * 0xb8002000-0xb8002fff WEIM CTRL reg        IGNORED
 * 0xb8003000-0xb8003fff M3IF CTRL reg        IGNORED
 * 0xb8004000-0xb8004fff EMI CTRL reg         IGNORED
 * 0xb8005000-0xbaffffff Reserved             IGNORED
 * 0xbb000000-0xbb000fff NAND flash area buf  IGNORED
 * 0xbb001000-0xbb0011ff NAND flash reserved  IGNORED
 * 0xbb001200-0xbb001dff Reserved             IGNORED
 * 0xbb001e00-0xbb001fff NAN flash CTRL reg   IGNORED
 * 0xbb012000-0xbfffffff Reserved             IGNORED
 * 0xc0000000-0xffffffff Reserved             IGNORED
 */

typedef struct IMX25PDK {
    FslIMX25State soc;
    MemoryRegion ram;
    MemoryRegion ram_alias;
} IMX25PDK;

static struct arm_boot_info imx25_pdk_binfo;

static void imx25_pdk_init(MachineState *machine)
{
    IMX25PDK *s = g_new0(IMX25PDK, 1);
    unsigned int ram_size;
    unsigned int alias_offset;
    int i;

    object_initialize_child(OBJECT(machine), "soc", &s->soc, sizeof(s->soc),
                            TYPE_FSL_IMX25, &error_abort, NULL);

    object_property_set_bool(OBJECT(&s->soc), true, "realized", &error_fatal);

    /* We need to initialize our memory */
    if (machine->ram_size > (FSL_IMX25_SDRAM0_SIZE + FSL_IMX25_SDRAM1_SIZE)) {
        warn_report("RAM size " RAM_ADDR_FMT " above max supported, "
                    "reduced to %x", machine->ram_size,
                    FSL_IMX25_SDRAM0_SIZE + FSL_IMX25_SDRAM1_SIZE);
        machine->ram_size = FSL_IMX25_SDRAM0_SIZE + FSL_IMX25_SDRAM1_SIZE;
    }

    memory_region_allocate_system_memory(&s->ram, NULL, "imx25.ram",
                                         machine->ram_size);
    memory_region_add_subregion(get_system_memory(), FSL_IMX25_SDRAM0_ADDR,
                                &s->ram);

    /* initialize the alias memory if any */
    for (i = 0, ram_size = machine->ram_size, alias_offset = 0;
         (i < 2) && ram_size; i++) {
        unsigned int size;
        static const struct {
            hwaddr addr;
            unsigned int size;
        } ram[2] = {
            { FSL_IMX25_SDRAM0_ADDR, FSL_IMX25_SDRAM0_SIZE },
            { FSL_IMX25_SDRAM1_ADDR, FSL_IMX25_SDRAM1_SIZE },
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

    imx25_pdk_binfo.ram_size = machine->ram_size;
    imx25_pdk_binfo.kernel_filename = machine->kernel_filename;
    imx25_pdk_binfo.kernel_cmdline = machine->kernel_cmdline;
    imx25_pdk_binfo.initrd_filename = machine->initrd_filename;
    imx25_pdk_binfo.loader_start = FSL_IMX25_SDRAM0_ADDR;
    imx25_pdk_binfo.board_id = 1771,
    imx25_pdk_binfo.nb_cpus = 1;

    /*
     * We test explicitly for qtest here as it is not done (yet?) in
     * arm_load_kernel(). Without this the "make check" command would
     * fail.
     */
    if (!qtest_enabled()) {
        arm_load_kernel(&s->soc.cpu, &imx25_pdk_binfo);
    }
}

static void imx25_pdk_machine_init(MachineClass *mc)
{
    mc->desc = "ARM i.MX25 PDK board (ARM926)";
    mc->init = imx25_pdk_init;
    mc->ignore_memory_transaction_failures = true;
}

DEFINE_MACHINE("imx25-pdk", imx25_pdk_machine_init)
