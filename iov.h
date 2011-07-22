/*
 * Helpers for getting linearized buffers from iov / filling buffers into iovs
 *
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * Author(s):
 *  Amit Shah <amit.shah@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu-common.h"

size_t iov_from_buf(struct iovec *iov, unsigned int iov_cnt,
                    const void *buf, size_t iov_off, size_t size);
size_t iov_to_buf(const struct iovec *iov, const unsigned int iov_cnt,
                  void *buf, size_t iov_off, size_t size);
size_t iov_size(const struct iovec *iov, const unsigned int iov_cnt);
