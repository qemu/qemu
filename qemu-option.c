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
#include "qemu-option.h"

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

int check_params(char *buf, int buf_size,
                 const char * const *params, const char *str)
{
    const char *p;
    int i;

    p = str;
    while (*p != '\0') {
        p = get_opt_name(buf, buf_size, p, '=');
        if (*p != '=') {
            return -1;
        }
        p++;
        for (i = 0; params[i] != NULL; i++) {
            if (!strcmp(params[i], buf)) {
                break;
            }
        }
        if (params[i] == NULL) {
            return -1;
        }
        p = get_opt_value(NULL, 0, p);
        if (*p != ',') {
            break;
        }
        p++;
    }
    return 0;
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

static int parse_option_bool(const char *name, const char *value, int *ret)
{
    if (value != NULL) {
        if (!strcmp(value, "on")) {
            *ret = 1;
        } else if (!strcmp(value, "off")) {
            *ret = 0;
        } else {
            fprintf(stderr, "Option '%s': Use 'on' or 'off'\n", name);
            return -1;
        }
    } else {
        *ret = 1;
    }
    return 0;
}

static int parse_option_number(const char *name, const char *value, uint64_t *ret)
{
    char *postfix;
    uint64_t number;

    if (value != NULL) {
        number = strtoull(value, &postfix, 0);
        if (*postfix != '\0') {
            fprintf(stderr, "Option '%s' needs a number as parameter\n", name);
            return -1;
        }
        *ret = number;
    } else {
        fprintf(stderr, "Option '%s' needs a parameter\n", name);
        return -1;
    }
    return 0;
}

static int parse_option_size(const char *name, const char *value, uint64_t *ret)
{
    char *postfix;
    double sizef;

    if (value != NULL) {
        sizef = strtod(value, &postfix);
        switch (*postfix) {
        case 'T':
            sizef *= 1024;
        case 'G':
            sizef *= 1024;
        case 'M':
            sizef *= 1024;
        case 'K':
        case 'k':
            sizef *= 1024;
        case 'b':
        case '\0':
            *ret = (uint64_t) sizef;
            break;
        default:
            fprintf(stderr, "Option '%s' needs size as parameter\n", name);
            fprintf(stderr, "You may use k, M, G or T suffixes for "
                    "kilobytes, megabytes, gigabytes and terabytes.\n");
            return -1;
        }
    } else {
        fprintf(stderr, "Option '%s' needs a parameter\n", name);
        return -1;
    }
    return 0;
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
    int flag;

    // Find a matching parameter
    list = get_option_parameter(list, name);
    if (list == NULL) {
        fprintf(stderr, "Unknown option '%s'\n", name);
        return -1;
    }

    // Process parameter
    switch (list->type) {
    case OPT_FLAG:
        if (-1 == parse_option_bool(name, value, &flag))
            return -1;
        list->value.n = flag;
        break;

    case OPT_STRING:
        if (value != NULL) {
            list->value.s = strdup(value);
        } else {
            fprintf(stderr, "Option '%s' needs a parameter\n", name);
            return -1;
        }
        break;

    case OPT_SIZE:
        if (-1 == parse_option_size(name, value, &list->value.n))
            return -1;
        break;

    default:
        fprintf(stderr, "Bug: Option '%s' has an unknown type\n", name);
        return -1;
    }

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
            free(cur->value.s);
        }
        cur++;
    }

    free(list);
}

/*
 * Parses a parameter string (param) into an option list (dest).
 *
 * list is the templace is. If dest is NULL, a new copy of list is created for
 * it. If list is NULL, this function fails.
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
    QEMUOptionParameter *cur;
    QEMUOptionParameter *allocated = NULL;
    char name[256];
    char value[256];
    char *param_delim, *value_delim;
    char next_delim;
    size_t num_options;

    if (list == NULL) {
        return NULL;
    }

    if (dest == NULL) {
        // Count valid options
        num_options = 0;
        cur = list;
        while (cur->name) {
            num_options++;
            cur++;
        }

        // Create a copy of the option list to fill in values
        dest = qemu_mallocz((num_options + 1) * sizeof(QEMUOptionParameter));
        allocated = dest;
        memcpy(dest, list, (num_options + 1) * sizeof(QEMUOptionParameter));
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
                printf("%s=(unkown type) ", list->name);
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

/* ------------------------------------------------------------------ */

