/*
 * eBPF RSS loader
 *
 * Developed by Daynix Computing LTD (http://www.daynix.com)
 *
 * Authors:
 *  Andrew Melnychenko <andrew@daynix.com>
 *  Yuri Benditovich <yuri.benditovich@daynix.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/qapi-types-misc.h"
#include "qapi/qapi-commands-ebpf.h"

#include <bpf/libbpf.h>
#include <bpf/bpf.h>

#include "hw/virtio/virtio-net.h" /* VIRTIO_NET_RSS_MAX_TABLE_LEN */

#include "ebpf/ebpf_rss.h"
#include "ebpf/rss.bpf.skeleton.h"
#include "ebpf/ebpf.h"

#include "trace.h"

void ebpf_rss_init(struct EBPFRSSContext *ctx)
{
    if (ctx != NULL) {
        ctx->obj = NULL;
        ctx->program_fd = -1;
        ctx->map_configuration = -1;
        ctx->map_toeplitz_key = -1;
        ctx->map_indirections_table = -1;

        ctx->mmap_configuration = NULL;
        ctx->mmap_toeplitz_key = NULL;
        ctx->mmap_indirections_table = NULL;
    }
}

bool ebpf_rss_is_loaded(struct EBPFRSSContext *ctx)
{
    return ctx != NULL && (ctx->obj != NULL || ctx->program_fd != -1);
}

static bool ebpf_rss_mmap(struct EBPFRSSContext *ctx, Error **errp)
{
    ctx->mmap_configuration = mmap(NULL, qemu_real_host_page_size(),
                                   PROT_READ | PROT_WRITE, MAP_SHARED,
                                   ctx->map_configuration, 0);
    if (ctx->mmap_configuration == MAP_FAILED) {
        trace_ebpf_rss_mmap_error(ctx, "configuration");
        error_setg(errp, "Unable to map eBPF configuration array");
        return false;
    }
    ctx->mmap_toeplitz_key = mmap(NULL, qemu_real_host_page_size(),
                                   PROT_READ | PROT_WRITE, MAP_SHARED,
                                   ctx->map_toeplitz_key, 0);
    if (ctx->mmap_toeplitz_key == MAP_FAILED) {
        trace_ebpf_rss_mmap_error(ctx, "toeplitz key");
        error_setg(errp, "Unable to map eBPF toeplitz array");
        goto toeplitz_fail;
    }
    ctx->mmap_indirections_table = mmap(NULL, qemu_real_host_page_size(),
                                   PROT_READ | PROT_WRITE, MAP_SHARED,
                                   ctx->map_indirections_table, 0);
    if (ctx->mmap_indirections_table == MAP_FAILED) {
        trace_ebpf_rss_mmap_error(ctx, "indirections table");
        error_setg(errp, "Unable to map eBPF indirection array");
        goto indirection_fail;
    }

    trace_ebpf_rss_mmap(ctx,
                        ctx->mmap_configuration,
                        ctx->mmap_toeplitz_key,
                        ctx->mmap_indirections_table);
    return true;

indirection_fail:
    munmap(ctx->mmap_toeplitz_key, qemu_real_host_page_size());
    ctx->mmap_toeplitz_key = NULL;
toeplitz_fail:
    munmap(ctx->mmap_configuration, qemu_real_host_page_size());
    ctx->mmap_configuration = NULL;

    ctx->mmap_indirections_table = NULL;
    return false;
}

static void ebpf_rss_munmap(struct EBPFRSSContext *ctx)
{
    munmap(ctx->mmap_indirections_table, qemu_real_host_page_size());
    munmap(ctx->mmap_toeplitz_key, qemu_real_host_page_size());
    munmap(ctx->mmap_configuration, qemu_real_host_page_size());

    ctx->mmap_configuration = NULL;
    ctx->mmap_toeplitz_key = NULL;
    ctx->mmap_indirections_table = NULL;
}

bool ebpf_rss_load(struct EBPFRSSContext *ctx, Error **errp)
{
    struct rss_bpf *rss_bpf_ctx;

    if (ebpf_rss_is_loaded(ctx)) {
        return false;
    }

    rss_bpf_ctx = rss_bpf__open();
    if (rss_bpf_ctx == NULL) {
        trace_ebpf_rss_open_error(ctx);
        error_setg(errp, "Unable to open eBPF RSS object");
        goto error;
    }

    bpf_program__set_type(rss_bpf_ctx->progs.tun_rss_steering_prog, BPF_PROG_TYPE_SOCKET_FILTER);

    if (rss_bpf__load(rss_bpf_ctx)) {
        trace_ebpf_rss_load_error(ctx);
        error_setg(errp, "Unable to load eBPF program");
        goto error;
    }

    ctx->obj = rss_bpf_ctx;
    ctx->program_fd = bpf_program__fd(
            rss_bpf_ctx->progs.tun_rss_steering_prog);
    ctx->map_configuration = bpf_map__fd(
            rss_bpf_ctx->maps.tap_rss_map_configurations);
    ctx->map_indirections_table = bpf_map__fd(
            rss_bpf_ctx->maps.tap_rss_map_indirection_table);
    ctx->map_toeplitz_key = bpf_map__fd(
            rss_bpf_ctx->maps.tap_rss_map_toeplitz_key);

    trace_ebpf_rss_load(ctx,
                        ctx->program_fd,
                        ctx->map_configuration,
                        ctx->map_indirections_table,
                        ctx->map_toeplitz_key);
    if (!ebpf_rss_mmap(ctx, errp)) {
        goto error;
    }

    return true;
error:
    rss_bpf__destroy(rss_bpf_ctx);
    ctx->obj = NULL;
    ctx->program_fd = -1;
    ctx->map_configuration = -1;
    ctx->map_toeplitz_key = -1;
    ctx->map_indirections_table = -1;

    return false;
}

