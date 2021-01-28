/*
 * FUSE: Filesystem in Userspace
 * Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
 *
 * Implementation of option parsing routines (dealing with `struct
 * fuse_args`).
 *
 * This program can be distributed under the terms of the GNU LGPLv2.
 * See the file COPYING.LIB
 */

#include "qemu/osdep.h"
#include "fuse_opt.h"
#include "fuse_i.h"
#include "fuse_misc.h"


struct fuse_opt_context {
    void *data;
    const struct fuse_opt *opt;
    fuse_opt_proc_t proc;
    int argctr;
    int argc;
    char **argv;
    struct fuse_args outargs;
    char *opts;
    int nonopt;
};

void fuse_opt_free_args(struct fuse_args *args)
{
    if (args) {
        if (args->argv && args->allocated) {
            int i;
            for (i = 0; i < args->argc; i++) {
                free(args->argv[i]);
            }
            free(args->argv);
        }
        args->argc = 0;
        args->argv = NULL;
        args->allocated = 0;
    }
}

static int alloc_failed(void)
{
    fuse_log(FUSE_LOG_ERR, "fuse: memory allocation failed\n");
    return -1;
}

int fuse_opt_add_arg(struct fuse_args *args, const char *arg)
{
    char **newargv;
    char *newarg;

    assert(!args->argv || args->allocated);

    newarg = strdup(arg);
    if (!newarg) {
        return alloc_failed();
    }

    newargv = realloc(args->argv, (args->argc + 2) * sizeof(char *));
    if (!newargv) {
        free(newarg);
        return alloc_failed();
    }

    args->argv = newargv;
    args->allocated = 1;
    args->argv[args->argc++] = newarg;
    args->argv[args->argc] = NULL;
    return 0;
}

static int fuse_opt_insert_arg_common(struct fuse_args *args, int pos,
                                      const char *arg)
{
    assert(pos <= args->argc);
    if (fuse_opt_add_arg(args, arg) == -1) {
        return -1;
    }

    if (pos != args->argc - 1) {
        char *newarg = args->argv[args->argc - 1];
        memmove(&args->argv[pos + 1], &args->argv[pos],
                sizeof(char *) * (args->argc - pos - 1));
        args->argv[pos] = newarg;
    }
    return 0;
}

int fuse_opt_insert_arg(struct fuse_args *args, int pos, const char *arg)
{
    return fuse_opt_insert_arg_common(args, pos, arg);
}

static int next_arg(struct fuse_opt_context *ctx, const char *opt)
{
    if (ctx->argctr + 1 >= ctx->argc) {
        fuse_log(FUSE_LOG_ERR, "fuse: missing argument after `%s'\n", opt);
        return -1;
    }
    ctx->argctr++;
    return 0;
}

static int add_arg(struct fuse_opt_context *ctx, const char *arg)
{
    return fuse_opt_add_arg(&ctx->outargs, arg);
}

static int add_opt_common(char **opts, const char *opt, int esc)
{
    unsigned oldlen = *opts ? strlen(*opts) : 0;
    char *d = realloc(*opts, oldlen + 1 + strlen(opt) * 2 + 1);

    if (!d) {
        return alloc_failed();
    }

    *opts = d;
    if (oldlen) {
        d += oldlen;
        *d++ = ',';
    }

    for (; *opt; opt++) {
        if (esc && (*opt == ',' || *opt == '\\')) {
            *d++ = '\\';
        }
        *d++ = *opt;
    }
    *d = '\0';

    return 0;
}

int fuse_opt_add_opt(char **opts, const char *opt)
{
    return add_opt_common(opts, opt, 0);
}

int fuse_opt_add_opt_escaped(char **opts, const char *opt)
{
    return add_opt_common(opts, opt, 1);
}

static int add_opt(struct fuse_opt_context *ctx, const char *opt)
{
    return add_opt_common(&ctx->opts, opt, 1);
}

