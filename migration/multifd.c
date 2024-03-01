/*
 * Multifd common code
 *
 * Copyright (c) 2019-2020 Red Hat Inc
 *
 * Authors:
 *  Juan Quintela <quintela@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/rcu.h"
#include "exec/target_page.h"
#include "sysemu/sysemu.h"
#include "exec/ramblock.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "fd.h"
#include "file.h"
#include "migration.h"
#include "migration-stats.h"
#include "socket.h"
#include "tls.h"
#include "qemu-file.h"
#include "trace.h"
#include "multifd.h"
#include "threadinfo.h"
#include "options.h"
#include "qemu/yank.h"
#include "io/channel-file.h"
#include "io/channel-socket.h"
#include "yank_functions.h"

/* Multiple fd's */

#define MULTIFD_MAGIC 0x11223344U
#define MULTIFD_VERSION 1

typedef struct {
    uint32_t magic;
    uint32_t version;
    unsigned char uuid[16]; /* QemuUUID */
    uint8_t id;
    uint8_t unused1[7];     /* Reserved for future use */
    uint64_t unused2[4];    /* Reserved for future use */
} __attribute__((packed)) MultiFDInit_t;

struct {
    MultiFDSendParams *params;
    /* array of pages to sent */
    MultiFDPages_t *pages;
    /*
     * Global number of generated multifd packets.
     *
     * Note that we used 'uintptr_t' because it'll naturally support atomic
     * operations on both 32bit / 64 bits hosts.  It means on 32bit systems
     * multifd will overflow the packet_num easier, but that should be
     * fine.
     *
     * Another option is to use QEMU's Stat64 then it'll be 64 bits on all
     * hosts, however so far it does not support atomic fetch_add() yet.
     * Make it easy for now.
     */
    uintptr_t packet_num;
    /*
     * Synchronization point past which no more channels will be
     * created.
     */
    QemuSemaphore channels_created;
    /* send channels ready */
    QemuSemaphore channels_ready;
    /*
     * Have we already run terminate threads.  There is a race when it
     * happens that we got one error while we are exiting.
     * We will use atomic operations.  Only valid values are 0 and 1.
     */
    int exiting;
    /* multifd ops */
    MultiFDMethods *ops;
} *multifd_send_state;

struct {
    MultiFDRecvParams *params;
    MultiFDRecvData *data;
    /* number of created threads */
    int count;
    /*
     * This is always posted by the recv threads, the migration thread
     * uses it to wait for recv threads to finish assigned tasks.
     */
    QemuSemaphore sem_sync;
    /* global number of generated multifd packets */
    uint64_t packet_num;
    int exiting;
    /* multifd ops */
    MultiFDMethods *ops;
} *multifd_recv_state;

static bool multifd_use_packets(void)
{
    return !migrate_mapped_ram();
}

void multifd_send_channel_created(void)
{
    qemu_sem_post(&multifd_send_state->channels_created);
}

static void multifd_set_file_bitmap(MultiFDSendParams *p)
{
    MultiFDPages_t *pages = p->pages;

    assert(pages->block);

    for (int i = 0; i < p->pages->num; i++) {
        ramblock_set_file_bmap_atomic(pages->block, pages->offset[i]);
    }
}

/* Multifd without compression */

/**
 * nocomp_send_setup: setup send side
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static int nocomp_send_setup(MultiFDSendParams *p, Error **errp)
{
    if (migrate_zero_copy_send()) {
        p->write_flags |= QIO_CHANNEL_WRITE_FLAG_ZERO_COPY;
    }

    return 0;
}

/**
 * nocomp_send_cleanup: cleanup send side
 *
 * For no compression this function does nothing.
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static void nocomp_send_cleanup(MultiFDSendParams *p, Error **errp)
{
    return;
}

static void multifd_send_prepare_iovs(MultiFDSendParams *p)
{
    MultiFDPages_t *pages = p->pages;

    for (int i = 0; i < pages->num; i++) {
        p->iov[p->iovs_num].iov_base = pages->block->host + pages->offset[i];
        p->iov[p->iovs_num].iov_len = p->page_size;
        p->iovs_num++;
    }

    p->next_packet_size = pages->num * p->page_size;
}

/**
 * nocomp_send_prepare: prepare date to be able to send
 *
 * For no compression we just have to calculate the size of the
 * packet.
 *
 * Returns 0 for success or -1 for error
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static int nocomp_send_prepare(MultiFDSendParams *p, Error **errp)
{
    bool use_zero_copy_send = migrate_zero_copy_send();
    int ret;

    if (!multifd_use_packets()) {
        multifd_send_prepare_iovs(p);
        multifd_set_file_bitmap(p);

        return 0;
    }

    if (!use_zero_copy_send) {
        /*
         * Only !zerocopy needs the header in IOV; zerocopy will
         * send it separately.
         */
        multifd_send_prepare_header(p);
    }

    multifd_send_prepare_iovs(p);
    p->flags |= MULTIFD_FLAG_NOCOMP;

    multifd_send_fill_packet(p);

    if (use_zero_copy_send) {
        /* Send header first, without zerocopy */
        ret = qio_channel_write_all(p->c, (void *)p->packet,
                                    p->packet_len, errp);
        if (ret != 0) {
            return -1;
        }
    }

    return 0;
}

/**
 * nocomp_recv_setup: setup receive side
 *
 * For no compression this function does nothing.
 *
 * Returns 0 for success or -1 for error
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static int nocomp_recv_setup(MultiFDRecvParams *p, Error **errp)
{
    return 0;
}

/**
 * nocomp_recv_cleanup: setup receive side
 *
 * For no compression this function does nothing.
 *
 * @p: Params for the channel that we are using
 */
static void nocomp_recv_cleanup(MultiFDRecvParams *p)
{
}

