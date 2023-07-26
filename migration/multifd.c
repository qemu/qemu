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
#include "ram.h"
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

/* Multifd without compression */

/**
 * nocomp_send_setup: setup send side
 *
 * For no compression this function does nothing.
 *
 * Returns 0 for success or -1 for error
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static int nocomp_send_setup(MultiFDSendParams *p, Error **errp)
{
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
    MultiFDPages_t *pages = p->pages;

    for (int i = 0; i < p->normal_num; i++) {
        p->iov[p->iovs_num].iov_base = pages->block->host + p->normal[i];
        p->iov[p->iovs_num].iov_len = p->page_size;
        p->iovs_num++;
    }

    p->next_packet_size = p->normal_num * p->page_size;
    p->flags |= MULTIFD_FLAG_NOCOMP;
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
 * nocomp_recv_pages: read the data from the channel into actual pages
 *
 * For no compression we just need to read things into the correct place.
 *
 * Returns 0 for success or -1 for error
 *
 * @p: Params for the channel that we are using
 * @errp: pointer to an error
 */
static int nocomp_recv_pages(MultiFDRecvParams *p, Error **errp)
{
    uint32_t flags = p->flags & MULTIFD_FLAG_COMPRESSION_MASK;

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
    .recv_pages = nocomp_recv_pages
};

static MultiFDMethods *multifd_ops[MULTIFD_COMPRESSION__MAX] = {
    [MULTIFD_COMPRESSION_NONE] = &multifd_nocomp_ops,
};

void multifd_register_ops(int method, MultiFDMethods *ops)
{
    assert(0 < method && method < MULTIFD_COMPRESSION__MAX);
    multifd_ops[method] = ops;
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
    stat64_add(&mig_stats.transferred, size);
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
        error_setg(errp, "multifd: received channel version %u "
                   "expected %u", msg.version, MULTIFD_VERSION);
        return -1;
    }

    return msg.id;
}

static MultiFDPages_t *multifd_pages_init(size_t size)
{
    MultiFDPages_t *pages = g_new0(MultiFDPages_t, 1);

    pages->allocated = size;
    pages->offset = g_new0(ram_addr_t, size);

    return pages;
}

static void multifd_pages_clear(MultiFDPages_t *pages)
{
    pages->num = 0;
    pages->allocated = 0;
    pages->packet_num = 0;
    pages->block = NULL;
    g_free(pages->offset);
    pages->offset = NULL;
    g_free(pages);
}

