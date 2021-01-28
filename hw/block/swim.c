/*
 * QEMU Macintosh floppy disk controller emulator (SWIM)
 *
 * Copyright (c) 2014-2018 Laurent Vivier <laurent@vivier.eu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Only the basic support: it allows to switch from IWM (Integrated WOZ
 * Machine) mode to the SWIM mode and makes the linux driver happy.
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"
#include "sysemu/block-backend.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/block/block.h"
#include "hw/block/swim.h"
#include "hw/qdev-properties.h"

/* IWM registers */

#define IWM_PH0L                0
#define IWM_PH0H                1
#define IWM_PH1L                2
#define IWM_PH1H                3
#define IWM_PH2L                4
#define IWM_PH2H                5
#define IWM_PH3L                6
#define IWM_PH3H                7
#define IWM_MTROFF              8
#define IWM_MTRON               9
#define IWM_INTDRIVE            10
#define IWM_EXTDRIVE            11
#define IWM_Q6L                 12
#define IWM_Q6H                 13
#define IWM_Q7L                 14
#define IWM_Q7H                 15

/* SWIM registers */

#define SWIM_WRITE_DATA         0
#define SWIM_WRITE_MARK         1
#define SWIM_WRITE_CRC          2
#define SWIM_WRITE_PARAMETER    3
#define SWIM_WRITE_PHASE        4
#define SWIM_WRITE_SETUP        5
#define SWIM_WRITE_MODE0        6
#define SWIM_WRITE_MODE1        7

#define SWIM_READ_DATA          8
#define SWIM_READ_MARK          9
#define SWIM_READ_ERROR         10
#define SWIM_READ_PARAMETER     11
#define SWIM_READ_PHASE         12
#define SWIM_READ_SETUP         13
#define SWIM_READ_STATUS        14
#define SWIM_READ_HANDSHAKE     15

#define REG_SHIFT               9

#define SWIM_MODE_IWM  0
#define SWIM_MODE_SWIM 1

/* bits in phase register */

#define SWIM_SEEK_NEGATIVE   0x074
#define SWIM_STEP            0x071
#define SWIM_MOTOR_ON        0x072
#define SWIM_MOTOR_OFF       0x076
#define SWIM_INDEX           0x073
#define SWIM_EJECT           0x077
#define SWIM_SETMFM          0x171
#define SWIM_SETGCR          0x175
#define SWIM_RELAX           0x033
#define SWIM_LSTRB           0x008
#define SWIM_CA_MASK         0x077

/* Select values for swim_select and swim_readbit */

#define SWIM_READ_DATA_0     0x074
#define SWIM_TWOMEG_DRIVE    0x075
#define SWIM_SINGLE_SIDED    0x076
#define SWIM_DRIVE_PRESENT   0x077
#define SWIM_DISK_IN         0x170
#define SWIM_WRITE_PROT      0x171
#define SWIM_TRACK_ZERO      0x172
#define SWIM_TACHO           0x173
#define SWIM_READ_DATA_1     0x174
#define SWIM_MFM_MODE        0x175
#define SWIM_SEEK_COMPLETE   0x176
#define SWIM_ONEMEG_MEDIA    0x177

/* Bits in handshake register */

#define SWIM_MARK_BYTE       0x01
#define SWIM_CRC_ZERO        0x02
#define SWIM_RDDATA          0x04
#define SWIM_SENSE           0x08
#define SWIM_MOTEN           0x10
#define SWIM_ERROR           0x20
#define SWIM_DAT2BYTE        0x40
#define SWIM_DAT1BYTE        0x80

/* bits in setup register */

#define SWIM_S_INV_WDATA     0x01
#define SWIM_S_3_5_SELECT    0x02
#define SWIM_S_GCR           0x04
#define SWIM_S_FCLK_DIV2     0x08
#define SWIM_S_ERROR_CORR    0x10
#define SWIM_S_IBM_DRIVE     0x20
#define SWIM_S_GCR_WRITE     0x40
#define SWIM_S_TIMEOUT       0x80

/* bits in mode register */

#define SWIM_CLFIFO          0x01
#define SWIM_ENBL1           0x02
#define SWIM_ENBL2           0x04
#define SWIM_ACTION          0x08
#define SWIM_WRITE_MODE      0x10
#define SWIM_HEDSEL          0x20
#define SWIM_MOTON           0x80

static void fd_recalibrate(FDrive *drive)
{
}

static void swim_change_cb(void *opaque, bool load, Error **errp)
{
    FDrive *drive = opaque;

    if (!load) {
        blk_set_perm(drive->blk, 0, BLK_PERM_ALL, &error_abort);
    } else {
        if (!blkconf_apply_backend_options(drive->conf,
                                           !blk_supports_write_perm(drive->blk),
                                           false, errp)) {
            return;
        }
    }
}

static const BlockDevOps swim_block_ops = {
    .change_media_cb = swim_change_cb,
};