/**
 * nocomp_recv: read the data from the channel
 *
 * For no compression we just need to read things into the correct place.
 *
 * Returns 0 for success or -1 for error
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static int nocomp_recv(MultiFDRecvParams *p, Error **errp)
{
    uint32_t flags;

    if (!multifd_use_packets()) {
        return multifd_file_recv_data(p, errp);
    }

    flags = p->flags & MULTIFD_FLAG_COMPRESSION_MASK;

    if (flags != MULTIFD_FLAG_NOCOMP) {
        error_setg(errp, "multifd %u: flags received %x flags expected %x",
                   p->id, flags, MULTIFD_FLAG_NOCOMP);
        return -1;
    }
    for (int i = 0; i < p->normal_num; i++) {
        p->iov[i].iov_base = p->host + p->normal[i];
        p->iov[i].iov_len = p->page_size;
    }
    return qio_channel_readv_all(p->c, p->iov, p->normal_num, errp);
}

static MultiFDMethods multifd_nocomp_ops = {
    .send_setup = nocomp_send_setup,
    .send_cleanup = nocomp_send_cleanup,
    .send_prepare = nocomp_send_prepare,
    .recv_setup = nocomp_recv_setup,
    .recv_cleanup = nocomp_recv_cleanup,
    .recv = nocomp_recv
};

static MultiFDMethods *multifd_ops[MULTIFD_COMPRESSION__MAX] = {
    [MULTIFD_COMPRESSION_NONE] = &multifd_nocomp_ops,
};

void multifd_register_ops(int method, MultiFDMethods *ops)
{
    assert(0 < method && method < MULTIFD_COMPRESSION__MAX);
    multifd_ops[method] = ops;
}

/* Reset a MultiFDPages_t* object for the next use */
static void multifd_pages_reset(MultiFDPages_t *pages)
{
    /*
     * We don't need to touch offset[] array, because it will be
     * overwritten later when reused.
     */
    pages->num = 0;
    pages->block = NULL;
}

static int multifd_send_initial_packet(MultiFDSendParams *p, Error **errp)
{
    MultiFDInit_t msg = {};
    size_t size = sizeof(msg);
    int ret;

    msg.magic = cpu_to_be32(MULTIFD_MAGIC);
    msg.version = cpu_to_be32(MULTIFD_VERSION);
    msg.id = p->id;
    memcpy(msg.uuid, &qemu_uuid.data, sizeof(msg.uuid));

    ret = qio_channel_write_all(p->c, (char *)&msg, size, errp);
    if (ret != 0) {
        return -1;
    }
    stat64_add(&mig_stats.multifd_bytes, size);
    return 0;
}

static int multifd_recv_initial_packet(QIOChannel *c, Error **errp)
{
    MultiFDInit_t msg;
    int ret;

    ret = qio_channel_read_all(c, (char *)&msg, sizeof(msg), errp);
    if (ret != 0) {
        return -1;
    }

    msg.magic = be32_to_cpu(msg.magic);
    msg.version = be32_to_cpu(msg.version);

    if (msg.magic != MULTIFD_MAGIC) {
        error_setg(errp, "multifd: received packet magic %x "
                   "expected %x", msg.magic, MULTIFD_MAGIC);
        return -1;
    }

    if (msg.version != MULTIFD_VERSION) {
        error_setg(errp, "multifd: received packet version %u "
                   "expected %u", msg.version, MULTIFD_VERSION);
        return -1;
    }

    if (memcmp(msg.uuid, &qemu_uuid, sizeof(qemu_uuid))) {
        char *uuid = qemu_uuid_unparse_strdup(&qemu_uuid);
        char *msg_uuid = qemu_uuid_unparse_strdup((const QemuUUID *)msg.uuid);

        error_setg(errp, "multifd: received uuid '%s' and expected "
                   "uuid '%s' for channel %hhd", msg_uuid, uuid, msg.id);
        g_free(uuid);
        g_free(msg_uuid);
        return -1;
    }

    if (msg.id > migrate_multifd_channels()) {
        error_setg(errp, "multifd: received channel id %u is greater than "
                   "number of channels %u", msg.id, migrate_multifd_channels());
        return -1;
    }

    return msg.id;
}

static MultiFDPages_t *multifd_pages_init(uint32_t n)
{
    MultiFDPages_t *pages = g_new0(MultiFDPages_t, 1);

    pages->allocated = n;
    pages->offset = g_new0(ram_addr_t, n);

    return pages;
}

static void multifd_pages_clear(MultiFDPages_t *pages)
{
    multifd_pages_reset(pages);
    pages->allocated = 0;
    g_free(pages->offset);
    pages->offset = NULL;
    g_free(pages);
}

void multifd_send_fill_packet(MultiFDSendParams *p)
{
    MultiFDPacket_t *packet = p->packet;
    MultiFDPages_t *pages = p->pages;
    uint64_t packet_num;
    int i;

    packet->flags = cpu_to_be32(p->flags);
    packet->pages_alloc = cpu_to_be32(p->pages->allocated);
    packet->normal_pages = cpu_to_be32(pages->num);
    packet->next_packet_size = cpu_to_be32(p->next_packet_size);

    packet_num = qatomic_fetch_inc(&multifd_send_state->packet_num);
    packet->packet_num = cpu_to_be64(packet_num);

    if (pages->block) {
        strncpy(packet->ramblock, pages->block->idstr, 256);
    }

    for (i = 0; i < pages->num; i++) {
        /* there are architectures where ram_addr_t is 32 bit */
        uint64_t temp = pages->offset[i];

        packet->offset[i] = cpu_to_be64(temp);
    }

    p->packets_sent++;
    p->total_normal_pages += pages->num;

    trace_multifd_send(p->id, packet_num, pages->num, p->flags,
                       p->next_packet_size);
}

