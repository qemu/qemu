/* SPDX-License-Identifier: MIT */
/*
 * Pixman stride and buffer size helpers.
 * Expects PIXMAN_FORMAT_BPP() and pixman_format_code_t to be
 * already defined (by either <pixman.h> or "pixman-minimal.h").
 */

#ifndef QEMU_PIXMAN_HELPERS_H
#define QEMU_PIXMAN_HELPERS_H

/*
 * Compute the row stride for a pixman image, aligned to sizeof(uint32_t),
 * as pixman does. Returns -1 on integer overflow.
 */
static inline int qemu_pixman_stride(pixman_format_code_t format, int width)
{
    int stride;

    if (unlikely(__builtin_mul_overflow(width, PIXMAN_FORMAT_BPP(format),
                                        &stride)) ||
        unlikely(__builtin_add_overflow(stride, 31, &stride))) {
        return -1;
    }
    return (stride / 32) * sizeof(uint32_t);
}

/*
 * Compute stride and buffer size for a pixman image.
 * If *rowstride_bytes is 0, compute it from format and width
 * (aligned to sizeof(uint32_t), as pixman does).
 * Returns false on integer overflow.
 */
static inline bool qemu_pixman_image_calc_size(pixman_format_code_t format,
                                               int width, int height,
                                               int *rowstride_bytes,
                                               size_t *buf_size)
{
    if (!*rowstride_bytes) {
        *rowstride_bytes = qemu_pixman_stride(format, width);
        if (*rowstride_bytes < 0) {
            return false;
        }
    }

    return likely(!__builtin_mul_overflow((size_t)height,
                                          (size_t)*rowstride_bytes, buf_size));
}

#endif /* QEMU_PIXMAN_HELPERS_H */
