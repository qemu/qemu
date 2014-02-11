/*
 * Channel subsystem base support.
 *
 * Copyright 2012 IBM Corp.
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include <hw/qdev.h>
#include "qemu/bitops.h"
#include "exec/address-spaces.h"
#include "cpu.h"
#include "ioinst.h"
#include "css.h"
#include "trace.h"
#include "hw/s390x/s390_flic.h"

typedef struct CrwContainer {
    CRW crw;
    QTAILQ_ENTRY(CrwContainer) sibling;
} CrwContainer;

typedef struct ChpInfo {
    uint8_t in_use;
    uint8_t type;
    uint8_t is_virtual;
} ChpInfo;

typedef struct SubchSet {
    SubchDev *sch[MAX_SCHID + 1];
    unsigned long schids_used[BITS_TO_LONGS(MAX_SCHID + 1)];
    unsigned long devnos_used[BITS_TO_LONGS(MAX_SCHID + 1)];
} SubchSet;

typedef struct CssImage {
    SubchSet *sch_set[MAX_SSID + 1];
    ChpInfo chpids[MAX_CHPID + 1];
} CssImage;

typedef struct IoAdapter {
    uint32_t id;
    uint8_t type;
    uint8_t isc;
    QTAILQ_ENTRY(IoAdapter) sibling;
} IoAdapter;

typedef struct ChannelSubSys {
    QTAILQ_HEAD(, CrwContainer) pending_crws;
    bool do_crw_mchk;
    bool crws_lost;
    uint8_t max_cssid;
    uint8_t max_ssid;
    bool chnmon_active;
    uint64_t chnmon_area;
    CssImage *css[MAX_CSSID + 1];
    uint8_t default_cssid;
    QTAILQ_HEAD(, IoAdapter) io_adapters;
} ChannelSubSys;

static ChannelSubSys *channel_subsys;

int css_create_css_image(uint8_t cssid, bool default_image)
{
    trace_css_new_image(cssid, default_image ? "(default)" : "");
    if (cssid > MAX_CSSID) {
        return -EINVAL;
    }
    if (channel_subsys->css[cssid]) {
        return -EBUSY;
    }
    channel_subsys->css[cssid] = g_malloc0(sizeof(CssImage));
    if (default_image) {
        channel_subsys->default_cssid = cssid;
    }
    return 0;
}

int css_register_io_adapter(uint8_t type, uint8_t isc, bool swap,
                            bool maskable, uint32_t *id)
{
    IoAdapter *adapter;
    bool found = false;
    int ret;
    S390FLICState *fs = s390_get_flic();
    S390FLICStateClass *fsc = S390_FLIC_COMMON_GET_CLASS(fs);

    *id = 0;
    QTAILQ_FOREACH(adapter, &channel_subsys->io_adapters, sibling) {
        if ((adapter->type == type) && (adapter->isc == isc)) {
            *id = adapter->id;
            found = true;
            ret = 0;
            break;
        }
        if (adapter->id >= *id) {
            *id = adapter->id + 1;
        }
    }
    if (found) {
        goto out;
    }
    adapter = g_new0(IoAdapter, 1);
    ret = fsc->register_io_adapter(fs, *id, isc, swap, maskable);
    if (ret == 0) {
        adapter->id = *id;
        adapter->isc = isc;
        adapter->type = type;
        QTAILQ_INSERT_TAIL(&channel_subsys->io_adapters, adapter, sibling);
    } else {
        g_free(adapter);
        fprintf(stderr, "Unexpected error %d when registering adapter %d\n",
                ret, *id);
    }
out:
    return ret;
}

uint16_t css_build_subchannel_id(SubchDev *sch)
{
    if (channel_subsys->max_cssid > 0) {
        return (sch->cssid << 8) | (1 << 3) | (sch->ssid << 1) | 1;
    }
    return (sch->ssid << 1) | 1;
}

static void css_inject_io_interrupt(SubchDev *sch)
{
    S390CPU *cpu = s390_cpu_addr2state(0);
    uint8_t isc = (sch->curr_status.pmcw.flags & PMCW_FLAGS_MASK_ISC) >> 11;

    trace_css_io_interrupt(sch->cssid, sch->ssid, sch->schid,
                           sch->curr_status.pmcw.intparm, isc, "");
    s390_io_interrupt(cpu,
                      css_build_subchannel_id(sch),
                      sch->schid,
                      sch->curr_status.pmcw.intparm,
                      isc << 27);
}

void css_conditional_io_interrupt(SubchDev *sch)
{
    /*
     * If the subchannel is not currently status pending, make it pending
     * with alert status.
     */
    if (!(sch->curr_status.scsw.ctrl & SCSW_STCTL_STATUS_PEND)) {
        S390CPU *cpu = s390_cpu_addr2state(0);
        uint8_t isc = (sch->curr_status.pmcw.flags & PMCW_FLAGS_MASK_ISC) >> 11;

        trace_css_io_interrupt(sch->cssid, sch->ssid, sch->schid,
                               sch->curr_status.pmcw.intparm, isc,
                               "(unsolicited)");
        sch->curr_status.scsw.ctrl &= ~SCSW_CTRL_MASK_STCTL;
        sch->curr_status.scsw.ctrl |=
            SCSW_STCTL_ALERT | SCSW_STCTL_STATUS_PEND;
        /* Inject an I/O interrupt. */
        s390_io_interrupt(cpu,
                          css_build_subchannel_id(sch),
                          sch->schid,
                          sch->curr_status.pmcw.intparm,
                          isc << 27);
    }
}

void css_adapter_interrupt(uint8_t isc)
{
    S390CPU *cpu = s390_cpu_addr2state(0);
    uint32_t io_int_word = (isc << 27) | IO_INT_WORD_AI;

    trace_css_adapter_interrupt(isc);
    s390_io_interrupt(cpu, 0, 0, 0, io_int_word);
}

static void sch_handle_clear_func(SubchDev *sch)
{
    PMCW *p = &sch->curr_status.pmcw;
    SCSW *s = &sch->curr_status.scsw;
    int path;

    /* Path management: In our simple css, we always choose the only path. */
    path = 0x80;

    /* Reset values prior to 'issuing the clear signal'. */
    p->lpum = 0;
    p->pom = 0xff;
    s->flags &= ~SCSW_FLAGS_MASK_PNO;

    /* We always 'attempt to issue the clear signal', and we always succeed. */
    sch->channel_prog = 0x0;
    sch->last_cmd_valid = false;
    s->ctrl &= ~SCSW_ACTL_CLEAR_PEND;
    s->ctrl |= SCSW_STCTL_STATUS_PEND;

    s->dstat = 0;
    s->cstat = 0;
    p->lpum = path;

}