static int multifd_recv_unfill_packet(MultiFDRecvParams *p, Error **errp)
{
    MultiFDPacket_t *packet = p->packet;
    int i;

    packet->magic = be32_to_cpu(packet->magic);
    if (packet->magic != MULTIFD_MAGIC) {
        error_setg(errp, "multifd: received packet "
                   "magic %x and expected magic %x",
                   packet->magic, MULTIFD_MAGIC);
        return -1;
    }

    packet->version = be32_to_cpu(packet->version);
    if (packet->version != MULTIFD_VERSION) {
        error_setg(errp, "multifd: received packet "
                   "version %u and expected version %u",
                   packet->version, MULTIFD_VERSION);
        return -1;
    }

    p->flags = be32_to_cpu(packet->flags);

    packet->pages_alloc = be32_to_cpu(packet->pages_alloc);
    /*
     * If we received a packet that is 100 times bigger than expected
     * just stop migration.  It is a magic number.
     */
    if (packet->pages_alloc > p->page_count) {
        error_setg(errp, "multifd: received packet "
                   "with size %u and expected a size of %u",
                   packet->pages_alloc, p->page_count) ;
        return -1;
    }

    p->normal_num = be32_to_cpu(packet->normal_pages);
    if (p->normal_num > packet->pages_alloc) {
        error_setg(errp, "multifd: received packet "
                   "with %u pages and expected maximum pages are %u",
                   p->normal_num, packet->pages_alloc) ;
        return -1;
    }

    p->next_packet_size = be32_to_cpu(packet->next_packet_size);
    p->packet_num = be64_to_cpu(packet->packet_num);
    p->packets_recved++;
    p->total_normal_pages += p->normal_num;

    trace_multifd_recv(p->id, p->packet_num, p->normal_num, p->flags,
                       p->next_packet_size);

    if (p->normal_num == 0) {
        return 0;
    }

    /* make sure that ramblock is 0 terminated */
    packet->ramblock[255] = 0;
    p->block = qemu_ram_block_by_name(packet->ramblock);
    if (!p->block) {
        error_setg(errp, "multifd: unknown ram block %s",
                   packet->ramblock);
        return -1;
    }

    p->host = p->block->host;
    for (i = 0; i < p->normal_num; i++) {
        uint64_t offset = be64_to_cpu(packet->offset[i]);

        if (offset > (p->block->used_length - p->page_size)) {
            error_setg(errp, "multifd: offset too long %" PRIu64
                       " (max " RAM_ADDR_FMT ")",
                       offset, p->block->used_length);
            return -1;
        }
        p->normal[i] = offset;
    }

    return 0;
}

static bool multifd_send_should_exit(void)
{
    return qatomic_read(&multifd_send_state->exiting);
}

static bool multifd_recv_should_exit(void)
{
    return qatomic_read(&multifd_recv_state->exiting);
}

/*
 * The migration thread can wait on either of the two semaphores.  This
 * function can be used to kick the main thread out of waiting on either of
 * them.  Should mostly only be called when something wrong happened with
 * the current multifd send thread.
 */
static void multifd_send_kick_main(MultiFDSendParams *p)
{
    qemu_sem_post(&p->sem_sync);
    qemu_sem_post(&multifd_send_state->channels_ready);
}

/*
 * How we use multifd_send_state->pages and channel->pages?
 *
 * We create a pages for each channel, and a main one.  Each time that
 * we need to send a batch of pages we interchange the ones between
 * multifd_send_state and the channel that is sending it.  There are
 * two reasons for that:
 *    - to not have to do so many mallocs during migration
 *    - to make easier to know what to free at the end of migration
 *
 * This way we always know who is the owner of each "pages" struct,
 * and we don't need any locking.  It belongs to the migration thread
 * or to the channel thread.  Switching is safe because the migration
 * thread is using the channel mutex when changing it, and the channel
 * have to had finish with its own, otherwise pending_job can't be
 * false.
 *
 * Returns true if succeed, false otherwise.
 */
static bool multifd_send_pages(void)
{
    int i;
    static int next_channel;
    MultiFDSendParams *p = NULL; /* make happy gcc */
    MultiFDPages_t *pages = multifd_send_state->pages;

    if (multifd_send_should_exit()) {
        return false;
    }

    /* We wait here, until at least one channel is ready */
    qemu_sem_wait(&multifd_send_state->channels_ready);

    /*
     * next_channel can remain from a previous migration that was
     * using more channels, so ensure it doesn't overflow if the
     * limit is lower now.
     */
    next_channel %= migrate_multifd_channels();
    for (i = next_channel;; i = (i + 1) % migrate_multifd_channels()) {
        if (multifd_send_should_exit()) {
            return false;
        }
        p = &multifd_send_state->params[i];
        /*
         * Lockless read to p->pending_job is safe, because only multifd
         * sender thread can clear it.
         */
        if (qatomic_read(&p->pending_job) == false) {
            next_channel = (i + 1) % migrate_multifd_channels();
            break;
        }
    }

    /*
     * Make sure we read p->pending_job before all the rest.  Pairs with
     * qatomic_store_release() in multifd_send_thread().
     */
    smp_mb_acquire();
    assert(!p->pages->num);
    multifd_send_state->pages = p->pages;
    p->pages = pages;
    /*
     * Making sure p->pages is setup before marking pending_job=true. Pairs
     * with the qatomic_load_acquire() in multifd_send_thread().
     */
    qatomic_store_release(&p->pending_job, true);
    qemu_sem_post(&p->sem);

    return true;
}

static inline bool multifd_queue_empty(MultiFDPages_t *pages)
{
    return pages->num == 0;
}

static inline bool multifd_queue_full(MultiFDPages_t *pages)
{
    return pages->num == pages->allocated;
}

static inline void multifd_enqueue(MultiFDPages_t *pages, ram_addr_t offset)
{
    pages->offset[pages->num++] = offset;
}

