/*
 * Copyright (c) 2003-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/time.h>
#include <getopt.h>

#include "cmd.h"
#include "qemu-aio.h"

#define _(x)	x	/* not gettext support yet */

/* from libxcmd/command.c */

cmdinfo_t	*cmdtab;
int		ncmds;

static argsfunc_t	args_func;
static checkfunc_t	check_func;
static int		ncmdline;
static char		**cmdline;

static int
compare(const void *a, const void *b)
{
	return strcmp(((const cmdinfo_t *)a)->name,
		      ((const cmdinfo_t *)b)->name);
}

void add_command(const cmdinfo_t *ci)
{
    cmdtab = g_realloc((void *)cmdtab, ++ncmds * sizeof(*cmdtab));
    cmdtab[ncmds - 1] = *ci;
    qsort(cmdtab, ncmds, sizeof(*cmdtab), compare);
}

static int
check_command(
	const cmdinfo_t	*ci)
{
	if (check_func)
		return check_func(ci);
	return 1;
}

void
add_check_command(
	checkfunc_t	cf)
{
	check_func = cf;
}

int
command_usage(
	const cmdinfo_t *ci)
{
	printf("%s %s -- %s\n", ci->name, ci->args, ci->oneline);
	return 0;
}

int
command(
	const cmdinfo_t	*ct,
	int		argc,
	char		**argv)
{
	char		*cmd = argv[0];

	if (!check_command(ct))
		return 0;

	if (argc-1 < ct->argmin || (ct->argmax != -1 && argc-1 > ct->argmax)) {
		if (ct->argmax == -1)
			fprintf(stderr,
	_("bad argument count %d to %s, expected at least %d arguments\n"),
				argc-1, cmd, ct->argmin);
		else if (ct->argmin == ct->argmax)
			fprintf(stderr,
	_("bad argument count %d to %s, expected %d arguments\n"),
				argc-1, cmd, ct->argmin);
		else
			fprintf(stderr,
	_("bad argument count %d to %s, expected between %d and %d arguments\n"),
			argc-1, cmd, ct->argmin, ct->argmax);
		return 0;
	}
	optind = 0;
	return ct->cfunc(argc, argv);
}

const cmdinfo_t *
find_command(
	const char	*cmd)
{
	cmdinfo_t	*ct;

	for (ct = cmdtab; ct < &cmdtab[ncmds]; ct++) {
		if (strcmp(ct->name, cmd) == 0 ||
		    (ct->altname && strcmp(ct->altname, cmd) == 0))
			return (const cmdinfo_t *)ct;
	}
	return NULL;
}

void add_user_command(char *optarg)
{
    cmdline = g_realloc(cmdline, ++ncmdline * sizeof(char *));
    cmdline[ncmdline-1] = optarg;
}

static int
args_command(
	int	index)
{
	if (args_func)
		return args_func(index);
	return 0;
}

void
add_args_command(
	argsfunc_t	af)
{
	args_func = af;
}

static void prep_fetchline(void *opaque)
{
    int *fetchable = opaque;

    qemu_aio_set_fd_handler(STDIN_FILENO, NULL, NULL, NULL, NULL, NULL);
    *fetchable= 1;
}

static char *get_prompt(void);

void command_loop(void)
{
    int c, i, j = 0, done = 0, fetchable = 0, prompted = 0;
    char *input;
    char **v;
    const cmdinfo_t *ct;

    for (i = 0; !done && i < ncmdline; i++) {
        input = strdup(cmdline[i]);
        if (!input) {
            fprintf(stderr, _("cannot strdup command '%s': %s\n"),
                    cmdline[i], strerror(errno));
            exit(1);
        }
        v = breakline(input, &c);
        if (c) {
            ct = find_command(v[0]);
            if (ct) {
                if (ct->flags & CMD_FLAG_GLOBAL) {
                    done = command(ct, c, v);
                } else {
                    j = 0;
                    while (!done && (j = args_command(j))) {
                        done = command(ct, c, v);
                    }
                }
            } else {
                fprintf(stderr, _("command \"%s\" not found\n"), v[0]);
            }
	}
        doneline(input, v);
    }
    if (cmdline) {
        g_free(cmdline);
        return;
    }

    while (!done) {
        if (!prompted) {
            printf("%s", get_prompt());
            fflush(stdout);
            qemu_aio_set_fd_handler(STDIN_FILENO, prep_fetchline, NULL, NULL,
                                    NULL, &fetchable);
            prompted = 1;
        }

        qemu_aio_wait();

        if (!fetchable) {
            continue;
        }
        input = fetchline();
        if (input == NULL) {
            break;
        }
        v = breakline(input, &c);
        if (c) {
            ct = find_command(v[0]);
            if (ct) {
                done = command(ct, c, v);
            } else {
                fprintf(stderr, _("command \"%s\" not found\n"), v[0]);
            }
        }
        doneline(input, v);

        prompted = 0;
        fetchable = 0;
    }
    qemu_aio_set_fd_handler(STDIN_FILENO, NULL, NULL, NULL, NULL, NULL);
}

/* from libxcmd/input.c */

