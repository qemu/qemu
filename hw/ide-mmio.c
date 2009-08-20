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
#include "hw.h"
#include "block.h"
#include "block_int.h"
#include "sysemu.h"
#include "dma.h"
#include "ide-internal.h"

/***********************************************************/
/* MMIO based ide port
 * This emulates IDE device connected directly to the CPU bus without
 * dedicated ide controller, which is often seen on embedded boards.
 */

typedef struct {
    IDEBus *bus;
    int shift;
} MMIOState;

static uint32_t mmio_ide_read (void *opaque, target_phys_addr_t addr)
{
    MMIOState *s = (MMIOState*)opaque;
    IDEBus *bus = s->bus;
    addr >>= s->shift;
    if (addr & 7)
        return ide_ioport_read(bus, addr);
    else
        return ide_data_readw(bus, 0);
}

static void mmio_ide_write (void *opaque, target_phys_addr_t addr,
	uint32_t val)
{
    MMIOState *s = (MMIOState*)opaque;
    IDEBus *bus = s->bus;
    addr >>= s->shift;
    if (addr & 7)
        ide_ioport_write(bus, addr, val);
    else
        ide_data_writew(bus, 0, val);
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
    MMIOState *s= (MMIOState*)opaque;
    IDEBus *bus = s->bus;
    return ide_status_read(bus, 0);
}

static void mmio_ide_cmd_write (void *opaque, target_phys_addr_t addr,
	uint32_t val)
{
    MMIOState *s = (MMIOState*)opaque;
    IDEBus *bus = s->bus;
    ide_cmd_write(bus, 0, val);
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

static void mmio_ide_save(QEMUFile* f, void *opaque)
{
    MMIOState *s = opaque;

    idebus_save(f, s->bus);
    ide_save(f, &s->bus->ifs[0]);
    ide_save(f, &s->bus->ifs[1]);
}

static int mmio_ide_load(QEMUFile* f, void *opaque, int version_id)
{
    MMIOState *s = opaque;

    idebus_load(f, s->bus, version_id);
    ide_load(f, &s->bus->ifs[0], version_id);
    ide_load(f, &s->bus->ifs[1], version_id);
    return 0;
}

void mmio_ide_init (target_phys_addr_t membase, target_phys_addr_t membase2,
                    qemu_irq irq, int shift,
                    BlockDriverState *hd0, BlockDriverState *hd1)
{
    MMIOState *s = qemu_mallocz(sizeof(MMIOState));
    IDEBus *bus = qemu_mallocz(sizeof(*bus));
    int mem1, mem2;

    ide_init2(bus, hd0, hd1, irq);

    s->bus = bus;
    s->shift = shift;

    mem1 = cpu_register_io_memory(mmio_ide_reads, mmio_ide_writes, s);
    mem2 = cpu_register_io_memory(mmio_ide_status, mmio_ide_cmd, s);
    cpu_register_physical_memory(membase, 16 << shift, mem1);
    cpu_register_physical_memory(membase2, 2 << shift, mem2);
    register_savevm("mmio-ide", 0, 3, mmio_ide_save, mmio_ide_load, s);
}