/* Returns true if enqueue successful, false otherwise */
bool multifd_queue_page(RAMBlock *block, ram_addr_t offset)
{
    MultiFDPages_t *pages;

retry:
    pages = multifd_send_state->pages;

    /* If the queue is empty, we can already enqueue now */
    if (multifd_queue_empty(pages)) {
        pages->block = block;
        multifd_enqueue(pages, offset);
        return true;
    }

    /*
     * Not empty, meanwhile we need a flush.  It can because of either:
     *
     * (1) The page is not on the same ramblock of previous ones, or,
     * (2) The queue is full.
     *
     * After flush, always retry.
     */
    if (pages->block != block || multifd_queue_full(pages)) {
        if (!multifd_send_pages()) {
            return false;
        }
        goto retry;
    }

    /* Not empty, and we still have space, do it! */
    multifd_enqueue(pages, offset);
    return true;
}

/* Multifd send side hit an error; remember it and prepare to quit */
static void multifd_send_set_error(Error *err)
{
    /*
     * We don't want to exit each threads twice.  Depending on where
     * we get the error, or if there are two independent errors in two
     * threads at the same time, we can end calling this function
     * twice.
     */
    if (qatomic_xchg(&multifd_send_state->exiting, 1)) {
        return;
    }

    if (err) {
        MigrationState *s = migrate_get_current();
        migrate_set_error(s, err);
        if (s->state == MIGRATION_STATUS_SETUP ||
            s->state == MIGRATION_STATUS_PRE_SWITCHOVER ||
            s->state == MIGRATION_STATUS_DEVICE ||
            s->state == MIGRATION_STATUS_ACTIVE) {
            migrate_set_state(&s->state, s->state,
                              MIGRATION_STATUS_FAILED);
        }
    }
}

static void multifd_send_terminate_threads(void)
{
    int i;

    trace_multifd_send_terminate_threads();

    /*
     * Tell everyone we're quitting.  No xchg() needed here; we simply
     * always set it.
     */
    qatomic_set(&multifd_send_state->exiting, 1);

    /*
     * Firstly, kick all threads out; no matter whether they are just idle,
     * or blocked in an IO system call.
     */
    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDSendParams *p = &multifd_send_state->params[i];

        qemu_sem_post(&p->sem);
        if (p->c) {
            qio_channel_shutdown(p->c, QIO_CHANNEL_SHUTDOWN_BOTH, NULL);
        }
    }

    /*
     * Finally recycle all the threads.
     */
    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDSendParams *p = &multifd_send_state->params[i];

        if (p->tls_thread_created) {
            qemu_thread_join(&p->tls_thread);
        }

        if (p->thread_created) {
            qemu_thread_join(&p->thread);
        }
    }
}

static bool multifd_send_cleanup_channel(MultiFDSendParams *p, Error **errp)
{
    if (p->c) {
        migration_ioc_unregister_yank(p->c);
        /*
         * An explicit close() on the channel here is normally not
         * required, but can be helpful for "file:" iochannels, where it
         * will include fdatasync() to make sure the data is flushed to the
         * disk backend.
         *
         * The object_unref() cannot guarantee that because: (1) finalize()
         * of the iochannel is only triggered on the last reference, and
         * it's not guaranteed that we always hold the last refcount when
         * reaching here, and, (2) even if finalize() is invoked, it only
         * does a close(fd) without data flush.
         */
        qio_channel_close(p->c, &error_abort);
        object_unref(OBJECT(p->c));
        p->c = NULL;
    }
    qemu_sem_destroy(&p->sem);
    qemu_sem_destroy(&p->sem_sync);
    g_free(p->name);
    p->name = NULL;
    multifd_pages_clear(p->pages);
    p->pages = NULL;
    p->packet_len = 0;
    g_free(p->packet);
    p->packet = NULL;
    g_free(p->iov);
    p->iov = NULL;
    multifd_send_state->ops->send_cleanup(p, errp);

    return *errp == NULL;
}

static void multifd_send_cleanup_state(void)
{
    file_cleanup_outgoing_migration();
    fd_cleanup_outgoing_migration();
    socket_cleanup_outgoing_migration();
    qemu_sem_destroy(&multifd_send_state->channels_created);
    qemu_sem_destroy(&multifd_send_state->channels_ready);
    g_free(multifd_send_state->params);
    multifd_send_state->params = NULL;
    multifd_pages_clear(multifd_send_state->pages);
    multifd_send_state->pages = NULL;
    g_free(multifd_send_state);
    multifd_send_state = NULL;
}

void multifd_send_shutdown(void)
{
    int i;

    if (!migrate_multifd()) {
        return;
    }

    multifd_send_terminate_threads();

    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDSendParams *p = &multifd_send_state->params[i];
        Error *local_err = NULL;

        if (!multifd_send_cleanup_channel(p, &local_err)) {
            migrate_set_error(migrate_get_current(), local_err);
            error_free(local_err);
        }
    }

    multifd_send_cleanup_state();
}

static int multifd_zero_copy_flush(QIOChannel *c)
{
    int ret;
    Error *err = NULL;

    ret = qio_channel_flush(c, &err);
    if (ret < 0) {
        error_report_err(err);
        return -1;
    }
    if (ret == 1) {
        stat64_add(&mig_stats.dirty_sync_missed_zero_copy, 1);
    }

    return ret;
}

int multifd_send_sync_main(void)
{
    int i;
    bool flush_zero_copy;

    if (!migrate_multifd()) {
        return 0;
    }
    if (multifd_send_state->pages->num) {
        if (!multifd_send_pages()) {
            error_report("%s: multifd_send_pages fail", __func__);
            return -1;
        }
    }

    flush_zero_copy = migrate_zero_copy_send();

    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDSendParams *p = &multifd_send_state->params[i];

        if (multifd_send_should_exit()) {
            return -1;
        }

        trace_multifd_send_sync_main_signal(p->id);

        /*
         * We should be the only user so far, so not possible to be set by
         * others concurrently.
         */
        assert(qatomic_read(&p->pending_sync) == false);
        qatomic_set(&p->pending_sync, true);
        qemu_sem_post(&p->sem);
    }
    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDSendParams *p = &multifd_send_state->params[i];

        if (multifd_send_should_exit()) {
            return -1;
        }

        qemu_sem_wait(&multifd_send_state->channels_ready);
        trace_multifd_send_sync_main_wait(p->id);
        qemu_sem_wait(&p->sem_sync);

        if (flush_zero_copy && p->c && (multifd_zero_copy_flush(p->c) < 0)) {
            return -1;
        }
    }
    trace_multifd_send_sync_main(multifd_send_state->packet_num);

    return 0;
}