static void multifd_send_fill_packet(MultiFDSendParams *p)
{
    MultiFDPacket_t *packet = p->packet;
    int i;

    packet->flags = cpu_to_be32(p->flags);
    packet->pages_alloc = cpu_to_be32(p->pages->allocated);
    packet->normal_pages = cpu_to_be32(p->normal_num);
    packet->next_packet_size = cpu_to_be32(p->next_packet_size);
    packet->packet_num = cpu_to_be64(p->packet_num);

    if (p->pages->block) {
        strncpy(packet->ramblock, p->pages->block->idstr, 256);
    }

    for (i = 0; i < p->normal_num; i++) {
        /* there are architectures where ram_addr_t is 32 bit */
        uint64_t temp = p->normal[i];

        packet->offset[i] = cpu_to_be64(temp);
    }
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

struct {
    MultiFDSendParams *params;
    /* array of pages to sent */
    MultiFDPages_t *pages;
    /* global number of generated multifd packets */
    uint64_t packet_num;
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
 */

static int multifd_send_pages(QEMUFile *f)
{
    int i;
    static int next_channel;
    MultiFDSendParams *p = NULL; /* make happy gcc */
    MultiFDPages_t *pages = multifd_send_state->pages;

    if (qatomic_read(&multifd_send_state->exiting)) {
        return -1;
    }

    qemu_sem_wait(&multifd_send_state->channels_ready);
    /*
     * next_channel can remain from a previous migration that was
     * using more channels, so ensure it doesn't overflow if the
     * limit is lower now.
     */
    next_channel %= migrate_multifd_channels();
    for (i = next_channel;; i = (i + 1) % migrate_multifd_channels()) {
        p = &multifd_send_state->params[i];

        qemu_mutex_lock(&p->mutex);
        if (p->quit) {
            error_report("%s: channel %d has already quit!", __func__, i);
            qemu_mutex_unlock(&p->mutex);
            return -1;
        }
        if (!p->pending_job) {
            p->pending_job++;
            next_channel = (i + 1) % migrate_multifd_channels();
            break;
        }
        qemu_mutex_unlock(&p->mutex);
    }
    assert(!p->pages->num);
    assert(!p->pages->block);

    p->packet_num = multifd_send_state->packet_num++;
    multifd_send_state->pages = p->pages;
    p->pages = pages;
    qemu_mutex_unlock(&p->mutex);
    qemu_sem_post(&p->sem);

    return 1;
}

int multifd_queue_page(QEMUFile *f, RAMBlock *block, ram_addr_t offset)
{
    MultiFDPages_t *pages = multifd_send_state->pages;
    bool changed = false;

    if (!pages->block) {
        pages->block = block;
    }

    if (pages->block == block) {
        pages->offset[pages->num] = offset;
        pages->num++;

        if (pages->num < pages->allocated) {
            return 1;
        }
    } else {
        changed = true;
    }

    if (multifd_send_pages(f) < 0) {
        return -1;
    }

    if (changed) {
        return multifd_queue_page(f, block, offset);
    }

    return 1;
}

static void multifd_send_terminate_threads(Error *err)
{
    int i;

    trace_multifd_send_terminate_threads(err != NULL);

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

    /*
     * We don't want to exit each threads twice.  Depending on where
     * we get the error, or if there are two independent errors in two
     * threads at the same time, we can end calling this function
     * twice.
     */
    if (qatomic_xchg(&multifd_send_state->exiting, 1)) {
        return;
    }

    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDSendParams *p = &multifd_send_state->params[i];

        qemu_mutex_lock(&p->mutex);
        p->quit = true;
        qemu_sem_post(&p->sem);
        if (p->c) {
            qio_channel_shutdown(p->c, QIO_CHANNEL_SHUTDOWN_BOTH, NULL);
        }
        qemu_mutex_unlock(&p->mutex);
    }
}

void multifd_save_cleanup(void)
{
    int i;

    if (!migrate_multifd()) {
        return;
    }
    multifd_send_terminate_threads(NULL);
    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDSendParams *p = &multifd_send_state->params[i];

        if (p->running) {
            qemu_thread_join(&p->thread);
        }
    }
    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDSendParams *p = &multifd_send_state->params[i];
        Error *local_err = NULL;

        if (p->registered_yank) {
            migration_ioc_unregister_yank(p->c);
        }
        socket_send_channel_destroy(p->c);
        p->c = NULL;
        qemu_mutex_destroy(&p->mutex);
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
        g_free(p->normal);
        p->normal = NULL;
        multifd_send_state->ops->send_cleanup(p, &local_err);
        if (local_err) {
            migrate_set_error(migrate_get_current(), local_err);
            error_free(local_err);
        }
    }
    qemu_sem_destroy(&multifd_send_state->channels_ready);
    g_free(multifd_send_state->params);
    multifd_send_state->params = NULL;
    multifd_pages_clear(multifd_send_state->pages);
    multifd_send_state->pages = NULL;
    g_free(multifd_send_state);
    multifd_send_state = NULL;
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

