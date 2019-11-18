/*
 * IPMI BMC emulation
 *
 * Copyright (c) 2015 Corey Minyard, MontaVista Software, LLC
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
#include "sysemu/sysemu.h"
#include "qemu/timer.h"
#include "hw/ipmi/ipmi.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "hw/loader.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#define IPMI_NETFN_CHASSIS            0x00

#define IPMI_CMD_GET_CHASSIS_CAPABILITIES 0x00
#define IPMI_CMD_GET_CHASSIS_STATUS       0x01
#define IPMI_CMD_CHASSIS_CONTROL          0x02
#define IPMI_CMD_GET_SYS_RESTART_CAUSE    0x09

#define IPMI_NETFN_SENSOR_EVENT       0x04

#define IPMI_CMD_PLATFORM_EVENT_MSG       0x02
#define IPMI_CMD_SET_SENSOR_EVT_ENABLE    0x28
#define IPMI_CMD_GET_SENSOR_EVT_ENABLE    0x29
#define IPMI_CMD_REARM_SENSOR_EVTS        0x2a
#define IPMI_CMD_GET_SENSOR_EVT_STATUS    0x2b
#define IPMI_CMD_GET_SENSOR_READING       0x2d
#define IPMI_CMD_SET_SENSOR_TYPE          0x2e
#define IPMI_CMD_GET_SENSOR_TYPE          0x2f
#define IPMI_CMD_SET_SENSOR_READING       0x30

/* #define IPMI_NETFN_APP             0x06 In ipmi.h */

#define IPMI_CMD_GET_DEVICE_ID            0x01
#define IPMI_CMD_COLD_RESET               0x02
#define IPMI_CMD_WARM_RESET               0x03
#define IPMI_CMD_SET_ACPI_POWER_STATE     0x06
#define IPMI_CMD_GET_ACPI_POWER_STATE     0x07
#define IPMI_CMD_GET_DEVICE_GUID          0x08
#define IPMI_CMD_RESET_WATCHDOG_TIMER     0x22
#define IPMI_CMD_SET_WATCHDOG_TIMER       0x24
#define IPMI_CMD_GET_WATCHDOG_TIMER       0x25
#define IPMI_CMD_SET_BMC_GLOBAL_ENABLES   0x2e
#define IPMI_CMD_GET_BMC_GLOBAL_ENABLES   0x2f
#define IPMI_CMD_CLR_MSG_FLAGS            0x30
#define IPMI_CMD_GET_MSG_FLAGS            0x31
#define IPMI_CMD_GET_MSG                  0x33
#define IPMI_CMD_SEND_MSG                 0x34
#define IPMI_CMD_READ_EVT_MSG_BUF         0x35

#define IPMI_NETFN_STORAGE            0x0a

#define IPMI_CMD_GET_SDR_REP_INFO         0x20
#define IPMI_CMD_GET_SDR_REP_ALLOC_INFO   0x21
#define IPMI_CMD_RESERVE_SDR_REP          0x22
#define IPMI_CMD_GET_SDR                  0x23
#define IPMI_CMD_ADD_SDR                  0x24
#define IPMI_CMD_PARTIAL_ADD_SDR          0x25
#define IPMI_CMD_DELETE_SDR               0x26
#define IPMI_CMD_CLEAR_SDR_REP            0x27
#define IPMI_CMD_GET_SDR_REP_TIME         0x28
#define IPMI_CMD_SET_SDR_REP_TIME         0x29
#define IPMI_CMD_ENTER_SDR_REP_UPD_MODE   0x2A
#define IPMI_CMD_EXIT_SDR_REP_UPD_MODE    0x2B
#define IPMI_CMD_RUN_INIT_AGENT           0x2C
#define IPMI_CMD_GET_FRU_AREA_INFO        0x10
#define IPMI_CMD_READ_FRU_DATA            0x11
#define IPMI_CMD_WRITE_FRU_DATA           0x12
#define IPMI_CMD_GET_SEL_INFO             0x40
#define IPMI_CMD_GET_SEL_ALLOC_INFO       0x41
#define IPMI_CMD_RESERVE_SEL              0x42
#define IPMI_CMD_GET_SEL_ENTRY            0x43
#define IPMI_CMD_ADD_SEL_ENTRY            0x44
#define IPMI_CMD_PARTIAL_ADD_SEL_ENTRY    0x45
#define IPMI_CMD_DELETE_SEL_ENTRY         0x46
#define IPMI_CMD_CLEAR_SEL                0x47
#define IPMI_CMD_GET_SEL_TIME             0x48
#define IPMI_CMD_SET_SEL_TIME             0x49


/* Same as a timespec struct. */
struct ipmi_time {
    long tv_sec;
    long tv_nsec;
};

#define MAX_SEL_SIZE 128

typedef struct IPMISel {
    uint8_t sel[MAX_SEL_SIZE][16];
    unsigned int next_free;
    long time_offset;
    uint16_t reservation;
    uint8_t last_addition[4];
    uint8_t last_clear[4];
    uint8_t overflow;
} IPMISel;

#define MAX_SDR_SIZE 16384

typedef struct IPMISdr {
    uint8_t sdr[MAX_SDR_SIZE];
    unsigned int next_free;
    uint16_t next_rec_id;
    uint16_t reservation;
    uint8_t last_addition[4];
    uint8_t last_clear[4];
    uint8_t overflow;
} IPMISdr;

typedef struct IPMIFru {
    char *filename;
    unsigned int nentries;
    uint16_t areasize;
    uint8_t *data;
} IPMIFru;

typedef struct IPMISensor {
    uint8_t status;
    uint8_t reading;
    uint16_t states_suppt;
    uint16_t assert_suppt;
    uint16_t deassert_suppt;
    uint16_t states;
    uint16_t assert_states;
    uint16_t deassert_states;
    uint16_t assert_enable;
    uint16_t deassert_enable;
    uint8_t  sensor_type;
    uint8_t  evt_reading_type_code;
} IPMISensor;
#define IPMI_SENSOR_GET_PRESENT(s)       ((s)->status & 0x01)
#define IPMI_SENSOR_SET_PRESENT(s, v)    ((s)->status = (s->status & ~0x01) | \
                                             !!(v))
#define IPMI_SENSOR_GET_SCAN_ON(s)       ((s)->status & 0x40)
#define IPMI_SENSOR_SET_SCAN_ON(s, v)    ((s)->status = (s->status & ~0x40) | \
                                             ((!!(v)) << 6))
#define IPMI_SENSOR_GET_EVENTS_ON(s)     ((s)->status & 0x80)
#define IPMI_SENSOR_SET_EVENTS_ON(s, v)  ((s)->status = (s->status & ~0x80) | \
                                             ((!!(v)) << 7))
#define IPMI_SENSOR_GET_RET_STATUS(s)    ((s)->status & 0xc0)
#define IPMI_SENSOR_SET_RET_STATUS(s, v) ((s)->status = (s->status & ~0xc0) | \
                                             (v & 0xc0))
#define IPMI_SENSOR_IS_DISCRETE(s) ((s)->evt_reading_type_code != 1)

#define MAX_SENSORS 20
#define IPMI_WATCHDOG_SENSOR 0

#define MAX_NETFNS 64

typedef struct IPMIRcvBufEntry {
    QTAILQ_ENTRY(IPMIRcvBufEntry) entry;
    uint8_t len;
    uint8_t buf[MAX_IPMI_MSG_SIZE];
} IPMIRcvBufEntry;

struct IPMIBmcSim {
    IPMIBmc parent;

    QEMUTimer *timer;

    uint8_t bmc_global_enables;
    uint8_t msg_flags;

    bool     watchdog_initialized;
    uint8_t  watchdog_use;
    uint8_t  watchdog_action;
    uint8_t  watchdog_pretimeout; /* In seconds */
    bool     watchdog_expired;
    uint16_t watchdog_timeout; /* in 100's of milliseconds */

    bool     watchdog_running;
    bool     watchdog_preaction_ran;
    int64_t  watchdog_expiry;

    uint8_t device_id;
    uint8_t ipmi_version;
    uint8_t device_rev;
    uint8_t fwrev1;
    uint8_t fwrev2;
    uint32_t mfg_id;
    uint16_t product_id;

    uint8_t restart_cause;

    uint8_t acpi_power_state[2];
    QemuUUID uuid;

    IPMISel sel;
    IPMISdr sdr;
    IPMIFru fru;
    IPMISensor sensors[MAX_SENSORS];
    char *sdr_filename;

    /* Odd netfns are for responses, so we only need the even ones. */
    const IPMINetfn *netfns[MAX_NETFNS / 2];

    /* We allow one event in the buffer */
    uint8_t evtbuf[16];

    QTAILQ_HEAD(, IPMIRcvBufEntry) rcvbufs;
};

#define IPMI_BMC_MSG_FLAG_WATCHDOG_TIMEOUT_MASK        (1 << 3)
#define IPMI_BMC_MSG_FLAG_EVT_BUF_FULL                 (1 << 1)
#define IPMI_BMC_MSG_FLAG_RCV_MSG_QUEUE                (1 << 0)
#define IPMI_BMC_MSG_FLAG_WATCHDOG_TIMEOUT_MASK_SET(s) \
    (IPMI_BMC_MSG_FLAG_WATCHDOG_TIMEOUT_MASK & (s)->msg_flags)
#define IPMI_BMC_MSG_FLAG_EVT_BUF_FULL_SET(s) \
    (IPMI_BMC_MSG_FLAG_EVT_BUF_FULL & (s)->msg_flags)
#define IPMI_BMC_MSG_FLAG_RCV_MSG_QUEUE_SET(s) \
    (IPMI_BMC_MSG_FLAG_RCV_MSG_QUEUE & (s)->msg_flags)

#define IPMI_BMC_RCV_MSG_QUEUE_INT_BIT    0
#define IPMI_BMC_EVBUF_FULL_INT_BIT       1
#define IPMI_BMC_EVENT_MSG_BUF_BIT        2
#define IPMI_BMC_EVENT_LOG_BIT            3
#define IPMI_BMC_MSG_INTS_ON(s) ((s)->bmc_global_enables & \
                                 (1 << IPMI_BMC_RCV_MSG_QUEUE_INT_BIT))
#define IPMI_BMC_EVBUF_FULL_INT_ENABLED(s) ((s)->bmc_global_enables & \
                                        (1 << IPMI_BMC_EVBUF_FULL_INT_BIT))
#define IPMI_BMC_EVENT_LOG_ENABLED(s) ((s)->bmc_global_enables & \
                                       (1 << IPMI_BMC_EVENT_LOG_BIT))
#define IPMI_BMC_EVENT_MSG_BUF_ENABLED(s) ((s)->bmc_global_enables & \
                                           (1 << IPMI_BMC_EVENT_MSG_BUF_BIT))

