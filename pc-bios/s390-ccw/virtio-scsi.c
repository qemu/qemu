/*
 * Virtio-SCSI implementation for s390 machine loader for qemu
 *
 * Copyright 2015 IBM Corp.
 * Author: Eugene "jno" Dvurechenski <jno@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "libc.h"
#include "s390-ccw.h"
#include "virtio.h"
#include "scsi.h"
#include "virtio-scsi.h"
#include "s390-time.h"
#include "helper.h"

static ScsiDevice default_scsi_device;
static VirtioScsiCmdReq req;
static VirtioScsiCmdResp resp;

static uint8_t scsi_inquiry_std_response[256];
static ScsiInquiryEvpdPages scsi_inquiry_evpd_pages_response;
static ScsiInquiryEvpdBl scsi_inquiry_evpd_bl_response;

static inline void vs_assert(bool term, const char **msgs)
{
    if (!term) {
        int i = 0;

        sclp_print("\n! ");
        while (msgs[i]) {
            sclp_print(msgs[i++]);
        }
        panic(" !\n");
    }
}

static void virtio_scsi_verify_response(VirtioScsiCmdResp *resp,
                                        const char *title)
{
    const char *mr[] = {
        title, ": response ", virtio_scsi_response_msg(resp), 0
    };
    const char *ms[] = {
        title,
        CDB_STATUS_VALID(resp->status) ? ": " : ": invalid ",
        scsi_cdb_status_msg(resp->status),
        resp->status == CDB_STATUS_CHECK_CONDITION ? " " : 0,
        resp->sense_len ? scsi_cdb_asc_msg(resp->sense)
                        : "no sense data",
        scsi_sense_response(resp->sense)  == 0x70 ? ", sure" : "?",
        0
    };

    vs_assert(resp->response == VIRTIO_SCSI_S_OK, mr);
    vs_assert(resp->status == CDB_STATUS_GOOD, ms);
}

static void prepare_request(VDev *vdev, const void *cdb, int cdb_size,
                            void *data, uint32_t data_size)
{
    const ScsiDevice *sdev = vdev->scsi_device;

    memset(&req, 0, sizeof(req));
    req.lun = make_lun(sdev->channel, sdev->target, sdev->lun);
    memcpy(&req.cdb, cdb, cdb_size);

    memset(&resp, 0, sizeof(resp));
    resp.status = 0xff;     /* set invalid  */
    resp.response = 0xff;   /*              */

    if (data && data_size) {
        memset(data, 0, data_size);
    }
}

static inline void vs_io_assert(bool term, const char *msg)
{
    if (!term) {
        virtio_scsi_verify_response(&resp, msg);
    }
}

static void vs_run(const char *title, VirtioCmd *cmd, VDev *vdev,
                   const void *cdb, int cdb_size,
                   void *data, uint32_t data_size)
{
    prepare_request(vdev, cdb, cdb_size, data, data_size);
    vs_io_assert(virtio_run(vdev, VR_REQUEST, cmd) == 0, title);
}

/* SCSI protocol implementation routines */

static bool scsi_inquiry(VDev *vdev, uint8_t evpd, uint8_t page,
                         void *data, uint32_t data_size)
{
    ScsiCdbInquiry cdb = {
        .command = 0x12,
        .b1 = evpd,
        .b2 = page,
        .alloc_len = data_size < 65535 ? data_size : 65535,
    };
    VirtioCmd inquiry[] = {
        { &req, sizeof(req), VRING_DESC_F_NEXT },
        { &resp, sizeof(resp), VRING_DESC_F_WRITE | VRING_DESC_F_NEXT },
        { data, data_size, VRING_DESC_F_WRITE },
    };

    vs_run("inquiry", inquiry, vdev, &cdb, sizeof(cdb), data, data_size);

    return virtio_scsi_response_ok(&resp);
}

