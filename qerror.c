/*
 * QError Module
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "monitor.h"
#include "qjson.h"
#include "qerror.h"
#include "qemu-common.h"

static void qerror_destroy_obj(QObject *obj);

static const QType qerror_type = {
    .code = QTYPE_QERROR,
    .destroy = qerror_destroy_obj,
};

/**
 * The 'desc' parameter is a printf-like string, the format of the format
 * string is:
 *
 * %(KEY)
 *
 * Where KEY is a QDict key, which has to be passed to qerror_from_info().
 *
 * Example:
 *
 * "foo error on device: %(device) slot: %(slot_nr)"
 *
 * A single percent sign can be printed if followed by a second one,
 * for example:
 *
 * "running out of foo: %(foo)%%"
 *
 * Please keep the entries in alphabetical order.
 * Use scripts/check-qerror.sh to check.
 */
static const QErrorStringTable qerror_table[] = {
    {
         QERR_ADD_CLIENT_FAILED,
         "Could not add client",
    },
    {
         QERR_AMBIGUOUS_PATH,
         "Path '%(path)' does not uniquely identify an object"
    },
    {
         QERR_BAD_BUS_FOR_DEVICE,
         "Device '%(device)' can't go on a %(bad_bus_type) bus",
    },
    {
         QERR_BASE_NOT_FOUND,
         "Base '%(base)' not found",
    },
    {
         QERR_BLOCK_FORMAT_FEATURE_NOT_SUPPORTED,
         "Block format '%(format)' used by device '%(name)' does not support feature '%(feature)'",
    },
    {
         QERR_BUS_NO_HOTPLUG,
         "Bus '%(bus)' does not support hotplugging",
    },
    {
         QERR_BUS_NOT_FOUND,
         "Bus '%(bus)' not found",
    },
    {
         QERR_COMMAND_DISABLED,
         "The command %(name) has been disabled for this instance",
    },
    {
         QERR_COMMAND_NOT_FOUND,
         "The command %(name) has not been found",
    },
    {
         QERR_DEVICE_ENCRYPTED,
         "'%(device)' (%(filename)) is encrypted",
    },
    {
         QERR_DEVICE_FEATURE_BLOCKS_MIGRATION,
         "Migration is disabled when using feature '%(feature)' in device '%(device)'",
    },
    {
         QERR_DEVICE_HAS_NO_MEDIUM,
         "Device '%(device)' has no medium",
    },
    {
         QERR_DEVICE_INIT_FAILED,
         "Device '%(device)' could not be initialized",
    },
    {
         QERR_DEVICE_IN_USE,
         "Device '%(device)' is in use",
    },
    {
         QERR_DEVICE_IS_READ_ONLY,
         "Device '%(device)' is read only",
    },
    {
         QERR_DEVICE_LOCKED,
         "Device '%(device)' is locked",
    },
    {
         QERR_DEVICE_MULTIPLE_BUSSES,
         "Device '%(device)' has multiple child busses",
    },
    {
         QERR_DEVICE_NO_BUS,
         "Device '%(device)' has no child bus",
    },
    {
         QERR_DEVICE_NO_HOTPLUG,
         "Device '%(device)' does not support hotplugging",
    },
    {
         QERR_DEVICE_NOT_ACTIVE,
         "Device '%(device)' has not been activated",
    },
    {
         QERR_DEVICE_NOT_ENCRYPTED,
         "Device '%(device)' is not encrypted",
    },
    {
         QERR_DEVICE_NOT_FOUND,
         "Device '%(device)' not found",
    },
    {
         QERR_DEVICE_NOT_REMOVABLE,
         "Device '%(device)' is not removable",
    },
    {
         QERR_DUPLICATE_ID,
         "Duplicate ID '%(id)' for %(object)",
    },
    {
         QERR_FD_NOT_FOUND,
         "File descriptor named '%(name)' not found",
    },
    {
         QERR_FD_NOT_SUPPLIED,
         "No file descriptor supplied via SCM_RIGHTS",
    },
    {
         QERR_FEATURE_DISABLED,
         "The feature '%(name)' is not enabled",
    },
    {
         QERR_INVALID_BLOCK_FORMAT,
         "Invalid block format '%(name)'",
    },
    {
         QERR_INVALID_OPTION_GROUP,
         "There is no option group '%(group)'",
    },
    {
         QERR_INVALID_PARAMETER,
         "Invalid parameter '%(name)'",
    },
    {
         QERR_INVALID_PARAMETER_COMBINATION,
         "Invalid parameter combination",
    },
    {
         QERR_INVALID_PARAMETER_TYPE,
         "Invalid parameter type for '%(name)', expected: %(expected)",
    },
    {
         QERR_INVALID_PARAMETER_VALUE,
         "Parameter '%(name)' expects %(expected)",
    },
    {
         QERR_INVALID_PASSWORD,
         "Password incorrect",
    },
    {
         QERR_IO_ERROR,
         "An IO error has occurred",
    },
    {
         QERR_JSON_PARSE_ERROR,
         "JSON parse error, %(message)",

    },
    {
         QERR_JSON_PARSING,
         "Invalid JSON syntax",
    },
    {
         QERR_KVM_MISSING_CAP,
         "Using KVM without %(capability), %(feature) unavailable",
    },
    {
         QERR_MIGRATION_ACTIVE,
         "There's a migration process in progress",
    },
    {
         QERR_MIGRATION_NOT_SUPPORTED,
         "State blocked by non-migratable device '%(device)'",
    },
    {
         QERR_MIGRATION_EXPECTED,
         "An incoming migration is expected before this command can be executed",
    },
    {
         QERR_MISSING_PARAMETER,
         "Parameter '%(name)' is missing",
    },
    {
         QERR_NO_BUS_FOR_DEVICE,
         "No '%(bus)' bus found for device '%(device)'",
    },
    {
         QERR_NOT_SUPPORTED,
         "Not supported",
    },
    {
         QERR_OPEN_FILE_FAILED,
         "Could not open '%(filename)'",
    },
    {
         QERR_PERMISSION_DENIED,
         "Insufficient permission to perform this operation",
    },
    {
         QERR_PROPERTY_NOT_FOUND,
         "Property '%(device).%(property)' not found",
    },
    {
         QERR_PROPERTY_VALUE_BAD,
         "Property '%(device).%(property)' doesn't take value '%(value)'",
    },
    {
         QERR_PROPERTY_VALUE_IN_USE,
         "Property '%(device).%(property)' can't take value '%(value)', it's in use",
    },
    {
         QERR_PROPERTY_VALUE_NOT_FOUND,
         "Property '%(device).%(property)' can't find value '%(value)'",
    },
    {
         QERR_PROPERTY_VALUE_NOT_POWER_OF_2,
         "Property '%(device).%(property)' doesn't take "
                     "value '%(value)', it's not a power of 2",
    },
    {
         QERR_PROPERTY_VALUE_OUT_OF_RANGE,
         "Property '%(device).%(property)' doesn't take "
                     "value %(value) (minimum: %(min), maximum: %(max))",
    },
    {
         QERR_QGA_COMMAND_FAILED,
         "Guest agent command failed, error was '%(message)'",
    },
    {
         QERR_QGA_LOGGING_FAILED,
         "Guest agent failed to log non-optional log statement",
    },
    {
         QERR_QMP_BAD_INPUT_OBJECT,
         "Expected '%(expected)' in QMP input",
    },
    {
         QERR_QMP_BAD_INPUT_OBJECT_MEMBER,
         "QMP input object member '%(member)' expects '%(expected)'",
    },
    {
         QERR_QMP_EXTRA_MEMBER,
         "QMP input object member '%(member)' is unexpected",
    },
    {
         QERR_RESET_REQUIRED,
         "Resetting the Virtual Machine is required",
    },
    {
         QERR_SET_PASSWD_FAILED,
         "Could not set password",
    },
    {
         QERR_TOO_MANY_FILES,
         "Too many open files",
    },
    {
         QERR_UNDEFINED_ERROR,
         "An undefined error has occurred",
    },
    {
         QERR_UNKNOWN_BLOCK_FORMAT_FEATURE,
         "'%(device)' uses a %(format) feature which is not "
                     "supported by this qemu version: %(feature)",
    },
    {
         QERR_UNSUPPORTED,
         "this feature or command is not currently supported",
    },
    {
         QERR_VIRTFS_FEATURE_BLOCKS_MIGRATION,
         "Migration is disabled when VirtFS export path '%(path)' "
                     "is mounted in the guest using mount_tag '%(tag)'",
    },
    {
         QERR_VNC_SERVER_FAILED,
         "Could not start VNC server on %(target)",
    },
    {
         QERR_SOCKET_CONNECT_FAILED,
         "Failed to connect to socket",
    },
    {
         QERR_SOCKET_LISTEN_FAILED,
         "Failed to set socket to listening mode",
    },
    {
         QERR_SOCKET_BIND_FAILED,
         "Failed to bind socket",
    },
    {
         QERR_SOCKET_CREATE_FAILED,
         "Failed to create socket",
    },
    {}
};

