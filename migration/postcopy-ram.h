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
bool postcopy_ram_supported_by_host(MigrationIncomingState *mis,
                                    Error **errp);

/*
 * Make all of RAM sensitive to accesses to areas that haven't yet been written
 * and wire up anything necessary to deal with it.
 */
int postcopy_ram_incoming_setup(MigrationIncomingState *mis);

/*
 * Initialise postcopy-ram, setting the RAM to a state where we can go into
 * postcopy later; must be called prior to any precopy.
 * called from ram.c's similarly named ram_postcopy_incoming_init
 */
int postcopy_ram_incoming_init(MigrationIncomingState *mis);

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
 */
void postcopy_discard_send_init(MigrationState *ms, const char *name);

/*
 * Called by the bitmap code for each chunk to discard.
 * May send a discard message, may just leave it queued to
 * be sent later.
 * @start,@length: a range of pages in the migration bitmap in the
 *  RAM block passed to postcopy_discard_send_init() (length=1 is one page)
 */
void postcopy_discard_send_range(MigrationState *ms, unsigned long start,
                                 unsigned long length);

/*
 * Called at the end of each RAMBlock by the bitmap code.
 * Sends any outstanding discard messages.
 */
void postcopy_discard_send_finish(MigrationState *ms);

/*
 * Place a page (from) at (host) efficiently
 *    There are restrictions on how 'from' must be mapped, in general best
 *    to use other postcopy_ routines to allocate.
 * returns 0 on success
 */
int postcopy_place_page(MigrationIncomingState *mis, void *host, void *from,
                        RAMBlock *rb);

/*
 * Place a zero page at (host) atomically
 * returns 0 on success
 */
int postcopy_place_page_zero(MigrationIncomingState *mis, void *host,
                             RAMBlock *rb);

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

PostcopyState postcopy_state_get(void);
/* Set the state and return the old state */
PostcopyState postcopy_state_set(PostcopyState new_state);

void postcopy_fault_thread_notify(MigrationIncomingState *mis);

/*
 * To be called once at the start before any device initialisation
 */
void postcopy_infrastructure_init(void);

/* Add a notifier to a list to be called when checking whether the devices
 * can support postcopy.
 * It's data is a *PostcopyNotifyData
 * It should return 0 if OK, or a negative value on failure.
 * On failure it must set the data->errp to an error.
 *
 */
enum PostcopyNotifyReason {
    POSTCOPY_NOTIFY_PROBE = 0,
    POSTCOPY_NOTIFY_INBOUND_ADVISE,
    POSTCOPY_NOTIFY_INBOUND_LISTEN,
    POSTCOPY_NOTIFY_INBOUND_END,
};

struct PostcopyNotifyData {
    enum PostcopyNotifyReason reason;
    Error **errp;
};

void postcopy_add_notifier(NotifierWithReturn *nn);
void postcopy_remove_notifier(NotifierWithReturn *n);
/* Call the notifier list set by postcopy_add_start_notifier */
int postcopy_notify(enum PostcopyNotifyReason reason, Error **errp);

void postcopy_thread_create(MigrationIncomingState *mis,
                            QemuThread *thread, const char *name,
                            void *(*fn)(void *), int joinable);

struct PostCopyFD;

/* ufd is a pointer to the struct uffd_msg *TODO: more Portable! */
typedef int (*pcfdhandler)(struct PostCopyFD *pcfd, void *ufd);
/* Notification to wake, either on place or on reception of
 * a fault on something that's already arrived (race)
 */
typedef int (*pcfdwake)(struct PostCopyFD *pcfd, RAMBlock *rb, uint64_t offset);

struct PostCopyFD {
    int fd;
    /* Data to pass to handler */
    void *data;
    /* Handler to be called whenever we get a poll event */
    pcfdhandler handler;
    /* Notification to wake shared client */
    pcfdwake waker;
    /* A string to use in error messages */
    const char *idstr;
};

/* Register a userfaultfd owned by an external process for
 * shared memory.
 */
void postcopy_register_shared_ufd(struct PostCopyFD *pcfd);
void postcopy_unregister_shared_ufd(struct PostCopyFD *pcfd);
/* Call each of the shared 'waker's registered telling them of
 * availability of a block.
 */
int postcopy_notify_shared_wake(RAMBlock *rb, uint64_t offset);
/* postcopy_wake_shared: Notify a client ufd that a page is available
 *
 * Returns 0 on success
 *
 * @pcfd: Structure with fd, handler and name as above
 * @client_addr: Address in the client program, not QEMU
 * @rb: The RAMBlock the page is in
 */
int postcopy_wake_shared(struct PostCopyFD *pcfd, uint64_t client_addr,
                         RAMBlock *rb);
/* Callback from shared fault handlers to ask for a page */
int postcopy_request_shared_page(struct PostCopyFD *pcfd, RAMBlock *rb,
                                 uint64_t client_addr, uint64_t offset);

/* Hard-code channels for now for postcopy preemption */
enum PostcopyChannels {
    RAM_CHANNEL_PRECOPY = 0,
    RAM_CHANNEL_POSTCOPY = 1,
    RAM_CHANNEL_MAX,
};

void postcopy_preempt_new_channel(MigrationIncomingState *mis, QEMUFile *file);
void postcopy_preempt_setup(MigrationState *s);
int postcopy_preempt_establish_channel(MigrationState *s);

#endif
