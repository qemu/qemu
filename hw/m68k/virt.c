/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QEMU Virtual M68K Machine
 *
 * (c) 2020 Laurent Vivier <laurent@vivier.eu>
 *
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/guest-random.h"
#include "exec/target_page.h"
#include "system/system.h"
#include "cpu.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "elf.h"
#include "hw/loader.h"
#include "ui/console.h"
#include "hw/sysbus.h"
#include "standard-headers/asm-m68k/bootinfo.h"
#include "standard-headers/asm-m68k/bootinfo-virt.h"
#include "bootinfo.h"
#include "net/net.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "system/qtest.h"
#include "system/runstate.h"
#include "system/reset.h"

#include "hw/intc/m68k_irqc.h"
#include "hw/misc/virt_ctrl.h"
#include "hw/char/goldfish_tty.h"
#include "hw/rtc/goldfish_rtc.h"
#include "hw/intc/goldfish_pic.h"
#include "hw/virtio/virtio-mmio.h"
#include "hw/virtio/virtio-blk.h"

/*
 * 6 goldfish-pic for CPU IRQ #1 to IRQ #6
 * CPU IRQ #1 -> PIC #1
 *               IRQ #1 to IRQ #31 -> unused
 *               IRQ #32 -> goldfish-tty
 * CPU IRQ #2 -> PIC #2
 *               IRQ #1 to IRQ #32 -> virtio-mmio from 1 to 32
 * CPU IRQ #3 -> PIC #3
 *               IRQ #1 to IRQ #32 -> virtio-mmio from 33 to 64
 * CPU IRQ #4 -> PIC #4
 *               IRQ #1 to IRQ #32 -> virtio-mmio from 65 to 96
 * CPU IRQ #5 -> PIC #5
 *               IRQ #1 to IRQ #32 -> virtio-mmio from 97 to 128
 * CPU IRQ #6 -> PIC #6
 *               IRQ #1 -> goldfish-rtc
 *               IRQ #2 to IRQ #32 -> unused
 * CPU IRQ #7 -> NMI
 */

#define PIC_IRQ_BASE(num)     (8 + (num - 1) * 32)
#define PIC_IRQ(num, irq)     (PIC_IRQ_BASE(num) + irq - 1)
#define PIC_GPIO(pic_irq)     (qdev_get_gpio_in(pic_dev[(pic_irq - 8) / 32], \
                                                (pic_irq - 8) % 32))

#define VIRT_GF_PIC_MMIO_BASE 0xff000000     /* MMIO: 0xff000000 - 0xff005fff */
#define VIRT_GF_PIC_IRQ_BASE  1              /* IRQ: #1 -> #6 */
#define VIRT_GF_PIC_NB        6

/* 2 goldfish-rtc (and timer) */
#define VIRT_GF_RTC_MMIO_BASE 0xff006000     /* MMIO: 0xff006000 - 0xff007fff */
#define VIRT_GF_RTC_IRQ_BASE  PIC_IRQ(6, 1)  /* PIC: #6, IRQ: #1 */
#define VIRT_GF_RTC_NB        2

/* 1 goldfish-tty */
#define VIRT_GF_TTY_MMIO_BASE 0xff008000     /* MMIO: 0xff008000 - 0xff008fff */
#define VIRT_GF_TTY_IRQ_BASE  PIC_IRQ(1, 32) /* PIC: #1, IRQ: #32 */

/* 1 virt-ctrl */
#define VIRT_CTRL_MMIO_BASE 0xff009000    /* MMIO: 0xff009000 - 0xff009fff */
#define VIRT_CTRL_IRQ_BASE  PIC_IRQ(1, 1) /* PIC: #1, IRQ: #1 */

/*
 * virtio-mmio size is 0x200 bytes
 * we use 4 goldfish-pic to attach them,
 * we can attach 32 virtio devices / goldfish-pic
 * -> we can manage 32 * 4 = 128 virtio devices
 */
#define VIRT_VIRTIO_MMIO_BASE 0xff010000     /* MMIO: 0xff010000 - 0xff01ffff */
#define VIRT_VIRTIO_IRQ_BASE  PIC_IRQ(2, 1)  /* PIC: 2, 3, 4, 5, IRQ: ALL */

