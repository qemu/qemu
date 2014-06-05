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

#include <stdio.h>
#include <string.h>

#include "qemu-common.h"
#include "qemu/error-report.h"
#include "qapi/qmp/types.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "qemu/option_int.h"

/*
 * Extracts the name of an option from the parameter string (p points at the
 * first byte of the option name)
 *
 * The option name is delimited by delim (usually , or =) or the string end
 * and is copied into buf. If the option name is longer than buf_size, it is
 * truncated. buf is always zero terminated.
 *
 * The return value is the position of the delimiter/zero byte after the option
 * name in p.
 */
const char *get_opt_name(char *buf, int buf_size, const char *p, char delim)
{
    char *q;

    q = buf;
    while (*p != '\0' && *p != delim) {
        if (q && (q - buf) < buf_size - 1)
            *q++ = *p;
        p++;
    }
    if (q)
        *q = '\0';

    return p;
}

/*
 * Extracts the value of an option from the parameter string p (p points at the
 * first byte of the option value)
 *
 * This function is comparable to get_opt_name with the difference that the
 * delimiter is fixed to be comma which starts a new option. To specify an
 * option value that contains commas, double each comma.
 */
const char *get_opt_value(char *buf, int buf_size, const char *p)
{
    char *q;

    q = buf;
    while (*p != '\0') {
        if (*p == ',') {
            if (*(p + 1) != ',')
                break;
            p++;
        }
        if (q && (q - buf) < buf_size - 1)
            *q++ = *p;
        p++;
    }
    if (q)
        *q = '\0';

    return p;
}

int get_next_param_value(char *buf, int buf_size,
                         const char *tag, const char **pstr)
{
    const char *p;
    char option[128];

    p = *pstr;
    for(;;) {
        p = get_opt_name(option, sizeof(option), p, '=');
        if (*p != '=')
            break;
        p++;
        if (!strcmp(tag, option)) {
            *pstr = get_opt_value(buf, buf_size, p);
            if (**pstr == ',') {
                (*pstr)++;
            }
            return strlen(buf);
        } else {
            p = get_opt_value(NULL, 0, p);
        }
        if (*p != ',')
            break;
        p++;
    }
    return 0;
}

int get_param_value(char *buf, int buf_size,
                    const char *tag, const char *str)
{
    return get_next_param_value(buf, buf_size, tag, &str);
}

/*
 * Searches an option list for an option with the given name
 */
QEMUOptionParameter *get_option_parameter(QEMUOptionParameter *list,
    const char *name)
{
    while (list && list->name) {
        if (!strcmp(list->name, name)) {
            return list;
        }
        list++;
    }

    return NULL;
}

static void parse_option_bool(const char *name, const char *value, bool *ret,
                              Error **errp)
{
    if (value != NULL) {
        if (!strcmp(value, "on")) {
            *ret = 1;
        } else if (!strcmp(value, "off")) {
            *ret = 0;
        } else {
            error_set(errp,QERR_INVALID_PARAMETER_VALUE, name, "'on' or 'off'");
        }
    } else {
        *ret = 1;
    }
}

static void parse_option_number(const char *name, const char *value,
                                uint64_t *ret, Error **errp)
{
    char *postfix;
    uint64_t number;

    if (value != NULL) {
        number = strtoull(value, &postfix, 0);
        if (*postfix != '\0') {
            error_set(errp, QERR_INVALID_PARAMETER_VALUE, name, "a number");
            return;
        }
        *ret = number;
    } else {
        error_set(errp, QERR_INVALID_PARAMETER_VALUE, name, "a number");
    }
}

static const QemuOptDesc *find_desc_by_name(const QemuOptDesc *desc,
                                            const char *name)
{
    int i;

    for (i = 0; desc[i].name != NULL; i++) {
        if (strcmp(desc[i].name, name) == 0) {
            return &desc[i];
        }
    }

    return NULL;
}

void parse_option_size(const char *name, const char *value,
                       uint64_t *ret, Error **errp)
{
    char *postfix;
    double sizef;

    if (value != NULL) {
        sizef = strtod(value, &postfix);
        switch (*postfix) {
        case 'T':
            sizef *= 1024;
            /* fall through */
        case 'G':
            sizef *= 1024;
            /* fall through */
        case 'M':
            sizef *= 1024;
            /* fall through */
        case 'K':
        case 'k':
            sizef *= 1024;
            /* fall through */
        case 'b':
        case '\0':
            *ret = (uint64_t) sizef;
            break;
        default:
            error_set(errp, QERR_INVALID_PARAMETER_VALUE, name, "a size");
#if 0 /* conversion from qerror_report() to error_set() broke this: */
            error_printf_unless_qmp("You may use k, M, G or T suffixes for "
                    "kilobytes, megabytes, gigabytes and terabytes.\n");
#endif
            return;
        }
    } else {
        error_set(errp, QERR_INVALID_PARAMETER_VALUE, name, "a size");
    }
}