static int call_proc(struct fuse_opt_context *ctx, const char *arg, int key,
                     int iso)
{
    if (key == FUSE_OPT_KEY_DISCARD) {
        return 0;
    }

    if (key != FUSE_OPT_KEY_KEEP && ctx->proc) {
        int res = ctx->proc(ctx->data, arg, key, &ctx->outargs);
        if (res == -1 || !res) {
            return res;
        }
    }
    if (iso) {
        return add_opt(ctx, arg);
    } else {
        return add_arg(ctx, arg);
    }
}

static int match_template(const char *t, const char *arg, unsigned *sepp)
{
    int arglen = strlen(arg);
    const char *sep = strchr(t, '=');
    sep = sep ? sep : strchr(t, ' ');
    if (sep && (!sep[1] || sep[1] == '%')) {
        int tlen = sep - t;
        if (sep[0] == '=') {
            tlen++;
        }
        if (arglen >= tlen && strncmp(arg, t, tlen) == 0) {
            *sepp = sep - t;
            return 1;
        }
    }
    if (strcmp(t, arg) == 0) {
        *sepp = 0;
        return 1;
    }
    return 0;
}

static const struct fuse_opt *find_opt(const struct fuse_opt *opt,
                                       const char *arg, unsigned *sepp)
{
    for (; opt && opt->templ; opt++) {
        if (match_template(opt->templ, arg, sepp)) {
            return opt;
        }
    }
    return NULL;
}

int fuse_opt_match(const struct fuse_opt *opts, const char *opt)
{
    unsigned dummy;
    return find_opt(opts, opt, &dummy) ? 1 : 0;
}

static int process_opt_param(void *var, const char *format, const char *param,
                             const char *arg)
{
    assert(format[0] == '%');
    if (format[1] == 's') {
        char **s = var;
        char *copy = strdup(param);
        if (!copy) {
            return alloc_failed();
        }

        free(*s);
        *s = copy;
    } else {
        if (sscanf(param, format, var) != 1) {
            fuse_log(FUSE_LOG_ERR, "fuse: invalid parameter in option `%s'\n",
                     arg);
            return -1;
        }
    }
    return 0;
}

static int process_opt(struct fuse_opt_context *ctx, const struct fuse_opt *opt,
                       unsigned sep, const char *arg, int iso)
{
    if (opt->offset == -1U) {
        if (call_proc(ctx, arg, opt->value, iso) == -1) {
            return -1;
        }
    } else {
        void *var = (char *)ctx->data + opt->offset;
        if (sep && opt->templ[sep + 1]) {
            const char *param = arg + sep;
            if (opt->templ[sep] == '=') {
                param++;
            }
            if (process_opt_param(var, opt->templ + sep + 1, param, arg) ==
                -1) {
                return -1;
            }
        } else {
            *(int *)var = opt->value;
        }
    }
    return 0;
}

static int process_opt_sep_arg(struct fuse_opt_context *ctx,
                               const struct fuse_opt *opt, unsigned sep,
                               const char *arg, int iso)
{
    int res;
    char *newarg;
    char *param;

    if (next_arg(ctx, arg) == -1) {
        return -1;
    }

    param = ctx->argv[ctx->argctr];
    newarg = malloc(sep + strlen(param) + 1);
    if (!newarg) {
        return alloc_failed();
    }

    memcpy(newarg, arg, sep);
    strcpy(newarg + sep, param);
    res = process_opt(ctx, opt, sep, newarg, iso);
    free(newarg);

    return res;
}

static int process_gopt(struct fuse_opt_context *ctx, const char *arg, int iso)
{
    unsigned sep;
    const struct fuse_opt *opt = find_opt(ctx->opt, arg, &sep);
    if (opt) {
        for (; opt; opt = find_opt(opt + 1, arg, &sep)) {
            int res;
            if (sep && opt->templ[sep] == ' ' && !arg[sep]) {
                res = process_opt_sep_arg(ctx, opt, sep, arg, iso);
            } else {
                res = process_opt(ctx, opt, sep, arg, iso);
            }
            if (res == -1) {
                return -1;
            }
        }
        return 0;
    } else {
        return call_proc(ctx, arg, FUSE_OPT_KEY_OPT, iso);
    }
}

