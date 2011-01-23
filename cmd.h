/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
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
#ifndef __COMMAND_H__
#define __COMMAND_H__

#define CMD_FLAG_GLOBAL	((int)0x80000000)	/* don't iterate "args" */

typedef int (*cfunc_t)(int argc, char **argv);
typedef void (*helpfunc_t)(void);

typedef struct cmdinfo {
	const char	*name;
	const char	*altname;
	cfunc_t		cfunc;
	int		argmin;
	int		argmax;
	int		canpush;
	int		flags;
	const char	*args;
	const char	*oneline;
	helpfunc_t      help;
} cmdinfo_t;

extern cmdinfo_t	*cmdtab;
extern int		ncmds;

void help_init(void);
void quit_init(void);

typedef int (*argsfunc_t)(int index);
typedef int (*checkfunc_t)(const cmdinfo_t *ci);

void add_command(const cmdinfo_t *ci);
void add_user_command(char *optarg);
void add_args_command(argsfunc_t af);
void add_check_command(checkfunc_t cf);

const cmdinfo_t *find_command(const char *cmd);

void command_loop(void);
int command_usage(const cmdinfo_t *ci);
int command(const cmdinfo_t *ci, int argc, char **argv);

/* from input.h */
char **breakline(char *input, int *count);
void doneline(char *input, char **vec);
char *fetchline(void);

long long cvtnum(char *s);
void cvtstr(double value, char *str, size_t sz);

struct timeval tsub(struct timeval t1, struct timeval t2);
double tdiv(double value, struct timeval tv);

enum {
	DEFAULT_TIME		= 0x0,
	TERSE_FIXED_TIME	= 0x1,
	VERBOSE_FIXED_TIME	= 0x2
};

void timestr(struct timeval *tv, char *str, size_t sz, int flags);

extern char *progname;

#endif	/* __COMMAND_H__ */
