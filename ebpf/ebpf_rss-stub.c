/*
 * eBPF RSS stub file
 *
 * Developed by Daynix Computing LTD (http://www.daynix.com)
 *
 * Authors:
 *  Yuri Benditovich <yuri.benditovich@daynix.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "ebpf/ebpf_rss.h"

void ebpf_rss_init(struct EBPFRSSContext *ctx)
{

}

bool ebpf_rss_is_loaded(struct EBPFRSSContext *ctx)
{
    return false;
}

bool ebpf_rss_load(struct EBPFRSSContext *ctx)
{
    return false;
}

bool ebpf_rss_set_all(struct EBPFRSSContext *ctx, struct EBPFRSSConfig *config,
                      uint16_t *indirections_table, uint8_t *toeplitz_key)
{
    return false;
}

void ebpf_rss_unload(struct EBPFRSSContext *ctx)
{

}