#if defined(ENABLE_READLINE)
# include <readline/history.h>
# include <readline/readline.h>
#elif defined(ENABLE_EDITLINE)
# include <histedit.h>
#endif

static char *
get_prompt(void)
{
	static char	prompt[FILENAME_MAX + 2 /*"> "*/ + 1 /*"\0"*/ ];

	if (!prompt[0])
		snprintf(prompt, sizeof(prompt), "%s> ", progname);
	return prompt;
}

#if defined(ENABLE_READLINE)
char *
fetchline(void)
{
	char	*line;

	line = readline(get_prompt());
	if (line && *line)
		add_history(line);
	return line;
}
#elif defined(ENABLE_EDITLINE)
static char *el_get_prompt(EditLine *e) { return get_prompt(); }
char *
fetchline(void)
{
	static EditLine	*el;
	static History	*hist;
	HistEvent	hevent;
	char		*line;
	int		count;

	if (!el) {
		hist = history_init();
		history(hist, &hevent, H_SETSIZE, 100);
		el = el_init(progname, stdin, stdout, stderr);
		el_source(el, NULL);
		el_set(el, EL_SIGNAL, 1);
		el_set(el, EL_PROMPT, el_get_prompt);
		el_set(el, EL_HIST, history, (const char *)hist);
	}
	line = strdup(el_gets(el, &count));
	if (line) {
		if (count > 0)
			line[count-1] = '\0';
		if (*line)
			history(hist, &hevent, H_ENTER, line);
	}
	return line;
}
#else
# define MAXREADLINESZ	1024
char *
fetchline(void)
{
	char	*p, *line = malloc(MAXREADLINESZ);

	if (!line)
		return NULL;
	if (!fgets(line, MAXREADLINESZ, stdin)) {
		free(line);
		return NULL;
	}
	p = line + strlen(line);
	if (p != line && p[-1] == '\n')
		p[-1] = '\0';
	return line;
}
#endif

static char *qemu_strsep(char **input, const char *delim)
{
    char *result = *input;
    if (result != NULL) {
        char *p;

        for (p = result; *p != '\0'; p++) {
            if (strchr(delim, *p)) {
                break;
            }
        }
        if (*p == '\0') {
            *input = NULL;
        } else {
            *p = '\0';
            *input = p + 1;
        }
    }
    return result;
}

char **breakline(char *input, int *count)
{
    int c = 0;
    char *p;
    char **rval = calloc(sizeof(char *), 1);
    char **tmp;

    while (rval && (p = qemu_strsep(&input, " ")) != NULL) {
        if (!*p) {
            continue;
        }
        c++;
        tmp = realloc(rval, sizeof(*rval) * (c + 1));
        if (!tmp) {
            free(rval);
            rval = NULL;
            c = 0;
            break;
        } else {
            rval = tmp;
        }
        rval[c - 1] = p;
        rval[c] = NULL;
    }
    *count = c;
    return rval;
}

void
doneline(
	char	*input,
	char	**vec)
{
	free(input);
	free(vec);
}

#define EXABYTES(x)	((long long)(x) << 60)
#define PETABYTES(x)	((long long)(x) << 50)
#define TERABYTES(x)	((long long)(x) << 40)
#define GIGABYTES(x)	((long long)(x) << 30)
#define MEGABYTES(x)	((long long)(x) << 20)
#define KILOBYTES(x)	((long long)(x) << 10)

long long
cvtnum(
	char		*s)
{
	long long	i;
	char		*sp;
	int		c;

	i = strtoll(s, &sp, 0);
	if (i == 0 && sp == s)
		return -1LL;
	if (*sp == '\0')
		return i;

	if (sp[1] != '\0')
		return -1LL;

	c = qemu_tolower(*sp);
	switch (c) {
	default:
		return i;
	case 'k':
		return KILOBYTES(i);
	case 'm':
		return MEGABYTES(i);
	case 'g':
		return GIGABYTES(i);
	case 't':
		return TERABYTES(i);
	case 'p':
		return PETABYTES(i);
	case 'e':
		return  EXABYTES(i);
	}
	return -1LL;
}

#define TO_EXABYTES(x)	((x) / EXABYTES(1))
#define TO_PETABYTES(x)	((x) / PETABYTES(1))
#define TO_TERABYTES(x)	((x) / TERABYTES(1))
#define TO_GIGABYTES(x)	((x) / GIGABYTES(1))
#define TO_MEGABYTES(x)	((x) / MEGABYTES(1))
#define TO_KILOBYTES(x)	((x) / KILOBYTES(1))

