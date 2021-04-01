/*
 * QEMU yank feature
 *
 * Copyright (c) Lukas Straub <lukasstraub2@web.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef YANK_H
#define YANK_H

#include "qapi/qapi-types-yank.h"

typedef void (YankFn)(void *opaque);

/**
 * yank_register_instance: Register a new instance.
 *
 * This registers a new instance for yanking. Must be called before any yank
 * function is registered for this instance.
 *
 * This function is thread-safe.
 *
 * @instance: The instance.
 * @errp: Error object.
 *
 * Returns true on success or false if an error occured.
 */
bool yank_register_instance(const YankInstance *instance, Error **errp);

/**
 * yank_unregister_instance: Unregister a instance.
 *
 * This unregisters a instance. Must be called only after every yank function
 * of the instance has been unregistered.
 *
 * This function is thread-safe.
 *
 * @instance: The instance.
 */
void yank_unregister_instance(const YankInstance *instance);

/**
 * yank_register_function: Register a yank function
 *
 * This registers a yank function. All limitations of qmp oob commands apply
 * to the yank function as well. See docs/devel/qapi-code-gen.txt under
 * "An OOB-capable command handler must satisfy the following conditions".
 *
 * This function is thread-safe.
 *
 * @instance: The instance.
 * @func: The yank function.
 * @opaque: Will be passed to the yank function.
 */
void yank_register_function(const YankInstance *instance,
                            YankFn *func,
                            void *opaque);

/**
 * yank_unregister_function: Unregister a yank function
 *
 * This unregisters a yank function.
 *
 * This function is thread-safe.
 *
 * @instance: The instance.
 * @func: func that was passed to yank_register_function.
 * @opaque: opaque that was passed to yank_register_function.
 */
void yank_unregister_function(const YankInstance *instance,
                              YankFn *func,
                              void *opaque);

#define BLOCKDEV_YANK_INSTANCE(the_node_name) (&(YankInstance) { \
        .type = YANK_INSTANCE_TYPE_BLOCK_NODE, \
        .u.block_node.node_name = (the_node_name) })

#define CHARDEV_YANK_INSTANCE(the_id) (&(YankInstance) { \
        .type = YANK_INSTANCE_TYPE_CHARDEV, \
        .u.chardev.id = (the_id) })

#define MIGRATION_YANK_INSTANCE (&(YankInstance) { \
        .type = YANK_INSTANCE_TYPE_MIGRATION })

#endif
