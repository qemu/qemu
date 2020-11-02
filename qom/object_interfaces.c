#include "qemu/osdep.h"

#include "qemu/cutils.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-qom.h"
#include "qapi/qapi-visit-qom.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qobject-output-visitor.h"
#include "qom/object_interfaces.h"
#include "qemu/help_option.h"
#include "qemu/id.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/qemu-print.h"
#include "qapi/opts-visitor.h"
#include "qemu/config-file.h"

bool user_creatable_complete(UserCreatable *uc, Error **errp)
{
    UserCreatableClass *ucc = USER_CREATABLE_GET_CLASS(uc);
    Error *err = NULL;

    if (ucc->complete) {
        ucc->complete(uc, &err);
        error_propagate(errp, err);
    }
    return !err;
}

bool user_creatable_can_be_deleted(UserCreatable *uc)
{

    UserCreatableClass *ucc = USER_CREATABLE_GET_CLASS(uc);

    if (ucc->can_be_deleted) {
        return ucc->can_be_deleted(uc);
    } else {
        return true;
    }
}

static void object_set_properties_from_qdict(Object *obj, const QDict *qdict,
                                             Visitor *v, Error **errp)
{
    const QDictEntry *e;
    Error *local_err = NULL;

    if (!visit_start_struct(v, NULL, NULL, 0, &local_err)) {
        goto out;
    }
    for (e = qdict_first(qdict); e; e = qdict_next(qdict, e)) {
        if (!object_property_set(obj, e->key, v, &local_err)) {
            break;
        }
    }
    if (!local_err) {
        visit_check_struct(v, &local_err);
    }
    visit_end_struct(v, NULL);

out:
    if (local_err) {
        error_propagate(errp, local_err);
    }
}

void object_set_properties_from_keyval(Object *obj, const QDict *qdict,
                                       bool from_json, Error **errp)
{
    Visitor *v;
    if (from_json) {
        v = qobject_input_visitor_new(QOBJECT(qdict));
    } else {
        v = qobject_input_visitor_new_keyval(QOBJECT(qdict));
    }
    object_set_properties_from_qdict(obj, qdict, v, errp);
    visit_free(v);
}

