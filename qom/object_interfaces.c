#include "qom/object_interfaces.h"
#include "qemu/module.h"

void user_creatable_complete(Object *obj, Error **errp)
{

    UserCreatableClass *ucc;
    UserCreatable *uc =
        (UserCreatable *)object_dynamic_cast(obj, TYPE_USER_CREATABLE);

    if (!uc) {
        return;
    }

    ucc = USER_CREATABLE_GET_CLASS(uc);
    if (ucc->complete) {
        ucc->complete(uc, errp);
    }
}

bool user_creatable_can_be_deleted(UserCreatable *uc, Error **errp)
{

    UserCreatableClass *ucc = USER_CREATABLE_GET_CLASS(uc);

    if (ucc->can_be_deleted) {
        return ucc->can_be_deleted(uc, errp);
    } else {
        return true;
    }
}

Object *user_creatable_add(const char *type, const char *id,
                           const QDict *qdict,
                           Visitor *v, Error **errp)
{
    Object *obj;
    ObjectClass *klass;
    const QDictEntry *e;
    Error *local_err = NULL;

    klass = object_class_by_name(type);
    if (!klass) {
        error_setg(errp, "invalid object type: %s", type);
        return NULL;
    }

    if (!object_class_dynamic_cast(klass, TYPE_USER_CREATABLE)) {
        error_setg(errp, "object type '%s' isn't supported by object-add",
                   type);
        return NULL;
    }

    if (object_class_is_abstract(klass)) {
        error_setg(errp, "object type '%s' is abstract", type);
        return NULL;
    }

    obj = object_new(type);
    if (qdict) {
        for (e = qdict_first(qdict); e; e = qdict_next(qdict, e)) {
            object_property_set(obj, v, e->key, &local_err);
            if (local_err) {
                goto out;
            }
        }
    }

    object_property_add_child(object_get_objects_root(),
                              id, obj, &local_err);
    if (local_err) {
        goto out;
    }

    user_creatable_complete(obj, &local_err);
    if (local_err) {
        object_property_del(object_get_objects_root(),
                            id, &error_abort);
        goto out;
    }
out:
    if (local_err) {
        error_propagate(errp, local_err);
        object_unref(obj);
        return NULL;
    }
    return obj;
}

void user_creatable_del(const char *id, Error **errp)
{
    Object *container;
    Object *obj;

    container = object_get_objects_root();
    obj = object_resolve_path_component(container, id);
    if (!obj) {
        error_setg(errp, "object id not found");
        return;
    }

    if (!user_creatable_can_be_deleted(USER_CREATABLE(obj), errp)) {
        error_setg(errp, "%s is in use, can not be deleted", id);
        return;
    }
    object_unparent(obj);
}

static void register_types(void)
{
    static const TypeInfo uc_interface_info = {
        .name          = TYPE_USER_CREATABLE,
        .parent        = TYPE_INTERFACE,
        .class_size = sizeof(UserCreatableClass),
    };

    type_register_static(&uc_interface_info);
}

type_init(register_types)
