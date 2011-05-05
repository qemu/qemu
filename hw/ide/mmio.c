/*
 * QEMU IDE Emulation: mmio support (for embedded).
 *
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2006 Openedhand Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <hw/hw.h>
#include "block.h"
#include "block_int.h"
#include "dma.h"

#include <hw/ide/internal.h>

/***********************************************************/
/* MMIO based ide port
 * This emulates IDE device connected directly to the CPU bus without
 * dedicated ide controller, which is often seen on embedded boards.
 */

typedef struct {
    IDEBus bus;
    int shift;
} MMIOState;

static void mmio_ide_reset(void *opaque)
{
    MMIOState *s = opaque;

    ide_bus_reset(&s->bus);
}

static uint32_t mmio_ide_read (void *opaque, target_phys_addr_t addr)
{
    MMIOState *s = opaque;
    addr >>= s->shift;
    if (addr & 7)
        return ide_ioport_read(&s->bus, addr);
    else
        return ide_data_readw(&s->bus, 0);
}

static void mmio_ide_write (void *opaque, target_phys_addr_t addr,
	uint32_t val)
{
    MMIOState *s = opaque;
    addr >>= s->shift;
    if (addr & 7)
        ide_ioport_write(&s->bus, addr, val);
    else
        ide_data_writew(&s->bus, 0, val);
}

static CPUReadMemoryFunc * const mmio_ide_reads[] = {
    mmio_ide_read,
    mmio_ide_read,
    mmio_ide_read,
};

static CPUWriteMemoryFunc * const mmio_ide_writes[] = {
    mmio_ide_write,
    mmio_ide_write,
    mmio_ide_write,
};

static uint32_t mmio_ide_status_read (void *opaque, target_phys_addr_t addr)
{
    MMIOState *s= opaque;
    return ide_status_read(&s->bus, 0);
}

static void mmio_ide_cmd_write (void *opaque, target_phys_addr_t addr,
	uint32_t val)
{
    MMIOState *s = opaque;
    ide_cmd_write(&s->bus, 0, val);
}

static CPUReadMemoryFunc * const mmio_ide_status[] = {
    mmio_ide_status_read,
    mmio_ide_status_read,
    mmio_ide_status_read,
};

static CPUWriteMemoryFunc * const mmio_ide_cmd[] = {
    mmio_ide_cmd_write,
    mmio_ide_cmd_write,
    mmio_ide_cmd_write,
};

static const VMStateDescription vmstate_ide_mmio = {
    .name = "mmio-ide",
    .version_id = 3,
    .minimum_version_id = 0,
    .minimum_version_id_old = 0,
    .fields      = (VMStateField []) {
        VMSTATE_IDE_BUS(bus, MMIOState),
        VMSTATE_IDE_DRIVES(bus.ifs, MMIOState),
        VMSTATE_END_OF_LIST()
    }
};

void mmio_ide_init (target_phys_addr_t membase, target_phys_addr_t membase2,
                    qemu_irq irq, int shift,
                    DriveInfo *hd0, DriveInfo *hd1)
{
    MMIOState *s = qemu_mallocz(sizeof(MMIOState));
    int mem1, mem2;

    ide_init2_with_non_qdev_drives(&s->bus, hd0, hd1, irq);

    s->shift = shift;

    mem1 = cpu_register_io_memory(mmio_ide_reads, mmio_ide_writes, s,
                                  DEVICE_NATIVE_ENDIAN);
    mem2 = cpu_register_io_memory(mmio_ide_status, mmio_ide_cmd, s,
                                  DEVICE_NATIVE_ENDIAN);
    cpu_register_physical_memory(membase, 16 << shift, mem1);
    cpu_register_physical_memory(membase2, 2 << shift, mem2);
    vmstate_register(NULL, 0, &vmstate_ide_mmio, s);
    qemu_register_reset(mmio_ide_reset, s);
}

