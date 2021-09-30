/*
 * QEMU MegaRAID SAS 8708EM2 Host Bus Adapter emulation
 * Based on the linux driver code at drivers/scsi/megaraid
 *
 * Copyright (c) 2009-2012 Hannes Reinecke, SUSE Labs
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "sysemu/dma.h"
#include "sysemu/block-backend.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "qemu/iov.h"
#include "qemu/module.h"
#include "hw/scsi/scsi.h"
#include "scsi/constants.h"
#include "trace.h"
#include "qapi/error.h"
#include "mfi.h"
#include "migration/vmstate.h"
#include "qom/object.h"

#define MEGASAS_VERSION_GEN1 "1.70"
#define MEGASAS_VERSION_GEN2 "1.80"
#define MEGASAS_MAX_FRAMES 2048         /* Firmware limit at 65535 */
#define MEGASAS_DEFAULT_FRAMES 1000     /* Windows requires this */
#define MEGASAS_GEN2_DEFAULT_FRAMES 1008     /* Windows requires this */
#define MEGASAS_MAX_SGE 128             /* Firmware limit */
#define MEGASAS_DEFAULT_SGE 80
#define MEGASAS_MAX_SECTORS 0xFFFF      /* No real limit */
#define MEGASAS_MAX_ARRAYS 128

#define MEGASAS_HBA_SERIAL "QEMU123456"
#define NAA_LOCALLY_ASSIGNED_ID 0x3ULL
#define IEEE_COMPANY_LOCALLY_ASSIGNED 0x525400

#define MEGASAS_FLAG_USE_JBOD      0
#define MEGASAS_MASK_USE_JBOD      (1 << MEGASAS_FLAG_USE_JBOD)
#define MEGASAS_FLAG_USE_QUEUE64   1
#define MEGASAS_MASK_USE_QUEUE64   (1 << MEGASAS_FLAG_USE_QUEUE64)

typedef struct MegasasCmd {
    uint32_t index;
    uint16_t flags;
    uint16_t count;
    uint64_t context;

    hwaddr pa;
    hwaddr pa_size;
    uint32_t dcmd_opcode;
    union mfi_frame *frame;
    SCSIRequest *req;
    QEMUSGList qsg;
    void *iov_buf;
    size_t iov_size;
    size_t iov_offset;
    struct MegasasState *state;
} MegasasCmd;

struct MegasasState {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

    MemoryRegion mmio_io;
    MemoryRegion port_io;
    MemoryRegion queue_io;
    uint32_t frame_hi;

    uint32_t fw_state;
    uint32_t fw_sge;
    uint32_t fw_cmds;
    uint32_t flags;
    uint32_t fw_luns;
    uint32_t intr_mask;
    uint32_t doorbell;
    uint32_t busy;
    uint32_t diag;
    uint32_t adp_reset;
    OnOffAuto msi;
    OnOffAuto msix;

    MegasasCmd *event_cmd;
    uint16_t event_locale;
    int event_class;
    uint32_t event_count;
    uint32_t shutdown_event;
    uint32_t boot_event;

    uint64_t sas_addr;
    char *hba_serial;

    uint64_t reply_queue_pa;
    void *reply_queue;
    uint16_t reply_queue_len;
    uint16_t reply_queue_head;
    uint16_t reply_queue_tail;
    uint64_t consumer_pa;
    uint64_t producer_pa;

    MegasasCmd frames[MEGASAS_MAX_FRAMES];
    DECLARE_BITMAP(frame_map, MEGASAS_MAX_FRAMES);
    SCSIBus bus;
};
typedef struct MegasasState MegasasState;

struct MegasasBaseClass {
    PCIDeviceClass parent_class;
    const char *product_name;
    const char *product_version;
    int mmio_bar;
    int ioport_bar;
    int osts;
};
typedef struct MegasasBaseClass MegasasBaseClass;

#define TYPE_MEGASAS_BASE "megasas-base"
#define TYPE_MEGASAS_GEN1 "megasas"
#define TYPE_MEGASAS_GEN2 "megasas-gen2"

DECLARE_OBJ_CHECKERS(MegasasState, MegasasBaseClass,
                     MEGASAS, TYPE_MEGASAS_BASE)


#define MEGASAS_INTR_DISABLED_MASK 0xFFFFFFFF

static bool megasas_intr_enabled(MegasasState *s)
{
    if ((s->intr_mask & MEGASAS_INTR_DISABLED_MASK) !=
        MEGASAS_INTR_DISABLED_MASK) {
        return true;
    }
    return false;
}

static bool megasas_use_queue64(MegasasState *s)
{
    return s->flags & MEGASAS_MASK_USE_QUEUE64;
}

static bool megasas_use_msix(MegasasState *s)
{
    return s->msix != ON_OFF_AUTO_OFF;
}

static bool megasas_is_jbod(MegasasState *s)
{
    return s->flags & MEGASAS_MASK_USE_JBOD;
}

static void megasas_frame_set_cmd_status(MegasasState *s,
                                         unsigned long frame, uint8_t v)
{
    PCIDevice *pci = &s->parent_obj;
    stb_pci_dma(pci, frame + offsetof(struct mfi_frame_header, cmd_status), v);
}

static void megasas_frame_set_scsi_status(MegasasState *s,
                                          unsigned long frame, uint8_t v)
{
    PCIDevice *pci = &s->parent_obj;
    stb_pci_dma(pci, frame + offsetof(struct mfi_frame_header, scsi_status), v);
}

static inline const char *mfi_frame_desc(unsigned int cmd)
{
    static const char *mfi_frame_descs[] = {
        "MFI init", "LD Read", "LD Write", "LD SCSI", "PD SCSI",
        "MFI Doorbell", "MFI Abort", "MFI SMP", "MFI Stop"
    };

    if (cmd < ARRAY_SIZE(mfi_frame_descs)) {
        return mfi_frame_descs[cmd];
    }

    return "Unknown";
}

/*
 * Context is considered opaque, but the HBA firmware is running
 * in little endian mode. So convert it to little endian, too.
 */
static uint64_t megasas_frame_get_context(MegasasState *s,
                                          unsigned long frame)
{
    PCIDevice *pci = &s->parent_obj;
    return ldq_le_pci_dma(pci, frame + offsetof(struct mfi_frame_header, context));
}

static bool megasas_frame_is_ieee_sgl(MegasasCmd *cmd)
{
    return cmd->flags & MFI_FRAME_IEEE_SGL;
}

static bool megasas_frame_is_sgl64(MegasasCmd *cmd)
{
    return cmd->flags & MFI_FRAME_SGL64;
}

static bool megasas_frame_is_sense64(MegasasCmd *cmd)
{
    return cmd->flags & MFI_FRAME_SENSE64;
}

static uint64_t megasas_sgl_get_addr(MegasasCmd *cmd,
                                     union mfi_sgl *sgl)
{
    uint64_t addr;

    if (megasas_frame_is_ieee_sgl(cmd)) {
        addr = le64_to_cpu(sgl->sg_skinny->addr);
    } else if (megasas_frame_is_sgl64(cmd)) {
        addr = le64_to_cpu(sgl->sg64->addr);
    } else {
        addr = le32_to_cpu(sgl->sg32->addr);
    }
    return addr;
}

static uint32_t megasas_sgl_get_len(MegasasCmd *cmd,
                                    union mfi_sgl *sgl)
{
    uint32_t len;

    if (megasas_frame_is_ieee_sgl(cmd)) {
        len = le32_to_cpu(sgl->sg_skinny->len);
    } else if (megasas_frame_is_sgl64(cmd)) {
        len = le32_to_cpu(sgl->sg64->len);
    } else {
        len = le32_to_cpu(sgl->sg32->len);
    }
    return len;
}

static union mfi_sgl *megasas_sgl_next(MegasasCmd *cmd,
                                       union mfi_sgl *sgl)
{
    uint8_t *next = (uint8_t *)sgl;

    if (megasas_frame_is_ieee_sgl(cmd)) {
        next += sizeof(struct mfi_sg_skinny);
    } else if (megasas_frame_is_sgl64(cmd)) {
        next += sizeof(struct mfi_sg64);
    } else {
        next += sizeof(struct mfi_sg32);
    }

    if (next >= (uint8_t *)cmd->frame + cmd->pa_size) {
        return NULL;
    }
    return (union mfi_sgl *)next;
}

static void megasas_soft_reset(MegasasState *s);

static int megasas_map_sgl(MegasasState *s, MegasasCmd *cmd, union mfi_sgl *sgl)
{
    int i;
    int iov_count = 0;
    size_t iov_size = 0;

    cmd->flags = le16_to_cpu(cmd->frame->header.flags);
    iov_count = cmd->frame->header.sge_count;
    if (!iov_count || iov_count > MEGASAS_MAX_SGE) {
        trace_megasas_iovec_sgl_overflow(cmd->index, iov_count,
                                         MEGASAS_MAX_SGE);
        return -1;
    }
    pci_dma_sglist_init(&cmd->qsg, PCI_DEVICE(s), iov_count);
    for (i = 0; i < iov_count; i++) {
        dma_addr_t iov_pa, iov_size_p;

        if (!sgl) {
            trace_megasas_iovec_sgl_underflow(cmd->index, i);
            goto unmap;
        }
        iov_pa = megasas_sgl_get_addr(cmd, sgl);
        iov_size_p = megasas_sgl_get_len(cmd, sgl);
        if (!iov_pa || !iov_size_p) {
            trace_megasas_iovec_sgl_invalid(cmd->index, i,
                                            iov_pa, iov_size_p);
            goto unmap;
        }
        qemu_sglist_add(&cmd->qsg, iov_pa, iov_size_p);
        sgl = megasas_sgl_next(cmd, sgl);
        iov_size += (size_t)iov_size_p;
    }
    if (cmd->iov_size > iov_size) {
        trace_megasas_iovec_overflow(cmd->index, iov_size, cmd->iov_size);
    } else if (cmd->iov_size < iov_size) {
        trace_megasas_iovec_underflow(cmd->index, iov_size, cmd->iov_size);
    }
    cmd->iov_offset = 0;
    return 0;
unmap:
    qemu_sglist_destroy(&cmd->qsg);
    return -1;
}

/*
 * passthrough sense and io sense are at the same offset
 */
static int megasas_build_sense(MegasasCmd *cmd, uint8_t *sense_ptr,
    uint8_t sense_len)
{
    PCIDevice *pcid = PCI_DEVICE(cmd->state);
    uint32_t pa_hi = 0, pa_lo;
    hwaddr pa;
    int frame_sense_len;

    frame_sense_len = cmd->frame->header.sense_len;
    if (sense_len > frame_sense_len) {
        sense_len = frame_sense_len;
    }
    if (sense_len) {
        pa_lo = le32_to_cpu(cmd->frame->pass.sense_addr_lo);
        if (megasas_frame_is_sense64(cmd)) {
            pa_hi = le32_to_cpu(cmd->frame->pass.sense_addr_hi);
        }
        pa = ((uint64_t) pa_hi << 32) | pa_lo;
        pci_dma_write(pcid, pa, sense_ptr, sense_len);
        cmd->frame->header.sense_len = sense_len;
    }
    return sense_len;
}

static void megasas_write_sense(MegasasCmd *cmd, SCSISense sense)
{
    uint8_t sense_buf[SCSI_SENSE_BUF_SIZE];
    uint8_t sense_len = 18;

    memset(sense_buf, 0, sense_len);
    sense_buf[0] = 0xf0;
    sense_buf[2] = sense.key;
    sense_buf[7] = 10;
    sense_buf[12] = sense.asc;
    sense_buf[13] = sense.ascq;
    megasas_build_sense(cmd, sense_buf, sense_len);
}

static void megasas_copy_sense(MegasasCmd *cmd)
{
    uint8_t sense_buf[SCSI_SENSE_BUF_SIZE];
    uint8_t sense_len;

    sense_len = scsi_req_get_sense(cmd->req, sense_buf,
                                   SCSI_SENSE_BUF_SIZE);
    megasas_build_sense(cmd, sense_buf, sense_len);
}

/*
 * Format an INQUIRY CDB
 */
static int megasas_setup_inquiry(uint8_t *cdb, int pg, int len)
{
    memset(cdb, 0, 6);
    cdb[0] = INQUIRY;
    if (pg > 0) {
        cdb[1] = 0x1;
        cdb[2] = pg;
    }
    cdb[3] = (len >> 8) & 0xff;
    cdb[4] = (len & 0xff);
    return len;
}

/*
 * Encode lba and len into a READ_16/WRITE_16 CDB
 */
