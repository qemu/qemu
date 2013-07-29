/*
 * ARMV7M System emulation.
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "hw/sysbus.h"
#include "hw/arm/arm.h"
#include "hw/loader.h"
#include "elf.h"
#include "sysemu/qtest.h"
#include "qemu/error-report.h"

/* Bitbanded IO.  Each word corresponds to a single bit.  */

/* Get the byte address of the real memory for a bitband access.  */
static inline uint32_t bitband_addr(void * opaque, uint32_t addr)
{
    uint32_t res;

    res = *(uint32_t *)opaque;
    res |= (addr & 0x1ffffff) >> 5;
    return res;

}

static uint32_t bitband_readb(void *opaque, hwaddr offset)
{
    uint8_t v;
    cpu_physical_memory_read(bitband_addr(opaque, offset), &v, 1);
    return (v & (1 << ((offset >> 2) & 7))) != 0;
}

static void bitband_writeb(void *opaque, hwaddr offset,
                           uint32_t value)
{
    uint32_t addr;
    uint8_t mask;
    uint8_t v;
    addr = bitband_addr(opaque, offset);
    mask = (1 << ((offset >> 2) & 7));
    cpu_physical_memory_read(addr, &v, 1);
    if (value & 1)
        v |= mask;
    else
        v &= ~mask;
    cpu_physical_memory_write(addr, &v, 1);
}

static uint32_t bitband_readw(void *opaque, hwaddr offset)
{
    uint32_t addr;
    uint16_t mask;
    uint16_t v;
    addr = bitband_addr(opaque, offset) & ~1;
    mask = (1 << ((offset >> 2) & 15));
    mask = tswap16(mask);
    cpu_physical_memory_read(addr, &v, 2);
    return (v & mask) != 0;
}

static void bitband_writew(void *opaque, hwaddr offset,
                           uint32_t value)
{
    uint32_t addr;
    uint16_t mask;
    uint16_t v;
    addr = bitband_addr(opaque, offset) & ~1;
    mask = (1 << ((offset >> 2) & 15));
    mask = tswap16(mask);
    cpu_physical_memory_read(addr, &v, 2);
    if (value & 1)
        v |= mask;
    else
        v &= ~mask;
    cpu_physical_memory_write(addr, &v, 2);
}

static uint32_t bitband_readl(void *opaque, hwaddr offset)
{
    uint32_t addr;
    uint32_t mask;
    uint32_t v;
    addr = bitband_addr(opaque, offset) & ~3;
    mask = (1 << ((offset >> 2) & 31));
    mask = tswap32(mask);
    cpu_physical_memory_read(addr, &v, 4);
    return (v & mask) != 0;
}

static void bitband_writel(void *opaque, hwaddr offset,
                           uint32_t value)
{
    uint32_t addr;
    uint32_t mask;
    uint32_t v;
    addr = bitband_addr(opaque, offset) & ~3;
    mask = (1 << ((offset >> 2) & 31));
    mask = tswap32(mask);
    cpu_physical_memory_read(addr, &v, 4);
    if (value & 1)
        v |= mask;
    else
        v &= ~mask;
    cpu_physical_memory_write(addr, &v, 4);
}

static const MemoryRegionOps bitband_ops = {
    .old_mmio = {
        .read = { bitband_readb, bitband_readw, bitband_readl, },
        .write = { bitband_writeb, bitband_writew, bitband_writel, },
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

#define TYPE_BITBAND "ARM,bitband-memory"
#define BITBAND(obj) OBJECT_CHECK(BitBandState, (obj), TYPE_BITBAND)

typedef struct {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    uint32_t base;
} BitBandState;

static int bitband_init(SysBusDevice *dev)
{
    BitBandState *s = BITBAND(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &bitband_ops, &s->base,
                          "bitband", 0x02000000);
    sysbus_init_mmio(dev, &s->iomem);
    return 0;
}

static void armv7m_bitband_init(void)
{
    DeviceState *dev;

    dev = qdev_create(NULL, TYPE_BITBAND);
    qdev_prop_set_uint32(dev, "base", 0x20000000);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, 0x22000000);

    dev = qdev_create(NULL, TYPE_BITBAND);
    qdev_prop_set_uint32(dev, "base", 0x40000000);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, 0x42000000);
}

/* Board init.  */

