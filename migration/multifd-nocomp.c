/*
 * Multifd RAM migration without compression
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
#include "system/ramblock.h"
#include "exec/target_page.h"
#include "file.h"
#include "migration-stats.h"
#include "multifd.h"
#include "options.h"
#include "migration.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "trace.h"
#include "qemu-file.h"

static MultiFDSendData *multifd_ram_send;

void multifd_ram_payload_alloc(MultiFDPages_t *pages)
{
    pages->offset = g_new0(ram_addr_t, multifd_ram_page_count());
}

void multifd_ram_payload_free(MultiFDPages_t *pages)
{
    g_clear_pointer(&pages->offset, g_free);
}

void multifd_ram_save_setup(void)
{
    multifd_ram_send = multifd_send_data_alloc();
}

void multifd_ram_save_cleanup(void)
{
    g_clear_pointer(&multifd_ram_send, multifd_send_data_free);
}

static void multifd_set_file_bitmap(MultiFDSendParams *p)
{
    MultiFDPages_t *pages = &p->data->u.ram;

    assert(pages->block);

    for (int i = 0; i < pages->normal_num; i++) {
        ramblock_set_file_bmap_atomic(pages->block, pages->offset[i], true);
    }

    for (int i = pages->normal_num; i < pages->num; i++) {
        ramblock_set_file_bmap_atomic(pages->block, pages->offset[i], false);
    }
}

static int multifd_nocomp_send_setup(MultiFDSendParams *p, Error **errp)
{
    uint32_t page_count = multifd_ram_page_count();

    if (migrate_zero_copy_send()) {
        p->write_flags |= QIO_CHANNEL_WRITE_FLAG_ZERO_COPY;
    }

    if (!migrate_mapped_ram()) {
        /* We need one extra place for the packet header */
        p->iov = g_new0(struct iovec, page_count + 1);
    } else {
        p->iov = g_new0(struct iovec, page_count);
    }

    return 0;
}

static void multifd_nocomp_send_cleanup(MultiFDSendParams *p, Error **errp)
{
    g_free(p->iov);
    p->iov = NULL;
}

static void multifd_ram_prepare_header(MultiFDSendParams *p)
{
    p->iov[0].iov_len = p->packet_len;
    p->iov[0].iov_base = p->packet;
    p->iovs_num++;
}

static void multifd_send_prepare_iovs(MultiFDSendParams *p)
{
    MultiFDPages_t *pages = &p->data->u.ram;
    uint32_t page_size = multifd_ram_page_size();

    for (int i = 0; i < pages->normal_num; i++) {
        p->iov[p->iovs_num].iov_base = pages->block->host + pages->offset[i];
        p->iov[p->iovs_num].iov_len = page_size;
        p->iovs_num++;
    }

    p->next_packet_size = pages->normal_num * page_size;
}

static int multifd_nocomp_send_prepare(MultiFDSendParams *p, Error **errp)
{
    bool use_zero_copy_send = migrate_zero_copy_send();
    int ret;

    multifd_send_zero_page_detect(p);

    if (migrate_mapped_ram()) {
        multifd_send_prepare_iovs(p);
        multifd_set_file_bitmap(p);

        return 0;
    }

    if (!use_zero_copy_send) {
        /*
         * Only !zerocopy needs the header in IOV; zerocopy will
         * send it separately.
         */
        multifd_ram_prepare_header(p);
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

        stat64_add(&mig_stats.multifd_bytes, p->packet_len);
    }

    return 0;
}

static int multifd_nocomp_recv_setup(MultiFDRecvParams *p, Error **errp)
{
    p->iov = g_new0(struct iovec, multifd_ram_page_count());
    return 0;
}

static void multifd_nocomp_recv_cleanup(MultiFDRecvParams *p)
{
    g_free(p->iov);
    p->iov = NULL;
}