static void megasas_encode_lba(uint8_t *cdb, uint64_t lba,
                               uint32_t len, bool is_write)
{
    memset(cdb, 0x0, 16);
    if (is_write) {
        cdb[0] = WRITE_16;
    } else {
        cdb[0] = READ_16;
    }
    cdb[2] = (lba >> 56) & 0xff;
    cdb[3] = (lba >> 48) & 0xff;
    cdb[4] = (lba >> 40) & 0xff;
    cdb[5] = (lba >> 32) & 0xff;
    cdb[6] = (lba >> 24) & 0xff;
    cdb[7] = (lba >> 16) & 0xff;
    cdb[8] = (lba >> 8) & 0xff;
    cdb[9] = (lba) & 0xff;
    cdb[10] = (len >> 24) & 0xff;
    cdb[11] = (len >> 16) & 0xff;
    cdb[12] = (len >> 8) & 0xff;
    cdb[13] = (len) & 0xff;
}

/*
 * Utility functions
 */
static uint64_t megasas_fw_time(void)
{
    struct tm curtime;

    qemu_get_timedate(&curtime, 0);
    return ((uint64_t)curtime.tm_sec & 0xff) << 48 |
        ((uint64_t)curtime.tm_min & 0xff)  << 40 |
        ((uint64_t)curtime.tm_hour & 0xff) << 32 |
        ((uint64_t)curtime.tm_mday & 0xff) << 24 |
        ((uint64_t)curtime.tm_mon & 0xff)  << 16 |
        ((uint64_t)(curtime.tm_year + 1900) & 0xffff);
}

/*
 * Default disk sata address
 * 0x1221 is the magic number as
 * present in real hardware,
 * so use it here, too.
 */
static uint64_t megasas_get_sata_addr(uint16_t id)
{
    uint64_t addr = (0x1221ULL << 48);
    return addr | ((uint64_t)id << 24);
}

/*
 * Frame handling
 */
static int megasas_next_index(MegasasState *s, int index, int limit)
{
    index++;
    if (index == limit) {
        index = 0;
    }
    return index;
}

static MegasasCmd *megasas_lookup_frame(MegasasState *s,
    hwaddr frame)
{
    MegasasCmd *cmd = NULL;
    int num = 0, index;

    index = s->reply_queue_head;

    while (num < s->fw_cmds && index < MEGASAS_MAX_FRAMES) {
        if (s->frames[index].pa && s->frames[index].pa == frame) {
            cmd = &s->frames[index];
            break;
        }
        index = megasas_next_index(s, index, s->fw_cmds);
        num++;
    }

    return cmd;
}

static void megasas_unmap_frame(MegasasState *s, MegasasCmd *cmd)
{
    PCIDevice *p = PCI_DEVICE(s);

    if (cmd->pa_size) {
        pci_dma_unmap(p, cmd->frame, cmd->pa_size, 0, 0);
    }
    cmd->frame = NULL;
    cmd->pa = 0;
    cmd->pa_size = 0;
    qemu_sglist_destroy(&cmd->qsg);
    clear_bit(cmd->index, s->frame_map);
}

/*
 * This absolutely needs to be locked if
 * qemu ever goes multithreaded.
 */
static MegasasCmd *megasas_enqueue_frame(MegasasState *s,
    hwaddr frame, uint64_t context, int count)
{
    PCIDevice *pcid = PCI_DEVICE(s);
    MegasasCmd *cmd = NULL;
    int frame_size = MEGASAS_MAX_SGE * sizeof(union mfi_sgl);
    hwaddr frame_size_p = frame_size;
    unsigned long index;

    index = 0;
    while (index < s->fw_cmds) {
        index = find_next_zero_bit(s->frame_map, s->fw_cmds, index);
        if (!s->frames[index].pa)
            break;
        /* Busy frame found */
        trace_megasas_qf_mapped(index);
    }
    if (index >= s->fw_cmds) {
        /* All frames busy */
        trace_megasas_qf_busy(frame);
        return NULL;
    }
    cmd = &s->frames[index];
    set_bit(index, s->frame_map);
    trace_megasas_qf_new(index, frame);

    cmd->pa = frame;
    /* Map all possible frames */
    cmd->frame = pci_dma_map(pcid, frame, &frame_size_p, 0);
    if (!cmd->frame || frame_size_p != frame_size) {
        trace_megasas_qf_map_failed(cmd->index, (unsigned long)frame);
        if (cmd->frame) {
            megasas_unmap_frame(s, cmd);
        }
        s->event_count++;
        return NULL;
    }
    cmd->pa_size = frame_size_p;
    cmd->context = context;
    if (!megasas_use_queue64(s)) {
        cmd->context &= (uint64_t)0xFFFFFFFF;
    }
    cmd->count = count;
    cmd->dcmd_opcode = -1;
    s->busy++;

    if (s->consumer_pa) {
        s->reply_queue_tail = ldl_le_pci_dma(pcid, s->consumer_pa);
    }
    trace_megasas_qf_enqueue(cmd->index, cmd->count, cmd->context,
                             s->reply_queue_head, s->reply_queue_tail, s->busy);

    return cmd;
}

static void megasas_complete_frame(MegasasState *s, uint64_t context)
{
    PCIDevice *pci_dev = PCI_DEVICE(s);
    int tail, queue_offset;

    /* Decrement busy count */
    s->busy--;
    if (s->reply_queue_pa) {
        /*
         * Put command on the reply queue.
         * Context is opaque, but emulation is running in
         * little endian. So convert it.
         */
        if (megasas_use_queue64(s)) {
            queue_offset = s->reply_queue_head * sizeof(uint64_t);
            stq_le_pci_dma(pci_dev, s->reply_queue_pa + queue_offset, context);
        } else {
            queue_offset = s->reply_queue_head * sizeof(uint32_t);
            stl_le_pci_dma(pci_dev, s->reply_queue_pa + queue_offset, context);
        }
        s->reply_queue_tail = ldl_le_pci_dma(pci_dev, s->consumer_pa);
        trace_megasas_qf_complete(context, s->reply_queue_head,
                                  s->reply_queue_tail, s->busy);
    }

    if (megasas_intr_enabled(s)) {
        /* Update reply queue pointer */
        s->reply_queue_tail = ldl_le_pci_dma(pci_dev, s->consumer_pa);
        tail = s->reply_queue_head;
        s->reply_queue_head = megasas_next_index(s, tail, s->fw_cmds);
        trace_megasas_qf_update(s->reply_queue_head, s->reply_queue_tail,
                                s->busy);
        stl_le_pci_dma(pci_dev, s->producer_pa, s->reply_queue_head);
        /* Notify HBA */
        if (msix_enabled(pci_dev)) {
            trace_megasas_msix_raise(0);
            msix_notify(pci_dev, 0);
        } else if (msi_enabled(pci_dev)) {
            trace_megasas_msi_raise(0);
            msi_notify(pci_dev, 0);
        } else {
            s->doorbell++;
            if (s->doorbell == 1) {
                trace_megasas_irq_raise();
                pci_irq_assert(pci_dev);
            }
        }
    } else {
        trace_megasas_qf_complete_noirq(context);
    }
}

static void megasas_complete_command(MegasasCmd *cmd)
{
    cmd->iov_size = 0;
    cmd->iov_offset = 0;

    cmd->req->hba_private = NULL;
    scsi_req_unref(cmd->req);
    cmd->req = NULL;

    megasas_unmap_frame(cmd->state, cmd);
    megasas_complete_frame(cmd->state, cmd->context);
}

static void megasas_reset_frames(MegasasState *s)
{
    int i;
    MegasasCmd *cmd;

    for (i = 0; i < s->fw_cmds; i++) {
        cmd = &s->frames[i];
        if (cmd->pa) {
            megasas_unmap_frame(s, cmd);
        }
    }
    bitmap_zero(s->frame_map, MEGASAS_MAX_FRAMES);
}

static void megasas_abort_command(MegasasCmd *cmd)
{
    /* Never abort internal commands.  */
    if (cmd->dcmd_opcode != -1) {
        return;
    }
    if (cmd->req != NULL) {
        scsi_req_cancel(cmd->req);
    }
}

static int megasas_init_firmware(MegasasState *s, MegasasCmd *cmd)
{
    PCIDevice *pcid = PCI_DEVICE(s);
    uint32_t pa_hi, pa_lo;
    hwaddr iq_pa, initq_size = sizeof(struct mfi_init_qinfo);
    struct mfi_init_qinfo *initq = NULL;
    uint32_t flags;
    int ret = MFI_STAT_OK;

    if (s->reply_queue_pa) {
        trace_megasas_initq_mapped(s->reply_queue_pa);
        goto out;
    }
    pa_lo = le32_to_cpu(cmd->frame->init.qinfo_new_addr_lo);
    pa_hi = le32_to_cpu(cmd->frame->init.qinfo_new_addr_hi);
    iq_pa = (((uint64_t) pa_hi << 32) | pa_lo);
    trace_megasas_init_firmware((uint64_t)iq_pa);
    initq = pci_dma_map(pcid, iq_pa, &initq_size, 0);
    if (!initq || initq_size != sizeof(*initq)) {
        trace_megasas_initq_map_failed(cmd->index);
        s->event_count++;
        ret = MFI_STAT_MEMORY_NOT_AVAILABLE;
        goto out;
    }
    s->reply_queue_len = le32_to_cpu(initq->rq_entries) & 0xFFFF;
    if (s->reply_queue_len > s->fw_cmds) {
        trace_megasas_initq_mismatch(s->reply_queue_len, s->fw_cmds);
        s->event_count++;
        ret = MFI_STAT_INVALID_PARAMETER;
        goto out;
    }
    pa_lo = le32_to_cpu(initq->rq_addr_lo);
    pa_hi = le32_to_cpu(initq->rq_addr_hi);
    s->reply_queue_pa = ((uint64_t) pa_hi << 32) | pa_lo;
    pa_lo = le32_to_cpu(initq->ci_addr_lo);
    pa_hi = le32_to_cpu(initq->ci_addr_hi);
    s->consumer_pa = ((uint64_t) pa_hi << 32) | pa_lo;
    pa_lo = le32_to_cpu(initq->pi_addr_lo);
    pa_hi = le32_to_cpu(initq->pi_addr_hi);
    s->producer_pa = ((uint64_t) pa_hi << 32) | pa_lo;
    s->reply_queue_head = ldl_le_pci_dma(pcid, s->producer_pa);
    s->reply_queue_head %= MEGASAS_MAX_FRAMES;
    s->reply_queue_tail = ldl_le_pci_dma(pcid, s->consumer_pa);
    s->reply_queue_tail %= MEGASAS_MAX_FRAMES;
    flags = le32_to_cpu(initq->flags);
    if (flags & MFI_QUEUE_FLAG_CONTEXT64) {
        s->flags |= MEGASAS_MASK_USE_QUEUE64;
    }
    trace_megasas_init_queue((unsigned long)s->reply_queue_pa,
                             s->reply_queue_len, s->reply_queue_head,
                             s->reply_queue_tail, flags);
    megasas_reset_frames(s);
    s->fw_state = MFI_FWSTATE_OPERATIONAL;
out:
    if (initq) {
        pci_dma_unmap(pcid, initq, initq_size, 0, 0);
    }
    return ret;
}

static int megasas_map_dcmd(MegasasState *s, MegasasCmd *cmd)
{
    dma_addr_t iov_pa, iov_size;
    int iov_count;

    cmd->flags = le16_to_cpu(cmd->frame->header.flags);
    iov_count = cmd->frame->header.sge_count;
    if (!iov_count) {
        trace_megasas_dcmd_zero_sge(cmd->index);
        cmd->iov_size = 0;
        return 0;
    } else if (iov_count > 1) {
        trace_megasas_dcmd_invalid_sge(cmd->index, iov_count);
        cmd->iov_size = 0;
        return -EINVAL;
    }
    iov_pa = megasas_sgl_get_addr(cmd, &cmd->frame->dcmd.sgl);
    iov_size = megasas_sgl_get_len(cmd, &cmd->frame->dcmd.sgl);
    pci_dma_sglist_init(&cmd->qsg, PCI_DEVICE(s), 1);
    qemu_sglist_add(&cmd->qsg, iov_pa, iov_size);
    cmd->iov_size = iov_size;
    return 0;
}

static void megasas_finish_dcmd(MegasasCmd *cmd, uint32_t iov_size)
{
    trace_megasas_finish_dcmd(cmd->index, iov_size);

    if (iov_size > cmd->iov_size) {
        if (megasas_frame_is_ieee_sgl(cmd)) {
            cmd->frame->dcmd.sgl.sg_skinny->len = cpu_to_le32(iov_size);
        } else if (megasas_frame_is_sgl64(cmd)) {
            cmd->frame->dcmd.sgl.sg64->len = cpu_to_le32(iov_size);
        } else {
            cmd->frame->dcmd.sgl.sg32->len = cpu_to_le32(iov_size);
        }
    }
}

