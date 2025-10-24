/*
 * bootloader support
 *
 * Copyright IBM, Corp. 2012, 2020
 *
 * Authors:
 *  Christian Borntraeger <borntraeger@de.ibm.com>
 *  Janosch Frank <frankja@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at your
 * option) any later version.  See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/datadir.h"
#include "qapi/error.h"
#include "system/reset.h"
#include "system/runstate.h"
#include "system/tcg.h"
#include "elf.h"
#include "hw/loader.h"
#include "hw/qdev-properties.h"
#include "hw/boards.h"
#include "hw/s390x/virtio-ccw.h"
#include "hw/s390x/vfio-ccw.h"
#include "hw/s390x/css.h"
#include "hw/s390x/ebcdic.h"
#include "hw/scsi/scsi.h"
#include "hw/virtio/virtio-net.h"
#include "ipl.h"
#include "qemu/error-report.h"
#include "qemu/config-file.h"
#include "qemu/cutils.h"
#include "qemu/option.h"
#include "qemu/ctype.h"
#include "standard-headers/linux/virtio_ids.h"

#define KERN_IMAGE_START                0x010000UL
#define LINUX_MAGIC_ADDR                0x010008UL
#define KERN_PARM_AREA_SIZE_ADDR        0x010430UL
#define KERN_PARM_AREA                  0x010480UL
#define LEGACY_KERN_PARM_AREA_SIZE      0x000380UL
#define INITRD_START                    0x800000UL
#define INITRD_PARM_START               0x010408UL
#define PARMFILE_START                  0x001000UL
#define ZIPL_IMAGE_START                0x009000UL
#define BIOS_MAX_SIZE                   0x300000UL
#define IPL_PSW_MASK                    (PSW_MASK_32 | PSW_MASK_64)

/* Place the IPLB chain immediately before the BIOS in memory */
static uint64_t find_iplb_chain_addr(uint64_t bios_addr, uint16_t count)
{
    return (bios_addr & TARGET_PAGE_MASK)
            - (count * sizeof(IplParameterBlock));
}

static const VMStateDescription vmstate_iplb_extended = {
    .name = "ipl/iplb_extended",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(reserved_ext, IplParameterBlock, 4096 - 200),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_iplb = {
    .name = "ipl/iplb",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(reserved1, IplParameterBlock, 110),
        VMSTATE_UINT16(devno, IplParameterBlock),
        VMSTATE_UINT8_ARRAY(reserved2, IplParameterBlock, 88),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_iplb_extended,
        NULL
    }
};

