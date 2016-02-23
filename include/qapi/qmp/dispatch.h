/*
 * Core Definitions for QAPI/QMP Dispatch
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef QMP_CORE_H
#define QMP_CORE_H

#include "qapi/qmp/qobject.h"
#include "qapi/qmp/qdict.h"

typedef void (QmpCommandFunc)(QDict *, QObject **, Error **);

typedef enum QmpCommandType
{
    QCT_NORMAL,
} QmpCommandType;

typedef enum QmpCommandOptions
{
    QCO_NO_OPTIONS = 0x0,
    QCO_NO_SUCCESS_RESP = 0x1,
} QmpCommandOptions;

typedef struct QmpCommand
{
    const char *name;
    QmpCommandType type;
    QmpCommandFunc *fn;
    QmpCommandOptions options;
    QTAILQ_ENTRY(QmpCommand) node;
    bool enabled;
} QmpCommand;

void qmp_register_command(const char *name, QmpCommandFunc *fn,
                          QmpCommandOptions options);
QmpCommand *qmp_find_command(const char *name);
QObject *qmp_dispatch(QObject *request);
void qmp_disable_command(const char *name);
void qmp_enable_command(const char *name);
bool qmp_command_is_enabled(const QmpCommand *cmd);
const char *qmp_command_name(const QmpCommand *cmd);
bool qmp_has_success_response(const QmpCommand *cmd);
QObject *qmp_build_error_object(Error *err);
typedef void (*qmp_cmd_callback_fn)(QmpCommand *cmd, void *opaque);
void qmp_for_each_command(qmp_cmd_callback_fn fn, void *opaque);

#endif