#define IPMI_BMC_WATCHDOG_USE_MASK 0xc7
#define IPMI_BMC_WATCHDOG_ACTION_MASK 0x77
#define IPMI_BMC_WATCHDOG_GET_USE(s) ((s)->watchdog_use & 0x7)
#define IPMI_BMC_WATCHDOG_GET_DONT_LOG(s) (((s)->watchdog_use >> 7) & 0x1)
#define IPMI_BMC_WATCHDOG_GET_DONT_STOP(s) (((s)->watchdog_use >> 6) & 0x1)
#define IPMI_BMC_WATCHDOG_GET_PRE_ACTION(s) (((s)->watchdog_action >> 4) & 0x7)
#define IPMI_BMC_WATCHDOG_PRE_NONE               0
#define IPMI_BMC_WATCHDOG_PRE_SMI                1
#define IPMI_BMC_WATCHDOG_PRE_NMI                2
#define IPMI_BMC_WATCHDOG_PRE_MSG_INT            3
#define IPMI_BMC_WATCHDOG_GET_ACTION(s) ((s)->watchdog_action & 0x7)
#define IPMI_BMC_WATCHDOG_ACTION_NONE            0
#define IPMI_BMC_WATCHDOG_ACTION_RESET           1
#define IPMI_BMC_WATCHDOG_ACTION_POWER_DOWN      2
#define IPMI_BMC_WATCHDOG_ACTION_POWER_CYCLE     3

#define RSP_BUFFER_INITIALIZER { }

static inline void rsp_buffer_pushmore(RspBuffer *rsp, uint8_t *bytes,
                                       unsigned int n)
{
    if (rsp->len + n >= sizeof(rsp->buffer)) {
        rsp_buffer_set_error(rsp, IPMI_CC_REQUEST_DATA_TRUNCATED);
        return;
    }

    memcpy(&rsp->buffer[rsp->len], bytes, n);
    rsp->len += n;
}

static void ipmi_sim_handle_timeout(IPMIBmcSim *ibs);

static void ipmi_gettime(struct ipmi_time *time)
{
    int64_t stime;

    stime = qemu_clock_get_ns(QEMU_CLOCK_HOST);
    time->tv_sec = stime / 1000000000LL;
    time->tv_nsec = stime % 1000000000LL;
}

