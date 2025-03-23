/*
 * Calxeda Highbank SoC emulation
 *
 * Copyright (c) 2010-2012 Calxeda
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qemu/datadir.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/arm/boot.h"
#include "hw/arm/machines-qom.h"
#include "hw/loader.h"
#include "net/net.h"
#include "system/runstate.h"
#include "system/system.h"
#include "hw/boards.h"
#include "qemu/error-report.h"
#include "hw/char/pl011.h"
#include "hw/ide/ahci-sysbus.h"
#include "hw/cpu/a9mpcore.h"
#include "hw/cpu/a15mpcore.h"
#include "qemu/log.h"
#include "qom/object.h"
#include "cpu.h"
#include "target/arm/cpu-qom.h"

#define SMP_BOOT_ADDR           0x100
#define SMP_BOOT_REG            0x40
#define MPCORE_PERIPHBASE       0xfff10000

#define MVBAR_ADDR              0x200
#define BOARD_SETUP_ADDR        (MVBAR_ADDR + 8 * sizeof(uint32_t))

#define GIC_EXT_IRQS            128 /* EnergyCore ECX-1000 & ECX-2000 */

/* Board init.  */

#define NUM_REGS      0x200
static void hb_regs_write(void *opaque, hwaddr offset,
                          uint64_t value, unsigned size)
{
    uint32_t *regs = opaque;

    if (offset == 0xf00) {
        if (value == 1 || value == 2) {
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        } else if (value == 3) {
            qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
        }
    }

    if (offset / 4 >= NUM_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                  "highbank: bad write offset 0x%" HWADDR_PRIx "\n", offset);
        return;
    }
    regs[offset / 4] = value;
}

static uint64_t hb_regs_read(void *opaque, hwaddr offset,
                             unsigned size)
{
    uint32_t value;
    uint32_t *regs = opaque;

    if (offset / 4 >= NUM_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                  "highbank: bad read offset 0x%" HWADDR_PRIx "\n", offset);
        return 0;
    }
    value = regs[offset / 4];

    if ((offset == 0x100) || (offset == 0x108) || (offset == 0x10C)) {
        value |= 0x30000000;
    }

    return value;
}

static const MemoryRegionOps hb_mem_ops = {
    .read = hb_regs_read,
    .write = hb_regs_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

#define TYPE_HIGHBANK_REGISTERS "highbank-regs"
OBJECT_DECLARE_SIMPLE_TYPE(HighbankRegsState, HIGHBANK_REGISTERS)

struct HighbankRegsState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    uint32_t regs[NUM_REGS];
};

static const VMStateDescription vmstate_highbank_regs = {
    .name = "highbank-regs",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, HighbankRegsState, NUM_REGS),
        VMSTATE_END_OF_LIST(),
    },
};

static void highbank_regs_reset(DeviceState *dev)
{
    HighbankRegsState *s = HIGHBANK_REGISTERS(dev);

    s->regs[0x40] = 0x05F20121;
    s->regs[0x41] = 0x2;
    s->regs[0x42] = 0x05F30121;
    s->regs[0x43] = 0x05F40121;
}

static void highbank_regs_init(Object *obj)
{
    HighbankRegsState *s = HIGHBANK_REGISTERS(obj);
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &hb_mem_ops, s->regs,
                          "highbank_regs", 0x1000);
    sysbus_init_mmio(dev, &s->iomem);
}

static void highbank_regs_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Calxeda Highbank registers";
    dc->vmsd = &vmstate_highbank_regs;
    device_class_set_legacy_reset(dc, highbank_regs_reset);
}

static const TypeInfo highbank_regs_info = {
    .name          = TYPE_HIGHBANK_REGISTERS,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(HighbankRegsState),
    .instance_init = highbank_regs_init,
    .class_init    = highbank_regs_class_init,
};

static void highbank_regs_register_types(void)
{
    type_register_static(&highbank_regs_info);
}

type_init(highbank_regs_register_types)

static struct arm_boot_info highbank_binfo;

enum cxmachines {
    CALXEDA_HIGHBANK,
    CALXEDA_MIDWAY,
};