static void sch_handle_halt_func(SubchDev *sch)
{

    PMCW *p = &sch->curr_status.pmcw;
    SCSW *s = &sch->curr_status.scsw;
    int path;

    /* Path management: In our simple css, we always choose the only path. */
    path = 0x80;

    /* We always 'attempt to issue the halt signal', and we always succeed. */
    sch->channel_prog = 0x0;
    sch->last_cmd_valid = false;
    s->ctrl &= ~SCSW_ACTL_HALT_PEND;
    s->ctrl |= SCSW_STCTL_STATUS_PEND;

    if ((s->ctrl & (SCSW_ACTL_SUBCH_ACTIVE | SCSW_ACTL_DEVICE_ACTIVE)) ||
        !((s->ctrl & SCSW_ACTL_START_PEND) ||
          (s->ctrl & SCSW_ACTL_SUSP))) {
        s->dstat = SCSW_DSTAT_DEVICE_END;
    }
    s->cstat = 0;
    p->lpum = path;

}

static void copy_sense_id_to_guest(SenseId *dest, SenseId *src)
{
    int i;

    dest->reserved = src->reserved;
    dest->cu_type = cpu_to_be16(src->cu_type);
    dest->cu_model = src->cu_model;
    dest->dev_type = cpu_to_be16(src->dev_type);
    dest->dev_model = src->dev_model;
    dest->unused = src->unused;
    for (i = 0; i < ARRAY_SIZE(dest->ciw); i++) {
        dest->ciw[i].type = src->ciw[i].type;
        dest->ciw[i].command = src->ciw[i].command;
        dest->ciw[i].count = cpu_to_be16(src->ciw[i].count);
    }
}

static CCW1 copy_ccw_from_guest(hwaddr addr)
{
    CCW1 tmp;
    CCW1 ret;

    cpu_physical_memory_read(addr, &tmp, sizeof(tmp));
    ret.cmd_code = tmp.cmd_code;
    ret.flags = tmp.flags;
    ret.count = be16_to_cpu(tmp.count);
    ret.cda = be32_to_cpu(tmp.cda);

    return ret;
}

static int css_interpret_ccw(SubchDev *sch, hwaddr ccw_addr)
{
    int ret;
    bool check_len;
    int len;
    CCW1 ccw;

    if (!ccw_addr) {
        return -EIO;
    }

    ccw = copy_ccw_from_guest(ccw_addr);

    /* Check for invalid command codes. */
    if ((ccw.cmd_code & 0x0f) == 0) {
        return -EINVAL;
    }
    if (((ccw.cmd_code & 0x0f) == CCW_CMD_TIC) &&
        ((ccw.cmd_code & 0xf0) != 0)) {
        return -EINVAL;
    }

    if (ccw.flags & CCW_FLAG_SUSPEND) {
        return -EINPROGRESS;
    }

    check_len = !((ccw.flags & CCW_FLAG_SLI) && !(ccw.flags & CCW_FLAG_DC));

    /* Look at the command. */
    switch (ccw.cmd_code) {
    case CCW_CMD_NOOP:
        /* Nothing to do. */
        ret = 0;
        break;
    case CCW_CMD_BASIC_SENSE:
        if (check_len) {
            if (ccw.count != sizeof(sch->sense_data)) {
                ret = -EINVAL;
                break;
            }
        }
        len = MIN(ccw.count, sizeof(sch->sense_data));
        cpu_physical_memory_write(ccw.cda, sch->sense_data, len);
        sch->curr_status.scsw.count = ccw.count - len;
        memset(sch->sense_data, 0, sizeof(sch->sense_data));
        ret = 0;
        break;
    case CCW_CMD_SENSE_ID:
    {
        SenseId sense_id;

        copy_sense_id_to_guest(&sense_id, &sch->id);
        /* Sense ID information is device specific. */
        if (check_len) {
            if (ccw.count != sizeof(sense_id)) {
                ret = -EINVAL;
                break;
            }
        }
        len = MIN(ccw.count, sizeof(sense_id));
        /*
         * Only indicate 0xff in the first sense byte if we actually
         * have enough place to store at least bytes 0-3.
         */
        if (len >= 4) {
            sense_id.reserved = 0xff;
        } else {
            sense_id.reserved = 0;
        }
        cpu_physical_memory_write(ccw.cda, &sense_id, len);
        sch->curr_status.scsw.count = ccw.count - len;
        ret = 0;
        break;
    }
    case CCW_CMD_TIC:
        if (sch->last_cmd_valid && (sch->last_cmd.cmd_code == CCW_CMD_TIC)) {
            ret = -EINVAL;
            break;
        }
        if (ccw.flags & (CCW_FLAG_CC | CCW_FLAG_DC)) {
            ret = -EINVAL;
            break;
        }
        sch->channel_prog = ccw.cda;
        ret = -EAGAIN;
        break;
    default:
        if (sch->ccw_cb) {
            /* Handle device specific commands. */
            ret = sch->ccw_cb(sch, ccw);
        } else {
            ret = -ENOSYS;
        }
        break;
    }
    sch->last_cmd = ccw;
    sch->last_cmd_valid = true;
    if (ret == 0) {
        if (ccw.flags & CCW_FLAG_CC) {
            sch->channel_prog += 8;
            ret = -EAGAIN;
        }
    }

    return ret;
}