static int process_real_option_group(struct fuse_opt_context *ctx, char *opts)
{
    char *s = opts;
    char *d = s;
    int end = 0;

    while (!end) {
        if (*s == '\0') {
            end = 1;
        }
        if (*s == ',' || end) {
            int res;

            *d = '\0';
            res = process_gopt(ctx, opts, 1);
            if (res == -1) {
                return -1;
            }
            d = opts;
        } else {
            if (s[0] == '\\' && s[1] != '\0') {
                s++;
                if (s[0] >= '0' && s[0] <= '3' && s[1] >= '0' && s[1] <= '7' &&
                    s[2] >= '0' && s[2] <= '7') {
                    *d++ = (s[0] - '0') * 0100 + (s[1] - '0') * 0010 +
                           (s[2] - '0');
                    s += 2;
                } else {
                    *d++ = *s;
                }
            } else {
                *d++ = *s;
            }
        }
        s++;
    }

    return 0;
}

static int process_option_group(struct fuse_opt_context *ctx, const char *opts)
{
    int res;
    char *copy = strdup(opts);

    if (!copy) {
        fuse_log(FUSE_LOG_ERR, "fuse: memory allocation failed\n");
        return -1;
    }
    res = process_real_option_group(ctx, copy);
    free(copy);
    return res;
}

static int process_one(struct fuse_opt_context *ctx, const char *arg)
{
    if (ctx->nonopt || arg[0] != '-') {
        return call_proc(ctx, arg, FUSE_OPT_KEY_NONOPT, 0);
    } else if (arg[1] == 'o') {
        if (arg[2]) {
            return process_option_group(ctx, arg + 2);
        } else {
            if (next_arg(ctx, arg) == -1) {
                return -1;
            }

            return process_option_group(ctx, ctx->argv[ctx->argctr]);
        }
    } else if (arg[1] == '-' && !arg[2]) {
        if (add_arg(ctx, arg) == -1) {
            return -1;
        }
        ctx->nonopt = ctx->outargs.argc;
        return 0;
    } else {
        return process_gopt(ctx, arg, 0);
    }
}

static int opt_parse(struct fuse_opt_context *ctx)
{
    if (ctx->argc) {
        if (add_arg(ctx, ctx->argv[0]) == -1) {
            return -1;
        }
    }

    for (ctx->argctr = 1; ctx->argctr < ctx->argc; ctx->argctr++) {
        if (process_one(ctx, ctx->argv[ctx->argctr]) == -1) {
            return -1;
        }
    }

    if (ctx->opts) {
        if (fuse_opt_insert_arg(&ctx->outargs, 1, "-o") == -1 ||
            fuse_opt_insert_arg(&ctx->outargs, 2, ctx->opts) == -1) {
            return -1;
        }
    }

    /* If option separator ("--") is the last argument, remove it */
    if (ctx->nonopt && ctx->nonopt == ctx->outargs.argc &&
        strcmp(ctx->outargs.argv[ctx->outargs.argc - 1], "--") == 0) {
        free(ctx->outargs.argv[ctx->outargs.argc - 1]);
        ctx->outargs.argv[--ctx->outargs.argc] = NULL;
    }

    return 0;
}

int fuse_opt_parse(struct fuse_args *args, void *data,
                   const struct fuse_opt opts[], fuse_opt_proc_t proc)
{
    int res;
    struct fuse_opt_context ctx = {
        .data = data,
        .opt = opts,
        .proc = proc,
    };

    if (!args || !args->argv || !args->argc) {
        return 0;
    }

    ctx.argc = args->argc;
    ctx.argv = args->argv;

    res = opt_parse(&ctx);
    if (res != -1) {
        struct fuse_args tmp = *args;
        *args = ctx.outargs;
        ctx.outargs = tmp;
    }
    free(ctx.opts);
    fuse_opt_free_args(&ctx.outargs);
    return res;
}
