/*
 * QEMU RISC-V Virt Board Compatible with Kendryte K230 SDK
 *
 * Copyright (c) 2025 Chao Liu <chao.liu.zevorn@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Provides a board compatible with the Kendryte K230 SDK
 *
 * K230 Technical Reference Manual V0.3.1 (2024-11-18):
 * https://github.com/revyos/external-docs/blob/master/K230/en-us/K230_Technical_Reference_Manual_V0.3.1_20241118.pdf
 *
 * For more information, see <https://www.kendryte.com/en/proDetail/230>
 */

#include "qemu/osdep.h"
#include "cpu-qom.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "system/device_tree.h"
#include "system/system.h"
#include "system/memory.h"
#include "target/riscv/cpu.h"
#include "hw/core/loader.h"
#include "hw/core/sysbus.h"
#include "hw/riscv/k230.h"
#include "hw/riscv/boot.h"
#include "hw/riscv/machines-qom.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/intc/sifive_plic.h"
#include "hw/char/serial-mm.h"
#include "hw/misc/unimp.h"

/* Align K230_SDK k230_canmv_defconfig */
#define K230_DIRECT_OPENSBI_ADDR 0x8000000
#define K230_DIRECT_KERNEL_ADDR  0x8200000
#define K230_DIRECT_DTB_ADDR     0xa000000

