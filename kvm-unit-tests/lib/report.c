/*
 * Test result reporting
 *
 * Copyright (c) Siemens AG, 2014
 *
 * Authors:
 *  Jan Kiszka <jan.kiszka@siemens.com>
 *  Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */

#include "libcflat.h"
#include "asm/spinlock.h"

static unsigned int tests, failures, xfailures;
static char prefixes[256];
static struct spinlock lock;

void report_prefix_push(const char *prefix)
{
	spin_lock(&lock);
	strcat(prefixes, prefix);
	strcat(prefixes, ": ");
	spin_unlock(&lock);
}

void report_prefix_pop(void)
{
	char *p, *q;

	spin_lock(&lock);

	if (!*prefixes)
		return;

	for (p = prefixes, q = strstr(p, ": ") + 2;
			*q;
			p = q, q = strstr(p, ": ") + 2)
		;
	*p = '\0';

	spin_unlock(&lock);
}

void va_report_xfail(const char *msg_fmt, bool xfail, bool cond, va_list va)
{
	char *pass = xfail ? "XPASS" : "PASS";
	char *fail = xfail ? "XFAIL" : "FAIL";
	char buf[2000];

	spin_lock(&lock);

	tests++;
	printf("%s: ", cond ? pass : fail);
	puts(prefixes);
	vsnprintf(buf, sizeof(buf), msg_fmt, va);
	puts(buf);
	puts("\n");
	if (xfail && cond)
		failures++;
	else if (xfail)
		xfailures++;
	else if (!cond)
		failures++;

	spin_unlock(&lock);
}

void report(const char *msg_fmt, bool pass, ...)
{
	va_list va;
	va_start(va, pass);
	va_report_xfail(msg_fmt, false, pass, va);
	va_end(va);
}

void report_xfail(const char *msg_fmt, bool xfail, bool pass, ...)
{
	va_list va;
	va_start(va, pass);
	va_report_xfail(msg_fmt, xfail, pass, va);
	va_end(va);
}

int report_summary(void)
{
	spin_lock(&lock);

	printf("\nSUMMARY: %d tests, %d unexpected failures", tests, failures);
	if (xfailures)
		printf(", %d expected failures\n", xfailures);
	else
		printf("\n");
	return failures > 0 ? 1 : 0;

	spin_unlock(&lock);
}
