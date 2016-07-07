/*
 * Copyright (C) 2015 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Author: Daniel P. Berrange <berrange@redhat.com>
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qom/object.h"
#include "qemu/module.h"


#define TYPE_DUMMY "qemu-dummy"

typedef struct DummyObject DummyObject;
typedef struct DummyObjectClass DummyObjectClass;

#define DUMMY_OBJECT(obj)                               \
    OBJECT_CHECK(DummyObject, (obj), TYPE_DUMMY)

typedef enum DummyAnimal DummyAnimal;

enum DummyAnimal {
    DUMMY_FROG,
    DUMMY_ALLIGATOR,
    DUMMY_PLATYPUS,

    DUMMY_LAST,
};

static const char *const dummy_animal_map[DUMMY_LAST + 1] = {
    [DUMMY_FROG] = "frog",
    [DUMMY_ALLIGATOR] = "alligator",
    [DUMMY_PLATYPUS] = "platypus",
    [DUMMY_LAST] = NULL,
};

struct DummyObject {
    Object parent_obj;

    bool bv;
    DummyAnimal av;
    char *sv;
};

struct DummyObjectClass {
    ObjectClass parent_class;
};


static void dummy_set_bv(Object *obj,
                         bool value,
                         Error **errp)
{
    DummyObject *dobj = DUMMY_OBJECT(obj);

    dobj->bv = value;
}

static bool dummy_get_bv(Object *obj,
                         Error **errp)
{
    DummyObject *dobj = DUMMY_OBJECT(obj);

    return dobj->bv;
}


static void dummy_set_av(Object *obj,
                         int value,
                         Error **errp)
{
    DummyObject *dobj = DUMMY_OBJECT(obj);

    dobj->av = value;
}

static int dummy_get_av(Object *obj,
                        Error **errp)
{
    DummyObject *dobj = DUMMY_OBJECT(obj);

    return dobj->av;
}


static void dummy_set_sv(Object *obj,
                         const char *value,
                         Error **errp)
{
    DummyObject *dobj = DUMMY_OBJECT(obj);

    g_free(dobj->sv);
    dobj->sv = g_strdup(value);
}

static char *dummy_get_sv(Object *obj,
                          Error **errp)
{
    DummyObject *dobj = DUMMY_OBJECT(obj);

    return g_strdup(dobj->sv);
}


static void dummy_init(Object *obj)
{
    object_property_add_bool(obj, "bv",
                             dummy_get_bv,
                             dummy_set_bv,
                             NULL);
}


static void dummy_class_init(ObjectClass *cls, void *data)
{
    object_class_property_add_bool(cls, "bv",
                                   dummy_get_bv,
                                   dummy_set_bv,
                                   NULL);
    object_class_property_add_str(cls, "sv",
                                  dummy_get_sv,
                                  dummy_set_sv,
                                  NULL);
    object_class_property_add_enum(cls, "av",
                                   "DummyAnimal",
                                   dummy_animal_map,
                                   dummy_get_av,
                                   dummy_set_av,
                                   NULL);
}


static void dummy_finalize(Object *obj)
{
    DummyObject *dobj = DUMMY_OBJECT(obj);

    g_free(dobj->sv);
}


static const TypeInfo dummy_info = {
    .name          = TYPE_DUMMY,
    .parent        = TYPE_OBJECT,
    .instance_size = sizeof(DummyObject),
    .instance_init = dummy_init,
    .instance_finalize = dummy_finalize,
    .class_size = sizeof(DummyObjectClass),
    .class_init = dummy_class_init,
};


/*
 * The following 3 object classes are used to
 * simulate the kind of relationships seen in
 * qdev, which result in complex object
 * property destruction ordering.
 *
 * DummyDev has a 'bus' child to a DummyBus
 * DummyBus has a 'backend' child to a DummyBackend
 * DummyDev has a 'backend' link to DummyBackend
 *
 * When DummyDev is finalized, it unparents the
 * DummyBackend, which unparents the DummyDev
 * which deletes the 'backend' link from DummyDev
 * to DummyBackend. This illustrates that the
 * object_property_del_all() method needs to
 * cope with the list of properties being changed
 * while it iterates over them.
 */
typedef struct DummyDev DummyDev;
typedef struct DummyDevClass DummyDevClass;
typedef struct DummyBus DummyBus;
typedef struct DummyBusClass DummyBusClass;
typedef struct DummyBackend DummyBackend;
typedef struct DummyBackendClass DummyBackendClass;

#define TYPE_DUMMY_DEV "qemu-dummy-dev"
#define TYPE_DUMMY_BUS "qemu-dummy-bus"
#define TYPE_DUMMY_BACKEND "qemu-dummy-backend"

#define DUMMY_DEV(obj)                               \
    OBJECT_CHECK(DummyDev, (obj), TYPE_DUMMY_DEV)
