/*
 * QEMU Floppy disk emulator (Intel 82078)
 *
 * Copyright (c) 2003, 2007 Jocelyn Mayer
 * Copyright (c) 2008 Hervé Poussineau
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
/*
 * The controller is used in Sun4m systems in a slightly different
 * way. There are changes in DOR register and DMA is not available.
 */

#include "qemu/osdep.h"
#include "hw/block/fdc.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "hw/acpi/aml-build.h"
#include "hw/irq.h"
#include "hw/isa/isa.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "hw/block/block.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"
#include "sysemu/sysemu.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "trace.h"
#include "qom/object.h"
#include "fdc-internal.h"

OBJECT_DECLARE_SIMPLE_TYPE(FDCtrlISABus, ISA_FDC)

struct FDCtrlISABus {
    /*< private >*/
    ISADevice parent_obj;
    /*< public >*/

    uint32_t iobase;
    uint32_t irq;
    uint32_t dma;
    struct FDCtrl state;
    int32_t bootindexA;
    int32_t bootindexB;
};

static void fdctrl_external_reset_isa(DeviceState *d)
{
    FDCtrlISABus *isa = ISA_FDC(d);
    FDCtrl *s = &isa->state;

    fdctrl_reset(s, 0);
}

void isa_fdc_init_drives(ISADevice *fdc, DriveInfo **fds)
{
    fdctrl_init_drives(&ISA_FDC(fdc)->state.bus, fds);
}

static const MemoryRegionPortio fdc_portio_list[] = {
    { 1, 5, 1, .read = fdctrl_read, .write = fdctrl_write },
    { 7, 1, 1, .read = fdctrl_read, .write = fdctrl_write },
    PORTIO_END_OF_LIST(),
};

static void isabus_fdc_realize(DeviceState *dev, Error **errp)
{
    ISADevice *isadev = ISA_DEVICE(dev);
    FDCtrlISABus *isa = ISA_FDC(dev);
    FDCtrl *fdctrl = &isa->state;
    Error *err = NULL;

    isa_register_portio_list(isadev, &fdctrl->portio_list,
                             isa->iobase, fdc_portio_list, fdctrl,
                             "fdc");

    isa_init_irq(isadev, &fdctrl->irq, isa->irq);
    fdctrl->dma_chann = isa->dma;
    if (fdctrl->dma_chann != -1) {
        IsaDmaClass *k;
        fdctrl->dma = isa_get_dma(isa_bus_from_device(isadev), isa->dma);
        if (!fdctrl->dma) {
            error_setg(errp, "ISA controller does not support DMA");
            return;
        }
        k = ISADMA_GET_CLASS(fdctrl->dma);
        k->register_channel(fdctrl->dma, fdctrl->dma_chann,
                            &fdctrl_transfer_handler, fdctrl);
    }

    qdev_set_legacy_instance_id(dev, isa->iobase, 2);

    fdctrl_realize_common(dev, fdctrl, &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }
}

FloppyDriveType isa_fdc_get_drive_type(ISADevice *fdc, int i)
{
    FDCtrlISABus *isa = ISA_FDC(fdc);

    return isa->state.drives[i].drive;
}

static void isa_fdc_get_drive_max_chs(FloppyDriveType type, uint8_t *maxc,
                                      uint8_t *maxh, uint8_t *maxs)
{
    const FDFormat *fdf;

    *maxc = *maxh = *maxs = 0;
    for (fdf = fd_formats; fdf->drive != FLOPPY_DRIVE_TYPE_NONE; fdf++) {
        if (fdf->drive != type) {
            continue;
        }
        if (*maxc < fdf->max_track) {
            *maxc = fdf->max_track;
        }
        if (*maxh < fdf->max_head) {
            *maxh = fdf->max_head;
        }
        if (*maxs < fdf->last_sect) {
            *maxs = fdf->last_sect;
        }
    }
    (*maxc)--;
}

static Aml *build_fdinfo_aml(int idx, FloppyDriveType type)
{
    Aml *dev, *fdi;
    uint8_t maxc, maxh, maxs;

    isa_fdc_get_drive_max_chs(type, &maxc, &maxh, &maxs);

    dev = aml_device("FLP%c", 'A' + idx);

    aml_append(dev, aml_name_decl("_ADR", aml_int(idx)));

    fdi = aml_package(16);
    aml_append(fdi, aml_int(idx));  /* Drive Number */
    aml_append(fdi,
        aml_int(cmos_get_fd_drive_type(type)));  /* Device Type */
    /*
     * the values below are the limits of the drive, and are thus independent
     * of the inserted media
     */
    aml_append(fdi, aml_int(maxc));  /* Maximum Cylinder Number */
    aml_append(fdi, aml_int(maxs));  /* Maximum Sector Number */
    aml_append(fdi, aml_int(maxh));  /* Maximum Head Number */
    /*
     * SeaBIOS returns the below values for int 0x13 func 0x08 regardless of
     * the drive type, so shall we
     */
    aml_append(fdi, aml_int(0xAF));  /* disk_specify_1 */
    aml_append(fdi, aml_int(0x02));  /* disk_specify_2 */
    aml_append(fdi, aml_int(0x25));  /* disk_motor_wait */
    aml_append(fdi, aml_int(0x02));  /* disk_sector_siz */
    aml_append(fdi, aml_int(0x12));  /* disk_eot */
    aml_append(fdi, aml_int(0x1B));  /* disk_rw_gap */
    aml_append(fdi, aml_int(0xFF));  /* disk_dtl */
    aml_append(fdi, aml_int(0x6C));  /* disk_formt_gap */
    aml_append(fdi, aml_int(0xF6));  /* disk_fill */
    aml_append(fdi, aml_int(0x0F));  /* disk_head_sttl */
    aml_append(fdi, aml_int(0x08));  /* disk_motor_strt */

    aml_append(dev, aml_name_decl("_FDI", fdi));
    return dev;
}

