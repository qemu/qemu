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
    -1, "{ 'class': 'AddClientFailed', 'data': {} }"

#define QERR_AMBIGUOUS_PATH \
    -1, "{ 'class': 'AmbiguousPath', 'data': { 'path': %s } }"

#define QERR_BAD_BUS_FOR_DEVICE \
    -1, "{ 'class': 'BadBusForDevice', 'data': { 'device': %s, 'bad_bus_type': %s } }"

#define QERR_BASE_NOT_FOUND \
    -1, "{ 'class': 'BaseNotFound', 'data': { 'base': %s } }"

#define QERR_BLOCK_FORMAT_FEATURE_NOT_SUPPORTED \
    -1, "{ 'class': 'BlockFormatFeatureNotSupported', 'data': { 'format': %s, 'name': %s, 'feature': %s } }"

#define QERR_BUFFER_OVERRUN \
    -1, "{ 'class': 'BufferOverrun', 'data': {} }"

#define QERR_BUS_NO_HOTPLUG \
    -1, "{ 'class': 'BusNoHotplug', 'data': { 'bus': %s } }"

#define QERR_BUS_NOT_FOUND \
    -1, "{ 'class': 'BusNotFound', 'data': { 'bus': %s } }"

#define QERR_COMMAND_DISABLED \
    -1, "{ 'class': 'CommandDisabled', 'data': { 'name': %s } }"

#define QERR_COMMAND_NOT_FOUND \
    -1, "{ 'class': 'CommandNotFound', 'data': { 'name': %s } }"

#define QERR_DEVICE_ENCRYPTED \
    -1, "{ 'class': 'DeviceEncrypted', 'data': { 'device': %s, 'filename': %s } }"

#define QERR_DEVICE_FEATURE_BLOCKS_MIGRATION \
    -1, "{ 'class': 'DeviceFeatureBlocksMigration', 'data': { 'device': %s, 'feature': %s } }"

#define QERR_DEVICE_HAS_NO_MEDIUM \
    -1, "{ 'class': 'DeviceHasNoMedium', 'data': { 'device': %s } }"

#define QERR_DEVICE_INIT_FAILED \
    -1, "{ 'class': 'DeviceInitFailed', 'data': { 'device': %s } }"

#define QERR_DEVICE_IN_USE \
    -1, "{ 'class': 'DeviceInUse', 'data': { 'device': %s } }"

#define QERR_DEVICE_IS_READ_ONLY \
    -1, "{ 'class': 'DeviceIsReadOnly', 'data': { 'device': %s } }"

#define QERR_DEVICE_LOCKED \
    -1, "{ 'class': 'DeviceLocked', 'data': { 'device': %s } }"

#define QERR_DEVICE_MULTIPLE_BUSSES \
    -1, "{ 'class': 'DeviceMultipleBusses', 'data': { 'device': %s } }"

#define QERR_DEVICE_NO_BUS \
    -1, "{ 'class': 'DeviceNoBus', 'data': { 'device': %s } }"

#define QERR_DEVICE_NO_HOTPLUG \
    -1, "{ 'class': 'DeviceNoHotplug', 'data': { 'device': %s } }"

#define QERR_DEVICE_NOT_ACTIVE \
    -1, "{ 'class': 'DeviceNotActive', 'data': { 'device': %s } }"

#define QERR_DEVICE_NOT_ENCRYPTED \
    -1, "{ 'class': 'DeviceNotEncrypted', 'data': { 'device': %s } }"

#define QERR_DEVICE_NOT_FOUND \
    -1, "{ 'class': 'DeviceNotFound', 'data': { 'device': %s } }"

#define QERR_DEVICE_NOT_REMOVABLE \
    -1, "{ 'class': 'DeviceNotRemovable', 'data': { 'device': %s } }"

#define QERR_DUPLICATE_ID \
    -1, "{ 'class': 'DuplicateId', 'data': { 'id': %s, 'object': %s } }"

#define QERR_FD_NOT_FOUND \
    -1, "{ 'class': 'FdNotFound', 'data': { 'name': %s } }"

#define QERR_FD_NOT_SUPPLIED \
    -1, "{ 'class': 'FdNotSupplied', 'data': {} }"

#define QERR_FEATURE_DISABLED \
    -1, "{ 'class': 'FeatureDisabled', 'data': { 'name': %s } }"

#define QERR_INVALID_BLOCK_FORMAT \
    -1, "{ 'class': 'InvalidBlockFormat', 'data': { 'name': %s } }"

#define QERR_INVALID_OPTION_GROUP \
    -1, "{ 'class': 'InvalidOptionGroup', 'data': { 'group': %s } }"

#define QERR_INVALID_PARAMETER \
    -1, "{ 'class': 'InvalidParameter', 'data': { 'name': %s } }"

#define QERR_INVALID_PARAMETER_COMBINATION \
    -1, "{ 'class': 'InvalidParameterCombination', 'data': {} }"

#define QERR_INVALID_PARAMETER_TYPE \
    -1, "{ 'class': 'InvalidParameterType', 'data': { 'name': %s,'expected': %s } }"

#define QERR_INVALID_PARAMETER_VALUE \
    -1, "{ 'class': 'InvalidParameterValue', 'data': { 'name': %s, 'expected': %s } }"

#define QERR_INVALID_PASSWORD \
    -1, "{ 'class': 'InvalidPassword', 'data': {} }"

#define QERR_IO_ERROR \
    -1, "{ 'class': 'IOError', 'data': {} }"