#define DUMMY_BUS(obj)                               \
    OBJECT_CHECK(DummyBus, (obj), TYPE_DUMMY_BUS)
#define DUMMY_BACKEND(obj)                               \
    OBJECT_CHECK(DummyBackend, (obj), TYPE_DUMMY_BACKEND)

struct DummyDev {
    Object parent_obj;

    DummyBus *bus;
};

struct DummyDevClass {
    ObjectClass parent_class;
};

struct DummyBus {
    Object parent_obj;

    DummyBackend *backend;
};

struct DummyBusClass {
    ObjectClass parent_class;
};

struct DummyBackend {
    Object parent_obj;
};

struct DummyBackendClass {
    ObjectClass parent_class;
};


static void dummy_dev_finalize(Object *obj)
{
    DummyDev *dev = DUMMY_DEV(obj);

    object_unref(OBJECT(dev->bus));
}

static void dummy_dev_init(Object *obj)
{
    DummyDev *dev = DUMMY_DEV(obj);
    DummyBus *bus = DUMMY_BUS(object_new(TYPE_DUMMY_BUS));
    DummyBackend *backend = DUMMY_BACKEND(object_new(TYPE_DUMMY_BACKEND));

    object_property_add_child(obj, "bus", OBJECT(bus), NULL);
    dev->bus = bus;
    object_property_add_child(OBJECT(bus), "backend", OBJECT(backend), NULL);
    bus->backend = backend;

    object_property_add_link(obj, "backend", TYPE_DUMMY_BACKEND,
                             (Object **)&bus->backend, NULL, 0, NULL);
}

static void dummy_dev_unparent(Object *obj)
{
    DummyDev *dev = DUMMY_DEV(obj);
    object_unparent(OBJECT(dev->bus));
}

static void dummy_dev_class_init(ObjectClass *klass, void *opaque)
{
    klass->unparent = dummy_dev_unparent;
}


static void dummy_bus_finalize(Object *obj)
{
    DummyBus *bus = DUMMY_BUS(obj);

    object_unref(OBJECT(bus->backend));
}

static void dummy_bus_init(Object *obj)
{
}

static void dummy_bus_unparent(Object *obj)
{
    DummyBus *bus = DUMMY_BUS(obj);
    object_property_del(obj->parent, "backend", NULL);
    object_unparent(OBJECT(bus->backend));
}

static void dummy_bus_class_init(ObjectClass *klass, void *opaque)
{
    klass->unparent = dummy_bus_unparent;
}

static void dummy_backend_init(Object *obj)
{
}


static const TypeInfo dummy_dev_info = {
    .name          = TYPE_DUMMY_DEV,
    .parent        = TYPE_OBJECT,
    .instance_size = sizeof(DummyDev),
    .instance_init = dummy_dev_init,
    .instance_finalize = dummy_dev_finalize,
    .class_size = sizeof(DummyDevClass),
    .class_init = dummy_dev_class_init,
};

static const TypeInfo dummy_bus_info = {
    .name          = TYPE_DUMMY_BUS,
    .parent        = TYPE_OBJECT,
    .instance_size = sizeof(DummyBus),
    .instance_init = dummy_bus_init,
    .instance_finalize = dummy_bus_finalize,
    .class_size = sizeof(DummyBusClass),
    .class_init = dummy_bus_class_init,
};

static const TypeInfo dummy_backend_info = {
    .name          = TYPE_DUMMY_BACKEND,
    .parent        = TYPE_OBJECT,
    .instance_size = sizeof(DummyBackend),
    .instance_init = dummy_backend_init,
    .class_size = sizeof(DummyBackendClass),
};



static void test_dummy_createv(void)
{
    Error *err = NULL;
    Object *parent = object_get_objects_root();
    DummyObject *dobj = DUMMY_OBJECT(
        object_new_with_props(TYPE_DUMMY,
                              parent,
                              "dummy0",
                              &err,
                              "bv", "yes",
                              "sv", "Hiss hiss hiss",
                              "av", "platypus",
                              NULL));

    g_assert(err == NULL);
    g_assert_cmpstr(dobj->sv, ==, "Hiss hiss hiss");
    g_assert(dobj->bv == true);
    g_assert(dobj->av == DUMMY_PLATYPUS);

    g_assert(object_resolve_path_component(parent, "dummy0")
             == OBJECT(dobj));

    object_unparent(OBJECT(dobj));
}


static Object *new_helper(Error **errp,
                          Object *parent,
                          ...)
{
    va_list vargs;
    Object *obj;

    va_start(vargs, parent);
    obj = object_new_with_propv(TYPE_DUMMY,
                                parent,
                                "dummy0",
                                errp,
                                vargs);
    va_end(vargs);
    return obj;
}

