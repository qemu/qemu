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

#include "qobject.h"
#include "qdict.h"
#include "error.h"

typedef void (QmpCommandFunc)(QDict *, QObject **, Error **);

typedef enum QmpCommandType
{
    QCT_NORMAL,
} QmpCommandType;

typedef struct QmpCommand
{
    const char *name;
    QmpCommandType type;
    QmpCommandFunc *fn;
    QTAILQ_ENTRY(QmpCommand) node;
    bool enabled;
} QmpCommand;

void qmp_register_command(const char *name, QmpCommandFunc *fn);
QmpCommand *qmp_find_command(const char *name);
QObject *qmp_dispatch(QObject *request);
void qmp_disable_command(const char *name);
bool qmp_command_is_enabled(const char *name);
char **qmp_get_command_list(void);

#endif

