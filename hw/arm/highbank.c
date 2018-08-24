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
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/devices.h"
#include "hw/loader.h"
#include "net/net.h"
#include "sysemu/kvm.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "exec/address-spaces.h"
#include "qemu/error-report.h"
#include "hw/char/pl011.h"
#include "hw/ide/ahci.h"
#include "hw/cpu/a9mpcore.h"
#include "hw/cpu/a15mpcore.h"
#include "qemu/log.h"

#define SMP_BOOT_ADDR           0x100
#define SMP_BOOT_REG            0x40
#define MPCORE_PERIPHBASE       0xfff10000

#define MVBAR_ADDR              0x200
#define BOARD_SETUP_ADDR        (MVBAR_ADDR + 8 * sizeof(uint32_t))

#define NIRQ_GIC                160

/* Board init.  */

static void hb_write_board_setup(ARMCPU *cpu,
                                 const struct arm_boot_info *info)
{
    arm_write_secure_board_setup_dummy_smc(cpu, info, MVBAR_ADDR);
}

static void hb_write_secondary(ARMCPU *cpu, const struct arm_boot_info *info)
{
    int n;
    uint32_t smpboot[] = {
        0xee100fb0, /* mrc p15, 0, r0, c0, c0, 5 - read current core id */
        0xe210000f, /* ands r0, r0, #0x0f */
        0xe3a03040, /* mov r3, #0x40 - jump address is 0x40 + 0x10 * core id */
        0xe0830200, /* add r0, r3, r0, lsl #4 */
        0xe59f2024, /* ldr r2, privbase */
        0xe3a01001, /* mov r1, #1 */
        0xe5821100, /* str r1, [r2, #256] - set GICC_CTLR.Enable */
        0xe3a010ff, /* mov r1, #0xff */
        0xe5821104, /* str r1, [r2, #260] - set GICC_PMR.Priority to 0xff */
        0xf57ff04f, /* dsb */
        0xe320f003, /* wfi */
        0xe5901000, /* ldr     r1, [r0] */
        0xe1110001, /* tst     r1, r1 */
        0x0afffffb, /* beq     <wfi> */
        0xe12fff11, /* bx      r1 */
        MPCORE_PERIPHBASE   /* privbase: MPCore peripheral base address.  */
    };
    for (n = 0; n < ARRAY_SIZE(smpboot); n++) {
        smpboot[n] = tswap32(smpboot[n]);
    }
    rom_add_blob_fixed("smpboot", smpboot, sizeof(smpboot), SMP_BOOT_ADDR);
}

static void hb_reset_secondary(ARMCPU *cpu, const struct arm_boot_info *info)
{
    CPUARMState *env = &cpu->env;

    switch (info->nb_cpus) {
    case 4:
        address_space_stl_notdirty(&address_space_memory,
                                   SMP_BOOT_REG + 0x30, 0,
                                   MEMTXATTRS_UNSPECIFIED, NULL);
    case 3:
        address_space_stl_notdirty(&address_space_memory,
                                   SMP_BOOT_REG + 0x20, 0,
                                   MEMTXATTRS_UNSPECIFIED, NULL);
    case 2:
        address_space_stl_notdirty(&address_space_memory,
                                   SMP_BOOT_REG + 0x10, 0,
                                   MEMTXATTRS_UNSPECIFIED, NULL);
        env->regs[15] = SMP_BOOT_ADDR;
        break;
    default:
        break;
    }
}

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
#define HIGHBANK_REGISTERS(obj) \
    OBJECT_CHECK(HighbankRegsState, (obj), TYPE_HIGHBANK_REGISTERS)

typedef struct {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    uint32_t regs[NUM_REGS];
} HighbankRegsState;

