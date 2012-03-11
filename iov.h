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

/**
 * count and return data size, in bytes, of an iovec
 * starting at `iov' of `iov_cnt' number of elements.
 */
size_t iov_size(const struct iovec *iov, const unsigned int iov_cnt);

/**
 * Copy from single continuous buffer to scatter-gather vector of buffers
 * (iovec) and back like memcpy() between two continuous memory regions.
 * Data in single continuous buffer starting at address `buf' and
 * `bytes' bytes long will be copied to/from an iovec `iov' with
 * `iov_cnt' number of elements, starting at byte position `offset'
 * within the iovec.  If the iovec does not contain enough space,
 * only part of data will be copied, up to the end of the iovec.
 * Number of bytes actually copied will be returned, which is
 *  min(bytes, iov_size(iov)-offset)
 */
size_t iov_from_buf(struct iovec *iov, unsigned int iov_cnt,
                    size_t offset, const void *buf, size_t bytes);
size_t iov_to_buf(const struct iovec *iov, const unsigned int iov_cnt,
                  size_t offset, void *buf, size_t bytes);

/**
 * Set data bytes pointed out by iovec `iov' of size `iov_cnt' elements,
 * starting at byte offset `start', to value `fillc', repeating it
 * `bytes' number of times.
 * If `bytes' is large enough, only last bytes portion of iovec,
 * up to the end of it, will be filled with the specified value.
 * Function return actual number of bytes processed, which is
 * min(size, iov_size(iov) - offset).
 */
size_t iov_memset(const struct iovec *iov, const unsigned int iov_cnt,
                  size_t offset, int fillc, size_t bytes);

/**
 * Produce a text hexdump of iovec `iov' with `iov_cnt' number of elements
 * in file `fp', prefixing each line with `prefix' and processing not more
 * than `limit' data bytes.
 */
void iov_hexdump(const struct iovec *iov, const unsigned int iov_cnt,
                 FILE *fp, const char *prefix, size_t limit);
