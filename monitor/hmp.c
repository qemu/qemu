/*
 * QEMU monitor
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
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
#include <dirent.h>
#include "hw/qdev-core.h"
#include "monitor-internal.h"
#include "monitor/hmp.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qnum.h"
#include "qemu/config-file.h"
#include "qemu/ctype.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "qemu/option.h"
#include "qemu/units.h"
#include "sysemu/block-backend.h"
#include "trace.h"

static void monitor_command_cb(void *opaque, const char *cmdline,
                               void *readline_opaque)
{
    MonitorHMP *mon = opaque;

    monitor_suspend(&mon->common);
    handle_hmp_command(mon, cmdline);
    monitor_resume(&mon->common);
}

void monitor_read_command(MonitorHMP *mon, int show_prompt)
{
    if (!mon->rs) {
        return;
    }

    readline_start(mon->rs, "(qemu) ", 0, monitor_command_cb, NULL);
    if (show_prompt) {
        readline_show_prompt(mon->rs);
    }
}

int monitor_read_password(MonitorHMP *mon, ReadLineFunc *readline_func,
                          void *opaque)
{
    if (mon->rs) {
        readline_start(mon->rs, "Password: ", 1, readline_func, opaque);
        /* prompt is printed on return from the command handler */
        return 0;
    } else {
        monitor_printf(&mon->common,
                       "terminal does not support password prompting\n");
        return -ENOTTY;
    }
}

static int get_str(char *buf, int buf_size, const char **pp)
{
    const char *p;
    char *q;
    int c;

    q = buf;
    p = *pp;
    while (qemu_isspace(*p)) {
        p++;
    }
    if (*p == '\0') {
    fail:
        *q = '\0';
        *pp = p;
        return -1;
    }
    if (*p == '\"') {
        p++;
        while (*p != '\0' && *p != '\"') {
            if (*p == '\\') {
                p++;
                c = *p++;
                switch (c) {
                case 'n':
                    c = '\n';
                    break;
                case 'r':
                    c = '\r';
                    break;
                case '\\':
                case '\'':
                case '\"':
                    break;
                default:
                    printf("unsupported escape code: '\\%c'\n", c);
                    goto fail;
                }
                if ((q - buf) < buf_size - 1) {
                    *q++ = c;
                }
            } else {
                if ((q - buf) < buf_size - 1) {
                    *q++ = *p;
                }
                p++;
            }
        }
        if (*p != '\"') {
            printf("unterminated string\n");
            goto fail;
        }
        p++;
    } else {
        while (*p != '\0' && !qemu_isspace(*p)) {
            if ((q - buf) < buf_size - 1) {
                *q++ = *p;
            }
            p++;
        }
    }
    *q = '\0';
    *pp = p;
    return 0;
}

#define MAX_ARGS 16

static void free_cmdline_args(char **args, int nb_args)
{
    int i;

    assert(nb_args <= MAX_ARGS);

    for (i = 0; i < nb_args; i++) {
        g_free(args[i]);
    }

}

/*
 * Parse the command line to get valid args.
 * @cmdline: command line to be parsed.
 * @pnb_args: location to store the number of args, must NOT be NULL.
 * @args: location to store the args, which should be freed by caller, must
 *        NOT be NULL.
 *
 * Returns 0 on success, negative on failure.
 *
 * NOTE: this parser is an approximate form of the real command parser. Number
 *       of args have a limit of MAX_ARGS. If cmdline contains more, it will
 *       return with failure.
 */