/**
 * qerror_new(): Create a new QError
 *
 * Return strong reference.
 */
static QError *qerror_new(void)
{
    QError *qerr;

    qerr = g_malloc0(sizeof(*qerr));
    QOBJECT_INIT(qerr, &qerror_type);

    return qerr;
}

static QDict *error_obj_from_fmt_no_fail(const char *fmt, va_list *va)
{
    QObject *obj;
    QDict *ret;

    obj = qobject_from_jsonv(fmt, va);
    if (!obj) {
        fprintf(stderr, "invalid json in error dict '%s'\n", fmt);
        abort();
    }
    if (qobject_type(obj) != QTYPE_QDICT) {
        fprintf(stderr, "error is not a dict '%s'\n", fmt);
        abort();
    }

    ret = qobject_to_qdict(obj);
    obj = qdict_get(ret, "class");
    if (!obj) {
        fprintf(stderr, "missing 'class' key in '%s'\n", fmt);
        abort();
    }
    if (qobject_type(obj) != QTYPE_QSTRING) {
        fprintf(stderr, "'class' key value should be a string in '%s'\n", fmt);
        abort();
    }

    obj = qdict_get(ret, "data");
    if (!obj) {
        fprintf(stderr, "missing 'data' key in '%s'\n", fmt);
        abort();
    }
    if (qobject_type(obj) != QTYPE_QDICT) {
        fprintf(stderr, "'data' key value should be a dict in '%s'\n", fmt);
        abort();
    }

    return ret;
}