static const MemMapEntry memmap[] = {
    [K230_DEV_DDRC] =         { 0x00000000,  0x80000000 },
    [K230_DEV_KPU_L2_CACHE] = { 0x80000000,  0x00200000 },
    [K230_DEV_SRAM] =         { 0x80200000,  0x00200000 },
    [K230_DEV_KPU_CFG] =      { 0x80400000,  0x00000800 },
    [K230_DEV_FFT] =          { 0x80400800,  0x00000400 },
    [K230_DEV_AI_2D_ENGINE] = { 0x80400C00,  0x00000800 },
    [K230_DEV_GSDMA] =        { 0x80800000,  0x00004000 },
    [K230_DEV_DMA] =          { 0x80804000,  0x00004000 },
    [K230_DEV_DECOMP_GZIP] =  { 0x80808000,  0x00004000 },
    [K230_DEV_NON_AI_2D] =    { 0x8080C000,  0x00004000 },
    [K230_DEV_ISP] =          { 0x90000000,  0x00008000 },
    [K230_DEV_DEWARP] =       { 0x90008000,  0x00001000 },
    [K230_DEV_RX_CSI] =       { 0x90009000,  0x00002000 },
    [K230_DEV_H264] =         { 0x90400000,  0x00010000 },
    [K230_DEV_2P5D] =         { 0x90800000,  0x00040000 },
    [K230_DEV_VO] =           { 0x90840000,  0x00010000 },
    [K230_DEV_VO_CFG] =       { 0x90850000,  0x00001000 },
    [K230_DEV_3D_ENGINE] =    { 0x90A00000,  0x00000800 },
    [K230_DEV_PMU] =          { 0x91000000,  0x00000C00 },
    [K230_DEV_RTC] =          { 0x91000C00,  0x00000400 },
    [K230_DEV_CMU] =          { 0x91100000,  0x00001000 },
    [K230_DEV_RMU] =          { 0x91101000,  0x00001000 },
    [K230_DEV_BOOT] =         { 0x91102000,  0x00001000 },
    [K230_DEV_PWR] =          { 0x91103000,  0x00001000 },
    [K230_DEV_MAILBOX] =      { 0x91104000,  0x00001000 },
    [K230_DEV_IOMUX] =        { 0x91105000,  0x00000800 },
    [K230_DEV_TIMER] =        { 0x91105800,  0x00000800 },
    [K230_DEV_WDT0] =         { 0x91106000,  0x00000800 },
    [K230_DEV_WDT1] =         { 0x91106800,  0x00000800 },
    [K230_DEV_TS] =           { 0x91107000,  0x00000800 },
    [K230_DEV_HDI] =          { 0x91107800,  0x00000800 },
    [K230_DEV_STC] =          { 0x91108000,  0x00000800 },
    [K230_DEV_BOOTROM] =      { 0x91200000,  0x00010000 },
    [K230_DEV_SECURITY] =     { 0x91210000,  0x00008000 },
    [K230_DEV_UART0] =        { 0x91400000,  0x00001000 },
    [K230_DEV_UART1] =        { 0x91401000,  0x00001000 },
    [K230_DEV_UART2] =        { 0x91402000,  0x00001000 },
    [K230_DEV_UART3] =        { 0x91403000,  0x00001000 },
    [K230_DEV_UART4] =        { 0x91404000,  0x00001000 },
    [K230_DEV_I2C0] =         { 0x91405000,  0x00001000 },
    [K230_DEV_I2C1] =         { 0x91406000,  0x00001000 },
    [K230_DEV_I2C2] =         { 0x91407000,  0x00001000 },
    [K230_DEV_I2C3] =         { 0x91408000,  0x00001000 },
    [K230_DEV_I2C4] =         { 0x91409000,  0x00001000 },
    [K230_DEV_PWM] =          { 0x9140A000,  0x00001000 },
    [K230_DEV_GPIO0] =        { 0x9140B000,  0x00001000 },
    [K230_DEV_GPIO1] =        { 0x9140C000,  0x00001000 },
    [K230_DEV_ADC] =          { 0x9140D000,  0x00001000 },
    [K230_DEV_CODEC] =        { 0x9140E000,  0x00001000 },
    [K230_DEV_I2S] =          { 0x9140F000,  0x00001000 },
    [K230_DEV_USB0] =         { 0x91500000,  0x00010000 },
    [K230_DEV_USB1] =         { 0x91540000,  0x00010000 },
    [K230_DEV_SD0] =          { 0x91580000,  0x00001000 },
    [K230_DEV_SD1] =          { 0x91581000,  0x00001000 },
    [K230_DEV_QSPI0] =        { 0x91582000,  0x00001000 },
    [K230_DEV_QSPI1] =        { 0x91583000,  0x00001000 },
    [K230_DEV_SPI] =          { 0x91584000,  0x00001000 },
    [K230_DEV_HI_SYS_CFG] =   { 0x91585000,  0x00000400 },
    [K230_DEV_DDRC_CFG] =     { 0x98000000,  0x02000000 },
    [K230_DEV_FLASH] =        { 0xC0000000,  0x08000000 },
    [K230_DEV_PLIC] =         { 0xF00000000, 0x00400000 },
    [K230_DEV_CLINT] =        { 0xF04000000, 0x00400000 },
};

static void k230_soc_init(Object *obj)
{
    K230SoCState *s = RISCV_K230_SOC(obj);
    RISCVHartArrayState *cpu0 = &s->c908_cpu;

    object_initialize_child(obj, "c908-cpu", cpu0, TYPE_RISCV_HART_ARRAY);
    qdev_prop_set_uint32(DEVICE(cpu0), "hartid-base", 0);
    qdev_prop_set_string(DEVICE(cpu0), "cpu-type", TYPE_RISCV_CPU_THEAD_C908);
    qdev_prop_set_uint64(DEVICE(cpu0), "resetvec",
                         memmap[K230_DEV_BOOTROM].base);
}

static DeviceState *k230_create_plic(int base_hartid, int hartid_count)
{
    g_autofree char *plic_hart_config = NULL;

    /* Per-socket PLIC hart topology configuration string */
    plic_hart_config = riscv_plic_hart_config_string(hartid_count);

    /* Per-socket PLIC */
    return sifive_plic_create(memmap[K230_DEV_PLIC].base,
                              plic_hart_config, hartid_count, base_hartid,
                              K230_PLIC_NUM_SOURCES,
                              K230_PLIC_NUM_PRIORITIES,
                              K230_PLIC_PRIORITY_BASE, K230_PLIC_PENDING_BASE,
                              K230_PLIC_ENABLE_BASE, K230_PLIC_ENABLE_STRIDE,
                              K230_PLIC_CONTEXT_BASE,
                              K230_PLIC_CONTEXT_STRIDE,
                              memmap[K230_DEV_PLIC].size);
}

