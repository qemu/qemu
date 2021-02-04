/*
 * QEMU HAXM support
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * Copyright (c) 2011 Intel Corporation
 *  Written by:
 *  Jiang Yunhong<yunhong.jiang@intel.com>
 *  Xin Xiaohui<xiaohui.xin@intel.com>
 *  Zhang Xiantao<xiantao.zhang@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef TARGET_I386_HAX_WINDOWS_H
#define TARGET_I386_HAX_WINDOWS_H

#include <winioctl.h>
#include <windef.h>

#include "hax-accel-ops.h"

#define HAX_INVALID_FD INVALID_HANDLE_VALUE

static inline void hax_mod_close(struct hax_state *hax)
{
    CloseHandle(hax->fd);
}

static inline void hax_close_fd(hax_fd fd)
{
    CloseHandle(fd);
}

static inline int hax_invalid_fd(hax_fd fd)
{
    return (fd == INVALID_HANDLE_VALUE);
}

#define HAX_DEVICE_TYPE 0x4000

#define HAX_IOCTL_VERSION       CTL_CODE(HAX_DEVICE_TYPE, 0x900, \
                                         METHOD_BUFFERED, FILE_ANY_ACCESS)
#define HAX_IOCTL_CREATE_VM     CTL_CODE(HAX_DEVICE_TYPE, 0x901, \
                                         METHOD_BUFFERED, FILE_ANY_ACCESS)
#define HAX_IOCTL_CAPABILITY    CTL_CODE(HAX_DEVICE_TYPE, 0x910, \
                                         METHOD_BUFFERED, FILE_ANY_ACCESS)

#define HAX_VM_IOCTL_VCPU_CREATE   CTL_CODE(HAX_DEVICE_TYPE, 0x902, \
                                            METHOD_BUFFERED, FILE_ANY_ACCESS)
#define HAX_VM_IOCTL_ALLOC_RAM     CTL_CODE(HAX_DEVICE_TYPE, 0x903, \
                                            METHOD_BUFFERED, FILE_ANY_ACCESS)
#define HAX_VM_IOCTL_SET_RAM       CTL_CODE(HAX_DEVICE_TYPE, 0x904, \
                                            METHOD_BUFFERED, FILE_ANY_ACCESS)
#define HAX_VM_IOCTL_VCPU_DESTROY  CTL_CODE(HAX_DEVICE_TYPE, 0x905, \
                                            METHOD_BUFFERED, FILE_ANY_ACCESS)
#define HAX_VM_IOCTL_ADD_RAMBLOCK  CTL_CODE(HAX_DEVICE_TYPE, 0x913, \
                                            METHOD_BUFFERED, FILE_ANY_ACCESS)

#define HAX_VCPU_IOCTL_RUN      CTL_CODE(HAX_DEVICE_TYPE, 0x906, \
                                         METHOD_BUFFERED, FILE_ANY_ACCESS)
#define HAX_VCPU_IOCTL_SET_MSRS CTL_CODE(HAX_DEVICE_TYPE, 0x907, \
                                         METHOD_BUFFERED, FILE_ANY_ACCESS)
#define HAX_VCPU_IOCTL_GET_MSRS CTL_CODE(HAX_DEVICE_TYPE, 0x908, \
                                         METHOD_BUFFERED, FILE_ANY_ACCESS)
#define HAX_VCPU_IOCTL_SET_FPU  CTL_CODE(HAX_DEVICE_TYPE, 0x909, \
                                         METHOD_BUFFERED, FILE_ANY_ACCESS)
#define HAX_VCPU_IOCTL_GET_FPU  CTL_CODE(HAX_DEVICE_TYPE, 0x90a, \
                                         METHOD_BUFFERED, FILE_ANY_ACCESS)

#define HAX_VCPU_IOCTL_SETUP_TUNNEL  CTL_CODE(HAX_DEVICE_TYPE, 0x90b, \
                                              METHOD_BUFFERED, FILE_ANY_ACCESS)
#define HAX_VCPU_IOCTL_INTERRUPT     CTL_CODE(HAX_DEVICE_TYPE, 0x90c, \
                                              METHOD_BUFFERED, FILE_ANY_ACCESS)
#define HAX_VCPU_SET_REGS            CTL_CODE(HAX_DEVICE_TYPE, 0x90d, \
                                              METHOD_BUFFERED, FILE_ANY_ACCESS)
#define HAX_VCPU_GET_REGS            CTL_CODE(HAX_DEVICE_TYPE, 0x90e, \
                                              METHOD_BUFFERED, FILE_ANY_ACCESS)

#define HAX_VM_IOCTL_NOTIFY_QEMU_VERSION CTL_CODE(HAX_DEVICE_TYPE, 0x910, \
                                                  METHOD_BUFFERED,        \
                                                  FILE_ANY_ACCESS)
#endif /* TARGET_I386_HAX_WINDOWS_H */