/**
 * qerror_from_info(): Create a new QError from error information
 *
 * Return strong reference.
 */
static QError *qerror_from_info(ErrorClass err_class, const char *fmt,
                                va_list *va)
{
    QError *qerr;

    qerr = qerror_new();
    loc_save(&qerr->loc);

    qerr->err_class = err_class;
    qerr->error = error_obj_from_fmt_no_fail(fmt, va);
    qerr->err_msg = qerror_format(fmt, qerr->error);

    return qerr;
}

static void parse_error(const QErrorStringTable *entry, int c)
{
    fprintf(stderr, "expected '%c' in '%s'", c, entry->desc);
    abort();
}

static const char *append_field(QDict *error, QString *outstr,
                                const QErrorStringTable *entry,
                                const char *start)
{
    QObject *obj;
    QDict *qdict;
    QString *key_qs;
    const char *end, *key;

    if (*start != '%')
        parse_error(entry, '%');
    start++;
    if (*start != '(')
        parse_error(entry, '(');
    start++;

    end = strchr(start, ')');
    if (!end)
        parse_error(entry, ')');

    key_qs = qstring_from_substr(start, 0, end - start - 1);
    key = qstring_get_str(key_qs);

    qdict = qobject_to_qdict(qdict_get(error, "data"));
    obj = qdict_get(qdict, key);
    if (!obj) {
        abort();
    }

    switch (qobject_type(obj)) {
        case QTYPE_QSTRING:
            qstring_append(outstr, qdict_get_str(qdict, key));
            break;
        case QTYPE_QINT:
            qstring_append_int(outstr, qdict_get_int(qdict, key));
            break;
        default:
            abort();
    }

    QDECREF(key_qs);
    return ++end;
}

