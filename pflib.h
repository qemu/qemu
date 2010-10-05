#ifndef __QEMU_PFLIB_H
#define __QEMU_PFLIB_H

/*
 * PixelFormat conversion library.
 *
 * Author: Gerd Hoffmann <kraxel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

typedef struct QemuPfConv QemuPfConv;

QemuPfConv *qemu_pf_conv_get(PixelFormat *dst, PixelFormat *src);
void qemu_pf_conv_run(QemuPfConv *conv, void *dst, void *src, uint32_t cnt);
void qemu_pf_conv_put(QemuPfConv *conv);

#endif