static VMStateDescription vmstate_highbank_regs = {
    .name = "highbank-regs",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
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

static void highbank_regs_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Calxeda Highbank registers";
    dc->vmsd = &vmstate_highbank_regs;
    dc->reset = highbank_regs_reset;
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
    ram_addr_t ram_size = machine->ram_size;
    const char *kernel_filename = machine->kernel_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;
    const char *initrd_filename = machine->initrd_filename;
    DeviceState *dev = NULL;
    SysBusDevice *busdev;
    qemu_irq pic[128];
    int n;
    qemu_irq cpu_irq[4];
    qemu_irq cpu_fiq[4];
    qemu_irq cpu_virq[4];
    qemu_irq cpu_vfiq[4];
    MemoryRegion *sysram;
    MemoryRegion *dram;
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
        assert(0);
    }

    for (n = 0; n < smp_cpus; n++) {
        Object *cpuobj;
        ARMCPU *cpu;

        cpuobj = object_new(machine->cpu_type);
        cpu = ARM_CPU(cpuobj);

        object_property_set_int(cpuobj, QEMU_PSCI_CONDUIT_SMC,
                                "psci-conduit", &error_abort);

        if (n) {
            /* Secondary CPUs start in PSCI powered-down state */
            object_property_set_bool(cpuobj, true,
                                     "start-powered-off", &error_abort);
        }

        if (object_property_find(cpuobj, "reset-cbar", NULL)) {
            object_property_set_int(cpuobj, MPCORE_PERIPHBASE,
                                    "reset-cbar", &error_abort);
        }
        object_property_set_bool(cpuobj, true, "realized", &error_fatal);
        cpu_irq[n] = qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_IRQ);
        cpu_fiq[n] = qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_FIQ);
        cpu_virq[n] = qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_VIRQ);
        cpu_vfiq[n] = qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_VFIQ);
    }

    sysmem = get_system_memory();
    dram = g_new(MemoryRegion, 1);
    memory_region_allocate_system_memory(dram, NULL, "highbank.dram", ram_size);
    /* SDRAM at address zero.  */
    memory_region_add_subregion(sysmem, 0, dram);

    sysram = g_new(MemoryRegion, 1);
    memory_region_init_ram(sysram, NULL, "highbank.sysram", 0x8000,
                           &error_fatal);
    memory_region_add_subregion(sysmem, 0xfff88000, sysram);
    if (bios_name != NULL) {
        sysboot_filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
        if (sysboot_filename != NULL) {
            if (load_image_targphys(sysboot_filename, 0xfff88000, 0x8000) < 0) {
                error_report("Unable to load %s", bios_name);
                exit(1);
            }
            g_free(sysboot_filename);
        } else {
            error_report("Unable to find %s", bios_name);
            exit(1);
        }
    }

    switch (machine_id) {
    case CALXEDA_HIGHBANK:
        dev = qdev_create(NULL, "l2x0");
        qdev_init_nofail(dev);
        busdev = SYS_BUS_DEVICE(dev);
        sysbus_mmio_map(busdev, 0, 0xfff12000);

        dev = qdev_create(NULL, TYPE_A9MPCORE_PRIV);
        break;
    case CALXEDA_MIDWAY:
        dev = qdev_create(NULL, TYPE_A15MPCORE_PRIV);
        break;
    }
    qdev_prop_set_uint32(dev, "num-cpu", smp_cpus);
    qdev_prop_set_uint32(dev, "num-irq", NIRQ_GIC);
    qdev_init_nofail(dev);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(busdev, 0, MPCORE_PERIPHBASE);
    for (n = 0; n < smp_cpus; n++) {
        sysbus_connect_irq(busdev, n, cpu_irq[n]);
        sysbus_connect_irq(busdev, n + smp_cpus, cpu_fiq[n]);
        sysbus_connect_irq(busdev, n + 2 * smp_cpus, cpu_virq[n]);
        sysbus_connect_irq(busdev, n + 3 * smp_cpus, cpu_vfiq[n]);
    }

    for (n = 0; n < 128; n++) {
        pic[n] = qdev_get_gpio_in(dev, n);
    }

    dev = qdev_create(NULL, "sp804");
    qdev_prop_set_uint32(dev, "freq0", 150000000);
    qdev_prop_set_uint32(dev, "freq1", 150000000);
    qdev_init_nofail(dev);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(busdev, 0, 0xfff34000);
    sysbus_connect_irq(busdev, 0, pic[18]);
    pl011_create(0xfff36000, pic[20], serial_hd(0));

    dev = qdev_create(NULL, TYPE_HIGHBANK_REGISTERS);
    qdev_init_nofail(dev);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(busdev, 0, 0xfff3c000);

    sysbus_create_simple("pl061", 0xfff30000, pic[14]);
    sysbus_create_simple("pl061", 0xfff31000, pic[15]);
    sysbus_create_simple("pl061", 0xfff32000, pic[16]);
    sysbus_create_simple("pl061", 0xfff33000, pic[17]);
    sysbus_create_simple("pl031", 0xfff35000, pic[19]);
    sysbus_create_simple("pl022", 0xfff39000, pic[23]);

    sysbus_create_simple(TYPE_SYSBUS_AHCI, 0xffe08000, pic[83]);

    if (nd_table[0].used) {
        qemu_check_nic_model(&nd_table[0], "xgmac");
        dev = qdev_create(NULL, "xgmac");
        qdev_set_nic_properties(dev, &nd_table[0]);
        qdev_init_nofail(dev);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, 0xfff50000);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, pic[77]);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 1, pic[78]);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 2, pic[79]);

        qemu_check_nic_model(&nd_table[1], "xgmac");
        dev = qdev_create(NULL, "xgmac");
        qdev_set_nic_properties(dev, &nd_table[1]);
        qdev_init_nofail(dev);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, 0xfff51000);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, pic[80]);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 1, pic[81]);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 2, pic[82]);
    }

    /* TODO create and connect IDE devices for ide_drive_get() */

    highbank_binfo.ram_size = ram_size;
    highbank_binfo.kernel_filename = kernel_filename;
    highbank_binfo.kernel_cmdline = kernel_cmdline;
    highbank_binfo.initrd_filename = initrd_filename;
    /* highbank requires a dtb in order to boot, and the dtb will override
     * the board ID. The following value is ignored, so set it to -1 to be
     * clear that the value is meaningless.
     */
    highbank_binfo.board_id = -1;
    highbank_binfo.nb_cpus = smp_cpus;
    highbank_binfo.loader_start = 0;
    highbank_binfo.write_secondary_boot = hb_write_secondary;
    highbank_binfo.secondary_cpu_reset_hook = hb_reset_secondary;
    if (!kvm_enabled()) {
        highbank_binfo.board_setup_addr = BOARD_SETUP_ADDR;
        highbank_binfo.write_board_setup = hb_write_board_setup;
        highbank_binfo.secure_board_setup = true;
    } else {
        warn_report("cannot load built-in Monitor support "
                    "if KVM is enabled. Some guests (such as Linux) "
                    "may not boot.");
    }

    arm_load_kernel(ARM_CPU(first_cpu), &highbank_binfo);
}

