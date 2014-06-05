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

#ifndef QEMU_OPTIONS_H
#define QEMU_OPTIONS_H

#include <stdint.h>
#include "qemu/queue.h"
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"

enum QEMUOptionParType {
    OPT_FLAG,
    OPT_NUMBER,
    OPT_SIZE,
    OPT_STRING,
};

typedef struct QEMUOptionParameter {
    const char *name;
    enum QEMUOptionParType type;
    union {
        uint64_t n;
        char* s;
    } value;
    const char *help;
    bool assigned;
} QEMUOptionParameter;


const char *get_opt_name(char *buf, int buf_size, const char *p, char delim);
const char *get_opt_value(char *buf, int buf_size, const char *p);
int get_next_param_value(char *buf, int buf_size,
                         const char *tag, const char **pstr);
int get_param_value(char *buf, int buf_size,
                    const char *tag, const char *str);


/*
 * The following functions take a parameter list as input. This is a pointer to
 * the first element of a QEMUOptionParameter array which is terminated by an
 * entry with entry->name == NULL.
 */

QEMUOptionParameter *get_option_parameter(QEMUOptionParameter *list,
    const char *name);
int set_option_parameter(QEMUOptionParameter *list, const char *name,
    const char *value);
int set_option_parameter_int(QEMUOptionParameter *list, const char *name,
    uint64_t value);
QEMUOptionParameter *append_option_parameters(QEMUOptionParameter *dest,
    QEMUOptionParameter *list);
QEMUOptionParameter *parse_option_parameters(const char *param,
    QEMUOptionParameter *list, QEMUOptionParameter *dest);
void parse_option_size(const char *name, const char *value,
                       uint64_t *ret, Error **errp);
void free_option_parameters(QEMUOptionParameter *list);
void print_option_parameters(QEMUOptionParameter *list);
void print_option_help(QEMUOptionParameter *list);
bool has_help_option(const char *param);
bool is_valid_option_list(const char *param);

/* ------------------------------------------------------------------ */

typedef struct QemuOpt QemuOpt;
typedef struct QemuOpts QemuOpts;
typedef struct QemuOptsList QemuOptsList;

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
bool qemu_opt_get_bool(QemuOpts *opts, const char *name, bool defval);
uint64_t qemu_opt_get_number(QemuOpts *opts, const char *name, uint64_t defval);
uint64_t qemu_opt_get_size(QemuOpts *opts, const char *name, uint64_t defval);
int qemu_opt_unset(QemuOpts *opts, const char *name);
int qemu_opt_set(QemuOpts *opts, const char *name, const char *value);
void qemu_opt_set_err(QemuOpts *opts, const char *name, const char *value,
                      Error **errp);
int qemu_opt_set_bool(QemuOpts *opts, const char *name, bool val);
int qemu_opt_set_number(QemuOpts *opts, const char *name, int64_t val);
typedef int (*qemu_opt_loopfunc)(const char *name, const char *value, void *opaque);
int qemu_opt_foreach(QemuOpts *opts, qemu_opt_loopfunc func, void *opaque,
                     int abort_on_failure);

QemuOpts *qemu_opts_find(QemuOptsList *list, const char *id);
QemuOpts *qemu_opts_create(QemuOptsList *list, const char *id,
                           int fail_if_exists, Error **errp);
void qemu_opts_reset(QemuOptsList *list);
void qemu_opts_loc_restore(QemuOpts *opts);
int qemu_opts_set(QemuOptsList *list, const char *id,
                  const char *name, const char *value);
const char *qemu_opts_id(QemuOpts *opts);
void qemu_opts_set_id(QemuOpts *opts, char *id);
void qemu_opts_del(QemuOpts *opts);
void qemu_opts_validate(QemuOpts *opts, const QemuOptDesc *desc, Error **errp);
int qemu_opts_do_parse(QemuOpts *opts, const char *params, const char *firstname);
QemuOpts *qemu_opts_parse(QemuOptsList *list, const char *params, int permit_abbrev);
void qemu_opts_set_defaults(QemuOptsList *list, const char *params,
                            int permit_abbrev);
QemuOpts *qemu_opts_from_qdict(QemuOptsList *list, const QDict *qdict,
                               Error **errp);
QDict *qemu_opts_to_qdict(QemuOpts *opts, QDict *qdict);
void qemu_opts_absorb_qdict(QemuOpts *opts, QDict *qdict, Error **errp);

typedef int (*qemu_opts_loopfunc)(QemuOpts *opts, void *opaque);
void qemu_opts_print(QemuOpts *opts);
int qemu_opts_foreach(QemuOptsList *list, qemu_opts_loopfunc func, void *opaque,
                      int abort_on_failure);

#endif