typedef struct {
    M68kCPU *cpu;
    hwaddr initial_pc;
    hwaddr initial_stack;
} ResetInfo;

static void main_cpu_reset(void *opaque)
{
    ResetInfo *reset_info = opaque;
    M68kCPU *cpu = reset_info->cpu;
    CPUState *cs = CPU(cpu);

    cpu_reset(cs);
    cpu->env.aregs[7] = reset_info->initial_stack;
    cpu->env.pc = reset_info->initial_pc;
}

static void rerandomize_rng_seed(void *opaque)
{
    struct bi_record *rng_seed = opaque;
    qemu_guest_getrandom_nofail((void *)rng_seed->data + 2,
                                be16_to_cpu(*(uint16_t *)rng_seed->data));
}

static void virt_init(MachineState *machine)
{
    M68kCPU *cpu = NULL;
    int32_t kernel_size;
    uint64_t elf_entry;
    ram_addr_t initrd_base;
    int32_t initrd_size;
    ram_addr_t ram_size = machine->ram_size;
    const char *kernel_filename = machine->kernel_filename;
    const char *initrd_filename = machine->initrd_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;
    hwaddr parameters_base;
    DeviceState *dev;
    DeviceState *irqc_dev;
    DeviceState *pic_dev[VIRT_GF_PIC_NB];
    SysBusDevice *sysbus;
    hwaddr io_base;
    int i;
    ResetInfo *reset_info;
    uint8_t rng_seed[32];

    if (ram_size > 3399672 * KiB) {
        /*
         * The physical memory can be up to 4 GiB - 16 MiB, but linux
         * kernel crashes after this limit (~ 3.2 GiB)
         */
        error_report("Too much memory for this machine: %" PRId64 " KiB, "
                     "maximum 3399672 KiB", ram_size / KiB);
        exit(1);
    }

    reset_info = g_new0(ResetInfo, 1);

    /* init CPUs */
    cpu = M68K_CPU(cpu_create(machine->cpu_type));

    reset_info->cpu = cpu;
    qemu_register_reset(main_cpu_reset, reset_info);

    /* RAM */
    memory_region_add_subregion(get_system_memory(), 0, machine->ram);

    /* IRQ Controller */

    irqc_dev = qdev_new(TYPE_M68K_IRQC);
    object_property_set_link(OBJECT(irqc_dev), "m68k-cpu",
                             OBJECT(cpu), &error_abort);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(irqc_dev), &error_fatal);

    /*
     * 6 goldfish-pic
     *
     * map: 0xff000000 - 0xff006fff = 28 KiB
     * IRQ: #1 (lower priority) -> #6 (higher priority)
     *
     */
    io_base = VIRT_GF_PIC_MMIO_BASE;
    for (i = 0; i < VIRT_GF_PIC_NB; i++) {
        pic_dev[i] = qdev_new(TYPE_GOLDFISH_PIC);
        sysbus = SYS_BUS_DEVICE(pic_dev[i]);
        qdev_prop_set_uint8(pic_dev[i], "index", i);
        sysbus_realize_and_unref(sysbus, &error_fatal);

        sysbus_mmio_map(sysbus, 0, io_base);
        sysbus_connect_irq(sysbus, 0, qdev_get_gpio_in(irqc_dev, i));

        io_base += 0x1000;
    }

    /* goldfish-rtc */
    io_base = VIRT_GF_RTC_MMIO_BASE;
    for (i = 0; i < VIRT_GF_RTC_NB; i++) {
        dev = qdev_new(TYPE_GOLDFISH_RTC);
        qdev_prop_set_bit(dev, "big-endian", true);
        sysbus = SYS_BUS_DEVICE(dev);
        sysbus_realize_and_unref(sysbus, &error_fatal);
        sysbus_mmio_map(sysbus, 0, io_base);
        sysbus_connect_irq(sysbus, 0, PIC_GPIO(VIRT_GF_RTC_IRQ_BASE + i));

        io_base += 0x1000;
    }

    /* goldfish-tty */
    dev = qdev_new(TYPE_GOLDFISH_TTY);
    sysbus = SYS_BUS_DEVICE(dev);
    qdev_prop_set_chr(dev, "chardev", serial_hd(0));
    sysbus_realize_and_unref(sysbus, &error_fatal);
    sysbus_mmio_map(sysbus, 0, VIRT_GF_TTY_MMIO_BASE);
    sysbus_connect_irq(sysbus, 0, PIC_GPIO(VIRT_GF_TTY_IRQ_BASE));

    /* virt controller */
    dev = sysbus_create_simple(TYPE_VIRT_CTRL, VIRT_CTRL_MMIO_BASE,
                               PIC_GPIO(VIRT_CTRL_IRQ_BASE));

    /* virtio-mmio */
    io_base = VIRT_VIRTIO_MMIO_BASE;
    for (i = 0; i < 128; i++) {
        dev = qdev_new(TYPE_VIRTIO_MMIO);
        qdev_prop_set_bit(dev, "force-legacy", false);
        sysbus = SYS_BUS_DEVICE(dev);
        sysbus_realize_and_unref(sysbus, &error_fatal);
        sysbus_connect_irq(sysbus, 0, PIC_GPIO(VIRT_VIRTIO_IRQ_BASE + i));
        sysbus_mmio_map(sysbus, 0, io_base);
        io_base += 0x200;
    }

    if (kernel_filename) {
        CPUState *cs = CPU(cpu);
        uint64_t high;
        void *param_blob, *param_ptr, *param_rng_seed;

        if (kernel_cmdline) {
            param_blob = g_malloc(strlen(kernel_cmdline) + 1024);
        } else {
            param_blob = g_malloc(1024);
        }

        kernel_size = load_elf(kernel_filename, NULL, NULL, NULL,
                               &elf_entry, NULL, &high, NULL, ELFDATA2MSB,
                               EM_68K, 0, 0);
        if (kernel_size < 0) {
            error_report("could not load kernel '%s'", kernel_filename);
            exit(1);
        }
        reset_info->initial_pc = elf_entry;
        parameters_base = (high + 1) & ~1;
        param_ptr = param_blob;

        BOOTINFO1(param_ptr, BI_MACHTYPE, MACH_VIRT);
        if (m68k_feature(&cpu->env, M68K_FEATURE_M68020)) {
            BOOTINFO1(param_ptr, BI_CPUTYPE, CPU_68020);
        } else if (m68k_feature(&cpu->env, M68K_FEATURE_M68030)) {
            BOOTINFO1(param_ptr, BI_MMUTYPE, MMU_68030);
            BOOTINFO1(param_ptr, BI_CPUTYPE, CPU_68030);
        } else if (m68k_feature(&cpu->env, M68K_FEATURE_M68040)) {
            BOOTINFO1(param_ptr, BI_FPUTYPE, FPU_68040);
            BOOTINFO1(param_ptr, BI_MMUTYPE, MMU_68040);
            BOOTINFO1(param_ptr, BI_CPUTYPE, CPU_68040);
        } else if (m68k_feature(&cpu->env, M68K_FEATURE_M68060)) {
            BOOTINFO1(param_ptr, BI_FPUTYPE, FPU_68060);
            BOOTINFO1(param_ptr, BI_MMUTYPE, MMU_68060);
            BOOTINFO1(param_ptr, BI_CPUTYPE, CPU_68060);
        }
        BOOTINFO2(param_ptr, BI_MEMCHUNK, 0, ram_size);

        BOOTINFO1(param_ptr, BI_VIRT_QEMU_VERSION,
                  ((QEMU_VERSION_MAJOR << 24) | (QEMU_VERSION_MINOR << 16) |
                   (QEMU_VERSION_MICRO << 8)));
        BOOTINFO2(param_ptr, BI_VIRT_GF_PIC_BASE,
                  VIRT_GF_PIC_MMIO_BASE, VIRT_GF_PIC_IRQ_BASE);
        BOOTINFO2(param_ptr, BI_VIRT_GF_RTC_BASE,
                  VIRT_GF_RTC_MMIO_BASE, VIRT_GF_RTC_IRQ_BASE);
        BOOTINFO2(param_ptr, BI_VIRT_GF_TTY_BASE,
                  VIRT_GF_TTY_MMIO_BASE, VIRT_GF_TTY_IRQ_BASE);
        BOOTINFO2(param_ptr, BI_VIRT_CTRL_BASE,
                  VIRT_CTRL_MMIO_BASE, VIRT_CTRL_IRQ_BASE);
        BOOTINFO2(param_ptr, BI_VIRT_VIRTIO_BASE,
                  VIRT_VIRTIO_MMIO_BASE, VIRT_VIRTIO_IRQ_BASE);

        if (kernel_cmdline) {
            BOOTINFOSTR(param_ptr, BI_COMMAND_LINE,
                        kernel_cmdline);
        }

        /* Pass seed to RNG. */
        param_rng_seed = param_ptr;
        qemu_guest_getrandom_nofail(rng_seed, sizeof(rng_seed));
        BOOTINFODATA(param_ptr, BI_RNG_SEED,
                     rng_seed, sizeof(rng_seed));

        /* load initrd */
        if (initrd_filename) {
            initrd_size = get_image_size(initrd_filename);
            if (initrd_size < 0) {
                error_report("could not load initial ram disk '%s'",
                             initrd_filename);
                exit(1);
            }

            initrd_base = (ram_size - initrd_size) & TARGET_PAGE_MASK;
            load_image_targphys(initrd_filename, initrd_base,
                                ram_size - initrd_base);
            BOOTINFO2(param_ptr, BI_RAMDISK, initrd_base,
                      initrd_size);
        } else {
            initrd_base = 0;
            initrd_size = 0;
        }
        BOOTINFO0(param_ptr, BI_LAST);
        rom_add_blob_fixed_as("bootinfo", param_blob, param_ptr - param_blob,
                              parameters_base, cs->as);
        qemu_register_reset_nosnapshotload(rerandomize_rng_seed,
                            rom_ptr_for_as(cs->as, parameters_base,
                                           param_ptr - param_blob) +
                            (param_rng_seed - param_blob));
        g_free(param_blob);
    }
}