/*
 * Sets the value of a parameter in a given option list. The parsing of the
 * value depends on the type of option:
 *
 * OPT_FLAG (uses value.n):
 *      If no value is given, the flag is set to 1.
 *      Otherwise the value must be "on" (set to 1) or "off" (set to 0)
 *
 * OPT_STRING (uses value.s):
 *      value is strdup()ed and assigned as option value
 *
 * OPT_SIZE (uses value.n):
 *      The value is converted to an integer. Suffixes for kilobytes etc. are
 *      allowed (powers of 1024).
 *
 * Returns 0 on succes, -1 in error cases
 */
int set_option_parameter(QEMUOptionParameter *list, const char *name,
    const char *value)
{
    bool flag;
    Error *local_err = NULL;

    // Find a matching parameter
    list = get_option_parameter(list, name);
    if (list == NULL) {
        fprintf(stderr, "Unknown option '%s'\n", name);
        return -1;
    }

    // Process parameter
    switch (list->type) {
    case OPT_FLAG:
        parse_option_bool(name, value, &flag, &local_err);
        if (!local_err) {
            list->value.n = flag;
        }
        break;

    case OPT_STRING:
        if (value != NULL) {
            list->value.s = g_strdup(value);
        } else {
            fprintf(stderr, "Option '%s' needs a parameter\n", name);
            return -1;
        }
        break;

    case OPT_SIZE:
        parse_option_size(name, value, &list->value.n, &local_err);
        break;

    default:
        fprintf(stderr, "Bug: Option '%s' has an unknown type\n", name);
        return -1;
    }

    if (local_err) {
        qerror_report_err(local_err);
        error_free(local_err);
        return -1;
    }

    list->assigned = true;

    return 0;
}

/*
 * Sets the given parameter to an integer instead of a string.
 * This function cannot be used to set string options.
 *
 * Returns 0 on success, -1 in error cases
 */
int set_option_parameter_int(QEMUOptionParameter *list, const char *name,
    uint64_t value)
{
    // Find a matching parameter
    list = get_option_parameter(list, name);
    if (list == NULL) {
        fprintf(stderr, "Unknown option '%s'\n", name);
        return -1;
    }

    // Process parameter
    switch (list->type) {
    case OPT_FLAG:
    case OPT_NUMBER:
    case OPT_SIZE:
        list->value.n = value;
        break;

    default:
        return -1;
    }

    list->assigned = true;

    return 0;
}

/*
 * Frees a option list. If it contains strings, the strings are freed as well.
 */
void free_option_parameters(QEMUOptionParameter *list)
{
    QEMUOptionParameter *cur = list;

    while (cur && cur->name) {
        if (cur->type == OPT_STRING) {
            g_free(cur->value.s);
        }
        cur++;
    }

    g_free(list);
}

/*
 * Count valid options in list
 */
static size_t count_option_parameters(QEMUOptionParameter *list)
{
    size_t num_options = 0;

    while (list && list->name) {
        num_options++;
        list++;
    }

    return num_options;
}

/*
 * Append an option list (list) to an option list (dest).
 *
 * If dest is NULL, a new copy of list is created.
 *
 * Returns a pointer to the first element of dest (or the newly allocated copy)
 */
QEMUOptionParameter *append_option_parameters(QEMUOptionParameter *dest,
    QEMUOptionParameter *list)
{
    size_t num_options, num_dest_options;

    num_options = count_option_parameters(dest);
    num_dest_options = num_options;

    num_options += count_option_parameters(list);

    dest = g_realloc(dest, (num_options + 1) * sizeof(QEMUOptionParameter));
    dest[num_dest_options].name = NULL;

    while (list && list->name) {
        if (get_option_parameter(dest, list->name) == NULL) {
            dest[num_dest_options++] = *list;
            dest[num_dest_options].name = NULL;
        }
        list++;
    }

    return dest;
}

/*
 * Parses a parameter string (param) into an option list (dest).
 *
 * list is the template option list. If dest is NULL, a new copy of list is
 * created. If list is NULL, this function fails.
 *
 * A parameter string consists of one or more parameters, separated by commas.
 * Each parameter consists of its name and possibly of a value. In the latter
 * case, the value is delimited by an = character. To specify a value which
 * contains commas, double each comma so it won't be recognized as the end of
 * the parameter.
 *
 * For more details of the parsing see above.
 *
 * Returns a pointer to the first element of dest (or the newly allocated copy)
 * or NULL in error cases
 */
