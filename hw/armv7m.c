/*
 * ARMV7M System emulation.
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the GPL.
 */

#include "sysbus.h"
#include "arm-misc.h"
#include "sysemu.h"

/* Bitbanded IO.  Each word corresponds to a single bit.  */

/* Get the byte address of the real memory for a bitband acess.  */
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

static CPUReadMemoryFunc *bitband_readfn[] = {
   bitband_readb,
   bitband_readw,
   bitband_readl
};

static CPUWriteMemoryFunc *bitband_writefn[] = {
   bitband_writeb,
   bitband_writew,
   bitband_writel
};

static void armv7m_bitband_init(void)
{
    int iomemtype;
    static uint32_t bitband1_offset = 0x20000000;
    static uint32_t bitband2_offset = 0x40000000;

    iomemtype = cpu_register_io_memory(0, bitband_readfn, bitband_writefn,
                                       &bitband1_offset);
    cpu_register_physical_memory(0x22000000, 0x02000000, iomemtype);
    iomemtype = cpu_register_io_memory(0, bitband_readfn, bitband_writefn,
                                       &bitband2_offset);
    cpu_register_physical_memory(0x42000000, 0x02000000, iomemtype);
}

/* Board init.  */
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
    uint32_t pc;
    int image_size;
    uint64_t entry;
    uint64_t lowaddr;
    int i;

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
                                 qemu_ram_alloc(flash_size) | IO_MEM_ROM);
    cpu_register_physical_memory(0x20000000, sram_size,
                                 qemu_ram_alloc(sram_size) | IO_MEM_RAM);
    armv7m_bitband_init();

    nvic = qdev_create(NULL, "armv7m_nvic");
    qdev_set_prop_ptr(nvic, "cpu", env);
    qdev_init(nvic);
    cpu_pic = arm_pic_init_cpu(env);
    sysbus_connect_irq(sysbus_from_qdev(nvic), 0, cpu_pic[ARM_PIC_CPU_IRQ]);
    for (i = 0; i < 64; i++) {
        pic[i] = qdev_get_irq_sink(nvic, i);
    }

    image_size = load_elf(kernel_filename, 0, &entry, &lowaddr, NULL);
    if (image_size < 0) {
        image_size = load_image_targphys(kernel_filename, 0, flash_size);
	lowaddr = 0;
    }
    if (image_size < 0) {
        fprintf(stderr, "qemu: could not load kernel '%s'\n",
                kernel_filename);
        exit(1);
    }

    /* If the image was loaded at address zero then assume it is a
       regular ROM image and perform the normal CPU reset sequence.
       Otherwise jump directly to the entry point.  */
    if (lowaddr == 0) {
	env->regs[13] = ldl_phys(0);
	pc = ldl_phys(4);
    } else {
	pc = entry;
    }
    env->thumb = pc & 1;
    env->regs[15] = pc & ~1;

    /* Hack to map an additional page of ram at the top of the address
       space.  This stops qemu complaining about executing code outside RAM
       when returning from an exception.  */
    cpu_register_physical_memory(0xfffff000, 0x1000,
                                 qemu_ram_alloc(0x1000) | IO_MEM_RAM);

    return pic;
}