static void virt_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    mc->desc = "QEMU M68K Virtual Machine";
    mc->init = virt_init;
    mc->default_cpu_type = M68K_CPU_TYPE_NAME("m68040");
    mc->max_cpus = 1;
    mc->no_floppy = 1;
    mc->no_parallel = 1;
    mc->default_ram_id = "m68k_virt.ram";
}

static const TypeInfo virt_machine_info = {
    .name       = MACHINE_TYPE_NAME("virt"),
    .parent     = TYPE_MACHINE,
    .abstract   = true,
    .class_init = virt_machine_class_init,
};

static void virt_machine_register_types(void)
{
    type_register_static(&virt_machine_info);
}

type_init(virt_machine_register_types)

#define DEFINE_VIRT_MACHINE_IMPL(latest, ...) \
    static void MACHINE_VER_SYM(class_init, virt, __VA_ARGS__)( \
        ObjectClass *oc, \
        void *data) \
    { \
        MachineClass *mc = MACHINE_CLASS(oc); \
        MACHINE_VER_SYM(options, virt, __VA_ARGS__)(mc); \
        mc->desc = "QEMU " MACHINE_VER_STR(__VA_ARGS__) " M68K Virtual Machine"; \
        MACHINE_VER_DEPRECATION(__VA_ARGS__); \
        if (latest) { \
            mc->alias = "virt"; \
        } \
    } \
    static const TypeInfo MACHINE_VER_SYM(info, virt, __VA_ARGS__) = \
    { \
        .name = MACHINE_VER_TYPE_NAME("virt", __VA_ARGS__), \
        .parent = MACHINE_TYPE_NAME("virt"), \
        .class_init = MACHINE_VER_SYM(class_init, virt, __VA_ARGS__), \
    }; \
    static void MACHINE_VER_SYM(register, virt, __VA_ARGS__)(void) \
    { \
        MACHINE_VER_DELETION(__VA_ARGS__); \
        type_register_static(&MACHINE_VER_SYM(info, virt, __VA_ARGS__)); \
    } \
    type_init(MACHINE_VER_SYM(register, virt, __VA_ARGS__));

