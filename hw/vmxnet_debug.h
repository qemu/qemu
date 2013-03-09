/*
 * QEMU VMWARE VMXNET* paravirtual NICs - debugging facilities
 *
 * Copyright (c) 2012 Ravello Systems LTD (http://ravellosystems.com)
 *
 * Developed by Daynix Computing LTD (http://www.daynix.com)
 *
 * Authors:
 * Dmitry Fleytman <dmitry@daynix.com>
 * Tamir Shomer <tamirs@daynix.com>
 * Yan Vugenfirer <yan@daynix.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef _QEMU_VMXNET_DEBUG_H
#define _QEMU_VMXNET_DEBUG_H

#define VMXNET_DEVICE_NAME "vmxnet3"

/* #define VMXNET_DEBUG_CB */
#define VMXNET_DEBUG_WARNINGS
#define VMXNET_DEBUG_ERRORS
/* #define VMXNET_DEBUG_INTERRUPTS */
/* #define VMXNET_DEBUG_CONFIG */
/* #define VMXNET_DEBUG_RINGS */
/* #define VMXNET_DEBUG_PACKETS */
/* #define VMXNET_DEBUG_SHMEM_ACCESS */

#ifdef VMXNET_DEBUG_SHMEM_ACCESS
#define VMW_SHPRN(fmt, ...)                                                   \
    do {                                                                      \
        printf("[%s][SH][%s]: " fmt "\n", VMXNET_DEVICE_NAME, __func__,       \
            ## __VA_ARGS__);                                                  \
    } while (0)
#else
#define VMW_SHPRN(fmt, ...) do {} while (0)
#endif

#ifdef VMXNET_DEBUG_CB
#define VMW_CBPRN(fmt, ...)                                                   \
    do {                                                                      \
        printf("[%s][CB][%s]: " fmt "\n", VMXNET_DEVICE_NAME, __func__,       \
            ## __VA_ARGS__);                                                  \
    } while (0)
#else
#define VMW_CBPRN(fmt, ...) do {} while (0)
#endif

#ifdef VMXNET_DEBUG_PACKETS
#define VMW_PKPRN(fmt, ...)                                                   \
    do {                                                                      \
        printf("[%s][PK][%s]: " fmt "\n", VMXNET_DEVICE_NAME, __func__,       \
            ## __VA_ARGS__);                                                  \
    } while (0)
#else
#define VMW_PKPRN(fmt, ...) do {} while (0)
#endif

#ifdef VMXNET_DEBUG_WARNINGS
#define VMW_WRPRN(fmt, ...)                                                   \
    do {                                                                      \
        printf("[%s][WR][%s]: " fmt "\n", VMXNET_DEVICE_NAME, __func__,       \
            ## __VA_ARGS__);                                                  \
    } while (0)
#else
#define VMW_WRPRN(fmt, ...) do {} while (0)
#endif

#ifdef VMXNET_DEBUG_ERRORS
#define VMW_ERPRN(fmt, ...)                                                   \
    do {                                                                      \
        printf("[%s][ER][%s]: " fmt "\n", VMXNET_DEVICE_NAME, __func__,       \
            ## __VA_ARGS__);                                                  \
    } while (0)
#else
#define VMW_ERPRN(fmt, ...) do {} while (0)
#endif

#ifdef VMXNET_DEBUG_INTERRUPTS
#define VMW_IRPRN(fmt, ...)                                                   \
    do {                                                                      \
        printf("[%s][IR][%s]: " fmt "\n", VMXNET_DEVICE_NAME, __func__,       \
            ## __VA_ARGS__);                                                  \
    } while (0)
#else
#define VMW_IRPRN(fmt, ...) do {} while (0)
#endif

#ifdef VMXNET_DEBUG_CONFIG
#define VMW_CFPRN(fmt, ...)                                                   \
    do {                                                                      \
        printf("[%s][CF][%s]: " fmt "\n", VMXNET_DEVICE_NAME, __func__,       \
            ## __VA_ARGS__);                                                  \
    } while (0)
#else
#define VMW_CFPRN(fmt, ...) do {} while (0)
#endif

#ifdef VMXNET_DEBUG_RINGS
#define VMW_RIPRN(fmt, ...)                                                   \
    do {                                                                      \
        printf("[%s][RI][%s]: " fmt "\n", VMXNET_DEVICE_NAME, __func__,       \
            ## __VA_ARGS__);                                                  \
    } while (0)
#else
#define VMW_RIPRN(fmt, ...) do {} while (0)
#endif

#define VMXNET_MF       "%02X:%02X:%02X:%02X:%02X:%02X"
#define VMXNET_MA(a)    (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]

#endif /* _QEMU_VMXNET3_DEBUG_H  */
