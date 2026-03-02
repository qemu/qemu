/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * multifd colo implementation
 *
 * Copyright (c) Lukas Straub <lukasstraub2@web.de>
 */

#include "qemu/osdep.h"
#include "multifd.h"
#include "multifd-colo.h"
#include "migration/colo.h"
#include "system/ramblock.h"

void multifd_colo_prepare_recv(MultiFDRecvParams *p)
{
    /*
     * While we're still in precopy state (not yet in colo state), we copy
     * received pages to both guest and cache. No need to set dirty bits,
     * since guest and cache memory are in sync.
     */
    if (migration_incoming_in_colo_state()) {
        colo_record_bitmap(p->block, p->normal, p->normal_num);
        colo_record_bitmap(p->block, p->zero, p->zero_num);
    }
}

void multifd_colo_process_recv(MultiFDRecvParams *p)
{
    if (!migration_incoming_in_colo_state()) {
        for (int i = 0; i < p->normal_num; i++) {
            void *guest = p->block->host + p->normal[i];
            void *cache = p->host + p->normal[i];
            memcpy(guest, cache, multifd_ram_page_size());
        }
        for (int i = 0; i < p->zero_num; i++) {
            void *guest = p->block->host + p->zero[i];
            memset(guest, 0, multifd_ram_page_size());
        }
    }
}