#define DEFINE_VIRT_MACHINE_AS_LATEST(major, minor) \
    DEFINE_VIRT_MACHINE_IMPL(true, major, minor)
#define DEFINE_VIRT_MACHINE(major, minor) \
    DEFINE_VIRT_MACHINE_IMPL(false, major, minor)

static void virt_machine_10_1_options(MachineClass *mc)
{
}
DEFINE_VIRT_MACHINE_AS_LATEST(10, 1)

static void virt_machine_10_0_options(MachineClass *mc)
{
    virt_machine_10_1_options(mc);
    compat_props_add(mc->compat_props, hw_compat_10_0, hw_compat_10_0_len);
}
DEFINE_VIRT_MACHINE(10, 0)

static void virt_machine_9_2_options(MachineClass *mc)
{
    virt_machine_10_0_options(mc);
    compat_props_add(mc->compat_props, hw_compat_9_2, hw_compat_9_2_len);
}
DEFINE_VIRT_MACHINE(9, 2)

static void virt_machine_9_1_options(MachineClass *mc)
{
    virt_machine_9_2_options(mc);
    compat_props_add(mc->compat_props, hw_compat_9_1, hw_compat_9_1_len);
}
DEFINE_VIRT_MACHINE(9, 1)

static void virt_machine_9_0_options(MachineClass *mc)
{
    virt_machine_9_1_options(mc);
    compat_props_add(mc->compat_props, hw_compat_9_0, hw_compat_9_0_len);
}
DEFINE_VIRT_MACHINE(9, 0)