static void test_dummy_createlist(void)
{
    Error *err = NULL;
    Object *parent = object_get_objects_root();
    DummyObject *dobj = DUMMY_OBJECT(
        new_helper(&err,
                   parent,
                   "bv", "yes",
                   "sv", "Hiss hiss hiss",
                   "av", "platypus",
                   NULL));

    g_assert(err == NULL);
    g_assert_cmpstr(dobj->sv, ==, "Hiss hiss hiss");
    g_assert(dobj->bv == true);
    g_assert(dobj->av == DUMMY_PLATYPUS);

    g_assert(object_resolve_path_component(parent, "dummy0")
             == OBJECT(dobj));

    object_unparent(OBJECT(dobj));
}

static void test_dummy_badenum(void)
{
    Error *err = NULL;
    Object *parent = object_get_objects_root();
    Object *dobj =
        object_new_with_props(TYPE_DUMMY,
                              parent,
                              "dummy0",
                              &err,
                              "bv", "yes",
                              "sv", "Hiss hiss hiss",
                              "av", "yeti",
                              NULL);

    g_assert(dobj == NULL);
    g_assert(err != NULL);
    g_assert_cmpstr(error_get_pretty(err), ==,
                    "Invalid parameter 'yeti'");

    g_assert(object_resolve_path_component(parent, "dummy0")
             == NULL);

    error_free(err);
}


static void test_dummy_getenum(void)
{
    Error *err = NULL;
    int val;
    Object *parent = object_get_objects_root();
    DummyObject *dobj = DUMMY_OBJECT(
        object_new_with_props(TYPE_DUMMY,
                         parent,
                         "dummy0",
                         &err,
                         "av", "platypus",
                         NULL));

    g_assert(err == NULL);
    g_assert(dobj->av == DUMMY_PLATYPUS);

    val = object_property_get_enum(OBJECT(dobj),
                                   "av",
                                   "DummyAnimal",
                                   &err);
    g_assert(err == NULL);
    g_assert(val == DUMMY_PLATYPUS);

    /* A bad enum type name */
    val = object_property_get_enum(OBJECT(dobj),
                                   "av",
                                   "BadAnimal",
                                   &err);
    g_assert(err != NULL);
    error_free(err);
    err = NULL;

    /* A non-enum property name */
    val = object_property_get_enum(OBJECT(dobj),
                                   "iv",
                                   "DummyAnimal",
                                   &err);
    g_assert(err != NULL);
    error_free(err);

    object_unparent(OBJECT(dobj));
}


static void test_dummy_iterator(void)
{
    Object *parent = object_get_objects_root();
    DummyObject *dobj = DUMMY_OBJECT(
        object_new_with_props(TYPE_DUMMY,
                              parent,
                              "dummy0",
                              &error_abort,
                              "bv", "yes",
                              "sv", "Hiss hiss hiss",
                              "av", "platypus",
                              NULL));

    ObjectProperty *prop;
    ObjectPropertyIterator iter;
    bool seenbv = false, seensv = false, seenav = false, seentype;

    object_property_iter_init(&iter, OBJECT(dobj));
    while ((prop = object_property_iter_next(&iter))) {
        if (g_str_equal(prop->name, "bv")) {
            seenbv = true;
        } else if (g_str_equal(prop->name, "sv")) {
            seensv = true;
        } else if (g_str_equal(prop->name, "av")) {
            seenav = true;
        } else if (g_str_equal(prop->name, "type")) {
            /* This prop comes from the base Object class */
            seentype = true;
        } else {
            g_printerr("Found prop '%s'\n", prop->name);
            g_assert_not_reached();
        }
    }
    g_assert(seenbv);
    g_assert(seenav);
    g_assert(seensv);
    g_assert(seentype);

    object_unparent(OBJECT(dobj));
}


static void test_dummy_delchild(void)
{
    Object *parent = object_get_objects_root();
    DummyDev *dev = DUMMY_DEV(
        object_new_with_props(TYPE_DUMMY_DEV,
                              parent,
                              "dev0",
                              &error_abort,
                              NULL));

    object_unparent(OBJECT(dev));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    module_call_init(MODULE_INIT_QOM);
    type_register_static(&dummy_info);
    type_register_static(&dummy_dev_info);
    type_register_static(&dummy_bus_info);
    type_register_static(&dummy_backend_info);

    g_test_add_func("/qom/proplist/createlist", test_dummy_createlist);
    g_test_add_func("/qom/proplist/createv", test_dummy_createv);
    g_test_add_func("/qom/proplist/badenum", test_dummy_badenum);
    g_test_add_func("/qom/proplist/getenum", test_dummy_getenum);
    g_test_add_func("/qom/proplist/iterator", test_dummy_iterator);
    g_test_add_func("/qom/proplist/delchild", test_dummy_delchild);

    return g_test_run();
}