static void *multifd_send_thread(void *opaque)
{
    MultiFDSendParams *p = opaque;
    MigrationThread *thread = NULL;
    Error *local_err = NULL;
    int ret = 0;
    bool use_packets = multifd_use_packets();

    thread = migration_threads_add(p->name, qemu_get_thread_id());

    trace_multifd_send_thread_start(p->id);
    rcu_register_thread();

    if (use_packets) {
        if (multifd_send_initial_packet(p, &local_err) < 0) {
            ret = -1;
            goto out;
        }
    }

    while (true) {
        qemu_sem_post(&multifd_send_state->channels_ready);
        qemu_sem_wait(&p->sem);

        if (multifd_send_should_exit()) {
            break;
        }

        /*
         * Read pending_job flag before p->pages.  Pairs with the
         * qatomic_store_release() in multifd_send_pages().
         */
        if (qatomic_load_acquire(&p->pending_job)) {
            MultiFDPages_t *pages = p->pages;

            p->iovs_num = 0;
            assert(pages->num);

            ret = multifd_send_state->ops->send_prepare(p, &local_err);
            if (ret != 0) {
                break;
            }

            if (migrate_mapped_ram()) {
                ret = file_write_ramblock_iov(p->c, p->iov, p->iovs_num,
                                              p->pages->block, &local_err);
            } else {
                ret = qio_channel_writev_full_all(p->c, p->iov, p->iovs_num,
                                                  NULL, 0, p->write_flags,
                                                  &local_err);
            }

            if (ret != 0) {
                break;
            }

            stat64_add(&mig_stats.multifd_bytes,
                       p->next_packet_size + p->packet_len);

            multifd_pages_reset(p->pages);
            p->next_packet_size = 0;

            /*
             * Making sure p->pages is published before saying "we're
             * free".  Pairs with the smp_mb_acquire() in
             * multifd_send_pages().
             */
            qatomic_store_release(&p->pending_job, false);
        } else {
            /*
             * If not a normal job, must be a sync request.  Note that
             * pending_sync is a standalone flag (unlike pending_job), so
             * it doesn't require explicit memory barriers.
             */
            assert(qatomic_read(&p->pending_sync));

            if (use_packets) {
                p->flags = MULTIFD_FLAG_SYNC;
                multifd_send_fill_packet(p);
                ret = qio_channel_write_all(p->c, (void *)p->packet,
                                            p->packet_len, &local_err);
                if (ret != 0) {
                    break;
                }
                /* p->next_packet_size will always be zero for a SYNC packet */
                stat64_add(&mig_stats.multifd_bytes, p->packet_len);
                p->flags = 0;
            }

            qatomic_set(&p->pending_sync, false);
            qemu_sem_post(&p->sem_sync);
        }
    }

out:
    if (ret) {
        assert(local_err);
        trace_multifd_send_error(p->id);
        multifd_send_set_error(local_err);
        multifd_send_kick_main(p);
        error_free(local_err);
    }

    rcu_unregister_thread();
    migration_threads_remove(thread);
    trace_multifd_send_thread_end(p->id, p->packets_sent, p->total_normal_pages);

    return NULL;
}

static void multifd_new_send_channel_async(QIOTask *task, gpointer opaque);

typedef struct {
    MultiFDSendParams *p;
    QIOChannelTLS *tioc;
} MultiFDTLSThreadArgs;

static void *multifd_tls_handshake_thread(void *opaque)
{
    MultiFDTLSThreadArgs *args = opaque;

    qio_channel_tls_handshake(args->tioc,
                              multifd_new_send_channel_async,
                              args->p,
                              NULL,
                              NULL);
    g_free(args);

    return NULL;
}

static bool multifd_tls_channel_connect(MultiFDSendParams *p,
                                        QIOChannel *ioc,
                                        Error **errp)
{
    MigrationState *s = migrate_get_current();
    const char *hostname = s->hostname;
    MultiFDTLSThreadArgs *args;
    QIOChannelTLS *tioc;

    tioc = migration_tls_client_create(ioc, hostname, errp);
    if (!tioc) {
        return false;
    }

    /*
     * Ownership of the socket channel now transfers to the newly
     * created TLS channel, which has already taken a reference.
     */
    object_unref(OBJECT(ioc));
    trace_multifd_tls_outgoing_handshake_start(ioc, tioc, hostname);
    qio_channel_set_name(QIO_CHANNEL(tioc), "multifd-tls-outgoing");

    args = g_new0(MultiFDTLSThreadArgs, 1);
    args->tioc = tioc;
    args->p = p;

    p->tls_thread_created = true;
    qemu_thread_create(&p->tls_thread, "multifd-tls-handshake-worker",
                       multifd_tls_handshake_thread, args,
                       QEMU_THREAD_JOINABLE);
    return true;
}

void multifd_channel_connect(MultiFDSendParams *p, QIOChannel *ioc)
{
    qio_channel_set_delay(ioc, false);

    migration_ioc_register_yank(ioc);
    /* Setup p->c only if the channel is completely setup */
    p->c = ioc;

    p->thread_created = true;
    qemu_thread_create(&p->thread, p->name, multifd_send_thread, p,
                       QEMU_THREAD_JOINABLE);
}