static bool scsi_test_unit_ready(VDev *vdev)
{
    ScsiCdbTestUnitReady cdb = {
        .command = 0x00,
    };
    VirtioCmd test_unit_ready[] = {
        { &req, sizeof(req), VRING_DESC_F_NEXT },
        { &resp, sizeof(resp), VRING_DESC_F_WRITE },
    };

    prepare_request(vdev, &cdb, sizeof(cdb), 0, 0);
    virtio_run(vdev, VR_REQUEST, test_unit_ready); /* ignore errors here */

    return virtio_scsi_response_ok(&resp);
}

static bool scsi_report_luns(VDev *vdev, void *data, uint32_t data_size)
{
    ScsiCdbReportLuns cdb = {
        .command = 0xa0,
        .select_report = 0x02, /* REPORT ALL */
        .alloc_len = data_size,
    };
    VirtioCmd report_luns[] = {
        { &req, sizeof(req), VRING_DESC_F_NEXT },
        { &resp, sizeof(resp), VRING_DESC_F_WRITE | VRING_DESC_F_NEXT },
        { data, data_size, VRING_DESC_F_WRITE },
    };

    vs_run("report luns", report_luns,
           vdev, &cdb, sizeof(cdb), data, data_size);

    return virtio_scsi_response_ok(&resp);
}

static bool scsi_read_10(VDev *vdev,
                         unsigned long sector, int sectors, void *data,
                         unsigned int data_size)
{
    ScsiCdbRead10 cdb = {
        .command = 0x28,
        .lba = sector,
        .xfer_length = sectors,
    };
    VirtioCmd read_10[] = {
        { &req, sizeof(req), VRING_DESC_F_NEXT },
        { &resp, sizeof(resp), VRING_DESC_F_WRITE | VRING_DESC_F_NEXT },
        { data, data_size, VRING_DESC_F_WRITE },
    };

    debug_print_int("read_10  sector", sector);
    debug_print_int("read_10 sectors", sectors);

    vs_run("read(10)", read_10, vdev, &cdb, sizeof(cdb), data, data_size);

    return virtio_scsi_response_ok(&resp);
}

static bool scsi_read_capacity(VDev *vdev,
                               void *data, uint32_t data_size)
{
    ScsiCdbReadCapacity16 cdb = {
        .command = 0x9e, /* SERVICE_ACTION_IN_16 */
        .service_action = 0x10, /* SA_READ_CAPACITY */
        .alloc_len = data_size,
    };
    VirtioCmd read_capacity_16[] = {
        { &req, sizeof(req), VRING_DESC_F_NEXT },
        { &resp, sizeof(resp), VRING_DESC_F_WRITE | VRING_DESC_F_NEXT },
        { data, data_size, VRING_DESC_F_WRITE },
    };

    vs_run("read capacity", read_capacity_16,
           vdev, &cdb, sizeof(cdb), data, data_size);

    return virtio_scsi_response_ok(&resp);
}

/* virtio-scsi routines */

/*
 * Tries to locate a SCSI device and adds the information for the found
 * device to the vdev->scsi_device structure.
 * Returns 0 if SCSI device could be located, or a error code < 0 otherwise
 */