static int64_t ipmi_getmonotime(void)
{
    return qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

static void ipmi_timeout(void *opaque)
{
    IPMIBmcSim *ibs = opaque;

    ipmi_sim_handle_timeout(ibs);
}

static void set_timestamp(IPMIBmcSim *ibs, uint8_t *ts)
{
    unsigned int val;
    struct ipmi_time now;

    ipmi_gettime(&now);
    val = now.tv_sec + ibs->sel.time_offset;
    ts[0] = val & 0xff;
    ts[1] = (val >> 8) & 0xff;
    ts[2] = (val >> 16) & 0xff;
    ts[3] = (val >> 24) & 0xff;
}

static void sdr_inc_reservation(IPMISdr *sdr)
{
    sdr->reservation++;
    if (sdr->reservation == 0) {
        sdr->reservation = 1;
    }
}

static int sdr_add_entry(IPMIBmcSim *ibs,
                         const struct ipmi_sdr_header *sdrh_entry,
                         unsigned int len, uint16_t *recid)
{
    struct ipmi_sdr_header *sdrh =
        (struct ipmi_sdr_header *) &ibs->sdr.sdr[ibs->sdr.next_free];

    if ((len < IPMI_SDR_HEADER_SIZE) || (len > 255)) {
        return 1;
    }

    if (ipmi_sdr_length(sdrh_entry) != len) {
        return 1;
    }

    if (ibs->sdr.next_free + len > MAX_SDR_SIZE) {
        ibs->sdr.overflow = 1;
        return 1;
    }

    memcpy(sdrh, sdrh_entry, len);
    sdrh->rec_id[0] = ibs->sdr.next_rec_id & 0xff;
    sdrh->rec_id[1] = (ibs->sdr.next_rec_id >> 8) & 0xff;
    sdrh->sdr_version = 0x51; /* Conform to IPMI 1.5 spec */

    if (recid) {
        *recid = ibs->sdr.next_rec_id;
    }
    ibs->sdr.next_rec_id++;
    set_timestamp(ibs, ibs->sdr.last_addition);
    ibs->sdr.next_free += len;
    sdr_inc_reservation(&ibs->sdr);
    return 0;
}

static int sdr_find_entry(IPMISdr *sdr, uint16_t recid,
                          unsigned int *retpos, uint16_t *nextrec)
{
    unsigned int pos = *retpos;

    while (pos < sdr->next_free) {
        struct ipmi_sdr_header *sdrh =
            (struct ipmi_sdr_header *) &sdr->sdr[pos];
        uint16_t trec = ipmi_sdr_recid(sdrh);
        unsigned int nextpos = pos + ipmi_sdr_length(sdrh);

        if (trec == recid) {
            if (nextrec) {
                if (nextpos >= sdr->next_free) {
                    *nextrec = 0xffff;
                } else {
                    *nextrec = (sdr->sdr[nextpos] |
                                (sdr->sdr[nextpos + 1] << 8));
                }
            }
            *retpos = pos;
            return 0;
        }
        pos = nextpos;
    }
    return 1;
}

int ipmi_bmc_sdr_find(IPMIBmc *b, uint16_t recid,
                      const struct ipmi_sdr_compact **sdr, uint16_t *nextrec)

{
    IPMIBmcSim *ibs = IPMI_BMC_SIMULATOR(b);
    unsigned int pos;

    pos = 0;
    if (sdr_find_entry(&ibs->sdr, recid, &pos, nextrec)) {
        return -1;
    }

    *sdr = (const struct ipmi_sdr_compact *) &ibs->sdr.sdr[pos];
    return 0;
}

static void sel_inc_reservation(IPMISel *sel)
{
    sel->reservation++;
    if (sel->reservation == 0) {
        sel->reservation = 1;
    }
}

/* Returns 1 if the SEL is full and can't hold the event. */
static int sel_add_event(IPMIBmcSim *ibs, uint8_t *event)
{
    uint8_t ts[4];

    event[0] = 0xff;
    event[1] = 0xff;
    set_timestamp(ibs, ts);
    if (event[2] < 0xe0) { /* Don't set timestamps for type 0xe0-0xff. */
        memcpy(event + 3, ts, 4);
    }
    if (ibs->sel.next_free == MAX_SEL_SIZE) {
        ibs->sel.overflow = 1;
        return 1;
    }
    event[0] = ibs->sel.next_free & 0xff;
    event[1] = (ibs->sel.next_free >> 8) & 0xff;
    memcpy(ibs->sel.last_addition, ts, 4);
    memcpy(ibs->sel.sel[ibs->sel.next_free], event, 16);
    ibs->sel.next_free++;
    sel_inc_reservation(&ibs->sel);
    return 0;
}

static int attn_set(IPMIBmcSim *ibs)
{
    return IPMI_BMC_MSG_FLAG_RCV_MSG_QUEUE_SET(ibs)
        || IPMI_BMC_MSG_FLAG_EVT_BUF_FULL_SET(ibs)
        || IPMI_BMC_MSG_FLAG_WATCHDOG_TIMEOUT_MASK_SET(ibs);
}

static int attn_irq_enabled(IPMIBmcSim *ibs)
{
    return (IPMI_BMC_MSG_INTS_ON(ibs) &&
            (IPMI_BMC_MSG_FLAG_RCV_MSG_QUEUE_SET(ibs) ||
             IPMI_BMC_MSG_FLAG_WATCHDOG_TIMEOUT_MASK_SET(ibs)))
        || (IPMI_BMC_EVBUF_FULL_INT_ENABLED(ibs) &&
            IPMI_BMC_MSG_FLAG_EVT_BUF_FULL_SET(ibs));
}

void ipmi_bmc_gen_event(IPMIBmc *b, uint8_t *evt, bool log)
{
    IPMIBmcSim *ibs = IPMI_BMC_SIMULATOR(b);
    IPMIInterface *s = ibs->parent.intf;
    IPMIInterfaceClass *k = IPMI_INTERFACE_GET_CLASS(s);

    if (!IPMI_BMC_EVENT_MSG_BUF_ENABLED(ibs)) {
        return;
    }

    if (log && IPMI_BMC_EVENT_LOG_ENABLED(ibs)) {
        sel_add_event(ibs, evt);
    }

    if (ibs->msg_flags & IPMI_BMC_MSG_FLAG_EVT_BUF_FULL) {
        goto out;
    }

    memcpy(ibs->evtbuf, evt, 16);
    ibs->msg_flags |= IPMI_BMC_MSG_FLAG_EVT_BUF_FULL;
    k->set_atn(s, 1, attn_irq_enabled(ibs));
 out:
    return;
}
static void gen_event(IPMIBmcSim *ibs, unsigned int sens_num, uint8_t deassert,
                      uint8_t evd1, uint8_t evd2, uint8_t evd3)
{
    IPMIInterface *s = ibs->parent.intf;
    IPMIInterfaceClass *k = IPMI_INTERFACE_GET_CLASS(s);
    uint8_t evt[16];
    IPMISensor *sens = ibs->sensors + sens_num;

    if (!IPMI_BMC_EVENT_MSG_BUF_ENABLED(ibs)) {
        return;
    }
    if (!IPMI_SENSOR_GET_EVENTS_ON(sens)) {
        return;
    }

    evt[2] = 0x2; /* System event record */
    evt[7] = ibs->parent.slave_addr;
    evt[8] = 0;
    evt[9] = 0x04; /* Format version */
    evt[10] = sens->sensor_type;
    evt[11] = sens_num;
    evt[12] = sens->evt_reading_type_code | (!!deassert << 7);
    evt[13] = evd1;
    evt[14] = evd2;
    evt[15] = evd3;

    if (IPMI_BMC_EVENT_LOG_ENABLED(ibs)) {
        sel_add_event(ibs, evt);
    }

    if (ibs->msg_flags & IPMI_BMC_MSG_FLAG_EVT_BUF_FULL) {
        return;
    }

    memcpy(ibs->evtbuf, evt, 16);
    ibs->msg_flags |= IPMI_BMC_MSG_FLAG_EVT_BUF_FULL;
    k->set_atn(s, 1, attn_irq_enabled(ibs));
}

static void sensor_set_discrete_bit(IPMIBmcSim *ibs, unsigned int sensor,
                                    unsigned int bit, unsigned int val,
                                    uint8_t evd1, uint8_t evd2, uint8_t evd3)
{
    IPMISensor *sens;
    uint16_t mask;

    if (sensor >= MAX_SENSORS) {
        return;
    }
    if (bit >= 16) {
        return;
    }

    mask = (1 << bit);
    sens = ibs->sensors + sensor;
    if (val) {
        sens->states |= mask & sens->states_suppt;
        if (sens->assert_states & mask) {
            return; /* Already asserted */
        }
        sens->assert_states |= mask & sens->assert_suppt;
        if (sens->assert_enable & mask & sens->assert_states) {
            /* Send an event on assert */
            gen_event(ibs, sensor, 0, evd1, evd2, evd3);
        }
    } else {
        sens->states &= ~(mask & sens->states_suppt);
        if (sens->deassert_states & mask) {
            return; /* Already deasserted */
        }
        sens->deassert_states |= mask & sens->deassert_suppt;
        if (sens->deassert_enable & mask & sens->deassert_states) {
            /* Send an event on deassert */
            gen_event(ibs, sensor, 1, evd1, evd2, evd3);
        }
    }
}

static void ipmi_init_sensors_from_sdrs(IPMIBmcSim *s)
{
    unsigned int i, pos;
    IPMISensor *sens;

    for (i = 0; i < MAX_SENSORS; i++) {
        memset(s->sensors + i, 0, sizeof(*sens));
    }

    pos = 0;
    for (i = 0; !sdr_find_entry(&s->sdr, i, &pos, NULL); i++) {
        struct ipmi_sdr_compact *sdr =
            (struct ipmi_sdr_compact *) &s->sdr.sdr[pos];
        unsigned int len = sdr->header.rec_length;

        if (len < 20) {
            continue;
        }
        if (sdr->header.rec_type != IPMI_SDR_COMPACT_TYPE) {
            continue; /* Not a sensor SDR we set from */
        }

        if (sdr->sensor_owner_number >= MAX_SENSORS) {
            continue;
        }
        sens = s->sensors + sdr->sensor_owner_number;

        IPMI_SENSOR_SET_PRESENT(sens, 1);
        IPMI_SENSOR_SET_SCAN_ON(sens, (sdr->sensor_init >> 6) & 1);
        IPMI_SENSOR_SET_EVENTS_ON(sens, (sdr->sensor_init >> 5) & 1);
        sens->assert_suppt = sdr->assert_mask[0] | (sdr->assert_mask[1] << 8);
        sens->deassert_suppt =
            sdr->deassert_mask[0] | (sdr->deassert_mask[1] << 8);
        sens->states_suppt =
            sdr->discrete_mask[0] | (sdr->discrete_mask[1] << 8);
        sens->sensor_type = sdr->sensor_type;
        sens->evt_reading_type_code = sdr->reading_type & 0x7f;

        /* Enable all the events that are supported. */
        sens->assert_enable = sens->assert_suppt;
        sens->deassert_enable = sens->deassert_suppt;
    }
}

int ipmi_sim_register_netfn(IPMIBmcSim *s, unsigned int netfn,
                        const IPMINetfn *netfnd)
{
    if ((netfn & 1) || (netfn >= MAX_NETFNS) || (s->netfns[netfn / 2])) {
        return -1;
    }
    s->netfns[netfn / 2] = netfnd;
    return 0;
}

static const IPMICmdHandler *ipmi_get_handler(IPMIBmcSim *ibs,
                                              unsigned int netfn,
                                              unsigned int cmd)
{
    const IPMICmdHandler *hdl;

    if (netfn & 1 || netfn >= MAX_NETFNS || !ibs->netfns[netfn / 2]) {
        return NULL;
    }

    if (cmd >= ibs->netfns[netfn / 2]->cmd_nums) {
        return NULL;
    }

    hdl = &ibs->netfns[netfn / 2]->cmd_handlers[cmd];
    if (!hdl->cmd_handler) {
        return NULL;
    }

    return hdl;
}

static void next_timeout(IPMIBmcSim *ibs)
{
    int64_t next;
    if (ibs->watchdog_running) {
        next = ibs->watchdog_expiry;
    } else {
        /* Wait a minute */
        next = ipmi_getmonotime() + 60 * 1000000000LL;
    }
    timer_mod_ns(ibs->timer, next);
}

static void ipmi_sim_handle_command(IPMIBmc *b,
                                    uint8_t *cmd, unsigned int cmd_len,
                                    unsigned int max_cmd_len,
                                    uint8_t msg_id)
{
    IPMIBmcSim *ibs = IPMI_BMC_SIMULATOR(b);
    IPMIInterface *s = ibs->parent.intf;
    IPMIInterfaceClass *k = IPMI_INTERFACE_GET_CLASS(s);
    const IPMICmdHandler *hdl;
    RspBuffer rsp = RSP_BUFFER_INITIALIZER;

    /* Set up the response, set the low bit of NETFN. */
    /* Note that max_rsp_len must be at least 3 */
    if (sizeof(rsp.buffer) < 3) {
        rsp_buffer_set_error(&rsp, IPMI_CC_REQUEST_DATA_TRUNCATED);
        goto out;
    }

    rsp_buffer_push(&rsp, cmd[0] | 0x04);
    rsp_buffer_push(&rsp, cmd[1]);
    rsp_buffer_push(&rsp, 0); /* Assume success */

    /* If it's too short or it was truncated, return an error. */
    if (cmd_len < 2) {
        rsp_buffer_set_error(&rsp, IPMI_CC_REQUEST_DATA_LENGTH_INVALID);
        goto out;
    }
    if (cmd_len > max_cmd_len) {
        rsp_buffer_set_error(&rsp, IPMI_CC_REQUEST_DATA_TRUNCATED);
        goto out;
    }

    if ((cmd[0] & 0x03) != 0) {
        /* Only have stuff on LUN 0 */
        rsp_buffer_set_error(&rsp, IPMI_CC_COMMAND_INVALID_FOR_LUN);
        goto out;
    }

    hdl = ipmi_get_handler(ibs, cmd[0] >> 2, cmd[1]);
    if (!hdl) {
        rsp_buffer_set_error(&rsp, IPMI_CC_INVALID_CMD);
        goto out;
    }

    if (cmd_len < hdl->cmd_len_min) {
        rsp_buffer_set_error(&rsp, IPMI_CC_REQUEST_DATA_LENGTH_INVALID);
        goto out;
    }

    hdl->cmd_handler(ibs, cmd, cmd_len, &rsp);

 out:
    k->handle_rsp(s, msg_id, rsp.buffer, rsp.len);

    next_timeout(ibs);
}

static void ipmi_sim_handle_timeout(IPMIBmcSim *ibs)
{
    IPMIInterface *s = ibs->parent.intf;
    IPMIInterfaceClass *k = IPMI_INTERFACE_GET_CLASS(s);

    if (!ibs->watchdog_running) {
        goto out;
    }

    if (!ibs->watchdog_preaction_ran) {
        switch (IPMI_BMC_WATCHDOG_GET_PRE_ACTION(ibs)) {
        case IPMI_BMC_WATCHDOG_PRE_NMI:
            ibs->msg_flags |= IPMI_BMC_MSG_FLAG_WATCHDOG_TIMEOUT_MASK;
            k->do_hw_op(s, IPMI_SEND_NMI, 0);
            sensor_set_discrete_bit(ibs, IPMI_WATCHDOG_SENSOR, 8, 1,
                                    0xc8, (2 << 4) | 0xf, 0xff);
            break;

        case IPMI_BMC_WATCHDOG_PRE_MSG_INT:
            ibs->msg_flags |= IPMI_BMC_MSG_FLAG_WATCHDOG_TIMEOUT_MASK;
            k->set_atn(s, 1, attn_irq_enabled(ibs));
            sensor_set_discrete_bit(ibs, IPMI_WATCHDOG_SENSOR, 8, 1,
                                    0xc8, (3 << 4) | 0xf, 0xff);
            break;

        default:
            goto do_full_expiry;
        }

        ibs->watchdog_preaction_ran = 1;
        /* Issued the pretimeout, do the rest of the timeout now. */
        ibs->watchdog_expiry = ipmi_getmonotime();
        ibs->watchdog_expiry += ibs->watchdog_pretimeout * 1000000000LL;
        goto out;
    }

 do_full_expiry:
    ibs->watchdog_running = 0; /* Stop the watchdog on a timeout */
    ibs->watchdog_expired |= (1 << IPMI_BMC_WATCHDOG_GET_USE(ibs));
    switch (IPMI_BMC_WATCHDOG_GET_ACTION(ibs)) {
    case IPMI_BMC_WATCHDOG_ACTION_NONE:
        sensor_set_discrete_bit(ibs, IPMI_WATCHDOG_SENSOR, 0, 1,
                                0xc0, ibs->watchdog_use & 0xf, 0xff);
        break;

    case IPMI_BMC_WATCHDOG_ACTION_RESET:
        sensor_set_discrete_bit(ibs, IPMI_WATCHDOG_SENSOR, 1, 1,
                                0xc1, ibs->watchdog_use & 0xf, 0xff);
        k->do_hw_op(s, IPMI_RESET_CHASSIS, 0);
        break;

    case IPMI_BMC_WATCHDOG_ACTION_POWER_DOWN:
        sensor_set_discrete_bit(ibs, IPMI_WATCHDOG_SENSOR, 2, 1,
                                0xc2, ibs->watchdog_use & 0xf, 0xff);
        k->do_hw_op(s, IPMI_POWEROFF_CHASSIS, 0);
        break;

    case IPMI_BMC_WATCHDOG_ACTION_POWER_CYCLE:
        sensor_set_discrete_bit(ibs, IPMI_WATCHDOG_SENSOR, 2, 1,
                                0xc3, ibs->watchdog_use & 0xf, 0xff);
        k->do_hw_op(s, IPMI_POWERCYCLE_CHASSIS, 0);
        break;
    }

 out:
    next_timeout(ibs);
}

static void chassis_capabilities(IPMIBmcSim *ibs,
                                 uint8_t *cmd, unsigned int cmd_len,
                                 RspBuffer *rsp)
{
    rsp_buffer_push(rsp, 0);
    rsp_buffer_push(rsp, ibs->parent.slave_addr);
    rsp_buffer_push(rsp, ibs->parent.slave_addr);
    rsp_buffer_push(rsp, ibs->parent.slave_addr);
    rsp_buffer_push(rsp, ibs->parent.slave_addr);
}

static void chassis_status(IPMIBmcSim *ibs,
                           uint8_t *cmd, unsigned int cmd_len,
                           RspBuffer *rsp)
{
    rsp_buffer_push(rsp, 0x61); /* Unknown power restore, power is on */
    rsp_buffer_push(rsp, 0);
    rsp_buffer_push(rsp, 0);
    rsp_buffer_push(rsp, 0);
}

static void chassis_control(IPMIBmcSim *ibs,
                            uint8_t *cmd, unsigned int cmd_len,
                            RspBuffer *rsp)
{
    IPMIInterface *s = ibs->parent.intf;
    IPMIInterfaceClass *k = IPMI_INTERFACE_GET_CLASS(s);

    switch (cmd[2] & 0xf) {
    case 0: /* power down */
        rsp_buffer_set_error(rsp, k->do_hw_op(s, IPMI_POWEROFF_CHASSIS, 0));
        break;
    case 1: /* power up */
        rsp_buffer_set_error(rsp, k->do_hw_op(s, IPMI_POWERON_CHASSIS, 0));
        break;
    case 2: /* power cycle */
        rsp_buffer_set_error(rsp, k->do_hw_op(s, IPMI_POWERCYCLE_CHASSIS, 0));
        break;
    case 3: /* hard reset */
        rsp_buffer_set_error(rsp, k->do_hw_op(s, IPMI_RESET_CHASSIS, 0));
        break;
    case 4: /* pulse diagnostic interrupt */
        rsp_buffer_set_error(rsp, k->do_hw_op(s, IPMI_PULSE_DIAG_IRQ, 0));
        break;
    case 5: /* soft shutdown via ACPI by overtemp emulation */
        rsp_buffer_set_error(rsp, k->do_hw_op(s,
                                          IPMI_SHUTDOWN_VIA_ACPI_OVERTEMP, 0));
        break;
    default:
        rsp_buffer_set_error(rsp, IPMI_CC_INVALID_DATA_FIELD);
        return;
    }
}

static void chassis_get_sys_restart_cause(IPMIBmcSim *ibs,
                           uint8_t *cmd, unsigned int cmd_len,
                           RspBuffer *rsp)

{
    rsp_buffer_push(rsp, ibs->restart_cause & 0xf); /* Restart Cause */
    rsp_buffer_push(rsp, 0);  /* Channel 0 */
}

static void get_device_id(IPMIBmcSim *ibs,
                          uint8_t *cmd, unsigned int cmd_len,
                          RspBuffer *rsp)
{
    rsp_buffer_push(rsp, ibs->device_id);
    rsp_buffer_push(rsp, ibs->device_rev & 0xf);
    rsp_buffer_push(rsp, ibs->fwrev1 & 0x7f);
    rsp_buffer_push(rsp, ibs->fwrev2);
    rsp_buffer_push(rsp, ibs->ipmi_version);
    rsp_buffer_push(rsp, 0x07); /* sensor, SDR, and SEL. */
    rsp_buffer_push(rsp, ibs->mfg_id & 0xff);
    rsp_buffer_push(rsp, (ibs->mfg_id >> 8) & 0xff);
    rsp_buffer_push(rsp, (ibs->mfg_id >> 16) & 0xff);
    rsp_buffer_push(rsp, ibs->product_id & 0xff);
    rsp_buffer_push(rsp, (ibs->product_id >> 8) & 0xff);
}

static void set_global_enables(IPMIBmcSim *ibs, uint8_t val)
{
    IPMIInterface *s = ibs->parent.intf;
    IPMIInterfaceClass *k = IPMI_INTERFACE_GET_CLASS(s);
    bool irqs_on;

    ibs->bmc_global_enables = val;

    irqs_on = val & (IPMI_BMC_EVBUF_FULL_INT_BIT |
                     IPMI_BMC_RCV_MSG_QUEUE_INT_BIT);

    k->set_irq_enable(s, irqs_on);
}

static void cold_reset(IPMIBmcSim *ibs,
                       uint8_t *cmd, unsigned int cmd_len,
                       RspBuffer *rsp)
{
    IPMIInterface *s = ibs->parent.intf;
    IPMIInterfaceClass *k = IPMI_INTERFACE_GET_CLASS(s);

    /* Disable all interrupts */
    set_global_enables(ibs, 1 << IPMI_BMC_EVENT_LOG_BIT);

    if (k->reset) {
        k->reset(s, true);
    }
}

static void warm_reset(IPMIBmcSim *ibs,
                       uint8_t *cmd, unsigned int cmd_len,
                       RspBuffer *rsp)
{
    IPMIInterface *s = ibs->parent.intf;
    IPMIInterfaceClass *k = IPMI_INTERFACE_GET_CLASS(s);

    if (k->reset) {
        k->reset(s, false);
    }
}
static void set_acpi_power_state(IPMIBmcSim *ibs,
                                 uint8_t *cmd, unsigned int cmd_len,
                                 RspBuffer *rsp)
{
    ibs->acpi_power_state[0] = cmd[2];
    ibs->acpi_power_state[1] = cmd[3];
}

static void get_acpi_power_state(IPMIBmcSim *ibs,
                                 uint8_t *cmd, unsigned int cmd_len,
                                 RspBuffer *rsp)
{
    rsp_buffer_push(rsp, ibs->acpi_power_state[0]);
    rsp_buffer_push(rsp, ibs->acpi_power_state[1]);
}

static void get_device_guid(IPMIBmcSim *ibs,
                            uint8_t *cmd, unsigned int cmd_len,
                            RspBuffer *rsp)
{
    unsigned int i;

    /* An uninitialized uuid is all zeros, use that to know if it is set. */
    for (i = 0; i < 16; i++) {
        if (ibs->uuid.data[i]) {
            goto uuid_set;
        }
    }
    /* No uuid is set, return an error. */
    rsp_buffer_set_error(rsp, IPMI_CC_INVALID_CMD);
    return;

 uuid_set:
    for (i = 0; i < 16; i++) {
        rsp_buffer_push(rsp, ibs->uuid.data[i]);
    }
}

static void set_bmc_global_enables(IPMIBmcSim *ibs,
                                   uint8_t *cmd, unsigned int cmd_len,
                                   RspBuffer *rsp)
{
    set_global_enables(ibs, cmd[2]);
}

static void get_bmc_global_enables(IPMIBmcSim *ibs,
                                   uint8_t *cmd, unsigned int cmd_len,
                                   RspBuffer *rsp)
{
    rsp_buffer_push(rsp, ibs->bmc_global_enables);
}

static void clr_msg_flags(IPMIBmcSim *ibs,
                          uint8_t *cmd, unsigned int cmd_len,
                          RspBuffer *rsp)
{
    IPMIInterface *s = ibs->parent.intf;
    IPMIInterfaceClass *k = IPMI_INTERFACE_GET_CLASS(s);

    ibs->msg_flags &= ~cmd[2];
    k->set_atn(s, attn_set(ibs), attn_irq_enabled(ibs));
}

static void get_msg_flags(IPMIBmcSim *ibs,
                          uint8_t *cmd, unsigned int cmd_len,
                          RspBuffer *rsp)
{
    rsp_buffer_push(rsp, ibs->msg_flags);
}

static void read_evt_msg_buf(IPMIBmcSim *ibs,
                             uint8_t *cmd, unsigned int cmd_len,
                             RspBuffer *rsp)
{
    IPMIInterface *s = ibs->parent.intf;
    IPMIInterfaceClass *k = IPMI_INTERFACE_GET_CLASS(s);
    unsigned int i;

    if (!(ibs->msg_flags & IPMI_BMC_MSG_FLAG_EVT_BUF_FULL)) {
        rsp_buffer_set_error(rsp, 0x80);
        return;
    }
    for (i = 0; i < 16; i++) {
        rsp_buffer_push(rsp, ibs->evtbuf[i]);
    }
    ibs->msg_flags &= ~IPMI_BMC_MSG_FLAG_EVT_BUF_FULL;
    k->set_atn(s, attn_set(ibs), attn_irq_enabled(ibs));
}

static void get_msg(IPMIBmcSim *ibs,
                    uint8_t *cmd, unsigned int cmd_len,
                    RspBuffer *rsp)
{
    IPMIRcvBufEntry *msg;

    if (QTAILQ_EMPTY(&ibs->rcvbufs)) {
        rsp_buffer_set_error(rsp, 0x80); /* Queue empty */
        goto out;
    }
    rsp_buffer_push(rsp, 0); /* Channel 0 */
    msg = QTAILQ_FIRST(&ibs->rcvbufs);
    rsp_buffer_pushmore(rsp, msg->buf, msg->len);
    QTAILQ_REMOVE(&ibs->rcvbufs, msg, entry);
    g_free(msg);

    if (QTAILQ_EMPTY(&ibs->rcvbufs)) {
        IPMIInterface *s = ibs->parent.intf;
        IPMIInterfaceClass *k = IPMI_INTERFACE_GET_CLASS(s);

        ibs->msg_flags &= ~IPMI_BMC_MSG_FLAG_RCV_MSG_QUEUE;
        k->set_atn(s, attn_set(ibs), attn_irq_enabled(ibs));
    }

out:
    return;
}

static unsigned char
ipmb_checksum(unsigned char *data, int size, unsigned char csum)
{
    for (; size > 0; size--, data++) {
            csum += *data;
    }

    return -csum;
}

static void send_msg(IPMIBmcSim *ibs,
                     uint8_t *cmd, unsigned int cmd_len,
                     RspBuffer *rsp)
{
    IPMIInterface *s = ibs->parent.intf;
    IPMIInterfaceClass *k = IPMI_INTERFACE_GET_CLASS(s);
    IPMIRcvBufEntry *msg;
    uint8_t *buf;
    uint8_t netfn, rqLun, rsLun, rqSeq;

    if (cmd[2] != 0) {
        /* We only handle channel 0 with no options */
        rsp_buffer_set_error(rsp, IPMI_CC_INVALID_DATA_FIELD);
        return;
    }

    if (cmd_len < 10) {
        rsp_buffer_set_error(rsp, IPMI_CC_REQUEST_DATA_LENGTH_INVALID);
        return;
    }

    if (cmd[3] != 0x40) {
        /* We only emulate a MC at address 0x40. */
        rsp_buffer_set_error(rsp, 0x83); /* NAK on write */
        return;
    }

    cmd += 3; /* Skip the header. */
    cmd_len -= 3;

    /*
     * At this point we "send" the message successfully.  Any error will
     * be returned in the response.
     */
    if (ipmb_checksum(cmd, cmd_len, 0) != 0 ||
        cmd[3] != 0x20) { /* Improper response address */
        return; /* No response */
    }

    netfn = cmd[1] >> 2;
    rqLun = cmd[4] & 0x3;
    rsLun = cmd[1] & 0x3;
    rqSeq = cmd[4] >> 2;

    if (rqLun != 2) {
        /* We only support LUN 2 coming back to us. */
        return;
    }

    msg = g_malloc(sizeof(*msg));
    msg->buf[0] = ((netfn | 1) << 2) | rqLun; /* NetFN, and make a response */
    msg->buf[1] = ipmb_checksum(msg->buf, 1, 0);
    msg->buf[2] = cmd[0]; /* rsSA */
    msg->buf[3] = (rqSeq << 2) | rsLun;
    msg->buf[4] = cmd[5]; /* Cmd */
    msg->buf[5] = 0; /* Completion Code */
    msg->len = 6;

    if ((cmd[1] >> 2) != IPMI_NETFN_APP || cmd[5] != IPMI_CMD_GET_DEVICE_ID) {
        /* Not a command we handle. */
        msg->buf[5] = IPMI_CC_INVALID_CMD;
        goto end_msg;
    }

    buf = msg->buf + msg->len; /* After the CC */
    buf[0] = 0;
    buf[1] = 0;
    buf[2] = 0;
    buf[3] = 0;
    buf[4] = 0x51;
    buf[5] = 0;
    buf[6] = 0;
    buf[7] = 0;
    buf[8] = 0;
    buf[9] = 0;
    buf[10] = 0;
    msg->len += 11;

 end_msg:
    msg->buf[msg->len] = ipmb_checksum(msg->buf, msg->len, 0);
    msg->len++;
    QTAILQ_INSERT_TAIL(&ibs->rcvbufs, msg, entry);
    ibs->msg_flags |= IPMI_BMC_MSG_FLAG_RCV_MSG_QUEUE;
    k->set_atn(s, 1, attn_irq_enabled(ibs));
}

static void do_watchdog_reset(IPMIBmcSim *ibs)
{
    if (IPMI_BMC_WATCHDOG_GET_ACTION(ibs) ==
        IPMI_BMC_WATCHDOG_ACTION_NONE) {
        ibs->watchdog_running = 0;
        return;
    }
    ibs->watchdog_preaction_ran = 0;


    /* Timeout is in tenths of a second, offset is in seconds */
    ibs->watchdog_expiry = ipmi_getmonotime();
    ibs->watchdog_expiry += ibs->watchdog_timeout * 100000000LL;
    if (IPMI_BMC_WATCHDOG_GET_PRE_ACTION(ibs) != IPMI_BMC_WATCHDOG_PRE_NONE) {
        ibs->watchdog_expiry -= ibs->watchdog_pretimeout * 1000000000LL;
    }
    ibs->watchdog_running = 1;
}

static void reset_watchdog_timer(IPMIBmcSim *ibs,
                                 uint8_t *cmd, unsigned int cmd_len,
                                 RspBuffer *rsp)
{
    if (!ibs->watchdog_initialized) {
        rsp_buffer_set_error(rsp, 0x80);
        return;
    }
    do_watchdog_reset(ibs);
}

static void set_watchdog_timer(IPMIBmcSim *ibs,
                               uint8_t *cmd, unsigned int cmd_len,
                               RspBuffer *rsp)
{
    IPMIInterface *s = ibs->parent.intf;
    IPMIInterfaceClass *k = IPMI_INTERFACE_GET_CLASS(s);
    unsigned int val;

    val = cmd[2] & 0x7; /* Validate use */
    if (val == 0 || val > 5) {
        rsp_buffer_set_error(rsp, IPMI_CC_INVALID_DATA_FIELD);
        return;
    }
    val = cmd[3] & 0x7; /* Validate action */
    switch (val) {
    case IPMI_BMC_WATCHDOG_ACTION_NONE:
        break;

    case IPMI_BMC_WATCHDOG_ACTION_RESET:
        rsp_buffer_set_error(rsp, k->do_hw_op(s, IPMI_RESET_CHASSIS, 1));
        break;

    case IPMI_BMC_WATCHDOG_ACTION_POWER_DOWN:
        rsp_buffer_set_error(rsp, k->do_hw_op(s, IPMI_POWEROFF_CHASSIS, 1));
        break;

    case IPMI_BMC_WATCHDOG_ACTION_POWER_CYCLE:
        rsp_buffer_set_error(rsp, k->do_hw_op(s, IPMI_POWERCYCLE_CHASSIS, 1));
        break;

    default:
        rsp_buffer_set_error(rsp, IPMI_CC_INVALID_DATA_FIELD);
    }
    if (rsp->buffer[2]) {
        rsp_buffer_set_error(rsp, IPMI_CC_INVALID_DATA_FIELD);
        return;
    }

    val = (cmd[3] >> 4) & 0x7; /* Validate preaction */
    switch (val) {
    case IPMI_BMC_WATCHDOG_PRE_MSG_INT:
    case IPMI_BMC_WATCHDOG_PRE_NONE:
        break;

    case IPMI_BMC_WATCHDOG_PRE_NMI:
        if (k->do_hw_op(s, IPMI_SEND_NMI, 1)) {
            /* NMI not supported. */
            rsp_buffer_set_error(rsp, IPMI_CC_INVALID_DATA_FIELD);
            return;
        }
        break;

    default:
        /* We don't support PRE_SMI */
        rsp_buffer_set_error(rsp, IPMI_CC_INVALID_DATA_FIELD);
        return;
    }

    ibs->watchdog_initialized = 1;
    ibs->watchdog_use = cmd[2] & IPMI_BMC_WATCHDOG_USE_MASK;
    ibs->watchdog_action = cmd[3] & IPMI_BMC_WATCHDOG_ACTION_MASK;
    ibs->watchdog_pretimeout = cmd[4];
    ibs->watchdog_expired &= ~cmd[5];
    ibs->watchdog_timeout = cmd[6] | (((uint16_t) cmd[7]) << 8);
    if (ibs->watchdog_running & IPMI_BMC_WATCHDOG_GET_DONT_STOP(ibs)) {
        do_watchdog_reset(ibs);
    } else {
        ibs->watchdog_running = 0;
    }
}

static void get_watchdog_timer(IPMIBmcSim *ibs,
                               uint8_t *cmd, unsigned int cmd_len,
                               RspBuffer *rsp)
{
    rsp_buffer_push(rsp, ibs->watchdog_use);
    rsp_buffer_push(rsp, ibs->watchdog_action);
    rsp_buffer_push(rsp, ibs->watchdog_pretimeout);
    rsp_buffer_push(rsp, ibs->watchdog_expired);
    rsp_buffer_push(rsp, ibs->watchdog_timeout & 0xff);
    rsp_buffer_push(rsp, (ibs->watchdog_timeout >> 8) & 0xff);
    if (ibs->watchdog_running) {
        long timeout;
        timeout = ((ibs->watchdog_expiry - ipmi_getmonotime() + 50000000)
                   / 100000000);
        rsp_buffer_push(rsp, timeout & 0xff);
        rsp_buffer_push(rsp, (timeout >> 8) & 0xff);
    } else {
        rsp_buffer_push(rsp, 0);
        rsp_buffer_push(rsp, 0);
    }
}

static void get_sdr_rep_info(IPMIBmcSim *ibs,
                             uint8_t *cmd, unsigned int cmd_len,
                             RspBuffer *rsp)
{
    unsigned int i;

    rsp_buffer_push(rsp, 0x51); /* Conform to IPMI 1.5 spec */
    rsp_buffer_push(rsp, ibs->sdr.next_rec_id & 0xff);
    rsp_buffer_push(rsp, (ibs->sdr.next_rec_id >> 8) & 0xff);
    rsp_buffer_push(rsp, (MAX_SDR_SIZE - ibs->sdr.next_free) & 0xff);
    rsp_buffer_push(rsp, ((MAX_SDR_SIZE - ibs->sdr.next_free) >> 8) & 0xff);
    for (i = 0; i < 4; i++) {
        rsp_buffer_push(rsp, ibs->sdr.last_addition[i]);
    }
    for (i = 0; i < 4; i++) {
        rsp_buffer_push(rsp, ibs->sdr.last_clear[i]);
    }
    /* Only modal support, reserve supported */
    rsp_buffer_push(rsp, (ibs->sdr.overflow << 7) | 0x22);
}

static void reserve_sdr_rep(IPMIBmcSim *ibs,
                            uint8_t *cmd, unsigned int cmd_len,
                            RspBuffer *rsp)
{
    rsp_buffer_push(rsp, ibs->sdr.reservation & 0xff);
    rsp_buffer_push(rsp, (ibs->sdr.reservation >> 8) & 0xff);
}

static void get_sdr(IPMIBmcSim *ibs,
                    uint8_t *cmd, unsigned int cmd_len,
                    RspBuffer *rsp)
{
    unsigned int pos;
    uint16_t nextrec;
    struct ipmi_sdr_header *sdrh;

    if (cmd[6]) {
        if ((cmd[2] | (cmd[3] << 8)) != ibs->sdr.reservation) {
            rsp_buffer_set_error(rsp, IPMI_CC_INVALID_RESERVATION);
            return;
        }
    }

    pos = 0;
    if (sdr_find_entry(&ibs->sdr, cmd[4] | (cmd[5] << 8),
                       &pos, &nextrec)) {
        rsp_buffer_set_error(rsp, IPMI_CC_REQ_ENTRY_NOT_PRESENT);
        return;
    }

    sdrh = (struct ipmi_sdr_header *) &ibs->sdr.sdr[pos];

    if (cmd[6] > ipmi_sdr_length(sdrh)) {
        rsp_buffer_set_error(rsp, IPMI_CC_PARM_OUT_OF_RANGE);
        return;
    }

    rsp_buffer_push(rsp, nextrec & 0xff);
    rsp_buffer_push(rsp, (nextrec >> 8) & 0xff);

    if (cmd[7] == 0xff) {
        cmd[7] = ipmi_sdr_length(sdrh) - cmd[6];
    }

    if ((cmd[7] + rsp->len) > sizeof(rsp->buffer)) {
        rsp_buffer_set_error(rsp, IPMI_CC_CANNOT_RETURN_REQ_NUM_BYTES);
        return;
    }

    rsp_buffer_pushmore(rsp, ibs->sdr.sdr + pos + cmd[6], cmd[7]);
}

static void add_sdr(IPMIBmcSim *ibs,
                    uint8_t *cmd, unsigned int cmd_len,
                    RspBuffer *rsp)
{
    uint16_t recid;
    struct ipmi_sdr_header *sdrh = (struct ipmi_sdr_header *) cmd + 2;

    if (sdr_add_entry(ibs, sdrh, cmd_len - 2, &recid)) {
        rsp_buffer_set_error(rsp, IPMI_CC_INVALID_DATA_FIELD);
        return;
    }
    rsp_buffer_push(rsp, recid & 0xff);
    rsp_buffer_push(rsp, (recid >> 8) & 0xff);
}

static void clear_sdr_rep(IPMIBmcSim *ibs,
                          uint8_t *cmd, unsigned int cmd_len,
                          RspBuffer *rsp)
{
    if ((cmd[2] | (cmd[3] << 8)) != ibs->sdr.reservation) {
        rsp_buffer_set_error(rsp, IPMI_CC_INVALID_RESERVATION);
        return;
    }

    if (cmd[4] != 'C' || cmd[5] != 'L' || cmd[6] != 'R') {
        rsp_buffer_set_error(rsp, IPMI_CC_INVALID_DATA_FIELD);
        return;
    }
    if (cmd[7] == 0xaa) {
        ibs->sdr.next_free = 0;
        ibs->sdr.overflow = 0;
        set_timestamp(ibs, ibs->sdr.last_clear);
        rsp_buffer_push(rsp, 1); /* Erasure complete */
        sdr_inc_reservation(&ibs->sdr);
    } else if (cmd[7] == 0) {
        rsp_buffer_push(rsp, 1); /* Erasure complete */
    } else {
        rsp_buffer_set_error(rsp, IPMI_CC_INVALID_DATA_FIELD);
        return;
    }
}

static void get_sel_info(IPMIBmcSim *ibs,
                         uint8_t *cmd, unsigned int cmd_len,
                         RspBuffer *rsp)
{
    unsigned int i, val;

    rsp_buffer_push(rsp, 0x51); /* Conform to IPMI 1.5 */
    rsp_buffer_push(rsp, ibs->sel.next_free & 0xff);
    rsp_buffer_push(rsp, (ibs->sel.next_free >> 8) & 0xff);
    val = (MAX_SEL_SIZE - ibs->sel.next_free) * 16;
    rsp_buffer_push(rsp, val & 0xff);
    rsp_buffer_push(rsp, (val >> 8) & 0xff);
    for (i = 0; i < 4; i++) {
        rsp_buffer_push(rsp, ibs->sel.last_addition[i]);
    }
    for (i = 0; i < 4; i++) {
        rsp_buffer_push(rsp, ibs->sel.last_clear[i]);
    }
    /* Only support Reserve SEL */
    rsp_buffer_push(rsp, (ibs->sel.overflow << 7) | 0x02);
}

static void get_fru_area_info(IPMIBmcSim *ibs,
                         uint8_t *cmd, unsigned int cmd_len,
                         RspBuffer *rsp)
{
    uint8_t fruid;
    uint16_t fru_entry_size;

    fruid = cmd[2];

    if (fruid >= ibs->fru.nentries) {
        rsp_buffer_set_error(rsp, IPMI_CC_INVALID_DATA_FIELD);
        return;
    }

    fru_entry_size = ibs->fru.areasize;

    rsp_buffer_push(rsp, fru_entry_size & 0xff);
    rsp_buffer_push(rsp, fru_entry_size >> 8 & 0xff);
    rsp_buffer_push(rsp, 0x0);
}

static void read_fru_data(IPMIBmcSim *ibs,
                         uint8_t *cmd, unsigned int cmd_len,
                         RspBuffer *rsp)
{
    uint8_t fruid;
    uint16_t offset;
    int i;
    uint8_t *fru_entry;
    unsigned int count;

    fruid = cmd[2];
    offset = (cmd[3] | cmd[4] << 8);

    if (fruid >= ibs->fru.nentries) {
        rsp_buffer_set_error(rsp, IPMI_CC_INVALID_DATA_FIELD);
        return;
    }

    if (offset >= ibs->fru.areasize - 1) {
        rsp_buffer_set_error(rsp, IPMI_CC_INVALID_DATA_FIELD);
        return;
    }

    fru_entry = &ibs->fru.data[fruid * ibs->fru.areasize];

    count = MIN(cmd[5], ibs->fru.areasize - offset);

    rsp_buffer_push(rsp, count & 0xff);
    for (i = 0; i < count; i++) {
        rsp_buffer_push(rsp, fru_entry[offset + i]);
    }
}

static void write_fru_data(IPMIBmcSim *ibs,
                         uint8_t *cmd, unsigned int cmd_len,
                         RspBuffer *rsp)
{
    uint8_t fruid;
    uint16_t offset;
    uint8_t *fru_entry;
    unsigned int count;

    fruid = cmd[2];
    offset = (cmd[3] | cmd[4] << 8);

    if (fruid >= ibs->fru.nentries) {
        rsp_buffer_set_error(rsp, IPMI_CC_INVALID_DATA_FIELD);
        return;
    }

    if (offset >= ibs->fru.areasize - 1) {
        rsp_buffer_set_error(rsp, IPMI_CC_INVALID_DATA_FIELD);
        return;
    }

    fru_entry = &ibs->fru.data[fruid * ibs->fru.areasize];

    count = MIN(cmd_len - 5, ibs->fru.areasize - offset);

    memcpy(fru_entry + offset, cmd + 5, count);

    rsp_buffer_push(rsp, count & 0xff);
}

static void reserve_sel(IPMIBmcSim *ibs,
                        uint8_t *cmd, unsigned int cmd_len,
                        RspBuffer *rsp)
{
    rsp_buffer_push(rsp, ibs->sel.reservation & 0xff);
    rsp_buffer_push(rsp, (ibs->sel.reservation >> 8) & 0xff);
}

static void get_sel_entry(IPMIBmcSim *ibs,
                          uint8_t *cmd, unsigned int cmd_len,
                          RspBuffer *rsp)
{
    unsigned int val;

    if (cmd[6]) {
        if ((cmd[2] | (cmd[3] << 8)) != ibs->sel.reservation) {
            rsp_buffer_set_error(rsp, IPMI_CC_INVALID_RESERVATION);
            return;
        }
    }
    if (ibs->sel.next_free == 0) {
        rsp_buffer_set_error(rsp, IPMI_CC_REQ_ENTRY_NOT_PRESENT);
        return;
    }
    if (cmd[6] > 15) {
        rsp_buffer_set_error(rsp, IPMI_CC_INVALID_DATA_FIELD);
        return;
    }
    if (cmd[7] == 0xff) {
        cmd[7] = 16;
    } else if ((cmd[7] + cmd[6]) > 16) {
        rsp_buffer_set_error(rsp, IPMI_CC_INVALID_DATA_FIELD);
        return;
    } else {
        cmd[7] += cmd[6];
    }

    val = cmd[4] | (cmd[5] << 8);
    if (val == 0xffff) {
        val = ibs->sel.next_free - 1;
    } else if (val >= ibs->sel.next_free) {
        rsp_buffer_set_error(rsp, IPMI_CC_REQ_ENTRY_NOT_PRESENT);
        return;
    }
    if ((val + 1) == ibs->sel.next_free) {
        rsp_buffer_push(rsp, 0xff);
        rsp_buffer_push(rsp, 0xff);
    } else {
        rsp_buffer_push(rsp, (val + 1) & 0xff);
        rsp_buffer_push(rsp, ((val + 1) >> 8) & 0xff);
    }
    for (; cmd[6] < cmd[7]; cmd[6]++) {
        rsp_buffer_push(rsp, ibs->sel.sel[val][cmd[6]]);
    }
}

static void add_sel_entry(IPMIBmcSim *ibs,
                          uint8_t *cmd, unsigned int cmd_len,
                          RspBuffer *rsp)
{
    if (sel_add_event(ibs, cmd + 2)) {
        rsp_buffer_set_error(rsp, IPMI_CC_OUT_OF_SPACE);
        return;
    }
    /* sel_add_event fills in the record number. */
    rsp_buffer_push(rsp, cmd[2]);
    rsp_buffer_push(rsp, cmd[3]);
}

static void clear_sel(IPMIBmcSim *ibs,
                      uint8_t *cmd, unsigned int cmd_len,
                      RspBuffer *rsp)
{
    if ((cmd[2] | (cmd[3] << 8)) != ibs->sel.reservation) {
        rsp_buffer_set_error(rsp, IPMI_CC_INVALID_RESERVATION);
        return;
    }

    if (cmd[4] != 'C' || cmd[5] != 'L' || cmd[6] != 'R') {
        rsp_buffer_set_error(rsp, IPMI_CC_INVALID_DATA_FIELD);
        return;
    }
    if (cmd[7] == 0xaa) {
        ibs->sel.next_free = 0;
        ibs->sel.overflow = 0;
        set_timestamp(ibs, ibs->sdr.last_clear);
        rsp_buffer_push(rsp, 1); /* Erasure complete */
        sel_inc_reservation(&ibs->sel);
    } else if (cmd[7] == 0) {
        rsp_buffer_push(rsp, 1); /* Erasure complete */
    } else {
        rsp_buffer_set_error(rsp, IPMI_CC_INVALID_DATA_FIELD);
        return;
    }
}

static void get_sel_time(IPMIBmcSim *ibs,
                         uint8_t *cmd, unsigned int cmd_len,
                         RspBuffer *rsp)
{
    uint32_t val;
    struct ipmi_time now;

    ipmi_gettime(&now);
    val = now.tv_sec + ibs->sel.time_offset;
    rsp_buffer_push(rsp, val & 0xff);
    rsp_buffer_push(rsp, (val >> 8) & 0xff);
    rsp_buffer_push(rsp, (val >> 16) & 0xff);
    rsp_buffer_push(rsp, (val >> 24) & 0xff);
}

static void set_sel_time(IPMIBmcSim *ibs,
                         uint8_t *cmd, unsigned int cmd_len,
                         RspBuffer *rsp)
{
    uint32_t val;
    struct ipmi_time now;

    val = cmd[2] | (cmd[3] << 8) | (cmd[4] << 16) | (cmd[5] << 24);
    ipmi_gettime(&now);
    ibs->sel.time_offset = now.tv_sec - ((long) val);
}

static void platform_event_msg(IPMIBmcSim *ibs,
                               uint8_t *cmd, unsigned int cmd_len,
                               RspBuffer *rsp)
{
    uint8_t event[16];

    event[2] = 2; /* System event record */
    event[7] = cmd[2]; /* Generator ID */
    event[8] = 0;
    event[9] = cmd[3]; /* EvMRev */
    event[10] = cmd[4]; /* Sensor type */
    event[11] = cmd[5]; /* Sensor number */
    event[12] = cmd[6]; /* Event dir / Event type */
    event[13] = cmd[7]; /* Event data 1 */
    event[14] = cmd[8]; /* Event data 2 */
    event[15] = cmd[9]; /* Event data 3 */

    if (sel_add_event(ibs, event)) {
        rsp_buffer_set_error(rsp, IPMI_CC_OUT_OF_SPACE);
    }
}

static void set_sensor_evt_enable(IPMIBmcSim *ibs,
                                  uint8_t *cmd, unsigned int cmd_len,
                                  RspBuffer *rsp)
{
    IPMISensor *sens;

    if ((cmd[2] >= MAX_SENSORS) ||
            !IPMI_SENSOR_GET_PRESENT(ibs->sensors + cmd[2])) {
        rsp_buffer_set_error(rsp, IPMI_CC_REQ_ENTRY_NOT_PRESENT);
        return;
    }
    sens = ibs->sensors + cmd[2];
    switch ((cmd[3] >> 4) & 0x3) {
    case 0: /* Do not change */
        break;
    case 1: /* Enable bits */
        if (cmd_len > 4) {
            sens->assert_enable |= cmd[4];
        }
        if (cmd_len > 5) {
            sens->assert_enable |= cmd[5] << 8;
        }
        if (cmd_len > 6) {
            sens->deassert_enable |= cmd[6];
        }
        if (cmd_len > 7) {
            sens->deassert_enable |= cmd[7] << 8;
        }
        break;
    case 2: /* Disable bits */
        if (cmd_len > 4) {
            sens->assert_enable &= ~cmd[4];
        }
        if (cmd_len > 5) {
            sens->assert_enable &= ~(cmd[5] << 8);
        }
        if (cmd_len > 6) {
            sens->deassert_enable &= ~cmd[6];
        }
        if (cmd_len > 7) {
            sens->deassert_enable &= ~(cmd[7] << 8);
        }
        break;
    case 3:
        rsp_buffer_set_error(rsp, IPMI_CC_INVALID_DATA_FIELD);
        return;
    }
    IPMI_SENSOR_SET_RET_STATUS(sens, cmd[3]);
}

static void get_sensor_evt_enable(IPMIBmcSim *ibs,
                                  uint8_t *cmd, unsigned int cmd_len,
                                  RspBuffer *rsp)
{
    IPMISensor *sens;

    if ((cmd[2] >= MAX_SENSORS) ||
        !IPMI_SENSOR_GET_PRESENT(ibs->sensors + cmd[2])) {
        rsp_buffer_set_error(rsp, IPMI_CC_REQ_ENTRY_NOT_PRESENT);
        return;
    }
    sens = ibs->sensors + cmd[2];
    rsp_buffer_push(rsp, IPMI_SENSOR_GET_RET_STATUS(sens));
    rsp_buffer_push(rsp, sens->assert_enable & 0xff);
    rsp_buffer_push(rsp, (sens->assert_enable >> 8) & 0xff);
    rsp_buffer_push(rsp, sens->deassert_enable & 0xff);
    rsp_buffer_push(rsp, (sens->deassert_enable >> 8) & 0xff);
}

static void rearm_sensor_evts(IPMIBmcSim *ibs,
                              uint8_t *cmd, unsigned int cmd_len,
                              RspBuffer *rsp)
{
    IPMISensor *sens;

    if ((cmd[2] >= MAX_SENSORS) ||
        !IPMI_SENSOR_GET_PRESENT(ibs->sensors + cmd[2])) {
        rsp_buffer_set_error(rsp, IPMI_CC_REQ_ENTRY_NOT_PRESENT);
        return;
    }
    sens = ibs->sensors + cmd[2];

    if ((cmd[3] & 0x80) == 0) {
        /* Just clear everything */
        sens->states = 0;
        return;
    }
}

static void get_sensor_evt_status(IPMIBmcSim *ibs,
                                  uint8_t *cmd, unsigned int cmd_len,
                                  RspBuffer *rsp)
{
    IPMISensor *sens;

    if ((cmd[2] >= MAX_SENSORS) ||
        !IPMI_SENSOR_GET_PRESENT(ibs->sensors + cmd[2])) {
        rsp_buffer_set_error(rsp, IPMI_CC_REQ_ENTRY_NOT_PRESENT);
        return;
    }
    sens = ibs->sensors + cmd[2];
    rsp_buffer_push(rsp, sens->reading);
    rsp_buffer_push(rsp, IPMI_SENSOR_GET_RET_STATUS(sens));
    rsp_buffer_push(rsp, sens->assert_states & 0xff);
    rsp_buffer_push(rsp, (sens->assert_states >> 8) & 0xff);
    rsp_buffer_push(rsp, sens->deassert_states & 0xff);
    rsp_buffer_push(rsp, (sens->deassert_states >> 8) & 0xff);
}

static void get_sensor_reading(IPMIBmcSim *ibs,
                               uint8_t *cmd, unsigned int cmd_len,
                               RspBuffer *rsp)
{
    IPMISensor *sens;

    if ((cmd[2] >= MAX_SENSORS) ||
            !IPMI_SENSOR_GET_PRESENT(ibs->sensors + cmd[2])) {
        rsp_buffer_set_error(rsp, IPMI_CC_REQ_ENTRY_NOT_PRESENT);
        return;
    }
    sens = ibs->sensors + cmd[2];
    rsp_buffer_push(rsp, sens->reading);
    rsp_buffer_push(rsp, IPMI_SENSOR_GET_RET_STATUS(sens));
    rsp_buffer_push(rsp, sens->states & 0xff);
    if (IPMI_SENSOR_IS_DISCRETE(sens)) {
        rsp_buffer_push(rsp, (sens->states >> 8) & 0xff);
    }
}

static void set_sensor_type(IPMIBmcSim *ibs,
                            uint8_t *cmd, unsigned int cmd_len,
                            RspBuffer *rsp)
{
    IPMISensor *sens;


    if ((cmd[2] >= MAX_SENSORS) ||
            !IPMI_SENSOR_GET_PRESENT(ibs->sensors + cmd[2])) {
        rsp_buffer_set_error(rsp, IPMI_CC_REQ_ENTRY_NOT_PRESENT);
        return;
    }
    sens = ibs->sensors + cmd[2];
    sens->sensor_type = cmd[3];
    sens->evt_reading_type_code = cmd[4] & 0x7f;
}

static void get_sensor_type(IPMIBmcSim *ibs,
                            uint8_t *cmd, unsigned int cmd_len,
                            RspBuffer *rsp)
{
    IPMISensor *sens;


    if ((cmd[2] >= MAX_SENSORS) ||
            !IPMI_SENSOR_GET_PRESENT(ibs->sensors + cmd[2])) {
        rsp_buffer_set_error(rsp, IPMI_CC_REQ_ENTRY_NOT_PRESENT);
        return;
    }
    sens = ibs->sensors + cmd[2];
    rsp_buffer_push(rsp, sens->sensor_type);
    rsp_buffer_push(rsp, sens->evt_reading_type_code);
}

/*
 * bytes   parameter
 *    1    sensor number
 *    2    operation (see below for bits meaning)
 *    3    sensor reading
 *  4:5    assertion states (optional)
 *  6:7    deassertion states (optional)
 *  8:10   event data 1,2,3 (optional)
 */
static void set_sensor_reading(IPMIBmcSim *ibs,
                               uint8_t *cmd, unsigned int cmd_len,
                               RspBuffer *rsp)
{
    IPMISensor *sens;
    uint8_t evd1 = 0;
    uint8_t evd2 = 0;
    uint8_t evd3 = 0;
    uint8_t new_reading = 0;
    uint16_t new_assert_states = 0;
    uint16_t new_deassert_states = 0;
    bool change_reading = false;
    bool change_assert = false;
    bool change_deassert = false;
    enum {
        SENSOR_GEN_EVENT_NONE,
        SENSOR_GEN_EVENT_DATA,
        SENSOR_GEN_EVENT_BMC,
    } do_gen_event = SENSOR_GEN_EVENT_NONE;

    if ((cmd[2] >= MAX_SENSORS) ||
            !IPMI_SENSOR_GET_PRESENT(ibs->sensors + cmd[2])) {
        rsp_buffer_set_error(rsp, IPMI_CC_REQ_ENTRY_NOT_PRESENT);
        return;
    }

    sens = ibs->sensors + cmd[2];

    /* [1:0] Sensor Reading operation */
    switch ((cmd[3]) & 0x3) {
    case 0: /* Do not change */
        break;
    case 1: /* write given value to sensor reading byte */
        new_reading = cmd[4];
        if (sens->reading != new_reading) {
            change_reading = true;
        }
        break;
    case 2:
    case 3:
        rsp_buffer_set_error(rsp, IPMI_CC_INVALID_DATA_FIELD);
        return;
    }

    /* [3:2] Deassertion bits operation */
    switch ((cmd[3] >> 2) & 0x3) {
    case 0: /* Do not change */
        break;
    case 1: /* write given value */
        if (cmd_len > 7) {
            new_deassert_states = cmd[7];
            change_deassert = true;
        }
        if (cmd_len > 8) {
            new_deassert_states |= (cmd[8] << 8);
        }
        break;

    case 2: /* mask on */
        if (cmd_len > 7) {
            new_deassert_states = (sens->deassert_states | cmd[7]);
            change_deassert = true;
        }
        if (cmd_len > 8) {
            new_deassert_states |= (sens->deassert_states | (cmd[8] << 8));
        }
        break;

    case 3: /* mask off */
        if (cmd_len > 7) {
            new_deassert_states = (sens->deassert_states & cmd[7]);
            change_deassert = true;
        }
        if (cmd_len > 8) {
            new_deassert_states |= (sens->deassert_states & (cmd[8] << 8));
        }
        break;
    }

    if (change_deassert && (new_deassert_states == sens->deassert_states)) {
        change_deassert = false;
    }

    /* [5:4] Assertion bits operation */
    switch ((cmd[3] >> 4) & 0x3) {
    case 0: /* Do not change */
        break;
    case 1: /* write given value */
        if (cmd_len > 5) {
            new_assert_states = cmd[5];
            change_assert = true;
        }
        if (cmd_len > 6) {
            new_assert_states |= (cmd[6] << 8);
        }
        break;

    case 2: /* mask on */
        if (cmd_len > 5) {
            new_assert_states = (sens->assert_states | cmd[5]);
            change_assert = true;
        }
        if (cmd_len > 6) {
            new_assert_states |= (sens->assert_states | (cmd[6] << 8));
        }
        break;

    case 3: /* mask off */
        if (cmd_len > 5) {
            new_assert_states = (sens->assert_states & cmd[5]);
            change_assert = true;
        }
        if (cmd_len > 6) {
            new_assert_states |= (sens->assert_states & (cmd[6] << 8));
        }
        break;
    }

    if (change_assert && (new_assert_states == sens->assert_states)) {
        change_assert = false;
    }

    if (cmd_len > 9) {
        evd1 = cmd[9];
    }
    if (cmd_len > 10) {
        evd2 = cmd[10];
    }
    if (cmd_len > 11) {
        evd3 = cmd[11];
    }

    /* [7:6] Event Data Bytes operation */
    switch ((cmd[3] >> 6) & 0x3) {
    case 0: /*
             * Don’t use Event Data bytes from this command. BMC will
             * generate it's own Event Data bytes based on its sensor
             * implementation.
             */
        evd1 = evd2 = evd3 = 0x0;
        do_gen_event = SENSOR_GEN_EVENT_BMC;
        break;
    case 1: /*
             * Write given values to event data bytes including bits
             * [3:0] Event Data 1.
             */
        do_gen_event = SENSOR_GEN_EVENT_DATA;
        break;
    case 2: /*
             * Write given values to event data bytes excluding bits
             * [3:0] Event Data 1.
             */
        evd1 &= 0xf0;
        do_gen_event = SENSOR_GEN_EVENT_DATA;
        break;
    case 3:
        rsp_buffer_set_error(rsp, IPMI_CC_INVALID_DATA_FIELD);
        return;
    }

    /*
     * Event Data Bytes operation and parameter are inconsistent. The
     * Specs are not clear on that topic but generating an error seems
     * correct.
     */
    if (do_gen_event == SENSOR_GEN_EVENT_DATA && cmd_len < 10) {
        rsp_buffer_set_error(rsp, IPMI_CC_INVALID_DATA_FIELD);
        return;
    }

    /* commit values */
    if (change_reading) {
        sens->reading = new_reading;
    }

    if (change_assert) {
        sens->assert_states = new_assert_states;
    }

    if (change_deassert) {
        sens->deassert_states = new_deassert_states;
    }

    /* TODO: handle threshold sensor */
    if (!IPMI_SENSOR_IS_DISCRETE(sens)) {
        return;
    }

    switch (do_gen_event) {
    case SENSOR_GEN_EVENT_DATA: {
        unsigned int bit = evd1 & 0xf;
        uint16_t mask = (1 << bit);

        if (sens->assert_states & mask & sens->assert_enable) {
            gen_event(ibs, cmd[2], 0, evd1, evd2, evd3);
        }

        if (sens->deassert_states & mask & sens->deassert_enable) {
            gen_event(ibs, cmd[2], 1, evd1, evd2, evd3);
        }
        break;
    }
    case SENSOR_GEN_EVENT_BMC:
        /*
         * TODO: generate event and event data bytes depending on the
         * sensor
         */
        break;
    case SENSOR_GEN_EVENT_NONE:
        break;
    }
}

static const IPMICmdHandler chassis_cmds[] = {
    [IPMI_CMD_GET_CHASSIS_CAPABILITIES] = { chassis_capabilities },
    [IPMI_CMD_GET_CHASSIS_STATUS] = { chassis_status },
    [IPMI_CMD_CHASSIS_CONTROL] = { chassis_control, 3 },
    [IPMI_CMD_GET_SYS_RESTART_CAUSE] = { chassis_get_sys_restart_cause }
};
static const IPMINetfn chassis_netfn = {
    .cmd_nums = ARRAY_SIZE(chassis_cmds),
    .cmd_handlers = chassis_cmds
};

static const IPMICmdHandler sensor_event_cmds[] = {
    [IPMI_CMD_PLATFORM_EVENT_MSG] = { platform_event_msg, 10 },
    [IPMI_CMD_SET_SENSOR_EVT_ENABLE] = { set_sensor_evt_enable, 4 },
    [IPMI_CMD_GET_SENSOR_EVT_ENABLE] = { get_sensor_evt_enable, 3 },
    [IPMI_CMD_REARM_SENSOR_EVTS] = { rearm_sensor_evts, 4 },
    [IPMI_CMD_GET_SENSOR_EVT_STATUS] = { get_sensor_evt_status, 3 },
    [IPMI_CMD_GET_SENSOR_READING] = { get_sensor_reading, 3 },
    [IPMI_CMD_SET_SENSOR_TYPE] = { set_sensor_type, 5 },
    [IPMI_CMD_GET_SENSOR_TYPE] = { get_sensor_type, 3 },
    [IPMI_CMD_SET_SENSOR_READING] = { set_sensor_reading, 5 },
};
static const IPMINetfn sensor_event_netfn = {
    .cmd_nums = ARRAY_SIZE(sensor_event_cmds),
    .cmd_handlers = sensor_event_cmds
};

static const IPMICmdHandler app_cmds[] = {
    [IPMI_CMD_GET_DEVICE_ID] = { get_device_id },
    [IPMI_CMD_COLD_RESET] = { cold_reset },
    [IPMI_CMD_WARM_RESET] = { warm_reset },
    [IPMI_CMD_SET_ACPI_POWER_STATE] = { set_acpi_power_state, 4 },
    [IPMI_CMD_GET_ACPI_POWER_STATE] = { get_acpi_power_state },
    [IPMI_CMD_GET_DEVICE_GUID] = { get_device_guid },
    [IPMI_CMD_SET_BMC_GLOBAL_ENABLES] = { set_bmc_global_enables, 3 },
    [IPMI_CMD_GET_BMC_GLOBAL_ENABLES] = { get_bmc_global_enables },
    [IPMI_CMD_CLR_MSG_FLAGS] = { clr_msg_flags, 3 },
    [IPMI_CMD_GET_MSG_FLAGS] = { get_msg_flags },
    [IPMI_CMD_GET_MSG] = { get_msg },
    [IPMI_CMD_SEND_MSG] = { send_msg, 3 },
    [IPMI_CMD_READ_EVT_MSG_BUF] = { read_evt_msg_buf },
    [IPMI_CMD_RESET_WATCHDOG_TIMER] = { reset_watchdog_timer },
    [IPMI_CMD_SET_WATCHDOG_TIMER] = { set_watchdog_timer, 8 },
    [IPMI_CMD_GET_WATCHDOG_TIMER] = { get_watchdog_timer },
};
static const IPMINetfn app_netfn = {
    .cmd_nums = ARRAY_SIZE(app_cmds),
    .cmd_handlers = app_cmds
};

static const IPMICmdHandler storage_cmds[] = {
    [IPMI_CMD_GET_FRU_AREA_INFO] = { get_fru_area_info, 3 },
    [IPMI_CMD_READ_FRU_DATA] = { read_fru_data, 5 },
    [IPMI_CMD_WRITE_FRU_DATA] = { write_fru_data, 5 },
    [IPMI_CMD_GET_SDR_REP_INFO] = { get_sdr_rep_info },
    [IPMI_CMD_RESERVE_SDR_REP] = { reserve_sdr_rep },
    [IPMI_CMD_GET_SDR] = { get_sdr, 8 },
    [IPMI_CMD_ADD_SDR] = { add_sdr },
    [IPMI_CMD_CLEAR_SDR_REP] = { clear_sdr_rep, 8 },
    [IPMI_CMD_GET_SEL_INFO] = { get_sel_info },
    [IPMI_CMD_RESERVE_SEL] = { reserve_sel },
    [IPMI_CMD_GET_SEL_ENTRY] = { get_sel_entry, 8 },
    [IPMI_CMD_ADD_SEL_ENTRY] = { add_sel_entry, 18 },
    [IPMI_CMD_CLEAR_SEL] = { clear_sel, 8 },
    [IPMI_CMD_GET_SEL_TIME] = { get_sel_time },
    [IPMI_CMD_SET_SEL_TIME] = { set_sel_time, 6 },
};

static const IPMINetfn storage_netfn = {
    .cmd_nums = ARRAY_SIZE(storage_cmds),
    .cmd_handlers = storage_cmds
};

static void register_cmds(IPMIBmcSim *s)
{
    ipmi_sim_register_netfn(s, IPMI_NETFN_CHASSIS, &chassis_netfn);
    ipmi_sim_register_netfn(s, IPMI_NETFN_SENSOR_EVENT, &sensor_event_netfn);
    ipmi_sim_register_netfn(s, IPMI_NETFN_APP, &app_netfn);
    ipmi_sim_register_netfn(s, IPMI_NETFN_STORAGE, &storage_netfn);
}

static uint8_t init_sdrs[] = {
    /* Watchdog device */
    0x00, 0x00, 0x51, 0x02,   35, 0x20, 0x00, 0x00,
    0x23, 0x01, 0x63, 0x00, 0x23, 0x6f, 0x0f, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc8,
    'W',  'a',  't',  'c',  'h',  'd',  'o',  'g',
};

static void ipmi_sdr_init(IPMIBmcSim *ibs)
{
    unsigned int i;
    int len;
    size_t sdrs_size;
    uint8_t *sdrs;

    sdrs_size = sizeof(init_sdrs);
    sdrs = init_sdrs;
    if (ibs->sdr_filename &&
        !g_file_get_contents(ibs->sdr_filename, (gchar **) &sdrs, &sdrs_size,
                             NULL)) {
        error_report("failed to load sdr file '%s'", ibs->sdr_filename);
        sdrs_size = sizeof(init_sdrs);
        sdrs = init_sdrs;
    }

    for (i = 0; i < sdrs_size; i += len) {
        struct ipmi_sdr_header *sdrh;

        if (i + IPMI_SDR_HEADER_SIZE > sdrs_size) {
            error_report("Problem with recid 0x%4.4x", i);
            break;
        }
        sdrh = (struct ipmi_sdr_header *) &sdrs[i];
        len = ipmi_sdr_length(sdrh);
        if (i + len > sdrs_size) {
            error_report("Problem with recid 0x%4.4x", i);
            break;
        }
        sdr_add_entry(ibs, sdrh, len, NULL);
    }

    if (sdrs != init_sdrs) {
        g_free(sdrs);
    }
}

static const VMStateDescription vmstate_ipmi_sim = {
    .name = TYPE_IPMI_BMC_SIMULATOR,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT8(bmc_global_enables, IPMIBmcSim),
        VMSTATE_UINT8(msg_flags, IPMIBmcSim),
        VMSTATE_BOOL(watchdog_initialized, IPMIBmcSim),
        VMSTATE_UINT8(watchdog_use, IPMIBmcSim),
        VMSTATE_UINT8(watchdog_action, IPMIBmcSim),
        VMSTATE_UINT8(watchdog_pretimeout, IPMIBmcSim),
        VMSTATE_BOOL(watchdog_expired, IPMIBmcSim),
        VMSTATE_UINT16(watchdog_timeout, IPMIBmcSim),
        VMSTATE_BOOL(watchdog_running, IPMIBmcSim),
        VMSTATE_BOOL(watchdog_preaction_ran, IPMIBmcSim),
        VMSTATE_INT64(watchdog_expiry, IPMIBmcSim),
        VMSTATE_UINT8_ARRAY(evtbuf, IPMIBmcSim, 16),
        VMSTATE_UINT8(sensors[IPMI_WATCHDOG_SENSOR].status, IPMIBmcSim),
        VMSTATE_UINT8(sensors[IPMI_WATCHDOG_SENSOR].reading, IPMIBmcSim),
        VMSTATE_UINT16(sensors[IPMI_WATCHDOG_SENSOR].states, IPMIBmcSim),
        VMSTATE_UINT16(sensors[IPMI_WATCHDOG_SENSOR].assert_states, IPMIBmcSim),
        VMSTATE_UINT16(sensors[IPMI_WATCHDOG_SENSOR].deassert_states,
                       IPMIBmcSim),
        VMSTATE_UINT16(sensors[IPMI_WATCHDOG_SENSOR].assert_enable, IPMIBmcSim),
        VMSTATE_END_OF_LIST()
    }
};

