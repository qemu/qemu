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

#include "sysemu.h"
#include "sysbus.h"
#include "net.h"
#include "arm-misc.h"
#include "exec-memory.h"
#include "exynos4210.h"
#include "boards.h"

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
};

static QEMUMachine exynos4_machines[EXYNOS4_NUM_OF_BOARDS];

static void lan9215_init(uint32_t base, qemu_irq irq)
{
    DeviceState *dev;
    SysBusDevice *s;

    /* This should be a 9215 but the 9118 is close enough */
    if (nd_table[0].vlan) {
        qemu_check_nic_model(&nd_table[0], "lan9118");
        dev = qdev_create(NULL, "lan9118");
        qdev_set_nic_properties(dev, &nd_table[0]);
        qdev_prop_set_uint32(dev, "mode_16bit", 1);
        qdev_init_nofail(dev);
        s = sysbus_from_qdev(dev);
        sysbus_mmio_map(s, 0, base);
        sysbus_connect_irq(s, 0, irq);
    }
}

static Exynos4210State *exynos4_boards_init_common(
        const char *kernel_filename,
        const char *kernel_cmdline,
        const char *initrd_filename,
        Exynos4BoardType board_type)
{
    if (smp_cpus != EXYNOS4210_NCPUS) {
        fprintf(stderr, "%s board supports only %d CPU cores. Ignoring smp_cpus"
                " value.\n",
                exynos4_machines[board_type].name,
                exynos4_machines[board_type].max_cpus);
    }

    exynos4_board_binfo.ram_size = exynos4_board_ram_size[board_type];
    exynos4_board_binfo.board_id = exynos4_board_id[board_type];
    exynos4_board_binfo.smp_bootreg_addr =
            exynos4_board_smp_bootreg_addr[board_type];
    exynos4_board_binfo.kernel_filename = kernel_filename;
    exynos4_board_binfo.initrd_filename = initrd_filename;
    exynos4_board_binfo.kernel_cmdline = kernel_cmdline;
    exynos4_board_binfo.gic_cpu_if_addr =
            EXYNOS4210_SMP_PRIVATE_BASE_ADDR + 0x100;

    PRINT_DEBUG("\n ram_size: %luMiB [0x%08lx]\n"
            " kernel_filename: %s\n"
            " kernel_cmdline: %s\n"
            " initrd_filename: %s\n",
            exynos4_board_ram_size[board_type] / 1048576,
            exynos4_board_ram_size[board_type],
            kernel_filename,
            kernel_cmdline,
            initrd_filename);

    return exynos4210_init(get_system_memory(),
            exynos4_board_ram_size[board_type]);
}

static void nuri_init(ram_addr_t ram_size,
        const char *boot_device,
        const char *kernel_filename, const char *kernel_cmdline,
        const char *initrd_filename, const char *cpu_model)
{
    exynos4_boards_init_common(kernel_filename, kernel_cmdline,
                initrd_filename, EXYNOS4_BOARD_NURI);

    arm_load_kernel(first_cpu, &exynos4_board_binfo);
}

static void smdkc210_init(ram_addr_t ram_size,
        const char *boot_device,
        const char *kernel_filename, const char *kernel_cmdline,
        const char *initrd_filename, const char *cpu_model)
{
    Exynos4210State *s = exynos4_boards_init_common(kernel_filename,
            kernel_cmdline, initrd_filename, EXYNOS4_BOARD_SMDKC210);

    lan9215_init(SMDK_LAN9118_BASE_ADDR,
            qemu_irq_invert(s->irq_table[exynos4210_get_irq(37, 1)]));
    arm_load_kernel(first_cpu, &exynos4_board_binfo);
}

static QEMUMachine exynos4_machines[EXYNOS4_NUM_OF_BOARDS] = {
    [EXYNOS4_BOARD_NURI] = {
        .name = "nuri",
        .desc = "Samsung NURI board (Exynos4210)",
        .init = nuri_init,
        .max_cpus = EXYNOS4210_NCPUS,
    },
    [EXYNOS4_BOARD_SMDKC210] = {
        .name = "smdkc210",
        .desc = "Samsung SMDKC210 board (Exynos4210)",
        .init = smdkc210_init,
        .max_cpus = EXYNOS4210_NCPUS,
    },
};

static void exynos4_machine_init(void)
{
    qemu_register_machine(&exynos4_machines[EXYNOS4_BOARD_NURI]);
    qemu_register_machine(&exynos4_machines[EXYNOS4_BOARD_SMDKC210]);
}

machine_init(exynos4_machine_init);
