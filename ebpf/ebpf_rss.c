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

#include <bpf/libbpf.h>
#include <bpf/bpf.h>

#include "hw/virtio/virtio-net.h" /* VIRTIO_NET_RSS_MAX_TABLE_LEN */

#include "ebpf/ebpf_rss.h"
#include "ebpf/rss.bpf.skeleton.h"
#include "trace.h"

void ebpf_rss_init(struct EBPFRSSContext *ctx)
{
    if (ctx != NULL) {
        ctx->obj = NULL;
    }
}

bool ebpf_rss_is_loaded(struct EBPFRSSContext *ctx)
{
    return ctx != NULL && ctx->obj != NULL;
}

bool ebpf_rss_load(struct EBPFRSSContext *ctx)
{
    struct rss_bpf *rss_bpf_ctx;

    if (ctx == NULL) {
        return false;
    }

    rss_bpf_ctx = rss_bpf__open();
    if (rss_bpf_ctx == NULL) {
        trace_ebpf_error("eBPF RSS", "can not open eBPF RSS object");
        goto error;
    }

    bpf_program__set_socket_filter(rss_bpf_ctx->progs.tun_rss_steering_prog);

    if (rss_bpf__load(rss_bpf_ctx)) {
        trace_ebpf_error("eBPF RSS", "can not load RSS program");
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

    return true;
error:
    rss_bpf__destroy(rss_bpf_ctx);
    ctx->obj = NULL;

    return false;
}

static bool ebpf_rss_set_config(struct EBPFRSSContext *ctx,
                                struct EBPFRSSConfig *config)
{
    uint32_t map_key = 0;

    if (!ebpf_rss_is_loaded(ctx)) {
        return false;
    }
    if (bpf_map_update_elem(ctx->map_configuration,
                            &map_key, config, 0) < 0) {
        return false;
    }
    return true;
}

static bool ebpf_rss_set_indirections_table(struct EBPFRSSContext *ctx,
                                            uint16_t *indirections_table,
                                            size_t len)
{
    uint32_t i = 0;

    if (!ebpf_rss_is_loaded(ctx) || indirections_table == NULL ||
       len > VIRTIO_NET_RSS_MAX_TABLE_LEN) {
        return false;
    }

    for (; i < len; ++i) {
        if (bpf_map_update_elem(ctx->map_indirections_table, &i,
                                indirections_table + i, 0) < 0) {
            return false;
        }
    }
    return true;
}

static bool ebpf_rss_set_toepliz_key(struct EBPFRSSContext *ctx,
                                     uint8_t *toeplitz_key)
{
    uint32_t map_key = 0;

    /* prepare toeplitz key */
    uint8_t toe[VIRTIO_NET_RSS_MAX_KEY_SIZE] = {};

    if (!ebpf_rss_is_loaded(ctx) || toeplitz_key == NULL) {
        return false;
    }
    memcpy(toe, toeplitz_key, VIRTIO_NET_RSS_MAX_KEY_SIZE);
    *(uint32_t *)toe = ntohl(*(uint32_t *)toe);

    if (bpf_map_update_elem(ctx->map_toeplitz_key, &map_key, toe,
                            0) < 0) {
        return false;
    }
    return true;
}

bool ebpf_rss_set_all(struct EBPFRSSContext *ctx, struct EBPFRSSConfig *config,
                      uint16_t *indirections_table, uint8_t *toeplitz_key)
{
    if (!ebpf_rss_is_loaded(ctx) || config == NULL ||
        indirections_table == NULL || toeplitz_key == NULL) {
        return false;
    }

    if (!ebpf_rss_set_config(ctx, config)) {
        return false;
    }

    if (!ebpf_rss_set_indirections_table(ctx, indirections_table,
                                      config->indirections_len)) {
        return false;
    }

    if (!ebpf_rss_set_toepliz_key(ctx, toeplitz_key)) {
        return false;
    }

    return true;
}

void ebpf_rss_unload(struct EBPFRSSContext *ctx)
{
    if (!ebpf_rss_is_loaded(ctx)) {
        return;
    }

    rss_bpf__destroy(ctx->obj);
    ctx->obj = NULL;
}