static const VMStateDescription vmstate_ipl = {
    .name = "ipl",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(compat_start_addr, S390IPLState),
        VMSTATE_UINT64(compat_bios_start_addr, S390IPLState),
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

static uint64_t get_max_kernel_cmdline_size(void)
{
    uint64_t *size_ptr = rom_ptr(KERN_PARM_AREA_SIZE_ADDR, sizeof(*size_ptr));

    if (size_ptr) {
        uint64_t size;

        size = be64_to_cpu(*size_ptr);
        if (size) {
            return size;
        }
    }
    return LEGACY_KERN_PARM_AREA_SIZE;
}

static void s390_ipl_realize(DeviceState *dev, Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    S390IPLState *ipl = S390_IPL(dev);
    uint32_t *ipl_psw;
    uint64_t pentry;
    char *magic;
    int kernel_size;

    int bios_size;
    char *bios_filename;

    /*
     * Always load the bios if it was enforced,
     * even if an external kernel has been defined.
     */
    if (!ipl->kernel || ipl->enforce_bios) {
        uint64_t fwbase;

        if (ms->ram_size < BIOS_MAX_SIZE) {
            error_setg(errp, "not enough RAM to load the BIOS file");
            return;
        }

        fwbase = (MIN(ms->ram_size, 0x80000000U) - BIOS_MAX_SIZE) & ~0xffffUL;

        bios_filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, ipl->firmware);
        if (bios_filename == NULL) {
            error_setg(errp, "could not find stage1 bootloader");
            return;
        }

        bios_size = load_elf(bios_filename, NULL,
                             bios_translate_addr, &fwbase,
                             &ipl->bios_start_addr, NULL, NULL, NULL,
                             ELFDATA2MSB, EM_S390, 0, 0);
        if (bios_size > 0) {
            /* Adjust ELF start address to final location */
            ipl->bios_start_addr += fwbase;
        } else {
            /* Try to load non-ELF file */
            bios_size = load_image_targphys(bios_filename, ZIPL_IMAGE_START,
                                            4096, NULL);
            ipl->bios_start_addr = ZIPL_IMAGE_START;
        }
        g_free(bios_filename);

        if (bios_size == -1) {
            error_setg(errp, "could not load bootloader '%s'", ipl->firmware);
            return;
        }

        /* default boot target is the bios */
        ipl->start_addr = ipl->bios_start_addr;
    }

    if (ipl->kernel) {
        kernel_size = load_elf(ipl->kernel, NULL, NULL, NULL,
                               &pentry, NULL,
                               NULL, NULL, ELFDATA2MSB, EM_S390, 0, 0);
        if (kernel_size < 0) {
            kernel_size = load_image_targphys(ipl->kernel, 0, ms->ram_size,
                                              NULL);
            if (kernel_size < 0) {
                error_setg(errp, "could not load kernel '%s'", ipl->kernel);
                return;
            }
            /* if this is Linux use KERN_IMAGE_START */
            magic = rom_ptr(LINUX_MAGIC_ADDR, 6);
            if (magic && !memcmp(magic, "S390EP", 6)) {
                pentry = KERN_IMAGE_START;
            } else {
                /* if not Linux load the address of the (short) IPL PSW */
                ipl_psw = rom_ptr(4, 4);
                if (ipl_psw) {
                    pentry = be32_to_cpu(*ipl_psw) & PSW_MASK_SHORT_ADDR;
                } else {
                    error_setg(errp, "Could not get IPL PSW");
                    return;
                }
            }
        }
        /*
         * Is it a Linux kernel (starting at 0x10000)? If yes, we fill in the
         * kernel parameters here as well. Note: For old kernels (up to 3.2)
         * we can not rely on the ELF entry point - it was 0x800 (the SALIPL
         * loader) and it won't work. For this case we force it to 0x10000, too.
         */
        if (pentry == KERN_IMAGE_START || pentry == 0x800) {
            size_t cmdline_size = strlen(ipl->cmdline) + 1;
            char *parm_area = rom_ptr(KERN_PARM_AREA, cmdline_size);

            ipl->start_addr = KERN_IMAGE_START;
            /* Overwrite parameters in the kernel image, which are "rom" */
            if (parm_area) {
                uint64_t max_cmdline_size = get_max_kernel_cmdline_size();

                if (cmdline_size > max_cmdline_size) {
                    error_setg(errp,
                               "kernel command line exceeds maximum size:"
                               " %zu > %" PRIu64,
                               cmdline_size, max_cmdline_size);
                    return;
                }

                strcpy(parm_area, ipl->cmdline);
            }
        } else {
            ipl->start_addr = pentry;
        }

        if (ipl->initrd) {
            ram_addr_t initrd_offset;
            int initrd_size;
            uint64_t *romptr;

            initrd_offset = INITRD_START;
            while (kernel_size + 0x100000 > initrd_offset) {
                initrd_offset += 0x100000;
            }
            initrd_size = load_image_targphys(ipl->initrd, initrd_offset,
                                              ms->ram_size - initrd_offset,
                                              NULL);
            if (initrd_size == -1) {
                error_setg(errp, "could not load initrd '%s'", ipl->initrd);
                return;
            }

            /*
             * we have to overwrite values in the kernel image,
             * which are "rom"
             */
            romptr = rom_ptr(INITRD_PARM_START, 16);
            if (romptr) {
                stq_be_p(romptr, initrd_offset);
                stq_be_p(romptr + 1, initrd_size);
            }
        }
    }
    /*
     * Don't ever use the migrated values, they could come from a different
     * BIOS and therefore don't work. But still migrate the values, so
     * QEMUs relying on it don't break.
     */
    ipl->compat_start_addr = ipl->start_addr;
    ipl->compat_bios_start_addr = ipl->bios_start_addr;
    /*
     * Because this Device is not on any bus in the qbus tree (it is
     * not a sysbus device and it's not on some other bus like a PCI
     * bus) it will not be automatically reset by the 'reset the
     * sysbus' hook registered by vl.c like most devices. So we must
     * manually register a reset hook for it.
     * TODO: there should be a better way to do this.
     */
    qemu_register_reset(resettable_cold_reset_fn, dev);
}