static void sch_handle_start_func(SubchDev *sch, ORB *orb)
{

    PMCW *p = &sch->curr_status.pmcw;
    SCSW *s = &sch->curr_status.scsw;
    int path;
    int ret;

    /* Path management: In our simple css, we always choose the only path. */
    path = 0x80;

    if (!(s->ctrl & SCSW_ACTL_SUSP)) {
        /* Look at the orb and try to execute the channel program. */
        assert(orb != NULL); /* resume does not pass an orb */
        p->intparm = orb->intparm;
        if (!(orb->lpm & path)) {
            /* Generate a deferred cc 3 condition. */
            s->flags |= SCSW_FLAGS_MASK_CC;
            s->ctrl &= ~SCSW_CTRL_MASK_STCTL;
            s->ctrl |= (SCSW_STCTL_ALERT | SCSW_STCTL_STATUS_PEND);
            return;
        }
    } else {
        s->ctrl &= ~(SCSW_ACTL_SUSP | SCSW_ACTL_RESUME_PEND);
    }
    sch->last_cmd_valid = false;
    do {
        ret = css_interpret_ccw(sch, sch->channel_prog);
        switch (ret) {
        case -EAGAIN:
            /* ccw chain, continue processing */
            break;
        case 0:
            /* success */
            s->ctrl &= ~SCSW_ACTL_START_PEND;
            s->ctrl &= ~SCSW_CTRL_MASK_STCTL;
            s->ctrl |= SCSW_STCTL_PRIMARY | SCSW_STCTL_SECONDARY |
                    SCSW_STCTL_STATUS_PEND;
            s->dstat = SCSW_DSTAT_CHANNEL_END | SCSW_DSTAT_DEVICE_END;
            break;
        case -ENOSYS:
            /* unsupported command, generate unit check (command reject) */
            s->ctrl &= ~SCSW_ACTL_START_PEND;
            s->dstat = SCSW_DSTAT_UNIT_CHECK;
            /* Set sense bit 0 in ecw0. */
            sch->sense_data[0] = 0x80;
            s->ctrl &= ~SCSW_CTRL_MASK_STCTL;
            s->ctrl |= SCSW_STCTL_PRIMARY | SCSW_STCTL_SECONDARY |
                    SCSW_STCTL_ALERT | SCSW_STCTL_STATUS_PEND;
            break;
        case -EFAULT:
            /* memory problem, generate channel data check */
            s->ctrl &= ~SCSW_ACTL_START_PEND;
            s->cstat = SCSW_CSTAT_DATA_CHECK;
            s->ctrl &= ~SCSW_CTRL_MASK_STCTL;
            s->ctrl |= SCSW_STCTL_PRIMARY | SCSW_STCTL_SECONDARY |
                    SCSW_STCTL_ALERT | SCSW_STCTL_STATUS_PEND;
            break;
        case -EBUSY:
            /* subchannel busy, generate deferred cc 1 */
            s->flags &= ~SCSW_FLAGS_MASK_CC;
            s->flags |= (1 << 8);
            s->ctrl &= ~SCSW_CTRL_MASK_STCTL;
            s->ctrl |= SCSW_STCTL_ALERT | SCSW_STCTL_STATUS_PEND;
            break;
        case -EINPROGRESS:
            /* channel program has been suspended */
            s->ctrl &= ~SCSW_ACTL_START_PEND;
            s->ctrl |= SCSW_ACTL_SUSP;
            break;
        default:
            /* error, generate channel program check */
            s->ctrl &= ~SCSW_ACTL_START_PEND;
            s->cstat = SCSW_CSTAT_PROG_CHECK;
            s->ctrl &= ~SCSW_CTRL_MASK_STCTL;
            s->ctrl |= SCSW_STCTL_PRIMARY | SCSW_STCTL_SECONDARY |
                    SCSW_STCTL_ALERT | SCSW_STCTL_STATUS_PEND;
            break;
        }
    } while (ret == -EAGAIN);

}

/*
 * On real machines, this would run asynchronously to the main vcpus.
 * We might want to make some parts of the ssch handling (interpreting
 * read/writes) asynchronous later on if we start supporting more than
 * our current very simple devices.
 */
static void do_subchannel_work(SubchDev *sch, ORB *orb)
{

    SCSW *s = &sch->curr_status.scsw;

    if (s->ctrl & SCSW_FCTL_CLEAR_FUNC) {
        sch_handle_clear_func(sch);
    } else if (s->ctrl & SCSW_FCTL_HALT_FUNC) {
        sch_handle_halt_func(sch);
    } else if (s->ctrl & SCSW_FCTL_START_FUNC) {
        sch_handle_start_func(sch, orb);
    } else {
        /* Cannot happen. */
        return;
    }
    css_inject_io_interrupt(sch);
}

static void copy_pmcw_to_guest(PMCW *dest, const PMCW *src)
{
    int i;

    dest->intparm = cpu_to_be32(src->intparm);
    dest->flags = cpu_to_be16(src->flags);
    dest->devno = cpu_to_be16(src->devno);
    dest->lpm = src->lpm;
    dest->pnom = src->pnom;
    dest->lpum = src->lpum;
    dest->pim = src->pim;
    dest->mbi = cpu_to_be16(src->mbi);
    dest->pom = src->pom;
    dest->pam = src->pam;
    for (i = 0; i < ARRAY_SIZE(dest->chpid); i++) {
        dest->chpid[i] = src->chpid[i];
    }
    dest->chars = cpu_to_be32(src->chars);
}

static void copy_scsw_to_guest(SCSW *dest, const SCSW *src)
{
    dest->flags = cpu_to_be16(src->flags);
    dest->ctrl = cpu_to_be16(src->ctrl);
    dest->cpa = cpu_to_be32(src->cpa);
    dest->dstat = src->dstat;
    dest->cstat = src->cstat;
    dest->count = cpu_to_be16(src->count);
}

static void copy_schib_to_guest(SCHIB *dest, const SCHIB *src)
{
    int i;

    copy_pmcw_to_guest(&dest->pmcw, &src->pmcw);
    copy_scsw_to_guest(&dest->scsw, &src->scsw);
    dest->mba = cpu_to_be64(src->mba);
    for (i = 0; i < ARRAY_SIZE(dest->mda); i++) {
        dest->mda[i] = src->mda[i];
    }
}

int css_do_stsch(SubchDev *sch, SCHIB *schib)
{
    /* Use current status. */
    copy_schib_to_guest(schib, &sch->curr_status);
    return 0;
}

static void copy_pmcw_from_guest(PMCW *dest, const PMCW *src)
{
    int i;

    dest->intparm = be32_to_cpu(src->intparm);
    dest->flags = be16_to_cpu(src->flags);
    dest->devno = be16_to_cpu(src->devno);
    dest->lpm = src->lpm;
    dest->pnom = src->pnom;
    dest->lpum = src->lpum;
    dest->pim = src->pim;
    dest->mbi = be16_to_cpu(src->mbi);
    dest->pom = src->pom;
    dest->pam = src->pam;
    for (i = 0; i < ARRAY_SIZE(dest->chpid); i++) {
        dest->chpid[i] = src->chpid[i];
    }
    dest->chars = be32_to_cpu(src->chars);
}

static void copy_scsw_from_guest(SCSW *dest, const SCSW *src)
{
    dest->flags = be16_to_cpu(src->flags);
    dest->ctrl = be16_to_cpu(src->ctrl);
    dest->cpa = be32_to_cpu(src->cpa);
    dest->dstat = src->dstat;
    dest->cstat = src->cstat;
    dest->count = be16_to_cpu(src->count);
}

static void copy_schib_from_guest(SCHIB *dest, const SCHIB *src)
{
    int i;

    copy_pmcw_from_guest(&dest->pmcw, &src->pmcw);
    copy_scsw_from_guest(&dest->scsw, &src->scsw);
    dest->mba = be64_to_cpu(src->mba);
    for (i = 0; i < ARRAY_SIZE(dest->mda); i++) {
        dest->mda[i] = src->mda[i];
    }
}