/*
 * When TLS is enabled this function is called once to establish the
 * TLS connection and a second time after the TLS handshake to create
 * the multifd channel. Without TLS it goes straight into the channel
 * creation.
 */
static void multifd_new_send_channel_async(QIOTask *task, gpointer opaque)
{
    MultiFDSendParams *p = opaque;
    QIOChannel *ioc = QIO_CHANNEL(qio_task_get_source(task));
    Error *local_err = NULL;
    bool ret;

    trace_multifd_new_send_channel_async(p->id);

    if (qio_task_propagate_error(task, &local_err)) {
        ret = false;
        goto out;
    }

    trace_multifd_set_outgoing_channel(ioc, object_get_typename(OBJECT(ioc)),
                                       migrate_get_current()->hostname);

    if (migrate_channel_requires_tls_upgrade(ioc)) {
        ret = multifd_tls_channel_connect(p, ioc, &local_err);
        if (ret) {
            return;
        }
    } else {
        multifd_channel_connect(p, ioc);
        ret = true;
    }

out:
    /*
     * Here we're not interested whether creation succeeded, only that
     * it happened at all.
     */
    multifd_send_channel_created();

    if (ret) {
        return;
    }

    trace_multifd_new_send_channel_async_error(p->id, local_err);
    multifd_send_set_error(local_err);
    /*
     * For error cases (TLS or non-TLS), IO channel is always freed here
     * rather than when cleanup multifd: since p->c is not set, multifd
     * cleanup code doesn't even know its existence.
     */
    object_unref(OBJECT(ioc));
    error_free(local_err);
}

static bool multifd_new_send_channel_create(gpointer opaque, Error **errp)
{
    if (!multifd_use_packets()) {
        return file_send_channel_create(opaque, errp);
    }

    socket_send_channel_create(multifd_new_send_channel_async, opaque);
    return true;
}

bool multifd_send_setup(void)
{
    MigrationState *s = migrate_get_current();
    Error *local_err = NULL;
    int thread_count, ret = 0;
    uint32_t page_count = MULTIFD_PACKET_SIZE / qemu_target_page_size();
    bool use_packets = multifd_use_packets();
    uint8_t i;

    if (!migrate_multifd()) {
        return true;
    }

    thread_count = migrate_multifd_channels();
    multifd_send_state = g_malloc0(sizeof(*multifd_send_state));
    multifd_send_state->params = g_new0(MultiFDSendParams, thread_count);
    multifd_send_state->pages = multifd_pages_init(page_count);
    qemu_sem_init(&multifd_send_state->channels_created, 0);
    qemu_sem_init(&multifd_send_state->channels_ready, 0);
    qatomic_set(&multifd_send_state->exiting, 0);
    multifd_send_state->ops = multifd_ops[migrate_multifd_compression()];

    for (i = 0; i < thread_count; i++) {
        MultiFDSendParams *p = &multifd_send_state->params[i];

        qemu_sem_init(&p->sem, 0);
        qemu_sem_init(&p->sem_sync, 0);
        p->id = i;
        p->pages = multifd_pages_init(page_count);

        if (use_packets) {
            p->packet_len = sizeof(MultiFDPacket_t)
                          + sizeof(uint64_t) * page_count;
            p->packet = g_malloc0(p->packet_len);
            p->packet->magic = cpu_to_be32(MULTIFD_MAGIC);
            p->packet->version = cpu_to_be32(MULTIFD_VERSION);

            /* We need one extra place for the packet header */
            p->iov = g_new0(struct iovec, page_count + 1);
        } else {
            p->iov = g_new0(struct iovec, page_count);
        }
        p->name = g_strdup_printf("multifdsend_%d", i);
        p->page_size = qemu_target_page_size();
        p->page_count = page_count;
        p->write_flags = 0;

        if (!multifd_new_send_channel_create(p, &local_err)) {
            return false;
        }
    }

    /*
     * Wait until channel creation has started for all channels. The
     * creation can still fail, but no more channels will be created
     * past this point.
     */
    for (i = 0; i < thread_count; i++) {
        qemu_sem_wait(&multifd_send_state->channels_created);
    }

    for (i = 0; i < thread_count; i++) {
        MultiFDSendParams *p = &multifd_send_state->params[i];

        ret = multifd_send_state->ops->send_setup(p, &local_err);
        if (ret) {
            break;
        }
    }

    if (ret) {
        migrate_set_error(s, local_err);
        error_report_err(local_err);
        migrate_set_state(&s->state, MIGRATION_STATUS_SETUP,
                          MIGRATION_STATUS_FAILED);
        return false;
    }

    return true;
}

bool multifd_recv(void)
{
    int i;
    static int next_recv_channel;
    MultiFDRecvParams *p = NULL;
    MultiFDRecvData *data = multifd_recv_state->data;

    /*
     * next_channel can remain from a previous migration that was
     * using more channels, so ensure it doesn't overflow if the
     * limit is lower now.
     */
    next_recv_channel %= migrate_multifd_channels();
    for (i = next_recv_channel;; i = (i + 1) % migrate_multifd_channels()) {
        if (multifd_recv_should_exit()) {
            return false;
        }

        p = &multifd_recv_state->params[i];

        if (qatomic_read(&p->pending_job) == false) {
            next_recv_channel = (i + 1) % migrate_multifd_channels();
            break;
        }
    }

    /*
     * Order pending_job read before manipulating p->data below. Pairs
     * with qatomic_store_release() at multifd_recv_thread().
     */
    smp_mb_acquire();

    assert(!p->data->size);
    multifd_recv_state->data = p->data;
    p->data = data;

    /*
     * Order p->data update before setting pending_job. Pairs with
     * qatomic_load_acquire() at multifd_recv_thread().
     */
    qatomic_store_release(&p->pending_job, true);
    qemu_sem_post(&p->sem);

    return true;
}

MultiFDRecvData *multifd_get_recv_data(void)
{
    return multifd_recv_state->data;
}

