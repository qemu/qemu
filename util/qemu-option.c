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

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qemu-common.h"
#include "qemu/error-report.h"
#include "qapi/qmp/types.h"
#include "qapi/qmp/qerror.h"
#include "qemu/option_int.h"
#include "qemu/cutils.h"
#include "qemu/id.h"
#include "qemu/help_option.h"

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

static void parse_option_bool(const char *name, const char *value, bool *ret,
                              Error **errp)
{
    if (!strcmp(value, "on")) {
        *ret = 1;
    } else if (!strcmp(value, "off")) {
        *ret = 0;
    } else {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   name, "'on' or 'off'");
    }
}

static void parse_option_number(const char *name, const char *value,
                                uint64_t *ret, Error **errp)
{
    uint64_t number;
    int err;

    err = qemu_strtou64(value, NULL, 0, &number);
    if (err == -ERANGE) {
        error_setg(errp, "Value '%s' is too large for parameter '%s'",
                   value, name);
        return;
    }
    if (err) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, name, "a number");
        return;
    }
    *ret = number;
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
    uint64_t size;
    int err;

    err = qemu_strtosz(value, NULL, &size);
    if (err == -ERANGE) {
        error_setg(errp, "Value '%s' is out of range for parameter '%s'",
                   value, name);
        return;
    }
    if (err) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, name,
                   "a non-negative number below 2^64");
        error_append_hint(errp, "Optional suffix k, M, G, T, P or E means"
                          " kilo-, mega-, giga-, tera-, peta-\n"
                          "and exabytes, respectively.\n");
        return;
    }
    *ret = size;
}

bool has_help_option(const char *param)
{
    size_t buflen = strlen(param) + 1;
    char *buf = g_malloc(buflen);
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
    g_free(buf);
    return result;
}

bool is_valid_option_list(const char *param)
{
    size_t buflen = strlen(param) + 1;
    char *buf = g_malloc(buflen);
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
    g_free(buf);
    return result;
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

QemuOpt *qemu_opt_find(QemuOpts *opts, const char *name)
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
    QemuOpt *opt;

    if (opts == NULL) {
        return NULL;
    }

    opt = qemu_opt_find(opts, name);
    if (!opt) {
        const QemuOptDesc *desc = find_desc_by_name(opts->list->desc, name);
        if (desc && desc->def_value_str) {
            return desc->def_value_str;
        }
    }
    return opt ? opt->str : NULL;
}

void qemu_opt_iter_init(QemuOptsIter *iter, QemuOpts *opts, const char *name)
{
    iter->opts = opts;
    iter->opt = QTAILQ_FIRST(&opts->head);
    iter->name = name;
}

const char *qemu_opt_iter_next(QemuOptsIter *iter)
{
    QemuOpt *ret = iter->opt;
    if (iter->name) {
        while (ret && !g_str_equal(iter->name, ret->name)) {
            ret = QTAILQ_NEXT(ret, next);
        }
    }
    iter->opt = ret ? QTAILQ_NEXT(ret, next) : NULL;
    return ret ? ret->str : NULL;
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
    QemuOpt *opt;
    bool ret = defval;

    if (opts == NULL) {
        return ret;
    }

    opt = qemu_opt_find(opts, name);
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
    QemuOpt *opt;
    uint64_t ret = defval;

    if (opts == NULL) {
        return ret;
    }

    opt = qemu_opt_find(opts, name);
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
    QemuOpt *opt;
    uint64_t ret = defval;

    if (opts == NULL) {
        return ret;
    }

    opt = qemu_opt_find(opts, name);
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
        error_setg(errp, QERR_INVALID_PARAMETER, name);
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
    assert(opt->str);
    qemu_opt_parse(opt, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        qemu_opt_del(opt);
    }
}

void qemu_opt_set(QemuOpts *opts, const char *name, const char *value,
                  Error **errp)
{
    opt_set(opts, name, value, false, errp);
}

void qemu_opt_set_bool(QemuOpts *opts, const char *name, bool val,
                       Error **errp)
{
    QemuOpt *opt;
    const QemuOptDesc *desc = opts->list->desc;

    opt = g_malloc0(sizeof(*opt));
    opt->desc = find_desc_by_name(desc, name);
    if (!opt->desc && !opts_accepts_any(opts)) {
        error_setg(errp, QERR_INVALID_PARAMETER, name);
        g_free(opt);
        return;
    }

    opt->name = g_strdup(name);
    opt->opts = opts;
    opt->value.boolean = !!val;
    opt->str = g_strdup(val ? "on" : "off");
    QTAILQ_INSERT_TAIL(&opts->head, opt, next);
}

