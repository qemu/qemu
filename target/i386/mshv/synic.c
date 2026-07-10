/*
 * QEMU MSHV SynIC support
 *
 * Copyright Microsoft, Corp. 2026
 *
 * Authors: Magnus Kulke  <magnuskulke@microsoft.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/memalign.h"
#include "qemu/error-report.h"

#include "system/mshv.h"
#include "system/mshv_int.h"

#include "linux/mshv.h"
#include "hw/hyperv/hvgdk_mini.h"
#include "cpu.h"

#include <sys/ioctl.h>

bool mshv_synic_enabled(const CPUState *cpu)
{
    X86CPU *x86cpu = X86_CPU(cpu);

    return x86cpu->env.msr_hv_synic_control & 1;
}

static int get_vp_state(int cpu_fd, struct mshv_get_set_vp_state *state)
{
    int ret;

    ret = ioctl(cpu_fd, MSHV_GET_VP_STATE, state);
    if (ret < 0) {
        error_report("failed to get vp state: %s", strerror(errno));
        return -1;
    }

    return 0;
}

static int set_vp_state(int cpu_fd, const struct mshv_get_set_vp_state *state)
{
    int ret;

    ret = ioctl(cpu_fd, MSHV_SET_VP_STATE, state);
    if (ret < 0) {
        error_report("failed to set vp state: %s", strerror(errno));
        return -1;
    }

    return 0;
}

int mshv_get_simp(int cpu_fd, uint8_t *page)
{
    int ret;
    void *buffer;
    struct mshv_get_set_vp_state args = {0};

    buffer = qemu_memalign(HV_HYP_PAGE_SIZE, HV_HYP_PAGE_SIZE);
    args.buf_ptr = (uint64_t)buffer;
    args.buf_sz = HV_HYP_PAGE_SIZE;
    args.type = MSHV_VP_STATE_SIMP;

    ret = get_vp_state(cpu_fd, &args);

    if (ret < 0) {
        qemu_vfree(buffer);
        error_report("failed to get simp");
        return -1;
    }

    memcpy(page, buffer, HV_HYP_PAGE_SIZE);
    qemu_vfree(buffer);

    return 0;
}

int mshv_set_simp(int cpu_fd, const uint8_t *page)
{
    int ret;
    void *buffer;
    struct mshv_get_set_vp_state args = {0};

    buffer = qemu_memalign(HV_HYP_PAGE_SIZE, HV_HYP_PAGE_SIZE);
    args.buf_ptr = (uint64_t)buffer;
    args.buf_sz = HV_HYP_PAGE_SIZE;
    args.type = MSHV_VP_STATE_SIMP;

    assert(page);
    memcpy(buffer, page, HV_HYP_PAGE_SIZE);

    ret = set_vp_state(cpu_fd, &args);
    qemu_vfree(buffer);

    if (ret < 0) {
        error_report("failed to set simp");
        return -1;
    }

    return 0;
}

int mshv_get_siefp(int cpu_fd, uint8_t *page)
{
    int ret;
    void *buffer;
    struct mshv_get_set_vp_state args = {0};

    buffer = qemu_memalign(HV_HYP_PAGE_SIZE, HV_HYP_PAGE_SIZE);
    args.buf_ptr = (uint64_t)buffer;
    args.buf_sz = HV_HYP_PAGE_SIZE;
    args.type = MSHV_VP_STATE_SIEFP,

    ret = get_vp_state(cpu_fd, &args);

    if (ret < 0) {
        qemu_vfree(buffer);
        error_report("failed to get siefp");
        return -1;
    }

    memcpy(page, buffer, HV_HYP_PAGE_SIZE);
    qemu_vfree(buffer);

    return 0;
}

int mshv_set_siefp(int cpu_fd, const uint8_t *page)
{
    int ret;
    void *buffer;
    struct mshv_get_set_vp_state args = {0};

    buffer = qemu_memalign(HV_HYP_PAGE_SIZE, HV_HYP_PAGE_SIZE);
    args.buf_ptr = (uint64_t)buffer;
    args.buf_sz = HV_HYP_PAGE_SIZE;
    args.type = MSHV_VP_STATE_SIEFP,

    assert(page);
    memcpy(buffer, page, HV_HYP_PAGE_SIZE);

    ret = set_vp_state(cpu_fd, &args);
    qemu_vfree(buffer);

    if (ret < 0) {
        error_report("failed to set simp");
        return -1;
    }

    return 0;
}