static int megasas_ctrl_get_info(MegasasState *s, MegasasCmd *cmd)
{
    PCIDevice *pci_dev = PCI_DEVICE(s);
    PCIDeviceClass *pci_class = PCI_DEVICE_GET_CLASS(pci_dev);
    MegasasBaseClass *base_class = MEGASAS_GET_CLASS(s);
    struct mfi_ctrl_info info;
    size_t dcmd_size = sizeof(info);
    BusChild *kid;
    int num_pd_disks = 0;

    memset(&info, 0x0, dcmd_size);
    if (cmd->iov_size < dcmd_size) {
        trace_megasas_dcmd_invalid_xfer_len(cmd->index, cmd->iov_size,
                                            dcmd_size);
        return MFI_STAT_INVALID_PARAMETER;
    }

    info.pci.vendor = cpu_to_le16(pci_class->vendor_id);
    info.pci.device = cpu_to_le16(pci_class->device_id);
    info.pci.subvendor = cpu_to_le16(pci_class->subsystem_vendor_id);
    info.pci.subdevice = cpu_to_le16(pci_class->subsystem_id);

    /*
     * For some reason the firmware supports
     * only up to 8 device ports.
     * Despite supporting a far larger number
     * of devices for the physical devices.
     * So just display the first 8 devices
     * in the device port list, independent
     * of how many logical devices are actually
     * present.
     */
    info.host.type = MFI_INFO_HOST_PCIE;
    info.device.type = MFI_INFO_DEV_SAS3G;
    info.device.port_count = 8;
    QTAILQ_FOREACH(kid, &s->bus.qbus.children, sibling) {
        SCSIDevice *sdev = SCSI_DEVICE(kid->child);
        uint16_t pd_id;

        if (num_pd_disks < 8) {
            pd_id = ((sdev->id & 0xFF) << 8) | (sdev->lun & 0xFF);
            info.device.port_addr[num_pd_disks] =
                cpu_to_le64(megasas_get_sata_addr(pd_id));
        }
        num_pd_disks++;
    }

    memcpy(info.product_name, base_class->product_name, 24);
    snprintf(info.serial_number, 32, "%s", s->hba_serial);
    snprintf(info.package_version, 0x60, "%s-QEMU", qemu_hw_version());
    memcpy(info.image_component[0].name, "APP", 3);
    snprintf(info.image_component[0].version, 10, "%s-QEMU",
             base_class->product_version);
    memcpy(info.image_component[0].build_date, "Apr  1 2014", 11);
    memcpy(info.image_component[0].build_time, "12:34:56", 8);
    info.image_component_count = 1;
    if (pci_dev->has_rom) {
        uint8_t biosver[32];
        uint8_t *ptr;

        ptr = memory_region_get_ram_ptr(&pci_dev->rom);
        memcpy(biosver, ptr + 0x41, 31);
        biosver[31] = 0;
        memcpy(info.image_component[1].name, "BIOS", 4);
        memcpy(info.image_component[1].version, biosver,
               strlen((const char *)biosver));
        info.image_component_count++;
    }
    info.current_fw_time = cpu_to_le32(megasas_fw_time());
    info.max_arms = 32;
    info.max_spans = 8;
    info.max_arrays = MEGASAS_MAX_ARRAYS;
    info.max_lds = MFI_MAX_LD;
    info.max_cmds = cpu_to_le16(s->fw_cmds);
    info.max_sg_elements = cpu_to_le16(s->fw_sge);
    info.max_request_size = cpu_to_le32(MEGASAS_MAX_SECTORS);
    if (!megasas_is_jbod(s))
        info.lds_present = cpu_to_le16(num_pd_disks);
    info.pd_present = cpu_to_le16(num_pd_disks);
    info.pd_disks_present = cpu_to_le16(num_pd_disks);
    info.hw_present = cpu_to_le32(MFI_INFO_HW_NVRAM |
                                   MFI_INFO_HW_MEM |
                                   MFI_INFO_HW_FLASH);
    info.memory_size = cpu_to_le16(512);
    info.nvram_size = cpu_to_le16(32);
    info.flash_size = cpu_to_le16(16);
    info.raid_levels = cpu_to_le32(MFI_INFO_RAID_0);
    info.adapter_ops = cpu_to_le32(MFI_INFO_AOPS_RBLD_RATE |
                                    MFI_INFO_AOPS_SELF_DIAGNOSTIC |
                                    MFI_INFO_AOPS_MIXED_ARRAY);
    info.ld_ops = cpu_to_le32(MFI_INFO_LDOPS_DISK_CACHE_POLICY |
                               MFI_INFO_LDOPS_ACCESS_POLICY |
                               MFI_INFO_LDOPS_IO_POLICY |
                               MFI_INFO_LDOPS_WRITE_POLICY |
                               MFI_INFO_LDOPS_READ_POLICY);
    info.max_strips_per_io = cpu_to_le16(s->fw_sge);
    info.stripe_sz_ops.min = 3;
    info.stripe_sz_ops.max = ctz32(MEGASAS_MAX_SECTORS + 1);
    info.properties.pred_fail_poll_interval = cpu_to_le16(300);
    info.properties.intr_throttle_cnt = cpu_to_le16(16);
    info.properties.intr_throttle_timeout = cpu_to_le16(50);
    info.properties.rebuild_rate = 30;
    info.properties.patrol_read_rate = 30;
    info.properties.bgi_rate = 30;
    info.properties.cc_rate = 30;
    info.properties.recon_rate = 30;
    info.properties.cache_flush_interval = 4;
    info.properties.spinup_drv_cnt = 2;
    info.properties.spinup_delay = 6;
    info.properties.ecc_bucket_size = 15;
    info.properties.ecc_bucket_leak_rate = cpu_to_le16(1440);
    info.properties.expose_encl_devices = 1;
    info.properties.OnOffProperties = cpu_to_le32(MFI_CTRL_PROP_EnableJBOD);
    info.pd_ops = cpu_to_le32(MFI_INFO_PDOPS_FORCE_ONLINE |
                               MFI_INFO_PDOPS_FORCE_OFFLINE);
    info.pd_mix_support = cpu_to_le32(MFI_INFO_PDMIX_SAS |
                                       MFI_INFO_PDMIX_SATA |
                                       MFI_INFO_PDMIX_LD);

    cmd->iov_size -= dma_buf_read((uint8_t *)&info, dcmd_size, &cmd->qsg);
    return MFI_STAT_OK;
}

static int megasas_mfc_get_defaults(MegasasState *s, MegasasCmd *cmd)
{
    struct mfi_defaults info;
    size_t dcmd_size = sizeof(struct mfi_defaults);

    memset(&info, 0x0, dcmd_size);
    if (cmd->iov_size < dcmd_size) {
        trace_megasas_dcmd_invalid_xfer_len(cmd->index, cmd->iov_size,
                                            dcmd_size);
        return MFI_STAT_INVALID_PARAMETER;
    }

    info.sas_addr = cpu_to_le64(s->sas_addr);
    info.stripe_size = 3;
    info.flush_time = 4;
    info.background_rate = 30;
    info.allow_mix_in_enclosure = 1;
    info.allow_mix_in_ld = 1;
    info.direct_pd_mapping = 1;
    /* Enable for BIOS support */
    info.bios_enumerate_lds = 1;
    info.disable_ctrl_r = 1;
    info.expose_enclosure_devices = 1;
    info.disable_preboot_cli = 1;
    info.cluster_disable = 1;

    cmd->iov_size -= dma_buf_read((uint8_t *)&info, dcmd_size, &cmd->qsg);
    return MFI_STAT_OK;
}

static int megasas_dcmd_get_bios_info(MegasasState *s, MegasasCmd *cmd)
{
    struct mfi_bios_data info;
    size_t dcmd_size = sizeof(info);

    memset(&info, 0x0, dcmd_size);
    if (cmd->iov_size < dcmd_size) {
        trace_megasas_dcmd_invalid_xfer_len(cmd->index, cmd->iov_size,
                                            dcmd_size);
        return MFI_STAT_INVALID_PARAMETER;
    }
    info.continue_on_error = 1;
    info.verbose = 1;
    if (megasas_is_jbod(s)) {
        info.expose_all_drives = 1;
    }

    cmd->iov_size -= dma_buf_read((uint8_t *)&info, dcmd_size, &cmd->qsg);
    return MFI_STAT_OK;
}

static int megasas_dcmd_get_fw_time(MegasasState *s, MegasasCmd *cmd)
{
    uint64_t fw_time;
    size_t dcmd_size = sizeof(fw_time);

    fw_time = cpu_to_le64(megasas_fw_time());

    cmd->iov_size -= dma_buf_read((uint8_t *)&fw_time, dcmd_size, &cmd->qsg);
    return MFI_STAT_OK;
}

static int megasas_dcmd_set_fw_time(MegasasState *s, MegasasCmd *cmd)
{
    uint64_t fw_time;

    /* This is a dummy; setting of firmware time is not allowed */
    memcpy(&fw_time, cmd->frame->dcmd.mbox, sizeof(fw_time));

    trace_megasas_dcmd_set_fw_time(cmd->index, fw_time);
    fw_time = cpu_to_le64(megasas_fw_time());
    return MFI_STAT_OK;
}

static int megasas_event_info(MegasasState *s, MegasasCmd *cmd)
{
    struct mfi_evt_log_state info;
    size_t dcmd_size = sizeof(info);

    memset(&info, 0, dcmd_size);

    info.newest_seq_num = cpu_to_le32(s->event_count);
    info.shutdown_seq_num = cpu_to_le32(s->shutdown_event);
    info.boot_seq_num = cpu_to_le32(s->boot_event);

    cmd->iov_size -= dma_buf_read((uint8_t *)&info, dcmd_size, &cmd->qsg);
    return MFI_STAT_OK;
}

static int megasas_event_wait(MegasasState *s, MegasasCmd *cmd)
{
    union mfi_evt event;

    if (cmd->iov_size < sizeof(struct mfi_evt_detail)) {
        trace_megasas_dcmd_invalid_xfer_len(cmd->index, cmd->iov_size,
                                            sizeof(struct mfi_evt_detail));
        return MFI_STAT_INVALID_PARAMETER;
    }
    s->event_count = cpu_to_le32(cmd->frame->dcmd.mbox[0]);
    event.word = cpu_to_le32(cmd->frame->dcmd.mbox[4]);
    s->event_locale = event.members.locale;
    s->event_class = event.members.class;
    s->event_cmd = cmd;
    /* Decrease busy count; event frame doesn't count here */
    s->busy--;
    cmd->iov_size = sizeof(struct mfi_evt_detail);
    return MFI_STAT_INVALID_STATUS;
}

static int megasas_dcmd_pd_get_list(MegasasState *s, MegasasCmd *cmd)
{
    struct mfi_pd_list info;
    size_t dcmd_size = sizeof(info);
    BusChild *kid;
    uint32_t offset, dcmd_limit, num_pd_disks = 0, max_pd_disks;

    memset(&info, 0, dcmd_size);
    offset = 8;
    dcmd_limit = offset + sizeof(struct mfi_pd_address);
    if (cmd->iov_size < dcmd_limit) {
        trace_megasas_dcmd_invalid_xfer_len(cmd->index, cmd->iov_size,
                                            dcmd_limit);
        return MFI_STAT_INVALID_PARAMETER;
    }

    max_pd_disks = (cmd->iov_size - offset) / sizeof(struct mfi_pd_address);
    if (max_pd_disks > MFI_MAX_SYS_PDS) {
        max_pd_disks = MFI_MAX_SYS_PDS;
    }
    QTAILQ_FOREACH(kid, &s->bus.qbus.children, sibling) {
        SCSIDevice *sdev = SCSI_DEVICE(kid->child);
        uint16_t pd_id;

        if (num_pd_disks >= max_pd_disks)
            break;

        pd_id = ((sdev->id & 0xFF) << 8) | (sdev->lun & 0xFF);
        info.addr[num_pd_disks].device_id = cpu_to_le16(pd_id);
        info.addr[num_pd_disks].encl_device_id = 0xFFFF;
        info.addr[num_pd_disks].encl_index = 0;
        info.addr[num_pd_disks].slot_number = sdev->id & 0xFF;
        info.addr[num_pd_disks].scsi_dev_type = sdev->type;
        info.addr[num_pd_disks].connect_port_bitmap = 0x1;
        info.addr[num_pd_disks].sas_addr[0] =
            cpu_to_le64(megasas_get_sata_addr(pd_id));
        num_pd_disks++;
        offset += sizeof(struct mfi_pd_address);
    }
    trace_megasas_dcmd_pd_get_list(cmd->index, num_pd_disks,
                                   max_pd_disks, offset);

    info.size = cpu_to_le32(offset);
    info.count = cpu_to_le32(num_pd_disks);

    cmd->iov_size -= dma_buf_read((uint8_t *)&info, offset, &cmd->qsg);
    return MFI_STAT_OK;
}