static const Property s390_ipl_properties[] = {
    DEFINE_PROP_STRING("kernel", S390IPLState, kernel),
    DEFINE_PROP_STRING("initrd", S390IPLState, initrd),
    DEFINE_PROP_STRING("cmdline", S390IPLState, cmdline),
    DEFINE_PROP_STRING("firmware", S390IPLState, firmware),
    DEFINE_PROP_BOOL("enforce_bios", S390IPLState, enforce_bios, false),
};

static void s390_ipl_set_boot_menu(S390IPLState *ipl)
{
    unsigned long splash_time = 0;

    if (!get_boot_device(0)) {
        if (current_machine->boot_config.has_menu && current_machine->boot_config.menu) {
            error_report("boot menu requires a bootindex to be specified for "
                         "the IPL device");
        }
        return;
    }

    switch (ipl->iplb.pbt) {
    case S390_IPL_TYPE_CCW:
        /* In the absence of -boot menu, use zipl parameters */
        if (!current_machine->boot_config.has_menu) {
            ipl->qipl.qipl_flags |= QIPL_FLAG_BM_OPTS_ZIPL;
            return;
        }
        break;
    case S390_IPL_TYPE_QEMU_SCSI:
        break;
    default:
        if (current_machine->boot_config.has_menu && current_machine->boot_config.menu) {
            error_report("boot menu is not supported for this device type");
        }
        return;
    }

    if (!current_machine->boot_config.has_menu || !current_machine->boot_config.menu) {
        return;
    }

    ipl->qipl.qipl_flags |= QIPL_FLAG_BM_OPTS_CMD;

    if (current_machine->boot_config.has_splash_time) {
        splash_time = current_machine->boot_config.splash_time;
    }
    if (splash_time > 0xffffffff) {
        error_report("splash-time is too large, forcing it to max value");
        ipl->qipl.boot_menu_timeout = 0xffffffff;
        return;
    }

    ipl->qipl.boot_menu_timeout = cpu_to_be32(splash_time);
}

#define CCW_DEVTYPE_NONE        0x00
#define CCW_DEVTYPE_VIRTIO      0x01
#define CCW_DEVTYPE_VIRTIO_NET  0x02
#define CCW_DEVTYPE_SCSI        0x03
#define CCW_DEVTYPE_VFIO        0x04

static CcwDevice *s390_get_ccw_device(DeviceState *dev_st, int *devtype)
{
    CcwDevice *ccw_dev = NULL;
    int tmp_dt = CCW_DEVTYPE_NONE;

    if (dev_st) {
        VirtIONet *virtio_net_dev = (VirtIONet *)
            object_dynamic_cast(OBJECT(dev_st), TYPE_VIRTIO_NET);
        VirtioCcwDevice *virtio_ccw_dev = (VirtioCcwDevice *)
            object_dynamic_cast(OBJECT(qdev_get_parent_bus(dev_st)->parent),
                                TYPE_VIRTIO_CCW_DEVICE);
        VFIOCCWDevice *vfio_ccw_dev = (VFIOCCWDevice *)
            object_dynamic_cast(OBJECT(dev_st), TYPE_VFIO_CCW);

        if (virtio_ccw_dev) {
            ccw_dev = CCW_DEVICE(virtio_ccw_dev);
            if (virtio_net_dev) {
                tmp_dt = CCW_DEVTYPE_VIRTIO_NET;
            } else {
                tmp_dt = CCW_DEVTYPE_VIRTIO;
            }
        } else if (vfio_ccw_dev) {
            ccw_dev = CCW_DEVICE(vfio_ccw_dev);
            tmp_dt = CCW_DEVTYPE_VFIO;
        } else {
            SCSIDevice *sd = (SCSIDevice *)
                object_dynamic_cast(OBJECT(dev_st),
                                    TYPE_SCSI_DEVICE);
            if (sd) {
                SCSIBus *sbus = scsi_bus_from_device(sd);
                VirtIODevice *vdev = (VirtIODevice *)
                    object_dynamic_cast(OBJECT(sbus->qbus.parent),
                                        TYPE_VIRTIO_DEVICE);
                if (vdev) {
                    ccw_dev = (CcwDevice *)
                        object_dynamic_cast(OBJECT(qdev_get_parent_bus(DEVICE(vdev))->parent),
                                            TYPE_CCW_DEVICE);
                    if (ccw_dev) {
                        tmp_dt = CCW_DEVTYPE_SCSI;
                    }
                }
            }
        }
    }
    if (devtype) {
        *devtype = tmp_dt;
    }
    return ccw_dev;
}

