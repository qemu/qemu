/*
 * bootloader support
 *
 * Copyright IBM, Corp. 2012
 *
 * Authors:
 *  Christian Borntraeger <borntraeger@de.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at your
 * option) any later version.  See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "sysemu/sysemu.h"
#include "cpu.h"
#include "elf.h"
#include "hw/loader.h"
#include "hw/s390x/virtio-ccw.h"
#include "hw/s390x/css.h"
#include "ipl.h"

#define KERN_IMAGE_START                0x010000UL
#define KERN_PARM_AREA                  0x010480UL
#define INITRD_START                    0x800000UL
#define INITRD_PARM_START               0x010408UL
#define INITRD_PARM_SIZE                0x010410UL
#define PARMFILE_START                  0x001000UL
#define ZIPL_IMAGE_START                0x009000UL
#define IPL_PSW_MASK                    (PSW_MASK_32 | PSW_MASK_64)

static bool iplb_extended_needed(void *opaque)
{
    S390IPLState *ipl = S390_IPL(object_resolve_path(TYPE_S390_IPL, NULL));

    return ipl->iplbext_migration;
}

static const VMStateDescription vmstate_iplb_extended = {
    .name = "ipl/iplb_extended",
    .version_id = 0,
    .minimum_version_id = 0,
    .needed = iplb_extended_needed,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(reserved_ext, IplParameterBlock, 4096 - 200),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_iplb = {
    .name = "ipl/iplb",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(reserved1, IplParameterBlock, 110),
        VMSTATE_UINT16(devno, IplParameterBlock),
        VMSTATE_UINT8_ARRAY(reserved2, IplParameterBlock, 88),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &vmstate_iplb_extended,
        NULL
    }
};

static const VMStateDescription vmstate_ipl = {
    .name = "ipl",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(start_addr, S390IPLState),
        VMSTATE_UINT64(bios_start_addr, S390IPLState),
        VMSTATE_STRUCT(iplb, S390IPLState, 0, vmstate_iplb, IplParameterBlock),
        VMSTATE_BOOL(iplb_valid, S390IPLState),
        VMSTATE_UINT8(cssid, S390IPLState),
        VMSTATE_UINT8(ssid, S390IPLState),
        VMSTATE_UINT16(devno, S390IPLState),
        VMSTATE_END_OF_LIST()
     }
};

static S390IPLState *get_ipl_device(void)
{
    return S390_IPL(object_resolve_path_type("", TYPE_S390_IPL, NULL));
}

static uint64_t bios_translate_addr(void *opaque, uint64_t srcaddr)
{
    uint64_t dstaddr = *(uint64_t *) opaque;
    /*
     * Assuming that our s390-ccw.img was linked for starting at address 0,
     * we can simply add the destination address for the final location
     */
    return srcaddr + dstaddr;
}

static void s390_ipl_realize(DeviceState *dev, Error **errp)
{
    S390IPLState *ipl = S390_IPL(dev);
    uint64_t pentry = KERN_IMAGE_START;
    int kernel_size;
    Error *err = NULL;

    int bios_size;
    char *bios_filename;

    /*
     * Always load the bios if it was enforced,
     * even if an external kernel has been defined.
     */
    if (!ipl->kernel || ipl->enforce_bios) {
        uint64_t fwbase = (MIN(ram_size, 0x80000000U) - 0x200000) & ~0xffffUL;

        if (bios_name == NULL) {
            bios_name = ipl->firmware;
        }

        bios_filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
        if (bios_filename == NULL) {
            error_setg(&err, "could not find stage1 bootloader");
            goto error;
        }

        bios_size = load_elf(bios_filename, bios_translate_addr, &fwbase,
                             &ipl->bios_start_addr, NULL, NULL, 1,
                             EM_S390, 0, 0);
        if (bios_size > 0) {
            /* Adjust ELF start address to final location */
            ipl->bios_start_addr += fwbase;
        } else {
            /* Try to load non-ELF file (e.g. s390-ccw.img) */
            bios_size = load_image_targphys(bios_filename, ZIPL_IMAGE_START,
                                            4096);
            ipl->bios_start_addr = ZIPL_IMAGE_START;
        }
        g_free(bios_filename);

        if (bios_size == -1) {
            error_setg(&err, "could not load bootloader '%s'", bios_name);
            goto error;
        }

        /* default boot target is the bios */
        ipl->start_addr = ipl->bios_start_addr;
    }

    if (ipl->kernel) {
        kernel_size = load_elf(ipl->kernel, NULL, NULL, &pentry, NULL,
                               NULL, 1, EM_S390, 0, 0);
        if (kernel_size < 0) {
            kernel_size = load_image_targphys(ipl->kernel, 0, ram_size);
        }
        if (kernel_size < 0) {
            error_setg(&err, "could not load kernel '%s'", ipl->kernel);
            goto error;
        }
        /*
         * Is it a Linux kernel (starting at 0x10000)? If yes, we fill in the
         * kernel parameters here as well. Note: For old kernels (up to 3.2)
         * we can not rely on the ELF entry point - it was 0x800 (the SALIPL
         * loader) and it won't work. For this case we force it to 0x10000, too.
         */
        if (pentry == KERN_IMAGE_START || pentry == 0x800) {
            ipl->start_addr = KERN_IMAGE_START;
            /* Overwrite parameters in the kernel image, which are "rom" */
            strcpy(rom_ptr(KERN_PARM_AREA), ipl->cmdline);
        } else {
            ipl->start_addr = pentry;
        }

        if (ipl->initrd) {
            ram_addr_t initrd_offset;
            int initrd_size;

            initrd_offset = INITRD_START;
            while (kernel_size + 0x100000 > initrd_offset) {
                initrd_offset += 0x100000;
            }
            initrd_size = load_image_targphys(ipl->initrd, initrd_offset,
                                              ram_size - initrd_offset);
            if (initrd_size == -1) {
                error_setg(&err, "could not load initrd '%s'", ipl->initrd);
                goto error;
            }

            /*
             * we have to overwrite values in the kernel image,
             * which are "rom"
             */
            stq_p(rom_ptr(INITRD_PARM_START), initrd_offset);
            stq_p(rom_ptr(INITRD_PARM_SIZE), initrd_size);
        }
    }
    qemu_register_reset(qdev_reset_all_fn, dev);
error:
    error_propagate(errp, err);
}