static int multifd_nocomp_recv(MultiFDRecvParams *p, Error **errp)
{
    uint32_t flags;

    if (migrate_mapped_ram()) {
        return multifd_file_recv_data(p, errp);
    }

    flags = p->flags & MULTIFD_FLAG_COMPRESSION_MASK;

    if (flags != MULTIFD_FLAG_NOCOMP) {
        error_setg(errp, "multifd %u: flags received %x flags expected %x",
                   p->id, flags, MULTIFD_FLAG_NOCOMP);
        return -1;
    }

    multifd_recv_zero_page_process(p);

    if (!p->normal_num) {
        return 0;
    }

    for (int i = 0; i < p->normal_num; i++) {
        p->iov[i].iov_base = p->host + p->normal[i];
        p->iov[i].iov_len = multifd_ram_page_size();
        ramblock_recv_bitmap_set_offset(p->block, p->normal[i]);
    }
    return qio_channel_readv_all(p->c, p->iov, p->normal_num, errp);
}

static void multifd_pages_reset(MultiFDPages_t *pages)
{
    /*
     * We don't need to touch offset[] array, because it will be
     * overwritten later when reused.
     */
    pages->num = 0;
    pages->normal_num = 0;
    pages->block = NULL;
}

void multifd_ram_fill_packet(MultiFDSendParams *p)
{
    MultiFDPacket_t *packet = p->packet;
    MultiFDPages_t *pages = &p->data->u.ram;
    uint32_t zero_num = pages->num - pages->normal_num;

    packet->pages_alloc = cpu_to_be32(multifd_ram_page_count());
    packet->normal_pages = cpu_to_be32(pages->normal_num);
    packet->zero_pages = cpu_to_be32(zero_num);

    if (pages->block) {
        pstrcpy(packet->ramblock, sizeof(packet->ramblock),
                pages->block->idstr);
    }

    for (int i = 0; i < pages->num; i++) {
        /* there are architectures where ram_addr_t is 32 bit */
        uint64_t temp = pages->offset[i];

        packet->offset[i] = cpu_to_be64(temp);
    }

    trace_multifd_send_ram_fill(p->id, pages->normal_num,
                                zero_num);
}

int multifd_ram_unfill_packet(MultiFDRecvParams *p, Error **errp)
{
    MultiFDPacket_t *packet = p->packet;
    uint32_t page_count = multifd_ram_page_count();
    uint32_t page_size = multifd_ram_page_size();
    uint32_t pages_per_packet = be32_to_cpu(packet->pages_alloc);
    int i;

    if (pages_per_packet > page_count) {
        error_setg(errp, "multifd: received packet with %u pages, expected %u",
                   pages_per_packet, page_count);
        return -1;
    }

    p->normal_num = be32_to_cpu(packet->normal_pages);
    if (p->normal_num > pages_per_packet) {
        error_setg(errp, "multifd: received packet with %u non-zero pages, "
                   "which exceeds maximum expected pages %u",
                   p->normal_num, pages_per_packet);
        return -1;
    }

    p->zero_num = be32_to_cpu(packet->zero_pages);
    if (p->zero_num > pages_per_packet - p->normal_num) {
        error_setg(errp,
                   "multifd: received packet with %u zero pages, expected maximum %u",
                   p->zero_num, pages_per_packet - p->normal_num);
        return -1;
    }

    if (p->normal_num == 0 && p->zero_num == 0) {
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

        if (offset > (p->block->used_length - page_size)) {
            error_setg(errp, "multifd: offset too long %" PRIu64
                       " (max " RAM_ADDR_FMT ")",
                       offset, p->block->used_length);
            return -1;
        }
        p->normal[i] = offset;
    }

    for (i = 0; i < p->zero_num; i++) {
        uint64_t offset = be64_to_cpu(packet->offset[p->normal_num + i]);

        if (offset > (p->block->used_length - page_size)) {
            error_setg(errp, "multifd: offset too long %" PRIu64
                       " (max " RAM_ADDR_FMT ")",
                       offset, p->block->used_length);
            return -1;
        }
        p->zero[i] = offset;
    }

    return 0;
}

static inline bool multifd_queue_empty(MultiFDPages_t *pages)
{
    return pages->num == 0;
}