static void k230_create_uart(MemoryRegion *sys_mem, DeviceState *plic,
                             int index)
{
    int uart_dev = K230_DEV_UART0 + index;
    g_autofree char *name = g_strdup_printf("uart%d", index);

    /* Cover the non-16550 part of the SDK's 0x1000 UART window. */
    create_unimplemented_device(name, memmap[uart_dev].base,
                                memmap[uart_dev].size);

    serial_mm_init(sys_mem, memmap[uart_dev].base, 2,
                   qdev_get_gpio_in(plic, K230_UART0_IRQ + index),
                   399193, serial_hd(index), DEVICE_LITTLE_ENDIAN);
}

static void k230_soc_realize(DeviceState *dev, Error **errp)
{
    K230SoCState *s = RISCV_K230_SOC(dev);
    MemoryRegion *sys_mem = get_system_memory();
    int c908_cpus;

    sysbus_realize(SYS_BUS_DEVICE(&s->c908_cpu), &error_fatal);

    c908_cpus = s->c908_cpu.num_harts;

    /* SRAM */
    memory_region_init_ram(&s->sram, OBJECT(dev), "sram",
                           memmap[K230_DEV_SRAM].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[K230_DEV_SRAM].base,
                                &s->sram);

    /* BootROM */
    memory_region_init_rom(&s->bootrom, OBJECT(dev), "bootrom",
                           memmap[K230_DEV_BOOTROM].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[K230_DEV_BOOTROM].base,
                                &s->bootrom);

    /* PLIC */
    s->c908_plic = k230_create_plic(C908_CPU_HARTID, c908_cpus);

    /* CLINT */
    riscv_aclint_swi_create(memmap[K230_DEV_CLINT].base,
                            C908_CPU_HARTID, c908_cpus, false);
    riscv_aclint_mtimer_create(memmap[K230_DEV_CLINT].base + 0x4000,
                               RISCV_ACLINT_DEFAULT_MTIMER_SIZE,
                               C908_CPU_HARTID, c908_cpus,
                               RISCV_ACLINT_DEFAULT_MTIMECMP,
                               RISCV_ACLINT_DEFAULT_MTIME,
                               RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ, true);

    /* UART */
    for (int i = 0; i < K230_UART_COUNT; i++) {
        k230_create_uart(sys_mem, DEVICE(s->c908_plic), i);
    }

    /* unimplemented devices */
    create_unimplemented_device("kpu.l2-cache",
                                memmap[K230_DEV_KPU_L2_CACHE].base,
                                memmap[K230_DEV_KPU_L2_CACHE].size);

    create_unimplemented_device("kpu_cfg", memmap[K230_DEV_KPU_CFG].base,
                                memmap[K230_DEV_KPU_CFG].size);

    create_unimplemented_device("fft", memmap[K230_DEV_FFT].base,
                                memmap[K230_DEV_FFT].size);

    create_unimplemented_device("2d-engine.ai",
                                memmap[K230_DEV_AI_2D_ENGINE].base,
                                memmap[K230_DEV_AI_2D_ENGINE].size);

    create_unimplemented_device("gsdma", memmap[K230_DEV_GSDMA].base,
                                memmap[K230_DEV_GSDMA].size);

    create_unimplemented_device("dma", memmap[K230_DEV_DMA].base,
                                memmap[K230_DEV_DMA].size);

    create_unimplemented_device("decomp-gzip",
                                memmap[K230_DEV_DECOMP_GZIP].base,
                                memmap[K230_DEV_DECOMP_GZIP].size);

    create_unimplemented_device("2d-engine.non-ai",
                                memmap[K230_DEV_NON_AI_2D].base,
                                memmap[K230_DEV_NON_AI_2D].size);

    create_unimplemented_device("isp", memmap[K230_DEV_ISP].base,
                                memmap[K230_DEV_ISP].size);

    create_unimplemented_device("dewarp", memmap[K230_DEV_DEWARP].base,
                                memmap[K230_DEV_DEWARP].size);

    create_unimplemented_device("rx-csi", memmap[K230_DEV_RX_CSI].base,
                                memmap[K230_DEV_RX_CSI].size);

    create_unimplemented_device("vpu", memmap[K230_DEV_H264].base,
                                memmap[K230_DEV_H264].size);

    create_unimplemented_device("gpu", memmap[K230_DEV_2P5D].base,
                                memmap[K230_DEV_2P5D].size);

    create_unimplemented_device("vo", memmap[K230_DEV_VO].base,
                                memmap[K230_DEV_VO].size);

    create_unimplemented_device("vo_cfg", memmap[K230_DEV_VO_CFG].base,
                                memmap[K230_DEV_VO_CFG].size);

    create_unimplemented_device("3d-engine", memmap[K230_DEV_3D_ENGINE].base,
                                memmap[K230_DEV_3D_ENGINE].size);

    create_unimplemented_device("pmu", memmap[K230_DEV_PMU].base,
                                memmap[K230_DEV_PMU].size);

    create_unimplemented_device("rtc", memmap[K230_DEV_RTC].base,
                                memmap[K230_DEV_RTC].size);

    create_unimplemented_device("cmu", memmap[K230_DEV_CMU].base,
                                memmap[K230_DEV_CMU].size);

    create_unimplemented_device("rmu", memmap[K230_DEV_RMU].base,
                                memmap[K230_DEV_RMU].size);

    create_unimplemented_device("boot", memmap[K230_DEV_BOOT].base,
                                memmap[K230_DEV_BOOT].size);

    create_unimplemented_device("pwr", memmap[K230_DEV_PWR].base,
                                memmap[K230_DEV_PWR].size);

    create_unimplemented_device("ipcm", memmap[K230_DEV_MAILBOX].base,
                                memmap[K230_DEV_MAILBOX].size);

    create_unimplemented_device("iomux", memmap[K230_DEV_IOMUX].base,
                                memmap[K230_DEV_IOMUX].size);

    create_unimplemented_device("timer", memmap[K230_DEV_TIMER].base,
                                memmap[K230_DEV_TIMER].size);

    create_unimplemented_device("wdt0", memmap[K230_DEV_WDT0].base,
                                memmap[K230_DEV_WDT0].size);

    create_unimplemented_device("wdt1", memmap[K230_DEV_WDT1].base,
                                memmap[K230_DEV_WDT1].size);

    create_unimplemented_device("ts", memmap[K230_DEV_TS].base,
                                memmap[K230_DEV_TS].size);

    create_unimplemented_device("hdi", memmap[K230_DEV_HDI].base,
                                memmap[K230_DEV_HDI].size);

    create_unimplemented_device("stc", memmap[K230_DEV_STC].base,
                                memmap[K230_DEV_STC].size);

    create_unimplemented_device("security", memmap[K230_DEV_SECURITY].base,
                                memmap[K230_DEV_SECURITY].size);

    create_unimplemented_device("i2c0", memmap[K230_DEV_I2C0].base,
                                memmap[K230_DEV_I2C0].size);

    create_unimplemented_device("i2c1", memmap[K230_DEV_I2C1].base,
                                memmap[K230_DEV_I2C1].size);

    create_unimplemented_device("i2c2", memmap[K230_DEV_I2C2].base,
                                memmap[K230_DEV_I2C2].size);

    create_unimplemented_device("i2c3", memmap[K230_DEV_I2C3].base,
                                memmap[K230_DEV_I2C3].size);

    create_unimplemented_device("i2c4", memmap[K230_DEV_I2C4].base,
                                memmap[K230_DEV_I2C4].size);

    create_unimplemented_device("pwm", memmap[K230_DEV_PWM].base,
                                memmap[K230_DEV_PWM].size);

    create_unimplemented_device("gpio0", memmap[K230_DEV_GPIO0].base,
                                memmap[K230_DEV_GPIO0].size);

    create_unimplemented_device("gpio1", memmap[K230_DEV_GPIO1].base,
                                memmap[K230_DEV_GPIO1].size);

    create_unimplemented_device("adc", memmap[K230_DEV_ADC].base,
                                memmap[K230_DEV_ADC].size);

    create_unimplemented_device("codec", memmap[K230_DEV_CODEC].base,
                                memmap[K230_DEV_CODEC].size);

    create_unimplemented_device("i2s", memmap[K230_DEV_I2S].base,
                                memmap[K230_DEV_I2S].size);

    create_unimplemented_device("usb0", memmap[K230_DEV_USB0].base,
                                memmap[K230_DEV_USB0].size);

    create_unimplemented_device("usb1", memmap[K230_DEV_USB1].base,
                                memmap[K230_DEV_USB1].size);

    create_unimplemented_device("sd0", memmap[K230_DEV_SD0].base,
                                memmap[K230_DEV_SD0].size);

    create_unimplemented_device("sd1", memmap[K230_DEV_SD1].base,
                                memmap[K230_DEV_SD1].size);

    create_unimplemented_device("qspi0", memmap[K230_DEV_QSPI0].base,
                                memmap[K230_DEV_QSPI0].size);

    create_unimplemented_device("qspi1", memmap[K230_DEV_QSPI1].base,
                                memmap[K230_DEV_QSPI1].size);

    create_unimplemented_device("spi", memmap[K230_DEV_SPI].base,
                                memmap[K230_DEV_SPI].size);

    create_unimplemented_device("hi_sys_cfg", memmap[K230_DEV_HI_SYS_CFG].base,
                                memmap[K230_DEV_HI_SYS_CFG].size);

    create_unimplemented_device("ddrc_cfg", memmap[K230_DEV_DDRC_CFG].base,
                                memmap[K230_DEV_DDRC_CFG].size);

    create_unimplemented_device("flash", memmap[K230_DEV_FLASH].base,
                                memmap[K230_DEV_FLASH].size);
}

