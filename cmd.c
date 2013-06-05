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
#include "block/aio.h"
#include "qemu/main-loop.h"

#define _(x)	x	/* not gettext support yet */

/* from libxcmd/command.c */

#define EXABYTES(x)	((long long)(x) << 60)
#define PETABYTES(x)	((long long)(x) << 50)
#define TERABYTES(x)	((long long)(x) << 40)
#define GIGABYTES(x)	((long long)(x) << 30)
#define MEGABYTES(x)	((long long)(x) << 20)
#define KILOBYTES(x)	((long long)(x) << 10)

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
	char		*trim;
	const char	*suffix;

	if (value >= EXABYTES(1)) {
		suffix = " EiB";
		snprintf(str, size - 4, "%.3f", TO_EXABYTES(value));
	} else if (value >= PETABYTES(1)) {
		suffix = " PiB";
		snprintf(str, size - 4, "%.3f", TO_PETABYTES(value));
	} else if (value >= TERABYTES(1)) {
		suffix = " TiB";
		snprintf(str, size - 4, "%.3f", TO_TERABYTES(value));
	} else if (value >= GIGABYTES(1)) {
		suffix = " GiB";
		snprintf(str, size - 4, "%.3f", TO_GIGABYTES(value));
	} else if (value >= MEGABYTES(1)) {
		suffix = " MiB";
		snprintf(str, size - 4, "%.3f", TO_MEGABYTES(value));
	} else if (value >= KILOBYTES(1)) {
		suffix = " KiB";
		snprintf(str, size - 4, "%.3f", TO_KILOBYTES(value));
	} else {
		suffix = " bytes";
		snprintf(str, size - 6, "%f", value);
	}

	trim = strstr(str, ".000");
	if (trim) {
		strcpy(trim, suffix);
	} else {
		strcat(str, suffix);
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