static Property swim_drive_properties[] = {
    DEFINE_PROP_INT32("unit", SWIMDrive, unit, -1),
    DEFINE_BLOCK_PROPERTIES(SWIMDrive, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void swim_drive_realize(DeviceState *qdev, Error **errp)
{
    SWIMDrive *dev = SWIM_DRIVE(qdev);
    SWIMBus *bus = SWIM_BUS(qdev->parent_bus);
    FDrive *drive;
    int ret;

    if (dev->unit == -1) {
        for (dev->unit = 0; dev->unit < SWIM_MAX_FD; dev->unit++) {
            drive = &bus->ctrl->drives[dev->unit];
            if (!drive->blk) {
                break;
            }
        }
    }

    if (dev->unit >= SWIM_MAX_FD) {
        error_setg(errp, "Can't create floppy unit %d, bus supports "
                   "only %d units", dev->unit, SWIM_MAX_FD);
        return;
    }

    drive = &bus->ctrl->drives[dev->unit];
    if (drive->blk) {
        error_setg(errp, "Floppy unit %d is in use", dev->unit);
        return;
    }

    if (!dev->conf.blk) {
        /* Anonymous BlockBackend for an empty drive */
        dev->conf.blk = blk_new(qemu_get_aio_context(), 0, BLK_PERM_ALL);
        ret = blk_attach_dev(dev->conf.blk, qdev);
        assert(ret == 0);
    }

    if (!blkconf_blocksizes(&dev->conf, errp)) {
        return;
    }

    if (dev->conf.logical_block_size != 512 ||
        dev->conf.physical_block_size != 512)
    {
        error_setg(errp, "Physical and logical block size must "
                   "be 512 for floppy");
        return;
    }

    /*
     * rerror/werror aren't supported by fdc and therefore not even registered
     * with qdev. So set the defaults manually before they are used in
     * blkconf_apply_backend_options().
     */
    dev->conf.rerror = BLOCKDEV_ON_ERROR_AUTO;
    dev->conf.werror = BLOCKDEV_ON_ERROR_AUTO;

    if (!blkconf_apply_backend_options(&dev->conf,
                                       !blk_supports_write_perm(dev->conf.blk),
                                       false, errp)) {
        return;
    }

    /*
     * 'enospc' is the default for -drive, 'report' is what blk_new() gives us
     * for empty drives.
     */
    if (blk_get_on_error(dev->conf.blk, 0) != BLOCKDEV_ON_ERROR_ENOSPC &&
        blk_get_on_error(dev->conf.blk, 0) != BLOCKDEV_ON_ERROR_REPORT) {
        error_setg(errp, "fdc doesn't support drive option werror");
        return;
    }
    if (blk_get_on_error(dev->conf.blk, 1) != BLOCKDEV_ON_ERROR_REPORT) {
        error_setg(errp, "fdc doesn't support drive option rerror");
        return;
    }

    drive->conf = &dev->conf;
    drive->blk = dev->conf.blk;
    drive->swimctrl = bus->ctrl;

    blk_set_dev_ops(drive->blk, &swim_block_ops, drive);
}

static void swim_drive_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    k->realize = swim_drive_realize;
    set_bit(DEVICE_CATEGORY_STORAGE, k->categories);
    k->bus_type = TYPE_SWIM_BUS;
    device_class_set_props(k, swim_drive_properties);
    k->desc = "virtual SWIM drive";
}

static const TypeInfo swim_drive_info = {
    .name = TYPE_SWIM_DRIVE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(SWIMDrive),
    .class_init = swim_drive_class_init,
};

static const TypeInfo swim_bus_info = {
    .name = TYPE_SWIM_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(SWIMBus),
};

static void iwmctrl_write(void *opaque, hwaddr reg, uint64_t value,
                          unsigned size)
{
    SWIMCtrl *swimctrl = opaque;

    reg >>= REG_SHIFT;

    swimctrl->regs[reg >> 1] = reg & 1;

    if (swimctrl->regs[IWM_Q6] &&
        swimctrl->regs[IWM_Q7]) {
        if (swimctrl->regs[IWM_MTR]) {
            /* data register */
            swimctrl->iwm_data = value;
        } else {
            /* mode register */
            swimctrl->iwm_mode = value;
            /* detect sequence to switch from IWM mode to SWIM mode */
            switch (swimctrl->iwm_switch) {
            case 0:
                if (value == 0x57) {
                    swimctrl->iwm_switch++;
                }
                break;
            case 1:
                if (value == 0x17) {
                    swimctrl->iwm_switch++;
                }
                break;
            case 2:
                if (value == 0x57) {
                    swimctrl->iwm_switch++;
                }
                break;
            case 3:
                if (value == 0x57) {
                    swimctrl->mode = SWIM_MODE_SWIM;
                    swimctrl->iwm_switch = 0;
                }
                break;
            }
        }
    }
}

static uint64_t iwmctrl_read(void *opaque, hwaddr reg, unsigned size)
{
    SWIMCtrl *swimctrl = opaque;

    reg >>= REG_SHIFT;

    swimctrl->regs[reg >> 1] = reg & 1;

    return 0;
}

static void swimctrl_write(void *opaque, hwaddr reg, uint64_t value,
                           unsigned size)
{
    SWIMCtrl *swimctrl = opaque;

    if (swimctrl->mode == SWIM_MODE_IWM) {
        iwmctrl_write(opaque, reg, value, size);
        return;
    }

    reg >>= REG_SHIFT;

    switch (reg) {
    case SWIM_WRITE_PHASE:
        swimctrl->swim_phase = value;
        break;
    case SWIM_WRITE_MODE0:
        swimctrl->swim_mode &= ~value;
        break;
    case SWIM_WRITE_MODE1:
        swimctrl->swim_mode |= value;
        break;
    case SWIM_WRITE_DATA:
    case SWIM_WRITE_MARK:
    case SWIM_WRITE_CRC:
    case SWIM_WRITE_PARAMETER:
    case SWIM_WRITE_SETUP:
        break;
    }
}

static uint64_t swimctrl_read(void *opaque, hwaddr reg, unsigned size)
{
    SWIMCtrl *swimctrl = opaque;
    uint32_t value = 0;

    if (swimctrl->mode == SWIM_MODE_IWM) {
        return iwmctrl_read(opaque, reg, size);
    }

    reg >>= REG_SHIFT;

    switch (reg) {
    case SWIM_READ_PHASE:
        value = swimctrl->swim_phase;
        break;
    case SWIM_READ_HANDSHAKE:
        if (swimctrl->swim_phase == SWIM_DRIVE_PRESENT) {
            /* always answer "no drive present" */
            value = SWIM_SENSE;
        }
        break;
    case SWIM_READ_DATA:
    case SWIM_READ_MARK:
    case SWIM_READ_ERROR:
    case SWIM_READ_PARAMETER:
    case SWIM_READ_SETUP:
    case SWIM_READ_STATUS:
        break;
    }

    return value;
}

static const MemoryRegionOps swimctrl_mem_ops = {
    .write = swimctrl_write,
    .read = swimctrl_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void sysbus_swim_reset(DeviceState *d)
{
    Swim *sys = SWIM(d);
    SWIMCtrl *ctrl = &sys->ctrl;
    int i;

    ctrl->mode = 0;
    ctrl->iwm_switch = 0;
    for (i = 0; i < 8; i++) {
        ctrl->regs[i] = 0;
    }
    ctrl->iwm_data = 0;
    ctrl->iwm_mode = 0;
    ctrl->swim_phase = 0;
    ctrl->swim_mode = 0;
    for (i = 0; i < SWIM_MAX_FD; i++) {
        fd_recalibrate(&ctrl->drives[i]);
    }
}

static void sysbus_swim_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    Swim *sbs = SWIM(obj);
    SWIMCtrl *swimctrl = &sbs->ctrl;

    memory_region_init_io(&swimctrl->iomem, obj, &swimctrl_mem_ops, swimctrl,
                          "swim", 0x2000);
    sysbus_init_mmio(sbd, &swimctrl->iomem);
}

static void sysbus_swim_realize(DeviceState *dev, Error **errp)
{
    Swim *sys = SWIM(dev);
    SWIMCtrl *swimctrl = &sys->ctrl;

    qbus_create_inplace(&swimctrl->bus, sizeof(SWIMBus), TYPE_SWIM_BUS, dev,
                        NULL);
    swimctrl->bus.ctrl = swimctrl;
}

static const VMStateDescription vmstate_fdrive = {
    .name = "fdrive",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_swim = {
    .name = "swim",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(mode, SWIMCtrl),
        /* IWM mode */
        VMSTATE_INT32(iwm_switch, SWIMCtrl),
        VMSTATE_UINT16_ARRAY(regs, SWIMCtrl, 8),
        VMSTATE_UINT8(iwm_data, SWIMCtrl),
        VMSTATE_UINT8(iwm_mode, SWIMCtrl),
        /* SWIM mode */
        VMSTATE_UINT8(swim_phase, SWIMCtrl),
        VMSTATE_UINT8(swim_mode, SWIMCtrl),
        /* Drives */
        VMSTATE_STRUCT_ARRAY(drives, SWIMCtrl, SWIM_MAX_FD, 1,
                             vmstate_fdrive, FDrive),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_sysbus_swim = {
    .name = "SWIM",
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(ctrl, Swim, 0, vmstate_swim, SWIMCtrl),
        VMSTATE_END_OF_LIST()
    }
};

static void sysbus_swim_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = sysbus_swim_realize;
    dc->reset = sysbus_swim_reset;
    dc->vmsd = &vmstate_sysbus_swim;
}

static const TypeInfo sysbus_swim_info = {
    .name          = TYPE_SWIM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Swim),
    .instance_init = sysbus_swim_init,
    .class_init    = sysbus_swim_class_init,
};

static void swim_register_types(void)
{
    type_register_static(&sysbus_swim_info);
    type_register_static(&swim_bus_info);
    type_register_static(&swim_drive_info);
}

type_init(swim_register_types)