int css_do_msch(SubchDev *sch, SCHIB *orig_schib)
{
    SCSW *s = &sch->curr_status.scsw;
    PMCW *p = &sch->curr_status.pmcw;
    int ret;
    SCHIB schib;

    if (!(sch->curr_status.pmcw.flags & PMCW_FLAGS_MASK_DNV)) {
        ret = 0;
        goto out;
    }

    if (s->ctrl & SCSW_STCTL_STATUS_PEND) {
        ret = -EINPROGRESS;
        goto out;
    }

    if (s->ctrl &
        (SCSW_FCTL_START_FUNC|SCSW_FCTL_HALT_FUNC|SCSW_FCTL_CLEAR_FUNC)) {
        ret = -EBUSY;
        goto out;
    }

    copy_schib_from_guest(&schib, orig_schib);
    /* Only update the program-modifiable fields. */
    p->intparm = schib.pmcw.intparm;
    p->flags &= ~(PMCW_FLAGS_MASK_ISC | PMCW_FLAGS_MASK_ENA |
                  PMCW_FLAGS_MASK_LM | PMCW_FLAGS_MASK_MME |
                  PMCW_FLAGS_MASK_MP);
    p->flags |= schib.pmcw.flags &
            (PMCW_FLAGS_MASK_ISC | PMCW_FLAGS_MASK_ENA |
             PMCW_FLAGS_MASK_LM | PMCW_FLAGS_MASK_MME |
             PMCW_FLAGS_MASK_MP);
    p->lpm = schib.pmcw.lpm;
    p->mbi = schib.pmcw.mbi;
    p->pom = schib.pmcw.pom;
    p->chars &= ~(PMCW_CHARS_MASK_MBFC | PMCW_CHARS_MASK_CSENSE);
    p->chars |= schib.pmcw.chars &
            (PMCW_CHARS_MASK_MBFC | PMCW_CHARS_MASK_CSENSE);
    sch->curr_status.mba = schib.mba;

    ret = 0;

out:
    return ret;
}

int css_do_xsch(SubchDev *sch)
{
    SCSW *s = &sch->curr_status.scsw;
    PMCW *p = &sch->curr_status.pmcw;
    int ret;

    if (!(p->flags & (PMCW_FLAGS_MASK_DNV | PMCW_FLAGS_MASK_ENA))) {
        ret = -ENODEV;
        goto out;
    }

    if (!(s->ctrl & SCSW_CTRL_MASK_FCTL) ||
        ((s->ctrl & SCSW_CTRL_MASK_FCTL) != SCSW_FCTL_START_FUNC) ||
        (!(s->ctrl &
           (SCSW_ACTL_RESUME_PEND | SCSW_ACTL_START_PEND | SCSW_ACTL_SUSP))) ||
        (s->ctrl & SCSW_ACTL_SUBCH_ACTIVE)) {
        ret = -EINPROGRESS;
        goto out;
    }

    if (s->ctrl & SCSW_CTRL_MASK_STCTL) {
        ret = -EBUSY;
        goto out;
    }

    /* Cancel the current operation. */
    s->ctrl &= ~(SCSW_FCTL_START_FUNC |
                 SCSW_ACTL_RESUME_PEND |
                 SCSW_ACTL_START_PEND |
                 SCSW_ACTL_SUSP);
    sch->channel_prog = 0x0;
    sch->last_cmd_valid = false;
    s->dstat = 0;
    s->cstat = 0;
    ret = 0;

out:
    return ret;
}

int css_do_csch(SubchDev *sch)
{
    SCSW *s = &sch->curr_status.scsw;
    PMCW *p = &sch->curr_status.pmcw;
    int ret;

    if (!(p->flags & (PMCW_FLAGS_MASK_DNV | PMCW_FLAGS_MASK_ENA))) {
        ret = -ENODEV;
        goto out;
    }

    /* Trigger the clear function. */
    s->ctrl &= ~(SCSW_CTRL_MASK_FCTL | SCSW_CTRL_MASK_ACTL);
    s->ctrl |= SCSW_FCTL_CLEAR_FUNC | SCSW_FCTL_CLEAR_FUNC;

    do_subchannel_work(sch, NULL);
    ret = 0;

out:
    return ret;
}

int css_do_hsch(SubchDev *sch)
{
    SCSW *s = &sch->curr_status.scsw;
    PMCW *p = &sch->curr_status.pmcw;
    int ret;

    if (!(p->flags & (PMCW_FLAGS_MASK_DNV | PMCW_FLAGS_MASK_ENA))) {
        ret = -ENODEV;
        goto out;
    }

    if (((s->ctrl & SCSW_CTRL_MASK_STCTL) == SCSW_STCTL_STATUS_PEND) ||
        (s->ctrl & (SCSW_STCTL_PRIMARY |
                    SCSW_STCTL_SECONDARY |
                    SCSW_STCTL_ALERT))) {
        ret = -EINPROGRESS;
        goto out;
    }

    if (s->ctrl & (SCSW_FCTL_HALT_FUNC | SCSW_FCTL_CLEAR_FUNC)) {
        ret = -EBUSY;
        goto out;
    }

    /* Trigger the halt function. */
    s->ctrl |= SCSW_FCTL_HALT_FUNC;
    s->ctrl &= ~SCSW_FCTL_START_FUNC;
    if (((s->ctrl & SCSW_CTRL_MASK_ACTL) ==
         (SCSW_ACTL_SUBCH_ACTIVE | SCSW_ACTL_DEVICE_ACTIVE)) &&
        ((s->ctrl & SCSW_CTRL_MASK_STCTL) == SCSW_STCTL_INTERMEDIATE)) {
        s->ctrl &= ~SCSW_STCTL_STATUS_PEND;
    }
    s->ctrl |= SCSW_ACTL_HALT_PEND;

    do_subchannel_work(sch, NULL);
    ret = 0;

out:
    return ret;
}

static void css_update_chnmon(SubchDev *sch)
{
    if (!(sch->curr_status.pmcw.flags & PMCW_FLAGS_MASK_MME)) {
        /* Not active. */
        return;
    }
    /* The counter is conveniently located at the beginning of the struct. */
    if (sch->curr_status.pmcw.chars & PMCW_CHARS_MASK_MBFC) {
        /* Format 1, per-subchannel area. */
        uint32_t count;

        count = ldl_phys(&address_space_memory, sch->curr_status.mba);
        count++;
        stl_phys(&address_space_memory, sch->curr_status.mba, count);
    } else {
        /* Format 0, global area. */
        uint32_t offset;
        uint16_t count;

        offset = sch->curr_status.pmcw.mbi << 5;
        count = lduw_phys(&address_space_memory,
                          channel_subsys->chnmon_area + offset);
        count++;
        stw_phys(&address_space_memory,
                 channel_subsys->chnmon_area + offset, count);
    }
}