QEMUOptionParameter *parse_option_parameters(const char *param,
    QEMUOptionParameter *list, QEMUOptionParameter *dest)
{
    QEMUOptionParameter *allocated = NULL;
    char name[256];
    char value[256];
    char *param_delim, *value_delim;
    char next_delim;
    int i;

    if (list == NULL) {
        return NULL;
    }

    if (dest == NULL) {
        dest = allocated = append_option_parameters(NULL, list);
    }

    for (i = 0; dest[i].name; i++) {
        dest[i].assigned = false;
    }

    while (*param) {

        // Find parameter name and value in the string
        param_delim = strchr(param, ',');
        value_delim = strchr(param, '=');

        if (value_delim && (value_delim < param_delim || !param_delim)) {
            next_delim = '=';
        } else {
            next_delim = ',';
            value_delim = NULL;
        }

        param = get_opt_name(name, sizeof(name), param, next_delim);
        if (value_delim) {
            param = get_opt_value(value, sizeof(value), param + 1);
        }
        if (*param != '\0') {
            param++;
        }

        // Set the parameter
        if (set_option_parameter(dest, name, value_delim ? value : NULL)) {
            goto fail;
        }
    }

    return dest;

fail:
    // Only free the list if it was newly allocated
    free_option_parameters(allocated);
    return NULL;
}

bool has_help_option(const char *param)
{
    size_t buflen = strlen(param) + 1;
    char *buf = g_malloc0(buflen);
    const char *p = param;
    bool result = false;

    while (*p) {
        p = get_opt_value(buf, buflen, p);
        if (*p) {
            p++;
        }

        if (is_help_option(buf)) {
            result = true;
            goto out;
        }
    }

out:
    free(buf);
    return result;
}

bool is_valid_option_list(const char *param)
{
    size_t buflen = strlen(param) + 1;
    char *buf = g_malloc0(buflen);
    const char *p = param;
    bool result = true;

    while (*p) {
        p = get_opt_value(buf, buflen, p);
        if (*p && !*++p) {
            result = false;
            goto out;
        }

        if (!*buf || *buf == ',') {
            result = false;
            goto out;
        }
    }

out:
    free(buf);
    return result;
}

/*
 * Prints all options of a list that have a value to stdout
 */
void print_option_parameters(QEMUOptionParameter *list)
{
    while (list && list->name) {
        switch (list->type) {
            case OPT_STRING:
                 if (list->value.s != NULL) {
                     printf("%s='%s' ", list->name, list->value.s);
                 }
                break;
            case OPT_FLAG:
                printf("%s=%s ", list->name, list->value.n ? "on" : "off");
                break;
            case OPT_SIZE:
            case OPT_NUMBER:
                printf("%s=%" PRId64 " ", list->name, list->value.n);
                break;
            default:
                printf("%s=(unknown type) ", list->name);
                break;
        }
        list++;
    }
}

/*
 * Prints an overview of all available options
 */
void print_option_help(QEMUOptionParameter *list)
{
    printf("Supported options:\n");
    while (list && list->name) {
        printf("%-16s %s\n", list->name,
            list->help ? list->help : "No description available");
        list++;
    }
}

void qemu_opts_print_help(QemuOptsList *list)
{
    QemuOptDesc *desc;

    assert(list);
    desc = list->desc;
    printf("Supported options:\n");
    while (desc && desc->name) {
        printf("%-16s %s\n", desc->name,
               desc->help ? desc->help : "No description available");
        desc++;
    }
}
/* ------------------------------------------------------------------ */

static QemuOpt *qemu_opt_find(QemuOpts *opts, const char *name)
{
    QemuOpt *opt;

    QTAILQ_FOREACH_REVERSE(opt, &opts->head, QemuOptHead, next) {
        if (strcmp(opt->name, name) != 0)
            continue;
        return opt;
    }
    return NULL;
}

static void qemu_opt_del(QemuOpt *opt)
{
    QTAILQ_REMOVE(&opt->opts->head, opt, next);
    g_free(opt->name);
    g_free(opt->str);
    g_free(opt);
}

/* qemu_opt_set allows many settings for the same option.
 * This function deletes all settings for an option.
 */
static void qemu_opt_del_all(QemuOpts *opts, const char *name)
{
    QemuOpt *opt, *next_opt;

    QTAILQ_FOREACH_SAFE(opt, &opts->head, next, next_opt) {
        if (!strcmp(opt->name, name)) {
            qemu_opt_del(opt);
        }
    }
}

const char *qemu_opt_get(QemuOpts *opts, const char *name)
{
    QemuOpt *opt = qemu_opt_find(opts, name);

    if (!opt) {
        const QemuOptDesc *desc = find_desc_by_name(opts->list->desc, name);
        if (desc && desc->def_value_str) {
            return desc->def_value_str;
        }
    }
    return opt ? opt->str : NULL;
}

/* Get a known option (or its default) and remove it from the list
 * all in one action. Return a malloced string of the option value.
 * Result must be freed by caller with g_free().
 */
char *qemu_opt_get_del(QemuOpts *opts, const char *name)
{
    QemuOpt *opt;
    const QemuOptDesc *desc;
    char *str = NULL;

    if (opts == NULL) {
        return NULL;
    }

    opt = qemu_opt_find(opts, name);
    if (!opt) {
        desc = find_desc_by_name(opts->list->desc, name);
        if (desc && desc->def_value_str) {
            str = g_strdup(desc->def_value_str);
        }
        return str;
    }
    str = opt->str;
    opt->str = NULL;
    qemu_opt_del_all(opts, name);
    return str;
}