static int megasas_dcmd_pd_list_query(MegasasState *s, MegasasCmd *cmd)
{
    uint16_t flags;

    /* mbox0 contains flags */
    flags = le16_to_cpu(cmd->frame->dcmd.mbox[0]);
    trace_megasas_dcmd_pd_list_query(cmd->index, flags);
    if (flags == MR_PD_QUERY_TYPE_ALL ||
        megasas_is_jbod(s)) {
        return megasas_dcmd_pd_get_list(s, cmd);
    }

    return MFI_STAT_OK;
}

static int megasas_pd_get_info_submit(SCSIDevice *sdev, int lun,
                                      MegasasCmd *cmd)
{
    struct mfi_pd_info *info = cmd->iov_buf;
    size_t dcmd_size = sizeof(struct mfi_pd_info);
    uint64_t pd_size;
    uint16_t pd_id = ((sdev->id & 0xFF) << 8) | (lun & 0xFF);
    uint8_t cmdbuf[6];
    size_t len, resid;

    if (!cmd->iov_buf) {
        cmd->iov_buf = g_malloc0(dcmd_size);
        info = cmd->iov_buf;
        info->inquiry_data[0] = 0x7f; /* Force PQual 0x3, PType 0x1f */
        info->vpd_page83[0] = 0x7f;
        megasas_setup_inquiry(cmdbuf, 0, sizeof(info->inquiry_data));
        cmd->req = scsi_req_new(sdev, cmd->index, lun, cmdbuf, cmd);
        if (!cmd->req) {
            trace_megasas_dcmd_req_alloc_failed(cmd->index,
                                                "PD get info std inquiry");
            g_free(cmd->iov_buf);
            cmd->iov_buf = NULL;
            return MFI_STAT_FLASH_ALLOC_FAIL;
        }
        trace_megasas_dcmd_internal_submit(cmd->index,
                                           "PD get info std inquiry", lun);
        len = scsi_req_enqueue(cmd->req);
        if (len > 0) {
            cmd->iov_size = len;
            scsi_req_continue(cmd->req);
        }
        return MFI_STAT_INVALID_STATUS;
    } else if (info->inquiry_data[0] != 0x7f && info->vpd_page83[0] == 0x7f) {
        megasas_setup_inquiry(cmdbuf, 0x83, sizeof(info->vpd_page83));
        cmd->req = scsi_req_new(sdev, cmd->index, lun, cmdbuf, cmd);
        if (!cmd->req) {
            trace_megasas_dcmd_req_alloc_failed(cmd->index,
                                                "PD get info vpd inquiry");
            return MFI_STAT_FLASH_ALLOC_FAIL;
        }
        trace_megasas_dcmd_internal_submit(cmd->index,
                                           "PD get info vpd inquiry", lun);
        len = scsi_req_enqueue(cmd->req);
        if (len > 0) {
            cmd->iov_size = len;
            scsi_req_continue(cmd->req);
        }
        return MFI_STAT_INVALID_STATUS;
    }
    /* Finished, set FW state */
    if ((info->inquiry_data[0] >> 5) == 0) {
        if (megasas_is_jbod(cmd->state)) {
            info->fw_state = cpu_to_le16(MFI_PD_STATE_SYSTEM);
        } else {
            info->fw_state = cpu_to_le16(MFI_PD_STATE_ONLINE);
        }
    } else {
        info->fw_state = cpu_to_le16(MFI_PD_STATE_OFFLINE);
    }

    info->ref.v.device_id = cpu_to_le16(pd_id);
    info->state.ddf.pd_type = cpu_to_le16(MFI_PD_DDF_TYPE_IN_VD|
                                          MFI_PD_DDF_TYPE_INTF_SAS);
    blk_get_geometry(sdev->conf.blk, &pd_size);
    info->raw_size = cpu_to_le64(pd_size);
    info->non_coerced_size = cpu_to_le64(pd_size);
    info->coerced_size = cpu_to_le64(pd_size);
    info->encl_device_id = 0xFFFF;
    info->slot_number = (sdev->id & 0xFF);
    info->path_info.count = 1;
    info->path_info.sas_addr[0] =
        cpu_to_le64(megasas_get_sata_addr(pd_id));
    info->connected_port_bitmap = 0x1;
    info->device_speed = 1;
    info->link_speed = 1;
    resid = dma_buf_read(cmd->iov_buf, dcmd_size, &cmd->qsg);
    g_free(cmd->iov_buf);
    cmd->iov_size = dcmd_size - resid;
    cmd->iov_buf = NULL;
    return MFI_STAT_OK;
}

static int megasas_dcmd_pd_get_info(MegasasState *s, MegasasCmd *cmd)
{
    size_t dcmd_size = sizeof(struct mfi_pd_info);
    uint16_t pd_id;
    uint8_t target_id, lun_id;
    SCSIDevice *sdev = NULL;
    int retval = MFI_STAT_DEVICE_NOT_FOUND;

    if (cmd->iov_size < dcmd_size) {
        return MFI_STAT_INVALID_PARAMETER;
    }

    /* mbox0 has the ID */
    pd_id = le16_to_cpu(cmd->frame->dcmd.mbox[0]);
    target_id = (pd_id >> 8) & 0xFF;
    lun_id = pd_id & 0xFF;
    sdev = scsi_device_find(&s->bus, 0, target_id, lun_id);
    trace_megasas_dcmd_pd_get_info(cmd->index, pd_id);

    if (sdev) {
        /* Submit inquiry */
        retval = megasas_pd_get_info_submit(sdev, pd_id, cmd);
    }

    return retval;
}

static int megasas_dcmd_ld_get_list(MegasasState *s, MegasasCmd *cmd)
{
    struct mfi_ld_list info;
    size_t dcmd_size = sizeof(info), resid;
    uint32_t num_ld_disks = 0, max_ld_disks;
    uint64_t ld_size;
    BusChild *kid;

    memset(&info, 0, dcmd_size);
    if (cmd->iov_size > dcmd_size) {
        trace_megasas_dcmd_invalid_xfer_len(cmd->index, cmd->iov_size,
                                            dcmd_size);
        return MFI_STAT_INVALID_PARAMETER;
    }

    max_ld_disks = (cmd->iov_size - 8) / 16;
    if (megasas_is_jbod(s)) {
        max_ld_disks = 0;
    }
    if (max_ld_disks > MFI_MAX_LD) {
        max_ld_disks = MFI_MAX_LD;
    }
    QTAILQ_FOREACH(kid, &s->bus.qbus.children, sibling) {
        SCSIDevice *sdev = SCSI_DEVICE(kid->child);

        if (num_ld_disks >= max_ld_disks) {
            break;
        }
        /* Logical device size is in blocks */
        blk_get_geometry(sdev->conf.blk, &ld_size);
        info.ld_list[num_ld_disks].ld.v.target_id = sdev->id;
        info.ld_list[num_ld_disks].state = MFI_LD_STATE_OPTIMAL;
        info.ld_list[num_ld_disks].size = cpu_to_le64(ld_size);
        num_ld_disks++;
    }
    info.ld_count = cpu_to_le32(num_ld_disks);
    trace_megasas_dcmd_ld_get_list(cmd->index, num_ld_disks, max_ld_disks);

    resid = dma_buf_read((uint8_t *)&info, dcmd_size, &cmd->qsg);
    cmd->iov_size = dcmd_size - resid;
    return MFI_STAT_OK;
}

static int megasas_dcmd_ld_list_query(MegasasState *s, MegasasCmd *cmd)
{
    uint16_t flags;
    struct mfi_ld_targetid_list info;
    size_t dcmd_size = sizeof(info), resid;
    uint32_t num_ld_disks = 0, max_ld_disks = s->fw_luns;
    BusChild *kid;

    /* mbox0 contains flags */
    flags = le16_to_cpu(cmd->frame->dcmd.mbox[0]);
    trace_megasas_dcmd_ld_list_query(cmd->index, flags);
    if (flags != MR_LD_QUERY_TYPE_ALL &&
        flags != MR_LD_QUERY_TYPE_EXPOSED_TO_HOST) {
        max_ld_disks = 0;
    }

    memset(&info, 0, dcmd_size);
    if (cmd->iov_size < 12) {
        trace_megasas_dcmd_invalid_xfer_len(cmd->index, cmd->iov_size,
                                            dcmd_size);
        return MFI_STAT_INVALID_PARAMETER;
    }
    dcmd_size = sizeof(uint32_t) * 2 + 3;
    max_ld_disks = cmd->iov_size - dcmd_size;
    if (megasas_is_jbod(s)) {
        max_ld_disks = 0;
    }
    if (max_ld_disks > MFI_MAX_LD) {
        max_ld_disks = MFI_MAX_LD;
    }
    QTAILQ_FOREACH(kid, &s->bus.qbus.children, sibling) {
        SCSIDevice *sdev = SCSI_DEVICE(kid->child);

        if (num_ld_disks >= max_ld_disks) {
            break;
        }
        info.targetid[num_ld_disks] = sdev->lun;
        num_ld_disks++;
        dcmd_size++;
    }
    info.ld_count = cpu_to_le32(num_ld_disks);
    info.size = dcmd_size;
    trace_megasas_dcmd_ld_get_list(cmd->index, num_ld_disks, max_ld_disks);

    resid = dma_buf_read((uint8_t *)&info, dcmd_size, &cmd->qsg);
    cmd->iov_size = dcmd_size - resid;
    return MFI_STAT_OK;
}

static int megasas_ld_get_info_submit(SCSIDevice *sdev, int lun,
                                      MegasasCmd *cmd)
{
    struct mfi_ld_info *info = cmd->iov_buf;
    size_t dcmd_size = sizeof(struct mfi_ld_info);
    uint8_t cdb[6];
    ssize_t len, resid;
    uint16_t sdev_id = ((sdev->id & 0xFF) << 8) | (lun & 0xFF);
    uint64_t ld_size;

    if (!cmd->iov_buf) {
        cmd->iov_buf = g_malloc0(dcmd_size);
        info = cmd->iov_buf;
        megasas_setup_inquiry(cdb, 0x83, sizeof(info->vpd_page83));
        cmd->req = scsi_req_new(sdev, cmd->index, lun, cdb, cmd);
        if (!cmd->req) {
            trace_megasas_dcmd_req_alloc_failed(cmd->index,
                                                "LD get info vpd inquiry");
            g_free(cmd->iov_buf);
            cmd->iov_buf = NULL;
            return MFI_STAT_FLASH_ALLOC_FAIL;
        }
        trace_megasas_dcmd_internal_submit(cmd->index,
                                           "LD get info vpd inquiry", lun);
        len = scsi_req_enqueue(cmd->req);
        if (len > 0) {
            cmd->iov_size = len;
            scsi_req_continue(cmd->req);
        }
        return MFI_STAT_INVALID_STATUS;
    }

    info->ld_config.params.state = MFI_LD_STATE_OPTIMAL;
    info->ld_config.properties.ld.v.target_id = lun;
    info->ld_config.params.stripe_size = 3;
    info->ld_config.params.num_drives = 1;
    info->ld_config.params.is_consistent = 1;
    /* Logical device size is in blocks */
    blk_get_geometry(sdev->conf.blk, &ld_size);
    info->size = cpu_to_le64(ld_size);
    memset(info->ld_config.span, 0, sizeof(info->ld_config.span));
    info->ld_config.span[0].start_block = 0;
    info->ld_config.span[0].num_blocks = info->size;
    info->ld_config.span[0].array_ref = cpu_to_le16(sdev_id);

    resid = dma_buf_read(cmd->iov_buf, dcmd_size, &cmd->qsg);
    g_free(cmd->iov_buf);
    cmd->iov_size = dcmd_size - resid;
    cmd->iov_buf = NULL;
    return MFI_STAT_OK;
}