int css_do_ssch(SubchDev *sch, ORB *orb)
{
    SCSW *s = &sch->curr_status.scsw;
    PMCW *p = &sch->curr_status.pmcw;
    int ret;

    if (!(p->flags & (PMCW_FLAGS_MASK_DNV | PMCW_FLAGS_MASK_ENA))) {
        ret = -ENODEV;
        goto out;
    }

    if (s->ctrl & SCSW_STCTL_STATUS_PEND) {
        ret = -EINPROGRESS;
        goto out;
    }

    if (s->ctrl & (SCSW_FCTL_START_FUNC |
                   SCSW_FCTL_HALT_FUNC |
                   SCSW_FCTL_CLEAR_FUNC)) {
        ret = -EBUSY;
        goto out;
    }

    /* If monitoring is active, update counter. */
    if (channel_subsys->chnmon_active) {
        css_update_chnmon(sch);
    }
    sch->channel_prog = orb->cpa;
    /* Trigger the start function. */
    s->ctrl |= (SCSW_FCTL_START_FUNC | SCSW_ACTL_START_PEND);
    s->flags &= ~SCSW_FLAGS_MASK_PNO;

    do_subchannel_work(sch, orb);
    ret = 0;

out:
    return ret;
}

static void copy_irb_to_guest(IRB *dest, const IRB *src, PMCW *pmcw)
{
    int i;
    uint16_t stctl = src->scsw.ctrl & SCSW_CTRL_MASK_STCTL;
    uint16_t actl = src->scsw.ctrl & SCSW_CTRL_MASK_ACTL;

    copy_scsw_to_guest(&dest->scsw, &src->scsw);

    for (i = 0; i < ARRAY_SIZE(dest->esw); i++) {
        dest->esw[i] = cpu_to_be32(src->esw[i]);
    }
    for (i = 0; i < ARRAY_SIZE(dest->ecw); i++) {
        dest->ecw[i] = cpu_to_be32(src->ecw[i]);
    }
    /* extended measurements enabled? */
    if ((src->scsw.flags & SCSW_FLAGS_MASK_ESWF) ||
        !(pmcw->flags & PMCW_FLAGS_MASK_TF) ||
        !(pmcw->chars & PMCW_CHARS_MASK_XMWME)) {
        return;
    }
    /* extended measurements pending? */
    if (!(stctl & SCSW_STCTL_STATUS_PEND)) {
        return;
    }
    if ((stctl & SCSW_STCTL_PRIMARY) ||
        (stctl == SCSW_STCTL_SECONDARY) ||
        ((stctl & SCSW_STCTL_INTERMEDIATE) && (actl & SCSW_ACTL_SUSP))) {
        for (i = 0; i < ARRAY_SIZE(dest->emw); i++) {
            dest->emw[i] = cpu_to_be32(src->emw[i]);
        }
    }
}

int css_do_tsch(SubchDev *sch, IRB *target_irb)
{
    SCSW *s = &sch->curr_status.scsw;
    PMCW *p = &sch->curr_status.pmcw;
    uint16_t stctl;
    uint16_t fctl;
    uint16_t actl;
    IRB irb;
    int ret;

    if (!(p->flags & (PMCW_FLAGS_MASK_DNV | PMCW_FLAGS_MASK_ENA))) {
        ret = 3;
        goto out;
    }

    stctl = s->ctrl & SCSW_CTRL_MASK_STCTL;
    fctl = s->ctrl & SCSW_CTRL_MASK_FCTL;
    actl = s->ctrl & SCSW_CTRL_MASK_ACTL;

    /* Prepare the irb for the guest. */
    memset(&irb, 0, sizeof(IRB));

    /* Copy scsw from current status. */
    memcpy(&irb.scsw, s, sizeof(SCSW));
    if (stctl & SCSW_STCTL_STATUS_PEND) {
        if (s->cstat & (SCSW_CSTAT_DATA_CHECK |
                        SCSW_CSTAT_CHN_CTRL_CHK |
                        SCSW_CSTAT_INTF_CTRL_CHK)) {
            irb.scsw.flags |= SCSW_FLAGS_MASK_ESWF;
            irb.esw[0] = 0x04804000;
        } else {
            irb.esw[0] = 0x00800000;
        }
        /* If a unit check is pending, copy sense data. */
        if ((s->dstat & SCSW_DSTAT_UNIT_CHECK) &&
            (p->chars & PMCW_CHARS_MASK_CSENSE)) {
            irb.scsw.flags |= SCSW_FLAGS_MASK_ESWF | SCSW_FLAGS_MASK_ECTL;
            memcpy(irb.ecw, sch->sense_data, sizeof(sch->sense_data));
            irb.esw[1] = 0x01000000 | (sizeof(sch->sense_data) << 8);
        }
    }
    /* Store the irb to the guest. */
    copy_irb_to_guest(target_irb, &irb, p);

    /* Clear conditions on subchannel, if applicable. */
    if (stctl & SCSW_STCTL_STATUS_PEND) {
        s->ctrl &= ~SCSW_CTRL_MASK_STCTL;
        if ((stctl != (SCSW_STCTL_INTERMEDIATE | SCSW_STCTL_STATUS_PEND)) ||
            ((fctl & SCSW_FCTL_HALT_FUNC) &&
             (actl & SCSW_ACTL_SUSP))) {
            s->ctrl &= ~SCSW_CTRL_MASK_FCTL;
        }
        if (stctl != (SCSW_STCTL_INTERMEDIATE | SCSW_STCTL_STATUS_PEND)) {
            s->flags &= ~SCSW_FLAGS_MASK_PNO;
            s->ctrl &= ~(SCSW_ACTL_RESUME_PEND |
                         SCSW_ACTL_START_PEND |
                         SCSW_ACTL_HALT_PEND |
                         SCSW_ACTL_CLEAR_PEND |
                         SCSW_ACTL_SUSP);
        } else {
            if ((actl & SCSW_ACTL_SUSP) &&
                (fctl & SCSW_FCTL_START_FUNC)) {
                s->flags &= ~SCSW_FLAGS_MASK_PNO;
                if (fctl & SCSW_FCTL_HALT_FUNC) {
                    s->ctrl &= ~(SCSW_ACTL_RESUME_PEND |
                                 SCSW_ACTL_START_PEND |
                                 SCSW_ACTL_HALT_PEND |
                                 SCSW_ACTL_CLEAR_PEND |
                                 SCSW_ACTL_SUSP);
                } else {
                    s->ctrl &= ~SCSW_ACTL_RESUME_PEND;
                }
            }
        }
        /* Clear pending sense data. */
        if (p->chars & PMCW_CHARS_MASK_CSENSE) {
            memset(sch->sense_data, 0 , sizeof(sch->sense_data));
        }
    }

    ret = ((stctl & SCSW_STCTL_STATUS_PEND) == 0);

out:
    return ret;
}

static void copy_crw_to_guest(CRW *dest, const CRW *src)
{
    dest->flags = cpu_to_be16(src->flags);
    dest->rsid = cpu_to_be16(src->rsid);
}

