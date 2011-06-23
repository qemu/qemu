/*
 * ARMV7M System emulation.
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "sysbus.h"
#include "arm-misc.h"
#include "loader.h"
#include "elf.h"

/* Bitbanded IO.  Each word corresponds to a single bit.  */

/* Get the byte address of the real memory for a bitband access.  */
static inline uint32_t bitband_addr(void * opaque, uint32_t addr)
{
    uint32_t res;

    res = *(uint32_t *)opaque;
    res |= (addr & 0x1ffffff) >> 5;
    return res;

}

static uint32_t bitband_readb(void *opaque, target_phys_addr_t offset)
{
    uint8_t v;
    cpu_physical_memory_read(bitband_addr(opaque, offset), &v, 1);
    return (v & (1 << ((offset >> 2) & 7))) != 0;
}

static void bitband_writeb(void *opaque, target_phys_addr_t offset,
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

static uint32_t bitband_readw(void *opaque, target_phys_addr_t offset)
{
    uint32_t addr;
    uint16_t mask;
    uint16_t v;
    addr = bitband_addr(opaque, offset) & ~1;
    mask = (1 << ((offset >> 2) & 15));
    mask = tswap16(mask);
    cpu_physical_memory_read(addr, (uint8_t *)&v, 2);
    return (v & mask) != 0;
}

static void bitband_writew(void *opaque, target_phys_addr_t offset,
                           uint32_t value)
{
    uint32_t addr;
    uint16_t mask;
    uint16_t v;
    addr = bitband_addr(opaque, offset) & ~1;
    mask = (1 << ((offset >> 2) & 15));
    mask = tswap16(mask);
    cpu_physical_memory_read(addr, (uint8_t *)&v, 2);
    if (value & 1)
        v |= mask;
    else
        v &= ~mask;
    cpu_physical_memory_write(addr, (uint8_t *)&v, 2);
}

static uint32_t bitband_readl(void *opaque, target_phys_addr_t offset)
{
    uint32_t addr;
    uint32_t mask;
    uint32_t v;
    addr = bitband_addr(opaque, offset) & ~3;
    mask = (1 << ((offset >> 2) & 31));
    mask = tswap32(mask);
    cpu_physical_memory_read(addr, (uint8_t *)&v, 4);
    return (v & mask) != 0;
}

static void bitband_writel(void *opaque, target_phys_addr_t offset,
                           uint32_t value)
{
    uint32_t addr;
    uint32_t mask;
    uint32_t v;
    addr = bitband_addr(opaque, offset) & ~3;
    mask = (1 << ((offset >> 2) & 31));
    mask = tswap32(mask);
    cpu_physical_memory_read(addr, (uint8_t *)&v, 4);
    if (value & 1)
        v |= mask;
    else
        v &= ~mask;
    cpu_physical_memory_write(addr, (uint8_t *)&v, 4);
}

static CPUReadMemoryFunc * const bitband_readfn[] = {
   bitband_readb,
   bitband_readw,
   bitband_readl
};

static CPUWriteMemoryFunc * const bitband_writefn[] = {
   bitband_writeb,
   bitband_writew,
   bitband_writel
};

typedef struct {
    SysBusDevice busdev;
    uint32_t base;
} BitBandState;

static int bitband_init(SysBusDevice *dev)
{
    BitBandState *s = FROM_SYSBUS(BitBandState, dev);
    int iomemtype;

    iomemtype = cpu_register_io_memory(bitband_readfn, bitband_writefn,
                                       &s->base, DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, 0x02000000, iomemtype);
    return 0;
}

static void armv7m_bitband_init(void)
{
    DeviceState *dev;

    dev = qdev_create(NULL, "ARM,bitband-memory");
    qdev_prop_set_uint32(dev, "base", 0x20000000);
    qdev_init_nofail(dev);
    sysbus_mmio_map(sysbus_from_qdev(dev), 0, 0x22000000);

    dev = qdev_create(NULL, "ARM,bitband-memory");
    qdev_prop_set_uint32(dev, "base", 0x40000000);
    qdev_init_nofail(dev);
    sysbus_mmio_map(sysbus_from_qdev(dev), 0, 0x42000000);
}

/* Board init.  */

static void armv7m_reset(void *opaque)
{
    cpu_reset((CPUState *)opaque);
}

/* Init CPU and memory for a v7-M based board.
   flash_size and sram_size are in kb.
   Returns the NVIC array.  */

qemu_irq *armv7m_init(int flash_size, int sram_size,
                      const char *kernel_filename, const char *cpu_model)
{
    CPUState *env;
    DeviceState *nvic;
    /* FIXME: make this local state.  */
    static qemu_irq pic[64];
    qemu_irq *cpu_pic;
    int image_size;
    uint64_t entry;
    uint64_t lowaddr;
    int i;
    int big_endian;

    flash_size *= 1024;
    sram_size *= 1024;

    if (!cpu_model)
	cpu_model = "cortex-m3";
    env = cpu_init(cpu_model);
    if (!env) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }

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
    cpu_register_physical_memory(0, flash_size,
                                 qemu_ram_alloc(NULL, "armv7m.flash",
                                                flash_size) | IO_MEM_ROM);
    cpu_register_physical_memory(0x20000000, sram_size,
                                 qemu_ram_alloc(NULL, "armv7m.sram",
                                                sram_size) | IO_MEM_RAM);
    armv7m_bitband_init();

    nvic = qdev_create(NULL, "armv7m_nvic");
    env->nvic = nvic;
    qdev_init_nofail(nvic);
    cpu_pic = arm_pic_init_cpu(env);
    sysbus_connect_irq(sysbus_from_qdev(nvic), 0, cpu_pic[ARM_PIC_CPU_IRQ]);
    for (i = 0; i < 64; i++) {
        pic[i] = qdev_get_gpio_in(nvic, i);
    }

#ifdef TARGET_WORDS_BIGENDIAN
    big_endian = 1;
#else
    big_endian = 0;
#endif

    image_size = load_elf(kernel_filename, NULL, NULL, &entry, &lowaddr,
                          NULL, big_endian, ELF_MACHINE, 1);
    if (image_size < 0) {
        image_size = load_image_targphys(kernel_filename, 0, flash_size);
	lowaddr = 0;
    }
    if (image_size < 0) {
        fprintf(stderr, "qemu: could not load kernel '%s'\n",
                kernel_filename);
        exit(1);
    }

    /* Hack to map an additional page of ram at the top of the address
       space.  This stops qemu complaining about executing code outside RAM
       when returning from an exception.  */
    cpu_register_physical_memory(0xfffff000, 0x1000,
                                 qemu_ram_alloc(NULL, "armv7m.hack", 
                                                0x1000) | IO_MEM_RAM);

    qemu_register_reset(armv7m_reset, env);
    return pic;
}

static SysBusDeviceInfo bitband_info = {
    .init = bitband_init,
    .qdev.name  = "ARM,bitband-memory",
    .qdev.size  = sizeof(BitBandState),
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("base", BitBandState, base, 0),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void armv7m_register_devices(void)
{
    sysbus_register_withprop(&bitband_info);
}

device_init(armv7m_register_devices)