bool qemu_opt_has_help_opt(QemuOpts *opts)
{
    QemuOpt *opt;

    QTAILQ_FOREACH_REVERSE(opt, &opts->head, QemuOptHead, next) {
        if (is_help_option(opt->name)) {
            return true;
        }
    }
    return false;
}

static bool qemu_opt_get_bool_helper(QemuOpts *opts, const char *name,
                                     bool defval, bool del)
{
    QemuOpt *opt = qemu_opt_find(opts, name);
    bool ret = defval;

    if (opt == NULL) {
        const QemuOptDesc *desc = find_desc_by_name(opts->list->desc, name);
        if (desc && desc->def_value_str) {
            parse_option_bool(name, desc->def_value_str, &ret, &error_abort);
        }
        return ret;
    }
    assert(opt->desc && opt->desc->type == QEMU_OPT_BOOL);
    ret = opt->value.boolean;
    if (del) {
        qemu_opt_del_all(opts, name);
    }
    return ret;
}

bool qemu_opt_get_bool(QemuOpts *opts, const char *name, bool defval)
{
    return qemu_opt_get_bool_helper(opts, name, defval, false);
}

bool qemu_opt_get_bool_del(QemuOpts *opts, const char *name, bool defval)
{
    return qemu_opt_get_bool_helper(opts, name, defval, true);
}

static uint64_t qemu_opt_get_number_helper(QemuOpts *opts, const char *name,
                                           uint64_t defval, bool del)
{
    QemuOpt *opt = qemu_opt_find(opts, name);
    uint64_t ret = defval;

    if (opt == NULL) {
        const QemuOptDesc *desc = find_desc_by_name(opts->list->desc, name);
        if (desc && desc->def_value_str) {
            parse_option_number(name, desc->def_value_str, &ret, &error_abort);
        }
        return ret;
    }
    assert(opt->desc && opt->desc->type == QEMU_OPT_NUMBER);
    ret = opt->value.uint;
    if (del) {
        qemu_opt_del_all(opts, name);
    }
    return ret;
}

uint64_t qemu_opt_get_number(QemuOpts *opts, const char *name, uint64_t defval)
{
    return qemu_opt_get_number_helper(opts, name, defval, false);
}

uint64_t qemu_opt_get_number_del(QemuOpts *opts, const char *name,
                                 uint64_t defval)
{
    return qemu_opt_get_number_helper(opts, name, defval, true);
}

static uint64_t qemu_opt_get_size_helper(QemuOpts *opts, const char *name,
                                         uint64_t defval, bool del)
{
    QemuOpt *opt = qemu_opt_find(opts, name);
    uint64_t ret = defval;

    if (opt == NULL) {
        const QemuOptDesc *desc = find_desc_by_name(opts->list->desc, name);
        if (desc && desc->def_value_str) {
            parse_option_size(name, desc->def_value_str, &ret, &error_abort);
        }
        return ret;
    }
    assert(opt->desc && opt->desc->type == QEMU_OPT_SIZE);
    ret = opt->value.uint;
    if (del) {
        qemu_opt_del_all(opts, name);
    }
    return ret;
}

uint64_t qemu_opt_get_size(QemuOpts *opts, const char *name, uint64_t defval)
{
    return qemu_opt_get_size_helper(opts, name, defval, false);
}

uint64_t qemu_opt_get_size_del(QemuOpts *opts, const char *name,
                               uint64_t defval)
{
    return qemu_opt_get_size_helper(opts, name, defval, true);
}

static void qemu_opt_parse(QemuOpt *opt, Error **errp)
{
    if (opt->desc == NULL)
        return;

    switch (opt->desc->type) {
    case QEMU_OPT_STRING:
        /* nothing */
        return;
    case QEMU_OPT_BOOL:
        parse_option_bool(opt->name, opt->str, &opt->value.boolean, errp);
        break;
    case QEMU_OPT_NUMBER:
        parse_option_number(opt->name, opt->str, &opt->value.uint, errp);
        break;
    case QEMU_OPT_SIZE:
        parse_option_size(opt->name, opt->str, &opt->value.uint, errp);
        break;
    default:
        abort();
    }
}

static bool opts_accepts_any(const QemuOpts *opts)
{
    return opts->list->desc[0].name == NULL;
}

int qemu_opt_unset(QemuOpts *opts, const char *name)
{
    QemuOpt *opt = qemu_opt_find(opts, name);

    assert(opts_accepts_any(opts));

    if (opt == NULL) {
        return -1;
    } else {
        qemu_opt_del(opt);
        return 0;
    }
}