static void multifd_recv_terminate_threads(Error *err)
{
    int i;

    trace_multifd_recv_terminate_threads(err != NULL);

    if (qatomic_xchg(&multifd_recv_state->exiting, 1)) {
        return;
    }

    if (err) {
        MigrationState *s = migrate_get_current();
        migrate_set_error(s, err);
        if (s->state == MIGRATION_STATUS_SETUP ||
            s->state == MIGRATION_STATUS_ACTIVE) {
            migrate_set_state(&s->state, s->state,
                              MIGRATION_STATUS_FAILED);
        }
    }

    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDRecvParams *p = &multifd_recv_state->params[i];

        /*
         * The migration thread and channels interact differently
         * depending on the presence of packets.
         */
        if (multifd_use_packets()) {
            /*
             * The channel receives as long as there are packets. When
             * packets end (i.e. MULTIFD_FLAG_SYNC is reached), the
             * channel waits for the migration thread to sync. If the
             * sync never happens, do it here.
             */
            qemu_sem_post(&p->sem_sync);
        } else {
            /*
             * The channel waits for the migration thread to give it
             * work. When the migration thread runs out of work, it
             * releases the channel and waits for any pending work to
             * finish. If we reach here (e.g. due to error) before the
             * work runs out, release the channel.
             */
            qemu_sem_post(&p->sem);
        }

        /*
         * We could arrive here for two reasons:
         *  - normal quit, i.e. everything went fine, just finished
         *  - error quit: We close the channels so the channel threads
         *    finish the qio_channel_read_all_eof()
         */
        if (p->c) {
            qio_channel_shutdown(p->c, QIO_CHANNEL_SHUTDOWN_BOTH, NULL);
        }
    }
}

void multifd_recv_shutdown(void)
{
    if (migrate_multifd()) {
        multifd_recv_terminate_threads(NULL);
    }
}

static void multifd_recv_cleanup_channel(MultiFDRecvParams *p)
{
    migration_ioc_unregister_yank(p->c);
    object_unref(OBJECT(p->c));
    p->c = NULL;
    qemu_mutex_destroy(&p->mutex);
    qemu_sem_destroy(&p->sem_sync);
    qemu_sem_destroy(&p->sem);
    g_free(p->name);
    p->name = NULL;
    p->packet_len = 0;
    g_free(p->packet);
    p->packet = NULL;
    g_free(p->iov);
    p->iov = NULL;
    g_free(p->normal);
    p->normal = NULL;
    multifd_recv_state->ops->recv_cleanup(p);
}

static void multifd_recv_cleanup_state(void)
{
    qemu_sem_destroy(&multifd_recv_state->sem_sync);
    g_free(multifd_recv_state->params);
    multifd_recv_state->params = NULL;
    g_free(multifd_recv_state->data);
    multifd_recv_state->data = NULL;
    g_free(multifd_recv_state);
    multifd_recv_state = NULL;
}

void multifd_recv_cleanup(void)
{
    int i;

    if (!migrate_multifd()) {
        return;
    }
    multifd_recv_terminate_threads(NULL);
    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDRecvParams *p = &multifd_recv_state->params[i];

        if (p->thread_created) {
            qemu_thread_join(&p->thread);
        }
    }
    for (i = 0; i < migrate_multifd_channels(); i++) {
        multifd_recv_cleanup_channel(&multifd_recv_state->params[i]);
    }
    multifd_recv_cleanup_state();
}

void multifd_recv_sync_main(void)
{
    int thread_count = migrate_multifd_channels();
    bool file_based = !multifd_use_packets();
    int i;

    if (!migrate_multifd()) {
        return;
    }

    /*
     * File-based channels don't use packets and therefore need to
     * wait for more work. Release them to start the sync.
     */
    if (file_based) {
        for (i = 0; i < thread_count; i++) {
            MultiFDRecvParams *p = &multifd_recv_state->params[i];

            trace_multifd_recv_sync_main_signal(p->id);
            qemu_sem_post(&p->sem);
        }
    }

    /*
     * Initiate the synchronization by waiting for all channels.
     *
     * For socket-based migration this means each channel has received
     * the SYNC packet on the stream.
     *
     * For file-based migration this means each channel is done with
     * the work (pending_job=false).
     */
    for (i = 0; i < thread_count; i++) {
        trace_multifd_recv_sync_main_wait(i);
        qemu_sem_wait(&multifd_recv_state->sem_sync);
    }

    if (file_based) {
        /*
         * For file-based loading is done in one iteration. We're
         * done.
         */
        return;
    }

    /*
     * Sync done. Release the channels for the next iteration.
     */
    for (i = 0; i < thread_count; i++) {
        MultiFDRecvParams *p = &multifd_recv_state->params[i];

        WITH_QEMU_LOCK_GUARD(&p->mutex) {
            if (multifd_recv_state->packet_num < p->packet_num) {
                multifd_recv_state->packet_num = p->packet_num;
            }
        }
        trace_multifd_recv_sync_main_signal(p->id);
        qemu_sem_post(&p->sem_sync);
    }
    trace_multifd_recv_sync_main(multifd_recv_state->packet_num);
}

