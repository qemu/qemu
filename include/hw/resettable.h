/*
 * Resettable interface header.
 *
 * Copyright (c) 2019 GreenSocs SAS
 *
 * Authors:
 *   Damien Hedde
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_RESETTABLE_H
#define HW_RESETTABLE_H

#include "qom/object.h"

#define TYPE_RESETTABLE_INTERFACE "resettable"

typedef struct ResettableClass ResettableClass;
DECLARE_CLASS_CHECKERS(ResettableClass, RESETTABLE,
                       TYPE_RESETTABLE_INTERFACE)


typedef struct ResettableState ResettableState;

/**
 * ResetType:
 * Types of reset.
 *
 * + Cold: reset resulting from a power cycle of the object.
 *
 * TODO: Support has to be added to handle more types. In particular,
 * ResettableState structure needs to be expanded.
 */
typedef enum ResetType {
    RESET_TYPE_COLD,
} ResetType;

/*
 * ResettableClass:
 * Interface for resettable objects.
 *
 * See docs/devel/reset.rst for more detailed information about how QEMU models
 * reset. This whole API must only be used when holding the iothread mutex.
 *
 * All objects which can be reset must implement this interface;
 * it is usually provided by a base class such as DeviceClass or BusClass.
 * Every Resettable object must maintain some state tracking the
 * progress of a reset operation by providing a ResettableState structure.
 * The functions defined in this module take care of updating the
 * state of the reset.
 * The base class implementation of the interface provides this
 * state and implements the associated method: get_state.
 *
 * Concrete object implementations (typically specific devices
 * such as a UART model) should provide the functions
 * for the phases.enter, phases.hold and phases.exit methods, which
 * they can set in their class init function, either directly or
 * by calling resettable_class_set_parent_phases().
 * The phase methods are guaranteed to only only ever be called once
 * for any reset event, in the order 'enter', 'hold', 'exit'.
 * An object will always move quickly from 'enter' to 'hold'
 * but might remain in 'hold' for an arbitrary period of time
 * before eventually reset is deasserted and the 'exit' phase is called.
 * Object implementations should be prepared for functions handling
 * inbound connections from other devices (such as qemu_irq handler
 * functions) to be called at any point during reset after their
 * 'enter' method has been called.
 *
 * Users of a resettable object should not call these methods
 * directly, but instead use the function resettable_reset().
 *
 * @phases.enter: This phase is called when the object enters reset. It
 * should reset local state of the object, but it must not do anything that
 * has a side-effect on other objects, such as raising or lowering a qemu_irq
 * line or reading or writing guest memory. It takes the reset's type as
 * argument.
 *
 * @phases.hold: This phase is called for entry into reset, once every object
 * in the system which is being reset has had its @phases.enter method called.
 * At this point devices can do actions that affect other objects.
 *
 * @phases.exit: This phase is called when the object leaves the reset state.
 * Actions affecting other objects are permitted.
 *
 * @get_state: Mandatory method which must return a pointer to a
 * ResettableState.
 *
 * @get_transitional_function: transitional method to handle Resettable objects
 * not yet fully moved to this interface. It will be removed as soon as it is
 * not needed anymore. This method is optional and may return a pointer to a
 * function to be used instead of the phases. If the method exists and returns
 * a non-NULL function pointer then that function is executed as a replacement
 * of the 'hold' phase method taking the object as argument. The two other phase
 * methods are not executed.
 *
 * @child_foreach: Executes a given callback on every Resettable child. Child
 * in this context means a child in the qbus tree, so the children of a qbus
 * are the devices on it, and the children of a device are all the buses it
 * owns. This is not the same as the QOM object hierarchy. The function takes
 * additional opaque and ResetType arguments which must be passed unmodified to
 * the callback.
 */
typedef void (*ResettableEnterPhase)(Object *obj, ResetType type);
typedef void (*ResettableHoldPhase)(Object *obj);
typedef void (*ResettableExitPhase)(Object *obj);
typedef ResettableState * (*ResettableGetState)(Object *obj);
typedef void (*ResettableTrFunction)(Object *obj);
typedef ResettableTrFunction (*ResettableGetTrFunction)(Object *obj);
typedef void (*ResettableChildCallback)(Object *, void *opaque,
                                        ResetType type);