static void armv7m_reset(void *opaque)
{
    ARMCPU *cpu = opaque;

    cpu_reset(CPU(cpu));
}

/* Init CPU and memory for a v7-M based board.
   flash_size and sram_size are in kb.
   Returns the NVIC array.  */

qemu_irq *armv7m_init(MemoryRegion *address_space_mem,
                      int flash_size, int sram_size,
                      const char *kernel_filename, const char *cpu_model)
{
    ARMCPU *cpu;
    CPUARMState *env;
    DeviceState *nvic;
    /* FIXME: make this local state.  */
    static qemu_irq pic[64];
    int image_size;
    uint64_t entry;
    uint64_t lowaddr;
    int i;
    int big_endian;
    MemoryRegion *sram = g_new(MemoryRegion, 1);
    MemoryRegion *flash = g_new(MemoryRegion, 1);
    MemoryRegion *hack = g_new(MemoryRegion, 1);

    flash_size *= 1024;
    sram_size *= 1024;

    if (cpu_model == NULL) {
	cpu_model = "cortex-m3";
    }
    cpu = cpu_arm_init(cpu_model);
    if (cpu == NULL) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }
    env = &cpu->env;

#if 0
    /* > 32Mb SRAM gets complicated because it overlaps the bitband area.
       We don't have proper commandline options, so allocate half of memory
       as SRAM, up to a maximum of 32Mb, and the rest as code.  */
    if (ram_size > (512 + 32) * 1024 * 1024)
        ram_size = (512 + 32) * 1024 * 1024;
    sram_size = (ram_size / 2) & TARGET_PAGE_MASK;
    if (sram_size > 32 * 1024 * 1024)
        sram_size = 32 * 1024 * 1024;
    code_size = ram_size - sram_size;
#endif

    /* Flash programming is done via the SCU, so pretend it is ROM.  */
    memory_region_init_ram(flash, NULL, "armv7m.flash", flash_size);
    vmstate_register_ram_global(flash);
    memory_region_set_readonly(flash, true);
    memory_region_add_subregion(address_space_mem, 0, flash);
    memory_region_init_ram(sram, NULL, "armv7m.sram", sram_size);
    vmstate_register_ram_global(sram);
    memory_region_add_subregion(address_space_mem, 0x20000000, sram);
    armv7m_bitband_init();

    nvic = qdev_create(NULL, "armv7m_nvic");
    env->nvic = nvic;
    qdev_init_nofail(nvic);
    sysbus_connect_irq(SYS_BUS_DEVICE(nvic), 0,
                       qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_IRQ));
    for (i = 0; i < 64; i++) {
        pic[i] = qdev_get_gpio_in(nvic, i);
    }

#ifdef TARGET_WORDS_BIGENDIAN
    big_endian = 1;
#else
    big_endian = 0;
#endif

    if (!kernel_filename && !qtest_enabled()) {
        fprintf(stderr, "Guest image must be specified (using -kernel)\n");
        exit(1);
    }

    if (kernel_filename) {
        image_size = load_elf(kernel_filename, NULL, NULL, &entry, &lowaddr,
                              NULL, big_endian, ELF_MACHINE, 1);
        if (image_size < 0) {
            image_size = load_image_targphys(kernel_filename, 0, flash_size);
            lowaddr = 0;
        }
        if (image_size < 0) {
            error_report("Could not load kernel '%s'", kernel_filename);
            exit(1);
        }
    }

    /* Hack to map an additional page of ram at the top of the address
       space.  This stops qemu complaining about executing code outside RAM
       when returning from an exception.  */
    memory_region_init_ram(hack, NULL, "armv7m.hack", 0x1000);
    vmstate_register_ram_global(hack);
    memory_region_add_subregion(address_space_mem, 0xfffff000, hack);

    qemu_register_reset(armv7m_reset, cpu);
    return pic;
}

static Property bitband_properties[] = {
    DEFINE_PROP_UINT32("base", BitBandState, base, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void bitband_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = bitband_init;
    dc->props = bitband_properties;
}

static const TypeInfo bitband_info = {
    .name          = TYPE_BITBAND,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BitBandState),
    .class_init    = bitband_class_init,
};

static void armv7m_register_types(void)
{
    type_register_static(&bitband_info);
}

type_init(armv7m_register_types)
