#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qom/object_interfaces.h"
#include "qemu/module.h"
#include "qapi-visit.h"
#include "qapi/opts-visitor.h"

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

Object *user_creatable_add_type(const char *type, const char *id,
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

    assert(qdict);
    obj = object_new(type);
    if (object_property_find(obj, "id", NULL)) {
        object_property_set_str(obj, id, "id", &local_err);
        if (local_err) {
            goto out;
        }
    }
    visit_start_struct(v, NULL, NULL, 0, &local_err);
    if (local_err) {
        goto out;
    }
    for (e = qdict_first(qdict); e; e = qdict_next(qdict, e)) {
        object_property_set(obj, v, e->key, &local_err);
        if (local_err) {
            break;
        }
    }
    if (!local_err) {
        visit_check_struct(v, &local_err);
    }
    visit_end_struct(v, NULL);
    if (local_err) {
        goto out;
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


Object *user_creatable_add_opts(QemuOpts *opts, Error **errp)
{
    Visitor *v;
    QDict *pdict;
    Object *obj;
    const char *id = qemu_opts_id(opts);
    char *type = qemu_opt_get_del(opts, "qom-type");

    if (!type) {
        error_setg(errp, QERR_MISSING_PARAMETER, "qom-type");
        return NULL;
    }
    if (!id) {
        error_setg(errp, QERR_MISSING_PARAMETER, "id");
        qemu_opt_set(opts, "qom-type", type, &error_abort);
        g_free(type);
        return NULL;
    }

    qemu_opts_set_id(opts, NULL);
    pdict = qemu_opts_to_qdict(opts, NULL);

    v = opts_visitor_new(opts);
    obj = user_creatable_add_type(type, id, pdict, v, errp);
    visit_free(v);

    qemu_opts_set_id(opts, (char *) id);
    qemu_opt_set(opts, "qom-type", type, &error_abort);
    g_free(type);
    QDECREF(pdict);
    return obj;
}


int user_creatable_add_opts_foreach(void *opaque, QemuOpts *opts, Error **errp)
{
    bool (*type_predicate)(const char *) = opaque;
    Object *obj = NULL;
    Error *err = NULL;
    const char *type;

    type = qemu_opt_get(opts, "qom-type");
    if (type && type_predicate &&
        !type_predicate(type)) {
        return 0;
    }

    obj = user_creatable_add_opts(opts, &err);
    if (!obj) {
        error_report_err(err);
        return -1;
    }
    object_unref(obj);
    return 0;
}


void user_creatable_del(const char *id, Error **errp)
{
    Object *container;
    Object *obj;

    container = object_get_objects_root();
    obj = object_resolve_path_component(container, id);
    if (!obj) {
        error_setg(errp, "object '%s' not found", id);
        return;
    }

    if (!user_creatable_can_be_deleted(USER_CREATABLE(obj), errp)) {
        error_setg(errp, "object '%s' is in use, can not be deleted", id);
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