static uint64_t s390_ipl_map_iplb_chain(IplParameterBlock *iplb_chain)
{
    S390IPLState *ipl = get_ipl_device();
    uint16_t count = be16_to_cpu(ipl->qipl.chain_len);
    uint64_t len = sizeof(IplParameterBlock) * count;
    uint64_t chain_addr = find_iplb_chain_addr(ipl->bios_start_addr, count);

    cpu_physical_memory_write(chain_addr, iplb_chain, len);
    return chain_addr;
}

void s390_ipl_fmt_loadparm(uint8_t *loadparm, char *str, Error **errp)
{
    /* Initialize the loadparm with spaces */
    memset(loadparm, ' ', LOADPARM_LEN);
    qdev_prop_sanitize_s390x_loadparm(loadparm, str, errp);
}

void s390_ipl_convert_loadparm(char *ascii_lp, uint8_t *ebcdic_lp)
{
    int i;

    /* Initialize the loadparm with EBCDIC spaces (0x40) */
    memset(ebcdic_lp, '@', LOADPARM_LEN);
    for (i = 0; i < LOADPARM_LEN && ascii_lp[i]; i++) {
        ebcdic_lp[i] = ascii2ebcdic[(uint8_t) ascii_lp[i]];
    }
}

static bool s390_build_iplb(DeviceState *dev_st, IplParameterBlock *iplb)
{
    CcwDevice *ccw_dev = NULL;
    SCSIDevice *sd;
    int devtype;
    uint8_t *lp;
    g_autofree void *scsi_lp = NULL;

    /*
     * Currently allow IPL only from CCW devices.
     */
    ccw_dev = s390_get_ccw_device(dev_st, &devtype);
    if (ccw_dev) {
        lp = ccw_dev->loadparm;

        switch (devtype) {
        case CCW_DEVTYPE_SCSI:
            sd = SCSI_DEVICE(dev_st);
            scsi_lp = object_property_get_str(OBJECT(sd), "loadparm", NULL);
            if (scsi_lp && strlen(scsi_lp) > 0) {
                lp = scsi_lp;
            }
            iplb->len = cpu_to_be32(S390_IPLB_MIN_QEMU_SCSI_LEN);
            iplb->blk0_len =
                cpu_to_be32(S390_IPLB_MIN_QEMU_SCSI_LEN - S390_IPLB_HEADER_LEN);
            iplb->pbt = S390_IPL_TYPE_QEMU_SCSI;
            iplb->scsi.lun = cpu_to_be32(sd->lun);
            iplb->scsi.target = cpu_to_be16(sd->id);
            iplb->scsi.channel = cpu_to_be16(sd->channel);
            iplb->scsi.devno = cpu_to_be16(ccw_dev->sch->devno);
            iplb->scsi.ssid = ccw_dev->sch->ssid & 3;
            break;
        case CCW_DEVTYPE_VFIO:
            iplb->len = cpu_to_be32(S390_IPLB_MIN_CCW_LEN);
            iplb->pbt = S390_IPL_TYPE_CCW;
            iplb->ccw.devno = cpu_to_be16(ccw_dev->sch->devno);
            iplb->ccw.ssid = ccw_dev->sch->ssid & 3;
            break;
        case CCW_DEVTYPE_VIRTIO_NET:
        case CCW_DEVTYPE_VIRTIO:
            iplb->len = cpu_to_be32(S390_IPLB_MIN_CCW_LEN);
            iplb->blk0_len =
                cpu_to_be32(S390_IPLB_MIN_CCW_LEN - S390_IPLB_HEADER_LEN);
            iplb->pbt = S390_IPL_TYPE_CCW;
            iplb->ccw.devno = cpu_to_be16(ccw_dev->sch->devno);
            iplb->ccw.ssid = ccw_dev->sch->ssid & 3;
            break;
        }

        /* If the device loadparm is empty use the global machine loadparm */
        if (memcmp(lp, NO_LOADPARM, 8) == 0) {
            lp = S390_CCW_MACHINE(qdev_get_machine())->loadparm;
        }

        s390_ipl_convert_loadparm((char *)lp, iplb->loadparm);
        iplb->flags |= DIAG308_FLAGS_LP_VALID;

        return true;
    }

    return false;
}

