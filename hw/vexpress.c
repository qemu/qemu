/*
 * ARM Versatile Express emulation.
 *
 * Copyright (c) 2010 - 2011 B Labs Ltd.
 * Copyright (c) 2011 Linaro Limited
 * Written by Bahadir Balban, Amit Mahajan, Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "sysbus.h"
#include "arm-misc.h"
#include "primecell.h"
#include "devices.h"
#include "net.h"
#include "sysemu.h"
#include "boards.h"

#define SMP_BOOT_ADDR 0xe0000000

#define VEXPRESS_BOARD_ID 0x8e0

static struct arm_boot_info vexpress_binfo = {
    .smp_loader_start = SMP_BOOT_ADDR,
};

static void vexpress_a9_init(ram_addr_t ram_size,
                     const char *boot_device,
                     const char *kernel_filename, const char *kernel_cmdline,
                     const char *initrd_filename, const char *cpu_model)
{
    CPUState *env = NULL;
    ram_addr_t ram_offset, vram_offset, sram_offset;
    DeviceState *dev, *sysctl, *pl041;
    SysBusDevice *busdev;
    qemu_irq *irqp;
    qemu_irq pic[64];
    int n;
    qemu_irq cpu_irq[4];
    uint32_t proc_id;
    uint32_t sys_id;
    ram_addr_t low_ram_size, vram_size, sram_size;

    if (!cpu_model) {
        cpu_model = "cortex-a9";
    }

    for (n = 0; n < smp_cpus; n++) {
        env = cpu_init(cpu_model);
        if (!env) {
            fprintf(stderr, "Unable to find CPU definition\n");
            exit(1);
        }
        irqp = arm_pic_init_cpu(env);
        cpu_irq[n] = irqp[ARM_PIC_CPU_IRQ];
    }

    if (ram_size > 0x40000000) {
        /* 1GB is the maximum the address space permits */
        fprintf(stderr, "vexpress: cannot model more than 1GB RAM\n");
        exit(1);
    }

    ram_offset = qemu_ram_alloc(NULL, "vexpress.highmem", ram_size);
    low_ram_size = ram_size;
    if (low_ram_size > 0x4000000) {
        low_ram_size = 0x4000000;
    }
    /* RAM is from 0x60000000 upwards. The bottom 64MB of the
     * address space should in theory be remappable to various
     * things including ROM or RAM; we always map the RAM there.
     */
    cpu_register_physical_memory(0x0, low_ram_size, ram_offset | IO_MEM_RAM);
    cpu_register_physical_memory(0x60000000, ram_size,
                                 ram_offset | IO_MEM_RAM);

    /* 0x1e000000 A9MPCore (SCU) private memory region */
    dev = qdev_create(NULL, "a9mpcore_priv");
    qdev_prop_set_uint32(dev, "num-cpu", smp_cpus);
    qdev_init_nofail(dev);
    busdev = sysbus_from_qdev(dev);
    vexpress_binfo.smp_priv_base = 0x1e000000;
    sysbus_mmio_map(busdev, 0, vexpress_binfo.smp_priv_base);
    for (n = 0; n < smp_cpus; n++) {
        sysbus_connect_irq(busdev, n, cpu_irq[n]);
    }
    /* Interrupts [42:0] are from the motherboard;
     * [47:43] are reserved; [63:48] are daughterboard
     * peripherals. Note that some documentation numbers
     * external interrupts starting from 32 (because the
     * A9MP has internal interrupts 0..31).
     */
    for (n = 0; n < 64; n++) {
        pic[n] = qdev_get_gpio_in(dev, n);
    }

    /* Motherboard peripherals CS7 : 0x10000000 .. 0x10020000 */
    sys_id = 0x1190f500;
    proc_id = 0x0c000191;

    /* 0x10000000 System registers */
    sysctl = qdev_create(NULL, "realview_sysctl");
    qdev_prop_set_uint32(sysctl, "sys_id", sys_id);
    qdev_init_nofail(sysctl);
    qdev_prop_set_uint32(sysctl, "proc_id", proc_id);
    sysbus_mmio_map(sysbus_from_qdev(sysctl), 0, 0x10000000);

    /* 0x10001000 SP810 system control */
    /* 0x10002000 serial bus PCI */
    /* 0x10004000 PL041 audio */
    pl041 = qdev_create(NULL, "pl041");
    qdev_prop_set_uint32(pl041, "nc_fifo_depth", 512);
    qdev_init_nofail(pl041);
    sysbus_mmio_map(sysbus_from_qdev(pl041), 0, 0x10004000);
    sysbus_connect_irq(sysbus_from_qdev(pl041), 0, pic[11]);

    dev = sysbus_create_varargs("pl181", 0x10005000, pic[9], pic[10], NULL);
    /* Wire up MMC card detect and read-only signals */
    qdev_connect_gpio_out(dev, 0,
                          qdev_get_gpio_in(sysctl, ARM_SYSCTL_GPIO_MMC_WPROT));
    qdev_connect_gpio_out(dev, 1,
                          qdev_get_gpio_in(sysctl, ARM_SYSCTL_GPIO_MMC_CARDIN));

    sysbus_create_simple("pl050_keyboard", 0x10006000, pic[12]);
    sysbus_create_simple("pl050_mouse", 0x10007000, pic[13]);

    sysbus_create_simple("pl011", 0x10009000, pic[5]);
    sysbus_create_simple("pl011", 0x1000a000, pic[6]);
    sysbus_create_simple("pl011", 0x1000b000, pic[7]);
    sysbus_create_simple("pl011", 0x1000c000, pic[8]);

    /* 0x1000f000 SP805 WDT */

    sysbus_create_simple("sp804", 0x10011000, pic[2]);
    sysbus_create_simple("sp804", 0x10012000, pic[3]);

    /* 0x10016000 Serial Bus DVI */

    sysbus_create_simple("pl031", 0x10017000, pic[4]); /* RTC */

    /* 0x1001a000 Compact Flash */

    /* 0x1001f000 PL111 CLCD (motherboard) */

    /* Daughterboard peripherals : 0x10020000 .. 0x20000000 */

    /* 0x10020000 PL111 CLCD (daughterboard) */
    sysbus_create_simple("pl111", 0x10020000, pic[44]);

    /* 0x10060000 AXI RAM */
    /* 0x100e0000 PL341 Dynamic Memory Controller */
    /* 0x100e1000 PL354 Static Memory Controller */
    /* 0x100e2000 System Configuration Controller */

    sysbus_create_simple("sp804", 0x100e4000, pic[48]);
    /* 0x100e5000 SP805 Watchdog module */
    /* 0x100e6000 BP147 TrustZone Protection Controller */
    /* 0x100e9000 PL301 'Fast' AXI matrix */
    /* 0x100ea000 PL301 'Slow' AXI matrix */
    /* 0x100ec000 TrustZone Address Space Controller */
    /* 0x10200000 CoreSight debug APB */
    /* 0x1e00a000 PL310 L2 Cache Controller */

    /* CS0: NOR0 flash          : 0x40000000 .. 0x44000000 */
    /* CS4: NOR1 flash          : 0x44000000 .. 0x48000000 */
    /* CS2: SRAM                : 0x48000000 .. 0x4a000000 */
    sram_size = 0x2000000;
    sram_offset = qemu_ram_alloc(NULL, "vexpress.sram", sram_size);
    cpu_register_physical_memory(0x48000000, sram_size,
                                 sram_offset | IO_MEM_RAM);

    /* CS3: USB, ethernet, VRAM : 0x4c000000 .. 0x50000000 */

    /* 0x4c000000 Video RAM */
    vram_size = 0x800000;
    vram_offset = qemu_ram_alloc(NULL, "vexpress.vram", vram_size);
    cpu_register_physical_memory(0x4c000000, vram_size,
                                 vram_offset | IO_MEM_RAM);

    /* 0x4e000000 LAN9118 Ethernet */
    if (nd_table[0].vlan) {
        lan9118_init(&nd_table[0], 0x4e000000, pic[15]);
    }

    /* 0x4f000000 ISP1761 USB */

    /* ??? Hack to map an additional page of ram for the secondary CPU
       startup code.  I guess this works on real hardware because the
       BootROM happens to be in ROM/flash or in memory that isn't clobbered
       until after Linux boots the secondary CPUs.  */
    ram_offset = qemu_ram_alloc(NULL, "vexpress.hack", 0x1000);
    cpu_register_physical_memory(SMP_BOOT_ADDR, 0x1000,
                                 ram_offset | IO_MEM_RAM);

    vexpress_binfo.ram_size = ram_size;
    vexpress_binfo.kernel_filename = kernel_filename;
    vexpress_binfo.kernel_cmdline = kernel_cmdline;
    vexpress_binfo.initrd_filename = initrd_filename;
    vexpress_binfo.nb_cpus = smp_cpus;
    vexpress_binfo.board_id = VEXPRESS_BOARD_ID;
    vexpress_binfo.loader_start = 0x60000000;
    arm_load_kernel(first_cpu, &vexpress_binfo);
}


static QEMUMachine vexpress_a9_machine = {
    .name = "vexpress-a9",
    .desc = "ARM Versatile Express for Cortex-A9",
    .init = vexpress_a9_init,
    .use_scsi = 1,
    .max_cpus = 4,
};

static void vexpress_machine_init(void)
{
    qemu_register_machine(&vexpress_a9_machine);
}

machine_init(vexpress_machine_init);