static int megasas_dcmd_ld_get_info(MegasasState *s, MegasasCmd *cmd)
{
    struct mfi_ld_info info;
    size_t dcmd_size = sizeof(info);
    uint16_t ld_id;
    uint32_t max_ld_disks = s->fw_luns;
    SCSIDevice *sdev = NULL;
    int retval = MFI_STAT_DEVICE_NOT_FOUND;

    if (cmd->iov_size < dcmd_size) {
        return MFI_STAT_INVALID_PARAMETER;
    }

    /* mbox0 has the ID */
    ld_id = le16_to_cpu(cmd->frame->dcmd.mbox[0]);
    trace_megasas_dcmd_ld_get_info(cmd->index, ld_id);

    if (megasas_is_jbod(s)) {
        return MFI_STAT_DEVICE_NOT_FOUND;
    }

    if (ld_id < max_ld_disks) {
        sdev = scsi_device_find(&s->bus, 0, ld_id, 0);
    }

    if (sdev) {
        retval = megasas_ld_get_info_submit(sdev, ld_id, cmd);
    }

    return retval;
}

static int megasas_dcmd_cfg_read(MegasasState *s, MegasasCmd *cmd)
{
    uint8_t data[4096] = { 0 };
    struct mfi_config_data *info;
    int num_pd_disks = 0, array_offset, ld_offset;
    BusChild *kid;

    if (cmd->iov_size > 4096) {
        return MFI_STAT_INVALID_PARAMETER;
    }

    QTAILQ_FOREACH(kid, &s->bus.qbus.children, sibling) {
        num_pd_disks++;
    }
    info = (struct mfi_config_data *)&data;
    /*
     * Array mapping:
     * - One array per SCSI device
     * - One logical drive per SCSI device
     *   spanning the entire device
     */
    info->array_count = num_pd_disks;
    info->array_size = sizeof(struct mfi_array) * num_pd_disks;
    info->log_drv_count = num_pd_disks;
    info->log_drv_size = sizeof(struct mfi_ld_config) * num_pd_disks;
    info->spares_count = 0;
    info->spares_size = sizeof(struct mfi_spare);
    info->size = sizeof(struct mfi_config_data) + info->array_size +
        info->log_drv_size;
    if (info->size > 4096) {
        return MFI_STAT_INVALID_PARAMETER;
    }

    array_offset = sizeof(struct mfi_config_data);
    ld_offset = array_offset + sizeof(struct mfi_array) * num_pd_disks;

    QTAILQ_FOREACH(kid, &s->bus.qbus.children, sibling) {
        SCSIDevice *sdev = SCSI_DEVICE(kid->child);
        uint16_t sdev_id = ((sdev->id & 0xFF) << 8) | (sdev->lun & 0xFF);
        struct mfi_array *array;
        struct mfi_ld_config *ld;
        uint64_t pd_size;
        int i;

        array = (struct mfi_array *)(data + array_offset);
        blk_get_geometry(sdev->conf.blk, &pd_size);
        array->size = cpu_to_le64(pd_size);
        array->num_drives = 1;
        array->array_ref = cpu_to_le16(sdev_id);
        array->pd[0].ref.v.device_id = cpu_to_le16(sdev_id);
        array->pd[0].ref.v.seq_num = 0;
        array->pd[0].fw_state = MFI_PD_STATE_ONLINE;
        array->pd[0].encl.pd = 0xFF;
        array->pd[0].encl.slot = (sdev->id & 0xFF);
        for (i = 1; i < MFI_MAX_ROW_SIZE; i++) {
            array->pd[i].ref.v.device_id = 0xFFFF;
            array->pd[i].ref.v.seq_num = 0;
            array->pd[i].fw_state = MFI_PD_STATE_UNCONFIGURED_GOOD;
            array->pd[i].encl.pd = 0xFF;
            array->pd[i].encl.slot = 0xFF;
        }
        array_offset += sizeof(struct mfi_array);
        ld = (struct mfi_ld_config *)(data + ld_offset);
        memset(ld, 0, sizeof(struct mfi_ld_config));
        ld->properties.ld.v.target_id = sdev->id;
        ld->properties.default_cache_policy = MR_LD_CACHE_READ_AHEAD |
            MR_LD_CACHE_READ_ADAPTIVE;
        ld->properties.current_cache_policy = MR_LD_CACHE_READ_AHEAD |
            MR_LD_CACHE_READ_ADAPTIVE;
        ld->params.state = MFI_LD_STATE_OPTIMAL;
        ld->params.stripe_size = 3;
        ld->params.num_drives = 1;
        ld->params.span_depth = 1;
        ld->params.is_consistent = 1;
        ld->span[0].start_block = 0;
        ld->span[0].num_blocks = cpu_to_le64(pd_size);
        ld->span[0].array_ref = cpu_to_le16(sdev_id);
        ld_offset += sizeof(struct mfi_ld_config);
    }

    cmd->iov_size -= dma_buf_read((uint8_t *)data, info->size, &cmd->qsg);
    return MFI_STAT_OK;
}

static int megasas_dcmd_get_properties(MegasasState *s, MegasasCmd *cmd)
{
    struct mfi_ctrl_props info;
    size_t dcmd_size = sizeof(info);

    memset(&info, 0x0, dcmd_size);
    if (cmd->iov_size < dcmd_size) {
        trace_megasas_dcmd_invalid_xfer_len(cmd->index, cmd->iov_size,
                                            dcmd_size);
        return MFI_STAT_INVALID_PARAMETER;
    }
    info.pred_fail_poll_interval = cpu_to_le16(300);
    info.intr_throttle_cnt = cpu_to_le16(16);
    info.intr_throttle_timeout = cpu_to_le16(50);
    info.rebuild_rate = 30;
    info.patrol_read_rate = 30;
    info.bgi_rate = 30;
    info.cc_rate = 30;
    info.recon_rate = 30;
    info.cache_flush_interval = 4;
    info.spinup_drv_cnt = 2;
    info.spinup_delay = 6;
    info.ecc_bucket_size = 15;
    info.ecc_bucket_leak_rate = cpu_to_le16(1440);
    info.expose_encl_devices = 1;

    cmd->iov_size -= dma_buf_read((uint8_t *)&info, dcmd_size, &cmd->qsg);
    return MFI_STAT_OK;
}

static int megasas_cache_flush(MegasasState *s, MegasasCmd *cmd)
{
    blk_drain_all();
    return MFI_STAT_OK;
}

static int megasas_ctrl_shutdown(MegasasState *s, MegasasCmd *cmd)
{
    s->fw_state = MFI_FWSTATE_READY;
    return MFI_STAT_OK;
}

/* Some implementations use CLUSTER RESET LD to simulate a device reset */
static int megasas_cluster_reset_ld(MegasasState *s, MegasasCmd *cmd)
{
    uint16_t target_id;
    int i;

    /* mbox0 contains the device index */
    target_id = le16_to_cpu(cmd->frame->dcmd.mbox[0]);
    trace_megasas_dcmd_reset_ld(cmd->index, target_id);
    for (i = 0; i < s->fw_cmds; i++) {
        MegasasCmd *tmp_cmd = &s->frames[i];
        if (tmp_cmd->req && tmp_cmd->req->dev->id == target_id) {
            SCSIDevice *d = tmp_cmd->req->dev;
            qdev_reset_all(&d->qdev);
        }
    }
    return MFI_STAT_OK;
}

static int megasas_dcmd_set_properties(MegasasState *s, MegasasCmd *cmd)
{
    struct mfi_ctrl_props info;
    size_t dcmd_size = sizeof(info);

    if (cmd->iov_size < dcmd_size) {
        trace_megasas_dcmd_invalid_xfer_len(cmd->index, cmd->iov_size,
                                            dcmd_size);
        return MFI_STAT_INVALID_PARAMETER;
    }
    dma_buf_write((uint8_t *)&info, dcmd_size, &cmd->qsg);
    trace_megasas_dcmd_unsupported(cmd->index, cmd->iov_size);
    return MFI_STAT_OK;
}

static int megasas_dcmd_dummy(MegasasState *s, MegasasCmd *cmd)
{
    trace_megasas_dcmd_dummy(cmd->index, cmd->iov_size);
    return MFI_STAT_OK;
}

static const struct dcmd_cmd_tbl_t {
    int opcode;
    const char *desc;
    int (*func)(MegasasState *s, MegasasCmd *cmd);
} dcmd_cmd_tbl[] = {
    { MFI_DCMD_CTRL_MFI_HOST_MEM_ALLOC, "CTRL_HOST_MEM_ALLOC",
      megasas_dcmd_dummy },
    { MFI_DCMD_CTRL_GET_INFO, "CTRL_GET_INFO",
      megasas_ctrl_get_info },
    { MFI_DCMD_CTRL_GET_PROPERTIES, "CTRL_GET_PROPERTIES",
      megasas_dcmd_get_properties },
    { MFI_DCMD_CTRL_SET_PROPERTIES, "CTRL_SET_PROPERTIES",
      megasas_dcmd_set_properties },
    { MFI_DCMD_CTRL_ALARM_GET, "CTRL_ALARM_GET",
      megasas_dcmd_dummy },
    { MFI_DCMD_CTRL_ALARM_ENABLE, "CTRL_ALARM_ENABLE",
      megasas_dcmd_dummy },
    { MFI_DCMD_CTRL_ALARM_DISABLE, "CTRL_ALARM_DISABLE",
      megasas_dcmd_dummy },
    { MFI_DCMD_CTRL_ALARM_SILENCE, "CTRL_ALARM_SILENCE",
      megasas_dcmd_dummy },
    { MFI_DCMD_CTRL_ALARM_TEST, "CTRL_ALARM_TEST",
      megasas_dcmd_dummy },
    { MFI_DCMD_CTRL_EVENT_GETINFO, "CTRL_EVENT_GETINFO",
      megasas_event_info },
    { MFI_DCMD_CTRL_EVENT_GET, "CTRL_EVENT_GET",
      megasas_dcmd_dummy },
    { MFI_DCMD_CTRL_EVENT_WAIT, "CTRL_EVENT_WAIT",
      megasas_event_wait },
    { MFI_DCMD_CTRL_SHUTDOWN, "CTRL_SHUTDOWN",
      megasas_ctrl_shutdown },
    { MFI_DCMD_HIBERNATE_STANDBY, "CTRL_STANDBY",
      megasas_dcmd_dummy },
    { MFI_DCMD_CTRL_GET_TIME, "CTRL_GET_TIME",
      megasas_dcmd_get_fw_time },
    { MFI_DCMD_CTRL_SET_TIME, "CTRL_SET_TIME",
      megasas_dcmd_set_fw_time },
    { MFI_DCMD_CTRL_BIOS_DATA_GET, "CTRL_BIOS_DATA_GET",
      megasas_dcmd_get_bios_info },
    { MFI_DCMD_CTRL_FACTORY_DEFAULTS, "CTRL_FACTORY_DEFAULTS",
      megasas_dcmd_dummy },
    { MFI_DCMD_CTRL_MFC_DEFAULTS_GET, "CTRL_MFC_DEFAULTS_GET",
      megasas_mfc_get_defaults },
    { MFI_DCMD_CTRL_MFC_DEFAULTS_SET, "CTRL_MFC_DEFAULTS_SET",
      megasas_dcmd_dummy },
    { MFI_DCMD_CTRL_CACHE_FLUSH, "CTRL_CACHE_FLUSH",
      megasas_cache_flush },
    { MFI_DCMD_PD_GET_LIST, "PD_GET_LIST",
      megasas_dcmd_pd_get_list },
    { MFI_DCMD_PD_LIST_QUERY, "PD_LIST_QUERY",
      megasas_dcmd_pd_list_query },
    { MFI_DCMD_PD_GET_INFO, "PD_GET_INFO",
      megasas_dcmd_pd_get_info },
    { MFI_DCMD_PD_STATE_SET, "PD_STATE_SET",
      megasas_dcmd_dummy },
    { MFI_DCMD_PD_REBUILD, "PD_REBUILD",
      megasas_dcmd_dummy },
    { MFI_DCMD_PD_BLINK, "PD_BLINK",
      megasas_dcmd_dummy },
    { MFI_DCMD_PD_UNBLINK, "PD_UNBLINK",
      megasas_dcmd_dummy },
    { MFI_DCMD_LD_GET_LIST, "LD_GET_LIST",
      megasas_dcmd_ld_get_list},
    { MFI_DCMD_LD_LIST_QUERY, "LD_LIST_QUERY",
      megasas_dcmd_ld_list_query },
    { MFI_DCMD_LD_GET_INFO, "LD_GET_INFO",
      megasas_dcmd_ld_get_info },
    { MFI_DCMD_LD_GET_PROP, "LD_GET_PROP",
      megasas_dcmd_dummy },
    { MFI_DCMD_LD_SET_PROP, "LD_SET_PROP",
      megasas_dcmd_dummy },
    { MFI_DCMD_LD_DELETE, "LD_DELETE",
      megasas_dcmd_dummy },
    { MFI_DCMD_CFG_READ, "CFG_READ",
      megasas_dcmd_cfg_read },
    { MFI_DCMD_CFG_ADD, "CFG_ADD",
      megasas_dcmd_dummy },
    { MFI_DCMD_CFG_CLEAR, "CFG_CLEAR",
      megasas_dcmd_dummy },
    { MFI_DCMD_CFG_FOREIGN_READ, "CFG_FOREIGN_READ",
      megasas_dcmd_dummy },
    { MFI_DCMD_CFG_FOREIGN_IMPORT, "CFG_FOREIGN_IMPORT",
      megasas_dcmd_dummy },
    { MFI_DCMD_BBU_STATUS, "BBU_STATUS",
      megasas_dcmd_dummy },
    { MFI_DCMD_BBU_CAPACITY_INFO, "BBU_CAPACITY_INFO",
      megasas_dcmd_dummy },
    { MFI_DCMD_BBU_DESIGN_INFO, "BBU_DESIGN_INFO",
      megasas_dcmd_dummy },
    { MFI_DCMD_BBU_PROP_GET, "BBU_PROP_GET",
      megasas_dcmd_dummy },
    { MFI_DCMD_CLUSTER, "CLUSTER",
      megasas_dcmd_dummy },
    { MFI_DCMD_CLUSTER_RESET_ALL, "CLUSTER_RESET_ALL",
      megasas_dcmd_dummy },
    { MFI_DCMD_CLUSTER_RESET_LD, "CLUSTER_RESET_LD",
      megasas_cluster_reset_ld },
    { -1, NULL, NULL }
};