static void opt_set(QemuOpts *opts, const char *name, const char *value,
                    bool prepend, Error **errp)
{
    QemuOpt *opt;
    const QemuOptDesc *desc;
    Error *local_err = NULL;

    desc = find_desc_by_name(opts->list->desc, name);
    if (!desc && !opts_accepts_any(opts)) {
        error_set(errp, QERR_INVALID_PARAMETER, name);
        return;
    }

    opt = g_malloc0(sizeof(*opt));
    opt->name = g_strdup(name);
    opt->opts = opts;
    if (prepend) {
        QTAILQ_INSERT_HEAD(&opts->head, opt, next);
    } else {
        QTAILQ_INSERT_TAIL(&opts->head, opt, next);
    }
    opt->desc = desc;
    opt->str = g_strdup(value);
    qemu_opt_parse(opt, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        qemu_opt_del(opt);
    }
}

int qemu_opt_set(QemuOpts *opts, const char *name, const char *value)
{
    Error *local_err = NULL;

    opt_set(opts, name, value, false, &local_err);
    if (local_err) {
        qerror_report_err(local_err);
        error_free(local_err);
        return -1;
    }

    return 0;
}

void qemu_opt_set_err(QemuOpts *opts, const char *name, const char *value,
                      Error **errp)
{
    opt_set(opts, name, value, false, errp);
}

int qemu_opt_set_bool(QemuOpts *opts, const char *name, bool val)
{
    QemuOpt *opt;
    const QemuOptDesc *desc = opts->list->desc;

    opt = g_malloc0(sizeof(*opt));
    opt->desc = find_desc_by_name(desc, name);
    if (!opt->desc && !opts_accepts_any(opts)) {
        qerror_report(QERR_INVALID_PARAMETER, name);
        g_free(opt);
        return -1;
    }

    opt->name = g_strdup(name);
    opt->opts = opts;
    opt->value.boolean = !!val;
    opt->str = g_strdup(val ? "on" : "off");
    QTAILQ_INSERT_TAIL(&opts->head, opt, next);

    return 0;
}

int qemu_opt_set_number(QemuOpts *opts, const char *name, int64_t val)
{
    QemuOpt *opt;
    const QemuOptDesc *desc = opts->list->desc;

    opt = g_malloc0(sizeof(*opt));
    opt->desc = find_desc_by_name(desc, name);
    if (!opt->desc && !opts_accepts_any(opts)) {
        qerror_report(QERR_INVALID_PARAMETER, name);
        g_free(opt);
        return -1;
    }

    opt->name = g_strdup(name);
    opt->opts = opts;
    opt->value.uint = val;
    opt->str = g_strdup_printf("%" PRId64, val);
    QTAILQ_INSERT_TAIL(&opts->head, opt, next);

    return 0;
}

int qemu_opt_foreach(QemuOpts *opts, qemu_opt_loopfunc func, void *opaque,
                     int abort_on_failure)
{
    QemuOpt *opt;
    int rc = 0;

    QTAILQ_FOREACH(opt, &opts->head, next) {
        rc = func(opt->name, opt->str, opaque);
        if (abort_on_failure  &&  rc != 0)
            break;
    }
    return rc;
}

QemuOpts *qemu_opts_find(QemuOptsList *list, const char *id)
{
    QemuOpts *opts;

    QTAILQ_FOREACH(opts, &list->head, next) {
        if (!opts->id && !id) {
            return opts;
        }
        if (opts->id && id && !strcmp(opts->id, id)) {
            return opts;
        }
    }
    return NULL;
}

static int id_wellformed(const char *id)
{
    int i;

    if (!qemu_isalpha(id[0])) {
        return 0;
    }
    for (i = 1; id[i]; i++) {
        if (!qemu_isalnum(id[i]) && !strchr("-._", id[i])) {
            return 0;
        }
    }
    return 1;
}

QemuOpts *qemu_opts_create(QemuOptsList *list, const char *id,
                           int fail_if_exists, Error **errp)
{
    QemuOpts *opts = NULL;

    if (id) {
        if (!id_wellformed(id)) {
            error_set(errp,QERR_INVALID_PARAMETER_VALUE, "id", "an identifier");
#if 0 /* conversion from qerror_report() to error_set() broke this: */
            error_printf_unless_qmp("Identifiers consist of letters, digits, '-', '.', '_', starting with a letter.\n");
#endif
            return NULL;
        }
        opts = qemu_opts_find(list, id);
        if (opts != NULL) {
            if (fail_if_exists && !list->merge_lists) {
                error_setg(errp, "Duplicate ID '%s' for %s", id, list->name);
                return NULL;
            } else {
                return opts;
            }
        }
    } else if (list->merge_lists) {
        opts = qemu_opts_find(list, NULL);
        if (opts) {
            return opts;
        }
    }
    opts = g_malloc0(sizeof(*opts));
    opts->id = g_strdup(id);
    opts->list = list;
    loc_save(&opts->loc);
    QTAILQ_INIT(&opts->head);
    QTAILQ_INSERT_TAIL(&list->head, opts, next);
    return opts;
}

void qemu_opts_reset(QemuOptsList *list)
{
    QemuOpts *opts, *next_opts;

    QTAILQ_FOREACH_SAFE(opts, &list->head, next, next_opts) {
        qemu_opts_del(opts);
    }
}