int cmos_get_fd_drive_type(FloppyDriveType fd0)
{
    int val;

    switch (fd0) {
    case FLOPPY_DRIVE_TYPE_144:
        /* 1.44 Mb 3"5 drive */
        val = 4;
        break;
    case FLOPPY_DRIVE_TYPE_288:
        /* 2.88 Mb 3"5 drive */
        val = 5;
        break;
    case FLOPPY_DRIVE_TYPE_120:
        /* 1.2 Mb 5"5 drive */
        val = 2;
        break;
    case FLOPPY_DRIVE_TYPE_NONE:
    default:
        val = 0;
        break;
    }
    return val;
}

static void fdc_isa_build_aml(ISADevice *isadev, Aml *scope)
{
    Aml *dev;
    Aml *crs;
    int i;

#define ACPI_FDE_MAX_FD 4
    uint32_t fde_buf[5] = {
        0, 0, 0, 0,     /* presence of floppy drives #0 - #3 */
        cpu_to_le32(2)  /* tape presence (2 == never present) */
    };

    crs = aml_resource_template();
    aml_append(crs, aml_io(AML_DECODE16, 0x03F2, 0x03F2, 0x00, 0x04));
    aml_append(crs, aml_io(AML_DECODE16, 0x03F7, 0x03F7, 0x00, 0x01));
    aml_append(crs, aml_irq_no_flags(6));
    aml_append(crs,
        aml_dma(AML_COMPATIBILITY, AML_NOTBUSMASTER, AML_TRANSFER8, 2));

    dev = aml_device("FDC0");
    aml_append(dev, aml_name_decl("_HID", aml_eisaid("PNP0700")));
    aml_append(dev, aml_name_decl("_CRS", crs));

    for (i = 0; i < MIN(MAX_FD, ACPI_FDE_MAX_FD); i++) {
        FloppyDriveType type = isa_fdc_get_drive_type(isadev, i);

        if (type < FLOPPY_DRIVE_TYPE_NONE) {
            fde_buf[i] = cpu_to_le32(1);  /* drive present */
            aml_append(dev, build_fdinfo_aml(i, type));
        }
    }
    aml_append(dev, aml_name_decl("_FDE",
               aml_buffer(sizeof(fde_buf), (uint8_t *)fde_buf)));

    aml_append(scope, dev);
}

static const VMStateDescription vmstate_isa_fdc = {
    .name = "fdc",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(state, FDCtrlISABus, 0, vmstate_fdc, FDCtrl),
        VMSTATE_END_OF_LIST()
    }
};

static Property isa_fdc_properties[] = {
    DEFINE_PROP_UINT32("iobase", FDCtrlISABus, iobase, 0x3f0),
    DEFINE_PROP_UINT32("irq", FDCtrlISABus, irq, 6),
    DEFINE_PROP_UINT32("dma", FDCtrlISABus, dma, 2),
    DEFINE_PROP_SIGNED("fdtypeA", FDCtrlISABus, state.qdev_for_drives[0].type,
                        FLOPPY_DRIVE_TYPE_AUTO, qdev_prop_fdc_drive_type,
                        FloppyDriveType),
    DEFINE_PROP_SIGNED("fdtypeB", FDCtrlISABus, state.qdev_for_drives[1].type,
                        FLOPPY_DRIVE_TYPE_AUTO, qdev_prop_fdc_drive_type,
                        FloppyDriveType),
    DEFINE_PROP_SIGNED("fallback", FDCtrlISABus, state.fallback,
                        FLOPPY_DRIVE_TYPE_288, qdev_prop_fdc_drive_type,
                        FloppyDriveType),
    DEFINE_PROP_END_OF_LIST(),
};

static void isabus_fdc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ISADeviceClass *isa = ISA_DEVICE_CLASS(klass);

    dc->desc = "virtual floppy controller";
    dc->realize = isabus_fdc_realize;
    dc->fw_name = "fdc";
    dc->reset = fdctrl_external_reset_isa;
    dc->vmsd = &vmstate_isa_fdc;
    isa->build_aml = fdc_isa_build_aml;
    device_class_set_props(dc, isa_fdc_properties);
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static void isabus_fdc_instance_init(Object *obj)
{
    FDCtrlISABus *isa = ISA_FDC(obj);

    device_add_bootindex_property(obj, &isa->bootindexA,
                                  "bootindexA", "/floppy@0",
                                  DEVICE(obj));
    device_add_bootindex_property(obj, &isa->bootindexB,
                                  "bootindexB", "/floppy@1",
                                  DEVICE(obj));
}

static const TypeInfo isa_fdc_info = {
    .name          = TYPE_ISA_FDC,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(FDCtrlISABus),
    .class_init    = isabus_fdc_class_init,
    .instance_init = isabus_fdc_instance_init,
};

static void isa_fdc_register_types(void)
{
    type_register_static(&isa_fdc_info);
}

type_init(isa_fdc_register_types)