static int megasas_handle_dcmd(MegasasState *s, MegasasCmd *cmd)
{
    int retval = 0;
    size_t len;
    const struct dcmd_cmd_tbl_t *cmdptr = dcmd_cmd_tbl;

    cmd->dcmd_opcode = le32_to_cpu(cmd->frame->dcmd.opcode);
    trace_megasas_handle_dcmd(cmd->index, cmd->dcmd_opcode);
    if (megasas_map_dcmd(s, cmd) < 0) {
        return MFI_STAT_MEMORY_NOT_AVAILABLE;
    }
    while (cmdptr->opcode != -1 && cmdptr->opcode != cmd->dcmd_opcode) {
        cmdptr++;
    }
    len = cmd->iov_size;
    if (cmdptr->opcode == -1) {
        trace_megasas_dcmd_unhandled(cmd->index, cmd->dcmd_opcode, len);
        retval = megasas_dcmd_dummy(s, cmd);
    } else {
        trace_megasas_dcmd_enter(cmd->index, cmdptr->desc, len);
        retval = cmdptr->func(s, cmd);
    }
    if (retval != MFI_STAT_INVALID_STATUS) {
        megasas_finish_dcmd(cmd, len);
    }
    return retval;
}

static int megasas_finish_internal_dcmd(MegasasCmd *cmd,
                                        SCSIRequest *req, size_t resid)
{
    int retval = MFI_STAT_OK;
    int lun = req->lun;

    trace_megasas_dcmd_internal_finish(cmd->index, cmd->dcmd_opcode, lun);
    cmd->iov_size -= resid;
    switch (cmd->dcmd_opcode) {
    case MFI_DCMD_PD_GET_INFO:
        retval = megasas_pd_get_info_submit(req->dev, lun, cmd);
        break;
    case MFI_DCMD_LD_GET_INFO:
        retval = megasas_ld_get_info_submit(req->dev, lun, cmd);
        break;
    default:
        trace_megasas_dcmd_internal_invalid(cmd->index, cmd->dcmd_opcode);
        retval = MFI_STAT_INVALID_DCMD;
        break;
    }
    if (retval != MFI_STAT_INVALID_STATUS) {
        megasas_finish_dcmd(cmd, cmd->iov_size);
    }
    return retval;
}

static int megasas_enqueue_req(MegasasCmd *cmd, bool is_write)
{
    int len;

    len = scsi_req_enqueue(cmd->req);
    if (len < 0) {
        len = -len;
    }
    if (len > 0) {
        if (len > cmd->iov_size) {
            if (is_write) {
                trace_megasas_iov_write_overflow(cmd->index, len,
                                                 cmd->iov_size);
            } else {
                trace_megasas_iov_read_overflow(cmd->index, len,
                                                cmd->iov_size);
            }
        }
        if (len < cmd->iov_size) {
            if (is_write) {
                trace_megasas_iov_write_underflow(cmd->index, len,
                                                  cmd->iov_size);
            } else {
                trace_megasas_iov_read_underflow(cmd->index, len,
                                                 cmd->iov_size);
            }
            cmd->iov_size = len;
        }
        scsi_req_continue(cmd->req);
    }
    return len;
}

static int megasas_handle_scsi(MegasasState *s, MegasasCmd *cmd,
                               int frame_cmd)
{
    uint8_t *cdb;
    int target_id, lun_id, cdb_len;
    bool is_write;
    struct SCSIDevice *sdev = NULL;
    bool is_logical = (frame_cmd == MFI_CMD_LD_SCSI_IO);

    cdb = cmd->frame->pass.cdb;
    target_id = cmd->frame->header.target_id;
    lun_id = cmd->frame->header.lun_id;
    cdb_len = cmd->frame->header.cdb_len;

    if (is_logical) {
        if (target_id >= MFI_MAX_LD || lun_id != 0) {
            trace_megasas_scsi_target_not_present(
                mfi_frame_desc(frame_cmd), is_logical, target_id, lun_id);
            return MFI_STAT_DEVICE_NOT_FOUND;
        }
    }
    sdev = scsi_device_find(&s->bus, 0, target_id, lun_id);

    cmd->iov_size = le32_to_cpu(cmd->frame->header.data_len);
    trace_megasas_handle_scsi(mfi_frame_desc(frame_cmd), is_logical,
                              target_id, lun_id, sdev, cmd->iov_size);

    if (!sdev || (megasas_is_jbod(s) && is_logical)) {
        trace_megasas_scsi_target_not_present(
            mfi_frame_desc(frame_cmd), is_logical, target_id, lun_id);
        return MFI_STAT_DEVICE_NOT_FOUND;
    }

    if (cdb_len > 16) {
        trace_megasas_scsi_invalid_cdb_len(
                mfi_frame_desc(frame_cmd), is_logical,
                target_id, lun_id, cdb_len);
        megasas_write_sense(cmd, SENSE_CODE(INVALID_OPCODE));
        cmd->frame->header.scsi_status = CHECK_CONDITION;
        s->event_count++;
        return MFI_STAT_SCSI_DONE_WITH_ERROR;
    }

    if (megasas_map_sgl(s, cmd, &cmd->frame->pass.sgl)) {
        megasas_write_sense(cmd, SENSE_CODE(TARGET_FAILURE));
        cmd->frame->header.scsi_status = CHECK_CONDITION;
        s->event_count++;
        return MFI_STAT_SCSI_DONE_WITH_ERROR;
    }

    cmd->req = scsi_req_new(sdev, cmd->index, lun_id, cdb, cmd);
    if (!cmd->req) {
        trace_megasas_scsi_req_alloc_failed(
                mfi_frame_desc(frame_cmd), target_id, lun_id);
        megasas_write_sense(cmd, SENSE_CODE(NO_SENSE));
        cmd->frame->header.scsi_status = BUSY;
        s->event_count++;
        return MFI_STAT_SCSI_DONE_WITH_ERROR;
    }

    is_write = (cmd->req->cmd.mode == SCSI_XFER_TO_DEV);
    if (cmd->iov_size) {
        if (is_write) {
            trace_megasas_scsi_write_start(cmd->index, cmd->iov_size);
        } else {
            trace_megasas_scsi_read_start(cmd->index, cmd->iov_size);
        }
    } else {
        trace_megasas_scsi_nodata(cmd->index);
    }
    megasas_enqueue_req(cmd, is_write);
    return MFI_STAT_INVALID_STATUS;
}

static int megasas_handle_io(MegasasState *s, MegasasCmd *cmd, int frame_cmd)
{
    uint32_t lba_count, lba_start_hi, lba_start_lo;
    uint64_t lba_start;
    bool is_write = (frame_cmd == MFI_CMD_LD_WRITE);
    uint8_t cdb[16];
    int len;
    struct SCSIDevice *sdev = NULL;
    int target_id, lun_id, cdb_len;

    lba_count = le32_to_cpu(cmd->frame->io.header.data_len);
    lba_start_lo = le32_to_cpu(cmd->frame->io.lba_lo);
    lba_start_hi = le32_to_cpu(cmd->frame->io.lba_hi);
    lba_start = ((uint64_t)lba_start_hi << 32) | lba_start_lo;

    target_id = cmd->frame->header.target_id;
    lun_id = cmd->frame->header.lun_id;
    cdb_len = cmd->frame->header.cdb_len;

    if (target_id < MFI_MAX_LD && lun_id == 0) {
        sdev = scsi_device_find(&s->bus, 0, target_id, lun_id);
    }

    trace_megasas_handle_io(cmd->index,
                            mfi_frame_desc(frame_cmd), target_id, lun_id,
                            (unsigned long)lba_start, (unsigned long)lba_count);
    if (!sdev) {
        trace_megasas_io_target_not_present(cmd->index,
            mfi_frame_desc(frame_cmd), target_id, lun_id);
        return MFI_STAT_DEVICE_NOT_FOUND;
    }

    if (cdb_len > 16) {
        trace_megasas_scsi_invalid_cdb_len(
            mfi_frame_desc(frame_cmd), 1, target_id, lun_id, cdb_len);
        megasas_write_sense(cmd, SENSE_CODE(INVALID_OPCODE));
        cmd->frame->header.scsi_status = CHECK_CONDITION;
        s->event_count++;
        return MFI_STAT_SCSI_DONE_WITH_ERROR;
    }

    cmd->iov_size = lba_count * sdev->blocksize;
    if (megasas_map_sgl(s, cmd, &cmd->frame->io.sgl)) {
        megasas_write_sense(cmd, SENSE_CODE(TARGET_FAILURE));
        cmd->frame->header.scsi_status = CHECK_CONDITION;
        s->event_count++;
        return MFI_STAT_SCSI_DONE_WITH_ERROR;
    }

    megasas_encode_lba(cdb, lba_start, lba_count, is_write);
    cmd->req = scsi_req_new(sdev, cmd->index,
                            lun_id, cdb, cmd);
    if (!cmd->req) {
        trace_megasas_scsi_req_alloc_failed(
            mfi_frame_desc(frame_cmd), target_id, lun_id);
        megasas_write_sense(cmd, SENSE_CODE(NO_SENSE));
        cmd->frame->header.scsi_status = BUSY;
        s->event_count++;
        return MFI_STAT_SCSI_DONE_WITH_ERROR;
    }
    len = megasas_enqueue_req(cmd, is_write);
    if (len > 0) {
        if (is_write) {
            trace_megasas_io_write_start(cmd->index, lba_start, lba_count, len);
        } else {
            trace_megasas_io_read_start(cmd->index, lba_start, lba_count, len);
        }
    }
    return MFI_STAT_INVALID_STATUS;
}

static QEMUSGList *megasas_get_sg_list(SCSIRequest *req)
{
    MegasasCmd *cmd = req->hba_private;

    if (cmd->dcmd_opcode != -1) {
        return NULL;
    } else {
        return &cmd->qsg;
    }
}

static void megasas_xfer_complete(SCSIRequest *req, uint32_t len)
{
    MegasasCmd *cmd = req->hba_private;
    uint8_t *buf;

    trace_megasas_io_complete(cmd->index, len);

    if (cmd->dcmd_opcode != -1) {
        scsi_req_continue(req);
        return;
    }

    buf = scsi_req_get_buf(req);
    if (cmd->dcmd_opcode == MFI_DCMD_PD_GET_INFO && cmd->iov_buf) {
        struct mfi_pd_info *info = cmd->iov_buf;

        if (info->inquiry_data[0] == 0x7f) {
            memset(info->inquiry_data, 0, sizeof(info->inquiry_data));
            memcpy(info->inquiry_data, buf, len);
        } else if (info->vpd_page83[0] == 0x7f) {
            memset(info->vpd_page83, 0, sizeof(info->vpd_page83));
            memcpy(info->vpd_page83, buf, len);
        }
        scsi_req_continue(req);
    } else if (cmd->dcmd_opcode == MFI_DCMD_LD_GET_INFO) {
        struct mfi_ld_info *info = cmd->iov_buf;

        if (cmd->iov_buf) {
            memcpy(info->vpd_page83, buf, sizeof(info->vpd_page83));
            scsi_req_continue(req);
        }
    }
}

