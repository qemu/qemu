/*
 * STM32 Microcontroller Flash Memory
 * The STM32 family stores its Flash memory at some base address in memory
 * (0x08000000 for medium density devices), and then aliases it to the
 * boot memory space, which starts at 0x00000000 (the System Memory can also
 * be aliased to 0x00000000, but this is not implemented here).  The processor
 * executes the code in the aliased memory at 0x00000000, but we need to
 * implement the "real" flash memory as well.  This "real" flash memory will
 * pass reads through to the memory at 0x00000000, which is where QEMU loads
 * the executable image.  Note that this is opposite of real hardware, where the
 * memory at 0x00000000 passes reads through the "real" flash memory, but it
 * works the same either way.
 *
 * Copyright (C) 2010 Andre Beckus
 *
 * Implementation based on ST Microelectronics "RM0008 Reference Manual Rev 10"
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "stm32.h"
//#include "exec-memory.h"

typedef struct {
    SysBusDevice busdev;
    MemoryRegion iomem;
    uint32_t size;
} Stm32Flash;

static uint64_t stm32_flash_read(void *opaque, target_phys_addr_t offset,
                          unsigned size)
{
    uint32_t v = 0;

    /* Pass the read through to base memory at 0x00000000 where QEMU has loaded
     * the executable image.
     */
    cpu_physical_memory_read(offset, (uint8_t *)&v, size);
    return v;
}

static void stm32_flash_write(void *opaque, target_phys_addr_t offset,
                       uint64_t value, unsigned size)
{
    /* Flash is treated as read only memory. */
    hw_error("stm32_flash: Attempted to write flash memory (read only)");
}

static const MemoryRegionOps stm32_flash_ops = {
    .read = stm32_flash_read,
    .write = stm32_flash_write,
    .endianness = DEVICE_NATIVE_ENDIAN
};

static int stm32_flash_init(SysBusDevice *dev)
{
    Stm32Flash *s = FROM_SYSBUS(Stm32Flash, dev);

    memory_region_init_io(
            &s->iomem,
            &stm32_flash_ops,
            &s,
            "stm32_flash",
            s->size);
    sysbus_init_mmio(dev, &s->iomem);
    return 0;
}

static Property stm32_flash_properties[] = {
    DEFINE_PROP_UINT32("size", Stm32Flash, size, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void stm32_flash_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = stm32_flash_init;
    dc->props = stm32_flash_properties;
}

static TypeInfo stm32_flash_info = {
    .name          = "stm32_flash",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Stm32Flash),
    .class_init    = stm32_flash_class_init,
};

static void stm32_flash_register_types(void)
{
    type_register_static(&stm32_flash_info);
}

type_init(stm32_flash_register_types)
