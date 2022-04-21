/*
 * Commandline option parsing functions
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2009 Kevin Wolf <kwolf@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef QEMU_OPTION_H
#define QEMU_OPTION_H

#include "qemu/queue.h"

/**
 * get_opt_value
 * @p: a pointer to the option name, delimited by commas
 * @value: a non-NULL pointer that will received the delimited options
 *
 * The @value char pointer will be allocated and filled with
 * the delimited options.
 *
 * Returns the position of the comma delimiter/zero byte after the
 * option name in @p.
 * The memory pointer in @value must be released with a call to g_free()
 * when no longer required.
 */
const char *get_opt_value(const char *p, char **value);

bool parse_option_size(const char *name, const char *value,
                       uint64_t *ret, Error **errp);
bool has_help_option(const char *param);

enum QemuOptType {
    QEMU_OPT_STRING = 0,  /* no parsing (use string as-is)                        */
    QEMU_OPT_BOOL,        /* on/off                                               */
    QEMU_OPT_NUMBER,      /* simple number                                        */
    QEMU_OPT_SIZE,        /* size, accepts (K)ilo, (M)ega, (G)iga, (T)era postfix */
};

typedef struct QemuOptDesc {
    const char *name;
    enum QemuOptType type;
    const char *help;
    const char *def_value_str;
} QemuOptDesc;

struct QemuOptsList {
    const char *name;
    const char *implied_opt_name;
    bool merge_lists;  /* Merge multiple uses of option into a single list? */
    QTAILQ_HEAD(, QemuOpts) head;
    QemuOptDesc desc[];
};

const char *qemu_opt_get(QemuOpts *opts, const char *name);
char *qemu_opt_get_del(QemuOpts *opts, const char *name);
/**
 * qemu_opt_has_help_opt:
 * @opts: options to search for a help request
 *
 * Check whether the options specified by @opts include one of the
 * standard strings which indicate that the user is asking for a
 * list of the valid values for a command line option (as defined
 * by is_help_option()).
 *
 * Returns: true if @opts includes 'help' or equivalent.
 */
bool qemu_opt_has_help_opt(QemuOpts *opts);
QemuOpt *qemu_opt_find(QemuOpts *opts, const char *name);
bool qemu_opt_get_bool(QemuOpts *opts, const char *name, bool defval);
uint64_t qemu_opt_get_number(QemuOpts *opts, const char *name, uint64_t defval);
uint64_t qemu_opt_get_size(QemuOpts *opts, const char *name, uint64_t defval);
bool qemu_opt_get_bool_del(QemuOpts *opts, const char *name, bool defval);
uint64_t qemu_opt_get_number_del(QemuOpts *opts, const char *name,
                                 uint64_t defval);
uint64_t qemu_opt_get_size_del(QemuOpts *opts, const char *name,
                               uint64_t defval);
int qemu_opt_unset(QemuOpts *opts, const char *name);
bool qemu_opt_set(QemuOpts *opts, const char *name, const char *value,
                  Error **errp);
bool qemu_opt_set_bool(QemuOpts *opts, const char *name, bool val,
                       Error **errp);
bool qemu_opt_set_number(QemuOpts *opts, const char *name, int64_t val,
                         Error **errp);
typedef int (*qemu_opt_loopfunc)(void *opaque,
                                 const char *name, const char *value,
                                 Error **errp);
int qemu_opt_foreach(QemuOpts *opts, qemu_opt_loopfunc func, void *opaque,
                     Error **errp);

typedef struct {
    QemuOpts *opts;
    QemuOpt *opt;
    const char *name;
} QemuOptsIter;

void qemu_opt_iter_init(QemuOptsIter *iter, QemuOpts *opts, const char *name);
const char *qemu_opt_iter_next(QemuOptsIter *iter);

QemuOpts *qemu_opts_find(QemuOptsList *list, const char *id);
QemuOpts *qemu_opts_create(QemuOptsList *list, const char *id,
                           int fail_if_exists, Error **errp);
void qemu_opts_reset(QemuOptsList *list);
void qemu_opts_loc_restore(QemuOpts *opts);
const char *qemu_opts_id(QemuOpts *opts);
void qemu_opts_set_id(QemuOpts *opts, char *id);
void qemu_opts_del(QemuOpts *opts);
bool qemu_opts_validate(QemuOpts *opts, const QemuOptDesc *desc, Error **errp);
bool qemu_opts_do_parse(QemuOpts *opts, const char *params,
                        const char *firstname, Error **errp);
QemuOpts *qemu_opts_parse_noisily(QemuOptsList *list, const char *params,
                                  bool permit_abbrev);
QemuOpts *qemu_opts_parse(QemuOptsList *list, const char *params,
                          bool permit_abbrev, Error **errp);
QemuOpts *qemu_opts_from_qdict(QemuOptsList *list, const QDict *qdict,
                               Error **errp);
QDict *qemu_opts_to_qdict_filtered(QemuOpts *opts, QDict *qdict,
                                   QemuOptsList *list, bool del);
QDict *qemu_opts_to_qdict(QemuOpts *opts, QDict *qdict);
bool qemu_opts_absorb_qdict(QemuOpts *opts, QDict *qdict, Error **errp);

typedef int (*qemu_opts_loopfunc)(void *opaque, QemuOpts *opts, Error **errp);
int qemu_opts_foreach(QemuOptsList *list, qemu_opts_loopfunc func,
                      void *opaque, Error **errp);
void qemu_opts_print(QemuOpts *opts, const char *sep);
void qemu_opts_print_help(QemuOptsList *list, bool print_caption);
void qemu_opts_free(QemuOptsList *list);
QemuOptsList *qemu_opts_append(QemuOptsList *dst, QemuOptsList *list);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(QemuOpts, qemu_opts_del)

#endif
