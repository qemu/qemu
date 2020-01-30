/*
 * Resettable interface.
 *
 * Copyright (c) 2019 GreenSocs SAS
 *
 * Authors:
 *   Damien Hedde
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/resettable.h"
#include "trace.h"

/**
 * resettable_phase_enter/hold/exit:
 * Function executing a phase recursively in a resettable object and its
 * children.
 */
static void resettable_phase_enter(Object *obj, void *opaque, ResetType type);
static void resettable_phase_hold(Object *obj, void *opaque, ResetType type);
static void resettable_phase_exit(Object *obj, void *opaque, ResetType type);

/**
 * enter_phase_in_progress:
 * True if we are currently in reset enter phase.
 *
 * exit_phase_in_progress:
 * count the number of exit phase we are in.
 *
 * Note: These flags are only used to guarantee (using asserts) that the reset
 * API is used correctly. We can use global variables because we rely on the
 * iothread mutex to ensure only one reset operation is in a progress at a
 * given time.
 */
static bool enter_phase_in_progress;
static unsigned exit_phase_in_progress;

void resettable_reset(Object *obj, ResetType type)
{
    trace_resettable_reset(obj, type);
    resettable_assert_reset(obj, type);
    resettable_release_reset(obj, type);
}

void resettable_assert_reset(Object *obj, ResetType type)
{
    /* TODO: change this assert when adding support for other reset types */
    assert(type == RESET_TYPE_COLD);
    trace_resettable_reset_assert_begin(obj, type);
    assert(!enter_phase_in_progress);

    enter_phase_in_progress = true;
    resettable_phase_enter(obj, NULL, type);
    enter_phase_in_progress = false;

    resettable_phase_hold(obj, NULL, type);

    trace_resettable_reset_assert_end(obj);
}

void resettable_release_reset(Object *obj, ResetType type)
{
    /* TODO: change this assert when adding support for other reset types */
    assert(type == RESET_TYPE_COLD);
    trace_resettable_reset_release_begin(obj, type);
    assert(!enter_phase_in_progress);

    exit_phase_in_progress += 1;
    resettable_phase_exit(obj, NULL, type);
    exit_phase_in_progress -= 1;

    trace_resettable_reset_release_end(obj);
}

bool resettable_is_in_reset(Object *obj)
{
    ResettableClass *rc = RESETTABLE_GET_CLASS(obj);
    ResettableState *s = rc->get_state(obj);

    return s->count > 0;
}

/**
 * resettable_child_foreach:
 * helper to avoid checking the existence of the method.
 */
static void resettable_child_foreach(ResettableClass *rc, Object *obj,
                                     ResettableChildCallback cb,
                                     void *opaque, ResetType type)
{
    if (rc->child_foreach) {
        rc->child_foreach(obj, cb, opaque, type);
    }
}

/**
 * resettable_get_tr_func:
 * helper to fetch transitional reset callback if any.
 */
static ResettableTrFunction resettable_get_tr_func(ResettableClass *rc,
                                                   Object *obj)
{
    ResettableTrFunction tr_func = NULL;
    if (rc->get_transitional_function) {
        tr_func = rc->get_transitional_function(obj);
    }
    return tr_func;
}

static void resettable_phase_enter(Object *obj, void *opaque, ResetType type)
{
    ResettableClass *rc = RESETTABLE_GET_CLASS(obj);
    ResettableState *s = rc->get_state(obj);
    const char *obj_typename = object_get_typename(obj);
    bool action_needed = false;

    /* exit phase has to finish properly before entering back in reset */
    assert(!s->exit_phase_in_progress);

    trace_resettable_phase_enter_begin(obj, obj_typename, s->count, type);

    /* Only take action if we really enter reset for the 1st time. */
    /*
     * TODO: if adding more ResetType support, some additional checks
     * are probably needed here.
     */
    if (s->count++ == 0) {
        action_needed = true;
    }
    /*
     * We limit the count to an arbitrary "big" value. The value is big
     * enough not to be triggered normally.
     * The assert will stop an infinite loop if there is a cycle in the
     * reset tree. The loop goes through resettable_foreach_child below
     * which at some point will call us again.
     */
    assert(s->count <= 50);

    /*
     * handle the children even if action_needed is at false so that
     * child counts are incremented too
     */
    resettable_child_foreach(rc, obj, resettable_phase_enter, NULL, type);

    /* execute enter phase for the object if needed */
    if (action_needed) {
        trace_resettable_phase_enter_exec(obj, obj_typename, type,
                                          !!rc->phases.enter);
        if (rc->phases.enter && !resettable_get_tr_func(rc, obj)) {
            rc->phases.enter(obj, type);
        }
        s->hold_phase_pending = true;
    }
    trace_resettable_phase_enter_end(obj, obj_typename, s->count);
}