void qemu_opts_loc_restore(QemuOpts *opts)
{
    loc_restore(&opts->loc);
}

int qemu_opts_set(QemuOptsList *list, const char *id,
                  const char *name, const char *value)
{
    QemuOpts *opts;
    Error *local_err = NULL;

    opts = qemu_opts_create(list, id, 1, &local_err);
    if (local_err) {
        qerror_report_err(local_err);
        error_free(local_err);
        return -1;
    }
    return qemu_opt_set(opts, name, value);
}

const char *qemu_opts_id(QemuOpts *opts)
{
    return opts->id;
}

/* The id string will be g_free()d by qemu_opts_del */
void qemu_opts_set_id(QemuOpts *opts, char *id)
{
    opts->id = id;
}

void qemu_opts_del(QemuOpts *opts)
{
    QemuOpt *opt;

    for (;;) {
        opt = QTAILQ_FIRST(&opts->head);
        if (opt == NULL)
            break;
        qemu_opt_del(opt);
    }
    QTAILQ_REMOVE(&opts->list->head, opts, next);
    g_free(opts->id);
    g_free(opts);
}

void qemu_opts_print(QemuOpts *opts)
{
    QemuOpt *opt;
    QemuOptDesc *desc = opts->list->desc;

    if (desc[0].name == NULL) {
        QTAILQ_FOREACH(opt, &opts->head, next) {
            printf("%s=\"%s\" ", opt->name, opt->str);
        }
        return;
    }
    for (; desc && desc->name; desc++) {
        const char *value;
        QemuOpt *opt = qemu_opt_find(opts, desc->name);

        value = opt ? opt->str : desc->def_value_str;
        if (!value) {
            continue;
        }
        if (desc->type == QEMU_OPT_STRING) {
            printf("%s='%s' ", desc->name, value);
        } else if ((desc->type == QEMU_OPT_SIZE ||
                    desc->type == QEMU_OPT_NUMBER) && opt) {
            printf("%s=%" PRId64 " ", desc->name, opt->value.uint);
        } else {
            printf("%s=%s ", desc->name, value);
        }
    }
}

static int opts_do_parse(QemuOpts *opts, const char *params,
                         const char *firstname, bool prepend)
{
    char option[128], value[1024];
    const char *p,*pe,*pc;
    Error *local_err = NULL;

    for (p = params; *p != '\0'; p++) {
        pe = strchr(p, '=');
        pc = strchr(p, ',');
        if (!pe || (pc && pc < pe)) {
            /* found "foo,more" */
            if (p == params && firstname) {
                /* implicitly named first option */
                pstrcpy(option, sizeof(option), firstname);
                p = get_opt_value(value, sizeof(value), p);
            } else {
                /* option without value, probably a flag */
                p = get_opt_name(option, sizeof(option), p, ',');
                if (strncmp(option, "no", 2) == 0) {
                    memmove(option, option+2, strlen(option+2)+1);
                    pstrcpy(value, sizeof(value), "off");
                } else {
                    pstrcpy(value, sizeof(value), "on");
                }
            }
        } else {
            /* found "foo=bar,more" */
            p = get_opt_name(option, sizeof(option), p, '=');
            if (*p != '=') {
                break;
            }
            p++;
            p = get_opt_value(value, sizeof(value), p);
        }
        if (strcmp(option, "id") != 0) {
            /* store and parse */
            opt_set(opts, option, value, prepend, &local_err);
            if (local_err) {
                qerror_report_err(local_err);
                error_free(local_err);
                return -1;
            }
        }
        if (*p != ',') {
            break;
        }
    }
    return 0;
}

int qemu_opts_do_parse(QemuOpts *opts, const char *params, const char *firstname)
{
    return opts_do_parse(opts, params, firstname, false);
}

static QemuOpts *opts_parse(QemuOptsList *list, const char *params,
                            int permit_abbrev, bool defaults)
{
    const char *firstname;
    char value[1024], *id = NULL;
    const char *p;
    QemuOpts *opts;
    Error *local_err = NULL;

    assert(!permit_abbrev || list->implied_opt_name);
    firstname = permit_abbrev ? list->implied_opt_name : NULL;

    if (strncmp(params, "id=", 3) == 0) {
        get_opt_value(value, sizeof(value), params+3);
        id = value;
    } else if ((p = strstr(params, ",id=")) != NULL) {
        get_opt_value(value, sizeof(value), p+4);
        id = value;
    }

    /*
     * This code doesn't work for defaults && !list->merge_lists: when
     * params has no id=, and list has an element with !opts->id, it
     * appends a new element instead of returning the existing opts.
     * However, we got no use for this case.  Guard against possible
     * (if unlikely) future misuse:
     */
    assert(!defaults || list->merge_lists);
    opts = qemu_opts_create(list, id, !defaults, &local_err);
    if (opts == NULL) {
        if (local_err) {
            qerror_report_err(local_err);
            error_free(local_err);
        }
        return NULL;
    }

    if (opts_do_parse(opts, params, firstname, defaults) != 0) {
        qemu_opts_del(opts);
        return NULL;
    }

    return opts;
}

