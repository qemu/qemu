/*
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef LIBQOS_RTAS_H
#define LIBQOS_RTAS_H
#include "libqos/malloc.h"

int qrtas_get_time_of_day(QGuestAllocator *alloc, struct tm *tm, uint32_t *ns);
uint32_t qrtas_ibm_read_pci_config(QGuestAllocator *alloc, uint64_t buid,
                                   uint32_t addr, uint32_t size);
int qrtas_ibm_write_pci_config(QGuestAllocator *alloc, uint64_t buid,
                               uint32_t addr, uint32_t size, uint32_t val);
int qrtas_check_exception(QGuestAllocator *alloc, uint32_t mask,
                          uint32_t buf_addr, uint32_t buf_len);
#endif /* LIBQOS_RTAS_H */
