/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * multifd colo header
 *
 * Copyright (c) Lukas Straub <lukasstraub2@web.de>
 */

#ifndef QEMU_MIGRATION_MULTIFD_COLO_H
#define QEMU_MIGRATION_MULTIFD_COLO_H

#ifdef CONFIG_REPLICATION

void multifd_colo_prepare_recv(MultiFDRecvParams *p);
void multifd_colo_process_recv(MultiFDRecvParams *p);

#else

static inline void multifd_colo_prepare_recv(MultiFDRecvParams *p) {}
static inline void multifd_colo_process_recv(MultiFDRecvParams *p) {}

#endif
#endif
