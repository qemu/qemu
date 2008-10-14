/*
 * QEMU live migration
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu-common.h"
#include "migration.h"
#include "console.h"

/* Migration speed throttling */
static uint32_t max_throttle = (32 << 20);

static MigrationState *current_migration;

void qemu_start_incoming_migration(const char *uri)
{
    const char *p;

    if (strstart(uri, "tcp:", &p))
        tcp_start_incoming_migration(p);
    else
        fprintf(stderr, "unknown migration protocol: %s\n", uri);
}

void do_migrate(int detach, const char *uri)
{
    MigrationState *s = NULL;
    const char *p;

    if (strstart(uri, "tcp:", &p))
	s = tcp_start_outgoing_migration(p, max_throttle, detach);
    else
        term_printf("unknown migration protocol: %s\n", uri);

    if (s == NULL)
	term_printf("migration failed\n");
    else {
	if (current_migration)
	    current_migration->release(current_migration);

	current_migration = s;
    }
}

void do_migrate_cancel(void)
{
    MigrationState *s = current_migration;

    if (s)
	s->cancel(s);
}

void do_migrate_set_speed(const char *value)
{
    double d;
    char *ptr;

    d = strtod(value, &ptr);
    switch (*ptr) {
    case 'G': case 'g':
	d *= 1024;
    case 'M': case 'm':
	d *= 1024;
    case 'K': case 'k':
	d *= 1024;
    default:
	break;
    }

    max_throttle = (uint32_t)d;
}

void do_info_migrate(void)
{
    MigrationState *s = current_migration;
    
    if (s) {
	term_printf("Migration status: ");
	switch (s->get_status(s)) {
	case MIG_STATE_ACTIVE:
	    term_printf("active\n");
	    break;
	case MIG_STATE_COMPLETED:
	    term_printf("completed\n");
	    break;
	case MIG_STATE_ERROR:
	    term_printf("failed\n");
	    break;
	case MIG_STATE_CANCELLED:
	    term_printf("cancelled\n");
	    break;
	}
    }
}