#define QERR_JSON_PARSE_ERROR \
    -1, "{ 'class': 'JSONParseError', 'data': { 'message': %s } }"

#define QERR_JSON_PARSING \
    -1, "{ 'class': 'JSONParsing', 'data': {} }"

#define QERR_KVM_MISSING_CAP \
    -1, "{ 'class': 'KVMMissingCap', 'data': { 'capability': %s, 'feature': %s } }"

#define QERR_MIGRATION_ACTIVE \
    -1, "{ 'class': 'MigrationActive', 'data': {} }"

#define QERR_MIGRATION_NOT_SUPPORTED \
    -1, "{ 'class': 'MigrationNotSupported', 'data': {'device': %s} }"

#define QERR_MIGRATION_EXPECTED \
    -1, "{ 'class': 'MigrationExpected', 'data': {} }"

#define QERR_MISSING_PARAMETER \
    -1, "{ 'class': 'MissingParameter', 'data': { 'name': %s } }"

#define QERR_NO_BUS_FOR_DEVICE \
    -1, "{ 'class': 'NoBusForDevice', 'data': { 'device': %s, 'bus': %s } }"

#define QERR_NOT_SUPPORTED \
    -1, "{ 'class': 'NotSupported', 'data': {} }"

#define QERR_OPEN_FILE_FAILED \
    -1, "{ 'class': 'OpenFileFailed', 'data': { 'filename': %s } }"

#define QERR_PERMISSION_DENIED \
    -1, "{ 'class': 'PermissionDenied', 'data': {} }"

#define QERR_PROPERTY_NOT_FOUND \
    -1, "{ 'class': 'PropertyNotFound', 'data': { 'device': %s, 'property': %s } }"

#define QERR_PROPERTY_VALUE_BAD \
    -1, "{ 'class': 'PropertyValueBad', 'data': { 'device': %s, 'property': %s, 'value': %s } }"

#define QERR_PROPERTY_VALUE_IN_USE \
    -1, "{ 'class': 'PropertyValueInUse', 'data': { 'device': %s, 'property': %s, 'value': %s } }"

#define QERR_PROPERTY_VALUE_NOT_FOUND \
    -1, "{ 'class': 'PropertyValueNotFound', 'data': { 'device': %s, 'property': %s, 'value': %s } }"

#define QERR_PROPERTY_VALUE_NOT_POWER_OF_2 \
    -1, "{ 'class': 'PropertyValueNotPowerOf2', 'data': { " \
    "'device': %s, 'property': %s, 'value': %"PRId64" } }"

#define QERR_PROPERTY_VALUE_OUT_OF_RANGE \
    -1, "{ 'class': 'PropertyValueOutOfRange', 'data': { 'device': %s, 'property': %s, 'value': %"PRId64", 'min': %"PRId64", 'max': %"PRId64" } }"

#define QERR_QGA_COMMAND_FAILED \
    -1, "{ 'class': 'QgaCommandFailed', 'data': { 'message': %s } }"

#define QERR_QGA_LOGGING_FAILED \
    -1, "{ 'class': 'QgaLoggingFailed', 'data': {} }"

#define QERR_QMP_BAD_INPUT_OBJECT \
    -1, "{ 'class': 'QMPBadInputObject', 'data': { 'expected': %s } }"

#define QERR_QMP_BAD_INPUT_OBJECT_MEMBER \
    -1, "{ 'class': 'QMPBadInputObjectMember', 'data': { 'member': %s, 'expected': %s } }"

#define QERR_QMP_EXTRA_MEMBER \
    -1, "{ 'class': 'QMPExtraInputObjectMember', 'data': { 'member': %s } }"

#define QERR_RESET_REQUIRED \
    -1, "{ 'class': 'ResetRequired', 'data': {} }"

#define QERR_SET_PASSWD_FAILED \
    -1, "{ 'class': 'SetPasswdFailed', 'data': {} }"

#define QERR_TOO_MANY_FILES \
    -1, "{ 'class': 'TooManyFiles', 'data': {} }"

#define QERR_UNDEFINED_ERROR \
    -1, "{ 'class': 'UndefinedError', 'data': {} }"

#define QERR_UNKNOWN_BLOCK_FORMAT_FEATURE \
    -1, "{ 'class': 'UnknownBlockFormatFeature', 'data': { 'device': %s, 'format': %s, 'feature': %s } }"

#define QERR_UNSUPPORTED \
    -1, "{ 'class': 'Unsupported', 'data': {} }"

#define QERR_VIRTFS_FEATURE_BLOCKS_MIGRATION \
    -1, "{ 'class': 'VirtFSFeatureBlocksMigration', 'data': { 'path': %s, 'tag': %s } }"

#define QERR_VNC_SERVER_FAILED \
    -1, "{ 'class': 'VNCServerFailed', 'data': { 'target': %s } }"

#define QERR_SOCKET_CONNECT_FAILED \
    -1, "{ 'class': 'SockConnectFailed', 'data': {} }"

#define QERR_SOCKET_LISTEN_FAILED \
    -1, "{ 'class': 'SockListenFailed', 'data': {} }"

#define QERR_SOCKET_BIND_FAILED \
    -1, "{ 'class': 'SockBindFailed', 'data': {} }"

#define QERR_SOCKET_CREATE_FAILED \
    -1, "{ 'class': 'SockCreateFailed', 'data': {} }"

#endif /* QERROR_H */
