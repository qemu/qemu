/*
 * JSON Parser 
 *
 * Copyright IBM, Corp. 2009
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef QEMU_JSON_PARSER_H
#define QEMU_JSON_PARSER_H

#include "qemu-common.h"
#include "qapi/qmp/qlist.h"
#include "qapi/error.h"

QObject *json_parser_parse(QList *tokens, va_list *ap);
QObject *json_parser_parse_err(QList *tokens, va_list *ap, Error **errp);

#endif