void s390_rebuild_iplb(uint16_t dev_index, IplParameterBlock *iplb)
{
    S390IPLState *ipl = get_ipl_device();
    uint16_t index;
    index = ipl->rebuilt_iplb ? ipl->iplb_index : dev_index;

    ipl->rebuilt_iplb = s390_build_iplb(get_boot_device(index), iplb);
    ipl->iplb_index = index;
}

static bool s390_init_all_iplbs(S390IPLState *ipl)
{
    int iplb_num = 0;
    IplParameterBlock iplb_chain[7];
    DeviceState *dev_st = get_boot_device(0);
    Object *machine = qdev_get_machine();

    /*
     * Parse the boot devices.  Generate an IPLB for only the first boot device
     * which will later be set with DIAG308.
     */
    if (!dev_st) {
        ipl->qipl.chain_len = 0;
        return false;
    }

    /* If no machine loadparm was defined fill it with spaces */
    if (memcmp(S390_CCW_MACHINE(machine)->loadparm, NO_LOADPARM, 8) == 0) {
        object_property_set_str(machine, "loadparm", "        ", NULL);
    }

    iplb_num = 1;
    s390_build_iplb(dev_st, &ipl->iplb);

    /*  Index any fallback boot devices */
    while (get_boot_device(iplb_num)) {
        iplb_num++;
    }

    if (iplb_num > MAX_BOOT_DEVS) {
        warn_report("Excess boot devices defined! %d boot devices found, "
                    "but only the first %d will be considered.",
                    iplb_num, MAX_BOOT_DEVS);

        iplb_num = MAX_BOOT_DEVS;
    }

    ipl->qipl.chain_len = cpu_to_be16(iplb_num - 1);

    /*
     * Build fallback IPLBs for any boot devices above index 0, up to a
     * maximum amount as defined in ipl.h
     */
    if (iplb_num > 1) {
        /* Start at 1 because the IPLB for boot index 0 is not chained */
        for (int i = 1; i < iplb_num; i++) {
            dev_st = get_boot_device(i);
            s390_build_iplb(dev_st, &iplb_chain[i - 1]);
        }

        ipl->qipl.next_iplb = cpu_to_be64(s390_ipl_map_iplb_chain(iplb_chain));
    }

    return iplb_num;
}

static void update_machine_ipl_properties(IplParameterBlock *iplb)
{
    Object *machine = qdev_get_machine();
    Error *err = NULL;

    /* Sync loadparm */
    if (iplb->flags & DIAG308_FLAGS_LP_VALID) {
        uint8_t *ebcdic_loadparm = iplb->loadparm;
        char ascii_loadparm[9];
        int i;

        for (i = 0; i < 8 && ebcdic_loadparm[i]; i++) {
            ascii_loadparm[i] = ebcdic2ascii[(uint8_t) ebcdic_loadparm[i]];
        }
        ascii_loadparm[i] = 0;
        object_property_set_str(machine, "loadparm", ascii_loadparm, &err);
    } else {
        object_property_set_str(machine, "loadparm", "        ", &err);
    }
    if (err) {
        warn_report_err(err);
    }
}

void s390_ipl_update_diag308(IplParameterBlock *iplb)
{
    S390IPLState *ipl = get_ipl_device();

    /*
     * The IPLB set and retrieved by subcodes 8/9 is completely
     * separate from the one managed via subcodes 5/6.
     */
    if (iplb->pbt == S390_IPL_TYPE_PV) {
        ipl->iplb_pv = *iplb;
        ipl->iplb_valid_pv = true;
    } else {
        ipl->iplb = *iplb;
        ipl->iplb_valid = true;
    }

    update_machine_ipl_properties(iplb);
}

IplParameterBlock *s390_ipl_get_iplb_pv(void)
{
    S390IPLState *ipl = get_ipl_device();

    if (!ipl->iplb_valid_pv) {
        return NULL;
    }
    return &ipl->iplb_pv;
}

IplParameterBlock *s390_ipl_get_iplb(void)
{
    S390IPLState *ipl = get_ipl_device();

    if (!ipl->iplb_valid) {
        return NULL;
    }
    return &ipl->iplb;
}

