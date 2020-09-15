#include <unistd.h>
#include <bpf/libbpf.h>

#include "ebpf/ebpf_rss.h"

bool ebpf_rss_is_loaded(struct EBPFRSSContext *ctx)
{
    return ctx != NULL && ctx->program_fd >= 0;
}

bool ebpf_rss_load(struct EBPFRSSContext *ctx)
{
    int err = 0;
    struct bpf_object *object = NULL;

    if (ctx == NULL) {
        return false;
    }

    /* TODO: remove hardcode - add proper elf path, like for seabios? */
    err = bpf_prog_load("./rss.bpf.o", BPF_PROG_TYPE_SOCKET_FILTER,
                        &object, &ctx->program_fd);
    if (err) {
        ctx->program_fd = -1;
        return false;
    }

    ctx->map_configuration =
            bpf_object__find_map_fd_by_name(object,
            "tap_rss_map_configurations");
    if (ctx->map_configuration < 0) {
        goto map_issue;
    }

    ctx->map_toeplitz_key =
            bpf_object__find_map_fd_by_name(object,
            "tap_rss_map_toeplitz_key");
    if (ctx->map_toeplitz_key < 0) {
        goto map_issue;
    }

    ctx->map_indirections_table =
            bpf_object__find_map_fd_by_name(object,
            "tap_rss_map_indirection_table");
    if (ctx->map_indirections_table < 0) {
        goto map_issue;
    }

    return true;
map_issue:
    close(ctx->program_fd);
    ctx->program_fd = -1;
    return false;
}

bool ebpf_rss_set_config(struct EBPFRSSContext *ctx,
                         struct EBPFRSSConfig *config)
{
    if (ctx == NULL || ctx->program_fd < 0) {
        return false;
    }

    uint32_t map_key = 0;
    if (bpf_map_update_elem(ctx->map_configuration,
                            &map_key, config, BPF_ANY) < 0) {
        return false;
    }

    return true;
}

bool ebpf_rss_set_inirection_table(struct EBPFRSSContext *ctx,
                                   uint16_t *indirection_table, size_t len)
{
    if (ctx == NULL || ctx->program_fd < 0 ||
       len > EBPF_RSS_INDIRECTION_TABLE_SIZE) {
        return false;
    }
    uint32_t i = 0;

    for (; i < len; ++i) {
        if (bpf_map_update_elem(ctx->map_configuration, &i,
                                indirection_table + i, BPF_ANY) < 0) {
            return false;
        }
    }

    return true;
}

bool ebpf_rss_set_toepliz_key(struct EBPFRSSContext *ctx, uint8_t *toeplitz_key)
{
    if (ctx == NULL || ctx->program_fd < 0) {
        return false;
    }

    uint32_t map_key = 0;
    if (bpf_map_update_elem(ctx->map_configuration, &map_key, toeplitz_key,
                            BPF_ANY) < 0) {
        return false;
    }

    return true;
}

bool ebpf_rss_set_all(struct EBPFRSSContext *ctx, struct EBPFRSSConfig *config,
                      uint16_t *indirection_table, uint8_t *toeplitz_key)
{
    if (ctx == NULL || config == NULL || indirection_table == NULL ||
       toeplitz_key == NULL || ctx->program_fd < 0) {
        return false;
    }

    if (!ebpf_rss_set_config(ctx, config)) {
        return false;
    }

    if (!ebpf_rss_set_inirection_table(ctx, indirection_table,
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
    if (ctx == NULL || ctx->program_fd < 0) {
        return;
    }

    close(ctx->program_fd);
    close(ctx->map_configuration);
    close(ctx->map_toeplitz_key);
    close(ctx->map_indirections_table);
}