static void k230_soc_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = k230_soc_realize;
}

static const TypeInfo k230_soc_type_info = {
    .name = TYPE_RISCV_K230_SOC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(K230SoCState),
    .instance_init = k230_soc_init,
    .class_init = k230_soc_class_init,
};

static void k230_soc_register_types(void)
{
    type_register_static(&k230_soc_type_info);
}

type_init(k230_soc_register_types)

static void k230_direct_boot(K230MachineState *s, MachineState *machine)
{
    const char *firmware_name = riscv_default_firmware_name(&s->soc.c908_cpu);
    RISCVBootInfo boot_info = {0};
    hwaddr start_addr = K230_DIRECT_OPENSBI_ADDR;
    hwaddr firmware_end_addr = 0;
    hwaddr kernel_entry = 0;
    int fdt_size = 0;

    if (machine->firmware && !strcmp(machine->firmware, "none")) {
        error_report("K230 direct boot requires OpenSBI firmware; omit "
                     "-bios none or pass OpenSBI with -bios");
        exit(EXIT_FAILURE);
    }

    if (!machine->dtb) {
        error_report("K230 direct boot requires -dtb");
        exit(EXIT_FAILURE);
    }

    machine->fdt = load_device_tree(machine->dtb, &fdt_size);
    if (!machine->fdt) {
        error_report("load_device_tree() failed");
        exit(EXIT_FAILURE);
    }

    qemu_fdt_add_path(machine->fdt, "/chosen");

    riscv_boot_info_init(&boot_info, &s->soc.c908_cpu);
    riscv_load_kernel(machine, &boot_info, K230_DIRECT_KERNEL_ADDR, true, NULL);
    kernel_entry = boot_info.image_low_addr;

    riscv_load_fdt(K230_DIRECT_DTB_ADDR, machine->fdt);

    firmware_end_addr = riscv_find_and_load_firmware(machine, firmware_name,
                                                     &start_addr, NULL);
    if (firmware_end_addr > K230_DIRECT_KERNEL_ADDR) {
        error_report("K230 firmware overlaps kernel address 0x%x",
                     K230_DIRECT_KERNEL_ADDR);
        exit(EXIT_FAILURE);
    }

    riscv_setup_rom_reset_vec(machine, &s->soc.c908_cpu, start_addr,
                              memmap[K230_DEV_BOOTROM].base,
                              memmap[K230_DEV_BOOTROM].size, kernel_entry,
                              K230_DIRECT_DTB_ADDR);
}