static void *multifd_recv_thread(void *opaque)
{
    MultiFDRecvParams *p = opaque;
    Error *local_err = NULL;
    bool use_packets = multifd_use_packets();
    int ret;

    trace_multifd_recv_thread_start(p->id);
    rcu_register_thread();

    while (true) {
        uint32_t flags = 0;
        bool has_data = false;
        p->normal_num = 0;

        if (use_packets) {
            if (multifd_recv_should_exit()) {
                break;
            }

            ret = qio_channel_read_all_eof(p->c, (void *)p->packet,
                                           p->packet_len, &local_err);
            if (ret == 0 || ret == -1) {   /* 0: EOF  -1: Error */
                break;
            }

            qemu_mutex_lock(&p->mutex);
            ret = multifd_recv_unfill_packet(p, &local_err);
            if (ret) {
                qemu_mutex_unlock(&p->mutex);
                break;
            }

            flags = p->flags;
            /* recv methods don't know how to handle the SYNC flag */
            p->flags &= ~MULTIFD_FLAG_SYNC;
            has_data = !!p->normal_num;
            qemu_mutex_unlock(&p->mutex);
        } else {
            /*
             * No packets, so we need to wait for the vmstate code to
             * give us work.
             */
            qemu_sem_wait(&p->sem);

            if (multifd_recv_should_exit()) {
                break;
            }

            /* pairs with qatomic_store_release() at multifd_recv() */
            if (!qatomic_load_acquire(&p->pending_job)) {
                /*
                 * Migration thread did not send work, this is
                 * equivalent to pending_sync on the sending
                 * side. Post sem_sync to notify we reached this
                 * point.
                 */
                qemu_sem_post(&multifd_recv_state->sem_sync);
                continue;
            }

            has_data = !!p->data->size;
        }

        if (has_data) {
            ret = multifd_recv_state->ops->recv(p, &local_err);
            if (ret != 0) {
                break;
            }
        }

        if (use_packets) {
            if (flags & MULTIFD_FLAG_SYNC) {
                qemu_sem_post(&multifd_recv_state->sem_sync);
                qemu_sem_wait(&p->sem_sync);
            }
        } else {
            p->total_normal_pages += p->data->size / qemu_target_page_size();
            p->data->size = 0;
            /*
             * Order data->size update before clearing
             * pending_job. Pairs with smp_mb_acquire() at
             * multifd_recv().
             */
            qatomic_store_release(&p->pending_job, false);
        }
    }

    if (local_err) {
        multifd_recv_terminate_threads(local_err);
        error_free(local_err);
    }

    rcu_unregister_thread();
    trace_multifd_recv_thread_end(p->id, p->packets_recved, p->total_normal_pages);

    return NULL;
}

int multifd_recv_setup(Error **errp)
{
    int thread_count;
    uint32_t page_count = MULTIFD_PACKET_SIZE / qemu_target_page_size();
    bool use_packets = multifd_use_packets();
    uint8_t i;

    /*
     * Return successfully if multiFD recv state is already initialised
     * or multiFD is not enabled.
     */
    if (multifd_recv_state || !migrate_multifd()) {
        return 0;
    }

    thread_count = migrate_multifd_channels();
    multifd_recv_state = g_malloc0(sizeof(*multifd_recv_state));
    multifd_recv_state->params = g_new0(MultiFDRecvParams, thread_count);

    multifd_recv_state->data = g_new0(MultiFDRecvData, 1);
    multifd_recv_state->data->size = 0;

    qatomic_set(&multifd_recv_state->count, 0);
    qatomic_set(&multifd_recv_state->exiting, 0);
    qemu_sem_init(&multifd_recv_state->sem_sync, 0);
    multifd_recv_state->ops = multifd_ops[migrate_multifd_compression()];

    for (i = 0; i < thread_count; i++) {
        MultiFDRecvParams *p = &multifd_recv_state->params[i];

        qemu_mutex_init(&p->mutex);
        qemu_sem_init(&p->sem_sync, 0);
        qemu_sem_init(&p->sem, 0);
        p->pending_job = false;
        p->id = i;

        p->data = g_new0(MultiFDRecvData, 1);
        p->data->size = 0;

        if (use_packets) {
            p->packet_len = sizeof(MultiFDPacket_t)
                + sizeof(uint64_t) * page_count;
            p->packet = g_malloc0(p->packet_len);
        }
        p->name = g_strdup_printf("multifdrecv_%d", i);
        p->iov = g_new0(struct iovec, page_count);
        p->normal = g_new0(ram_addr_t, page_count);
        p->page_count = page_count;
        p->page_size = qemu_target_page_size();
    }

    for (i = 0; i < thread_count; i++) {
        MultiFDRecvParams *p = &multifd_recv_state->params[i];
        int ret;

        ret = multifd_recv_state->ops->recv_setup(p, errp);
        if (ret) {
            return ret;
        }
    }
    return 0;
}

bool multifd_recv_all_channels_created(void)
{
    int thread_count = migrate_multifd_channels();

    if (!migrate_multifd()) {
        return true;
    }

    if (!multifd_recv_state) {
        /* Called before any connections created */
        return false;
    }

    return thread_count == qatomic_read(&multifd_recv_state->count);
}

/*
 * Try to receive all multifd channels to get ready for the migration.
 * Sets @errp when failing to receive the current channel.
 */
void multifd_recv_new_channel(QIOChannel *ioc, Error **errp)
{
    MultiFDRecvParams *p;
    Error *local_err = NULL;
    bool use_packets = multifd_use_packets();
    int id;

    if (use_packets) {
        id = multifd_recv_initial_packet(ioc, &local_err);
        if (id < 0) {
            multifd_recv_terminate_threads(local_err);
            error_propagate_prepend(errp, local_err,
                                    "failed to receive packet"
                                    " via multifd channel %d: ",
                                    qatomic_read(&multifd_recv_state->count));
            return;
        }
        trace_multifd_recv_new_channel(id);
    } else {
        id = qatomic_read(&multifd_recv_state->count);
    }

    p = &multifd_recv_state->params[id];
    if (p->c != NULL) {
        error_setg(&local_err, "multifd: received id '%d' already setup'",
                   id);
        multifd_recv_terminate_threads(local_err);
        error_propagate(errp, local_err);
        return;
    }
    p->c = ioc;
    object_ref(OBJECT(ioc));

    p->thread_created = true;
    qemu_thread_create(&p->thread, p->name, multifd_recv_thread, p,
                       QEMU_THREAD_JOINABLE);
    qatomic_inc(&multifd_recv_state->count);
}
