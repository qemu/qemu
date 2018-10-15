/*
 * QEMU Cirrus CLGD 54xx VGA Emulator, ISA bus support
 *
 * Copyright (c) 2004 Fabrice Bellard
 * Copyright (c) 2004 Makoto Suzuki (suzu)
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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/loader.h"
#include "hw/isa/isa.h"
#include "cirrus_vga_internal.h"

#define TYPE_ISA_CIRRUS_VGA "isa-cirrus-vga"
#define ISA_CIRRUS_VGA(obj) \
    OBJECT_CHECK(ISACirrusVGAState, (obj), TYPE_ISA_CIRRUS_VGA)

typedef struct ISACirrusVGAState {
    ISADevice parent_obj;

    CirrusVGAState cirrus_vga;
} ISACirrusVGAState;

static void isa_cirrus_vga_realizefn(DeviceState *dev, Error **errp)
{
    ISADevice *isadev = ISA_DEVICE(dev);
    ISACirrusVGAState *d = ISA_CIRRUS_VGA(dev);
    VGACommonState *s = &d->cirrus_vga.vga;

    /* follow real hardware, cirrus card emulated has 4 MB video memory.
       Also accept 8 MB/16 MB for backward compatibility. */
    if (s->vram_size_mb != 4 && s->vram_size_mb != 8 &&
        s->vram_size_mb != 16) {
        error_setg(errp, "Invalid cirrus_vga ram size '%u'",
                   s->vram_size_mb);
        return;
    }
    s->global_vmstate = true;
    vga_common_init(s, OBJECT(dev));
    cirrus_init_common(&d->cirrus_vga, OBJECT(dev), CIRRUS_ID_CLGD5430, 0,
                       isa_address_space(isadev),
                       isa_address_space_io(isadev));
    s->con = graphic_console_init(dev, 0, s->hw_ops, s);
    rom_add_vga(VGABIOS_CIRRUS_FILENAME);
    /* XXX ISA-LFB support */
    /* FIXME not qdev yet */
}

static Property isa_cirrus_vga_properties[] = {
    DEFINE_PROP_UINT32("vgamem_mb", struct ISACirrusVGAState,
                       cirrus_vga.vga.vram_size_mb, 4),
    DEFINE_PROP_BOOL("blitter", struct ISACirrusVGAState,
                     cirrus_vga.enable_blitter, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void isa_cirrus_vga_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd  = &vmstate_cirrus_vga;
    dc->realize = isa_cirrus_vga_realizefn;
    dc->props = isa_cirrus_vga_properties;
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static const TypeInfo isa_cirrus_vga_info = {
    .name          = TYPE_ISA_CIRRUS_VGA,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(ISACirrusVGAState),
    .class_init = isa_cirrus_vga_class_init,
};

static void cirrus_vga_isa_register_types(void)
{
    type_register_static(&isa_cirrus_vga_info);
}

type_init(cirrus_vga_isa_register_types)
