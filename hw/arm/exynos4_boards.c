/*
 *  Samsung exynos4 SoC based boards emulation
 *
 *  Copyright (c) 2011 Samsung Electronics Co., Ltd. All rights reserved.
 *    Maksim Kozlov <m.kozlov@samsung.com>
 *    Evgeny Voevodin <e.voevodin@samsung.com>
 *    Igor Mitsyanko  <i.mitsyanko@samsung.com>
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
 *
 */

#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "sysemu/qtest.h"
#include "hw/sysbus.h"
#include "net/net.h"
#include "hw/arm/arm.h"
#include "exec/address-spaces.h"
#include "hw/arm/exynos4210.h"
#include "hw/boards.h"

#undef DEBUG

//#define DEBUG

#ifdef DEBUG
    #undef PRINT_DEBUG
    #define  PRINT_DEBUG(fmt, args...) \
        do { \
            fprintf(stderr, "  [%s:%d]   "fmt, __func__, __LINE__, ##args); \
        } while (0)
#else
    #define  PRINT_DEBUG(fmt, args...)  do {} while (0)
#endif

#define SMDK_LAN9118_BASE_ADDR      0x05000000

typedef enum Exynos4BoardType {
    EXYNOS4_BOARD_NURI,
    EXYNOS4_BOARD_SMDKC210,
    EXYNOS4_NUM_OF_BOARDS
} Exynos4BoardType;

static int exynos4_board_id[EXYNOS4_NUM_OF_BOARDS] = {
    [EXYNOS4_BOARD_NURI]     = 0xD33,
    [EXYNOS4_BOARD_SMDKC210] = 0xB16,
};

static int exynos4_board_smp_bootreg_addr[EXYNOS4_NUM_OF_BOARDS] = {
    [EXYNOS4_BOARD_NURI]     = EXYNOS4210_SECOND_CPU_BOOTREG,
    [EXYNOS4_BOARD_SMDKC210] = EXYNOS4210_SECOND_CPU_BOOTREG,
};

static unsigned long exynos4_board_ram_size[EXYNOS4_NUM_OF_BOARDS] = {
    [EXYNOS4_BOARD_NURI]     = 0x40000000,
    [EXYNOS4_BOARD_SMDKC210] = 0x40000000,
};

static struct arm_boot_info exynos4_board_binfo = {
    .loader_start     = EXYNOS4210_BASE_BOOT_ADDR,
    .smp_loader_start = EXYNOS4210_SMP_BOOT_ADDR,
    .nb_cpus          = EXYNOS4210_NCPUS,
    .write_secondary_boot = exynos4210_write_secondary,
};

static void lan9215_init(uint32_t base, qemu_irq irq)
{
    DeviceState *dev;
    SysBusDevice *s;

    /* This should be a 9215 but the 9118 is close enough */
    if (nd_table[0].used) {
        qemu_check_nic_model(&nd_table[0], "lan9118");
        dev = qdev_create(NULL, "lan9118");
        qdev_set_nic_properties(dev, &nd_table[0]);
        qdev_prop_set_uint32(dev, "mode_16bit", 1);
        qdev_init_nofail(dev);
        s = SYS_BUS_DEVICE(dev);
        sysbus_mmio_map(s, 0, base);
        sysbus_connect_irq(s, 0, irq);
    }
}

static Exynos4210State *exynos4_boards_init_common(MachineState *machine,
                                                   Exynos4BoardType board_type)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);

    if (smp_cpus != EXYNOS4210_NCPUS && !qtest_enabled()) {
        fprintf(stderr, "%s board supports only %d CPU cores. Ignoring smp_cpus"
                " value.\n",
                mc->name, EXYNOS4210_NCPUS);
    }

    exynos4_board_binfo.ram_size = exynos4_board_ram_size[board_type];
    exynos4_board_binfo.board_id = exynos4_board_id[board_type];
    exynos4_board_binfo.smp_bootreg_addr =
            exynos4_board_smp_bootreg_addr[board_type];
    exynos4_board_binfo.kernel_filename = machine->kernel_filename;
    exynos4_board_binfo.initrd_filename = machine->initrd_filename;
    exynos4_board_binfo.kernel_cmdline = machine->kernel_cmdline;
    exynos4_board_binfo.gic_cpu_if_addr =
            EXYNOS4210_SMP_PRIVATE_BASE_ADDR + 0x100;

    PRINT_DEBUG("\n ram_size: %luMiB [0x%08lx]\n"
            " kernel_filename: %s\n"
            " kernel_cmdline: %s\n"
            " initrd_filename: %s\n",
            exynos4_board_ram_size[board_type] / 1048576,
            exynos4_board_ram_size[board_type],
            machine->kernel_filename,
            machine->kernel_cmdline,
            machine->initrd_filename);

    return exynos4210_init(get_system_memory(),
            exynos4_board_ram_size[board_type]);
}

static void nuri_init(MachineState *machine)
{
    exynos4_boards_init_common(machine, EXYNOS4_BOARD_NURI);

    arm_load_kernel(ARM_CPU(first_cpu), &exynos4_board_binfo);
}

static void smdkc210_init(MachineState *machine)
{
    Exynos4210State *s = exynos4_boards_init_common(machine,
                                                    EXYNOS4_BOARD_SMDKC210);

    lan9215_init(SMDK_LAN9118_BASE_ADDR,
            qemu_irq_invert(s->irq_table[exynos4210_get_irq(37, 1)]));
    arm_load_kernel(ARM_CPU(first_cpu), &exynos4_board_binfo);
}

static void nuri_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Samsung NURI board (Exynos4210)";
    mc->init = nuri_init;
    mc->max_cpus = EXYNOS4210_NCPUS;
}

static const TypeInfo nuri_type = {
    .name = MACHINE_TYPE_NAME("nuri"),
    .parent = TYPE_MACHINE,
    .class_init = nuri_class_init,
};

static void smdkc210_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Samsung SMDKC210 board (Exynos4210)";
    mc->init = smdkc210_init;
    mc->max_cpus = EXYNOS4210_NCPUS;
}

static const TypeInfo smdkc210_type = {
    .name = MACHINE_TYPE_NAME("smdkc210"),
    .parent = TYPE_MACHINE,
    .class_init = smdkc210_class_init,
};

static void exynos4_machines_init(void)
{
    type_register_static(&nuri_type);
    type_register_static(&smdkc210_type);
}

machine_init(exynos4_machines_init)
