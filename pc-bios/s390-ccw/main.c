/*
 * S390 virtio-ccw loading program
 *
 * Copyright (c) 2013 Alexander Graf <agraf@suse.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "libc.h"
#include "helper.h"
#include "s390-arch.h"
#include "s390-ccw.h"
#include "cio.h"
#include "virtio.h"
#include "virtio-scsi.h"
#include "dasd-ipl.h"

char stack[PAGE_SIZE * 8] __attribute__((__aligned__(PAGE_SIZE)));
static SubChannelId blk_schid = { .one = 1 };
static char loadparm_str[LOADPARM_LEN + 1];
QemuIplParameters qipl;
IplParameterBlock iplb __attribute__((__aligned__(PAGE_SIZE)));
static bool have_iplb;
static uint16_t cutype;
LowCore *lowcore; /* Yes, this *is* a pointer to address 0 */

#define LOADPARM_PROMPT "PROMPT  "
#define LOADPARM_EMPTY  "        "
#define BOOT_MENU_FLAG_MASK (QIPL_FLAG_BM_OPTS_CMD | QIPL_FLAG_BM_OPTS_ZIPL)

/*
 * Principles of Operations (SA22-7832-09) chapter 17 requires that
 * a subsystem-identification is at 184-187 and bytes 188-191 are zero
 * after list-directed-IPL and ccw-IPL.
 */
void write_subsystem_identification(void)
{
    lowcore->subchannel_id = blk_schid.sch_id;
    lowcore->subchannel_nr = blk_schid.sch_no;
    lowcore->io_int_parm = 0;
}

void write_iplb_location(void)
{
    if (cutype == CU_TYPE_VIRTIO && virtio_get_device_type() != VIRTIO_ID_NET) {
        lowcore->ptr_iplb = ptr2u32(&iplb);
    }
}

unsigned int get_loadparm_index(void)
{
    return atoui(loadparm_str);
}

static int is_dev_possibly_bootable(int dev_no, int sch_no)
{
    bool is_virtio;
    Schib schib;
    int r;

    blk_schid.sch_no = sch_no;
    r = stsch_err(blk_schid, &schib);
    if (r == 3 || r == -EIO) {
        return -ENODEV;
    }
    if (!schib.pmcw.dnv) {
        return false;
    }

    enable_subchannel(blk_schid);
    cutype = cu_type(blk_schid);

    /*
     * Note: we always have to run virtio_is_supported() here to make
     * sure that the vdev.senseid data gets pre-initialized correctly
     */
    is_virtio = virtio_is_supported(blk_schid);

    /* No specific devno given, just return whether the device is possibly bootable */
    if (dev_no < 0) {
        switch (cutype) {
        case CU_TYPE_VIRTIO:
            if (is_virtio) {
                /*
                 * Skip net devices since no IPLB is created and therefore
                 * no network bootloader has been loaded
                 */
                if (virtio_get_device_type() != VIRTIO_ID_NET) {
                    return true;
                }
            }
            return false;
        case CU_TYPE_DASD_3990:
        case CU_TYPE_DASD_2107:
            return true;
        default:
            return false;
        }
    }

    /* Caller asked for a specific devno */
    if (schib.pmcw.dev == dev_no) {
        return true;
    }

    return false;
}

/*
 * Find the subchannel connected to the given device (dev_no) and fill in the
 * subchannel information block (schib) with the connected subchannel's info.
 * NOTE: The global variable blk_schid is updated to contain the subchannel
 * information.
 *
 * If the caller gives dev_no=-1 then the user did not specify a boot device.
 * In this case we'll just use the first potentially bootable device we find.
 */
static bool find_subch(int dev_no)
{
    int i, r;

    for (i = 0; i < 0x10000; i++) {
        r = is_dev_possibly_bootable(dev_no, i);
        if (r < 0) {
            break;
        }
        if (r == true) {
            return true;
        }
    }

    return false;
}

static void menu_setup(void)
{
    if (memcmp(loadparm_str, LOADPARM_PROMPT, LOADPARM_LEN) == 0) {
        menu_set_parms(QIPL_FLAG_BM_OPTS_CMD, 0);
        return;
    }

    /* If loadparm was set to any other value, then do not enable menu */
    if (memcmp(loadparm_str, LOADPARM_EMPTY, LOADPARM_LEN) != 0) {
        return;
    }

    switch (iplb.pbt) {
    case S390_IPL_TYPE_CCW:
    case S390_IPL_TYPE_QEMU_SCSI:
        menu_set_parms(qipl.qipl_flags & BOOT_MENU_FLAG_MASK,
                       qipl.boot_menu_timeout);
        return;
    }
}