static int parse_cmdline(const char *cmdline,
                         int *pnb_args, char **args)
{
    const char *p;
    int nb_args, ret;
    char buf[1024];

    p = cmdline;
    nb_args = 0;
    for (;;) {
        while (qemu_isspace(*p)) {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        if (nb_args >= MAX_ARGS) {
            goto fail;
        }
        ret = get_str(buf, sizeof(buf), &p);
        if (ret < 0) {
            goto fail;
        }
        args[nb_args] = g_strdup(buf);
        nb_args++;
    }
    *pnb_args = nb_args;
    return 0;

 fail:
    free_cmdline_args(args, nb_args);
    return -1;
}

/*
 * Can command @cmd be executed in preconfig state?
 */
static bool cmd_can_preconfig(const HMPCommand *cmd)
{
    if (!cmd->flags) {
        return false;
    }

    return strchr(cmd->flags, 'p');
}

static bool cmd_available(const HMPCommand *cmd)
{
    return phase_check(PHASE_MACHINE_READY) || cmd_can_preconfig(cmd);
}

static void help_cmd_dump_one(Monitor *mon,
                              const HMPCommand *cmd,
                              char **prefix_args,
                              int prefix_args_nb)
{
    int i;

    if (!cmd_available(cmd)) {
        return;
    }

    for (i = 0; i < prefix_args_nb; i++) {
        monitor_printf(mon, "%s ", prefix_args[i]);
    }
    monitor_printf(mon, "%s %s -- %s\n", cmd->name, cmd->params, cmd->help);
}

/* @args[@arg_index] is the valid command need to find in @cmds */
static void help_cmd_dump(Monitor *mon, const HMPCommand *cmds,
                          char **args, int nb_args, int arg_index)
{
    const HMPCommand *cmd;
    size_t i;

    /* No valid arg need to compare with, dump all in *cmds */
    if (arg_index >= nb_args) {
        for (cmd = cmds; cmd->name != NULL; cmd++) {
            help_cmd_dump_one(mon, cmd, args, arg_index);
        }
        return;
    }

    /* Find one entry to dump */
    for (cmd = cmds; cmd->name != NULL; cmd++) {
        if (hmp_compare_cmd(args[arg_index], cmd->name) &&
            cmd_available(cmd)) {
            if (cmd->sub_table) {
                /* continue with next arg */
                help_cmd_dump(mon, cmd->sub_table,
                              args, nb_args, arg_index + 1);
            } else {
                help_cmd_dump_one(mon, cmd, args, arg_index);
            }
            return;
        }
    }

    /* Command not found */
    monitor_printf(mon, "unknown command: '");
    for (i = 0; i <= arg_index; i++) {
        monitor_printf(mon, "%s%s", args[i], i == arg_index ? "'\n" : " ");
    }
}

void hmp_help_cmd(Monitor *mon, const char *name)
{
    char *args[MAX_ARGS];
    int nb_args = 0;

    /* 1. parse user input */
    if (name) {
        /* special case for log, directly dump and return */
        if (!strcmp(name, "log")) {
            const QEMULogItem *item;
            monitor_printf(mon, "Log items (comma separated):\n");
            monitor_printf(mon, "%-15s %s\n", "none", "remove all logs");
            for (item = qemu_log_items; item->mask != 0; item++) {
                monitor_printf(mon, "%-15s %s\n", item->name, item->help);
            }
#ifdef CONFIG_TRACE_LOG
            monitor_printf(mon, "trace:PATTERN   enable trace events\n");
            monitor_printf(mon, "\nUse \"log trace:help\" to get a list of "
                           "trace events.\n\n");
#endif
            return;
        }

        if (parse_cmdline(name, &nb_args, args) < 0) {
            return;
        }
    }

    /* 2. dump the contents according to parsed args */
    help_cmd_dump(mon, hmp_cmds, args, nb_args, 0);

    free_cmdline_args(args, nb_args);
}

/*******************************************************************/

static const char *pch;
static sigjmp_buf expr_env;

static G_NORETURN G_GNUC_PRINTF(2, 3)
void expr_error(Monitor *mon, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    monitor_vprintf(mon, fmt, ap);
    monitor_printf(mon, "\n");
    va_end(ap);
    siglongjmp(expr_env, 1);
}

static void next(void)
{
    if (*pch != '\0') {
        pch++;
        while (qemu_isspace(*pch)) {
            pch++;
        }
    }
}

static int64_t expr_sum(Monitor *mon);

static int64_t expr_unary(Monitor *mon)
{
    int64_t n;
    char *p;
    int ret;

    switch (*pch) {
    case '+':
        next();
        n = expr_unary(mon);
        break;
    case '-':
        next();
        n = -expr_unary(mon);
        break;
    case '~':
        next();
        n = ~expr_unary(mon);
        break;
    case '(':
        next();
        n = expr_sum(mon);
        if (*pch != ')') {
            expr_error(mon, "')' expected");
        }
        next();
        break;
    case '\'':
        pch++;
        if (*pch == '\0') {
            expr_error(mon, "character constant expected");
        }
        n = *pch;
        pch++;
        if (*pch != '\'') {
            expr_error(mon, "missing terminating \' character");
        }
        next();
        break;
    case '$':
        {
            char buf[128], *q;
            int64_t reg = 0;

            pch++;
            q = buf;
            while ((*pch >= 'a' && *pch <= 'z') ||
                   (*pch >= 'A' && *pch <= 'Z') ||
                   (*pch >= '0' && *pch <= '9') ||
                   *pch == '_' || *pch == '.') {
                if ((q - buf) < sizeof(buf) - 1) {
                    *q++ = *pch;
                }
                pch++;
            }
            while (qemu_isspace(*pch)) {
                pch++;
            }
            *q = 0;
            ret = get_monitor_def(mon, &reg, buf);
            if (ret < 0) {
                expr_error(mon, "unknown register");
            }
            n = reg;
        }
        break;
    case '\0':
        expr_error(mon, "unexpected end of expression");
        n = 0;
        break;
    default:
        errno = 0;
        n = strtoull(pch, &p, 0);
        if (errno == ERANGE) {
            expr_error(mon, "number too large");
        }
        if (pch == p) {
            expr_error(mon, "invalid char '%c' in expression", *p);
        }
        pch = p;
        while (qemu_isspace(*pch)) {
            pch++;
        }
        break;
    }
    return n;
}

static int64_t expr_prod(Monitor *mon)
{
    int64_t val, val2;
    int op;

    val = expr_unary(mon);
    for (;;) {
        op = *pch;
        if (op != '*' && op != '/' && op != '%') {
            break;
        }
        next();
        val2 = expr_unary(mon);
        switch (op) {
        default:
        case '*':
            val *= val2;
            break;
        case '/':
        case '%':
            if (val2 == 0) {
                expr_error(mon, "division by zero");
            }
            if (op == '/') {
                val /= val2;
            } else {
                val %= val2;
            }
            break;
        }
    }
    return val;
}

static int64_t expr_logic(Monitor *mon)
{
    int64_t val, val2;
    int op;

    val = expr_prod(mon);
    for (;;) {
        op = *pch;
        if (op != '&' && op != '|' && op != '^') {
            break;
        }
        next();
        val2 = expr_prod(mon);
        switch (op) {
        default:
        case '&':
            val &= val2;
            break;
        case '|':
            val |= val2;
            break;
        case '^':
            val ^= val2;
            break;
        }
    }
    return val;
}

static int64_t expr_sum(Monitor *mon)
{
    int64_t val, val2;
    int op;

    val = expr_logic(mon);
    for (;;) {
        op = *pch;
        if (op != '+' && op != '-') {
            break;
        }
        next();
        val2 = expr_logic(mon);
        if (op == '+') {
            val += val2;
        } else {
            val -= val2;
        }
    }
    return val;
}

static int get_expr(Monitor *mon, int64_t *pval, const char **pp)
{
    pch = *pp;
    if (sigsetjmp(expr_env, 0)) {
        *pp = pch;
        return -1;
    }
    while (qemu_isspace(*pch)) {
        pch++;
    }
    *pval = expr_sum(mon);
    *pp = pch;
    return 0;
}

static int get_double(Monitor *mon, double *pval, const char **pp)
{
    const char *p = *pp;
    char *tailp;
    double d;

    d = strtod(p, &tailp);
    if (tailp == p) {
        monitor_printf(mon, "Number expected\n");
        return -1;
    }
    if (d != d || d - d != 0) {
        /* NaN or infinity */
        monitor_printf(mon, "Bad number\n");
        return -1;
    }
    *pval = d;
    *pp = tailp;
    return 0;
}

/*
 * Store the command-name in cmdname, and return a pointer to
 * the remaining of the command string.
 */
static const char *get_command_name(const char *cmdline,
                                    char *cmdname, size_t nlen)
{
    size_t len;
    const char *p, *pstart;

    p = cmdline;
    while (qemu_isspace(*p)) {
        p++;
    }
    if (*p == '\0') {
        return NULL;
    }
    pstart = p;
    while (*p != '\0' && *p != '/' && !qemu_isspace(*p)) {
        p++;
    }
    len = p - pstart;
    if (len > nlen - 1) {
        len = nlen - 1;
    }
    memcpy(cmdname, pstart, len);
    cmdname[len] = '\0';
    return p;
}

/**
 * Read key of 'type' into 'key' and return the current
 * 'type' pointer.
 */
static char *key_get_info(const char *type, char **key)
{
    size_t len;
    char *p, *str;

    if (*type == ',') {
        type++;
    }

    p = strchr(type, ':');
    if (!p) {
        *key = NULL;
        return NULL;
    }
    len = p - type;

    str = g_malloc(len + 1);
    memcpy(str, type, len);
    str[len] = '\0';

    *key = str;
    return ++p;
}

static int default_fmt_format = 'x';
static int default_fmt_size = 4;

static int is_valid_option(const char *c, const char *typestr)
{
    char option[3];

    option[0] = '-';
    option[1] = *c;
    option[2] = '\0';

    typestr = strstr(typestr, option);
    return (typestr != NULL);
}

static const HMPCommand *search_dispatch_table(const HMPCommand *disp_table,
                                               const char *cmdname)
{
    const HMPCommand *cmd;

    for (cmd = disp_table; cmd->name != NULL; cmd++) {
        if (hmp_compare_cmd(cmdname, cmd->name)) {
            return cmd;
        }
    }

    return NULL;
}

/*
 * Parse command name from @cmdp according to command table @table.
 * If blank, return NULL.
 * Else, if no valid command can be found, report to @mon, and return
 * NULL.
 * Else, change @cmdp to point right behind the name, and return its
 * command table entry.
 * Do not assume the return value points into @table!  It doesn't when
 * the command is found in a sub-command table.
 */
static const HMPCommand *monitor_parse_command(MonitorHMP *hmp_mon,
                                               const char *cmdp_start,
                                               const char **cmdp,
                                               HMPCommand *table)
{
    Monitor *mon = &hmp_mon->common;
    const char *p;
    const HMPCommand *cmd;
    char cmdname[256];

    /* extract the command name */
    p = get_command_name(*cmdp, cmdname, sizeof(cmdname));
    if (!p) {
        return NULL;
    }

    cmd = search_dispatch_table(table, cmdname);
    if (!cmd) {
        monitor_printf(mon, "unknown command: '%.*s'\n",
                       (int)(p - cmdp_start), cmdp_start);
        return NULL;
    }
    if (!cmd_available(cmd)) {
        monitor_printf(mon, "Command '%.*s' not available "
                            "until machine initialization has completed.\n",
                       (int)(p - cmdp_start), cmdp_start);
        return NULL;
    }

    /* filter out following useless space */
    while (qemu_isspace(*p)) {
        p++;
    }

    *cmdp = p;
    /* search sub command */
    if (cmd->sub_table != NULL && *p != '\0') {
        return monitor_parse_command(hmp_mon, cmdp_start, cmdp, cmd->sub_table);
    }

    return cmd;
}

/*
 * Parse arguments for @cmd.
 * If it can't be parsed, report to @mon, and return NULL.
 * Else, insert command arguments into a QDict, and return it.
 * Note: On success, caller has to free the QDict structure.
 */
static QDict *monitor_parse_arguments(Monitor *mon,
                                      const char **endp,
                                      const HMPCommand *cmd)
{
    const char *typestr;
    char *key;
    int c;
    const char *p = *endp;
    char buf[1024];
    QDict *qdict = qdict_new();

    /* parse the parameters */
    typestr = cmd->args_type;
    for (;;) {
        typestr = key_get_info(typestr, &key);
        if (!typestr) {
            break;
        }
        c = *typestr;
        typestr++;
        switch (c) {
        case 'F':
        case 'B':
        case 's':
            {
                int ret;

                while (qemu_isspace(*p)) {
                    p++;
                }
                if (*typestr == '?') {
                    typestr++;
                    if (*p == '\0') {
                        /* no optional string: NULL argument */
                        break;
                    }
                }
                ret = get_str(buf, sizeof(buf), &p);
                if (ret < 0) {
                    switch (c) {
                    case 'F':
                        monitor_printf(mon, "%s: filename expected\n",
                                       cmd->name);
                        break;
                    case 'B':
                        monitor_printf(mon, "%s: block device name expected\n",
                                       cmd->name);
                        break;
                    default:
                        monitor_printf(mon, "%s: string expected\n", cmd->name);
                        break;
                    }
                    goto fail;
                }
                qdict_put_str(qdict, key, buf);
            }
            break;
        case 'O':
            {
                QemuOptsList *opts_list;
                QemuOpts *opts;

                opts_list = qemu_find_opts(key);
                if (!opts_list || opts_list->desc->name) {
                    goto bad_type;
                }
                while (qemu_isspace(*p)) {
                    p++;
                }
                if (!*p) {
                    break;
                }
                if (get_str(buf, sizeof(buf), &p) < 0) {
                    goto fail;
                }
                opts = qemu_opts_parse_noisily(opts_list, buf, true);
                if (!opts) {
                    goto fail;
                }
                qemu_opts_to_qdict(opts, qdict);
                qemu_opts_del(opts);
            }
            break;
        case '/':
            {
                int count, format, size;

                while (qemu_isspace(*p)) {
                    p++;
                }
                if (*p == '/') {
                    /* format found */
                    p++;
                    count = 1;
                    if (qemu_isdigit(*p)) {
                        count = 0;
                        while (qemu_isdigit(*p)) {
                            count = count * 10 + (*p - '0');
                            p++;
                        }
                    }
                    size = -1;
                    format = -1;
                    for (;;) {
                        switch (*p) {
                        case 'o':
                        case 'd':
                        case 'u':
                        case 'x':
                        case 'i':
                        case 'c':
                            format = *p++;
                            break;
                        case 'b':
                            size = 1;
                            p++;
                            break;
                        case 'h':
                            size = 2;
                            p++;
                            break;
                        case 'w':
                            size = 4;
                            p++;
                            break;
                        case 'g':
                        case 'L':
                            size = 8;
                            p++;
                            break;
                        default:
                            goto next;
                        }
                    }
                next:
                    if (*p != '\0' && !qemu_isspace(*p)) {
                        monitor_printf(mon, "invalid char in format: '%c'\n",
                                       *p);
                        goto fail;
                    }
                    if (format < 0) {
                        format = default_fmt_format;
                    }
                    if (format != 'i') {
                        /* for 'i', not specifying a size gives -1 as size */
                        if (size < 0) {
                            size = default_fmt_size;
                        }
                        default_fmt_size = size;
                    }
                    default_fmt_format = format;
                } else {
                    count = 1;
                    format = default_fmt_format;
                    if (format != 'i') {
                        size = default_fmt_size;
                    } else {
                        size = -1;
                    }
                }
                qdict_put_int(qdict, "count", count);
                qdict_put_int(qdict, "format", format);
                qdict_put_int(qdict, "size", size);
            }
            break;
        case 'i':
        case 'l':
        case 'M':
            {
                int64_t val;

                while (qemu_isspace(*p)) {
                    p++;
                }
                if (*typestr == '?' || *typestr == '.') {
                    if (*typestr == '?') {
                        if (*p == '\0') {
                            typestr++;
                            break;
                        }
                    } else {
                        if (*p == '.') {
                            p++;
                            while (qemu_isspace(*p)) {
                                p++;
                            }
                        } else {
                            typestr++;
                            break;
                        }
                    }
                    typestr++;
                }
                if (get_expr(mon, &val, &p)) {
                    goto fail;
                }
                /* Check if 'i' is greater than 32-bit */
                if ((c == 'i') && ((val >> 32) & 0xffffffff)) {
                    monitor_printf(mon, "\'%s\' has failed: ", cmd->name);
                    monitor_printf(mon, "integer is for 32-bit values\n");
                    goto fail;
                } else if (c == 'M') {
                    if (val < 0) {
                        monitor_printf(mon, "enter a positive value\n");
                        goto fail;
                    }
                    val *= MiB;
                }
                qdict_put_int(qdict, key, val);
            }
            break;
        case 'o':
            {
                int ret;
                uint64_t val;
                const char *end;

                while (qemu_isspace(*p)) {
                    p++;
                }
                if (*typestr == '?') {
                    typestr++;
                    if (*p == '\0') {
                        break;
                    }
                }
                ret = qemu_strtosz_MiB(p, &end, &val);
                if (ret < 0 || val > INT64_MAX) {
                    monitor_printf(mon, "invalid size\n");
                    goto fail;
                }
                qdict_put_int(qdict, key, val);
                p = end;
            }
            break;
        case 'T':
            {
                double val;

                while (qemu_isspace(*p)) {
                    p++;
                }
                if (*typestr == '?') {
                    typestr++;
                    if (*p == '\0') {
                        break;
                    }
                }
                if (get_double(mon, &val, &p) < 0) {
                    goto fail;
                }
                if (p[0] && p[1] == 's') {
                    switch (*p) {
                    case 'm':
                        val /= 1e3; p += 2; break;
                    case 'u':
                        val /= 1e6; p += 2; break;
                    case 'n':
                        val /= 1e9; p += 2; break;
                    }
                }
                if (*p && !qemu_isspace(*p)) {
                    monitor_printf(mon, "Unknown unit suffix\n");
                    goto fail;
                }
                qdict_put(qdict, key, qnum_from_double(val));
            }
            break;
        case 'b':
            {
                const char *beg;
                bool val;

                while (qemu_isspace(*p)) {
                    p++;
                }
                beg = p;
                while (qemu_isgraph(*p)) {
                    p++;
                }
                if (p - beg == 2 && !memcmp(beg, "on", p - beg)) {
                    val = true;
                } else if (p - beg == 3 && !memcmp(beg, "off", p - beg)) {
                    val = false;
                } else {
                    monitor_printf(mon, "Expected 'on' or 'off'\n");
                    goto fail;
                }
                qdict_put_bool(qdict, key, val);
            }
            break;
        case '-':
            {
                const char *tmp = p;
                int skip_key = 0;
                int ret;
                /* option */

                c = *typestr++;
                if (c == '\0') {
                    goto bad_type;
                }
                while (qemu_isspace(*p)) {
                    p++;
                }
                if (*p == '-') {
                    p++;
                    if (c != *p) {
                        if (!is_valid_option(p, typestr)) {
                            monitor_printf(mon, "%s: unsupported option -%c\n",
                                           cmd->name, *p);
                            goto fail;
                        } else {
                            skip_key = 1;
                        }
                    }
                    if (skip_key) {
                        p = tmp;
                    } else if (*typestr == 's') {
                        /* has option with string value */
                        typestr++;
                        tmp = p++;
                        while (qemu_isspace(*p)) {
                            p++;
                        }
                        ret = get_str(buf, sizeof(buf), &p);
                        if (ret < 0) {
                            monitor_printf(mon, "%s: value expected for -%c\n",
                                           cmd->name, *tmp);
                            goto fail;
                        }
                        qdict_put_str(qdict, key, buf);
                    } else {
                        /* has boolean option */
                        p++;
                        qdict_put_bool(qdict, key, true);
                    }
                } else if (*typestr == 's') {
                    typestr++;
                }
            }
            break;
        case 'S':
            {
                /* package all remaining string */
                int len;

                while (qemu_isspace(*p)) {
                    p++;
                }
                if (*typestr == '?') {
                    typestr++;
                    if (*p == '\0') {
                        /* no remaining string: NULL argument */
                        break;
                    }
                }
                len = strlen(p);
                if (len <= 0) {
                    monitor_printf(mon, "%s: string expected\n",
                                   cmd->name);
                    goto fail;
                }
                qdict_put_str(qdict, key, p);
                p += len;
            }
            break;
        default:
        bad_type:
            monitor_printf(mon, "%s: unknown type '%c'\n", cmd->name, c);
            goto fail;
        }
        g_free(key);
        key = NULL;
    }
    /* check that all arguments were parsed */
    while (qemu_isspace(*p)) {
        p++;
    }
    if (*p != '\0') {
        monitor_printf(mon, "%s: extraneous characters at the end of line\n",
                       cmd->name);
        goto fail;
    }

    return qdict;

fail:
    qobject_unref(qdict);
    g_free(key);
    return NULL;
}

static void hmp_info_human_readable_text(Monitor *mon,
                                         HumanReadableText *(*handler)(Error **))
{
    Error *err = NULL;
    g_autoptr(HumanReadableText) info = handler(&err);

    if (hmp_handle_error(mon, err)) {
        return;
    }

    monitor_puts(mon, info->human_readable_text);
}

static void handle_hmp_command_exec(Monitor *mon,
                                    const HMPCommand *cmd,
                                    QDict *qdict)
{
    if (cmd->cmd_info_hrt) {
        hmp_info_human_readable_text(mon,
                                     cmd->cmd_info_hrt);
    } else {
        cmd->cmd(mon, qdict);
    }
}

typedef struct HandleHmpCommandCo {
    Monitor *mon;
    const HMPCommand *cmd;
    QDict *qdict;
    bool done;
} HandleHmpCommandCo;

static void handle_hmp_command_co(void *opaque)
{
    HandleHmpCommandCo *data = opaque;
    handle_hmp_command_exec(data->mon, data->cmd, data->qdict);
    monitor_set_cur(qemu_coroutine_self(), NULL);
    data->done = true;
}

void handle_hmp_command(MonitorHMP *mon, const char *cmdline)
{
    QDict *qdict;
    const HMPCommand *cmd;
    const char *cmd_start = cmdline;

    trace_handle_hmp_command(mon, cmdline);

    cmd = monitor_parse_command(mon, cmdline, &cmdline, hmp_cmds);
    if (!cmd) {
        return;
    }

    if (!cmd->cmd && !cmd->cmd_info_hrt) {
        /* FIXME: is it useful to try autoload modules here ??? */
        monitor_printf(&mon->common, "Command \"%.*s\" is not available.\n",
                       (int)(cmdline - cmd_start), cmd_start);
        return;
    }

    qdict = monitor_parse_arguments(&mon->common, &cmdline, cmd);
    if (!qdict) {
        while (cmdline > cmd_start && qemu_isspace(cmdline[-1])) {
            cmdline--;
        }
        monitor_printf(&mon->common, "Try \"help %.*s\" for more information\n",
                       (int)(cmdline - cmd_start), cmd_start);
        return;
    }

    if (!cmd->coroutine) {
        /* old_mon is non-NULL when called from qmp_human_monitor_command() */
        Monitor *old_mon = monitor_set_cur(qemu_coroutine_self(), &mon->common);
        handle_hmp_command_exec(&mon->common, cmd, qdict);
        monitor_set_cur(qemu_coroutine_self(), old_mon);
    } else {
        HandleHmpCommandCo data = {
            .mon = &mon->common,
            .cmd = cmd,
            .qdict = qdict,
            .done = false,
        };
        Coroutine *co = qemu_coroutine_create(handle_hmp_command_co, &data);
        monitor_set_cur(co, &mon->common);
        aio_co_enter(qemu_get_aio_context(), co);
        AIO_WAIT_WHILE_UNLOCKED(NULL, !data.done);
    }

    qobject_unref(qdict);
}

static void cmd_completion(MonitorHMP *mon, const char *name, const char *list)
{
    const char *p, *pstart;
    char cmd[128];
    int len;

    p = list;
    for (;;) {
        pstart = p;
        p = qemu_strchrnul(p, '|');
        len = p - pstart;
        if (len > sizeof(cmd) - 2) {
            len = sizeof(cmd) - 2;
        }
        memcpy(cmd, pstart, len);
        cmd[len] = '\0';
        readline_add_completion_of(mon->rs, name, cmd);
        if (*p == '\0') {
            break;
        }
        p++;
    }
}

static void file_completion(MonitorHMP *mon, const char *input)
{
    DIR *ffs;
    struct dirent *d;
    char path[1024];
    char file[1024], file_prefix[1024];
    int input_path_len;
    const char *p;

    p = strrchr(input, '/');
    if (!p) {
        input_path_len = 0;
        pstrcpy(file_prefix, sizeof(file_prefix), input);
        pstrcpy(path, sizeof(path), ".");
    } else {
        input_path_len = p - input + 1;
        memcpy(path, input, input_path_len);
        if (input_path_len > sizeof(path) - 1) {
            input_path_len = sizeof(path) - 1;
        }
        path[input_path_len] = '\0';
        pstrcpy(file_prefix, sizeof(file_prefix), p + 1);
    }

    ffs = opendir(path);
    if (!ffs) {
        return;
    }
    for (;;) {
        struct stat sb;
        d = readdir(ffs);
        if (!d) {
            break;
        }

        if (strcmp(d->d_name, ".") == 0 || strcmp(d->d_name, "..") == 0) {
            continue;
        }

        if (strstart(d->d_name, file_prefix, NULL)) {
            memcpy(file, input, input_path_len);
            if (input_path_len < sizeof(file)) {
                pstrcpy(file + input_path_len, sizeof(file) - input_path_len,
                        d->d_name);
            }
            /*
             * stat the file to find out if it's a directory.
             * In that case add a slash to speed up typing long paths
             */
            if (stat(file, &sb) == 0 && S_ISDIR(sb.st_mode)) {
                pstrcat(file, sizeof(file), "/");
            }
            readline_add_completion(mon->rs, file);
        }
    }
    closedir(ffs);
}

static const char *next_arg_type(const char *typestr)
{
    const char *p = strchr(typestr, ':');
    return (p != NULL ? ++p : typestr);
}

static void monitor_find_completion_by_table(MonitorHMP *mon,
                                             const HMPCommand *cmd_table,
                                             char **args,
                                             int nb_args)
{
    const char *cmdname;
    int i;
    const char *ptype, *old_ptype, *str;
    const HMPCommand *cmd;
    BlockBackend *blk = NULL;

    if (nb_args <= 1) {
        /* command completion */
        if (nb_args == 0) {
            cmdname = "";
        } else {
            cmdname = args[0];
        }
        readline_set_completion_index(mon->rs, strlen(cmdname));
        for (cmd = cmd_table; cmd->name != NULL; cmd++) {
            if (cmd_available(cmd)) {
                cmd_completion(mon, cmdname, cmd->name);
            }
        }
    } else {
        /* find the command */
        for (cmd = cmd_table; cmd->name != NULL; cmd++) {
            if (hmp_compare_cmd(args[0], cmd->name) &&
                cmd_available(cmd)) {
                break;
            }
        }
        if (!cmd->name) {
            return;
        }

        if (cmd->sub_table) {
            /* do the job again */
            monitor_find_completion_by_table(mon, cmd->sub_table,
                                             &args[1], nb_args - 1);
            return;
        }
        if (cmd->command_completion) {
            cmd->command_completion(mon->rs, nb_args, args[nb_args - 1]);
            return;
        }

        ptype = next_arg_type(cmd->args_type);
        for (i = 0; i < nb_args - 2; i++) {
            if (*ptype != '\0') {
                ptype = next_arg_type(ptype);
                while (*ptype == '?') {
                    ptype = next_arg_type(ptype);
                }
            }
        }
        str = args[nb_args - 1];
        old_ptype = NULL;
        while (*ptype == '-' && old_ptype != ptype) {
            old_ptype = ptype;
            ptype = next_arg_type(ptype);
        }
        switch (*ptype) {
        case 'F':
            /* file completion */
            readline_set_completion_index(mon->rs, strlen(str));
            file_completion(mon, str);
            break;
        case 'B':
            /* block device name completion */
            readline_set_completion_index(mon->rs, strlen(str));
            while ((blk = blk_next(blk)) != NULL) {
                readline_add_completion_of(mon->rs, str, blk_name(blk));
            }
            break;
        case 's':
        case 'S':
            if (!strcmp(cmd->name, "help|?")) {
                monitor_find_completion_by_table(mon, cmd_table,
                                                 &args[1], nb_args - 1);
            }
            break;
        default:
            break;
        }
    }
}

static void monitor_find_completion(void *opaque,
                                    const char *cmdline)
{
    MonitorHMP *mon = opaque;
    char *args[MAX_ARGS];
    int nb_args, len;

    /* 1. parse the cmdline */
    if (parse_cmdline(cmdline, &nb_args, args) < 0) {
        return;
    }

    /*
     * if the line ends with a space, it means we want to complete the
     * next arg
     */
    len = strlen(cmdline);
    if (len > 0 && qemu_isspace(cmdline[len - 1])) {
        if (nb_args >= MAX_ARGS) {
            goto cleanup;
        }
        args[nb_args++] = g_strdup("");
    }

    /* 2. auto complete according to args */
    monitor_find_completion_by_table(mon, hmp_cmds, args, nb_args);

cleanup:
    free_cmdline_args(args, nb_args);
}

static void monitor_read(void *opaque, const uint8_t *buf, int size)
{
    MonitorHMP *mon = container_of(opaque, MonitorHMP, common);
    int i;

    if (mon->rs) {
        for (i = 0; i < size; i++) {
            readline_handle_byte(mon->rs, buf[i]);
        }
    } else {
        if (size == 0 || buf[size - 1] != 0) {
            monitor_printf(&mon->common, "corrupted command\n");
        } else {
            handle_hmp_command(mon, (char *)buf);
        }
    }
}

static void monitor_event(void *opaque, QEMUChrEvent event)
{
    Monitor *mon = opaque;

    switch (event) {
    case CHR_EVENT_MUX_IN:
        qemu_mutex_lock(&mon->mon_lock);
        if (mon->mux_out) {
            mon->mux_out = 0;
            monitor_resume(mon);
        }
        qemu_mutex_unlock(&mon->mon_lock);
        break;

    case CHR_EVENT_MUX_OUT:
        qemu_mutex_lock(&mon->mon_lock);
        if (!mon->mux_out) {
            if (mon->reset_seen && !mon->suspend_cnt) {
                monitor_puts_locked(mon, "\n");
            } else {
                monitor_flush_locked(mon);
            }
            monitor_suspend(mon);
            mon->mux_out = 1;
        }
        qemu_mutex_unlock(&mon->mon_lock);
        break;

    case CHR_EVENT_OPENED:
        monitor_printf(mon, "QEMU %s monitor - type 'help' for more "
                       "information\n", QEMU_VERSION);
        qemu_mutex_lock(&mon->mon_lock);
        mon->reset_seen = 1;
        if (!mon->mux_out) {
            /* Suspend-resume forces the prompt to be printed.  */
            monitor_suspend(mon);
            monitor_resume(mon);
        }
        qemu_mutex_unlock(&mon->mon_lock);
        mon_refcount++;
        break;

    case CHR_EVENT_CLOSED:
        mon_refcount--;
        monitor_fdsets_cleanup();
        break;

    case CHR_EVENT_BREAK:
        /* Ignored */
        break;
    }
}


/*
 * These functions just adapt the readline interface in a typesafe way.  We
 * could cast function pointers but that discards compiler checks.
 */
static void G_GNUC_PRINTF(2, 3) monitor_readline_printf(void *opaque,
                                                       const char *fmt, ...)
{
    MonitorHMP *mon = opaque;
    va_list ap;
    va_start(ap, fmt);
    monitor_vprintf(&mon->common, fmt, ap);
    va_end(ap);
}

static void monitor_readline_flush(void *opaque)
{
    MonitorHMP *mon = opaque;
    monitor_flush(&mon->common);
}

void monitor_init_hmp(Chardev *chr, bool use_readline, Error **errp)
{
    MonitorHMP *mon = g_new0(MonitorHMP, 1);

    if (!qemu_chr_fe_init(&mon->common.chr, chr, errp)) {
        g_free(mon);
        return;
    }

    monitor_data_init(&mon->common, false, false, false);

    mon->use_readline = use_readline;
    if (mon->use_readline) {
        mon->rs = readline_init(monitor_readline_printf,
                                monitor_readline_flush,
                                mon,
                                monitor_find_completion);
        monitor_read_command(mon, 0);
    }

    qemu_chr_fe_set_handlers(&mon->common.chr, monitor_can_read, monitor_read,
                             monitor_event, NULL, &mon->common, NULL, true);
    monitor_list_append(&mon->common);
}