int css_do_stcrw(CRW *crw)
{
    CrwContainer *crw_cont;
    int ret;

    crw_cont = QTAILQ_FIRST(&channel_subsys->pending_crws);
    if (crw_cont) {
        QTAILQ_REMOVE(&channel_subsys->pending_crws, crw_cont, sibling);
        copy_crw_to_guest(crw, &crw_cont->crw);
        g_free(crw_cont);
        ret = 0;
    } else {
        /* List was empty, turn crw machine checks on again. */
        memset(crw, 0, sizeof(*crw));
        channel_subsys->do_crw_mchk = true;
        ret = 1;
    }

    return ret;
}

int css_do_tpi(IOIntCode *int_code, int lowcore)
{
    /* No pending interrupts for !KVM. */
    return 0;
 }

int css_collect_chp_desc(int m, uint8_t cssid, uint8_t f_chpid, uint8_t l_chpid,
                         int rfmt, void *buf)
{
    int i, desc_size;
    uint32_t words[8];
    uint32_t chpid_type_word;
    CssImage *css;

    if (!m && !cssid) {
        css = channel_subsys->css[channel_subsys->default_cssid];
    } else {
        css = channel_subsys->css[cssid];
    }
    if (!css) {
        return 0;
    }
    desc_size = 0;
    for (i = f_chpid; i <= l_chpid; i++) {
        if (css->chpids[i].in_use) {
            chpid_type_word = 0x80000000 | (css->chpids[i].type << 8) | i;
            if (rfmt == 0) {
                words[0] = cpu_to_be32(chpid_type_word);
                words[1] = 0;
                memcpy(buf + desc_size, words, 8);
                desc_size += 8;
            } else if (rfmt == 1) {
                words[0] = cpu_to_be32(chpid_type_word);
                words[1] = 0;
                words[2] = 0;
                words[3] = 0;
                words[4] = 0;
                words[5] = 0;
                words[6] = 0;
                words[7] = 0;
                memcpy(buf + desc_size, words, 32);
                desc_size += 32;
            }
        }
    }
    return desc_size;
}

void css_do_schm(uint8_t mbk, int update, int dct, uint64_t mbo)
{
    /* dct is currently ignored (not really meaningful for our devices) */
    /* TODO: Don't ignore mbk. */
    if (update && !channel_subsys->chnmon_active) {
        /* Enable measuring. */
        channel_subsys->chnmon_area = mbo;
        channel_subsys->chnmon_active = true;
    }
    if (!update && channel_subsys->chnmon_active) {
        /* Disable measuring. */
        channel_subsys->chnmon_area = 0;
        channel_subsys->chnmon_active = false;
    }
}

int css_do_rsch(SubchDev *sch)
{
    SCSW *s = &sch->curr_status.scsw;
    PMCW *p = &sch->curr_status.pmcw;
    int ret;

    if (!(p->flags & (PMCW_FLAGS_MASK_DNV | PMCW_FLAGS_MASK_ENA))) {
        ret = -ENODEV;
        goto out;
    }

    if (s->ctrl & SCSW_STCTL_STATUS_PEND) {
        ret = -EINPROGRESS;
        goto out;
    }

    if (((s->ctrl & SCSW_CTRL_MASK_FCTL) != SCSW_FCTL_START_FUNC) ||
        (s->ctrl & SCSW_ACTL_RESUME_PEND) ||
        (!(s->ctrl & SCSW_ACTL_SUSP))) {
        ret = -EINVAL;
        goto out;
    }

    /* If monitoring is active, update counter. */
    if (channel_subsys->chnmon_active) {
        css_update_chnmon(sch);
    }

    s->ctrl |= SCSW_ACTL_RESUME_PEND;
    do_subchannel_work(sch, NULL);
    ret = 0;

out:
    return ret;
}

int css_do_rchp(uint8_t cssid, uint8_t chpid)
{
    uint8_t real_cssid;

    if (cssid > channel_subsys->max_cssid) {
        return -EINVAL;
    }
    if (channel_subsys->max_cssid == 0) {
        real_cssid = channel_subsys->default_cssid;
    } else {
        real_cssid = cssid;
    }
    if (!channel_subsys->css[real_cssid]) {
        return -EINVAL;
    }

    if (!channel_subsys->css[real_cssid]->chpids[chpid].in_use) {
        return -ENODEV;
    }

    if (!channel_subsys->css[real_cssid]->chpids[chpid].is_virtual) {
        fprintf(stderr,
                "rchp unsupported for non-virtual chpid %x.%02x!\n",
                real_cssid, chpid);
        return -ENODEV;
    }

    /* We don't really use a channel path, so we're done here. */
    css_queue_crw(CRW_RSC_CHP, CRW_ERC_INIT,
                  channel_subsys->max_cssid > 0 ? 1 : 0, chpid);
    if (channel_subsys->max_cssid > 0) {
        css_queue_crw(CRW_RSC_CHP, CRW_ERC_INIT, 0, real_cssid << 8);
    }
    return 0;
}

bool css_schid_final(int m, uint8_t cssid, uint8_t ssid, uint16_t schid)
{
    SubchSet *set;
    uint8_t real_cssid;

    real_cssid = (!m && (cssid == 0)) ? channel_subsys->default_cssid : cssid;
    if (real_cssid > MAX_CSSID || ssid > MAX_SSID ||
        !channel_subsys->css[real_cssid] ||
        !channel_subsys->css[real_cssid]->sch_set[ssid]) {
        return true;
    }
    set = channel_subsys->css[real_cssid]->sch_set[ssid];
    return schid > find_last_bit(set->schids_used,
                                 (MAX_SCHID + 1) / sizeof(unsigned long));
}

static int css_add_virtual_chpid(uint8_t cssid, uint8_t chpid, uint8_t type)
{
    CssImage *css;

    trace_css_chpid_add(cssid, chpid, type);
    if (cssid > MAX_CSSID) {
        return -EINVAL;
    }
    css = channel_subsys->css[cssid];
    if (!css) {
        return -EINVAL;
    }
    if (css->chpids[chpid].in_use) {
        return -EEXIST;
    }
    css->chpids[chpid].in_use = 1;
    css->chpids[chpid].type = type;
    css->chpids[chpid].is_virtual = 1;

    css_generate_chp_crws(cssid, chpid);

    return 0;
}

void css_sch_build_virtual_schib(SubchDev *sch, uint8_t chpid, uint8_t type)
{
    PMCW *p = &sch->curr_status.pmcw;
    SCSW *s = &sch->curr_status.scsw;
    int i;
    CssImage *css = channel_subsys->css[sch->cssid];

    assert(css != NULL);
    memset(p, 0, sizeof(PMCW));
    p->flags |= PMCW_FLAGS_MASK_DNV;
    p->devno = sch->devno;
    /* single path */
    p->pim = 0x80;
    p->pom = 0xff;
    p->pam = 0x80;
    p->chpid[0] = chpid;
    if (!css->chpids[chpid].in_use) {
        css_add_virtual_chpid(sch->cssid, chpid, type);
    }

    memset(s, 0, sizeof(SCSW));
    sch->curr_status.mba = 0;
    for (i = 0; i < ARRAY_SIZE(sch->curr_status.mda); i++) {
        sch->curr_status.mda[i] = 0;
    }
}

