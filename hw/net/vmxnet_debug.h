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

#define VMXNET_DEBUG_WARNINGS
#define VMXNET_DEBUG_ERRORS

#undef VMXNET_DEBUG_CB
#undef VMXNET_DEBUG_INTERRUPTS
#undef VMXNET_DEBUG_CONFIG
#undef VMXNET_DEBUG_RINGS
#undef VMXNET_DEBUG_PACKETS
#undef VMXNET_DEBUG_SHMEM_ACCESS

#ifdef VMXNET_DEBUG_CB
#  define VMXNET_DEBUG_CB_ENABLED 1
#else
#  define VMXNET_DEBUG_CB_ENABLED 0
#endif

#ifdef VMXNET_DEBUG_WARNINGS
#  define VMXNET_DEBUG_WARNINGS_ENABLED 1
#else
#  define VMXNET_DEBUG_WARNINGS_ENABLED 0
#endif

#ifdef VMXNET_DEBUG_ERRORS
#  define VMXNET_DEBUG_ERRORS_ENABLED 1
#else
#  define VMXNET_DEBUG_ERRORS_ENABLED 0
#endif

#ifdef VMXNET_DEBUG_CONFIG
#  define VMXNET_DEBUG_CONFIG_ENABLED 1
#else
#  define VMXNET_DEBUG_CONFIG_ENABLED 0
#endif

#ifdef VMXNET_DEBUG_RINGS
#  define VMXNET_DEBUG_RINGS_ENABLED 1
#else
#  define VMXNET_DEBUG_RINGS_ENABLED 0
#endif

#ifdef VMXNET_DEBUG_PACKETS
#  define VMXNET_DEBUG_PACKETS_ENABLED 1
#else
#  define VMXNET_DEBUG_PACKETS_ENABLED 0
#endif

#ifdef VMXNET_DEBUG_INTERRUPTS
#  define VMXNET_DEBUG_INTERRUPTS_ENABLED 1
#else
#  define VMXNET_DEBUG_INTERRUPTS_ENABLED 0
#endif

#ifdef VMXNET_DEBUG_SHMEM_ACCESS
#  define VMXNET_DEBUG_SHMEM_ACCESS_ENABLED 1
#else
#  define VMXNET_DEBUG_SHMEM_ACCESS_ENABLED 0
#endif

#define VMW_SHPRN(fmt, ...)                                                   \
    do {                                                                      \
        if (VMXNET_DEBUG_SHMEM_ACCESS_ENABLED) {                              \
            printf("[%s][SH][%s]: " fmt "\n", VMXNET_DEVICE_NAME, __func__,   \
                ## __VA_ARGS__);                                              \
       }                                                                      \
    } while (0)

#define VMW_CBPRN(fmt, ...)                                                   \
    do {                                                                      \
        if (VMXNET_DEBUG_CB_ENABLED) {                                        \
            printf("[%s][CB][%s]: " fmt "\n", VMXNET_DEVICE_NAME, __func__,   \
                ## __VA_ARGS__);                                              \
        }                                                                     \
    } while (0)

#define VMW_PKPRN(fmt, ...)                                                   \
    do {                                                                      \
        if (VMXNET_DEBUG_PACKETS_ENABLED) {                                   \
            printf("[%s][PK][%s]: " fmt "\n", VMXNET_DEVICE_NAME, __func__,   \
                ## __VA_ARGS__);                                              \
        }                                                                     \
    } while (0)

#define VMW_WRPRN(fmt, ...)                                                   \
    do {                                                                      \
        if (VMXNET_DEBUG_WARNINGS_ENABLED) {                                  \
            printf("[%s][WR][%s]: " fmt "\n", VMXNET_DEVICE_NAME, __func__,   \
                ## __VA_ARGS__);                                              \
        }                                                                     \
    } while (0)

#define VMW_ERPRN(fmt, ...)                                                   \
    do {                                                                      \
        if (VMXNET_DEBUG_ERRORS_ENABLED) {                                    \
            printf("[%s][ER][%s]: " fmt "\n", VMXNET_DEVICE_NAME, __func__,   \
                ## __VA_ARGS__);                                              \
        }                                                                     \
    } while (0)

#define VMW_IRPRN(fmt, ...)                                                   \
    do {                                                                      \
        if (VMXNET_DEBUG_INTERRUPTS_ENABLED) {                                \
            printf("[%s][IR][%s]: " fmt "\n", VMXNET_DEVICE_NAME, __func__,   \
                ## __VA_ARGS__);                                              \
        }                                                                     \
    } while (0)

#define VMW_CFPRN(fmt, ...)                                                   \
    do {                                                                      \
        if (VMXNET_DEBUG_CONFIG_ENABLED) {                                    \
            printf("[%s][CF][%s]: " fmt "\n", VMXNET_DEVICE_NAME, __func__,   \
                ## __VA_ARGS__);                                              \
        }                                                                     \
    } while (0)

#define VMW_RIPRN(fmt, ...)                                                   \
    do {                                                                      \
        if (VMXNET_DEBUG_RINGS_ENABLED) {                                     \
            printf("[%s][RI][%s]: " fmt "\n", VMXNET_DEVICE_NAME, __func__,   \
                ## __VA_ARGS__);                                              \
        }                                                                     \
    } while (0)

#define VMXNET_MF       "%02X:%02X:%02X:%02X:%02X:%02X"
#define VMXNET_MA(a)    (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]

#endif /* _QEMU_VMXNET3_DEBUG_H  */
