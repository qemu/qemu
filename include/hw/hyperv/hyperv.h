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

typedef struct HvSintRoute HvSintRoute;
typedef void (*HvSintAckClb)(void *data);

HvSintRoute *hyperv_sint_route_new(uint32_t vp_index, uint32_t sint,
                                   HvSintAckClb sint_ack_clb,
                                   void *sint_ack_clb_data);
void hyperv_sint_route_ref(HvSintRoute *sint_route);
void hyperv_sint_route_unref(HvSintRoute *sint_route);

int hyperv_sint_route_set_sint(HvSintRoute *sint_route);

static inline uint32_t hyperv_vp_index(CPUState *cs)
{
    return cs->cpu_index;
}

void hyperv_synic_add(CPUState *cs);
void hyperv_synic_reset(CPUState *cs);
void hyperv_synic_update(CPUState *cs, bool enable,
                         hwaddr msg_page_addr, hwaddr event_page_addr);

#endif