struct QemuOpt {
    const char   *name;
    const char   *str;

    QemuOptDesc  *desc;
    union {
        int      bool;
        uint64_t uint;
    } value;

    QemuOpts     *opts;
    TAILQ_ENTRY(QemuOpt) next;
};

struct QemuOpts {
    const char *id;
    QemuOptsList *list;
    TAILQ_HEAD(, QemuOpt) head;
    TAILQ_ENTRY(QemuOpts) next;
};

static QemuOpt *qemu_opt_find(QemuOpts *opts, const char *name)
{
    QemuOpt *opt;

    TAILQ_FOREACH(opt, &opts->head, next) {
        if (strcmp(opt->name, name) != 0)
            continue;
        return opt;
    }
    return NULL;
}

const char *qemu_opt_get(QemuOpts *opts, const char *name)
{
    QemuOpt *opt = qemu_opt_find(opts, name);
    return opt ? opt->str : NULL;
}

int qemu_opt_get_bool(QemuOpts *opts, const char *name, int defval)
{
    QemuOpt *opt = qemu_opt_find(opts, name);

    if (opt == NULL)
        return defval;
    assert(opt->desc && opt->desc->type == QEMU_OPT_BOOL);
    return opt->value.bool;
}

uint64_t qemu_opt_get_number(QemuOpts *opts, const char *name, uint64_t defval)
{
    QemuOpt *opt = qemu_opt_find(opts, name);

    if (opt == NULL)
        return defval;
    assert(opt->desc && opt->desc->type == QEMU_OPT_NUMBER);
    return opt->value.uint;
}

uint64_t qemu_opt_get_size(QemuOpts *opts, const char *name, uint64_t defval)
{
    QemuOpt *opt = qemu_opt_find(opts, name);

    if (opt == NULL)
        return defval;
    assert(opt->desc && opt->desc->type == QEMU_OPT_SIZE);
    return opt->value.uint;
}

static int qemu_opt_parse(QemuOpt *opt)
{
    if (opt->desc == NULL)
        return 0;
    switch (opt->desc->type) {
    case QEMU_OPT_STRING:
        /* nothing */
        return 0;
    case QEMU_OPT_BOOL:
        return parse_option_bool(opt->name, opt->str, &opt->value.bool);
    case QEMU_OPT_NUMBER:
        return parse_option_number(opt->name, opt->str, &opt->value.uint);
    case QEMU_OPT_SIZE:
        return parse_option_size(opt->name, opt->str, &opt->value.uint);
    default:
        abort();
    }
}

static void qemu_opt_del(QemuOpt *opt)
{
    TAILQ_REMOVE(&opt->opts->head, opt, next);
    qemu_free((/* !const */ char*)opt->name);
    qemu_free((/* !const */ char*)opt->str);
    qemu_free(opt);
}

int qemu_opt_set(QemuOpts *opts, const char *name, const char *value)
{
    QemuOpt *opt;

    opt = qemu_opt_find(opts, name);
    if (!opt) {
        QemuOptDesc *desc = opts->list->desc;
        int i;

        for (i = 0; desc[i].name != NULL; i++) {
            if (strcmp(desc[i].name, name) == 0) {
                break;
            }
        }
        if (desc[i].name == NULL) {
            if (i == 0) {
                /* empty list -> allow any */;
            } else {
                fprintf(stderr, "option \"%s\" is not valid for %s\n",
                        name, opts->list->name);
                return -1;
            }
        }
        opt = qemu_mallocz(sizeof(*opt));
        opt->name = qemu_strdup(name);
        opt->opts = opts;
        TAILQ_INSERT_TAIL(&opts->head, opt, next);
        if (desc[i].name != NULL) {
            opt->desc = desc+i;
        }
    }
    qemu_free((/* !const */ char*)opt->str);
    opt->str = NULL;
    if (value) {
        opt->str = qemu_strdup(value);
    }
    if (qemu_opt_parse(opt) < 0) {
        fprintf(stderr, "Failed to parse \"%s\" for \"%s.%s\"\n", opt->str,
                opts->list->name, opt->name);
        qemu_opt_del(opt);
        return -1;
    }
    return 0;
}