static void megasas_command_complete(SCSIRequest *req, size_t resid)
{
    MegasasCmd *cmd = req->hba_private;
    uint8_t cmd_status = MFI_STAT_OK;

    trace_megasas_command_complete(cmd->index, req->status, resid);

    if (req->io_canceled) {
        return;
    }

    if (cmd->dcmd_opcode != -1) {
        /*
         * Internal command complete
         */
        cmd_status = megasas_finish_internal_dcmd(cmd, req, resid);
        if (cmd_status == MFI_STAT_INVALID_STATUS) {
            return;
        }
    } else {
        trace_megasas_scsi_complete(cmd->index, req->status,
                                    cmd->iov_size, req->cmd.xfer);
        if (req->status != GOOD) {
            cmd_status = MFI_STAT_SCSI_DONE_WITH_ERROR;
        }
        if (req->status == CHECK_CONDITION) {
            megasas_copy_sense(cmd);
        }

        cmd->frame->header.scsi_status = req->status;
    }
    cmd->frame->header.cmd_status = cmd_status;
    megasas_complete_command(cmd);
}

static void megasas_command_cancelled(SCSIRequest *req)
{
    MegasasCmd *cmd = req->hba_private;

    if (!cmd) {
        return;
    }
    cmd->frame->header.cmd_status = MFI_STAT_SCSI_IO_FAILED;
    megasas_complete_command(cmd);
}

static int megasas_handle_abort(MegasasState *s, MegasasCmd *cmd)
{
    uint64_t abort_ctx = le64_to_cpu(cmd->frame->abort.abort_context);
    hwaddr abort_addr, addr_hi, addr_lo;
    MegasasCmd *abort_cmd;

    addr_hi = le32_to_cpu(cmd->frame->abort.abort_mfi_addr_hi);
    addr_lo = le32_to_cpu(cmd->frame->abort.abort_mfi_addr_lo);
    abort_addr = ((uint64_t)addr_hi << 32) | addr_lo;

    abort_cmd = megasas_lookup_frame(s, abort_addr);
    if (!abort_cmd) {
        trace_megasas_abort_no_cmd(cmd->index, abort_ctx);
        s->event_count++;
        return MFI_STAT_OK;
    }
    if (!megasas_use_queue64(s)) {
        abort_ctx &= (uint64_t)0xFFFFFFFF;
    }
    if (abort_cmd->context != abort_ctx) {
        trace_megasas_abort_invalid_context(cmd->index, abort_cmd->context,
                                            abort_cmd->index);
        s->event_count++;
        return MFI_STAT_ABORT_NOT_POSSIBLE;
    }
    trace_megasas_abort_frame(cmd->index, abort_cmd->index);
    megasas_abort_command(abort_cmd);
    if (!s->event_cmd || abort_cmd != s->event_cmd) {
        s->event_cmd = NULL;
    }
    s->event_count++;
    return MFI_STAT_OK;
}

static void megasas_handle_frame(MegasasState *s, uint64_t frame_addr,
                                 uint32_t frame_count)
{
    uint8_t frame_status = MFI_STAT_INVALID_CMD;
    uint64_t frame_context;
    int frame_cmd;
    MegasasCmd *cmd;

    /*
     * Always read 64bit context, top bits will be
     * masked out if required in megasas_enqueue_frame()
     */
    frame_context = megasas_frame_get_context(s, frame_addr);

    cmd = megasas_enqueue_frame(s, frame_addr, frame_context, frame_count);
    if (!cmd) {
        /* reply queue full */
        trace_megasas_frame_busy(frame_addr);
        megasas_frame_set_scsi_status(s, frame_addr, BUSY);
        megasas_frame_set_cmd_status(s, frame_addr, MFI_STAT_SCSI_DONE_WITH_ERROR);
        megasas_complete_frame(s, frame_context);
        s->event_count++;
        return;
    }
    frame_cmd = cmd->frame->header.frame_cmd;
    switch (frame_cmd) {
    case MFI_CMD_INIT:
        frame_status = megasas_init_firmware(s, cmd);
        break;
    case MFI_CMD_DCMD:
        frame_status = megasas_handle_dcmd(s, cmd);
        break;
    case MFI_CMD_ABORT:
        frame_status = megasas_handle_abort(s, cmd);
        break;
    case MFI_CMD_PD_SCSI_IO:
    case MFI_CMD_LD_SCSI_IO:
        frame_status = megasas_handle_scsi(s, cmd, frame_cmd);
        break;
    case MFI_CMD_LD_READ:
    case MFI_CMD_LD_WRITE:
        frame_status = megasas_handle_io(s, cmd, frame_cmd);
        break;
    default:
        trace_megasas_unhandled_frame_cmd(cmd->index, frame_cmd);
        s->event_count++;
        break;
    }
    if (frame_status != MFI_STAT_INVALID_STATUS) {
        if (cmd->frame) {
            cmd->frame->header.cmd_status = frame_status;
        } else {
            megasas_frame_set_cmd_status(s, frame_addr, frame_status);
        }
        megasas_unmap_frame(s, cmd);
        megasas_complete_frame(s, cmd->context);
    }
}

static uint64_t megasas_mmio_read(void *opaque, hwaddr addr,
                                  unsigned size)
{
    MegasasState *s = opaque;
    PCIDevice *pci_dev = PCI_DEVICE(s);
    MegasasBaseClass *base_class = MEGASAS_GET_CLASS(s);
    uint32_t retval = 0;

    switch (addr) {
    case MFI_IDB:
        retval = 0;
        trace_megasas_mmio_readl("MFI_IDB", retval);
        break;
    case MFI_OMSG0:
    case MFI_OSP0:
        retval = (msix_present(pci_dev) ? MFI_FWSTATE_MSIX_SUPPORTED : 0) |
            (s->fw_state & MFI_FWSTATE_MASK) |
            ((s->fw_sge & 0xff) << 16) |
            (s->fw_cmds & 0xFFFF);
        trace_megasas_mmio_readl(addr == MFI_OMSG0 ? "MFI_OMSG0" : "MFI_OSP0",
                                 retval);
        break;
    case MFI_OSTS:
        if (megasas_intr_enabled(s) && s->doorbell) {
            retval = base_class->osts;
        }
        trace_megasas_mmio_readl("MFI_OSTS", retval);
        break;
    case MFI_OMSK:
        retval = s->intr_mask;
        trace_megasas_mmio_readl("MFI_OMSK", retval);
        break;
    case MFI_ODCR0:
        retval = s->doorbell ? 1 : 0;
        trace_megasas_mmio_readl("MFI_ODCR0", retval);
        break;
    case MFI_DIAG:
        retval = s->diag;
        trace_megasas_mmio_readl("MFI_DIAG", retval);
        break;
    case MFI_OSP1:
        retval = 15;
        trace_megasas_mmio_readl("MFI_OSP1", retval);
        break;
    default:
        trace_megasas_mmio_invalid_readl(addr);
        break;
    }
    return retval;
}

static int adp_reset_seq[] = {0x00, 0x04, 0x0b, 0x02, 0x07, 0x0d};

static void megasas_mmio_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size)
{
    MegasasState *s = opaque;
    PCIDevice *pci_dev = PCI_DEVICE(s);
    uint64_t frame_addr;
    uint32_t frame_count;
    int i;

    switch (addr) {
    case MFI_IDB:
        trace_megasas_mmio_writel("MFI_IDB", val);
        if (val & MFI_FWINIT_ABORT) {
            /* Abort all pending cmds */
            for (i = 0; i < s->fw_cmds; i++) {
                megasas_abort_command(&s->frames[i]);
            }
        }
        if (val & MFI_FWINIT_READY) {
            /* move to FW READY */
            megasas_soft_reset(s);
        }
        if (val & MFI_FWINIT_MFIMODE) {
            /* discard MFIs */
        }
        if (val & MFI_FWINIT_STOP_ADP) {
            /* Terminal error, stop processing */
            s->fw_state = MFI_FWSTATE_FAULT;
        }
        break;
    case MFI_OMSK:
        trace_megasas_mmio_writel("MFI_OMSK", val);
        s->intr_mask = val;
        if (!megasas_intr_enabled(s) &&
            !msi_enabled(pci_dev) &&
            !msix_enabled(pci_dev)) {
            trace_megasas_irq_lower();
            pci_irq_deassert(pci_dev);
        }
        if (megasas_intr_enabled(s)) {
            if (msix_enabled(pci_dev)) {
                trace_megasas_msix_enabled(0);
            } else if (msi_enabled(pci_dev)) {
                trace_megasas_msi_enabled(0);
            } else {
                trace_megasas_intr_enabled();
            }
        } else {
            trace_megasas_intr_disabled();
            megasas_soft_reset(s);
        }
        break;
    case MFI_ODCR0:
        trace_megasas_mmio_writel("MFI_ODCR0", val);
        s->doorbell = 0;
        if (megasas_intr_enabled(s)) {
            if (!msix_enabled(pci_dev) && !msi_enabled(pci_dev)) {
                trace_megasas_irq_lower();
                pci_irq_deassert(pci_dev);
            }
        }
        break;
    case MFI_IQPH:
        trace_megasas_mmio_writel("MFI_IQPH", val);
        /* Received high 32 bits of a 64 bit MFI frame address */
        s->frame_hi = val;
        break;
    case MFI_IQPL:
        trace_megasas_mmio_writel("MFI_IQPL", val);
        /* Received low 32 bits of a 64 bit MFI frame address */
        /* Fallthrough */
    case MFI_IQP:
        if (addr == MFI_IQP) {
            trace_megasas_mmio_writel("MFI_IQP", val);
            /* Received 64 bit MFI frame address */
            s->frame_hi = 0;
        }
        frame_addr = (val & ~0x1F);
        /* Add possible 64 bit offset */
        frame_addr |= ((uint64_t)s->frame_hi << 32);
        s->frame_hi = 0;
        frame_count = (val >> 1) & 0xF;
        megasas_handle_frame(s, frame_addr, frame_count);
        break;
    case MFI_SEQ:
        trace_megasas_mmio_writel("MFI_SEQ", val);
        /* Magic sequence to start ADP reset */
        if (adp_reset_seq[s->adp_reset++] == val) {
            if (s->adp_reset == 6) {
                s->adp_reset = 0;
                s->diag = MFI_DIAG_WRITE_ENABLE;
            }
        } else {
            s->adp_reset = 0;
            s->diag = 0;
        }
        break;
    case MFI_DIAG:
        trace_megasas_mmio_writel("MFI_DIAG", val);
        /* ADP reset */
        if ((s->diag & MFI_DIAG_WRITE_ENABLE) &&
            (val & MFI_DIAG_RESET_ADP)) {
            s->diag |= MFI_DIAG_RESET_ADP;
            megasas_soft_reset(s);
            s->adp_reset = 0;
            s->diag = 0;
        }
        break;
    default:
        trace_megasas_mmio_invalid_writel(addr, val);
        break;
    }
}

static const MemoryRegionOps megasas_mmio_ops = {
    .read = megasas_mmio_read,
    .write = megasas_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
    }
};

static uint64_t megasas_port_read(void *opaque, hwaddr addr,
                                  unsigned size)
{
    return megasas_mmio_read(opaque, addr & 0xff, size);
}

static void megasas_port_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size)
{
    megasas_mmio_write(opaque, addr & 0xff, val, size);
}

static const MemoryRegionOps megasas_port_ops = {
    .read = megasas_port_read,
    .write = megasas_port_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static uint64_t megasas_queue_read(void *opaque, hwaddr addr,
                                   unsigned size)
{
    return 0;
}

static void megasas_queue_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size)
{
    return;
}

static const MemoryRegionOps megasas_queue_ops = {
    .read = megasas_queue_read,
    .write = megasas_queue_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
    }
};

static void megasas_soft_reset(MegasasState *s)
{
    int i;
    MegasasCmd *cmd;

    trace_megasas_reset(s->fw_state);
    for (i = 0; i < s->fw_cmds; i++) {
        cmd = &s->frames[i];
        megasas_abort_command(cmd);
    }
    if (s->fw_state == MFI_FWSTATE_READY) {
        BusChild *kid;

        /*
         * The EFI firmware doesn't handle UA,
         * so we need to clear the Power On/Reset UA
         * after the initial reset.
         */
        QTAILQ_FOREACH(kid, &s->bus.qbus.children, sibling) {
            SCSIDevice *sdev = SCSI_DEVICE(kid->child);

            sdev->unit_attention = SENSE_CODE(NO_SENSE);
            scsi_device_unit_attention_reported(sdev);
        }
    }
    megasas_reset_frames(s);
    s->reply_queue_len = s->fw_cmds;
    s->reply_queue_pa = 0;
    s->consumer_pa = 0;
    s->producer_pa = 0;
    s->fw_state = MFI_FWSTATE_READY;
    s->doorbell = 0;
    s->intr_mask = MEGASAS_INTR_DISABLED_MASK;
    s->frame_hi = 0;
    s->flags &= ~MEGASAS_MASK_USE_QUEUE64;
    s->event_count++;
    s->boot_event = s->event_count;
}