int multifd_send_sync_main(QEMUFile *f)
{
    int i;
    bool flush_zero_copy;

    if (!migrate_multifd()) {
        return 0;
    }
    if (multifd_send_state->pages->num) {
        if (multifd_send_pages(f) < 0) {
            error_report("%s: multifd_send_pages fail", __func__);
            return -1;
        }
    }

    /*
     * When using zero-copy, it's necessary to flush the pages before any of
     * the pages can be sent again, so we'll make sure the new version of the
     * pages will always arrive _later_ than the old pages.
     *
     * Currently we achieve this by flushing the zero-page requested writes
     * per ram iteration, but in the future we could potentially optimize it
     * to be less frequent, e.g. only after we finished one whole scanning of
     * all the dirty bitmaps.
     */

    flush_zero_copy = migrate_zero_copy_send();

    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDSendParams *p = &multifd_send_state->params[i];

        trace_multifd_send_sync_main_signal(p->id);

        qemu_mutex_lock(&p->mutex);

        if (p->quit) {
            error_report("%s: channel %d has already quit", __func__, i);
            qemu_mutex_unlock(&p->mutex);
            return -1;
        }

        p->packet_num = multifd_send_state->packet_num++;
        p->flags |= MULTIFD_FLAG_SYNC;
        p->pending_job++;
        qemu_mutex_unlock(&p->mutex);
        qemu_sem_post(&p->sem);
    }
    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDSendParams *p = &multifd_send_state->params[i];

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
    bool use_zero_copy_send = migrate_zero_copy_send();

    thread = migration_threads_add(p->name, qemu_get_thread_id());

    trace_multifd_send_thread_start(p->id);
    rcu_register_thread();

    if (multifd_send_initial_packet(p, &local_err) < 0) {
        ret = -1;
        goto out;
    }
    /* initial packet */
    p->num_packets = 1;

    while (true) {
        qemu_sem_post(&multifd_send_state->channels_ready);
        qemu_sem_wait(&p->sem);

        if (qatomic_read(&multifd_send_state->exiting)) {
            break;
        }
        qemu_mutex_lock(&p->mutex);

        if (p->pending_job) {
            uint64_t packet_num = p->packet_num;
            uint32_t flags;
            p->normal_num = 0;

            if (use_zero_copy_send) {
                p->iovs_num = 0;
            } else {
                p->iovs_num = 1;
            }

            for (int i = 0; i < p->pages->num; i++) {
                p->normal[p->normal_num] = p->pages->offset[i];
                p->normal_num++;
            }

            if (p->normal_num) {
                ret = multifd_send_state->ops->send_prepare(p, &local_err);
                if (ret != 0) {
                    qemu_mutex_unlock(&p->mutex);
                    break;
                }
            }
            multifd_send_fill_packet(p);
            flags = p->flags;
            p->flags = 0;
            p->num_packets++;
            p->total_normal_pages += p->normal_num;
            p->pages->num = 0;
            p->pages->block = NULL;
            qemu_mutex_unlock(&p->mutex);

            trace_multifd_send(p->id, packet_num, p->normal_num, flags,
                               p->next_packet_size);

            if (use_zero_copy_send) {
                /* Send header first, without zerocopy */
                ret = qio_channel_write_all(p->c, (void *)p->packet,
                                            p->packet_len, &local_err);
                if (ret != 0) {
                    break;
                }
                stat64_add(&mig_stats.multifd_bytes, p->packet_len);
                stat64_add(&mig_stats.transferred, p->packet_len);
            } else {
                /* Send header using the same writev call */
                p->iov[0].iov_len = p->packet_len;
                p->iov[0].iov_base = p->packet;
            }

            ret = qio_channel_writev_full_all(p->c, p->iov, p->iovs_num, NULL,
                                              0, p->write_flags, &local_err);
            if (ret != 0) {
                break;
            }

            stat64_add(&mig_stats.multifd_bytes, p->next_packet_size);
            stat64_add(&mig_stats.transferred, p->next_packet_size);
            qemu_mutex_lock(&p->mutex);
            p->pending_job--;
            qemu_mutex_unlock(&p->mutex);

            if (flags & MULTIFD_FLAG_SYNC) {
                qemu_sem_post(&p->sem_sync);
            }
        } else if (p->quit) {
            qemu_mutex_unlock(&p->mutex);
            break;
        } else {
            qemu_mutex_unlock(&p->mutex);
            /* sometimes there are spurious wakeups */
        }
    }

out:
    if (local_err) {
        trace_multifd_send_error(p->id);
        multifd_send_terminate_threads(local_err);
        error_free(local_err);
    }

    /*
     * Error happen, I will exit, but I can't just leave, tell
     * who pay attention to me.
     */
    if (ret != 0) {
        qemu_sem_post(&p->sem_sync);
        qemu_sem_post(&multifd_send_state->channels_ready);
    }

    qemu_mutex_lock(&p->mutex);
    p->running = false;
    qemu_mutex_unlock(&p->mutex);

    rcu_unregister_thread();
    migration_threads_remove(thread);
    trace_multifd_send_thread_end(p->id, p->num_packets, p->total_normal_pages);

    return NULL;
}