void
cvtstr(
	double		value,
	char		*str,
	size_t		size)
{
	const char	*fmt;
	int		precise;

	precise = ((double)value * 1000 == (double)(int)value * 1000);

	if (value >= EXABYTES(1)) {
		fmt = precise ? "%.f EiB" : "%.3f EiB";
		snprintf(str, size, fmt, TO_EXABYTES(value));
	} else if (value >= PETABYTES(1)) {
		fmt = precise ? "%.f PiB" : "%.3f PiB";
		snprintf(str, size, fmt, TO_PETABYTES(value));
	} else if (value >= TERABYTES(1)) {
		fmt = precise ? "%.f TiB" : "%.3f TiB";
		snprintf(str, size, fmt, TO_TERABYTES(value));
	} else if (value >= GIGABYTES(1)) {
		fmt = precise ? "%.f GiB" : "%.3f GiB";
		snprintf(str, size, fmt, TO_GIGABYTES(value));
	} else if (value >= MEGABYTES(1)) {
		fmt = precise ? "%.f MiB" : "%.3f MiB";
		snprintf(str, size, fmt, TO_MEGABYTES(value));
	} else if (value >= KILOBYTES(1)) {
		fmt = precise ? "%.f KiB" : "%.3f KiB";
		snprintf(str, size, fmt, TO_KILOBYTES(value));
	} else {
		snprintf(str, size, "%f bytes", value);
	}
}

struct timeval
tsub(struct timeval t1, struct timeval t2)
{
	t1.tv_usec -= t2.tv_usec;
	if (t1.tv_usec < 0) {
		t1.tv_usec += 1000000;
		t1.tv_sec--;
	}
	t1.tv_sec -= t2.tv_sec;
	return t1;
}

double
tdiv(double value, struct timeval tv)
{
	return value / ((double)tv.tv_sec + ((double)tv.tv_usec / 1000000.0));
}

#define HOURS(sec)	((sec) / (60 * 60))
#define MINUTES(sec)	(((sec) % (60 * 60)) / 60)
#define SECONDS(sec)	((sec) % 60)

void
timestr(
	struct timeval	*tv,
	char		*ts,
	size_t		size,
	int		format)
{
	double		usec = (double)tv->tv_usec / 1000000.0;

	if (format & TERSE_FIXED_TIME) {
		if (!HOURS(tv->tv_sec)) {
			snprintf(ts, size, "%u:%02u.%02u",
				(unsigned int) MINUTES(tv->tv_sec),
				(unsigned int) SECONDS(tv->tv_sec),
				(unsigned int) (usec * 100));
			return;
		}
		format |= VERBOSE_FIXED_TIME;	/* fallback if hours needed */
	}

	if ((format & VERBOSE_FIXED_TIME) || tv->tv_sec) {
		snprintf(ts, size, "%u:%02u:%02u.%02u",
			(unsigned int) HOURS(tv->tv_sec),
			(unsigned int) MINUTES(tv->tv_sec),
			(unsigned int) SECONDS(tv->tv_sec),
			(unsigned int) (usec * 100));
	} else {
		snprintf(ts, size, "0.%04u sec", (unsigned int) (usec * 10000));
	}
}


/* from libxcmd/quit.c */

static cmdinfo_t quit_cmd;

/* ARGSUSED */
static int
quit_f(
	int	argc,
	char	**argv)
{
	return 1;
}

void
quit_init(void)
{
	quit_cmd.name = _("quit");
	quit_cmd.altname = _("q");
	quit_cmd.cfunc = quit_f;
	quit_cmd.argmin = -1;
	quit_cmd.argmax = -1;
	quit_cmd.flags = CMD_FLAG_GLOBAL;
	quit_cmd.oneline = _("exit the program");

	add_command(&quit_cmd);
}

/* from libxcmd/help.c */

static cmdinfo_t help_cmd;
static void help_onecmd(const char *cmd, const cmdinfo_t *ct);
static void help_oneline(const char *cmd, const cmdinfo_t *ct);

static void
help_all(void)
{
	const cmdinfo_t	*ct;

	for (ct = cmdtab; ct < &cmdtab[ncmds]; ct++)
		help_oneline(ct->name, ct);
	printf(_("\nUse 'help commandname' for extended help.\n"));
}

static int
help_f(
	int		argc,
	char		**argv)
{
	const cmdinfo_t	*ct;

	if (argc == 1) {
		help_all();
		return 0;
	}
	ct = find_command(argv[1]);
	if (ct == NULL) {
		printf(_("command %s not found\n"), argv[1]);
		return 0;
	}
	help_onecmd(argv[1], ct);
	return 0;
}

static void
help_onecmd(
	const char	*cmd,
	const cmdinfo_t	*ct)
{
	help_oneline(cmd, ct);
	if (ct->help)
		ct->help();
}

static void
help_oneline(
	const char	*cmd,
	const cmdinfo_t	*ct)
{
	if (cmd)
		printf("%s ", cmd);
	else {
		printf("%s ", ct->name);
		if (ct->altname)
			printf("(or %s) ", ct->altname);
	}
	if (ct->args)
		printf("%s ", ct->args);
	printf("-- %s\n", ct->oneline);
}

void
help_init(void)
{
	help_cmd.name = _("help");
	help_cmd.altname = _("?");
	help_cmd.cfunc = help_f;
	help_cmd.argmin = 0;
	help_cmd.argmax = 1;
	help_cmd.flags = CMD_FLAG_GLOBAL;
	help_cmd.args = _("[command]");
	help_cmd.oneline = _("help for one or all commands");

	add_command(&help_cmd);
}
