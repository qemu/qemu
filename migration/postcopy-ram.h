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
 * Make all of RAM sensitive to accesses to areas that haven't yet been written
 * and wire up anything necessary to deal with it.
 */
int postcopy_ram_enable_notify(MigrationIncomingState *mis);

/*
 * Initialise postcopy-ram, setting the RAM to a state where we can go into
 * postcopy later; must be called prior to any precopy.
 * called from ram.c's similarly named ram_postcopy_incoming_init
 */
int postcopy_ram_incoming_init(MigrationIncomingState *mis, size_t ram_pages);

/*
 * At the end of a migration where postcopy_ram_incoming_init was called.
 */
int postcopy_ram_incoming_cleanup(MigrationIncomingState *mis);

/*
 * Userfault requires us to mark RAM as NOHUGEPAGE prior to discard
 * however leaving it until after precopy means that most of the precopy
 * data is still THPd
 */
int postcopy_ram_prepare_discard(MigrationIncomingState *mis);

/*
 * Called at the start of each RAMBlock by the bitmap code.
 * Returns a new PDS
 */
PostcopyDiscardState *postcopy_discard_send_init(MigrationState *ms,
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

/*
 * Place a page (from) at (host) efficiently
 *    There are restrictions on how 'from' must be mapped, in general best
 *    to use other postcopy_ routines to allocate.
 * returns 0 on success
 */
int postcopy_place_page(MigrationIncomingState *mis, void *host, void *from,
                        size_t pagesize);

/*
 * Place a zero page at (host) atomically
 * returns 0 on success
 */
int postcopy_place_page_zero(MigrationIncomingState *mis, void *host,
                             size_t pagesize);

/* The current postcopy state is read/set by postcopy_state_get/set
 * which update it atomically.
 * The state is updated as postcopy messages are received, and
 * in general only one thread should be writing to the state at any one
 * time, initially the main thread and then the listen thread;
 * Corner cases are where either thread finishes early and/or errors.
 * The state is checked as messages are received to ensure that
 * the source is sending us messages in the correct order.
 * The state is also used by the RAM reception code to know if it
 * has to place pages atomically, and the cleanup code at the end of
 * the main thread to know if it has to delay cleanup until the end
 * of postcopy.
 */
typedef enum {
    POSTCOPY_INCOMING_NONE = 0,  /* Initial state - no postcopy */
    POSTCOPY_INCOMING_ADVISE,
    POSTCOPY_INCOMING_DISCARD,
    POSTCOPY_INCOMING_LISTENING,
    POSTCOPY_INCOMING_RUNNING,
    POSTCOPY_INCOMING_END
} PostcopyState;

/*
 * Allocate a page of memory that can be mapped at a later point in time
 * using postcopy_place_page
 * Returns: Pointer to allocated page
 */
void *postcopy_get_tmp_page(MigrationIncomingState *mis);

PostcopyState postcopy_state_get(void);
/* Set the state and return the old state */
PostcopyState postcopy_state_set(PostcopyState new_state);

#endif
