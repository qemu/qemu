/*
 * eBPF RSS header
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

#ifndef QEMU_EBPF_RSS_H
#define QEMU_EBPF_RSS_H

#include "qapi/error.h"

#define EBPF_RSS_MAX_FDS 4

struct EBPFRSSContext {
    void *obj;
    int program_fd;
    int map_configuration;
    int map_toeplitz_key;
    int map_indirections_table;

    /* mapped eBPF maps for direct access to omit bpf_map_update_elem() */
    void *mmap_configuration;
    void *mmap_toeplitz_key;
    void *mmap_indirections_table;
};

struct EBPFRSSConfig {
    uint8_t redirect;
    uint8_t populate_hash;
    uint32_t hash_types;
    uint16_t indirections_len;
    uint16_t default_queue;
} __attribute__((packed));

void ebpf_rss_init(struct EBPFRSSContext *ctx);

bool ebpf_rss_is_loaded(struct EBPFRSSContext *ctx);

bool ebpf_rss_load(struct EBPFRSSContext *ctx, Error **errp);

bool ebpf_rss_load_fds(struct EBPFRSSContext *ctx, int program_fd,
                       int config_fd, int toeplitz_fd, int table_fd,
                       Error **errp);

bool ebpf_rss_set_all(struct EBPFRSSContext *ctx, struct EBPFRSSConfig *config,
                      uint16_t *indirections_table, uint8_t *toeplitz_key,
                      Error **errp);

void ebpf_rss_unload(struct EBPFRSSContext *ctx);

#endif /* QEMU_EBPF_RSS_H */
