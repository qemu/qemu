/*
 * QEMU IDE Emulation: ISA Bus support.
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
#include <hw/pc.h>
#include "block.h"
#include "block_int.h"
#include "sysemu.h"
#include "dma.h"

#include <hw/ide/internal.h>

/***********************************************************/
/* ISA IDE definitions */

typedef struct ISAIDEState {
    IDEBus *bus;
} ISAIDEState;

static void isa_ide_save(QEMUFile* f, void *opaque)
{
    ISAIDEState *s = opaque;

    idebus_save(f, s->bus);
    ide_save(f, &s->bus->ifs[0]);
    ide_save(f, &s->bus->ifs[1]);
}

static int isa_ide_load(QEMUFile* f, void *opaque, int version_id)
{
    ISAIDEState *s = opaque;

    idebus_load(f, s->bus, version_id);
    ide_load(f, &s->bus->ifs[0], version_id);
    ide_load(f, &s->bus->ifs[1], version_id);
    return 0;
}

void isa_ide_init(int iobase, int iobase2, qemu_irq irq,
                  BlockDriverState *hd0, BlockDriverState *hd1)
{
    ISAIDEState *s;

    s = qemu_mallocz(sizeof(*s));
    s->bus = qemu_mallocz(sizeof(IDEBus));

    ide_init2(s->bus, hd0, hd1, irq);
    ide_init_ioport(s->bus, iobase, iobase2);
    register_savevm("isa-ide", 0, 3, isa_ide_save, isa_ide_load, s);
}