static int virtio_scsi_locate_device(VDev *vdev)
{
    const uint16_t channel = 0; /* again, it's what QEMU does */
    uint16_t target;
    static uint8_t data[16 + 8 * 63];
    ScsiLunReport *r = (void *) data;
    ScsiDevice *sdev = vdev->scsi_device;
    int i, luns;

    /* QEMU has hardcoded channel #0 in many places.
     * If this hardcoded value is ever changed, we'll need to add code for
     * vdev->config.scsi.max_channel != 0 here.
     */
    debug_print_int("config.scsi.max_channel", vdev->config.scsi.max_channel);
    debug_print_int("config.scsi.max_target ", vdev->config.scsi.max_target);
    debug_print_int("config.scsi.max_lun    ", vdev->config.scsi.max_lun);
    debug_print_int("config.scsi.max_sectors", vdev->config.scsi.max_sectors);

    if (vdev->scsi_device_selected) {
        sdev->channel = vdev->selected_scsi_device.channel;
        sdev->target = vdev->selected_scsi_device.target;
        sdev->lun = vdev->selected_scsi_device.lun;

        IPL_check(sdev->channel == 0, "non-zero channel requested");
        IPL_check(sdev->target <= vdev->config.scsi.max_target, "target# high");
        IPL_check(sdev->lun <= vdev->config.scsi.max_lun, "LUN# high");
        return 0;
    }

    for (target = 0; target <= vdev->config.scsi.max_target; target++) {
        sdev->channel = channel;
        sdev->target = target;
        sdev->lun = 0;          /* LUN has to be 0 for REPORT LUNS */
        if (!scsi_report_luns(vdev, data, sizeof(data))) {
            if (resp.response == VIRTIO_SCSI_S_BAD_TARGET) {
                continue;
            }
            print_int("target", target);
            virtio_scsi_verify_response(&resp, "SCSI cannot report LUNs");
        }
        if (r->lun_list_len == 0) {
            print_int("no LUNs for target", target);
            continue;
        }
        luns = r->lun_list_len / 8;
        debug_print_int("LUNs reported", luns);
        if (luns == 1) {
            /* There is no ",lun=#" arg for -device or ",lun=0" given.
             * Hence, the only LUN reported.
             * Usually, it's 0.
             */
            sdev->lun = r->lun[0].v16[0]; /* it's returned this way */
            debug_print_int("Have to use LUN", sdev->lun);
            return 0; /* we have to use this device */
        }
        for (i = 0; i < luns; i++) {
            if (r->lun[i].v64) {
                /* Look for non-zero LUN - we have where to choose from */
                sdev->lun = r->lun[i].v16[0];
                debug_print_int("Will use LUN", sdev->lun);
                return 0; /* we have found a device */
            }
        }
    }

    sclp_print("Warning: Could not locate a usable virtio-scsi device\n");
    return -ENODEV;
}

int virtio_scsi_read_many(VDev *vdev,
                          unsigned long sector, void *load_addr, int sec_num)
{
    int sector_count;
    int f = vdev->blk_factor;
    unsigned int data_size;
    unsigned int max_transfer = MIN_NON_ZERO(vdev->config.scsi.max_sectors,
                                             vdev->max_transfer);

    do {
        sector_count = MIN_NON_ZERO(sec_num, max_transfer);
        data_size = sector_count * virtio_get_block_size() * f;
        if (!scsi_read_10(vdev, sector * f, sector_count * f, load_addr,
                          data_size)) {
            virtio_scsi_verify_response(&resp, "virtio-scsi:read_many");
        }
        load_addr += data_size;
        sector += sector_count;
        sec_num -= sector_count;
    } while (sec_num > 0);

    return 0;
}

static bool virtio_scsi_inquiry_response_is_cdrom(void *data)
{
    const ScsiInquiryStd *response = data;
    const int resp_data_fmt = response->b3 & 0x0f;
    int i;

    IPL_check(resp_data_fmt == 2, "Wrong INQUIRY response format");
    if (resp_data_fmt != 2) {
        return false; /* cannot decode */
    }

    if ((response->peripheral_qdt & 0x1f) == SCSI_INQ_RDT_CDROM) {
        return true;
    }

    for (i = 0; i < sizeof(response->prod_id); i++) {
        if (response->prod_id[i] != QEMU_CDROM_SIGNATURE[i]) {
            return false;
        }
    }
    return true;
}

static void scsi_parse_capacity_report(void *data,
                                       uint64_t *last_lba, uint32_t *lb_len)
{
    ScsiReadCapacity16Data *p = data;

    if (last_lba) {
        *last_lba = p->ret_lba;
    }

    if (lb_len) {
        *lb_len = p->lb_len;
    }
}

