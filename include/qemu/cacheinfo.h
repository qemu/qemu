/*
 * QEMU host cacheinfo information
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef QEMU_CACHEINFO_H
#define QEMU_CACHEINFO_H

/*
 * These variables represent our best guess at the host icache and
 * dcache sizes, expressed both as the size in bytes and as the
 * base-2 log of the size in bytes. They are initialized at startup
 * (via an attribute 'constructor' function).
 */
extern int qemu_icache_linesize;
extern int qemu_icache_linesize_log;
extern int qemu_dcache_linesize;
extern int qemu_dcache_linesize_log;

#endif