static Property s390_ipl_properties[] = {
    DEFINE_PROP_STRING("kernel", S390IPLState, kernel),
    DEFINE_PROP_STRING("initrd", S390IPLState, initrd),
    DEFINE_PROP_STRING("cmdline", S390IPLState, cmdline),
    DEFINE_PROP_STRING("firmware", S390IPLState, firmware),
    DEFINE_PROP_BOOL("enforce_bios", S390IPLState, enforce_bios, false),
    DEFINE_PROP_BOOL("iplbext_migration", S390IPLState, iplbext_migration,
                     true),
    DEFINE_PROP_END_OF_LIST(),
};

/*
 * In addition to updating the iplstate, this function returns:
 * - 0 if system was ipled with external kernel
 * - -1 if no valid boot device was found
 * - ccw id of the boot device otherwise
 */
static uint64_t s390_update_iplstate(S390IPLState *ipl)
{
    DeviceState *dev_st;

    if (ipl->iplb_valid) {
        ipl->cssid = 0;
        ipl->ssid = 0;
        ipl->devno = ipl->iplb.devno;
        goto out;
    }

    if (ipl->kernel) {
        return 0;
    }

    dev_st = get_boot_device(0);
    if (dev_st) {
        VirtioCcwDevice *ccw_dev = (VirtioCcwDevice *) object_dynamic_cast(
            OBJECT(qdev_get_parent_bus(dev_st)->parent),
                TYPE_VIRTIO_CCW_DEVICE);
        if (ccw_dev) {
            ipl->cssid = ccw_dev->sch->cssid;
            ipl->ssid = ccw_dev->sch->ssid;
            ipl->devno = ccw_dev->sch->devno;
            ipl->iplb.len = cpu_to_be32(S390_IPLB_MIN_CCW_LEN);
            ipl->iplb.blk0_len =
                cpu_to_be32(S390_IPLB_MIN_CCW_LEN - S390_IPLB_HEADER_LEN);
            ipl->iplb.pbt = S390_IPL_TYPE_CCW;
            ipl->iplb.ccw.devno = cpu_to_be16(ccw_dev->sch->devno);
            ipl->iplb_valid = true;
            goto out;
        }
    }

    return -1;
out:
    return (uint32_t) (ipl->cssid << 24 | ipl->ssid << 16 | ipl->devno);
}

void s390_ipl_update_diag308(IplParameterBlock *iplb)
{
    S390IPLState *ipl = get_ipl_device();

    ipl->iplb = *iplb;
    ipl->iplb_valid = true;
}

IplParameterBlock *s390_ipl_get_iplb(void)
{
    S390IPLState *ipl = get_ipl_device();

    if (!ipl->iplb_valid) {
        return NULL;
    }
    return &ipl->iplb;
}

void s390_reipl_request(void)
{
    S390IPLState *ipl = get_ipl_device();

    ipl->reipl_requested = true;
    qemu_system_reset_request();
}

void s390_ipl_prepare_cpu(S390CPU *cpu)
{
    S390IPLState *ipl = get_ipl_device();

    cpu->env.psw.addr = ipl->start_addr;
    cpu->env.psw.mask = IPL_PSW_MASK;

    if (!ipl->kernel || ipl->iplb_valid) {
        cpu->env.psw.addr = ipl->bios_start_addr;
        cpu->env.regs[7] = s390_update_iplstate(ipl);
    }
}

static void s390_ipl_reset(DeviceState *dev)
{
    S390IPLState *ipl = S390_IPL(dev);

    if (!ipl->reipl_requested) {
        ipl->iplb_valid = false;
        memset(&ipl->iplb, 0, sizeof(IplParameterBlock));
    }
    ipl->reipl_requested = false;
}

static void s390_ipl_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = s390_ipl_realize;
    dc->props = s390_ipl_properties;
    dc->reset = s390_ipl_reset;
    dc->vmsd = &vmstate_ipl;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo s390_ipl_info = {
    .class_init = s390_ipl_class_init,
    .parent = TYPE_DEVICE,
    .name  = TYPE_S390_IPL,
    .instance_size  = sizeof(S390IPLState),
};

static void s390_ipl_register_types(void)
{
    type_register_static(&s390_ipl_info);
}

type_init(s390_ipl_register_types)