Object *user_creatable_add_type(const char *type, const char *id,
                                const QDict *qdict,
                                Visitor *v, Error **errp)
{
    ERRP_GUARD();
    Object *obj;
    ObjectClass *klass;
    Error *local_err = NULL;

    if (id != NULL && !id_wellformed(id)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "id", "an identifier");
        error_append_hint(errp, "Identifiers consist of letters, digits, "
                          "'-', '.', '_', starting with a letter.\n");
        return NULL;
    }

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
    object_set_properties_from_qdict(obj, qdict, v, &local_err);
    if (local_err) {
        goto out;
    }

    if (id != NULL) {
        object_property_try_add_child(object_get_objects_root(),
                                      id, obj, &local_err);
        if (local_err) {
            goto out;
        }
    }

    if (!user_creatable_complete(USER_CREATABLE(obj), &local_err)) {
        if (id != NULL) {
            object_property_del(object_get_objects_root(), id);
        }
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

void user_creatable_add_qapi(ObjectOptions *options, Error **errp)
{
    Visitor *v;
    QObject *qobj;
    QDict *props;
    Object *obj;

    v = qobject_output_visitor_new(&qobj);
    visit_type_ObjectOptions(v, NULL, &options, &error_abort);
    visit_complete(v, &qobj);
    visit_free(v);

    props = qobject_to(QDict, qobj);
    qdict_del(props, "qom-type");
    qdict_del(props, "id");

    v = qobject_input_visitor_new(QOBJECT(props));
    obj = user_creatable_add_type(ObjectType_str(options->qom_type),
                                  options->id, props, v, errp);
    object_unref(obj);
    qobject_unref(qobj);
    visit_free(v);
}

char *object_property_help(const char *name, const char *type,
                           QObject *defval, const char *description)
{
    GString *str = g_string_new(NULL);

    g_string_append_printf(str, "  %s=<%s>", name, type);
    if (description || defval) {
        if (str->len < 24) {
            g_string_append_printf(str, "%*s", 24 - (int)str->len, "");
        }
        g_string_append(str, " - ");
    }
    if (description) {
        g_string_append(str, description);
    }
    if (defval) {
        g_autofree char *def_json = g_string_free(qobject_to_json(defval),
                                                  false);
        g_string_append_printf(str, " (default: %s)", def_json);
    }

    return g_string_free(str, false);
}

static void user_creatable_print_types(void)
{
    GSList *l, *list;

    qemu_printf("List of user creatable objects:\n");
    list = object_class_get_list_sorted(TYPE_USER_CREATABLE, false);
    for (l = list; l != NULL; l = l->next) {
        ObjectClass *oc = OBJECT_CLASS(l->data);
        qemu_printf("  %s\n", object_class_get_name(oc));
    }
    g_slist_free(list);
}

bool type_print_class_properties(const char *type)
{
    ObjectClass *klass;
    ObjectPropertyIterator iter;
    ObjectProperty *prop;
    GPtrArray *array;
    int i;

    klass = object_class_by_name(type);
    if (!klass) {
        return false;
    }

    array = g_ptr_array_new();
    object_class_property_iter_init(&iter, klass);
    while ((prop = object_property_iter_next(&iter))) {
        if (!prop->set) {
            continue;
        }

        g_ptr_array_add(array,
                        object_property_help(prop->name, prop->type,
                                             prop->defval, prop->description));
    }
    g_ptr_array_sort(array, (GCompareFunc)qemu_pstrcmp0);
    if (array->len > 0) {
        qemu_printf("%s options:\n", type);
    } else {
        qemu_printf("There are no options for %s.\n", type);
    }
    for (i = 0; i < array->len; i++) {
        qemu_printf("%s\n", (char *)array->pdata[i]);
    }
    g_ptr_array_set_free_func(array, g_free);
    g_ptr_array_free(array, true);
    return true;
}

bool user_creatable_print_help(const char *type, QemuOpts *opts)
{
    if (is_help_option(type)) {
        user_creatable_print_types();
        return true;
    }

    if (qemu_opt_has_help_opt(opts)) {
        return type_print_class_properties(type);
    }

    return false;
}

static void user_creatable_print_help_from_qdict(QDict *args)
{
    const char *type = qdict_get_try_str(args, "qom-type");

    if (!type || !type_print_class_properties(type)) {
        user_creatable_print_types();
    }
}

ObjectOptions *user_creatable_parse_str(const char *optarg, Error **errp)
{
    ERRP_GUARD();
    QObject *obj;
    bool help;
    Visitor *v;
    ObjectOptions *options;

    if (optarg[0] == '{') {
        obj = qobject_from_json(optarg, errp);
        if (!obj) {
            return NULL;
        }
        v = qobject_input_visitor_new(obj);
    } else {
        QDict *args = keyval_parse(optarg, "qom-type", &help, errp);
        if (*errp) {
            return NULL;
        }
        if (help) {
            user_creatable_print_help_from_qdict(args);
            qobject_unref(args);
            return NULL;
        }

        obj = QOBJECT(args);
        v = qobject_input_visitor_new_keyval(obj);
    }

    visit_type_ObjectOptions(v, NULL, &options, errp);
    visit_free(v);
    qobject_unref(obj);

    return options;
}

bool user_creatable_add_from_str(const char *optarg, Error **errp)
{
    ERRP_GUARD();
    ObjectOptions *options;

    options = user_creatable_parse_str(optarg, errp);
    if (!options) {
        return false;
    }

    user_creatable_add_qapi(options, errp);
    qapi_free_ObjectOptions(options);
    return !*errp;
}

void user_creatable_process_cmdline(const char *optarg)
{
    if (!user_creatable_add_from_str(optarg, &error_fatal)) {
        /* Help was printed */
        exit(EXIT_SUCCESS);
    }
}

bool user_creatable_del(const char *id, Error **errp)
{
    QemuOptsList *opts_list;
    Object *container;
    Object *obj;

    container = object_get_objects_root();
    obj = object_resolve_path_component(container, id);
    if (!obj) {
        error_setg(errp, "object '%s' not found", id);
        return false;
    }

    if (!user_creatable_can_be_deleted(USER_CREATABLE(obj))) {
        error_setg(errp, "object '%s' is in use, can not be deleted", id);
        return false;
    }

    /*
     * if object was defined on the command-line, remove its corresponding
     * option group entry
     */
    opts_list = qemu_find_opts_err("object", NULL);
    if (opts_list) {
        qemu_opts_del(qemu_opts_find(opts_list, id));
    }

    object_unparent(obj);
    return true;
}

void user_creatable_cleanup(void)
{
    object_unparent(object_get_objects_root());
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