static void ipmi_fru_init(IPMIFru *fru)
{
    int fsize;
    int size = 0;

    if (!fru->filename) {
        goto out;
    }

    fsize = get_image_size(fru->filename);
    if (fsize > 0) {
        size = QEMU_ALIGN_UP(fsize, fru->areasize);
        fru->data = g_malloc0(size);
        if (load_image_size(fru->filename, fru->data, fsize) != fsize) {
            error_report("Could not load file '%s'", fru->filename);
            g_free(fru->data);
            fru->data = NULL;
        }
    }

out:
    if (!fru->data) {
        /* give one default FRU */
        size = fru->areasize;
        fru->data = g_malloc0(size);
    }

    fru->nentries = size / fru->areasize;
}

static void ipmi_sim_realize(DeviceState *dev, Error **errp)
{
    IPMIBmc *b = IPMI_BMC(dev);
    unsigned int i;
    IPMIBmcSim *ibs = IPMI_BMC_SIMULATOR(b);

    QTAILQ_INIT(&ibs->rcvbufs);

    ibs->bmc_global_enables = (1 << IPMI_BMC_EVENT_LOG_BIT);
    ibs->device_id = 0x20;
    ibs->ipmi_version = 0x02; /* IPMI 2.0 */
    ibs->restart_cause = 0;
    for (i = 0; i < 4; i++) {
        ibs->sel.last_addition[i] = 0xff;
        ibs->sel.last_clear[i] = 0xff;
        ibs->sdr.last_addition[i] = 0xff;
        ibs->sdr.last_clear[i] = 0xff;
    }

    ipmi_sdr_init(ibs);

    ipmi_fru_init(&ibs->fru);

    ibs->acpi_power_state[0] = 0;
    ibs->acpi_power_state[1] = 0;

    ipmi_init_sensors_from_sdrs(ibs);
    register_cmds(ibs);

    ibs->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, ipmi_timeout, ibs);

    vmstate_register(NULL, 0, &vmstate_ipmi_sim, ibs);
}