void s390_ipl_reset_request(CPUState *cs, enum s390_reset reset_type)
{
    S390IPLState *ipl = get_ipl_device();
    if (reset_type == S390_RESET_EXTERNAL || reset_type == S390_RESET_REIPL) {
        /* use CPU 0 for full resets */
        ipl->reset_cpu_index = 0;
    } else {
        ipl->reset_cpu_index = cs->cpu_index;
    }

    ipl->reset_type = reset_type;
    if (reset_type == S390_RESET_MODIFIED_CLEAR ||
        reset_type == S390_RESET_LOAD_NORMAL ||
        reset_type == S390_RESET_PV) {
        /* ignore -no-reboot, send no event  */
        qemu_system_reset_request(SHUTDOWN_CAUSE_SUBSYSTEM_RESET);
    } else {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    }
    /* as this is triggered by a CPU, make sure to exit the loop */
    if (tcg_enabled()) {
        cpu_loop_exit(cs);
    }
}

void s390_ipl_get_reset_request(CPUState **cs, enum s390_reset *reset_type)
{
    S390IPLState *ipl = get_ipl_device();

    *cs = qemu_get_cpu(ipl->reset_cpu_index);
    if (!*cs) {
        /* use any CPU */
        *cs = first_cpu;
    }
    *reset_type = ipl->reset_type;
}

void s390_ipl_clear_reset_request(void)
{
    S390IPLState *ipl = get_ipl_device();

    ipl->reset_type = S390_RESET_EXTERNAL;
    /* use CPU 0 for full resets */
    ipl->reset_cpu_index = 0;
}

static void s390_ipl_prepare_qipl(S390CPU *cpu)
{
    S390IPLState *ipl = get_ipl_device();
    uint8_t *addr;
    uint64_t len = 4096;

    addr = cpu_physical_memory_map(cpu->env.psa, &len, true);
    if (!addr || len < QIPL_ADDRESS + sizeof(QemuIplParameters)) {
        error_report("Cannot set QEMU IPL parameters");
        return;
    }
    memcpy(addr + QIPL_ADDRESS, &ipl->qipl, sizeof(QemuIplParameters));
    cpu_physical_memory_unmap(addr, len, 1, len);
}

int s390_ipl_prepare_pv_header(struct S390PVResponse *pv_resp, Error **errp)
{
    IplParameterBlock *ipib = s390_ipl_get_iplb_pv();
    IPLBlockPV *ipib_pv = &ipib->pv;
    void *hdr = g_malloc(ipib_pv->pv_header_len);
    int rc;

    cpu_physical_memory_read(ipib_pv->pv_header_addr, hdr,
                             ipib_pv->pv_header_len);
    rc = s390_pv_set_sec_parms((uintptr_t)hdr, ipib_pv->pv_header_len,
                               pv_resp, errp);
    g_free(hdr);
    return rc;
}

int s390_ipl_pv_unpack(struct S390PVResponse *pv_resp)
{
    IplParameterBlock *ipib = s390_ipl_get_iplb_pv();
    IPLBlockPV *ipib_pv = &ipib->pv;
    int i, rc = 0;

    for (i = 0; i < ipib_pv->num_comp; i++) {
        rc = s390_pv_unpack(ipib_pv->components[i].addr,
                            TARGET_PAGE_ALIGN(ipib_pv->components[i].size),
                            ipib_pv->components[i].tweak_pref,
                            pv_resp);
        if (rc) {
            break;
        }
    }
    return rc;
}

void s390_ipl_prepare_cpu(S390CPU *cpu)
{
    S390IPLState *ipl = get_ipl_device();

    cpu->env.psw.addr = ipl->start_addr;
    cpu->env.psw.mask = IPL_PSW_MASK;

    if (!ipl->kernel || ipl->iplb_valid) {
        cpu->env.psw.addr = ipl->bios_start_addr;
        if (!ipl->iplb_valid) {
            ipl->iplb_valid = s390_init_all_iplbs(ipl);
        } else {
            ipl->qipl.chain_len = 0;
        }
    }
    s390_ipl_set_boot_menu(ipl);
    s390_ipl_prepare_qipl(cpu);
}

static void s390_ipl_reset(DeviceState *dev)
{
    S390IPLState *ipl = S390_IPL(dev);

    if (ipl->reset_type != S390_RESET_REIPL) {
        ipl->iplb_valid = false;
        memset(&ipl->iplb, 0, sizeof(IplParameterBlock));
    }
}

static void s390_ipl_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = s390_ipl_realize;
    device_class_set_props(dc, s390_ipl_properties);
    device_class_set_legacy_reset(dc, s390_ipl_reset);
    dc->vmsd = &vmstate_ipl;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    /* Reason: Loads the ROMs and thus can only be used one time - internally */
    dc->user_creatable = false;
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