static void megasas_scsi_reset(DeviceState *dev)
{
    MegasasState *s = MEGASAS(dev);

    megasas_soft_reset(s);
}

static const VMStateDescription vmstate_megasas_gen1 = {
    .name = "megasas",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, MegasasState),
        VMSTATE_MSIX(parent_obj, MegasasState),

        VMSTATE_UINT32(fw_state, MegasasState),
        VMSTATE_UINT32(intr_mask, MegasasState),
        VMSTATE_UINT32(doorbell, MegasasState),
        VMSTATE_UINT64(reply_queue_pa, MegasasState),
        VMSTATE_UINT64(consumer_pa, MegasasState),
        VMSTATE_UINT64(producer_pa, MegasasState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_megasas_gen2 = {
    .name = "megasas-gen2",
    .version_id = 0,
    .minimum_version_id = 0,
    .minimum_version_id_old = 0,
    .fields      = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, MegasasState),
        VMSTATE_MSIX(parent_obj, MegasasState),

        VMSTATE_UINT32(fw_state, MegasasState),
        VMSTATE_UINT32(intr_mask, MegasasState),
        VMSTATE_UINT32(doorbell, MegasasState),
        VMSTATE_UINT64(reply_queue_pa, MegasasState),
        VMSTATE_UINT64(consumer_pa, MegasasState),
        VMSTATE_UINT64(producer_pa, MegasasState),
        VMSTATE_END_OF_LIST()
    }
};

static void megasas_scsi_uninit(PCIDevice *d)
{
    MegasasState *s = MEGASAS(d);

    if (megasas_use_msix(s)) {
        msix_uninit(d, &s->mmio_io, &s->mmio_io);
    }
    msi_uninit(d);
}

static const struct SCSIBusInfo megasas_scsi_info = {
    .tcq = true,
    .max_target = MFI_MAX_LD,
    .max_lun = 255,

    .transfer_data = megasas_xfer_complete,
    .get_sg_list = megasas_get_sg_list,
    .complete = megasas_command_complete,
    .cancel = megasas_command_cancelled,
};

static void megasas_scsi_realize(PCIDevice *dev, Error **errp)
{
    MegasasState *s = MEGASAS(dev);
    MegasasBaseClass *b = MEGASAS_GET_CLASS(s);
    uint8_t *pci_conf;
    int i, bar_type;
    Error *err = NULL;
    int ret;

    pci_conf = dev->config;

    /* PCI latency timer = 0 */
    pci_conf[PCI_LATENCY_TIMER] = 0;
    /* Interrupt pin 1 */
    pci_conf[PCI_INTERRUPT_PIN] = 0x01;

    if (s->msi != ON_OFF_AUTO_OFF) {
        ret = msi_init(dev, 0x50, 1, true, false, &err);
        /* Any error other than -ENOTSUP(board's MSI support is broken)
         * is a programming error */
        assert(!ret || ret == -ENOTSUP);
        if (ret && s->msi == ON_OFF_AUTO_ON) {
            /* Can't satisfy user's explicit msi=on request, fail */
            error_append_hint(&err, "You have to use msi=auto (default) or "
                    "msi=off with this machine type.\n");
            error_propagate(errp, err);
            return;
        } else if (ret) {
            /* With msi=auto, we fall back to MSI off silently */
            s->msi = ON_OFF_AUTO_OFF;
            error_free(err);
        }
    }

    memory_region_init_io(&s->mmio_io, OBJECT(s), &megasas_mmio_ops, s,
                          "megasas-mmio", 0x4000);
    memory_region_init_io(&s->port_io, OBJECT(s), &megasas_port_ops, s,
                          "megasas-io", 256);
    memory_region_init_io(&s->queue_io, OBJECT(s), &megasas_queue_ops, s,
                          "megasas-queue", 0x40000);

    if (megasas_use_msix(s) &&
        msix_init(dev, 15, &s->mmio_io, b->mmio_bar, 0x2000,
                  &s->mmio_io, b->mmio_bar, 0x3800, 0x68, NULL)) {
        /* TODO: check msix_init's error, and should fail on msix=on */
        s->msix = ON_OFF_AUTO_OFF;
    }

    if (pci_is_express(dev)) {
        pcie_endpoint_cap_init(dev, 0xa0);
    }

    bar_type = PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_64;
    pci_register_bar(dev, b->ioport_bar,
                     PCI_BASE_ADDRESS_SPACE_IO, &s->port_io);
    pci_register_bar(dev, b->mmio_bar, bar_type, &s->mmio_io);
    pci_register_bar(dev, 3, bar_type, &s->queue_io);

    if (megasas_use_msix(s)) {
        msix_vector_use(dev, 0);
    }

    s->fw_state = MFI_FWSTATE_READY;
    if (!s->sas_addr) {
        s->sas_addr = ((NAA_LOCALLY_ASSIGNED_ID << 24) |
                       IEEE_COMPANY_LOCALLY_ASSIGNED) << 36;
        s->sas_addr |= pci_dev_bus_num(dev) << 16;
        s->sas_addr |= PCI_SLOT(dev->devfn) << 8;
        s->sas_addr |= PCI_FUNC(dev->devfn);
    }
    if (!s->hba_serial) {
        s->hba_serial = g_strdup(MEGASAS_HBA_SERIAL);
    }
    if (s->fw_sge >= MEGASAS_MAX_SGE - MFI_PASS_FRAME_SIZE) {
        s->fw_sge = MEGASAS_MAX_SGE - MFI_PASS_FRAME_SIZE;
    } else if (s->fw_sge >= 128 - MFI_PASS_FRAME_SIZE) {
        s->fw_sge = 128 - MFI_PASS_FRAME_SIZE;
    } else {
        s->fw_sge = 64 - MFI_PASS_FRAME_SIZE;
    }
    if (s->fw_cmds > MEGASAS_MAX_FRAMES) {
        s->fw_cmds = MEGASAS_MAX_FRAMES;
    }
    trace_megasas_init(s->fw_sge, s->fw_cmds,
                       megasas_is_jbod(s) ? "jbod" : "raid");

    if (megasas_is_jbod(s)) {
        s->fw_luns = MFI_MAX_SYS_PDS;
    } else {
        s->fw_luns = MFI_MAX_LD;
    }
    s->producer_pa = 0;
    s->consumer_pa = 0;
    for (i = 0; i < s->fw_cmds; i++) {
        s->frames[i].index = i;
        s->frames[i].context = -1;
        s->frames[i].pa = 0;
        s->frames[i].state = s;
    }

    scsi_bus_init(&s->bus, sizeof(s->bus), DEVICE(dev), &megasas_scsi_info);
}

static Property megasas_properties_gen1[] = {
    DEFINE_PROP_UINT32("max_sge", MegasasState, fw_sge,
                       MEGASAS_DEFAULT_SGE),
    DEFINE_PROP_UINT32("max_cmds", MegasasState, fw_cmds,
                       MEGASAS_DEFAULT_FRAMES),
    DEFINE_PROP_STRING("hba_serial", MegasasState, hba_serial),
    DEFINE_PROP_UINT64("sas_address", MegasasState, sas_addr, 0),
    DEFINE_PROP_ON_OFF_AUTO("msi", MegasasState, msi, ON_OFF_AUTO_AUTO),
    DEFINE_PROP_ON_OFF_AUTO("msix", MegasasState, msix, ON_OFF_AUTO_AUTO),
    DEFINE_PROP_BIT("use_jbod", MegasasState, flags,
                    MEGASAS_FLAG_USE_JBOD, false),
    DEFINE_PROP_END_OF_LIST(),
};

static Property megasas_properties_gen2[] = {
    DEFINE_PROP_UINT32("max_sge", MegasasState, fw_sge,
                       MEGASAS_DEFAULT_SGE),
    DEFINE_PROP_UINT32("max_cmds", MegasasState, fw_cmds,
                       MEGASAS_GEN2_DEFAULT_FRAMES),
    DEFINE_PROP_STRING("hba_serial", MegasasState, hba_serial),
    DEFINE_PROP_UINT64("sas_address", MegasasState, sas_addr, 0),
    DEFINE_PROP_ON_OFF_AUTO("msi", MegasasState, msi, ON_OFF_AUTO_AUTO),
    DEFINE_PROP_ON_OFF_AUTO("msix", MegasasState, msix, ON_OFF_AUTO_AUTO),
    DEFINE_PROP_BIT("use_jbod", MegasasState, flags,
                    MEGASAS_FLAG_USE_JBOD, false),
    DEFINE_PROP_END_OF_LIST(),
};

typedef struct MegasasInfo {
    const char *name;
    const char *desc;
    const char *product_name;
    const char *product_version;
    uint16_t device_id;
    uint16_t subsystem_id;
    int ioport_bar;
    int mmio_bar;
    int osts;
    const VMStateDescription *vmsd;
    Property *props;
    InterfaceInfo *interfaces;
} MegasasInfo;

static struct MegasasInfo megasas_devices[] = {
    {
        .name = TYPE_MEGASAS_GEN1,
        .desc = "LSI MegaRAID SAS 1078",
        .product_name = "LSI MegaRAID SAS 8708EM2",
        .product_version = MEGASAS_VERSION_GEN1,
        .device_id = PCI_DEVICE_ID_LSI_SAS1078,
        .subsystem_id = 0x1013,
        .ioport_bar = 2,
        .mmio_bar = 0,
        .osts = MFI_1078_RM | 1,
        .vmsd = &vmstate_megasas_gen1,
        .props = megasas_properties_gen1,
        .interfaces = (InterfaceInfo[]) {
            { INTERFACE_CONVENTIONAL_PCI_DEVICE },
            { },
        },
    },{
        .name = TYPE_MEGASAS_GEN2,
        .desc = "LSI MegaRAID SAS 2108",
        .product_name = "LSI MegaRAID SAS 9260-8i",
        .product_version = MEGASAS_VERSION_GEN2,
        .device_id = PCI_DEVICE_ID_LSI_SAS0079,
        .subsystem_id = 0x9261,
        .ioport_bar = 0,
        .mmio_bar = 1,
        .osts = MFI_GEN2_RM,
        .vmsd = &vmstate_megasas_gen2,
        .props = megasas_properties_gen2,
        .interfaces = (InterfaceInfo[]) {
            { INTERFACE_PCIE_DEVICE },
            { }
        },
    }
};

static void megasas_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(oc);
    MegasasBaseClass *e = MEGASAS_CLASS(oc);
    const MegasasInfo *info = data;

    pc->realize = megasas_scsi_realize;
    pc->exit = megasas_scsi_uninit;
    pc->vendor_id = PCI_VENDOR_ID_LSI_LOGIC;
    pc->device_id = info->device_id;
    pc->subsystem_vendor_id = PCI_VENDOR_ID_LSI_LOGIC;
    pc->subsystem_id = info->subsystem_id;
    pc->class_id = PCI_CLASS_STORAGE_RAID;
    e->mmio_bar = info->mmio_bar;
    e->ioport_bar = info->ioport_bar;
    e->osts = info->osts;
    e->product_name = info->product_name;
    e->product_version = info->product_version;
    device_class_set_props(dc, info->props);
    dc->reset = megasas_scsi_reset;
    dc->vmsd = info->vmsd;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    dc->desc = info->desc;
}

static const TypeInfo megasas_info = {
    .name  = TYPE_MEGASAS_BASE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(MegasasState),
    .class_size = sizeof(MegasasBaseClass),
    .abstract = true,
};

static void megasas_register_types(void)
{
    int i;

    type_register_static(&megasas_info);
    for (i = 0; i < ARRAY_SIZE(megasas_devices); i++) {
        const MegasasInfo *info = &megasas_devices[i];
        TypeInfo type_info = {};

        type_info.name = info->name;
        type_info.parent = TYPE_MEGASAS_BASE;
        type_info.class_data = (void *)info;
        type_info.class_init = megasas_class_init;
        type_info.interfaces = info->interfaces;

        type_register(&type_info);
    }
}

type_init(megasas_register_types)