static int virtio_scsi_setup(VDev *vdev)
{
    int retry_test_unit_ready = 3;
    uint8_t data[256];
    uint32_t data_size = sizeof(data);
    ScsiInquiryEvpdPages *evpd = &scsi_inquiry_evpd_pages_response;
    ScsiInquiryEvpdBl *evpd_bl = &scsi_inquiry_evpd_bl_response;
    int i, ret;

    vdev->scsi_device = &default_scsi_device;
    ret = virtio_scsi_locate_device(vdev);
    if (ret < 0) {
        return ret;
    }

    /* We have to "ping" the device before it becomes readable */
    while (!scsi_test_unit_ready(vdev)) {

        if (!virtio_scsi_response_ok(&resp)) {
            uint8_t code = resp.sense[0] & SCSI_SENSE_CODE_MASK;
            uint8_t sense_key = resp.sense[2] & SCSI_SENSE_KEY_MASK;

            IPL_assert(resp.sense_len != 0, "virtio-scsi:setup: no SENSE data");

            IPL_assert(retry_test_unit_ready && code == 0x70 &&
                       sense_key == SCSI_SENSE_KEY_UNIT_ATTENTION,
                       "virtio-scsi:setup: cannot retry");

            /* retry on CHECK_CONDITION/UNIT_ATTENTION as it
             * may not designate a real error, but it may be
             * a result of device reset, etc.
             */
            retry_test_unit_ready--;
            sleep(1);
            continue;
        }

        virtio_scsi_verify_response(&resp, "virtio-scsi:setup");
    }

    /* read and cache SCSI INQUIRY response */
    if (!scsi_inquiry(vdev,
                      SCSI_INQUIRY_STANDARD,
                      SCSI_INQUIRY_STANDARD_NONE,
                      scsi_inquiry_std_response,
                      sizeof(scsi_inquiry_std_response))) {
        virtio_scsi_verify_response(&resp, "virtio-scsi:setup:inquiry");
    }

    if (virtio_scsi_inquiry_response_is_cdrom(scsi_inquiry_std_response)) {
        sclp_print("SCSI CD-ROM detected.\n");
        vdev->is_cdrom = true;
        vdev->scsi_block_size = VIRTIO_ISO_BLOCK_SIZE;
    }

    if (!scsi_inquiry(vdev,
                      SCSI_INQUIRY_EVPD,
                      SCSI_INQUIRY_EVPD_SUPPORTED_PAGES,
                      evpd,
                      sizeof(*evpd))) {
        virtio_scsi_verify_response(&resp, "virtio-scsi:setup:supported_pages");
    }

    debug_print_int("EVPD length", evpd->page_length);

    for (i = 0; i <= evpd->page_length; i++) {
        debug_print_int("supported EVPD page", evpd->byte[i]);

        if (evpd->byte[i] != SCSI_INQUIRY_EVPD_BLOCK_LIMITS) {
            continue;
        }

        if (!scsi_inquiry(vdev,
                          SCSI_INQUIRY_EVPD,
                          SCSI_INQUIRY_EVPD_BLOCK_LIMITS,
                          evpd_bl,
                          sizeof(*evpd_bl))) {
            virtio_scsi_verify_response(&resp, "virtio-scsi:setup:blocklimits");
        }

        debug_print_int("max transfer", evpd_bl->max_transfer);
        vdev->max_transfer = evpd_bl->max_transfer;
    }

    /*
     * The host sg driver will often be unhappy with particularly large
     * I/Os that exceed the block iovec limits.  Let's enforce something
     * reasonable, despite what the device configuration tells us.
     */

    vdev->max_transfer = MIN_NON_ZERO(VIRTIO_SCSI_MAX_SECTORS,
                                      vdev->max_transfer);

    if (!scsi_read_capacity(vdev, data, data_size)) {
        virtio_scsi_verify_response(&resp, "virtio-scsi:setup:read_capacity");
    }
    scsi_parse_capacity_report(data, &vdev->scsi_last_block,
                               (uint32_t *) &vdev->scsi_block_size);

    return 0;
}

int virtio_scsi_setup_device(SubChannelId schid)
{
    VDev *vdev = virtio_get_device();

    vdev->schid = schid;
    virtio_setup_ccw(vdev);

    IPL_assert(vdev->config.scsi.sense_size == VIRTIO_SCSI_SENSE_SIZE,
               "Config: sense size mismatch");
    IPL_assert(vdev->config.scsi.cdb_size == VIRTIO_SCSI_CDB_SIZE,
               "Config: CDB size mismatch");

    sclp_print("Using virtio-scsi.\n");

    return virtio_scsi_setup(vdev);
}