static QString *qerror_format_desc(QDict *error,
                                   const QErrorStringTable *entry)
{
    QString *qstring;
    const char *p;

    assert(entry != NULL);

    qstring = qstring_new();

    for (p = entry->desc; *p != '\0';) {
        if (*p != '%') {
            qstring_append_chr(qstring, *p++);
        } else if (*(p + 1) == '%') {
            qstring_append_chr(qstring, '%');
            p += 2;
        } else {
            p = append_field(error, qstring, entry, p);
        }
    }

    return qstring;
}

char *qerror_format(const char *fmt, QDict *error)
{
    const QErrorStringTable *entry = NULL;
    QString *qstr;
    char *ret;
    int i;

    for (i = 0; qerror_table[i].error_fmt; i++) {
        if (strcmp(qerror_table[i].error_fmt, fmt) == 0) {
            entry = &qerror_table[i];
            break;
        }
    }

    qstr = qerror_format_desc(error, entry);
    ret = g_strdup(qstring_get_str(qstr));
    QDECREF(qstr);

    return ret;
}

/**
 * qerror_human(): Format QError data into human-readable string.
 */
QString *qerror_human(const QError *qerror)
{
    return qstring_from_str(qerror->err_msg);
}

/**
 * qerror_print(): Print QError data
 *
 * This function will print the member 'desc' of the specified QError object,
 * it uses error_report() for this, so that the output is routed to the right
 * place (ie. stderr or Monitor's device).
 */
static void qerror_print(QError *qerror)
{
    QString *qstring = qerror_human(qerror);
    loc_push_restore(&qerror->loc);
    error_report("%s", qstring_get_str(qstring));
    loc_pop(&qerror->loc);
    QDECREF(qstring);
}

void qerror_report(ErrorClass eclass, const char *fmt, ...)
{
    va_list va;
    QError *qerror;

    va_start(va, fmt);
    qerror = qerror_from_info(eclass, fmt, &va);
    va_end(va);

    if (monitor_cur_is_qmp()) {
        monitor_set_error(cur_mon, qerror);
    } else {
        qerror_print(qerror);
        QDECREF(qerror);
    }
}

/* Evil... */
struct Error
{
    QDict *obj;
    char *msg;
    ErrorClass err_class;
};

void qerror_report_err(Error *err)
{
    QError *qerr;

    qerr = qerror_new();
    loc_save(&qerr->loc);
    QINCREF(err->obj);
    qerr->error = err->obj;
    qerr->err_msg = g_strdup(err->msg);
    qerr->err_class = err->err_class;

    if (monitor_cur_is_qmp()) {
        monitor_set_error(cur_mon, qerr);
    } else {
        qerror_print(qerr);
        QDECREF(qerr);
    }
}

void assert_no_error(Error *err)
{
    if (err) {
        qerror_report_err(err);
        abort();
    }
}

/**
 * qobject_to_qerror(): Convert a QObject into a QError
 */
static QError *qobject_to_qerror(const QObject *obj)
{
    if (qobject_type(obj) != QTYPE_QERROR) {
        return NULL;
    }

    return container_of(obj, QError, base);
}

/**
 * qerror_destroy_obj(): Free all memory allocated by a QError
 */
static void qerror_destroy_obj(QObject *obj)
{
    QError *qerr;

    assert(obj != NULL);
    qerr = qobject_to_qerror(obj);

    QDECREF(qerr->error);
    g_free(qerr->err_msg);
    g_free(qerr);
}
