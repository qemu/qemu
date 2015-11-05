/*
 * Postcopy migration for RAM
 *
 * Copyright 2013 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Dave Gilbert  <dgilbert@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#ifndef QEMU_POSTCOPY_RAM_H
#define QEMU_POSTCOPY_RAM_H

/* Return true if the host supports everything we need to do postcopy-ram */
bool postcopy_ram_supported_by_host(void);

/*
 * Discard the contents of 'length' bytes from 'start'
 * We can assume that if we've been called postcopy_ram_hosttest returned true
 */
int postcopy_ram_discard_range(MigrationIncomingState *mis, uint8_t *start,
                               size_t length);


/*
 * Called at the start of each RAMBlock by the bitmap code.
 * 'offset' is the bitmap offset of the named RAMBlock in the migration
 * bitmap.
 * Returns a new PDS
 */
PostcopyDiscardState *postcopy_discard_send_init(MigrationState *ms,
                                                 unsigned long offset,
                                                 const char *name);

/*
 * Called by the bitmap code for each chunk to discard.
 * May send a discard message, may just leave it queued to
 * be sent later.
 * @start,@length: a range of pages in the migration bitmap in the
 *  RAM block passed to postcopy_discard_send_init() (length=1 is one page)
 */
void postcopy_discard_send_range(MigrationState *ms, PostcopyDiscardState *pds,
                                 unsigned long start, unsigned long length);

/*
 * Called at the end of each RAMBlock by the bitmap code.
 * Sends any outstanding discard messages, frees the PDS.
 */
void postcopy_discard_send_finish(MigrationState *ms,
                                  PostcopyDiscardState *pds);

#endif
