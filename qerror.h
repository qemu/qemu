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
#ifndef QERROR_H
#define QERROR_H

#include "qdict.h"
#include "qstring.h"
#include "qemu-error.h"
#include "error.h"
#include "qapi-types.h"
#include <stdarg.h>

typedef struct QErrorStringTable {
    ErrorClass err_class;
    const char *error_fmt;
    const char *desc;
} QErrorStringTable;

typedef struct QError {
    QObject_HEAD;
    QDict *error;
    Location loc;
    char *err_msg;
    ErrorClass err_class;
} QError;

QString *qerror_human(const QError *qerror);
void qerror_report(ErrorClass err_class, const char *fmt, ...) GCC_FMT_ATTR(2, 3);
void qerror_report_err(Error *err);
void assert_no_error(Error *err);
char *qerror_format(const char *fmt, QDict *error);

/*
 * QError class list
 * Please keep the definitions in alphabetical order.
 * Use scripts/check-qerror.sh to check.
 */
#define QERR_ADD_CLIENT_FAILED \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'AddClientFailed', 'data': {} }"

#define QERR_AMBIGUOUS_PATH \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'AmbiguousPath', 'data': { 'path': %s } }"

#define QERR_BAD_BUS_FOR_DEVICE \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'BadBusForDevice', 'data': { 'device': %s, 'bad_bus_type': %s } }"

#define QERR_BASE_NOT_FOUND \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'BaseNotFound', 'data': { 'base': %s } }"

#define QERR_BLOCK_FORMAT_FEATURE_NOT_SUPPORTED \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'BlockFormatFeatureNotSupported', 'data': { 'format': %s, 'name': %s, 'feature': %s } }"

#define QERR_BUFFER_OVERRUN \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'BufferOverrun', 'data': {} }"

#define QERR_BUS_NO_HOTPLUG \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'BusNoHotplug', 'data': { 'bus': %s } }"

#define QERR_BUS_NOT_FOUND \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'BusNotFound', 'data': { 'bus': %s } }"

#define QERR_COMMAND_DISABLED \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'CommandDisabled', 'data': { 'name': %s } }"

#define QERR_COMMAND_NOT_FOUND \
    ERROR_CLASS_COMMAND_NOT_FOUND, "{ 'class': 'CommandNotFound', 'data': { 'name': %s } }"

#define QERR_DEVICE_ENCRYPTED \
    ERROR_CLASS_DEVICE_ENCRYPTED, "{ 'class': 'DeviceEncrypted', 'data': { 'device': %s, 'filename': %s } }"

#define QERR_DEVICE_FEATURE_BLOCKS_MIGRATION \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'DeviceFeatureBlocksMigration', 'data': { 'device': %s, 'feature': %s } }"

#define QERR_DEVICE_HAS_NO_MEDIUM \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'DeviceHasNoMedium', 'data': { 'device': %s } }"

#define QERR_DEVICE_INIT_FAILED \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'DeviceInitFailed', 'data': { 'device': %s } }"

#define QERR_DEVICE_IN_USE \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'DeviceInUse', 'data': { 'device': %s } }"

#define QERR_DEVICE_IS_READ_ONLY \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'DeviceIsReadOnly', 'data': { 'device': %s } }"

#define QERR_DEVICE_LOCKED \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'DeviceLocked', 'data': { 'device': %s } }"

#define QERR_DEVICE_MULTIPLE_BUSSES \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'DeviceMultipleBusses', 'data': { 'device': %s } }"

#define QERR_DEVICE_NO_BUS \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'DeviceNoBus', 'data': { 'device': %s } }"

#define QERR_DEVICE_NO_HOTPLUG \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'DeviceNoHotplug', 'data': { 'device': %s } }"

#define QERR_DEVICE_NOT_ACTIVE \
    ERROR_CLASS_DEVICE_NOT_ACTIVE, "{ 'class': 'DeviceNotActive', 'data': { 'device': %s } }"

#define QERR_DEVICE_NOT_ENCRYPTED \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'DeviceNotEncrypted', 'data': { 'device': %s } }"

#define QERR_DEVICE_NOT_FOUND \
    ERROR_CLASS_DEVICE_NOT_FOUND, "{ 'class': 'DeviceNotFound', 'data': { 'device': %s } }"

#define QERR_DEVICE_NOT_REMOVABLE \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'DeviceNotRemovable', 'data': { 'device': %s } }"

#define QERR_DUPLICATE_ID \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'DuplicateId', 'data': { 'id': %s, 'object': %s } }"

#define QERR_FD_NOT_FOUND \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'FdNotFound', 'data': { 'name': %s } }"

#define QERR_FD_NOT_SUPPLIED \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'FdNotSupplied', 'data': {} }"

#define QERR_FEATURE_DISABLED \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'FeatureDisabled', 'data': { 'name': %s } }"

#define QERR_INVALID_BLOCK_FORMAT \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'InvalidBlockFormat', 'data': { 'name': %s } }"

#define QERR_INVALID_OPTION_GROUP \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'InvalidOptionGroup', 'data': { 'group': %s } }"

#define QERR_INVALID_PARAMETER \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'InvalidParameter', 'data': { 'name': %s } }"

