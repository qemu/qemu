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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/bitops.h"
#include "qemu/error-report.h"
#include "exec/address-spaces.h"
#include "hw/s390x/ioinst.h"
#include "hw/qdev-properties.h"
#include "hw/s390x/css.h"
#include "trace.h"
#include "hw/s390x/s390_flic.h"
#include "hw/s390x/s390-virtio-ccw.h"
#include "hw/s390x/s390-ccw.h"

typedef struct CrwContainer {
    CRW crw;
    QTAILQ_ENTRY(CrwContainer) sibling;
} CrwContainer;

static const VMStateDescription vmstate_crw = {
    .name = "s390_crw",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT16(flags, CRW),
        VMSTATE_UINT16(rsid, CRW),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_crw_container = {
    .name = "s390_crw_container",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(crw, CrwContainer, 0, vmstate_crw, CRW),
        VMSTATE_END_OF_LIST()
    },
};

typedef struct ChpInfo {
    uint8_t in_use;
    uint8_t type;
    uint8_t is_virtual;
} ChpInfo;

static const VMStateDescription vmstate_chp_info = {
    .name = "s390_chp_info",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(in_use, ChpInfo),
        VMSTATE_UINT8(type, ChpInfo),
        VMSTATE_UINT8(is_virtual, ChpInfo),
        VMSTATE_END_OF_LIST()
    }
};

typedef struct SubchSet {
    SubchDev *sch[MAX_SCHID + 1];
    unsigned long schids_used[BITS_TO_LONGS(MAX_SCHID + 1)];
    unsigned long devnos_used[BITS_TO_LONGS(MAX_SCHID + 1)];
} SubchSet;