static inline bool multifd_queue_full(MultiFDPages_t *pages)
{
    return pages->num == multifd_ram_page_count();
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
    pages = &multifd_ram_send->u.ram;

    if (multifd_payload_empty(multifd_ram_send)) {
        multifd_pages_reset(pages);
        multifd_set_payload_type(multifd_ram_send, MULTIFD_PAYLOAD_RAM);
    }

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
        if (!multifd_send(&multifd_ram_send)) {
            return false;
        }
        goto retry;
    }

    /* Not empty, and we still have space, do it! */
    multifd_enqueue(pages, offset);
    return true;
}

/*
 * We have two modes for multifd flushes:
 *
 * - Per-section mode: this is the legacy way to flush, it requires one
 *   MULTIFD_FLAG_SYNC message for each RAM_SAVE_FLAG_EOS.
 *
 * - Per-round mode: this is the modern way to flush, it requires one
 *   MULTIFD_FLAG_SYNC message only for each round of RAM scan.  Normally
 *   it's paired with a new RAM_SAVE_FLAG_MULTIFD_FLUSH message in network
 *   based migrations.
 *
 * One thing to mention is mapped-ram always use the modern way to sync.
 */

/* Do we need a per-section multifd flush (legacy way)? */
bool multifd_ram_sync_per_section(void)
{
    if (!migrate_multifd()) {
        return false;
    }

    if (migrate_mapped_ram()) {
        return false;
    }

    return migrate_multifd_flush_after_each_section();
}

/* Do we need a per-round multifd flush (modern way)? */
bool multifd_ram_sync_per_round(void)
{
    if (!migrate_multifd()) {
        return false;
    }

    if (migrate_mapped_ram()) {
        return true;
    }

    return !migrate_multifd_flush_after_each_section();
}

int multifd_ram_flush_and_sync(QEMUFile *f)
{
    MultiFDSyncReq req;
    int ret;

    if (!migrate_multifd() || migration_in_postcopy()) {
        return 0;
    }

    if (!multifd_payload_empty(multifd_ram_send)) {
        if (!multifd_send(&multifd_ram_send)) {
            error_report("%s: multifd_send fail", __func__);
            return -1;
        }
    }

    /* File migrations only need to sync with threads */
    req = migrate_mapped_ram() ? MULTIFD_SYNC_LOCAL : MULTIFD_SYNC_ALL;

    ret = multifd_send_sync_main(req);
    if (ret) {
        return ret;
    }

    /* If we don't need to sync with remote at all, nothing else to do */
    if (req == MULTIFD_SYNC_LOCAL) {
        return 0;
    }

    /*
     * Old QEMUs don't understand RAM_SAVE_FLAG_MULTIFD_FLUSH, it relies
     * on RAM_SAVE_FLAG_EOS instead.
     */
    if (migrate_multifd_flush_after_each_section()) {
        return 0;
    }

    qemu_put_be64(f, RAM_SAVE_FLAG_MULTIFD_FLUSH);
    qemu_fflush(f);

    return 0;
}

bool multifd_send_prepare_common(MultiFDSendParams *p)
{
    MultiFDPages_t *pages = &p->data->u.ram;
    multifd_ram_prepare_header(p);
    multifd_send_zero_page_detect(p);

    if (!pages->normal_num) {
        p->next_packet_size = 0;
        return false;
    }

    return true;
}

static const MultiFDMethods multifd_nocomp_ops = {
    .send_setup = multifd_nocomp_send_setup,
    .send_cleanup = multifd_nocomp_send_cleanup,
    .send_prepare = multifd_nocomp_send_prepare,
    .recv_setup = multifd_nocomp_recv_setup,
    .recv_cleanup = multifd_nocomp_recv_cleanup,
    .recv = multifd_nocomp_recv
};

static void multifd_nocomp_register(void)
{
    multifd_register_ops(MULTIFD_COMPRESSION_NONE, &multifd_nocomp_ops);
}

migration_init(multifd_nocomp_register);
