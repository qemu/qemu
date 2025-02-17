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
#include "hw/qdev-properties.h"
#include "hw/arm/fsl-imx25.h"
#include "hw/arm/boot.h"
#include "hw/boards.h"
#include "qemu/error-report.h"
#include "system/qtest.h"
#include "hw/i2c/i2c.h"
#include "qemu/cutils.h"

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
    MemoryRegion ram_alias;
} IMX25PDK;

static struct arm_boot_info imx25_pdk_binfo;

static void imx25_pdk_init(MachineState *machine)
{
    IMX25PDK *s = g_new0(IMX25PDK, 1);
    unsigned int ram_size;
    unsigned int alias_offset;
    int i;

    object_initialize_child(OBJECT(machine), "soc", &s->soc, TYPE_FSL_IMX25);

    qdev_realize(DEVICE(&s->soc), NULL, &error_fatal);

    /* We need to initialize our memory */
    if (machine->ram_size > (FSL_IMX25_SDRAM0_SIZE + FSL_IMX25_SDRAM1_SIZE)) {
        char *sz = size_to_str(FSL_IMX25_SDRAM0_SIZE + FSL_IMX25_SDRAM1_SIZE);
        error_report("RAM size more than %s is not supported", sz);
        g_free(sz);
        exit(EXIT_FAILURE);
    }

    memory_region_add_subregion(get_system_memory(), FSL_IMX25_SDRAM0_ADDR,
                                machine->ram);

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
                                     machine->ram,
                                     alias_offset, ram[i].size - size);
            memory_region_add_subregion(get_system_memory(),
                                        ram[i].addr + size, &s->ram_alias);
        }

        alias_offset += ram[i].size;
    }

    imx25_pdk_binfo.ram_size = machine->ram_size;
    imx25_pdk_binfo.loader_start = FSL_IMX25_SDRAM0_ADDR;
    imx25_pdk_binfo.board_id = 1771;

    for (i = 0; i < FSL_IMX25_NUM_ESDHCS; i++) {
        BusState *bus;
        DeviceState *carddev;
        DriveInfo *di;
        BlockBackend *blk;

        di = drive_get(IF_SD, 0, i);
        blk = di ? blk_by_legacy_dinfo(di) : NULL;
        bus = qdev_get_child_bus(DEVICE(&s->soc.esdhc[i]), "sd-bus");
        carddev = qdev_new(TYPE_SD_CARD);
        qdev_prop_set_drive_err(carddev, "drive", blk, &error_fatal);
        qdev_realize_and_unref(carddev, bus, &error_fatal);
    }

    /*
     * We test explicitly for qtest here as it is not done (yet?) in
     * arm_load_kernel(). Without this the "make check" command would
     * fail.
     */
    if (!qtest_enabled()) {
        arm_load_kernel(&s->soc.cpu, machine, &imx25_pdk_binfo);
    }
}

static void imx25_pdk_machine_init(MachineClass *mc)
{
    mc->desc = "ARM i.MX25 PDK board (ARM926)";
    mc->init = imx25_pdk_init;
    mc->ignore_memory_transaction_failures = true;
    mc->default_ram_id = "imx25.ram";
    mc->auto_create_sdcard = true;
}

DEFINE_MACHINE("imx25-pdk", imx25_pdk_machine_init)
