/*
 * Reset container
 *
 * Copyright (c) 2024 Linaro, Ltd
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_RESETCONTAINER_H
#define HW_RESETCONTAINER_H

/*
 * The "reset container" is an object which implements the Resettable
 * interface. It contains a list of arbitrary other objects which also
 * implement Resettable. Resetting the reset container resets all the
 * objects in it.
 */

#include "qom/object.h"

#define TYPE_RESETTABLE_CONTAINER "resettable-container"
OBJECT_DECLARE_TYPE(ResettableContainer, ResettableContainerClass, RESETTABLE_CONTAINER)

/**
 * resettable_container_add: Add a resettable object to the container
 * @rc: container
 * @obj: object to add to the container
 *
 * Add @obj to the ResettableContainer @rc. @obj must implement the
 * Resettable interface.
 *
 * When @rc is reset, it will reset every object that has been added
 * to it, in the order they were added.
 */
void resettable_container_add(ResettableContainer *rc, Object *obj);

/**
 * resettable_container_remove: Remove an object from the container
 * @rc: container
 * @obj: object to remove from the container
 *
 * Remove @obj from the ResettableContainer @rc. @obj must have been
 * previously added to this container.
 */
void resettable_container_remove(ResettableContainer *rc, Object *obj);

#endif