void qemu_opt_set_number(QemuOpts *opts, const char *name, int64_t val,
                         Error **errp)
{
    QemuOpt *opt;
    const QemuOptDesc *desc = opts->list->desc;

    opt = g_malloc0(sizeof(*opt));
    opt->desc = find_desc_by_name(desc, name);
    if (!opt->desc && !opts_accepts_any(opts)) {
        error_setg(errp, QERR_INVALID_PARAMETER, name);
        g_free(opt);
        return;
    }

    opt->name = g_strdup(name);
    opt->opts = opts;
    opt->value.uint = val;
    opt->str = g_strdup_printf("%" PRId64, val);
    QTAILQ_INSERT_TAIL(&opts->head, opt, next);
}

/**
 * For each member of @opts, call @func(@opaque, name, value, @errp).
 * @func() may store an Error through @errp, but must return non-zero then.
 * When @func() returns non-zero, break the loop and return that value.
 * Return zero when the loop completes.
 */
int qemu_opt_foreach(QemuOpts *opts, qemu_opt_loopfunc func, void *opaque,
                     Error **errp)
{
    QemuOpt *opt;
    int rc;

    QTAILQ_FOREACH(opt, &opts->head, next) {
        rc = func(opaque, opt->name, opt->str, errp);
        if (rc) {
            return rc;
        }
        assert(!errp || !*errp);
    }
    return 0;
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

QemuOpts *qemu_opts_create(QemuOptsList *list, const char *id,
                           int fail_if_exists, Error **errp)
{
    QemuOpts *opts = NULL;

    if (id) {
        if (!id_wellformed(id)) {
            error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "id",
                       "an identifier");
            error_append_hint(errp, "Identifiers consist of letters, digits, "
                              "'-', '.', '_', starting with a letter.\n");
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

void qemu_opts_set(QemuOptsList *list, const char *id,
                   const char *name, const char *value, Error **errp)
{
    QemuOpts *opts;
    Error *local_err = NULL;

    opts = qemu_opts_create(list, id, 1, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    qemu_opt_set(opts, name, value, errp);
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

    if (opts == NULL) {
        return;
    }

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

/* print value, escaping any commas in value */
static void escaped_print(const char *value)
{
    const char *ptr;

    for (ptr = value; *ptr; ++ptr) {
        if (*ptr == ',') {
            putchar(',');
        }
        putchar(*ptr);
    }
}

void qemu_opts_print(QemuOpts *opts, const char *separator)
{
    QemuOpt *opt;
    QemuOptDesc *desc = opts->list->desc;
    const char *sep = "";

    if (opts->id) {
        printf("id=%s", opts->id); /* passed id_wellformed -> no commas */
        sep = separator;
    }

    if (desc[0].name == NULL) {
        QTAILQ_FOREACH(opt, &opts->head, next) {
            printf("%s%s=", sep, opt->name);
            escaped_print(opt->str);
            sep = separator;
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
            printf("%s%s=", sep, desc->name);
            escaped_print(value);
        } else if ((desc->type == QEMU_OPT_SIZE ||
                    desc->type == QEMU_OPT_NUMBER) && opt) {
            printf("%s%s=%" PRId64, sep, desc->name, opt->value.uint);
        } else {
            printf("%s%s=%s", sep, desc->name, value);
        }
        sep = separator;
    }
}

static void opts_do_parse(QemuOpts *opts, const char *params,
                          const char *firstname, bool prepend, Error **errp)
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
                error_propagate(errp, local_err);
                return;
            }
        }
        if (*p != ',') {
            break;
        }
    }
}

/**
 * Store options parsed from @params into @opts.
 * If @firstname is non-null, the first key=value in @params may omit
 * key=, and is treated as if key was @firstname.
 * On error, store an error object through @errp if non-null.
 */
void qemu_opts_do_parse(QemuOpts *opts, const char *params,
                       const char *firstname, Error **errp)
{
    opts_do_parse(opts, params, firstname, false, errp);
}

static QemuOpts *opts_parse(QemuOptsList *list, const char *params,
                            bool permit_abbrev, bool defaults, Error **errp)
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
        error_propagate(errp, local_err);
        return NULL;
    }

    opts_do_parse(opts, params, firstname, defaults, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        qemu_opts_del(opts);
        return NULL;
    }

    return opts;
}

/**
 * Create a QemuOpts in @list and with options parsed from @params.
 * If @permit_abbrev, the first key=value in @params may omit key=,
 * and is treated as if key was @list->implied_opt_name.
 * On error, store an error object through @errp if non-null.
 * Return the new QemuOpts on success, null pointer on error.
 */
QemuOpts *qemu_opts_parse(QemuOptsList *list, const char *params,
                          bool permit_abbrev, Error **errp)
{
    return opts_parse(list, params, permit_abbrev, false, errp);
}

/**
 * Create a QemuOpts in @list and with options parsed from @params.
 * If @permit_abbrev, the first key=value in @params may omit key=,
 * and is treated as if key was @list->implied_opt_name.
 * Report errors with error_report_err().  This is inappropriate in
 * QMP context.  Do not use this function there!
 * Return the new QemuOpts on success, null pointer on error.
 */