bool ebpf_rss_load_fds(struct EBPFRSSContext *ctx, int program_fd,
                       int config_fd, int toeplitz_fd, int table_fd,
                       Error **errp)
{
    if (ebpf_rss_is_loaded(ctx)) {
        error_setg(errp, "eBPF program is already loaded");
        return false;
    }

    if (program_fd < 0) {
        error_setg(errp, "eBPF program FD is not open");
        return false;
    }
    if (config_fd < 0) {
        error_setg(errp, "eBPF config FD is not open");
        return false;
    }
    if (toeplitz_fd < 0) {
        error_setg(errp, "eBPF toeplitz FD is not open");
        return false;
    }
    if (table_fd < 0) {
        error_setg(errp, "eBPF indirection FD is not open");
        return false;
    }

    ctx->program_fd = program_fd;
    ctx->map_configuration = config_fd;
    ctx->map_toeplitz_key = toeplitz_fd;
    ctx->map_indirections_table = table_fd;

    trace_ebpf_rss_load(ctx,
                        ctx->program_fd,
                        ctx->map_configuration,
                        ctx->map_indirections_table,
                        ctx->map_toeplitz_key);

    if (!ebpf_rss_mmap(ctx, errp)) {
        ctx->program_fd = -1;
        ctx->map_configuration = -1;
        ctx->map_toeplitz_key = -1;
        ctx->map_indirections_table = -1;
        return false;
    }

    return true;
}

static void ebpf_rss_set_config(struct EBPFRSSContext *ctx,
                                struct EBPFRSSConfig *config)
{
    memcpy(ctx->mmap_configuration, config, sizeof(*config));
}

static bool ebpf_rss_set_indirections_table(struct EBPFRSSContext *ctx,
                                            uint16_t *indirections_table,
                                            size_t len,
                                            Error **errp)
{
    char *cursor = ctx->mmap_indirections_table;

    if (len > VIRTIO_NET_RSS_MAX_TABLE_LEN) {
        error_setg(errp, "Indirections table length %zu exceeds limit %d",
                   len, VIRTIO_NET_RSS_MAX_TABLE_LEN);
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        *(uint16_t *)cursor = indirections_table[i];
        cursor += 8;
    }

    return true;
}

static void ebpf_rss_set_toepliz_key(struct EBPFRSSContext *ctx,
                                     uint8_t *toeplitz_key)
{
    /* prepare toeplitz key */
    uint8_t toe[VIRTIO_NET_RSS_MAX_KEY_SIZE] = {};

    memcpy(toe, toeplitz_key, VIRTIO_NET_RSS_MAX_KEY_SIZE);
    *(uint32_t *)toe = ntohl(*(uint32_t *)toe);

    memcpy(ctx->mmap_toeplitz_key, toe, VIRTIO_NET_RSS_MAX_KEY_SIZE);
}

bool ebpf_rss_set_all(struct EBPFRSSContext *ctx, struct EBPFRSSConfig *config,
                      uint16_t *indirections_table, uint8_t *toeplitz_key,
                      Error **errp)
{
    if (!ebpf_rss_is_loaded(ctx)) {
        error_setg(errp, "eBPF program is not loaded");
        return false;
    }
    if (config == NULL) {
        error_setg(errp, "eBPF config table is NULL");
        return false;
    }
    if (indirections_table == NULL) {
        error_setg(errp, "eBPF indirections table is NULL");
        return false;
    }
    if (toeplitz_key == NULL) {
        error_setg(errp, "eBPF toeplitz key is NULL");
        return false;
    }

    ebpf_rss_set_config(ctx, config);

    if (!ebpf_rss_set_indirections_table(ctx, indirections_table,
                                         config->indirections_len,
                                         errp)) {
        return false;
    }

    ebpf_rss_set_toepliz_key(ctx, toeplitz_key);

    trace_ebpf_rss_set_data(ctx, config, indirections_table, toeplitz_key);

    return true;
}

void ebpf_rss_unload(struct EBPFRSSContext *ctx)
{
    if (!ebpf_rss_is_loaded(ctx)) {
        return;
    }

    trace_ebpf_rss_unload(ctx);

    ebpf_rss_munmap(ctx);

    if (ctx->obj) {
        rss_bpf__destroy(ctx->obj);
    } else {
        close(ctx->program_fd);
        close(ctx->map_configuration);
        close(ctx->map_toeplitz_key);
        close(ctx->map_indirections_table);
    }

    ctx->obj = NULL;
    ctx->program_fd = -1;
    ctx->map_configuration = -1;
    ctx->map_toeplitz_key = -1;
    ctx->map_indirections_table = -1;
}

ebpf_binary_init(EBPF_PROGRAM_ID_RSS, rss_bpf__elf_bytes)