QemuOpts *qemu_opts_parse(QemuOptsList *list, const char *params,
                          int permit_abbrev)
{
    return opts_parse(list, params, permit_abbrev, false);
}

void qemu_opts_set_defaults(QemuOptsList *list, const char *params,
                            int permit_abbrev)
{
    QemuOpts *opts;

    opts = opts_parse(list, params, permit_abbrev, true);
    assert(opts);
}

typedef struct OptsFromQDictState {
    QemuOpts *opts;
    Error **errp;
} OptsFromQDictState;

static void qemu_opts_from_qdict_1(const char *key, QObject *obj, void *opaque)
{
    OptsFromQDictState *state = opaque;
    char buf[32];
    const char *value;
    int n;

    if (!strcmp(key, "id") || *state->errp) {
        return;
    }

    switch (qobject_type(obj)) {
    case QTYPE_QSTRING:
        value = qstring_get_str(qobject_to_qstring(obj));
        break;
    case QTYPE_QINT:
        n = snprintf(buf, sizeof(buf), "%" PRId64,
                     qint_get_int(qobject_to_qint(obj)));
        assert(n < sizeof(buf));
        value = buf;
        break;
    case QTYPE_QFLOAT:
        n = snprintf(buf, sizeof(buf), "%.17g",
                     qfloat_get_double(qobject_to_qfloat(obj)));
        assert(n < sizeof(buf));
        value = buf;
        break;
    case QTYPE_QBOOL:
        pstrcpy(buf, sizeof(buf),
                qbool_get_int(qobject_to_qbool(obj)) ? "on" : "off");
        value = buf;
        break;
    default:
        return;
    }

    qemu_opt_set_err(state->opts, key, value, state->errp);
}

/*
 * Create QemuOpts from a QDict.
 * Use value of key "id" as ID if it exists and is a QString.
 * Only QStrings, QInts, QFloats and QBools are copied.  Entries with
 * other types are silently ignored.
 */
QemuOpts *qemu_opts_from_qdict(QemuOptsList *list, const QDict *qdict,
                               Error **errp)
{
    OptsFromQDictState state;
    Error *local_err = NULL;
    QemuOpts *opts;

    opts = qemu_opts_create(list, qdict_get_try_str(qdict, "id"), 1,
                            &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return NULL;
    }

    assert(opts != NULL);

    state.errp = &local_err;
    state.opts = opts;
    qdict_iter(qdict, qemu_opts_from_qdict_1, &state);
    if (local_err) {
        error_propagate(errp, local_err);
        qemu_opts_del(opts);
        return NULL;
    }

    return opts;
}

/*
 * Adds all QDict entries to the QemuOpts that can be added and removes them
 * from the QDict. When this function returns, the QDict contains only those
 * entries that couldn't be added to the QemuOpts.
 */
void qemu_opts_absorb_qdict(QemuOpts *opts, QDict *qdict, Error **errp)
{
    const QDictEntry *entry, *next;

    entry = qdict_first(qdict);

    while (entry != NULL) {
        Error *local_err = NULL;
        OptsFromQDictState state = {
            .errp = &local_err,
            .opts = opts,
        };

        next = qdict_next(qdict, entry);

        if (find_desc_by_name(opts->list->desc, entry->key)) {
            qemu_opts_from_qdict_1(entry->key, entry->value, &state);
            if (local_err) {
                error_propagate(errp, local_err);
                return;
            } else {
                qdict_del(qdict, entry->key);
            }
        }

        entry = next;
    }
}

/*
 * Convert from QemuOpts to QDict.
 * The QDict values are of type QString.
 * TODO We'll want to use types appropriate for opt->desc->type, but
 * this is enough for now.
 */
QDict *qemu_opts_to_qdict(QemuOpts *opts, QDict *qdict)
{
    QemuOpt *opt;
    QObject *val;

    if (!qdict) {
        qdict = qdict_new();
    }
    if (opts->id) {
        qdict_put(qdict, "id", qstring_from_str(opts->id));
    }
    QTAILQ_FOREACH(opt, &opts->head, next) {
        val = QOBJECT(qstring_from_str(opt->str));
        qdict_put_obj(qdict, opt->name, val);
    }
    return qdict;
}

/* Validate parsed opts against descriptions where no
 * descriptions were provided in the QemuOptsList.
 */