/*
 * Initialize the channel I/O subsystem so we can talk to our ipl/boot device.
 */
static void css_setup(void)
{
    /*
     * Unconditionally enable mss support. In every sane configuration this
     * will succeed; and even if it doesn't, stsch_err() can handle it.
     */
    enable_mss_facility();
}

/*
 * Collect various pieces of information from the hypervisor/hardware that
 * we'll use to determine exactly how we'll boot.
 */
static void boot_setup(void)
{
    char lpmsg[] = "LOADPARM=[________]\n";

    sclp_get_loadparm_ascii(loadparm_str);
    memcpy(lpmsg + 10, loadparm_str, 8);
    sclp_print(lpmsg);

    /*
     * Clear out any potential S390EP magic (see jump_to_low_kernel()),
     * so we don't taint our decision-making process during a reboot.
     */
    memset((char *)S390EP, 0, 6);

    have_iplb = store_iplb(&iplb);
}

static void find_boot_device(void)
{
    VDev *vdev = virtio_get_device();
    bool found;

    switch (iplb.pbt) {
    case S390_IPL_TYPE_CCW:
        debug_print_int("device no. ", iplb.ccw.devno);
        blk_schid.ssid = iplb.ccw.ssid & 0x3;
        debug_print_int("ssid ", blk_schid.ssid);
        found = find_subch(iplb.ccw.devno);
        break;
    case S390_IPL_TYPE_QEMU_SCSI:
        vdev->scsi_device_selected = true;
        vdev->selected_scsi_device.channel = iplb.scsi.channel;
        vdev->selected_scsi_device.target = iplb.scsi.target;
        vdev->selected_scsi_device.lun = iplb.scsi.lun;
        blk_schid.ssid = iplb.scsi.ssid & 0x3;
        found = find_subch(iplb.scsi.devno);
        break;
    default:
        panic("List-directed IPL not supported yet!\n");
    }

    IPL_assert(found, "Boot device not found\n");
}

static int virtio_setup(void)
{
    VDev *vdev = virtio_get_device();
    QemuIplParameters *early_qipl = (QemuIplParameters *)QIPL_ADDRESS;
    int ret;

    memcpy(&qipl, early_qipl, sizeof(QemuIplParameters));

    if (have_iplb) {
        menu_setup();
    }

    switch (vdev->senseid.cu_model) {
    case VIRTIO_ID_NET:
        sclp_print("Network boot device detected\n");
        vdev->netboot_start_addr = qipl.netboot_start_addr;
        return 0;
    case VIRTIO_ID_BLOCK:
        ret = virtio_blk_setup_device(blk_schid);
        break;
    case VIRTIO_ID_SCSI:
        ret = virtio_scsi_setup_device(blk_schid);
        break;
    default:
        panic("\n! No IPL device available !\n");
    }

    if (!ret) {
        IPL_assert(virtio_ipl_disk_is_valid(), "No valid IPL device detected");
    }

    return ret;
}

static void ipl_boot_device(void)
{
    switch (cutype) {
    case CU_TYPE_DASD_3990:
    case CU_TYPE_DASD_2107:
        dasd_ipl(blk_schid, cutype); /* no return */
        break;
    case CU_TYPE_VIRTIO:
        if (virtio_setup() == 0) {
            zipl_load();             /* Only returns in case of errors */
        }
        break;
    default:
        print_int("Attempting to boot from unexpected device type", cutype);
        panic("\nBoot failed.\n");
    }
}

/*
 * No boot device has been specified, so we have to scan through the
 * channels to find one.
 */
static void probe_boot_device(void)
{
    int ssid, sch_no, ret;

    for (ssid = 0; ssid < 0x3; ssid++) {
        blk_schid.ssid = ssid;
        for (sch_no = 0; sch_no < 0x10000; sch_no++) {
            ret = is_dev_possibly_bootable(-1, sch_no);
            if (ret < 0) {
                break;
            }
            if (ret == true) {
                ipl_boot_device();      /* Only returns if unsuccessful */
            }
        }
    }

    sclp_print("Could not find a suitable boot device (none specified)\n");
}

void main(void)
{
    sclp_setup();
    css_setup();
    boot_setup();
    if (have_iplb) {
        find_boot_device();
        ipl_boot_device();
    } else {
        probe_boot_device();
    }

    panic("Failed to load OS from hard disk\n");
}