SubchDev *css_find_subch(uint8_t m, uint8_t cssid, uint8_t ssid, uint16_t schid)
{
    uint8_t real_cssid;

    real_cssid = (!m && (cssid == 0)) ? channel_subsys->default_cssid : cssid;

    if (!channel_subsys->css[real_cssid]) {
        return NULL;
    }

    if (!channel_subsys->css[real_cssid]->sch_set[ssid]) {
        return NULL;
    }

    return channel_subsys->css[real_cssid]->sch_set[ssid]->sch[schid];
}

bool css_subch_visible(SubchDev *sch)
{
    if (sch->ssid > channel_subsys->max_ssid) {
        return false;
    }

    if (sch->cssid != channel_subsys->default_cssid) {
        return (channel_subsys->max_cssid > 0);
    }

    return true;
}

bool css_present(uint8_t cssid)
{
    return (channel_subsys->css[cssid] != NULL);
}

bool css_devno_used(uint8_t cssid, uint8_t ssid, uint16_t devno)
{
    if (!channel_subsys->css[cssid]) {
        return false;
    }
    if (!channel_subsys->css[cssid]->sch_set[ssid]) {
        return false;
    }

    return !!test_bit(devno,
                      channel_subsys->css[cssid]->sch_set[ssid]->devnos_used);
}

void css_subch_assign(uint8_t cssid, uint8_t ssid, uint16_t schid,
                      uint16_t devno, SubchDev *sch)
{
    CssImage *css;
    SubchSet *s_set;

    trace_css_assign_subch(sch ? "assign" : "deassign", cssid, ssid, schid,
                           devno);
    if (!channel_subsys->css[cssid]) {
        fprintf(stderr,
                "Suspicious call to %s (%x.%x.%04x) for non-existing css!\n",
                __func__, cssid, ssid, schid);
        return;
    }
    css = channel_subsys->css[cssid];

    if (!css->sch_set[ssid]) {
        css->sch_set[ssid] = g_malloc0(sizeof(SubchSet));
    }
    s_set = css->sch_set[ssid];

    s_set->sch[schid] = sch;
    if (sch) {
        set_bit(schid, s_set->schids_used);
        set_bit(devno, s_set->devnos_used);
    } else {
        clear_bit(schid, s_set->schids_used);
        clear_bit(devno, s_set->devnos_used);
    }
}

void css_queue_crw(uint8_t rsc, uint8_t erc, int chain, uint16_t rsid)
{
    CrwContainer *crw_cont;

    trace_css_crw(rsc, erc, rsid, chain ? "(chained)" : "");
    /* TODO: Maybe use a static crw pool? */
    crw_cont = g_try_malloc0(sizeof(CrwContainer));
    if (!crw_cont) {
        channel_subsys->crws_lost = true;
        return;
    }
    crw_cont->crw.flags = (rsc << 8) | erc;
    if (chain) {
        crw_cont->crw.flags |= CRW_FLAGS_MASK_C;
    }
    crw_cont->crw.rsid = rsid;
    if (channel_subsys->crws_lost) {
        crw_cont->crw.flags |= CRW_FLAGS_MASK_R;
        channel_subsys->crws_lost = false;
    }

    QTAILQ_INSERT_TAIL(&channel_subsys->pending_crws, crw_cont, sibling);

    if (channel_subsys->do_crw_mchk) {
        S390CPU *cpu = s390_cpu_addr2state(0);

        channel_subsys->do_crw_mchk = false;
        /* Inject crw pending machine check. */
        s390_crw_mchk(cpu);
    }
}

void css_generate_sch_crws(uint8_t cssid, uint8_t ssid, uint16_t schid,
                           int hotplugged, int add)
{
    uint8_t guest_cssid;
    bool chain_crw;

    if (add && !hotplugged) {
        return;
    }
    if (channel_subsys->max_cssid == 0) {
        /* Default cssid shows up as 0. */
        guest_cssid = (cssid == channel_subsys->default_cssid) ? 0 : cssid;
    } else {
        /* Show real cssid to the guest. */
        guest_cssid = cssid;
    }
    /*
     * Only notify for higher subchannel sets/channel subsystems if the
     * guest has enabled it.
     */
    if ((ssid > channel_subsys->max_ssid) ||
        (guest_cssid > channel_subsys->max_cssid) ||
        ((channel_subsys->max_cssid == 0) &&
         (cssid != channel_subsys->default_cssid))) {
        return;
    }
    chain_crw = (channel_subsys->max_ssid > 0) ||
            (channel_subsys->max_cssid > 0);
    css_queue_crw(CRW_RSC_SUBCH, CRW_ERC_IPI, chain_crw ? 1 : 0, schid);
    if (chain_crw) {
        css_queue_crw(CRW_RSC_SUBCH, CRW_ERC_IPI, 0,
                      (guest_cssid << 8) | (ssid << 4));
    }
}

void css_generate_chp_crws(uint8_t cssid, uint8_t chpid)
{
    /* TODO */
}

int css_enable_mcsse(void)
{
    trace_css_enable_facility("mcsse");
    channel_subsys->max_cssid = MAX_CSSID;
    return 0;
}

int css_enable_mss(void)
{
    trace_css_enable_facility("mss");
    channel_subsys->max_ssid = MAX_SSID;
    return 0;
}