/* ram_size must be set to match the upper bound of memory in the
 * device tree (linux/arch/arm/boot/dts/highbank.dts), which is
 * normally 0xff900000 or -m 4089. When running this board on a
 * 32-bit host, set the reg value of memory to 0xf7ff00000 in the
 * device tree and pass -m 2047 to QEMU.
 */
static void calxeda_init(MachineState *machine, enum cxmachines machine_id)
{
    DeviceState *dev = NULL;
    SysBusDevice *busdev;
    qemu_irq pic[GIC_EXT_IRQS];
    int n;
    unsigned int smp_cpus = machine->smp.cpus;
    qemu_irq cpu_irq[4];
    qemu_irq cpu_fiq[4];
    qemu_irq cpu_virq[4];
    qemu_irq cpu_vfiq[4];
    MemoryRegion *sysram;
    MemoryRegion *sysmem;
    char *sysboot_filename;

    switch (machine_id) {
    case CALXEDA_HIGHBANK:
        machine->cpu_type = ARM_CPU_TYPE_NAME("cortex-a9");
        break;
    case CALXEDA_MIDWAY:
        machine->cpu_type = ARM_CPU_TYPE_NAME("cortex-a15");
        break;
    default:
        g_assert_not_reached();
    }

    for (n = 0; n < smp_cpus; n++) {
        Object *cpuobj;
        ARMCPU *cpu;

        cpuobj = object_new(machine->cpu_type);
        cpu = ARM_CPU(cpuobj);

        object_property_add_child(OBJECT(machine), "cpu[*]", cpuobj);
        object_property_set_int(cpuobj, "psci-conduit", QEMU_PSCI_CONDUIT_SMC,
                                &error_abort);

        if (object_property_find(cpuobj, "reset-cbar")) {
            object_property_set_int(cpuobj, "reset-cbar", MPCORE_PERIPHBASE,
                                    &error_abort);
        }
        qdev_realize(DEVICE(cpuobj), NULL, &error_fatal);
        cpu_irq[n] = qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_IRQ);
        cpu_fiq[n] = qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_FIQ);
        cpu_virq[n] = qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_VIRQ);
        cpu_vfiq[n] = qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_VFIQ);
    }

    sysmem = get_system_memory();
    /* SDRAM at address zero.  */
    memory_region_add_subregion(sysmem, 0, machine->ram);

    sysram = g_new(MemoryRegion, 1);
    memory_region_init_ram(sysram, NULL, "highbank.sysram", 0x8000,
                           &error_fatal);
    memory_region_add_subregion(sysmem, 0xfff88000, sysram);
    if (machine->firmware != NULL) {
        sysboot_filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, machine->firmware);
        if (sysboot_filename != NULL) {
            if (load_image_targphys(sysboot_filename, 0xfff88000, 0x8000,
                                    NULL) < 0) {
                error_report("Unable to load %s", machine->firmware);
                exit(1);
            }
            g_free(sysboot_filename);
        } else {
            error_report("Unable to find %s", machine->firmware);
            exit(1);
        }
    }

    switch (machine_id) {
    case CALXEDA_HIGHBANK:
        dev = qdev_new("l2x0");
        busdev = SYS_BUS_DEVICE(dev);
        sysbus_realize_and_unref(busdev, &error_fatal);
        sysbus_mmio_map(busdev, 0, 0xfff12000);

        dev = qdev_new(TYPE_A9MPCORE_PRIV);
        break;
    case CALXEDA_MIDWAY:
        dev = qdev_new(TYPE_A15MPCORE_PRIV);
        break;
    }
    qdev_prop_set_uint32(dev, "num-cpu", smp_cpus);
    qdev_prop_set_uint32(dev, "num-irq", GIC_EXT_IRQS + GIC_INTERNAL);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(busdev, &error_fatal);
    sysbus_mmio_map(busdev, 0, MPCORE_PERIPHBASE);
    for (n = 0; n < smp_cpus; n++) {
        sysbus_connect_irq(busdev, n, cpu_irq[n]);
        sysbus_connect_irq(busdev, n + smp_cpus, cpu_fiq[n]);
        sysbus_connect_irq(busdev, n + 2 * smp_cpus, cpu_virq[n]);
        sysbus_connect_irq(busdev, n + 3 * smp_cpus, cpu_vfiq[n]);
    }

    for (n = 0; n < GIC_EXT_IRQS; n++) {
        pic[n] = qdev_get_gpio_in(dev, n);
    }

    dev = qdev_new("sp804");
    qdev_prop_set_uint32(dev, "freq0", 150000000);
    qdev_prop_set_uint32(dev, "freq1", 150000000);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(busdev, &error_fatal);
    sysbus_mmio_map(busdev, 0, 0xfff34000);
    sysbus_connect_irq(busdev, 0, pic[18]);
    pl011_create(0xfff36000, pic[20], serial_hd(0));

    dev = qdev_new(TYPE_HIGHBANK_REGISTERS);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(busdev, &error_fatal);
    sysbus_mmio_map(busdev, 0, 0xfff3c000);

    sysbus_create_simple("pl061", 0xfff30000, pic[14]);
    sysbus_create_simple("pl061", 0xfff31000, pic[15]);
    sysbus_create_simple("pl061", 0xfff32000, pic[16]);
    sysbus_create_simple("pl061", 0xfff33000, pic[17]);
    sysbus_create_simple("pl031", 0xfff35000, pic[19]);
    sysbus_create_simple("pl022", 0xfff39000, pic[23]);

    sysbus_create_simple(TYPE_SYSBUS_AHCI, 0xffe08000, pic[83]);

    dev = qemu_create_nic_device("xgmac", true, NULL);
    if (dev) {
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, 0xfff50000);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, pic[77]);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 1, pic[78]);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 2, pic[79]);
    }

    dev = qemu_create_nic_device("xgmac", true, NULL);
    if (dev) {
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, 0xfff51000);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, pic[80]);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 1, pic[81]);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 2, pic[82]);
    }

    /* TODO create and connect IDE devices for ide_drive_get() */

    highbank_binfo.ram_size = machine->ram_size;
    /* highbank requires a dtb in order to boot, and the dtb will override
     * the board ID. The following value is ignored, so set it to -1 to be
     * clear that the value is meaningless.
     */
    highbank_binfo.board_id = -1;
    highbank_binfo.loader_start = 0;
    highbank_binfo.board_setup_addr = BOARD_SETUP_ADDR;
    highbank_binfo.psci_conduit = QEMU_PSCI_CONDUIT_SMC;

    arm_load_kernel(ARM_CPU(first_cpu), machine, &highbank_binfo);
}

