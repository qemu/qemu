/*
 *  Reset handlers.
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2016 Red Hat, Inc.
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
#include "system/reset.h"
#include "hw/resettable.h"
#include "hw/core/resetcontainer.h"

/*
 * Return a pointer to the singleton container that holds all the Resettable
 * items that will be reset when qemu_devices_reset() is called.
 */
static ResettableContainer *get_root_reset_container(void)
{
    static ResettableContainer *root_reset_container;

    if (!root_reset_container) {
        root_reset_container =
            RESETTABLE_CONTAINER(object_new(TYPE_RESETTABLE_CONTAINER));
    }
    return root_reset_container;
}

/*
 * This is an Object which implements Resettable simply to call the
 * callback function in the hold phase.
 */
#define TYPE_LEGACY_RESET "legacy-reset"
OBJECT_DECLARE_SIMPLE_TYPE(LegacyReset, LEGACY_RESET)

struct LegacyReset {
    Object parent;
    ResettableState reset_state;
    QEMUResetHandler *func;
    void *opaque;
    bool skip_on_snapshot_load;
};

OBJECT_DEFINE_SIMPLE_TYPE_WITH_INTERFACES(LegacyReset, legacy_reset, LEGACY_RESET, OBJECT, { TYPE_RESETTABLE_INTERFACE }, { })

static ResettableState *legacy_reset_get_state(Object *obj)
{
    LegacyReset *lr = LEGACY_RESET(obj);
    return &lr->reset_state;
}

static void legacy_reset_hold(Object *obj, ResetType type)
{
    LegacyReset *lr = LEGACY_RESET(obj);

    if (type == RESET_TYPE_SNAPSHOT_LOAD && lr->skip_on_snapshot_load) {
        return;
    }
    lr->func(lr->opaque);
}

static void legacy_reset_init(Object *obj)
{
}

static void legacy_reset_finalize(Object *obj)
{
}

static void legacy_reset_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->get_state = legacy_reset_get_state;
    rc->phases.hold = legacy_reset_hold;
}

void qemu_register_reset(QEMUResetHandler *func, void *opaque)
{
    Object *obj = object_new(TYPE_LEGACY_RESET);
    LegacyReset *lr = LEGACY_RESET(obj);

    lr->func = func;
    lr->opaque = opaque;
    qemu_register_resettable(obj);
}

void qemu_register_reset_nosnapshotload(QEMUResetHandler *func, void *opaque)
{
    Object *obj = object_new(TYPE_LEGACY_RESET);
    LegacyReset *lr = LEGACY_RESET(obj);

    lr->func = func;
    lr->opaque = opaque;
    lr->skip_on_snapshot_load = true;
    qemu_register_resettable(obj);
}

typedef struct FindLegacyInfo {
    QEMUResetHandler *func;
    void *opaque;
    LegacyReset *lr;
} FindLegacyInfo;

static void find_legacy_reset_cb(Object *obj, void *opaque, ResetType type)
{
    LegacyReset *lr;
    FindLegacyInfo *fli = opaque;

    /* Not everything in the ResettableContainer will be a LegacyReset */
    lr = LEGACY_RESET(object_dynamic_cast(obj, TYPE_LEGACY_RESET));
    if (lr && lr->func == fli->func && lr->opaque == fli->opaque) {
        fli->lr = lr;
    }
}

static LegacyReset *find_legacy_reset(QEMUResetHandler *func, void *opaque)
{
    /*
     * Find the LegacyReset with the specified func and opaque,
     * by getting the ResettableContainer to call our callback for
     * every item in it.
     */
    ResettableContainer *rootcon = get_root_reset_container();
    ResettableClass *rc = RESETTABLE_GET_CLASS(rootcon);
    FindLegacyInfo fli;

    fli.func = func;
    fli.opaque = opaque;
    fli.lr = NULL;
    rc->child_foreach(OBJECT(rootcon), find_legacy_reset_cb,
                      &fli, RESET_TYPE_COLD);
    return fli.lr;
}

void qemu_unregister_reset(QEMUResetHandler *func, void *opaque)
{
    Object *obj = OBJECT(find_legacy_reset(func, opaque));

    if (obj) {
        qemu_unregister_resettable(obj);
        object_unref(obj);
    }
}

void qemu_register_resettable(Object *obj)
{
    resettable_container_add(get_root_reset_container(), obj);
}

void qemu_unregister_resettable(Object *obj)
{
    resettable_container_remove(get_root_reset_container(), obj);
}

void qemu_devices_reset(ResetType type)
{
    /* Reset the simulation */
    resettable_reset(OBJECT(get_root_reset_container()), type);
}