static void k230_firmware_boot(K230MachineState *s, MachineState *machine)
{
    const char *firmware_name = riscv_default_firmware_name(&s->soc.c908_cpu);
    hwaddr start_addr = memmap[K230_DEV_DDRC].base;

    if (machine->dtb || (machine->kernel_cmdline && *machine->kernel_cmdline)) {
        error_report("K230 firmware boot does not support -dtb or -append");
        exit(EXIT_FAILURE);
    }

    riscv_find_and_load_firmware(machine, firmware_name, &start_addr, NULL);

    riscv_setup_rom_reset_vec(machine, &s->soc.c908_cpu, start_addr,
                              memmap[K230_DEV_BOOTROM].base,
                              memmap[K230_DEV_BOOTROM].size, 0, 0);
}

static void k230_machine_done(Notifier *notifier, void *data)
{
    K230MachineState *s = container_of(notifier, K230MachineState,
                                       machine_done);
    MachineState *machine = MACHINE(s);

    if (machine->kernel_filename) {
        k230_direct_boot(s, machine);
    } else {
        k230_firmware_boot(s, machine);
    }
}

static void k230_machine_init(MachineState *machine)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    K230MachineState *s = RISCV_K230_MACHINE(machine);
    MemoryRegion *sys_mem = get_system_memory();

    if (machine->ram_size < mc->default_ram_size) {
        char *sz = size_to_str(mc->default_ram_size);
        error_report("Invalid RAM size, should be %s", sz);
        g_free(sz);
        exit(EXIT_FAILURE);
    }

    /* Initialize SoC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc,
                            TYPE_RISCV_K230_SOC);
    qdev_realize(DEVICE(&s->soc), NULL, &error_fatal);

    /* Data Memory */
    memory_region_add_subregion(sys_mem, memmap[K230_DEV_DDRC].base,
                                machine->ram);

    s->machine_done.notify = k230_machine_done;
    qemu_add_machine_init_done_notifier(&s->machine_done);
}

static void k230_machine_instance_init(Object *obj)
{
}

static void k230_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "RISC-V Board compatible with Kendryte K230 SDK";
    mc->init = k230_machine_init;
    mc->default_cpus = 1;
    mc->default_ram_id = "riscv.K230.ram"; /* DDR */
    mc->default_ram_size = memmap[K230_DEV_DDRC].size;
}

static const TypeInfo k230_machine_typeinfo = {
    .name       = TYPE_RISCV_K230_MACHINE,
    .parent     = TYPE_MACHINE,
    .class_init = k230_machine_class_init,
    .instance_init = k230_machine_instance_init,
    .instance_size = sizeof(K230MachineState),
    .interfaces = riscv64_machine_interfaces,
};

static void k230_machine_init_register_types(void)
{
    type_register_static(&k230_machine_typeinfo);
}

type_init(k230_machine_init_register_types)