void subch_device_save(SubchDev *s, QEMUFile *f)
{
    int i;

    qemu_put_byte(f, s->cssid);
    qemu_put_byte(f, s->ssid);
    qemu_put_be16(f, s->schid);
    qemu_put_be16(f, s->devno);
    qemu_put_byte(f, s->thinint_active);
    /* SCHIB */
    /*     PMCW */
    qemu_put_be32(f, s->curr_status.pmcw.intparm);
    qemu_put_be16(f, s->curr_status.pmcw.flags);
    qemu_put_be16(f, s->curr_status.pmcw.devno);
    qemu_put_byte(f, s->curr_status.pmcw.lpm);
    qemu_put_byte(f, s->curr_status.pmcw.pnom);
    qemu_put_byte(f, s->curr_status.pmcw.lpum);
    qemu_put_byte(f, s->curr_status.pmcw.pim);
    qemu_put_be16(f, s->curr_status.pmcw.mbi);
    qemu_put_byte(f, s->curr_status.pmcw.pom);
    qemu_put_byte(f, s->curr_status.pmcw.pam);
    qemu_put_buffer(f, s->curr_status.pmcw.chpid, 8);
    qemu_put_be32(f, s->curr_status.pmcw.chars);
    /*     SCSW */
    qemu_put_be16(f, s->curr_status.scsw.flags);
    qemu_put_be16(f, s->curr_status.scsw.ctrl);
    qemu_put_be32(f, s->curr_status.scsw.cpa);
    qemu_put_byte(f, s->curr_status.scsw.dstat);
    qemu_put_byte(f, s->curr_status.scsw.cstat);
    qemu_put_be16(f, s->curr_status.scsw.count);
    qemu_put_be64(f, s->curr_status.mba);
    qemu_put_buffer(f, s->curr_status.mda, 4);
    /* end SCHIB */
    qemu_put_buffer(f, s->sense_data, 32);
    qemu_put_be64(f, s->channel_prog);
    /* last cmd */
    qemu_put_byte(f, s->last_cmd.cmd_code);
    qemu_put_byte(f, s->last_cmd.flags);
    qemu_put_be16(f, s->last_cmd.count);
    qemu_put_be32(f, s->last_cmd.cda);
    qemu_put_byte(f, s->last_cmd_valid);
    qemu_put_byte(f, s->id.reserved);
    qemu_put_be16(f, s->id.cu_type);
    qemu_put_byte(f, s->id.cu_model);
    qemu_put_be16(f, s->id.dev_type);
    qemu_put_byte(f, s->id.dev_model);
    qemu_put_byte(f, s->id.unused);
    for (i = 0; i < ARRAY_SIZE(s->id.ciw); i++) {
        qemu_put_byte(f, s->id.ciw[i].type);
        qemu_put_byte(f, s->id.ciw[i].command);
        qemu_put_be16(f, s->id.ciw[i].count);
    }
    return;
}

int subch_device_load(SubchDev *s, QEMUFile *f)
{
    int i;

    s->cssid = qemu_get_byte(f);
    s->ssid = qemu_get_byte(f);
    s->schid = qemu_get_be16(f);
    s->devno = qemu_get_be16(f);
    s->thinint_active = qemu_get_byte(f);
    /* SCHIB */
    /*     PMCW */
    s->curr_status.pmcw.intparm = qemu_get_be32(f);
    s->curr_status.pmcw.flags = qemu_get_be16(f);
    s->curr_status.pmcw.devno = qemu_get_be16(f);
    s->curr_status.pmcw.lpm = qemu_get_byte(f);
    s->curr_status.pmcw.pnom  = qemu_get_byte(f);
    s->curr_status.pmcw.lpum = qemu_get_byte(f);
    s->curr_status.pmcw.pim = qemu_get_byte(f);
    s->curr_status.pmcw.mbi = qemu_get_be16(f);
    s->curr_status.pmcw.pom = qemu_get_byte(f);
    s->curr_status.pmcw.pam = qemu_get_byte(f);
    qemu_get_buffer(f, s->curr_status.pmcw.chpid, 8);
    s->curr_status.pmcw.chars = qemu_get_be32(f);
    /*     SCSW */
    s->curr_status.scsw.flags = qemu_get_be16(f);
    s->curr_status.scsw.ctrl = qemu_get_be16(f);
    s->curr_status.scsw.cpa = qemu_get_be32(f);
    s->curr_status.scsw.dstat = qemu_get_byte(f);
    s->curr_status.scsw.cstat = qemu_get_byte(f);
    s->curr_status.scsw.count = qemu_get_be16(f);
    s->curr_status.mba = qemu_get_be64(f);
    qemu_get_buffer(f, s->curr_status.mda, 4);
    /* end SCHIB */
    qemu_get_buffer(f, s->sense_data, 32);
    s->channel_prog = qemu_get_be64(f);
    /* last cmd */
    s->last_cmd.cmd_code = qemu_get_byte(f);
    s->last_cmd.flags = qemu_get_byte(f);
    s->last_cmd.count = qemu_get_be16(f);
    s->last_cmd.cda = qemu_get_be32(f);
    s->last_cmd_valid = qemu_get_byte(f);
    s->id.reserved = qemu_get_byte(f);
    s->id.cu_type = qemu_get_be16(f);
    s->id.cu_model = qemu_get_byte(f);
    s->id.dev_type = qemu_get_be16(f);
    s->id.dev_model = qemu_get_byte(f);
    s->id.unused = qemu_get_byte(f);
    for (i = 0; i < ARRAY_SIZE(s->id.ciw); i++) {
        s->id.ciw[i].type = qemu_get_byte(f);
        s->id.ciw[i].command = qemu_get_byte(f);
        s->id.ciw[i].count = qemu_get_be16(f);
    }
    return 0;
}


static void css_init(void)
{
    channel_subsys = g_malloc0(sizeof(*channel_subsys));
    QTAILQ_INIT(&channel_subsys->pending_crws);
    channel_subsys->do_crw_mchk = true;
    channel_subsys->crws_lost = false;
    channel_subsys->chnmon_active = false;
    QTAILQ_INIT(&channel_subsys->io_adapters);
}
machine_init(css_init);

void css_reset_sch(SubchDev *sch)
{
    PMCW *p = &sch->curr_status.pmcw;

    p->intparm = 0;
    p->flags &= ~(PMCW_FLAGS_MASK_ISC | PMCW_FLAGS_MASK_ENA |
                  PMCW_FLAGS_MASK_LM | PMCW_FLAGS_MASK_MME |
                  PMCW_FLAGS_MASK_MP | PMCW_FLAGS_MASK_TF);
    p->flags |= PMCW_FLAGS_MASK_DNV;
    p->devno = sch->devno;
    p->pim = 0x80;
    p->lpm = p->pim;
    p->pnom = 0;
    p->lpum = 0;
    p->mbi = 0;
    p->pom = 0xff;
    p->pam = 0x80;
    p->chars &= ~(PMCW_CHARS_MASK_MBFC | PMCW_CHARS_MASK_XMWME |
                  PMCW_CHARS_MASK_CSENSE);

    memset(&sch->curr_status.scsw, 0, sizeof(sch->curr_status.scsw));
    sch->curr_status.mba = 0;

    sch->channel_prog = 0x0;
    sch->last_cmd_valid = false;
    sch->thinint_active = false;
}

void css_reset(void)
{
    CrwContainer *crw_cont;

    /* Clean up monitoring. */
    channel_subsys->chnmon_active = false;
    channel_subsys->chnmon_area = 0;

    /* Clear pending CRWs. */
    while ((crw_cont = QTAILQ_FIRST(&channel_subsys->pending_crws))) {
        QTAILQ_REMOVE(&channel_subsys->pending_crws, crw_cont, sibling);
        g_free(crw_cont);
    }
    channel_subsys->do_crw_mchk = true;
    channel_subsys->crws_lost = false;

    /* Reset maximum ids. */
    channel_subsys->max_cssid = 0;
    channel_subsys->max_ssid = 0;
}