void qemu_opts_validate(QemuOpts *opts, const QemuOptDesc *desc, Error **errp)
{
    QemuOpt *opt;
    Error *local_err = NULL;

    assert(opts_accepts_any(opts));

    QTAILQ_FOREACH(opt, &opts->head, next) {
        opt->desc = find_desc_by_name(desc, opt->name);
        if (!opt->desc) {
            error_set(errp, QERR_INVALID_PARAMETER, opt->name);
            return;
        }

        qemu_opt_parse(opt, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }
}

int qemu_opts_foreach(QemuOptsList *list, qemu_opts_loopfunc func, void *opaque,
                      int abort_on_failure)
{
    Location loc;
    QemuOpts *opts;
    int rc = 0;

    loc_push_none(&loc);
    QTAILQ_FOREACH(opts, &list->head, next) {
        loc_restore(&opts->loc);
        rc |= func(opts, opaque);
        if (abort_on_failure  &&  rc != 0)
            break;
    }
    loc_pop(&loc);
    return rc;
}

static size_t count_opts_list(QemuOptsList *list)
{
    QemuOptDesc *desc = NULL;
    size_t num_opts = 0;

    if (!list) {
        return 0;
    }

    desc = list->desc;
    while (desc && desc->name) {
        num_opts++;
        desc++;
    }

    return num_opts;
}

/* Convert QEMUOptionParameter to QemuOpts
 * FIXME: this function will be removed after all drivers
 * switch to QemuOpts
 */
QemuOptsList *params_to_opts(QEMUOptionParameter *list)
{
    QemuOptsList *opts = NULL;
    size_t num_opts, i = 0;

    if (!list) {
        return NULL;
    }

    num_opts = count_option_parameters(list);
    opts = g_malloc0(sizeof(QemuOptsList) +
                     (num_opts + 1) * sizeof(QemuOptDesc));
    QTAILQ_INIT(&opts->head);
    /* (const char *) members will point to malloced space and need to free */
    opts->allocated = true;

    while (list && list->name) {
        opts->desc[i].name = g_strdup(list->name);
        opts->desc[i].help = g_strdup(list->help);
        switch (list->type) {
        case OPT_FLAG:
            opts->desc[i].type = QEMU_OPT_BOOL;
            opts->desc[i].def_value_str =
                g_strdup(list->value.n ? "on" : "off");
            break;

        case OPT_NUMBER:
            opts->desc[i].type = QEMU_OPT_NUMBER;
            if (list->value.n) {
                opts->desc[i].def_value_str =
                    g_strdup_printf("%" PRIu64, list->value.n);
            }
            break;

        case OPT_SIZE:
            opts->desc[i].type = QEMU_OPT_SIZE;
            if (list->value.n) {
                opts->desc[i].def_value_str =
                    g_strdup_printf("%" PRIu64, list->value.n);
            }
            break;

        case OPT_STRING:
            opts->desc[i].type = QEMU_OPT_STRING;
            opts->desc[i].def_value_str = g_strdup(list->value.s);
            break;
        }

        i++;
        list++;
    }

    return opts;
}

/* convert QemuOpts to QEMUOptionParameter
 * Note: result QEMUOptionParameter has shorter lifetime than
 * input QemuOpts.
 * FIXME: this function will be removed after all drivers
 * switch to QemuOpts
 */
QEMUOptionParameter *opts_to_params(QemuOpts *opts)
{
    QEMUOptionParameter *dest = NULL;
    QemuOptDesc *desc;
    size_t num_opts, i = 0;
    const char *tmp;

    if (!opts || !opts->list || !opts->list->desc) {
        return NULL;
    }
    assert(!opts_accepts_any(opts));

    num_opts = count_opts_list(opts->list);
    dest = g_malloc0((num_opts + 1) * sizeof(QEMUOptionParameter));

    desc = opts->list->desc;
    while (desc && desc->name) {
        dest[i].name = desc->name;
        dest[i].help = desc->help;
        dest[i].assigned = qemu_opt_find(opts, desc->name) ? true : false;
        switch (desc->type) {
        case QEMU_OPT_STRING:
            dest[i].type = OPT_STRING;
            tmp = qemu_opt_get(opts, desc->name);
            dest[i].value.s = g_strdup(tmp);
            break;

        case QEMU_OPT_BOOL:
            dest[i].type = OPT_FLAG;
            dest[i].value.n = qemu_opt_get_bool(opts, desc->name, 0) ? 1 : 0;
            break;

        case QEMU_OPT_NUMBER:
            dest[i].type = OPT_NUMBER;
            dest[i].value.n = qemu_opt_get_number(opts, desc->name, 0);
            break;

        case QEMU_OPT_SIZE:
            dest[i].type = OPT_SIZE;
            dest[i].value.n = qemu_opt_get_size(opts, desc->name, 0);
            break;
        }

        i++;
        desc++;
    }

    return dest;
}

void qemu_opts_free(QemuOptsList *list)
{
    /* List members point to new malloced space and need to be freed.
     * FIXME:
     * Introduced for QEMUOptionParamter->QemuOpts conversion.
     * Will remove after all drivers switch to QemuOpts.
     */
    if (list && list->allocated) {
        QemuOptDesc *desc = list->desc;
        while (desc && desc->name) {
            g_free((char *)desc->name);
            g_free((char *)desc->help);
            g_free((char *)desc->def_value_str);
            desc++;
        }
    }

    g_free(list);
}
