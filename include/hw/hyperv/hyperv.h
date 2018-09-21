/*
 * Hyper-V guest/hypervisor interaction
 *
 * Copyright (c) 2015-2018 Virtuozzo International GmbH.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_HYPERV_HYPERV_H
#define HW_HYPERV_HYPERV_H

#include "cpu-qom.h"
#include "hw/hyperv/hyperv-proto.h"

typedef struct HvSintRoute HvSintRoute;

/*
 * Callback executed in a bottom-half when the status of posting the message
 * becomes known, before unblocking the connection for further messages
 */
typedef void (*HvSintMsgCb)(void *data, int status);

HvSintRoute *hyperv_sint_route_new(uint32_t vp_index, uint32_t sint,
                                   HvSintMsgCb cb, void *cb_data);
void hyperv_sint_route_ref(HvSintRoute *sint_route);
void hyperv_sint_route_unref(HvSintRoute *sint_route);

int hyperv_sint_route_set_sint(HvSintRoute *sint_route);

/*
 * Submit a message to be posted in vcpu context.  If the submission succeeds,
 * the status of posting the message is reported via the callback associated
 * with the @sint_route; until then no more messages are accepted.
 */
int hyperv_post_msg(HvSintRoute *sint_route, struct hyperv_message *msg);
/*
 * Set event flag @eventno, and signal the SINT if the flag has changed.
 */
int hyperv_set_event_flag(HvSintRoute *sint_route, unsigned eventno);

static inline uint32_t hyperv_vp_index(CPUState *cs)
{
    return cs->cpu_index;
}

void hyperv_synic_add(CPUState *cs);
void hyperv_synic_reset(CPUState *cs);
void hyperv_synic_update(CPUState *cs, bool enable,
                         hwaddr msg_page_addr, hwaddr event_page_addr);

#endif