static bool multifd_channel_connect(MultiFDSendParams *p,
                                    QIOChannel *ioc,
                                    Error *error);

static void multifd_tls_outgoing_handshake(QIOTask *task,
                                           gpointer opaque)
{
    MultiFDSendParams *p = opaque;
    QIOChannel *ioc = QIO_CHANNEL(qio_task_get_source(task));
    Error *err = NULL;

    if (qio_task_propagate_error(task, &err)) {
        trace_multifd_tls_outgoing_handshake_error(ioc, error_get_pretty(err));
    } else {
        trace_multifd_tls_outgoing_handshake_complete(ioc);
    }

    if (!multifd_channel_connect(p, ioc, err)) {
        /*
         * Error happen, mark multifd_send_thread status as 'quit' although it
         * is not created, and then tell who pay attention to me.
         */
        p->quit = true;
        qemu_sem_post(&multifd_send_state->channels_ready);
        qemu_sem_post(&p->sem_sync);
    }
}

static void *multifd_tls_handshake_thread(void *opaque)
{
    MultiFDSendParams *p = opaque;
    QIOChannelTLS *tioc = QIO_CHANNEL_TLS(p->c);

    qio_channel_tls_handshake(tioc,
                              multifd_tls_outgoing_handshake,
                              p,
                              NULL,
                              NULL);
    return NULL;
}

static void multifd_tls_channel_connect(MultiFDSendParams *p,
                                        QIOChannel *ioc,
                                        Error **errp)
{
    MigrationState *s = migrate_get_current();
    const char *hostname = s->hostname;
    QIOChannelTLS *tioc;

    tioc = migration_tls_client_create(ioc, hostname, errp);
    if (!tioc) {
        return;
    }

    object_unref(OBJECT(ioc));
    trace_multifd_tls_outgoing_handshake_start(ioc, tioc, hostname);
    qio_channel_set_name(QIO_CHANNEL(tioc), "multifd-tls-outgoing");
    p->c = QIO_CHANNEL(tioc);
    qemu_thread_create(&p->thread, "multifd-tls-handshake-worker",
                       multifd_tls_handshake_thread, p,
                       QEMU_THREAD_JOINABLE);
}

static bool multifd_channel_connect(MultiFDSendParams *p,
                                    QIOChannel *ioc,
                                    Error *error)
{
    trace_multifd_set_outgoing_channel(
        ioc, object_get_typename(OBJECT(ioc)),
        migrate_get_current()->hostname, error);

    if (error) {
        return false;
    }
    if (migrate_channel_requires_tls_upgrade(ioc)) {
        multifd_tls_channel_connect(p, ioc, &error);
        if (!error) {
            /*
             * tls_channel_connect will call back to this
             * function after the TLS handshake,
             * so we mustn't call multifd_send_thread until then
             */
            return true;
        } else {
            return false;
        }
    } else {
        migration_ioc_register_yank(ioc);
        p->registered_yank = true;
        p->c = ioc;
        qemu_thread_create(&p->thread, p->name, multifd_send_thread, p,
                           QEMU_THREAD_JOINABLE);
    }
    return true;
}

static void multifd_new_send_channel_cleanup(MultiFDSendParams *p,
                                             QIOChannel *ioc, Error *err)
{
     migrate_set_error(migrate_get_current(), err);
     /* Error happen, we need to tell who pay attention to me */
     qemu_sem_post(&multifd_send_state->channels_ready);
     qemu_sem_post(&p->sem_sync);
     /*
      * Although multifd_send_thread is not created, but main migration
      * thread need to judge whether it is running, so we need to mark
      * its status.
      */
     p->quit = true;
     object_unref(OBJECT(ioc));
     error_free(err);
}

