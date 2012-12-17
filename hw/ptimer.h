/*
 * General purpose implementation of a simple periodic countdown timer.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 * This code is licensed under the GNU LGPL.
 */
#ifndef PTIMER_H
#define PTIMER_H

#include "qemu-common.h"
#include "qemu/timer.h"
#include "migration/vmstate.h"

/* ptimer.c */
typedef struct ptimer_state ptimer_state;
typedef void (*ptimer_cb)(void *opaque);

ptimer_state *ptimer_init(QEMUBH *bh);
void ptimer_set_period(ptimer_state *s, int64_t period);
void ptimer_set_freq(ptimer_state *s, uint32_t freq);
void ptimer_set_limit(ptimer_state *s, uint64_t limit, int reload);
uint64_t ptimer_get_count(ptimer_state *s);
void ptimer_set_count(ptimer_state *s, uint64_t count);
void ptimer_run(ptimer_state *s, int oneshot);
void ptimer_stop(ptimer_state *s);

extern const VMStateDescription vmstate_ptimer;

#define VMSTATE_PTIMER(_field, _state) {                             \
    .name       = (stringify(_field)),                               \
    .version_id = (1),                                               \
    .vmsd       = &vmstate_ptimer,                                   \
    .size       = sizeof(ptimer_state *),                            \
    .flags      = VMS_STRUCT|VMS_POINTER,                            \
    .offset     = vmstate_offset_pointer(_state, _field, ptimer_state), \
}

#endif