static void highbank_init(MachineState *machine)
{
    calxeda_init(machine, CALXEDA_HIGHBANK);
}

static void midway_init(MachineState *machine)
{
    calxeda_init(machine, CALXEDA_MIDWAY);
}

static void highbank_class_init(ObjectClass *oc, const void *data)
{
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-a9"),
        NULL
    };
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Calxeda Highbank (ECX-1000)";
    mc->init = highbank_init;
    mc->valid_cpu_types = valid_cpu_types;
    mc->block_default_type = IF_IDE;
    mc->units_per_default_bus = 1;
    mc->max_cpus = 4;
    mc->ignore_memory_transaction_failures = true;
    mc->default_ram_id = "highbank.dram";
    mc->deprecation_reason = "no known users left for this machine";
}

static const TypeInfo highbank_type = {
    .name = MACHINE_TYPE_NAME("highbank"),
    .parent = TYPE_MACHINE,
    .class_init = highbank_class_init,
    .interfaces = arm_machine_interfaces,
};

static void midway_class_init(ObjectClass *oc, const void *data)
{
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-a15"),
        NULL
    };
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Calxeda Midway (ECX-2000)";
    mc->init = midway_init;
    mc->valid_cpu_types = valid_cpu_types;
    mc->block_default_type = IF_IDE;
    mc->units_per_default_bus = 1;
    mc->max_cpus = 4;
    mc->ignore_memory_transaction_failures = true;
    mc->default_ram_id = "highbank.dram";
    mc->deprecation_reason = "no known users left for this machine";
}

static const TypeInfo midway_type = {
    .name = MACHINE_TYPE_NAME("midway"),
    .parent = TYPE_MACHINE,
    .class_init = midway_class_init,
    .interfaces = arm_machine_interfaces,
};

static void calxeda_machines_init(void)
{
    type_register_static(&highbank_type);
    type_register_static(&midway_type);
}

type_init(calxeda_machines_init)