static void virt_machine_8_2_options(MachineClass *mc)
{
    virt_machine_9_0_options(mc);
    compat_props_add(mc->compat_props, hw_compat_8_2, hw_compat_8_2_len);
}
DEFINE_VIRT_MACHINE(8, 2)

static void virt_machine_8_1_options(MachineClass *mc)
{
    virt_machine_8_2_options(mc);
    compat_props_add(mc->compat_props, hw_compat_8_1, hw_compat_8_1_len);
}
DEFINE_VIRT_MACHINE(8, 1)

static void virt_machine_8_0_options(MachineClass *mc)
{
    virt_machine_8_1_options(mc);
    compat_props_add(mc->compat_props, hw_compat_8_0, hw_compat_8_0_len);
}
DEFINE_VIRT_MACHINE(8, 0)

static void virt_machine_7_2_options(MachineClass *mc)
{
    virt_machine_8_0_options(mc);
    compat_props_add(mc->compat_props, hw_compat_7_2, hw_compat_7_2_len);
}
DEFINE_VIRT_MACHINE(7, 2)

static void virt_machine_7_1_options(MachineClass *mc)
{
    virt_machine_7_2_options(mc);
    compat_props_add(mc->compat_props, hw_compat_7_1, hw_compat_7_1_len);
}
DEFINE_VIRT_MACHINE(7, 1)

static void virt_machine_7_0_options(MachineClass *mc)
{
    virt_machine_7_1_options(mc);
    compat_props_add(mc->compat_props, hw_compat_7_0, hw_compat_7_0_len);
}
DEFINE_VIRT_MACHINE(7, 0)

static void virt_machine_6_2_options(MachineClass *mc)
{
    virt_machine_7_0_options(mc);
    compat_props_add(mc->compat_props, hw_compat_6_2, hw_compat_6_2_len);
}
DEFINE_VIRT_MACHINE(6, 2)

static void virt_machine_6_1_options(MachineClass *mc)
{
    virt_machine_6_2_options(mc);
    compat_props_add(mc->compat_props, hw_compat_6_1, hw_compat_6_1_len);
}
DEFINE_VIRT_MACHINE(6, 1)

static void virt_machine_6_0_options(MachineClass *mc)
{
    virt_machine_6_1_options(mc);
    compat_props_add(mc->compat_props, hw_compat_6_0, hw_compat_6_0_len);
}
DEFINE_VIRT_MACHINE(6, 0)