static void multifd_new_send_channel_async(QIOTask *task, gpointer opaque)
{
    MultiFDSendParams *p = opaque;
    QIOChannel *sioc = QIO_CHANNEL(qio_task_get_source(task));
    Error *local_err = NULL;

    trace_multifd_new_send_channel_async(p->id);
    if (!qio_task_propagate_error(task, &local_err)) {
        p->c = sioc;
        qio_channel_set_delay(p->c, false);
        p->running = true;
        if (multifd_channel_connect(p, sioc, local_err)) {
            return;
        }
    }

    multifd_new_send_channel_cleanup(p, sioc, local_err);
}

int multifd_save_setup(Error **errp)
{
    int thread_count;
    uint32_t page_count = MULTIFD_PACKET_SIZE / qemu_target_page_size();
    uint8_t i;

    if (!migrate_multifd()) {
        return 0;
    }

    thread_count = migrate_multifd_channels();
    multifd_send_state = g_malloc0(sizeof(*multifd_send_state));
    multifd_send_state->params = g_new0(MultiFDSendParams, thread_count);
    multifd_send_state->pages = multifd_pages_init(page_count);
    qemu_sem_init(&multifd_send_state->channels_ready, 0);
    qatomic_set(&multifd_send_state->exiting, 0);
    multifd_send_state->ops = multifd_ops[migrate_multifd_compression()];

    for (i = 0; i < thread_count; i++) {
        MultiFDSendParams *p = &multifd_send_state->params[i];

        qemu_mutex_init(&p->mutex);
        qemu_sem_init(&p->sem, 0);
        qemu_sem_init(&p->sem_sync, 0);
        p->quit = false;
        p->pending_job = 0;
        p->id = i;
        p->pages = multifd_pages_init(page_count);
        p->packet_len = sizeof(MultiFDPacket_t)
                      + sizeof(uint64_t) * page_count;
        p->packet = g_malloc0(p->packet_len);
        p->packet->magic = cpu_to_be32(MULTIFD_MAGIC);
        p->packet->version = cpu_to_be32(MULTIFD_VERSION);
        p->name = g_strdup_printf("multifdsend_%d", i);
        /* We need one extra place for the packet header */
        p->iov = g_new0(struct iovec, page_count + 1);
        p->normal = g_new0(ram_addr_t, page_count);
        p->page_size = qemu_target_page_size();
        p->page_count = page_count;

        if (migrate_zero_copy_send()) {
            p->write_flags = QIO_CHANNEL_WRITE_FLAG_ZERO_COPY;
        } else {
            p->write_flags = 0;
        }

        socket_send_channel_create(multifd_new_send_channel_async, p);
    }

    for (i = 0; i < thread_count; i++) {
        MultiFDSendParams *p = &multifd_send_state->params[i];
        Error *local_err = NULL;
        int ret;

        ret = multifd_send_state->ops->send_setup(p, &local_err);
        if (ret) {
            error_propagate(errp, local_err);
            return ret;
        }
    }
    return 0;
}

struct {
    MultiFDRecvParams *params;
    /* number of created threads */
    int count;
    /* syncs main thread and channels */
    QemuSemaphore sem_sync;
    /* global number of generated multifd packets */
    uint64_t packet_num;
    /* multifd ops */
    MultiFDMethods *ops;
} *multifd_recv_state;

static void multifd_recv_terminate_threads(Error *err)
{
    int i;

    trace_multifd_recv_terminate_threads(err != NULL);

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

        qemu_mutex_lock(&p->mutex);
        p->quit = true;
        /*
         * We could arrive here for two reasons:
         *  - normal quit, i.e. everything went fine, just finished
         *  - error quit: We close the channels so the channel threads
         *    finish the qio_channel_read_all_eof()
         */
        if (p->c) {
            qio_channel_shutdown(p->c, QIO_CHANNEL_SHUTDOWN_BOTH, NULL);
        }
        qemu_mutex_unlock(&p->mutex);
    }
}

void multifd_load_shutdown(void)
{
    if (migrate_multifd()) {
        multifd_recv_terminate_threads(NULL);
    }
}