static Property ipmi_sim_properties[] = {
    DEFINE_PROP_UINT16("fruareasize", IPMIBmcSim, fru.areasize, 1024),
    DEFINE_PROP_STRING("frudatafile", IPMIBmcSim, fru.filename),
    DEFINE_PROP_STRING("sdrfile", IPMIBmcSim, sdr_filename),
    DEFINE_PROP_UINT8("device_id", IPMIBmcSim, device_id, 0x20),
    DEFINE_PROP_UINT8("ipmi_version", IPMIBmcSim, ipmi_version, 0x02),
    DEFINE_PROP_UINT8("device_rev", IPMIBmcSim, device_rev, 0),
    DEFINE_PROP_UINT8("fwrev1", IPMIBmcSim, fwrev1, 0),
    DEFINE_PROP_UINT8("fwrev2", IPMIBmcSim, fwrev2, 0),
    DEFINE_PROP_UINT32("mfg_id", IPMIBmcSim, mfg_id, 0),
    DEFINE_PROP_UINT16("product_id", IPMIBmcSim, product_id, 0),
    DEFINE_PROP_UUID_NODEFAULT("guid", IPMIBmcSim, uuid),
    DEFINE_PROP_END_OF_LIST(),
};

static void ipmi_sim_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    IPMIBmcClass *bk = IPMI_BMC_CLASS(oc);

    dc->hotpluggable = false;
    dc->realize = ipmi_sim_realize;
    device_class_set_props(dc, ipmi_sim_properties);
    bk->handle_command = ipmi_sim_handle_command;
}

static const TypeInfo ipmi_sim_type = {
    .name          = TYPE_IPMI_BMC_SIMULATOR,
    .parent        = TYPE_IPMI_BMC,
    .instance_size = sizeof(IPMIBmcSim),
    .class_init    = ipmi_sim_class_init,
};

static void ipmi_sim_register_types(void)
{
    type_register_static(&ipmi_sim_type);
}

type_init(ipmi_sim_register_types)