static void resettable_phase_hold(Object *obj, void *opaque, ResetType type)
{
    ResettableClass *rc = RESETTABLE_GET_CLASS(obj);
    ResettableState *s = rc->get_state(obj);
    const char *obj_typename = object_get_typename(obj);

    /* exit phase has to finish properly before entering back in reset */
    assert(!s->exit_phase_in_progress);

    trace_resettable_phase_hold_begin(obj, obj_typename, s->count, type);

    /* handle children first */
    resettable_child_foreach(rc, obj, resettable_phase_hold, NULL, type);

    /* exec hold phase */
    if (s->hold_phase_pending) {
        s->hold_phase_pending = false;
        ResettableTrFunction tr_func = resettable_get_tr_func(rc, obj);
        trace_resettable_phase_hold_exec(obj, obj_typename, !!rc->phases.hold);
        if (tr_func) {
            trace_resettable_transitional_function(obj, obj_typename);
            tr_func(obj);
        } else if (rc->phases.hold) {
            rc->phases.hold(obj);
        }
    }
    trace_resettable_phase_hold_end(obj, obj_typename, s->count);
}

static void resettable_phase_exit(Object *obj, void *opaque, ResetType type)
{
    ResettableClass *rc = RESETTABLE_GET_CLASS(obj);
    ResettableState *s = rc->get_state(obj);
    const char *obj_typename = object_get_typename(obj);

    assert(!s->exit_phase_in_progress);
    trace_resettable_phase_exit_begin(obj, obj_typename, s->count, type);

    /* exit_phase_in_progress ensures this phase is 'atomic' */
    s->exit_phase_in_progress = true;
    resettable_child_foreach(rc, obj, resettable_phase_exit, NULL, type);

    assert(s->count > 0);
    if (s->count == 1) {
        trace_resettable_phase_exit_exec(obj, obj_typename, !!rc->phases.exit);
        if (rc->phases.exit && !resettable_get_tr_func(rc, obj)) {
            rc->phases.exit(obj);
        }
        s->count = 0;
    }
    s->exit_phase_in_progress = false;
    trace_resettable_phase_exit_end(obj, obj_typename, s->count);
}

/*
 * resettable_get_count:
 * Get the count of the Resettable object @obj. Return 0 if @obj is NULL.
 */
static unsigned resettable_get_count(Object *obj)
{
    if (obj) {
        ResettableClass *rc = RESETTABLE_GET_CLASS(obj);
        return rc->get_state(obj)->count;
    }
    return 0;
}

void resettable_change_parent(Object *obj, Object *newp, Object *oldp)
{
    ResettableClass *rc = RESETTABLE_GET_CLASS(obj);
    ResettableState *s = rc->get_state(obj);
    unsigned newp_count = resettable_get_count(newp);
    unsigned oldp_count = resettable_get_count(oldp);

    /*
     * Ensure we do not change parent when in enter or exit phase.
     * During these phases, the reset subtree being updated is partly in reset
     * and partly not in reset (it depends on the actual position in
     * resettable_child_foreach()s). We are not able to tell in which part is a
     * leaving or arriving device. Thus we cannot set the reset count of the
     * moving device to the proper value.
     */
    assert(!enter_phase_in_progress && !exit_phase_in_progress);
    trace_resettable_change_parent(obj, oldp, oldp_count, newp, newp_count);

    /*
     * At most one of the two 'for' loops will be executed below
     * in order to cope with the difference between the two counts.
     */
    /* if newp is more reset than oldp */
    for (unsigned i = oldp_count; i < newp_count; i++) {
        resettable_assert_reset(obj, RESET_TYPE_COLD);
    }
    /*
     * if obj is leaving a bus under reset, we need to ensure
     * hold phase is not pending.
     */
    if (oldp_count && s->hold_phase_pending) {
        resettable_phase_hold(obj, NULL, RESET_TYPE_COLD);
    }
    /* if oldp is more reset than newp */
    for (unsigned i = newp_count; i < oldp_count; i++) {
        resettable_release_reset(obj, RESET_TYPE_COLD);
    }
}

void resettable_cold_reset_fn(void *opaque)
{
    resettable_reset((Object *) opaque, RESET_TYPE_COLD);
}

void resettable_class_set_parent_phases(ResettableClass *rc,
                                        ResettableEnterPhase enter,
                                        ResettableHoldPhase hold,
                                        ResettableExitPhase exit,
                                        ResettablePhases *parent_phases)
{
    *parent_phases = rc->phases;
    if (enter) {
        rc->phases.enter = enter;
    }
    if (hold) {
        rc->phases.hold = hold;
    }
    if (exit) {
        rc->phases.exit = exit;
    }
}

static const TypeInfo resettable_interface_info = {
    .name       = TYPE_RESETTABLE_INTERFACE,
    .parent     = TYPE_INTERFACE,
    .class_size = sizeof(ResettableClass),
};

static void reset_register_types(void)
{
    type_register_static(&resettable_interface_info);
}

type_init(reset_register_types)