void multifd_load_cleanup(void)
{
    int i;

    if (!migrate_multifd()) {
        return;
    }
    multifd_recv_terminate_threads(NULL);
    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDRecvParams *p = &multifd_recv_state->params[i];

        if (p->running) {
            /*
             * multifd_recv_thread may hung at MULTIFD_FLAG_SYNC handle code,
             * however try to wakeup it without harm in cleanup phase.
             */
            qemu_sem_post(&p->sem_sync);
        }

        qemu_thread_join(&p->thread);
    }
    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDRecvParams *p = &multifd_recv_state->params[i];

        migration_ioc_unregister_yank(p->c);
        object_unref(OBJECT(p->c));
        p->c = NULL;
        qemu_mutex_destroy(&p->mutex);
        qemu_sem_destroy(&p->sem_sync);
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
    qemu_sem_destroy(&multifd_recv_state->sem_sync);
    g_free(multifd_recv_state->params);
    multifd_recv_state->params = NULL;
    g_free(multifd_recv_state);
    multifd_recv_state = NULL;
}

void multifd_recv_sync_main(void)
{
    int i;

    if (!migrate_multifd()) {
        return;
    }
    for (i = 0; i < migrate_multifd_channels(); i++) {
        MultiFDRecvParams *p = &multifd_recv_state->params[i];

        trace_multifd_recv_sync_main_wait(p->id);
        qemu_sem_wait(&multifd_recv_state->sem_sync);
    }
    for (i = 0; i < migrate_multifd_channels(); i++) {
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
    int ret;

    trace_multifd_recv_thread_start(p->id);
    rcu_register_thread();

    while (true) {
        uint32_t flags;

        if (p->quit) {
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
        trace_multifd_recv(p->id, p->packet_num, p->normal_num, flags,
                           p->next_packet_size);
        p->num_packets++;
        p->total_normal_pages += p->normal_num;
        qemu_mutex_unlock(&p->mutex);

        if (p->normal_num) {
            ret = multifd_recv_state->ops->recv_pages(p, &local_err);
            if (ret != 0) {
                break;
            }
        }

        if (flags & MULTIFD_FLAG_SYNC) {
            qemu_sem_post(&multifd_recv_state->sem_sync);
            qemu_sem_wait(&p->sem_sync);
        }
    }

    if (local_err) {
        multifd_recv_terminate_threads(local_err);
        error_free(local_err);
    }
    qemu_mutex_lock(&p->mutex);
    p->running = false;
    qemu_mutex_unlock(&p->mutex);

    rcu_unregister_thread();
    trace_multifd_recv_thread_end(p->id, p->num_packets, p->total_normal_pages);

    return NULL;
}

int multifd_load_setup(Error **errp)
{
    int thread_count;
    uint32_t page_count = MULTIFD_PACKET_SIZE / qemu_target_page_size();
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
    qatomic_set(&multifd_recv_state->count, 0);
    qemu_sem_init(&multifd_recv_state->sem_sync, 0);
    multifd_recv_state->ops = multifd_ops[migrate_multifd_compression()];

    for (i = 0; i < thread_count; i++) {
        MultiFDRecvParams *p = &multifd_recv_state->params[i];

        qemu_mutex_init(&p->mutex);
        qemu_sem_init(&p->sem_sync, 0);
        p->quit = false;
        p->id = i;
        p->packet_len = sizeof(MultiFDPacket_t)
                      + sizeof(uint64_t) * page_count;
        p->packet = g_malloc0(p->packet_len);
        p->name = g_strdup_printf("multifdrecv_%d", i);
        p->iov = g_new0(struct iovec, page_count);
        p->normal = g_new0(ram_addr_t, page_count);
        p->page_count = page_count;
        p->page_size = qemu_target_page_size();
    }

    for (i = 0; i < thread_count; i++) {
        MultiFDRecvParams *p = &multifd_recv_state->params[i];
        Error *local_err = NULL;
        int ret;

        ret = multifd_recv_state->ops->recv_setup(p, &local_err);
        if (ret) {
            error_propagate(errp, local_err);
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
    int id;

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
    /* initial packet */
    p->num_packets = 1;

    p->running = true;
    qemu_thread_create(&p->thread, p->name, multifd_recv_thread, p,
                       QEMU_THREAD_JOINABLE);
    qatomic_inc(&multifd_recv_state->count);
}