#define QERR_INVALID_PARAMETER_COMBINATION \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'InvalidParameterCombination', 'data': {} }"

#define QERR_INVALID_PARAMETER_TYPE \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'InvalidParameterType', 'data': { 'name': %s,'expected': %s } }"

#define QERR_INVALID_PARAMETER_VALUE \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'InvalidParameterValue', 'data': { 'name': %s, 'expected': %s } }"

#define QERR_INVALID_PASSWORD \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'InvalidPassword', 'data': {} }"

#define QERR_IO_ERROR \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'IOError', 'data': {} }"

#define QERR_JSON_PARSE_ERROR \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'JSONParseError', 'data': { 'message': %s } }"

#define QERR_JSON_PARSING \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'JSONParsing', 'data': {} }"

#define QERR_KVM_MISSING_CAP \
    ERROR_CLASS_K_V_M_MISSING_CAP, "{ 'class': 'KVMMissingCap', 'data': { 'capability': %s, 'feature': %s } }"

#define QERR_MIGRATION_ACTIVE \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'MigrationActive', 'data': {} }"

#define QERR_MIGRATION_NOT_SUPPORTED \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'MigrationNotSupported', 'data': {'device': %s} }"

#define QERR_MIGRATION_EXPECTED \
    ERROR_CLASS_MIGRATION_EXPECTED, "{ 'class': 'MigrationExpected', 'data': {} }"

#define QERR_MISSING_PARAMETER \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'MissingParameter', 'data': { 'name': %s } }"

#define QERR_NO_BUS_FOR_DEVICE \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'NoBusForDevice', 'data': { 'device': %s, 'bus': %s } }"

#define QERR_NOT_SUPPORTED \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'NotSupported', 'data': {} }"

#define QERR_OPEN_FILE_FAILED \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'OpenFileFailed', 'data': { 'filename': %s } }"

#define QERR_PERMISSION_DENIED \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'PermissionDenied', 'data': {} }"

#define QERR_PROPERTY_NOT_FOUND \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'PropertyNotFound', 'data': { 'device': %s, 'property': %s } }"

#define QERR_PROPERTY_VALUE_BAD \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'PropertyValueBad', 'data': { 'device': %s, 'property': %s, 'value': %s } }"

#define QERR_PROPERTY_VALUE_IN_USE \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'PropertyValueInUse', 'data': { 'device': %s, 'property': %s, 'value': %s } }"

#define QERR_PROPERTY_VALUE_NOT_FOUND \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'PropertyValueNotFound', 'data': { 'device': %s, 'property': %s, 'value': %s } }"

#define QERR_PROPERTY_VALUE_NOT_POWER_OF_2 \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'PropertyValueNotPowerOf2', 'data': { " \
    "'device': %s, 'property': %s, 'value': %"PRId64" } }"

#define QERR_PROPERTY_VALUE_OUT_OF_RANGE \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'PropertyValueOutOfRange', 'data': { 'device': %s, 'property': %s, 'value': %"PRId64", 'min': %"PRId64", 'max': %"PRId64" } }"

#define QERR_QGA_COMMAND_FAILED \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'QgaCommandFailed', 'data': { 'message': %s } }"

#define QERR_QGA_LOGGING_FAILED \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'QgaLoggingFailed', 'data': {} }"

#define QERR_QMP_BAD_INPUT_OBJECT \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'QMPBadInputObject', 'data': { 'expected': %s } }"

#define QERR_QMP_BAD_INPUT_OBJECT_MEMBER \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'QMPBadInputObjectMember', 'data': { 'member': %s, 'expected': %s } }"

#define QERR_QMP_EXTRA_MEMBER \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'QMPExtraInputObjectMember', 'data': { 'member': %s } }"

#define QERR_RESET_REQUIRED \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'ResetRequired', 'data': {} }"

#define QERR_SET_PASSWD_FAILED \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'SetPasswdFailed', 'data': {} }"

#define QERR_TOO_MANY_FILES \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'TooManyFiles', 'data': {} }"

#define QERR_UNDEFINED_ERROR \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'UndefinedError', 'data': {} }"

#define QERR_UNKNOWN_BLOCK_FORMAT_FEATURE \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'UnknownBlockFormatFeature', 'data': { 'device': %s, 'format': %s, 'feature': %s } }"

#define QERR_UNSUPPORTED \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'Unsupported', 'data': {} }"

#define QERR_VIRTFS_FEATURE_BLOCKS_MIGRATION \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'VirtFSFeatureBlocksMigration', 'data': { 'path': %s, 'tag': %s } }"

#define QERR_VNC_SERVER_FAILED \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'VNCServerFailed', 'data': { 'target': %s } }"

#define QERR_SOCKET_CONNECT_FAILED \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'SockConnectFailed', 'data': {} }"

#define QERR_SOCKET_LISTEN_FAILED \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'SockListenFailed', 'data': {} }"

#define QERR_SOCKET_BIND_FAILED \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'SockBindFailed', 'data': {} }"

#define QERR_SOCKET_CREATE_FAILED \
    ERROR_CLASS_GENERIC_ERROR, "{ 'class': 'SockCreateFailed', 'data': {} }"

#endif /* QERROR_H */
