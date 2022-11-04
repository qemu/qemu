/*
 *  Self-announce
 *  (c) 2017-2019 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "net/announce.h"
#include "net/net.h"
#include "qapi/clone-visitor.h"
#include "qapi/qapi-visit-net.h"
#include "qapi/qapi-commands-net.h"
#include "trace.h"

static GData *named_timers;

int64_t qemu_announce_timer_step(AnnounceTimer *timer)
{
    int64_t step;

    step =  timer->params.initial +
            (timer->params.rounds - timer->round - 1) *
            timer->params.step;

    if (step < 0 || step > timer->params.max) {
        step = timer->params.max;
    }
    timer_mod(timer->tm, qemu_clock_get_ms(timer->type) + step);

    return step;
}

/*
 * If 'free_named' is true, then remove the timer from the list
 * and free the timer itself.
 */
void qemu_announce_timer_del(AnnounceTimer *timer, bool free_named)
{
    bool free_timer = false;
    if (timer->tm) {
        timer_free(timer->tm);
        timer->tm = NULL;
    }
    qapi_free_strList(timer->params.interfaces);
    timer->params.interfaces = NULL;
    if (free_named && timer->params.id) {
        AnnounceTimer *list_timer;
        /*
         * Sanity check: There should only be one timer on the list with
         * the id.
         */
        list_timer = g_datalist_get_data(&named_timers, timer->params.id);
        assert(timer == list_timer);
        free_timer = true;
        g_datalist_remove_data(&named_timers, timer->params.id);
    }
    trace_qemu_announce_timer_del(free_named, free_timer, timer->params.id);
    g_free(timer->params.id);
    timer->params.id = NULL;

    if (free_timer) {
        g_free(timer);
    }
}

/*
 * Under BQL/main thread
 * Reset the timer to the given parameters/type/notifier.
 */
void qemu_announce_timer_reset(AnnounceTimer *timer,
                               AnnounceParameters *params,
                               QEMUClockType type,
                               QEMUTimerCB *cb,
                               void *opaque)
{
    /*
     * We're under the BQL, so the current timer can't
     * be firing, so we should be able to delete it.
     */
    qemu_announce_timer_del(timer, false);

    QAPI_CLONE_MEMBERS(AnnounceParameters, &timer->params, params);
    timer->round = params->rounds;
    timer->type = type;
    timer->tm = timer_new_ms(type, cb, opaque);
}

#ifndef ETH_P_RARP
#define ETH_P_RARP 0x8035
#endif
#define ARP_HTYPE_ETH 0x0001
#define ARP_PTYPE_IP 0x0800
#define ARP_OP_REQUEST_REV 0x3

static int announce_self_create(uint8_t *buf,
                                uint8_t *mac_addr)
{
    /* Ethernet header. */
    memset(buf, 0xff, 6);         /* destination MAC addr */
    memcpy(buf + 6, mac_addr, 6); /* source MAC addr */
    *(uint16_t *)(buf + 12) = htons(ETH_P_RARP); /* ethertype */

    /* RARP header. */
    *(uint16_t *)(buf + 14) = htons(ARP_HTYPE_ETH); /* hardware addr space */
    *(uint16_t *)(buf + 16) = htons(ARP_PTYPE_IP); /* protocol addr space */
    *(buf + 18) = 6; /* hardware addr length (ethernet) */
    *(buf + 19) = 4; /* protocol addr length (IPv4) */
    *(uint16_t *)(buf + 20) = htons(ARP_OP_REQUEST_REV); /* opcode */
    memcpy(buf + 22, mac_addr, 6); /* source hw addr */
    memset(buf + 28, 0x00, 4);     /* source protocol addr */
    memcpy(buf + 32, mac_addr, 6); /* target hw addr */
    memset(buf + 38, 0x00, 4);     /* target protocol addr */

    /* Padding to get up to 60 bytes (ethernet min packet size, minus FCS). */
    memset(buf + 42, 0x00, 18);

    return 60; /* len (FCS will be added by hardware) */
}

/*
 * Helper to print ethernet mac address
 */
static const char *qemu_ether_ntoa(const MACAddr *mac)
{
    static char ret[18];

    snprintf(ret, sizeof(ret), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac->a[0], mac->a[1], mac->a[2], mac->a[3], mac->a[4], mac->a[5]);

    return ret;
}

static void qemu_announce_self_iter(NICState *nic, void *opaque)
{
    AnnounceTimer *timer = opaque;
    uint8_t buf[60];
    int len;
    bool skip;

    if (timer->params.has_interfaces) {
        strList *entry = timer->params.interfaces;
        /* Skip unless we find our name in the requested list */
        skip = true;

        while (entry) {
            if (!strcmp(entry->value, nic->ncs->name)) {
                /* Found us */
                skip = false;
                break;
            }
            entry = entry->next;
        }
    } else {
        skip = false;
    }

    trace_qemu_announce_self_iter(timer->params.id ?: "_",
                                  nic->ncs->name,
                                  qemu_ether_ntoa(&nic->conf->macaddr), skip);

    if (!skip) {
        len = announce_self_create(buf, nic->conf->macaddr.a);

        qemu_send_packet_raw(qemu_get_queue(nic), buf, len);

        /* if the NIC provides it's own announcement support, use it as well */
        if (nic->ncs->info->announce) {
            nic->ncs->info->announce(nic->ncs);
        }
    }
}
static void qemu_announce_self_once(void *opaque)
{
    AnnounceTimer *timer = (AnnounceTimer *)opaque;

    qemu_foreach_nic(qemu_announce_self_iter, timer);

    if (--timer->round) {
        qemu_announce_timer_step(timer);
    } else {
        qemu_announce_timer_del(timer, true);
    }
}

void qemu_announce_self(AnnounceTimer *timer, AnnounceParameters *params)
{
    qemu_announce_timer_reset(timer, params, QEMU_CLOCK_REALTIME,
                              qemu_announce_self_once, timer);
    if (params->rounds) {
        qemu_announce_self_once(timer);
    } else {
        qemu_announce_timer_del(timer, true);
    }
}

void qmp_announce_self(AnnounceParameters *params, Error **errp)
{
    AnnounceTimer *named_timer;

    if (!params->id) {
        params->id = g_strdup("");
    }

    named_timer = g_datalist_get_data(&named_timers, params->id);

    if (!named_timer) {
        named_timer = g_new0(AnnounceTimer, 1);
        g_datalist_set_data(&named_timers, params->id, named_timer);
    }

    qemu_announce_self(named_timer, params);
}