static void highbank_init(MachineState *machine)
{
    calxeda_init(machine, CALXEDA_HIGHBANK);
}

static void midway_init(MachineState *machine)
{
    calxeda_init(machine, CALXEDA_MIDWAY);
}

static void highbank_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Calxeda Highbank (ECX-1000)";
    mc->init = highbank_init;
    mc->block_default_type = IF_IDE;
    mc->units_per_default_bus = 1;
    mc->max_cpus = 4;
    mc->ignore_memory_transaction_failures = true;
}

static const TypeInfo highbank_type = {
    .name = MACHINE_TYPE_NAME("highbank"),
    .parent = TYPE_MACHINE,
    .class_init = highbank_class_init,
};

static void midway_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Calxeda Midway (ECX-2000)";
    mc->init = midway_init;
    mc->block_default_type = IF_IDE;
    mc->units_per_default_bus = 1;
    mc->max_cpus = 4;
    mc->ignore_memory_transaction_failures = true;
}

static const TypeInfo midway_type = {
    .name = MACHINE_TYPE_NAME("midway"),
    .parent = TYPE_MACHINE,
    .class_init = midway_class_init,
};

static void calxeda_machines_init(void)
{
    type_register_static(&highbank_type);
    type_register_static(&midway_type);
}

type_init(calxeda_machines_init)
