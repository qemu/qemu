/*
 * Test Server
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QTEST_H
#define QTEST_H

#include "chardev/char.h"

extern bool qtest_allowed;

static inline bool qtest_enabled(void)
{
    return qtest_allowed;
}

void G_GNUC_PRINTF(2, 3) qtest_sendf(CharFrontend *chr, const char *fmt, ...);
void qtest_set_command_cb(bool (*pc_cb)(CharFrontend *chr, gchar **words));
bool qtest_driver(void);

void qtest_server_init(const char *qtest_chrdev, const char *qtest_log, Error **errp);

void qtest_server_set_send_handler(void (*send)(void *, const char *),
                                 void *opaque);
void qtest_server_inproc_recv(void *opaque, const char *buf);

#endif