typedef void (*ResettableChildForeach)(Object *obj,
                                       ResettableChildCallback cb,
                                       void *opaque, ResetType type);
typedef struct ResettablePhases {
    ResettableEnterPhase enter;
    ResettableHoldPhase hold;
    ResettableExitPhase exit;
} ResettablePhases;
struct ResettableClass {
    InterfaceClass parent_class;

    /* Phase methods */
    ResettablePhases phases;

    /* State access method */
    ResettableGetState get_state;

    /* Transitional method for legacy reset compatibility */
    ResettableGetTrFunction get_transitional_function;

    /* Hierarchy handling method */
    ResettableChildForeach child_foreach;
};

/**
 * ResettableState:
 * Structure holding reset related state. The fields should not be accessed
 * directly; the definition is here to allow further inclusion into other
 * objects.
 *
 * @count: Number of reset level the object is into. It is incremented when
 * the reset operation starts and decremented when it finishes.
 * @hold_phase_pending: flag which indicates that we need to invoke the 'hold'
 * phase handler for this object.
 * @exit_phase_in_progress: true if we are currently in the exit phase
 */
struct ResettableState {
    unsigned count;
    bool hold_phase_pending;
    bool exit_phase_in_progress;
};

/**
 * resettable_state_clear:
 * Clear the state. It puts the state to the initial (zeroed) state required
 * to reuse an object. Typically used in realize step of base classes
 * implementing the interface.
 */
static inline void resettable_state_clear(ResettableState *state)
{
    memset(state, 0, sizeof(ResettableState));
}

/**
 * resettable_reset:
 * Trigger a reset on an object @obj of type @type. @obj must implement
 * Resettable interface.
 *
 * Calling this function is equivalent to calling @resettable_assert_reset()
 * then @resettable_release_reset().
 */
void resettable_reset(Object *obj, ResetType type);

/**
 * resettable_assert_reset:
 * Put an object @obj into reset. @obj must implement Resettable interface.
 *
 * @resettable_release_reset() must eventually be called after this call.
 * There must be one call to @resettable_release_reset() per call of
 * @resettable_assert_reset(), with the same type argument.
 *
 * NOTE: Until support for migration is added, the @resettable_release_reset()
 * must not be delayed. It must occur just after @resettable_assert_reset() so
 * that migration cannot be triggered in between. Prefer using
 * @resettable_reset() for now.
 */
void resettable_assert_reset(Object *obj, ResetType type);

/**
 * resettable_release_reset:
 * Release the object @obj from reset. @obj must implement Resettable interface.
 *
 * See @resettable_assert_reset() description for details.
 */
void resettable_release_reset(Object *obj, ResetType type);

/**
 * resettable_is_in_reset:
 * Return true if @obj is under reset.
 *
 * @obj must implement Resettable interface.
 */
bool resettable_is_in_reset(Object *obj);

/**
 * resettable_change_parent:
 * Indicate that the parent of Ressettable @obj is changing from @oldp to @newp.
 * All 3 objects must implement resettable interface. @oldp or @newp may be
 * NULL.
 *
 * This function will adapt the reset state of @obj so that it is coherent
 * with the reset state of @newp. It may trigger @resettable_assert_reset()
 * or @resettable_release_reset(). It will do such things only if the reset
 * state of @newp and @oldp are different.
 *
 * When using this function during reset, it must only be called during
 * a hold phase method. Calling this during enter or exit phase is an error.
 */
void resettable_change_parent(Object *obj, Object *newp, Object *oldp);

/**
 * resettable_cold_reset_fn:
 * Helper to call resettable_reset((Object *) opaque, RESET_TYPE_COLD).
 *
 * This function is typically useful to register a reset handler with
 * qemu_register_reset.
 */
void resettable_cold_reset_fn(void *opaque);

/**
 * resettable_class_set_parent_phases:
 *
 * Save @rc current reset phases into @parent_phases and override @rc phases
 * by the given new methods (@enter, @hold and @exit).
 * Each phase is overridden only if the new one is not NULL allowing to
 * override a subset of phases.
 */
void resettable_class_set_parent_phases(ResettableClass *rc,
                                        ResettableEnterPhase enter,
                                        ResettableHoldPhase hold,
                                        ResettableExitPhase exit,
                                        ResettablePhases *parent_phases);

#endif