static const VMStateDescription vmstate_scsw = {
    .name = "s390_scsw",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT16(flags, SCSW),
        VMSTATE_UINT16(ctrl, SCSW),
        VMSTATE_UINT32(cpa, SCSW),
        VMSTATE_UINT8(dstat, SCSW),
        VMSTATE_UINT8(cstat, SCSW),
        VMSTATE_UINT16(count, SCSW),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_pmcw = {
    .name = "s390_pmcw",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(intparm, PMCW),
        VMSTATE_UINT16(flags, PMCW),
        VMSTATE_UINT16(devno, PMCW),
        VMSTATE_UINT8(lpm, PMCW),
        VMSTATE_UINT8(pnom, PMCW),
        VMSTATE_UINT8(lpum, PMCW),
        VMSTATE_UINT8(pim, PMCW),
        VMSTATE_UINT16(mbi, PMCW),
        VMSTATE_UINT8(pom, PMCW),
        VMSTATE_UINT8(pam, PMCW),
        VMSTATE_UINT8_ARRAY(chpid, PMCW, 8),
        VMSTATE_UINT32(chars, PMCW),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_schib = {
    .name = "s390_schib",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(pmcw, SCHIB, 0, vmstate_pmcw, PMCW),
        VMSTATE_STRUCT(scsw, SCHIB, 0, vmstate_scsw, SCSW),
        VMSTATE_UINT64(mba, SCHIB),
        VMSTATE_UINT8_ARRAY(mda, SCHIB, 4),
        VMSTATE_END_OF_LIST()
    }
};


static const VMStateDescription vmstate_ccw1 = {
    .name = "s390_ccw1",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(cmd_code, CCW1),
        VMSTATE_UINT8(flags, CCW1),
        VMSTATE_UINT16(count, CCW1),
        VMSTATE_UINT32(cda, CCW1),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_ciw = {
    .name = "s390_ciw",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(type, CIW),
        VMSTATE_UINT8(command, CIW),
        VMSTATE_UINT16(count, CIW),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_sense_id = {
    .name = "s390_sense_id",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(reserved, SenseId),
        VMSTATE_UINT16(cu_type, SenseId),
        VMSTATE_UINT8(cu_model, SenseId),
        VMSTATE_UINT16(dev_type, SenseId),
        VMSTATE_UINT8(dev_model, SenseId),
        VMSTATE_UINT8(unused, SenseId),
        VMSTATE_STRUCT_ARRAY(ciw, SenseId, MAX_CIWS, 0, vmstate_ciw, CIW),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_orb = {
    .name = "s390_orb",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(intparm, ORB),
        VMSTATE_UINT16(ctrl0, ORB),
        VMSTATE_UINT8(lpm, ORB),
        VMSTATE_UINT8(ctrl1, ORB),
        VMSTATE_UINT32(cpa, ORB),
        VMSTATE_END_OF_LIST()
    }
};

static bool vmstate_schdev_orb_needed(void *opaque)
{
    return css_migration_enabled();
}

static const VMStateDescription vmstate_schdev_orb = {
    .name = "s390_subch_dev/orb",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = vmstate_schdev_orb_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(orb, SubchDev, 1, vmstate_orb, ORB),
        VMSTATE_END_OF_LIST()
    }
};

static int subch_dev_post_load(void *opaque, int version_id);
static int subch_dev_pre_save(void *opaque);

const char err_hint_devno[] = "Devno mismatch, tried to load wrong section!"
    " Likely reason: some sequences of plug and unplug  can break"
    " migration for machine versions prior to  2.7 (known design flaw).";

const VMStateDescription vmstate_subch_dev = {
    .name = "s390_subch_dev",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = subch_dev_post_load,
    .pre_save = subch_dev_pre_save,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_EQUAL(cssid, SubchDev, "Bug!"),
        VMSTATE_UINT8_EQUAL(ssid, SubchDev, "Bug!"),
        VMSTATE_UINT16(migrated_schid, SubchDev),
        VMSTATE_UINT16_EQUAL(devno, SubchDev, err_hint_devno),
        VMSTATE_BOOL(thinint_active, SubchDev),
        VMSTATE_STRUCT(curr_status, SubchDev, 0, vmstate_schib, SCHIB),
        VMSTATE_UINT8_ARRAY(sense_data, SubchDev, 32),
        VMSTATE_UINT64(channel_prog, SubchDev),
        VMSTATE_STRUCT(last_cmd, SubchDev, 0, vmstate_ccw1, CCW1),
        VMSTATE_BOOL(last_cmd_valid, SubchDev),
        VMSTATE_STRUCT(id, SubchDev, 0, vmstate_sense_id, SenseId),
        VMSTATE_BOOL(ccw_fmt_1, SubchDev),
        VMSTATE_UINT8(ccw_no_data_cnt, SubchDev),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_schdev_orb,
        NULL
    }
};

typedef struct IndAddrPtrTmp {
    IndAddr **parent;
    uint64_t addr;
    int32_t len;
} IndAddrPtrTmp;

static int post_load_ind_addr(void *opaque, int version_id)
{
    IndAddrPtrTmp *ptmp = opaque;
    IndAddr **ind_addr = ptmp->parent;

    if (ptmp->len != 0) {
        *ind_addr = get_indicator(ptmp->addr, ptmp->len);
    } else {
        *ind_addr = NULL;
    }
    return 0;
}

static int pre_save_ind_addr(void *opaque)
{
    IndAddrPtrTmp *ptmp = opaque;
    IndAddr *ind_addr = *(ptmp->parent);

    if (ind_addr != NULL) {
        ptmp->len = ind_addr->len;
        ptmp->addr = ind_addr->addr;
    } else {
        ptmp->len = 0;
        ptmp->addr = 0L;
    }

    return 0;
}

static const VMStateDescription vmstate_ind_addr_tmp = {
    .name = "s390_ind_addr_tmp",
    .pre_save = pre_save_ind_addr,
    .post_load = post_load_ind_addr,

    .fields = (const VMStateField[]) {
        VMSTATE_INT32(len, IndAddrPtrTmp),
        VMSTATE_UINT64(addr, IndAddrPtrTmp),
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_ind_addr = {
    .name = "s390_ind_addr_tmp",
    .fields = (const VMStateField[]) {
        VMSTATE_WITH_TMP(IndAddr*, IndAddrPtrTmp, vmstate_ind_addr_tmp),
        VMSTATE_END_OF_LIST()
    }
};

typedef struct CssImage {
    SubchSet *sch_set[MAX_SSID + 1];
    ChpInfo chpids[MAX_CHPID + 1];
} CssImage;

static const VMStateDescription vmstate_css_img = {
    .name = "s390_css_img",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        /* Subchannel sets have no relevant state. */
        VMSTATE_STRUCT_ARRAY(chpids, CssImage, MAX_CHPID + 1, 0,
                             vmstate_chp_info, ChpInfo),
        VMSTATE_END_OF_LIST()
    }

};

typedef struct IoAdapter {
    uint32_t id;
    uint8_t type;
    uint8_t isc;
    uint8_t flags;
} IoAdapter;

typedef struct ChannelSubSys {
    QTAILQ_HEAD(, CrwContainer) pending_crws;
    bool sei_pending;
    bool do_crw_mchk;
    bool crws_lost;
    uint8_t max_cssid;
    uint8_t max_ssid;
    bool chnmon_active;
    uint64_t chnmon_area;
    CssImage *css[MAX_CSSID + 1];
    uint8_t default_cssid;
    /* don't migrate, see css_register_io_adapters */
    IoAdapter *io_adapters[CSS_IO_ADAPTER_TYPE_NUMS][MAX_ISC + 1];
    /* don't migrate, see get_indicator and IndAddrPtrTmp */
    QTAILQ_HEAD(, IndAddr) indicator_addresses;
} ChannelSubSys;

static const VMStateDescription vmstate_css = {
    .name = "s390_css",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_QTAILQ_V(pending_crws, ChannelSubSys, 1, vmstate_crw_container,
                         CrwContainer, sibling),
        VMSTATE_BOOL(sei_pending, ChannelSubSys),
        VMSTATE_BOOL(do_crw_mchk, ChannelSubSys),
        VMSTATE_BOOL(crws_lost, ChannelSubSys),
        /* These were kind of migrated by virtio */
        VMSTATE_UINT8(max_cssid, ChannelSubSys),
        VMSTATE_UINT8(max_ssid, ChannelSubSys),
        VMSTATE_BOOL(chnmon_active, ChannelSubSys),
        VMSTATE_UINT64(chnmon_area, ChannelSubSys),
        VMSTATE_ARRAY_OF_POINTER_TO_STRUCT(css, ChannelSubSys, MAX_CSSID + 1,
                0, vmstate_css_img, CssImage),
        VMSTATE_UINT8(default_cssid, ChannelSubSys),
        VMSTATE_END_OF_LIST()
    }
};

static ChannelSubSys channel_subsys = {
    .pending_crws = QTAILQ_HEAD_INITIALIZER(channel_subsys.pending_crws),
    .do_crw_mchk = true,
    .sei_pending = false,
    .crws_lost = false,
    .chnmon_active = false,
    .indicator_addresses =
        QTAILQ_HEAD_INITIALIZER(channel_subsys.indicator_addresses),
};

static int subch_dev_pre_save(void *opaque)
{
    SubchDev *s = opaque;

    /* Prepare remote_schid for save */
    s->migrated_schid = s->schid;

    return 0;
}

static int subch_dev_post_load(void *opaque, int version_id)
{

    SubchDev *s = opaque;

    /* Re-assign the subchannel to remote_schid if necessary */
    if (s->migrated_schid != s->schid) {
        if (css_find_subch(true, s->cssid, s->ssid, s->schid) == s) {
            /*
             * Cleanup the slot before moving to s->migrated_schid provided
             * it still belongs to us, i.e. it was not changed by previous
             * invocation of this function.
             */
            css_subch_assign(s->cssid, s->ssid, s->schid, s->devno, NULL);
        }
        /* It's OK to re-assign without a prior de-assign. */
        s->schid = s->migrated_schid;
        css_subch_assign(s->cssid, s->ssid, s->schid, s->devno, s);
    }

    if (css_migration_enabled()) {
        /* No compat voodoo to do ;) */
        return 0;
    }
    /*
     * Hack alert. If we don't migrate the channel subsystem status
     * we still need to find out if the guest enabled mss/mcss-e.
     * If the subchannel is enabled, it certainly was able to access it,
     * so adjust the max_ssid/max_cssid values for relevant ssid/cssid
     * values. This is not watertight, but better than nothing.
     */
    if (s->curr_status.pmcw.flags & PMCW_FLAGS_MASK_ENA) {
        if (s->ssid) {
            channel_subsys.max_ssid = MAX_SSID;
        }
        if (s->cssid != channel_subsys.default_cssid) {
            channel_subsys.max_cssid = MAX_CSSID;
        }
    }
    return 0;
}

void css_register_vmstate(void)
{
    vmstate_register(NULL, 0, &vmstate_css, &channel_subsys);
}

IndAddr *get_indicator(hwaddr ind_addr, int len)
{
    IndAddr *indicator;

    QTAILQ_FOREACH(indicator, &channel_subsys.indicator_addresses, sibling) {
        if (indicator->addr == ind_addr) {
            indicator->refcnt++;
            return indicator;
        }
    }
    indicator = g_new0(IndAddr, 1);
    indicator->addr = ind_addr;
    indicator->len = len;
    indicator->refcnt = 1;
    QTAILQ_INSERT_TAIL(&channel_subsys.indicator_addresses,
                       indicator, sibling);
    return indicator;
}

static int s390_io_adapter_map(AdapterInfo *adapter, uint64_t map_addr,
                               bool do_map)
{
    S390FLICState *fs = s390_get_flic();
    S390FLICStateClass *fsc = s390_get_flic_class(fs);

    return fsc->io_adapter_map(fs, adapter->adapter_id, map_addr, do_map);
}

void release_indicator(AdapterInfo *adapter, IndAddr *indicator)
{
    assert(indicator->refcnt > 0);
    indicator->refcnt--;
    if (indicator->refcnt > 0) {
        return;
    }
    QTAILQ_REMOVE(&channel_subsys.indicator_addresses, indicator, sibling);
    if (indicator->map) {
        s390_io_adapter_map(adapter, indicator->map, false);
    }
    g_free(indicator);
}

int map_indicator(AdapterInfo *adapter, IndAddr *indicator)
{
    int ret;

    if (indicator->map) {
        return 0; /* already mapped is not an error */
    }
    indicator->map = indicator->addr;
    ret = s390_io_adapter_map(adapter, indicator->map, true);
    if ((ret != 0) && (ret != -ENOSYS)) {
        goto out_err;
    }
    return 0;

out_err:
    indicator->map = 0;
    return ret;
}

int css_create_css_image(uint8_t cssid, bool default_image)
{
    trace_css_new_image(cssid, default_image ? "(default)" : "");
    /* 255 is reserved */
    if (cssid == 255) {
        return -EINVAL;
    }
    if (channel_subsys.css[cssid]) {
        return -EBUSY;
    }
    channel_subsys.css[cssid] = g_new0(CssImage, 1);
    if (default_image) {
        channel_subsys.default_cssid = cssid;
    }
    return 0;
}

uint32_t css_get_adapter_id(CssIoAdapterType type, uint8_t isc)
{
    if (type >= CSS_IO_ADAPTER_TYPE_NUMS || isc > MAX_ISC ||
        !channel_subsys.io_adapters[type][isc]) {
        return -1;
    }

    return channel_subsys.io_adapters[type][isc]->id;
}

/**
 * css_register_io_adapters: Register I/O adapters per ISC during init
 *
 * @swap: an indication if byte swap is needed.
 * @maskable: an indication if the adapter is subject to the mask operation.
 * @flags: further characteristics of the adapter.
 *         e.g. suppressible, an indication if the adapter is subject to AIS.
 * @errp: location to store error information.
 */
void css_register_io_adapters(CssIoAdapterType type, bool swap, bool maskable,
                              uint8_t flags, Error **errp)
{
    uint32_t id;
    int ret, isc;
    IoAdapter *adapter;
    S390FLICState *fs = s390_get_flic();
    S390FLICStateClass *fsc = s390_get_flic_class(fs);

    /*
     * Disallow multiple registrations for the same device type.
     * Report an error if registering for an already registered type.
     */
    if (channel_subsys.io_adapters[type][0]) {
        error_setg(errp, "Adapters for type %d already registered", type);
    }

    for (isc = 0; isc <= MAX_ISC; isc++) {
        id = (type << 3) | isc;
        ret = fsc->register_io_adapter(fs, id, isc, swap, maskable, flags);
        if (ret == 0) {
            adapter = g_new0(IoAdapter, 1);
            adapter->id = id;
            adapter->isc = isc;
            adapter->type = type;
            adapter->flags = flags;
            channel_subsys.io_adapters[type][isc] = adapter;
        } else {
            error_setg_errno(errp, -ret, "Unexpected error %d when "
                             "registering adapter %d", ret, id);
            break;
        }
    }

    /*
     * No need to free registered adapters in kvm: kvm will clean up
     * when the machine goes away.
     */
    if (ret) {
        for (isc--; isc >= 0; isc--) {
            g_free(channel_subsys.io_adapters[type][isc]);
            channel_subsys.io_adapters[type][isc] = NULL;
        }
    }

}

static void css_clear_io_interrupt(uint16_t subchannel_id,
                                   uint16_t subchannel_nr)
{
    Error *err = NULL;
    static bool no_clear_irq;
    S390FLICState *fs = s390_get_flic();
    S390FLICStateClass *fsc = s390_get_flic_class(fs);
    int r;

    if (unlikely(no_clear_irq)) {
        return;
    }
    r = fsc->clear_io_irq(fs, subchannel_id, subchannel_nr);
    switch (r) {
    case 0:
        break;
    case -ENOSYS:
        no_clear_irq = true;
        /*
        * Ignore unavailability, as the user can't do anything
        * about it anyway.
        */
        break;
    default:
        error_setg_errno(&err, -r, "unexpected error condition");
        error_propagate(&error_abort, err);
    }
}

static inline uint16_t css_do_build_subchannel_id(uint8_t cssid, uint8_t ssid)
{
    if (channel_subsys.max_cssid > 0) {
        return (cssid << 8) | (1 << 3) | (ssid << 1) | 1;
    }
    return (ssid << 1) | 1;
}

uint16_t css_build_subchannel_id(SubchDev *sch)
{
    return css_do_build_subchannel_id(sch->cssid, sch->ssid);
}

void css_inject_io_interrupt(SubchDev *sch)
{
    uint8_t isc = (sch->curr_status.pmcw.flags & PMCW_FLAGS_MASK_ISC) >> 11;

    trace_css_io_interrupt(sch->cssid, sch->ssid, sch->schid,
                           sch->curr_status.pmcw.intparm, isc, "");
    s390_io_interrupt(css_build_subchannel_id(sch),
                      sch->schid,
                      sch->curr_status.pmcw.intparm,
                      isc << 27);
}

void css_conditional_io_interrupt(SubchDev *sch)
{
    /*
     * If the subchannel is not enabled, it is not made status pending
     * (see PoP p. 16-17, "Status Control").
     */
    if (!(sch->curr_status.pmcw.flags & PMCW_FLAGS_MASK_ENA)) {
        return;
    }

    /*
     * If the subchannel is not currently status pending, make it pending
     * with alert status.
     */
    if (!(sch->curr_status.scsw.ctrl & SCSW_STCTL_STATUS_PEND)) {
        uint8_t isc = (sch->curr_status.pmcw.flags & PMCW_FLAGS_MASK_ISC) >> 11;

        trace_css_io_interrupt(sch->cssid, sch->ssid, sch->schid,
                               sch->curr_status.pmcw.intparm, isc,
                               "(unsolicited)");
        sch->curr_status.scsw.ctrl &= ~SCSW_CTRL_MASK_STCTL;
        sch->curr_status.scsw.ctrl |=
            SCSW_STCTL_ALERT | SCSW_STCTL_STATUS_PEND;
        /* Inject an I/O interrupt. */
        s390_io_interrupt(css_build_subchannel_id(sch),
                          sch->schid,
                          sch->curr_status.pmcw.intparm,
                          isc << 27);
    }
}

int css_do_sic(S390CPU *cpu, uint8_t isc, uint16_t mode)
{
    CPUS390XState *env = &cpu->env;
    S390FLICState *fs = s390_get_flic();
    S390FLICStateClass *fsc = s390_get_flic_class(fs);
    int r;

    if (env->psw.mask & PSW_MASK_PSTATE) {
        r = -PGM_PRIVILEGED;
        goto out;
    }

    trace_css_do_sic(mode, isc);
    switch (mode) {
    case SIC_IRQ_MODE_ALL:
    case SIC_IRQ_MODE_SINGLE:
        break;
    default:
        r = -PGM_OPERAND;
        goto out;
    }

    r = fsc->modify_ais_mode(fs, isc, mode) ? -PGM_OPERATION : 0;
out:
    return r;
}

void css_adapter_interrupt(CssIoAdapterType type, uint8_t isc)
{
    S390FLICState *fs = s390_get_flic();
    S390FLICStateClass *fsc = s390_get_flic_class(fs);
    uint32_t io_int_word = (isc << 27) | IO_INT_WORD_AI;
    IoAdapter *adapter = channel_subsys.io_adapters[type][isc];

    if (!adapter) {
        return;
    }

    trace_css_adapter_interrupt(isc);
    if (fs->ais_supported) {
        if (fsc->inject_airq(fs, type, isc, adapter->flags)) {
            error_report("Failed to inject airq with AIS supported");
            exit(1);
        }
    } else {
        s390_io_interrupt(0, 0, 0, io_int_word);
    }
}

static void sch_handle_clear_func(SubchDev *sch)
{
    SCHIB *schib = &sch->curr_status;
    int path;

    /* Path management: In our simple css, we always choose the only path. */
    path = 0x80;

    /* Reset values prior to 'issuing the clear signal'. */
    schib->pmcw.lpum = 0;
    schib->pmcw.pom = 0xff;
    schib->scsw.flags &= ~SCSW_FLAGS_MASK_PNO;

    /* We always 'attempt to issue the clear signal', and we always succeed. */
    sch->channel_prog = 0x0;
    sch->last_cmd_valid = false;
    schib->scsw.ctrl &= ~SCSW_ACTL_CLEAR_PEND;
    schib->scsw.ctrl |= SCSW_STCTL_STATUS_PEND;

    schib->scsw.dstat = 0;
    schib->scsw.cstat = 0;
    schib->pmcw.lpum = path;

}

static void sch_handle_halt_func(SubchDev *sch)
{
    SCHIB *schib = &sch->curr_status;
    hwaddr curr_ccw = sch->channel_prog;
    int path;

    /* Path management: In our simple css, we always choose the only path. */
    path = 0x80;

    /* We always 'attempt to issue the halt signal', and we always succeed. */
    sch->channel_prog = 0x0;
    sch->last_cmd_valid = false;
    schib->scsw.ctrl &= ~SCSW_ACTL_HALT_PEND;
    schib->scsw.ctrl |= SCSW_STCTL_STATUS_PEND;

    if ((schib->scsw.ctrl & (SCSW_ACTL_SUBCH_ACTIVE |
                             SCSW_ACTL_DEVICE_ACTIVE)) ||
        !((schib->scsw.ctrl & SCSW_ACTL_START_PEND) ||
          (schib->scsw.ctrl & SCSW_ACTL_SUSP))) {
        schib->scsw.dstat = SCSW_DSTAT_DEVICE_END;
    }
    if ((schib->scsw.ctrl & (SCSW_ACTL_SUBCH_ACTIVE |
                             SCSW_ACTL_DEVICE_ACTIVE)) ||
        (schib->scsw.ctrl & SCSW_ACTL_SUSP)) {
        schib->scsw.cpa = curr_ccw + 8;
    }
    schib->scsw.cstat = 0;
    schib->pmcw.lpum = path;

}

/*
 * As the SenseId struct cannot be packed (would cause unaligned accesses), we
 * have to copy the individual fields to an unstructured area using the correct
 * layout (see SA22-7204-01 "Common I/O-Device Commands").
 */
static void copy_sense_id_to_guest(uint8_t *dest, SenseId *src)
{
    int i;

    dest[0] = src->reserved;
    stw_be_p(dest + 1, src->cu_type);
    dest[3] = src->cu_model;
    stw_be_p(dest + 4, src->dev_type);
    dest[6] = src->dev_model;
    dest[7] = src->unused;
    for (i = 0; i < ARRAY_SIZE(src->ciw); i++) {
        dest[8 + i * 4] = src->ciw[i].type;
        dest[9 + i * 4] = src->ciw[i].command;
        stw_be_p(dest + 10 + i * 4, src->ciw[i].count);
    }
}

static CCW1 copy_ccw_from_guest(hwaddr addr, bool fmt1)
{
    CCW0 tmp0;
    CCW1 tmp1;
    CCW1 ret;

    if (fmt1) {
        cpu_physical_memory_read(addr, &tmp1, sizeof(tmp1));
        ret.cmd_code = tmp1.cmd_code;
        ret.flags = tmp1.flags;
        ret.count = be16_to_cpu(tmp1.count);
        ret.cda = be32_to_cpu(tmp1.cda);
    } else {
        cpu_physical_memory_read(addr, &tmp0, sizeof(tmp0));
        if ((tmp0.cmd_code & 0x0f) == CCW_CMD_TIC) {
            ret.cmd_code = CCW_CMD_TIC;
            ret.flags = 0;
            ret.count = 0;
        } else {
            ret.cmd_code = tmp0.cmd_code;
            ret.flags = tmp0.flags;
            ret.count = be16_to_cpu(tmp0.count);
        }
        ret.cda = be16_to_cpu(tmp0.cda1) | (tmp0.cda0 << 16);
    }
    return ret;
}
/**
 * If out of bounds marks the stream broken. If broken returns -EINVAL,
 * otherwise the requested length (may be zero)
 */
static inline int cds_check_len(CcwDataStream *cds, int len)
{
    if (cds->at_byte + len > cds->count) {
        cds->flags |= CDS_F_STREAM_BROKEN;
    }
    return cds->flags & CDS_F_STREAM_BROKEN ? -EINVAL : len;
}

static inline bool cds_ccw_addrs_ok(hwaddr addr, int len, bool ccw_fmt1)
{
    return (addr + len) < (ccw_fmt1 ? (1UL << 31) : (1UL << 24));
}

static int ccw_dstream_rw_noflags(CcwDataStream *cds, void *buff, int len,
                                  CcwDataStreamOp op)
{
    int ret;

    ret = cds_check_len(cds, len);
    if (ret <= 0) {
        return ret;
    }
    if (!cds_ccw_addrs_ok(cds->cda, len, cds->flags & CDS_F_FMT)) {
        return -EINVAL; /* channel program check */
    }
    if (op == CDS_OP_A) {
        goto incr;
    }
    if (!cds->do_skip) {
        ret = address_space_rw(&address_space_memory, cds->cda,
                               MEMTXATTRS_UNSPECIFIED, buff, len, op);
    } else {
        ret = MEMTX_OK;
    }
    if (ret != MEMTX_OK) {
        cds->flags |= CDS_F_STREAM_BROKEN;
        return -EINVAL;
    }
incr:
    cds->at_byte += len;
    cds->cda += len;
    return 0;
}

/* returns values between 1 and bsz, where bsz is a power of 2 */
static inline uint16_t ida_continuous_left(hwaddr cda, uint64_t bsz)
{
    return bsz - (cda & (bsz - 1));
}

static inline uint64_t ccw_ida_block_size(uint8_t flags)
{
    if ((flags & CDS_F_C64) && !(flags & CDS_F_I2K)) {
        return 1ULL << 12;
    }
    return 1ULL << 11;
}

static inline int ida_read_next_idaw(CcwDataStream *cds)
{
    union {uint64_t fmt2; uint32_t fmt1; } idaw;
    int ret;
    hwaddr idaw_addr;
    bool idaw_fmt2 = cds->flags & CDS_F_C64;
    bool ccw_fmt1 = cds->flags & CDS_F_FMT;

    if (idaw_fmt2) {
        idaw_addr = cds->cda_orig + sizeof(idaw.fmt2) * cds->at_idaw;
        if (idaw_addr & 0x07 || !cds_ccw_addrs_ok(idaw_addr, 0, ccw_fmt1)) {
            return -EINVAL; /* channel program check */
        }
        ret = address_space_read(&address_space_memory, idaw_addr,
                                 MEMTXATTRS_UNSPECIFIED, &idaw.fmt2,
                                 sizeof(idaw.fmt2));
        cds->cda = be64_to_cpu(idaw.fmt2);
    } else {
        idaw_addr = cds->cda_orig + sizeof(idaw.fmt1) * cds->at_idaw;
        if (idaw_addr & 0x03 || !cds_ccw_addrs_ok(idaw_addr, 0, ccw_fmt1)) {
            return -EINVAL; /* channel program check */
        }
        ret = address_space_read(&address_space_memory, idaw_addr,
                                 MEMTXATTRS_UNSPECIFIED, &idaw.fmt1,
                                 sizeof(idaw.fmt1));
        cds->cda = be64_to_cpu(idaw.fmt1);
        if (cds->cda & 0x80000000) {
            return -EINVAL; /* channel program check */
        }
    }
    ++(cds->at_idaw);
    if (ret != MEMTX_OK) {
        /* assume inaccessible address */
        return -EINVAL; /* channel program check */
    }
    return 0;
}

static int ccw_dstream_rw_ida(CcwDataStream *cds, void *buff, int len,
                              CcwDataStreamOp op)
{
    uint64_t bsz = ccw_ida_block_size(cds->flags);
    int ret = 0;
    uint16_t cont_left, iter_len;

    ret = cds_check_len(cds, len);
    if (ret <= 0) {
        return ret;
    }
    if (!cds->at_idaw) {
        /* read first idaw */
        ret = ida_read_next_idaw(cds);
        if (ret) {
            goto err;
        }
        cont_left = ida_continuous_left(cds->cda, bsz);
    } else {
        cont_left = ida_continuous_left(cds->cda, bsz);
        if (cont_left == bsz) {
            ret = ida_read_next_idaw(cds);
            if (ret) {
                goto err;
            }
            if (cds->cda & (bsz - 1)) {
                ret = -EINVAL; /* channel program check */
                goto err;
            }
        }
    }
    do {
        iter_len = MIN(len, cont_left);
        if (op != CDS_OP_A) {
            if (!cds->do_skip) {
                ret = address_space_rw(&address_space_memory, cds->cda,
                                       MEMTXATTRS_UNSPECIFIED, buff, iter_len,
                                       op);
            } else {
                ret = MEMTX_OK;
            }
            if (ret != MEMTX_OK) {
                /* assume inaccessible address */
                ret = -EINVAL; /* channel program check */
                goto err;
            }
        }
        cds->at_byte += iter_len;
        cds->cda += iter_len;
        len -= iter_len;
        if (!len) {
            break;
        }
        ret = ida_read_next_idaw(cds);
        if (ret) {
            goto err;
        }
        cont_left = bsz;
    } while (true);
    return ret;
err:
    cds->flags |= CDS_F_STREAM_BROKEN;
    return ret;
}

void ccw_dstream_init(CcwDataStream *cds, CCW1 const *ccw, ORB const *orb)
{
    /*
     * We don't support MIDA (an optional facility) yet and we
     * catch this earlier. Just for expressing the precondition.
     */
    g_assert(!(orb->ctrl1 & ORB_CTRL1_MASK_MIDAW));
    cds->flags = (orb->ctrl0 & ORB_CTRL0_MASK_I2K ? CDS_F_I2K : 0) |
                 (orb->ctrl0 & ORB_CTRL0_MASK_C64 ? CDS_F_C64 : 0) |
                 (orb->ctrl0 & ORB_CTRL0_MASK_FMT ? CDS_F_FMT : 0) |
                 (ccw->flags & CCW_FLAG_IDA ? CDS_F_IDA : 0);

    cds->count = ccw->count;
    cds->cda_orig = ccw->cda;
    /* skip is only effective for read, read backwards, or sense commands */
    cds->do_skip = (ccw->flags & CCW_FLAG_SKIP) &&
        ((ccw->cmd_code & 0x0f) == CCW_CMD_BASIC_SENSE ||
         (ccw->cmd_code & 0x03) == 0x02 /* read */ ||
         (ccw->cmd_code & 0x0f) == 0x0c /* read backwards */);
    ccw_dstream_rewind(cds);
    if (!(cds->flags & CDS_F_IDA)) {
        cds->op_handler = ccw_dstream_rw_noflags;
    } else {
        cds->op_handler = ccw_dstream_rw_ida;
    }
}

static int css_interpret_ccw(SubchDev *sch, hwaddr ccw_addr,
                             bool suspend_allowed)
{
    int ret;
    bool check_len;
    int len;
    CCW1 ccw;

    if (!ccw_addr) {
        return -EINVAL; /* channel-program check */
    }
    /* Check doubleword aligned and 31 or 24 (fmt 0) bit addressable. */
    if (ccw_addr & (sch->ccw_fmt_1 ? 0x80000007 : 0xff000007)) {
        return -EINVAL;
    }

    /* Translate everything to format-1 ccws - the information is the same. */
    ccw = copy_ccw_from_guest(ccw_addr, sch->ccw_fmt_1);

    /* Check for invalid command codes. */
    if ((ccw.cmd_code & 0x0f) == 0) {
        return -EINVAL;
    }
    if (((ccw.cmd_code & 0x0f) == CCW_CMD_TIC) &&
        ((ccw.cmd_code & 0xf0) != 0)) {
        return -EINVAL;
    }
    if (!sch->ccw_fmt_1 && (ccw.count == 0) &&
        (ccw.cmd_code != CCW_CMD_TIC)) {
        return -EINVAL;
    }

    /* We don't support MIDA. */
    if (ccw.flags & CCW_FLAG_MIDA) {
        return -EINVAL;
    }

    if (ccw.flags & CCW_FLAG_SUSPEND) {
        return suspend_allowed ? -EINPROGRESS : -EINVAL;
    }

    check_len = !((ccw.flags & CCW_FLAG_SLI) && !(ccw.flags & CCW_FLAG_DC));

    if (!ccw.cda) {
        if (sch->ccw_no_data_cnt == 255) {
            return -EINVAL;
        }
        sch->ccw_no_data_cnt++;
    }

    /* Look at the command. */
    ccw_dstream_init(&sch->cds, &ccw, &(sch->orb));
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
        ret = ccw_dstream_write_buf(&sch->cds, sch->sense_data, len);
        sch->curr_status.scsw.count = ccw_dstream_residual_count(&sch->cds);
        if (!ret) {
            memset(sch->sense_data, 0, sizeof(sch->sense_data));
        }
        break;
    case CCW_CMD_SENSE_ID:
    {
        /* According to SA22-7204-01, Sense-ID can store up to 256 bytes */
        uint8_t sense_id[256];

        copy_sense_id_to_guest(sense_id, &sch->id);
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
            sense_id[0] = 0xff;
        } else {
            sense_id[0] = 0;
        }
        ret = ccw_dstream_write_buf(&sch->cds, sense_id, len);
        if (!ret) {
            sch->curr_status.scsw.count = ccw_dstream_residual_count(&sch->cds);
        }
        break;
    }
    case CCW_CMD_TIC:
        if (sch->last_cmd_valid && (sch->last_cmd.cmd_code == CCW_CMD_TIC)) {
            ret = -EINVAL;
            break;
        }
        if (ccw.flags || ccw.count) {
            /* We have already sanitized these if converted from fmt 0. */
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

static void sch_handle_start_func_virtual(SubchDev *sch)
{
    SCHIB *schib = &sch->curr_status;
    int path;
    int ret;
    bool suspend_allowed;

    /* Path management: In our simple css, we always choose the only path. */
    path = 0x80;

    if (!(schib->scsw.ctrl & SCSW_ACTL_SUSP)) {
        /* Start Function triggered via ssch, i.e. we have an ORB */
        ORB *orb = &sch->orb;
        schib->scsw.cstat = 0;
        schib->scsw.dstat = 0;
        /* Look at the orb and try to execute the channel program. */
        schib->pmcw.intparm = orb->intparm;
        if (!(orb->lpm & path)) {
            /* Generate a deferred cc 3 condition. */
            schib->scsw.flags |= SCSW_FLAGS_MASK_CC;
            schib->scsw.ctrl &= ~SCSW_CTRL_MASK_STCTL;
            schib->scsw.ctrl |= (SCSW_STCTL_ALERT | SCSW_STCTL_STATUS_PEND);
            return;
        }
        sch->ccw_fmt_1 = !!(orb->ctrl0 & ORB_CTRL0_MASK_FMT);
        schib->scsw.flags |= (sch->ccw_fmt_1) ? SCSW_FLAGS_MASK_FMT : 0;
        sch->ccw_no_data_cnt = 0;
        suspend_allowed = !!(orb->ctrl0 & ORB_CTRL0_MASK_SPND);
    } else {
        /* Start Function resumed via rsch */
        schib->scsw.ctrl &= ~(SCSW_ACTL_SUSP | SCSW_ACTL_RESUME_PEND);
        /* The channel program had been suspended before. */
        suspend_allowed = true;
    }
    sch->last_cmd_valid = false;
    do {
        ret = css_interpret_ccw(sch, sch->channel_prog, suspend_allowed);
        switch (ret) {
        case -EAGAIN:
            /* ccw chain, continue processing */
            break;
        case 0:
            /* success */
            schib->scsw.ctrl &= ~SCSW_ACTL_START_PEND;
            schib->scsw.ctrl &= ~SCSW_CTRL_MASK_STCTL;
            schib->scsw.ctrl |= SCSW_STCTL_PRIMARY | SCSW_STCTL_SECONDARY |
                    SCSW_STCTL_STATUS_PEND;
            schib->scsw.dstat = SCSW_DSTAT_CHANNEL_END | SCSW_DSTAT_DEVICE_END;
            schib->scsw.cpa = sch->channel_prog + 8;
            break;
        case -EIO:
            /* I/O errors, status depends on specific devices */
            break;
        case -ENOSYS:
            /* unsupported command, generate unit check (command reject) */
            schib->scsw.ctrl &= ~SCSW_ACTL_START_PEND;
            schib->scsw.dstat = SCSW_DSTAT_UNIT_CHECK;
            /* Set sense bit 0 in ecw0. */
            sch->sense_data[0] = 0x80;
            schib->scsw.ctrl &= ~SCSW_CTRL_MASK_STCTL;
            schib->scsw.ctrl |= SCSW_STCTL_PRIMARY | SCSW_STCTL_SECONDARY |
                    SCSW_STCTL_ALERT | SCSW_STCTL_STATUS_PEND;
            schib->scsw.cpa = sch->channel_prog + 8;
            break;
        case -EINPROGRESS:
            /* channel program has been suspended */
            schib->scsw.ctrl &= ~SCSW_ACTL_START_PEND;
            schib->scsw.ctrl |= SCSW_ACTL_SUSP;
            break;
        default:
            /* error, generate channel program check */
            schib->scsw.ctrl &= ~SCSW_ACTL_START_PEND;
            schib->scsw.cstat = SCSW_CSTAT_PROG_CHECK;
            schib->scsw.ctrl &= ~SCSW_CTRL_MASK_STCTL;
            schib->scsw.ctrl |= SCSW_STCTL_PRIMARY | SCSW_STCTL_SECONDARY |
                    SCSW_STCTL_ALERT | SCSW_STCTL_STATUS_PEND;
            schib->scsw.cpa = sch->channel_prog + 8;
            break;
        }
    } while (ret == -EAGAIN);

}

static IOInstEnding sch_handle_halt_func_passthrough(SubchDev *sch)
{
    int ret;

    ret = s390_ccw_halt(sch);
    if (ret == -ENOSYS) {
        sch_handle_halt_func(sch);
        return IOINST_CC_EXPECTED;
    }
    /*
     * Some conditions may have been detected prior to starting the halt
     * function; map them to the correct cc.
     * Note that we map both -ENODEV and -EACCES to cc 3 (there's not really
     * anything else we can do.)
     */
    switch (ret) {
    case -EBUSY:
        return IOINST_CC_BUSY;
    case -ENODEV:
    case -EACCES:
        return IOINST_CC_NOT_OPERATIONAL;
    default:
        return IOINST_CC_EXPECTED;
    }
}

static IOInstEnding sch_handle_clear_func_passthrough(SubchDev *sch)
{
    int ret;

    ret = s390_ccw_clear(sch);
    if (ret == -ENOSYS) {
        sch_handle_clear_func(sch);
        return IOINST_CC_EXPECTED;
    }
    /*
     * Some conditions may have been detected prior to starting the clear
     * function; map them to the correct cc.
     * Note that we map both -ENODEV and -EACCES to cc 3 (there's not really
     * anything else we can do.)
     */
    switch (ret) {
    case -ENODEV:
    case -EACCES:
        return IOINST_CC_NOT_OPERATIONAL;
    default:
        return IOINST_CC_EXPECTED;
    }
}

static IOInstEnding sch_handle_start_func_passthrough(SubchDev *sch)
{
    SCHIB *schib = &sch->curr_status;
    ORB *orb = &sch->orb;
    if (!(schib->scsw.ctrl & SCSW_ACTL_SUSP)) {
        assert(orb != NULL);
        schib->pmcw.intparm = orb->intparm;
    }
    return s390_ccw_cmd_request(sch);
}

/*
 * On real machines, this would run asynchronously to the main vcpus.
 * We might want to make some parts of the ssch handling (interpreting
 * read/writes) asynchronous later on if we start supporting more than
 * our current very simple devices.
 */
IOInstEnding do_subchannel_work_virtual(SubchDev *sch)
{
    SCHIB *schib = &sch->curr_status;

    if (schib->scsw.ctrl & SCSW_FCTL_CLEAR_FUNC) {
        sch_handle_clear_func(sch);
    } else if (schib->scsw.ctrl & SCSW_FCTL_HALT_FUNC) {
        sch_handle_halt_func(sch);
    } else if (schib->scsw.ctrl & SCSW_FCTL_START_FUNC) {
        /* Triggered by both ssch and rsch. */
        sch_handle_start_func_virtual(sch);
    }
    css_inject_io_interrupt(sch);
    /* inst must succeed if this func is called */
    return IOINST_CC_EXPECTED;
}

IOInstEnding do_subchannel_work_passthrough(SubchDev *sch)
{
    SCHIB *schib = &sch->curr_status;

    if (schib->scsw.ctrl & SCSW_FCTL_CLEAR_FUNC) {
        return sch_handle_clear_func_passthrough(sch);
    } else if (schib->scsw.ctrl & SCSW_FCTL_HALT_FUNC) {
        return sch_handle_halt_func_passthrough(sch);
    } else if (schib->scsw.ctrl & SCSW_FCTL_START_FUNC) {
        return sch_handle_start_func_passthrough(sch);
    }
    return IOINST_CC_EXPECTED;
}

static IOInstEnding do_subchannel_work(SubchDev *sch)
{
    if (!sch->do_subchannel_work) {
        return IOINST_CC_STATUS_PRESENT;
    }
    g_assert(sch->curr_status.scsw.ctrl & SCSW_CTRL_MASK_FCTL);
    return sch->do_subchannel_work(sch);
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

void copy_scsw_to_guest(SCSW *dest, const SCSW *src)
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
    /*
     * We copy the PMCW and SCSW in and out of local variables to
     * avoid taking the address of members of a packed struct.
     */
    PMCW src_pmcw, dest_pmcw;
    SCSW src_scsw, dest_scsw;

    src_pmcw = src->pmcw;
    copy_pmcw_to_guest(&dest_pmcw, &src_pmcw);
    dest->pmcw = dest_pmcw;
    src_scsw = src->scsw;
    copy_scsw_to_guest(&dest_scsw, &src_scsw);
    dest->scsw = dest_scsw;
    dest->mba = cpu_to_be64(src->mba);
    for (i = 0; i < ARRAY_SIZE(dest->mda); i++) {
        dest->mda[i] = src->mda[i];
    }
}

void copy_esw_to_guest(ESW *dest, const ESW *src)
{
    dest->word0 = cpu_to_be32(src->word0);
    dest->erw = cpu_to_be32(src->erw);
    dest->word2 = cpu_to_be64(src->word2);
    dest->word4 = cpu_to_be32(src->word4);
}

IOInstEnding css_do_stsch(SubchDev *sch, SCHIB *schib)
{
    int ret;

    /*
     * For some subchannels, we may want to update parts of
     * the schib (e.g., update path masks from the host device
     * for passthrough subchannels).
     */
    ret = s390_ccw_store(sch);

    /* Use current status. */
    copy_schib_to_guest(schib, &sch->curr_status);
    return ret;
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
    /*
     * We copy the PMCW and SCSW in and out of local variables to
     * avoid taking the address of members of a packed struct.
     */
    PMCW src_pmcw, dest_pmcw;
    SCSW src_scsw, dest_scsw;

    src_pmcw = src->pmcw;
    copy_pmcw_from_guest(&dest_pmcw, &src_pmcw);
    dest->pmcw = dest_pmcw;
    src_scsw = src->scsw;
    copy_scsw_from_guest(&dest_scsw, &src_scsw);
    dest->scsw = dest_scsw;
    dest->mba = be64_to_cpu(src->mba);
    for (i = 0; i < ARRAY_SIZE(dest->mda); i++) {
        dest->mda[i] = src->mda[i];
    }
}

IOInstEnding css_do_msch(SubchDev *sch, const SCHIB *orig_schib)
{
    SCHIB *schib = &sch->curr_status;
    uint16_t oldflags;
    SCHIB schib_copy;

    if (!(schib->pmcw.flags & PMCW_FLAGS_MASK_DNV)) {
        return IOINST_CC_EXPECTED;
    }

    if (schib->scsw.ctrl & SCSW_STCTL_STATUS_PEND) {
        return IOINST_CC_STATUS_PRESENT;
    }

    if (schib->scsw.ctrl &
        (SCSW_FCTL_START_FUNC|SCSW_FCTL_HALT_FUNC|SCSW_FCTL_CLEAR_FUNC)) {
        return IOINST_CC_BUSY;
    }

    copy_schib_from_guest(&schib_copy, orig_schib);
    /* Only update the program-modifiable fields. */
    schib->pmcw.intparm = schib_copy.pmcw.intparm;
    oldflags = schib->pmcw.flags;
    schib->pmcw.flags &= ~(PMCW_FLAGS_MASK_ISC | PMCW_FLAGS_MASK_ENA |
                  PMCW_FLAGS_MASK_LM | PMCW_FLAGS_MASK_MME |
                  PMCW_FLAGS_MASK_MP);
    schib->pmcw.flags |= schib_copy.pmcw.flags &
            (PMCW_FLAGS_MASK_ISC | PMCW_FLAGS_MASK_ENA |
             PMCW_FLAGS_MASK_LM | PMCW_FLAGS_MASK_MME |
             PMCW_FLAGS_MASK_MP);
    schib->pmcw.lpm = schib_copy.pmcw.lpm;
    schib->pmcw.mbi = schib_copy.pmcw.mbi;
    schib->pmcw.pom = schib_copy.pmcw.pom;
    schib->pmcw.chars &= ~(PMCW_CHARS_MASK_MBFC | PMCW_CHARS_MASK_CSENSE);
    schib->pmcw.chars |= schib_copy.pmcw.chars &
            (PMCW_CHARS_MASK_MBFC | PMCW_CHARS_MASK_CSENSE);
    schib->mba = schib_copy.mba;

    /* Has the channel been disabled? */
    if (sch->disable_cb && (oldflags & PMCW_FLAGS_MASK_ENA) != 0
        && (schib->pmcw.flags & PMCW_FLAGS_MASK_ENA) == 0) {
        sch->disable_cb(sch);
    }
    return IOINST_CC_EXPECTED;
}

IOInstEnding css_do_xsch(SubchDev *sch)
{
    SCHIB *schib = &sch->curr_status;

    if (~(schib->pmcw.flags) & (PMCW_FLAGS_MASK_DNV | PMCW_FLAGS_MASK_ENA)) {
        return IOINST_CC_NOT_OPERATIONAL;
    }

    if (schib->scsw.ctrl & SCSW_CTRL_MASK_STCTL) {
        return IOINST_CC_STATUS_PRESENT;
    }

    if (!(schib->scsw.ctrl & SCSW_CTRL_MASK_FCTL) ||
        ((schib->scsw.ctrl & SCSW_CTRL_MASK_FCTL) != SCSW_FCTL_START_FUNC) ||
        (!(schib->scsw.ctrl &
           (SCSW_ACTL_RESUME_PEND | SCSW_ACTL_START_PEND | SCSW_ACTL_SUSP))) ||
        (schib->scsw.ctrl & SCSW_ACTL_SUBCH_ACTIVE)) {
        return IOINST_CC_BUSY;
    }

    /* Cancel the current operation. */
    schib->scsw.ctrl &= ~(SCSW_FCTL_START_FUNC |
                 SCSW_ACTL_RESUME_PEND |
                 SCSW_ACTL_START_PEND |
                 SCSW_ACTL_SUSP);
    sch->channel_prog = 0x0;
    sch->last_cmd_valid = false;
    schib->scsw.dstat = 0;
    schib->scsw.cstat = 0;
    return IOINST_CC_EXPECTED;
}

IOInstEnding css_do_csch(SubchDev *sch)
{
    SCHIB *schib = &sch->curr_status;
    uint16_t old_scsw_ctrl;
    IOInstEnding ccode;

    if (~(schib->pmcw.flags) & (PMCW_FLAGS_MASK_DNV | PMCW_FLAGS_MASK_ENA)) {
        return IOINST_CC_NOT_OPERATIONAL;
    }

    /*
     * Save the current scsw.ctrl in case CSCH fails and we need
     * to revert the scsw to the status quo ante.
     */
    old_scsw_ctrl = schib->scsw.ctrl;

    /* Trigger the clear function. */
    schib->scsw.ctrl &= ~(SCSW_CTRL_MASK_FCTL | SCSW_CTRL_MASK_ACTL);
    schib->scsw.ctrl |= SCSW_FCTL_CLEAR_FUNC | SCSW_ACTL_CLEAR_PEND;

    ccode = do_subchannel_work(sch);

    if (ccode != IOINST_CC_EXPECTED) {
        schib->scsw.ctrl = old_scsw_ctrl;
    }

    return ccode;
}

IOInstEnding css_do_hsch(SubchDev *sch)
{
    SCHIB *schib = &sch->curr_status;
    uint16_t old_scsw_ctrl;
    IOInstEnding ccode;

    if (~(schib->pmcw.flags) & (PMCW_FLAGS_MASK_DNV | PMCW_FLAGS_MASK_ENA)) {
        return IOINST_CC_NOT_OPERATIONAL;
    }

    if (((schib->scsw.ctrl & SCSW_CTRL_MASK_STCTL) == SCSW_STCTL_STATUS_PEND) ||
        (schib->scsw.ctrl & (SCSW_STCTL_PRIMARY |
                    SCSW_STCTL_SECONDARY |
                    SCSW_STCTL_ALERT))) {
        return IOINST_CC_STATUS_PRESENT;
    }

    if (schib->scsw.ctrl & (SCSW_FCTL_HALT_FUNC | SCSW_FCTL_CLEAR_FUNC)) {
        return IOINST_CC_BUSY;
    }

    /*
     * Save the current scsw.ctrl in case HSCH fails and we need
     * to revert the scsw to the status quo ante.
     */
    old_scsw_ctrl = schib->scsw.ctrl;

    /* Trigger the halt function. */
    schib->scsw.ctrl |= SCSW_FCTL_HALT_FUNC;
    schib->scsw.ctrl &= ~SCSW_FCTL_START_FUNC;
    if (((schib->scsw.ctrl & SCSW_CTRL_MASK_ACTL) ==
         (SCSW_ACTL_SUBCH_ACTIVE | SCSW_ACTL_DEVICE_ACTIVE)) &&
        ((schib->scsw.ctrl & SCSW_CTRL_MASK_STCTL) ==
         SCSW_STCTL_INTERMEDIATE)) {
        schib->scsw.ctrl &= ~SCSW_STCTL_STATUS_PEND;
    }
    schib->scsw.ctrl |= SCSW_ACTL_HALT_PEND;

    ccode = do_subchannel_work(sch);

    if (ccode != IOINST_CC_EXPECTED) {
        schib->scsw.ctrl = old_scsw_ctrl;
    }

    return ccode;
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

        count = address_space_ldl(&address_space_memory,
                                  sch->curr_status.mba,
                                  MEMTXATTRS_UNSPECIFIED,
                                  NULL);
        count++;
        address_space_stl(&address_space_memory, sch->curr_status.mba, count,
                          MEMTXATTRS_UNSPECIFIED, NULL);
    } else {
        /* Format 0, global area. */
        uint32_t offset;
        uint16_t count;

        offset = sch->curr_status.pmcw.mbi << 5;
        count = address_space_lduw(&address_space_memory,
                                   channel_subsys.chnmon_area + offset,
                                   MEMTXATTRS_UNSPECIFIED,
                                   NULL);
        count++;
        address_space_stw(&address_space_memory,
                          channel_subsys.chnmon_area + offset, count,
                          MEMTXATTRS_UNSPECIFIED, NULL);
    }
}

IOInstEnding css_do_ssch(SubchDev *sch, ORB *orb)
{
    SCHIB *schib = &sch->curr_status;
    uint16_t old_scsw_ctrl, old_scsw_flags;
    IOInstEnding ccode;

    if (~(schib->pmcw.flags) & (PMCW_FLAGS_MASK_DNV | PMCW_FLAGS_MASK_ENA)) {
        return IOINST_CC_NOT_OPERATIONAL;
    }

    if (schib->scsw.ctrl & SCSW_STCTL_STATUS_PEND) {
        return IOINST_CC_STATUS_PRESENT;
    }

    if (schib->scsw.ctrl & (SCSW_FCTL_START_FUNC |
                   SCSW_FCTL_HALT_FUNC |
                   SCSW_FCTL_CLEAR_FUNC)) {
        return IOINST_CC_BUSY;
    }

    /* If monitoring is active, update counter. */
    if (channel_subsys.chnmon_active) {
        css_update_chnmon(sch);
    }
    sch->orb = *orb;
    sch->channel_prog = orb->cpa;

    /*
     * Save the current scsw.ctrl and scsw.flags in case SSCH fails and we need
     * to revert the scsw to the status quo ante.
     */
    old_scsw_ctrl = schib->scsw.ctrl;
    old_scsw_flags = schib->scsw.flags;

    /* Trigger the start function. */
    schib->scsw.ctrl |= (SCSW_FCTL_START_FUNC | SCSW_ACTL_START_PEND);
    schib->scsw.flags &= ~SCSW_FLAGS_MASK_PNO;

    ccode = do_subchannel_work(sch);

    if (ccode != IOINST_CC_EXPECTED) {
        schib->scsw.ctrl = old_scsw_ctrl;
        schib->scsw.flags = old_scsw_flags;
    }

    return ccode;
}

static void copy_irb_to_guest(IRB *dest, const IRB *src, const PMCW *pmcw,
                              int *irb_len)
{
    int i;
    uint16_t stctl = src->scsw.ctrl & SCSW_CTRL_MASK_STCTL;
    uint16_t actl = src->scsw.ctrl & SCSW_CTRL_MASK_ACTL;

    copy_scsw_to_guest(&dest->scsw, &src->scsw);

    copy_esw_to_guest(&dest->esw, &src->esw);

    for (i = 0; i < ARRAY_SIZE(dest->ecw); i++) {
        dest->ecw[i] = cpu_to_be32(src->ecw[i]);
    }
    *irb_len = sizeof(*dest) - sizeof(dest->emw);

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
    *irb_len = sizeof(*dest);
}

static void build_irb_sense_data(SubchDev *sch, IRB *irb)
{
    int i;

    /* Attention: sense_data is already BE! */
    memcpy(irb->ecw, sch->sense_data, sizeof(sch->sense_data));
    for (i = 0; i < ARRAY_SIZE(irb->ecw); i++) {
        irb->ecw[i] = be32_to_cpu(irb->ecw[i]);
    }
}

void build_irb_passthrough(SubchDev *sch, IRB *irb)
{
    /* Copy ESW from hardware */
    irb->esw = sch->esw;

    /*
     * If (irb->esw.erw & ESW_ERW_SENSE) is true, then the contents
     * of the ECW is sense data. If false, then it is model-dependent
     * information. Either way, copy it into the IRB for the guest to
     * read/decide what to do with.
     */
    build_irb_sense_data(sch, irb);
}

void build_irb_virtual(SubchDev *sch, IRB *irb)
{
    SCHIB *schib = &sch->curr_status;
    uint16_t stctl = schib->scsw.ctrl & SCSW_CTRL_MASK_STCTL;

    if (stctl & SCSW_STCTL_STATUS_PEND) {
        if (schib->scsw.cstat & (SCSW_CSTAT_DATA_CHECK |
                        SCSW_CSTAT_CHN_CTRL_CHK |
                        SCSW_CSTAT_INTF_CTRL_CHK)) {
            irb->scsw.flags |= SCSW_FLAGS_MASK_ESWF;
            irb->esw.word0 = 0x04804000;
        } else {
            irb->esw.word0 = 0x00800000;
        }
        /* If a unit check is pending, copy sense data. */
        if ((schib->scsw.dstat & SCSW_DSTAT_UNIT_CHECK) &&
            (schib->pmcw.chars & PMCW_CHARS_MASK_CSENSE)) {
            irb->scsw.flags |= SCSW_FLAGS_MASK_ESWF | SCSW_FLAGS_MASK_ECTL;
            build_irb_sense_data(sch, irb);
            irb->esw.erw = ESW_ERW_SENSE | (sizeof(sch->sense_data) << 8);
        }
    }
}

int css_do_tsch_get_irb(SubchDev *sch, IRB *target_irb, int *irb_len)
{
    SCHIB *schib = &sch->curr_status;
    PMCW p;
    uint16_t stctl;
    IRB irb;

    if (~(schib->pmcw.flags) & (PMCW_FLAGS_MASK_DNV | PMCW_FLAGS_MASK_ENA)) {
        return 3;
    }

    stctl = schib->scsw.ctrl & SCSW_CTRL_MASK_STCTL;

    /* Prepare the irb for the guest. */
    memset(&irb, 0, sizeof(IRB));

    /* Copy scsw from current status. */
    irb.scsw = schib->scsw;

    /* Build other IRB data, if necessary */
    if (sch->irb_cb) {
        sch->irb_cb(sch, &irb);
    }

    /* Store the irb to the guest. */
    p = schib->pmcw;
    copy_irb_to_guest(target_irb, &irb, &p, irb_len);

    return ((stctl & SCSW_STCTL_STATUS_PEND) == 0);
}

void css_do_tsch_update_subch(SubchDev *sch)
{
    SCHIB *schib = &sch->curr_status;
    uint16_t stctl;
    uint16_t fctl;
    uint16_t actl;

    stctl = schib->scsw.ctrl & SCSW_CTRL_MASK_STCTL;
    fctl = schib->scsw.ctrl & SCSW_CTRL_MASK_FCTL;
    actl = schib->scsw.ctrl & SCSW_CTRL_MASK_ACTL;

    /* Clear conditions on subchannel, if applicable. */
    if (stctl & SCSW_STCTL_STATUS_PEND) {
        schib->scsw.ctrl &= ~SCSW_CTRL_MASK_STCTL;
        if ((stctl != (SCSW_STCTL_INTERMEDIATE | SCSW_STCTL_STATUS_PEND)) ||
            ((fctl & SCSW_FCTL_HALT_FUNC) &&
             (actl & SCSW_ACTL_SUSP))) {
            schib->scsw.ctrl &= ~SCSW_CTRL_MASK_FCTL;
        }
        if (stctl != (SCSW_STCTL_INTERMEDIATE | SCSW_STCTL_STATUS_PEND)) {
            schib->scsw.flags &= ~SCSW_FLAGS_MASK_PNO;
            schib->scsw.ctrl &= ~(SCSW_ACTL_RESUME_PEND |
                         SCSW_ACTL_START_PEND |
                         SCSW_ACTL_HALT_PEND |
                         SCSW_ACTL_CLEAR_PEND |
                         SCSW_ACTL_SUSP);
        } else {
            if ((actl & SCSW_ACTL_SUSP) &&
                (fctl & SCSW_FCTL_START_FUNC)) {
                schib->scsw.flags &= ~SCSW_FLAGS_MASK_PNO;
                if (fctl & SCSW_FCTL_HALT_FUNC) {
                    schib->scsw.ctrl &= ~(SCSW_ACTL_RESUME_PEND |
                                 SCSW_ACTL_START_PEND |
                                 SCSW_ACTL_HALT_PEND |
                                 SCSW_ACTL_CLEAR_PEND |
                                 SCSW_ACTL_SUSP);
                } else {
                    schib->scsw.ctrl &= ~SCSW_ACTL_RESUME_PEND;
                }
            }
        }
        /* Clear pending sense data. */
        if (schib->pmcw.chars & PMCW_CHARS_MASK_CSENSE) {
            memset(sch->sense_data, 0 , sizeof(sch->sense_data));
        }
    }
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

    crw_cont = QTAILQ_FIRST(&channel_subsys.pending_crws);
    if (crw_cont) {
        QTAILQ_REMOVE(&channel_subsys.pending_crws, crw_cont, sibling);
        copy_crw_to_guest(crw, &crw_cont->crw);
        g_free(crw_cont);
        ret = 0;
    } else {
        /* List was empty, turn crw machine checks on again. */
        memset(crw, 0, sizeof(*crw));
        channel_subsys.do_crw_mchk = true;
        ret = 1;
    }

    return ret;
}

static void copy_crw_from_guest(CRW *dest, const CRW *src)
{
    dest->flags = be16_to_cpu(src->flags);
    dest->rsid = be16_to_cpu(src->rsid);
}

void css_undo_stcrw(CRW *crw)
{
    CrwContainer *crw_cont;

    crw_cont = g_try_new0(CrwContainer, 1);
    if (!crw_cont) {
        channel_subsys.crws_lost = true;
        return;
    }
    copy_crw_from_guest(&crw_cont->crw, crw);

    QTAILQ_INSERT_HEAD(&channel_subsys.pending_crws, crw_cont, sibling);
}

int css_collect_chp_desc(int m, uint8_t cssid, uint8_t f_chpid, uint8_t l_chpid,
                         int rfmt, void *buf)
{
    int i, desc_size;
    uint32_t words[8];
    uint32_t chpid_type_word;
    CssImage *css;

    if (!m && !cssid) {
        css = channel_subsys.css[channel_subsys.default_cssid];
    } else {
        css = channel_subsys.css[cssid];
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
    if (update && !channel_subsys.chnmon_active) {
        /* Enable measuring. */
        channel_subsys.chnmon_area = mbo;
        channel_subsys.chnmon_active = true;
    }
    if (!update && channel_subsys.chnmon_active) {
        /* Disable measuring. */
        channel_subsys.chnmon_area = 0;
        channel_subsys.chnmon_active = false;
    }
}

IOInstEnding css_do_rsch(SubchDev *sch)
{
    SCHIB *schib = &sch->curr_status;

    if (~(schib->pmcw.flags) & (PMCW_FLAGS_MASK_DNV | PMCW_FLAGS_MASK_ENA)) {
        return IOINST_CC_NOT_OPERATIONAL;
    }

    if (schib->scsw.ctrl & SCSW_STCTL_STATUS_PEND) {
        return IOINST_CC_STATUS_PRESENT;
    }

    if (((schib->scsw.ctrl & SCSW_CTRL_MASK_FCTL) != SCSW_FCTL_START_FUNC) ||
        (schib->scsw.ctrl & SCSW_ACTL_RESUME_PEND) ||
        (!(schib->scsw.ctrl & SCSW_ACTL_SUSP))) {
        return IOINST_CC_BUSY;
    }

    /* If monitoring is active, update counter. */
    if (channel_subsys.chnmon_active) {
        css_update_chnmon(sch);
    }

    schib->scsw.ctrl |= SCSW_ACTL_RESUME_PEND;
    return do_subchannel_work(sch);
}

int css_do_rchp(uint8_t cssid, uint8_t chpid)
{
    uint8_t real_cssid;

    if (cssid > channel_subsys.max_cssid) {
        return -EINVAL;
    }
    if (channel_subsys.max_cssid == 0) {
        real_cssid = channel_subsys.default_cssid;
    } else {
        real_cssid = cssid;
    }
    if (!channel_subsys.css[real_cssid]) {
        return -EINVAL;
    }

    if (!channel_subsys.css[real_cssid]->chpids[chpid].in_use) {
        return -ENODEV;
    }

    if (!channel_subsys.css[real_cssid]->chpids[chpid].is_virtual) {
        fprintf(stderr,
                "rchp unsupported for non-virtual chpid %x.%02x!\n",
                real_cssid, chpid);
        return -ENODEV;
    }

    /* We don't really use a channel path, so we're done here. */
    css_queue_crw(CRW_RSC_CHP, CRW_ERC_INIT, 1,
                  channel_subsys.max_cssid > 0 ? 1 : 0, chpid);
    if (channel_subsys.max_cssid > 0) {
        css_queue_crw(CRW_RSC_CHP, CRW_ERC_INIT, 1, 0, real_cssid << 8);
    }
    return 0;
}

bool css_schid_final(int m, uint8_t cssid, uint8_t ssid, uint16_t schid)
{
    SubchSet *set;
    uint8_t real_cssid;

    real_cssid = (!m && (cssid == 0)) ? channel_subsys.default_cssid : cssid;
    if (ssid > MAX_SSID ||
        !channel_subsys.css[real_cssid] ||
        !channel_subsys.css[real_cssid]->sch_set[ssid]) {
        return true;
    }
    set = channel_subsys.css[real_cssid]->sch_set[ssid];
    return schid > find_last_bit(set->schids_used,
                                 (MAX_SCHID + 1) / sizeof(unsigned long));
}

unsigned int css_find_free_chpid(uint8_t cssid)
{
    CssImage *css = channel_subsys.css[cssid];
    unsigned int chpid;

    if (!css) {
        return MAX_CHPID + 1;
    }

    for (chpid = 0; chpid <= MAX_CHPID; chpid++) {
        /* skip reserved chpid */
        if (chpid == VIRTIO_CCW_CHPID) {
            continue;
        }
        if (!css->chpids[chpid].in_use) {
            return chpid;
        }
    }
    return MAX_CHPID + 1;
}

static int css_add_chpid(uint8_t cssid, uint8_t chpid, uint8_t type,
                         bool is_virt)
{
    CssImage *css;

    trace_css_chpid_add(cssid, chpid, type);
    css = channel_subsys.css[cssid];
    if (!css) {
        return -EINVAL;
    }
    if (css->chpids[chpid].in_use) {
        return -EEXIST;
    }
    css->chpids[chpid].in_use = 1;
    css->chpids[chpid].type = type;
    css->chpids[chpid].is_virtual = is_virt;

    css_generate_chp_crws(cssid, chpid);

    return 0;
}

void css_sch_build_virtual_schib(SubchDev *sch, uint8_t chpid, uint8_t type)
{
    SCHIB *schib = &sch->curr_status;
    int i;
    CssImage *css = channel_subsys.css[sch->cssid];

    assert(css != NULL);
    memset(&schib->pmcw, 0, sizeof(PMCW));
    schib->pmcw.flags |= PMCW_FLAGS_MASK_DNV;
    schib->pmcw.devno = sch->devno;
    /* single path */
    schib->pmcw.pim = 0x80;
    schib->pmcw.pom = 0xff;
    schib->pmcw.pam = 0x80;
    schib->pmcw.chpid[0] = chpid;
    if (!css->chpids[chpid].in_use) {
        css_add_chpid(sch->cssid, chpid, type, true);
    }

    memset(&schib->scsw, 0, sizeof(SCSW));
    schib->mba = 0;
    for (i = 0; i < ARRAY_SIZE(schib->mda); i++) {
        schib->mda[i] = 0;
    }
}

SubchDev *css_find_subch(uint8_t m, uint8_t cssid, uint8_t ssid, uint16_t schid)
{
    uint8_t real_cssid;

    real_cssid = (!m && (cssid == 0)) ? channel_subsys.default_cssid : cssid;

    if (!channel_subsys.css[real_cssid]) {
        return NULL;
    }

    if (!channel_subsys.css[real_cssid]->sch_set[ssid]) {
        return NULL;
    }

    return channel_subsys.css[real_cssid]->sch_set[ssid]->sch[schid];
}

/**
 * Return free device number in subchannel set.
 *
 * Return index of the first free device number in the subchannel set
 * identified by @p cssid and @p ssid, beginning the search at @p
 * start and wrapping around at MAX_DEVNO. Return a value exceeding
 * MAX_SCHID if there are no free device numbers in the subchannel
 * set.
 */
static uint32_t css_find_free_devno(uint8_t cssid, uint8_t ssid,
                                    uint16_t start)
{
    uint32_t round;

    for (round = 0; round <= MAX_DEVNO; round++) {
        uint16_t devno = (start + round) % MAX_DEVNO;

        if (!css_devno_used(cssid, ssid, devno)) {
            return devno;
        }
    }
    return MAX_DEVNO + 1;
}

/**
 * Return first free subchannel (id) in subchannel set.
 *
 * Return index of the first free subchannel in the subchannel set
 * identified by @p cssid and @p ssid, if there is any. Return a value
 * exceeding MAX_SCHID if there are no free subchannels in the
 * subchannel set.
 */
static uint32_t css_find_free_subch(uint8_t cssid, uint8_t ssid)
{
    uint32_t schid;

    for (schid = 0; schid <= MAX_SCHID; schid++) {
        if (!css_find_subch(1, cssid, ssid, schid)) {
            return schid;
        }
    }
    return MAX_SCHID + 1;
}

/**
 * Return first free subchannel (id) in subchannel set for a device number
 *
 * Verify the device number @p devno is not used yet in the subchannel
 * set identified by @p cssid and @p ssid. Set @p schid to the index
 * of the first free subchannel in the subchannel set, if there is
 * any. Return true if everything succeeded and false otherwise.
 */
static bool css_find_free_subch_for_devno(uint8_t cssid, uint8_t ssid,
                                          uint16_t devno, uint16_t *schid,
                                          Error **errp)
{
    uint32_t free_schid;

    assert(schid);
    if (css_devno_used(cssid, ssid, devno)) {
        error_setg(errp, "Device %x.%x.%04x already exists",
                   cssid, ssid, devno);
        return false;
    }
    free_schid = css_find_free_subch(cssid, ssid);
    if (free_schid > MAX_SCHID) {
        error_setg(errp, "No free subchannel found for %x.%x.%04x",
                   cssid, ssid, devno);
        return false;
    }
    *schid = free_schid;
    return true;
}

/**
 * Return first free subchannel (id) and device number
 *
 * Locate the first free subchannel and first free device number in
 * any of the subchannel sets of the channel subsystem identified by
 * @p cssid. Return false if no free subchannel / device number could
 * be found. Otherwise set @p ssid, @p devno and @p schid to identify
 * the available subchannel and device number and return true.
 *
 * May modify @p ssid, @p devno and / or @p schid even if no free
 * subchannel / device number could be found.
 */
static bool css_find_free_subch_and_devno(uint8_t cssid, uint8_t *ssid,
                                          uint16_t *devno, uint16_t *schid,
                                          Error **errp)
{
    uint32_t free_schid, free_devno;

    assert(ssid && devno && schid);
    for (*ssid = 0; *ssid <= MAX_SSID; (*ssid)++) {
        free_schid = css_find_free_subch(cssid, *ssid);
        if (free_schid > MAX_SCHID) {
            continue;
        }
        free_devno = css_find_free_devno(cssid, *ssid, free_schid);
        if (free_devno > MAX_DEVNO) {
            continue;
        }
        *schid = free_schid;
        *devno = free_devno;
        return true;
    }
    error_setg(errp, "Virtual channel subsystem is full!");
    return false;
}

bool css_subch_visible(SubchDev *sch)
{
    if (sch->ssid > channel_subsys.max_ssid) {
        return false;
    }

    if (sch->cssid != channel_subsys.default_cssid) {
        return (channel_subsys.max_cssid > 0);
    }

    return true;
}

bool css_present(uint8_t cssid)
{
    return (channel_subsys.css[cssid] != NULL);
}

bool css_devno_used(uint8_t cssid, uint8_t ssid, uint16_t devno)
{
    if (!channel_subsys.css[cssid]) {
        return false;
    }
    if (!channel_subsys.css[cssid]->sch_set[ssid]) {
        return false;
    }

    return !!test_bit(devno,
                      channel_subsys.css[cssid]->sch_set[ssid]->devnos_used);
}

void css_subch_assign(uint8_t cssid, uint8_t ssid, uint16_t schid,
                      uint16_t devno, SubchDev *sch)
{
    CssImage *css;
    SubchSet *s_set;

    trace_css_assign_subch(sch ? "assign" : "deassign", cssid, ssid, schid,
                           devno);
    if (!channel_subsys.css[cssid]) {
        fprintf(stderr,
                "Suspicious call to %s (%x.%x.%04x) for non-existing css!\n",
                __func__, cssid, ssid, schid);
        return;
    }
    css = channel_subsys.css[cssid];

    if (!css->sch_set[ssid]) {
        css->sch_set[ssid] = g_new0(SubchSet, 1);
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

void css_crw_add_to_queue(CRW crw)
{
    CrwContainer *crw_cont;

    trace_css_crw((crw.flags & CRW_FLAGS_MASK_RSC) >> 8,
                  crw.flags & CRW_FLAGS_MASK_ERC,
                  crw.rsid,
                  (crw.flags & CRW_FLAGS_MASK_C) ? "(chained)" : "");

    /* TODO: Maybe use a static crw pool? */
    crw_cont = g_try_new0(CrwContainer, 1);
    if (!crw_cont) {
        channel_subsys.crws_lost = true;
        return;
    }

    crw_cont->crw = crw;

    QTAILQ_INSERT_TAIL(&channel_subsys.pending_crws, crw_cont, sibling);

    if (channel_subsys.do_crw_mchk) {
        channel_subsys.do_crw_mchk = false;
        /* Inject crw pending machine check. */
        s390_crw_mchk();
    }
}

void css_queue_crw(uint8_t rsc, uint8_t erc, int solicited,
                   int chain, uint16_t rsid)
{
    CRW crw;

    crw.flags = (rsc << 8) | erc;
    if (solicited) {
        crw.flags |= CRW_FLAGS_MASK_S;
    }
    if (chain) {
        crw.flags |= CRW_FLAGS_MASK_C;
    }
    crw.rsid = rsid;
    if (channel_subsys.crws_lost) {
        crw.flags |= CRW_FLAGS_MASK_R;
        channel_subsys.crws_lost = false;
    }

    css_crw_add_to_queue(crw);
}

void css_generate_sch_crws(uint8_t cssid, uint8_t ssid, uint16_t schid,
                           int hotplugged, int add)
{
    uint8_t guest_cssid;
    bool chain_crw;

    if (add && !hotplugged) {
        return;
    }
    if (channel_subsys.max_cssid == 0) {
        /* Default cssid shows up as 0. */
        guest_cssid = (cssid == channel_subsys.default_cssid) ? 0 : cssid;
    } else {
        /* Show real cssid to the guest. */
        guest_cssid = cssid;
    }
    /*
     * Only notify for higher subchannel sets/channel subsystems if the
     * guest has enabled it.
     */
    if ((ssid > channel_subsys.max_ssid) ||
        (guest_cssid > channel_subsys.max_cssid) ||
        ((channel_subsys.max_cssid == 0) &&
         (cssid != channel_subsys.default_cssid))) {
        return;
    }
    chain_crw = (channel_subsys.max_ssid > 0) ||
            (channel_subsys.max_cssid > 0);
    css_queue_crw(CRW_RSC_SUBCH, CRW_ERC_IPI, 0, chain_crw ? 1 : 0, schid);
    if (chain_crw) {
        css_queue_crw(CRW_RSC_SUBCH, CRW_ERC_IPI, 0, 0,
                      (guest_cssid << 8) | (ssid << 4));
    }
    /* RW_ERC_IPI --> clear pending interrupts */
    css_clear_io_interrupt(css_do_build_subchannel_id(cssid, ssid), schid);
}

void css_generate_chp_crws(uint8_t cssid, uint8_t chpid)
{
    /* TODO */
}

void css_generate_css_crws(uint8_t cssid)
{
    if (!channel_subsys.sei_pending) {
        css_queue_crw(CRW_RSC_CSS, CRW_ERC_EVENT, 0, 0, cssid);
    }
    channel_subsys.sei_pending = true;
}

void css_clear_sei_pending(void)
{
    channel_subsys.sei_pending = false;
}

int css_enable_mcsse(void)
{
    trace_css_enable_facility("mcsse");
    channel_subsys.max_cssid = MAX_CSSID;
    return 0;
}

int css_enable_mss(void)
{
    trace_css_enable_facility("mss");
    channel_subsys.max_ssid = MAX_SSID;
    return 0;
}

void css_reset_sch(SubchDev *sch)
{
    SCHIB *schib = &sch->curr_status;

    if ((schib->pmcw.flags & PMCW_FLAGS_MASK_ENA) != 0 && sch->disable_cb) {
        sch->disable_cb(sch);
    }

    schib->pmcw.intparm = 0;
    schib->pmcw.flags &= ~(PMCW_FLAGS_MASK_ISC | PMCW_FLAGS_MASK_ENA |
                  PMCW_FLAGS_MASK_LM | PMCW_FLAGS_MASK_MME |
                  PMCW_FLAGS_MASK_MP | PMCW_FLAGS_MASK_TF);
    schib->pmcw.flags |= PMCW_FLAGS_MASK_DNV;
    schib->pmcw.devno = sch->devno;
    schib->pmcw.pim = 0x80;
    schib->pmcw.lpm = schib->pmcw.pim;
    schib->pmcw.pnom = 0;
    schib->pmcw.lpum = 0;
    schib->pmcw.mbi = 0;
    schib->pmcw.pom = 0xff;
    schib->pmcw.pam = 0x80;
    schib->pmcw.chars &= ~(PMCW_CHARS_MASK_MBFC | PMCW_CHARS_MASK_XMWME |
                  PMCW_CHARS_MASK_CSENSE);

    memset(&schib->scsw, 0, sizeof(schib->scsw));
    schib->mba = 0;

    sch->channel_prog = 0x0;
    sch->last_cmd_valid = false;
    sch->thinint_active = false;
}

void css_reset(void)
{
    CrwContainer *crw_cont;

    /* Clean up monitoring. */
    channel_subsys.chnmon_active = false;
    channel_subsys.chnmon_area = 0;

    /* Clear pending CRWs. */
    while ((crw_cont = QTAILQ_FIRST(&channel_subsys.pending_crws))) {
        QTAILQ_REMOVE(&channel_subsys.pending_crws, crw_cont, sibling);
        g_free(crw_cont);
    }
    channel_subsys.sei_pending = false;
    channel_subsys.do_crw_mchk = true;
    channel_subsys.crws_lost = false;

    /* Reset maximum ids. */
    channel_subsys.max_cssid = 0;
    channel_subsys.max_ssid = 0;
}

static void get_css_devid(Object *obj, Visitor *v, const char *name,
                          void *opaque, Error **errp)
{
    Property *prop = opaque;
    CssDevId *dev_id = object_field_prop_ptr(obj, prop);
    char buffer[] = "xx.x.xxxx";
    char *p = buffer;
    int r;

    if (dev_id->valid) {

        r = snprintf(buffer, sizeof(buffer), "%02x.%1x.%04x", dev_id->cssid,
                     dev_id->ssid, dev_id->devid);
        assert(r == sizeof(buffer) - 1);

        /* drop leading zero */
        if (dev_id->cssid <= 0xf) {
            p++;
        }
    } else {
        snprintf(buffer, sizeof(buffer), "<unset>");
    }

    visit_type_str(v, name, &p, errp);
}

/*
 * parse <cssid>.<ssid>.<devid> and assert valid range for cssid/ssid
 */
static void set_css_devid(Object *obj, Visitor *v, const char *name,
                          void *opaque, Error **errp)
{
    Property *prop = opaque;
    CssDevId *dev_id = object_field_prop_ptr(obj, prop);
    char *str;
    int num, n1, n2;
    unsigned int cssid, ssid, devid;

    if (!visit_type_str(v, name, &str, errp)) {
        return;
    }

    num = sscanf(str, "%2x.%1x%n.%4x%n", &cssid, &ssid, &n1, &devid, &n2);
    if (num != 3 || (n2 - n1) != 5 || strlen(str) != n2) {
        error_set_from_qdev_prop_error(errp, EINVAL, obj, name, str);
        goto out;
    }
    if ((cssid > MAX_CSSID) || (ssid > MAX_SSID)) {
        error_setg(errp, "Invalid cssid or ssid: cssid %x, ssid %x",
                   cssid, ssid);
        goto out;
    }

    dev_id->cssid = cssid;
    dev_id->ssid = ssid;
    dev_id->devid = devid;
    dev_id->valid = true;

out:
    g_free(str);
}

const PropertyInfo css_devid_propinfo = {
    .name = "str",
    .description = "Identifier of an I/O device in the channel "
                   "subsystem, example: fe.1.23ab",
    .get = get_css_devid,
    .set = set_css_devid,
};

const PropertyInfo css_devid_ro_propinfo = {
    .name = "str",
    .description = "Read-only identifier of an I/O device in the channel "
                   "subsystem, example: fe.1.23ab",
    .get = get_css_devid,
};

SubchDev *css_create_sch(CssDevId bus_id, Error **errp)
{
    uint16_t schid = 0;
    SubchDev *sch;

    if (bus_id.valid) {
        if (!channel_subsys.css[bus_id.cssid]) {
            css_create_css_image(bus_id.cssid, false);
        }

        if (!css_find_free_subch_for_devno(bus_id.cssid, bus_id.ssid,
                                           bus_id.devid, &schid, errp)) {
            return NULL;
        }
    } else {
        for (bus_id.cssid = channel_subsys.default_cssid;;) {
            if (!channel_subsys.css[bus_id.cssid]) {
                css_create_css_image(bus_id.cssid, false);
            }

            if   (css_find_free_subch_and_devno(bus_id.cssid, &bus_id.ssid,
                                                &bus_id.devid, &schid,
                                                NULL)) {
                break;
            }
            bus_id.cssid = (bus_id.cssid + 1) % MAX_CSSID;
            if (bus_id.cssid == channel_subsys.default_cssid) {
                error_setg(errp, "Virtual channel subsystem is full!");
                return NULL;
            }
        }
    }

    sch = g_new0(SubchDev, 1);
    sch->cssid = bus_id.cssid;
    sch->ssid = bus_id.ssid;
    sch->devno = bus_id.devid;
    sch->schid = schid;
    css_subch_assign(sch->cssid, sch->ssid, schid, sch->devno, sch);
    return sch;
}

static int css_sch_get_chpids(SubchDev *sch, CssDevId *dev_id)
{
    char *fid_path;
    FILE *fd;
    uint32_t chpid[8];
    int i;
    SCHIB *schib = &sch->curr_status;

    fid_path = g_strdup_printf("/sys/bus/css/devices/%x.%x.%04x/chpids",
                               dev_id->cssid, dev_id->ssid, dev_id->devid);
    fd = fopen(fid_path, "r");
    if (fd == NULL) {
        error_report("%s: open %s failed", __func__, fid_path);
        g_free(fid_path);
        return -EINVAL;
    }

    if (fscanf(fd, "%x %x %x %x %x %x %x %x",
        &chpid[0], &chpid[1], &chpid[2], &chpid[3],
        &chpid[4], &chpid[5], &chpid[6], &chpid[7]) != 8) {
        fclose(fd);
        g_free(fid_path);
        return -EINVAL;
    }

    for (i = 0; i < ARRAY_SIZE(schib->pmcw.chpid); i++) {
        schib->pmcw.chpid[i] = chpid[i];
    }

    fclose(fd);
    g_free(fid_path);

    return 0;
}

static int css_sch_get_path_masks(SubchDev *sch, CssDevId *dev_id)
{
    char *fid_path;
    FILE *fd;
    uint32_t pim, pam, pom;
    SCHIB *schib = &sch->curr_status;

    fid_path = g_strdup_printf("/sys/bus/css/devices/%x.%x.%04x/pimpampom",
                               dev_id->cssid, dev_id->ssid, dev_id->devid);
    fd = fopen(fid_path, "r");
    if (fd == NULL) {
        error_report("%s: open %s failed", __func__, fid_path);
        g_free(fid_path);
        return -EINVAL;
    }

    if (fscanf(fd, "%x %x %x", &pim, &pam, &pom) != 3) {
        fclose(fd);
        g_free(fid_path);
        return -EINVAL;
    }

    schib->pmcw.pim = pim;
    schib->pmcw.pam = pam;
    schib->pmcw.pom = pom;
    fclose(fd);
    g_free(fid_path);

    return 0;
}

static int css_sch_get_chpid_type(uint8_t chpid, uint32_t *type,
                                  CssDevId *dev_id)
{
    char *fid_path;
    FILE *fd;

    fid_path = g_strdup_printf("/sys/devices/css%x/chp0.%02x/type",
                               dev_id->cssid, chpid);
    fd = fopen(fid_path, "r");
    if (fd == NULL) {
        error_report("%s: open %s failed", __func__, fid_path);
        g_free(fid_path);
        return -EINVAL;
    }

    if (fscanf(fd, "%x", type) != 1) {
        fclose(fd);
        g_free(fid_path);
        return -EINVAL;
    }

    fclose(fd);
    g_free(fid_path);

    return 0;
}

/*
 * We currently retrieve the real device information from sysfs to build the
 * guest subchannel information block without considering the migration feature.
 * We need to revisit this problem when we want to add migration support.
 */
int css_sch_build_schib(SubchDev *sch, CssDevId *dev_id)
{
    CssImage *css = channel_subsys.css[sch->cssid];
    SCHIB *schib = &sch->curr_status;
    uint32_t type;
    int i, ret;

    assert(css != NULL);
    memset(&schib->pmcw, 0, sizeof(PMCW));
    schib->pmcw.flags |= PMCW_FLAGS_MASK_DNV;
    /* We are dealing with I/O subchannels only. */
    schib->pmcw.devno = sch->devno;

    /* Grab path mask from sysfs. */
    ret = css_sch_get_path_masks(sch, dev_id);
    if (ret) {
        return ret;
    }

    /* Grab chpids from sysfs. */
    ret = css_sch_get_chpids(sch, dev_id);
    if (ret) {
        return ret;
    }

   /* Build chpid type. */
    for (i = 0; i < ARRAY_SIZE(schib->pmcw.chpid); i++) {
        if (schib->pmcw.chpid[i] && !css->chpids[schib->pmcw.chpid[i]].in_use) {
            ret = css_sch_get_chpid_type(schib->pmcw.chpid[i], &type, dev_id);
            if (ret) {
                return ret;
            }
            css_add_chpid(sch->cssid, schib->pmcw.chpid[i], type, false);
        }
    }

    memset(&schib->scsw, 0, sizeof(SCSW));
    schib->mba = 0;
    for (i = 0; i < ARRAY_SIZE(schib->mda); i++) {
        schib->mda[i] = 0;
    }

    return 0;
}
