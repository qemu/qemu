/*
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef KEYVAL_H
#define KEYVAL_H

QDict *keyval_parse_into(QDict *qdict, const char *params, const char *implied_key,
                         bool *p_help, Error **errp);
QDict *keyval_parse(const char *params, const char *implied_key,
                    bool *help, Error **errp);
void keyval_merge(QDict *old, const QDict *new, Error **errp);

#endif /* KEYVAL_H */