int qemu_opt_foreach(QemuOpts *opts, qemu_opt_loopfunc func, void *opaque,
                     int abort_on_failure)
{
    QemuOpt *opt;
    int rc = 0;

    TAILQ_FOREACH(opt, &opts->head, next) {
        rc = func(opt->name, opt->str, opaque);
        if (abort_on_failure  &&  rc != 0)
            break;
    }
    return rc;
}

QemuOpts *qemu_opts_find(QemuOptsList *list, const char *id)
{
    QemuOpts *opts;

    TAILQ_FOREACH(opts, &list->head, next) {
        if (!opts->id) {
            continue;
        }
        if (strcmp(opts->id, id) != 0) {
            continue;
        }
        return opts;
    }
    return NULL;
}

QemuOpts *qemu_opts_create(QemuOptsList *list, const char *id, int fail_if_exists)
{
    QemuOpts *opts = NULL;

    if (id) {
        opts = qemu_opts_find(list, id);
        if (opts != NULL) {
            if (fail_if_exists) {
                fprintf(stderr, "tried to create id \"%s\" twice for \"%s\"\n",
                        id, list->name);
                return NULL;
            } else {
                return opts;
            }
        }
    }
    opts = qemu_mallocz(sizeof(*opts));
    if (id) {
        opts->id = qemu_strdup(id);
    }
    opts->list = list;
    TAILQ_INIT(&opts->head);
    TAILQ_INSERT_TAIL(&list->head, opts, next);
    return opts;
}

int qemu_opts_set(QemuOptsList *list, const char *id,
                  const char *name, const char *value)
{
    QemuOpts *opts;

    opts = qemu_opts_create(list, id, 1);
    if (opts == NULL) {
        fprintf(stderr, "id \"%s\" not found for \"%s\"\n",
                id, list->name);
        return -1;
    }
    return qemu_opt_set(opts, name, value);
}

const char *qemu_opts_id(QemuOpts *opts)
{
    return opts->id;
}

void qemu_opts_del(QemuOpts *opts)
{
    QemuOpt *opt;

    for (;;) {
        opt = TAILQ_FIRST(&opts->head);
        if (opt == NULL)
            break;
        qemu_opt_del(opt);
    }
    TAILQ_REMOVE(&opts->list->head, opts, next);
    qemu_free(opts);
}

int qemu_opts_print(QemuOpts *opts, void *dummy)
{
    QemuOpt *opt;

    fprintf(stderr, "%s: %s:", opts->list->name,
            opts->id ? opts->id : "<noid>");
    TAILQ_FOREACH(opt, &opts->head, next) {
        fprintf(stderr, " %s=\"%s\"", opt->name, opt->str);
    }
    fprintf(stderr, "\n");
    return 0;
}

QemuOpts *qemu_opts_parse(QemuOptsList *list, const char *params, const char *firstname)
{
    char option[128], value[128], *id = NULL;
    QemuOpts *opts;
    const char *p,*pe,*pc;

    if (get_param_value(value, sizeof(value), "id", params))
        id = qemu_strdup(value);
    opts = qemu_opts_create(list, id, 1);
    if (opts == NULL)
        return NULL;

    p = params;
    for(;;) {
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
                if (strncmp(p, "no", 2) == 0) {
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
            if (-1 == qemu_opt_set(opts, option, value)) {
                qemu_opts_del(opts);
                return NULL;
            }
        }
        if (*p != ',') {
            break;
        }
        p++;
    }
    return opts;
}

int qemu_opts_foreach(QemuOptsList *list, qemu_opts_loopfunc func, void *opaque,
                      int abort_on_failure)
{
    QemuOpts *opts;
    int rc = 0;

    TAILQ_FOREACH(opts, &list->head, next) {
        rc = func(opts, opaque);
        if (abort_on_failure  &&  rc != 0)
            break;
    }
    return rc;
}