QemuOpts *qemu_opts_parse_noisily(QemuOptsList *list, const char *params,
                                  bool permit_abbrev)
{
    Error *err = NULL;
    QemuOpts *opts;

    opts = opts_parse(list, params, permit_abbrev, false, &err);
    if (err) {
        error_report_err(err);
    }
    return opts;
}

void qemu_opts_set_defaults(QemuOptsList *list, const char *params,
                            int permit_abbrev)
{
    QemuOpts *opts;

    opts = opts_parse(list, params, permit_abbrev, true, NULL);
    assert(opts);
}

typedef struct OptsFromQDictState {
    QemuOpts *opts;
    Error **errp;
} OptsFromQDictState;

static void qemu_opts_from_qdict_1(const char *key, QObject *obj, void *opaque)
{
    OptsFromQDictState *state = opaque;
    char buf[32], *tmp = NULL;
    const char *value;

    if (!strcmp(key, "id") || *state->errp) {
        return;
    }

    switch (qobject_type(obj)) {
    case QTYPE_QSTRING:
        value = qstring_get_str(qobject_to_qstring(obj));
        break;
    case QTYPE_QNUM:
        tmp = qnum_to_string(qobject_to_qnum(obj));
        value = tmp;
        break;
    case QTYPE_QBOOL:
        pstrcpy(buf, sizeof(buf),
                qbool_get_bool(qobject_to_qbool(obj)) ? "on" : "off");
        value = buf;
        break;
    default:
        return;
    }

    qemu_opt_set(state->opts, key, value, state->errp);
    g_free(tmp);
}

/*
 * Create QemuOpts from a QDict.
 * Use value of key "id" as ID if it exists and is a QString.  Only
 * QStrings, QNums and QBools are copied.  Entries with other types
 * are silently ignored.
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

    if (!qdict) {
        qdict = qdict_new();
    }
    if (opts->id) {
        qdict_put_str(qdict, "id", opts->id);
    }
    QTAILQ_FOREACH(opt, &opts->head, next) {
        qdict_put_str(qdict, opt->name, opt->str);
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
            error_setg(errp, QERR_INVALID_PARAMETER, opt->name);
            return;
        }

        qemu_opt_parse(opt, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }
}

/**
 * For each member of @list, call @func(@opaque, member, @errp).
 * Call it with the current location temporarily set to the member's.
 * @func() may store an Error through @errp, but must return non-zero then.
 * When @func() returns non-zero, break the loop and return that value.
 * Return zero when the loop completes.
 */
int qemu_opts_foreach(QemuOptsList *list, qemu_opts_loopfunc func,
                      void *opaque, Error **errp)
{
    Location loc;
    QemuOpts *opts;
    int rc = 0;

    loc_push_none(&loc);
    QTAILQ_FOREACH(opts, &list->head, next) {
        loc_restore(&opts->loc);
        rc = func(opaque, opts, errp);
        if (rc) {
            break;
        }
        assert(!errp || !*errp);
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

void qemu_opts_free(QemuOptsList *list)
{
    g_free(list);
}

/* Realloc dst option list and append options from an option list (list)
 * to it. dst could be NULL or a malloced list.
 * The lifetime of dst must be shorter than the input list because the
 * QemuOptDesc->name, ->help, and ->def_value_str strings are shared.
 */
QemuOptsList *qemu_opts_append(QemuOptsList *dst,
                               QemuOptsList *list)
{
    size_t num_opts, num_dst_opts;
    QemuOptDesc *desc;
    bool need_init = false;
    bool need_head_update;

    if (!list) {
        return dst;
    }

    /* If dst is NULL, after realloc, some area of dst should be initialized
     * before adding options to it.
     */
    if (!dst) {
        need_init = true;
        need_head_update = true;
    } else {
        /* Moreover, even if dst is not NULL, the realloc may move it to a
         * different address in which case we may get a stale tail pointer
         * in dst->head. */
        need_head_update = QTAILQ_EMPTY(&dst->head);
    }

    num_opts = count_opts_list(dst);
    num_dst_opts = num_opts;
    num_opts += count_opts_list(list);
    dst = g_realloc(dst, sizeof(QemuOptsList) +
                    (num_opts + 1) * sizeof(QemuOptDesc));
    if (need_init) {
        dst->name = NULL;
        dst->implied_opt_name = NULL;
        dst->merge_lists = false;
    }
    if (need_head_update) {
        QTAILQ_INIT(&dst->head);
    }
    dst->desc[num_dst_opts].name = NULL;

    /* append list->desc to dst->desc */
    if (list) {
        desc = list->desc;
        while (desc && desc->name) {
            if (find_desc_by_name(dst->desc, desc->name) == NULL) {
                dst->desc[num_dst_opts++] = *desc;
                dst->desc[num_dst_opts].name = NULL;
            }
            desc++;
        }
    }

    return dst;
}
