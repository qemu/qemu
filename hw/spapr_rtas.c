/*
 * QEMU PowerPC pSeries Logical Partition (aka sPAPR) hardware System Emulator
 *
 * Hypercall based emulated RTAS
 *
 * Copyright (c) 2010-2011 David Gibson, IBM Corporation.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */
#include "cpu.h"
#include "sysemu.h"
#include "qemu-char.h"
#include "hw/qdev.h"
#include "device_tree.h"

#include "hw/spapr.h"
#include "hw/spapr_vio.h"

#include <libfdt.h>

#define TOKEN_BASE      0x2000
#define TOKEN_MAX       0x100

static void rtas_display_character(sPAPREnvironment *spapr,
                                   uint32_t token, uint32_t nargs,
                                   target_ulong args,
                                   uint32_t nret, target_ulong rets)
{
    uint8_t c = rtas_ld(args, 0);
    VIOsPAPRDevice *sdev = spapr_vio_find_by_reg(spapr->vio_bus,
                                                 SPAPR_VTY_BASE_ADDRESS);

    if (!sdev) {
        rtas_st(rets, 0, -1);
    } else {
        vty_putchars(sdev, &c, sizeof(c));
        rtas_st(rets, 0, 0);
    }
}

static void rtas_get_time_of_day(sPAPREnvironment *spapr,
                                 uint32_t token, uint32_t nargs,
                                 target_ulong args,
                                 uint32_t nret, target_ulong rets)
{
    struct tm tm;

    if (nret != 8) {
        rtas_st(rets, 0, -3);
        return;
    }

    qemu_get_timedate(&tm, spapr->rtc_offset);

    rtas_st(rets, 0, 0); /* Success */
    rtas_st(rets, 1, tm.tm_year + 1900);
    rtas_st(rets, 2, tm.tm_mon + 1);
    rtas_st(rets, 3, tm.tm_mday);
    rtas_st(rets, 4, tm.tm_hour);
    rtas_st(rets, 5, tm.tm_min);
    rtas_st(rets, 6, tm.tm_sec);
    rtas_st(rets, 7, 0); /* we don't do nanoseconds */
}

static void rtas_set_time_of_day(sPAPREnvironment *spapr,
                                 uint32_t token, uint32_t nargs,
                                 target_ulong args,
                                 uint32_t nret, target_ulong rets)
{
    struct tm tm;

    tm.tm_year = rtas_ld(args, 0) - 1900;
    tm.tm_mon = rtas_ld(args, 1) - 1;
    tm.tm_mday = rtas_ld(args, 2);
    tm.tm_hour = rtas_ld(args, 3);
    tm.tm_min = rtas_ld(args, 4);
    tm.tm_sec = rtas_ld(args, 5);

    /* Just generate a monitor event for the change */
    rtc_change_mon_event(&tm);
    spapr->rtc_offset = qemu_timedate_diff(&tm);

    rtas_st(rets, 0, 0); /* Success */
}

static void rtas_power_off(sPAPREnvironment *spapr,
                           uint32_t token, uint32_t nargs, target_ulong args,
                           uint32_t nret, target_ulong rets)
{
    if (nargs != 2 || nret != 1) {
        rtas_st(rets, 0, -3);
        return;
    }
    qemu_system_shutdown_request();
    rtas_st(rets, 0, 0);
}

static void rtas_query_cpu_stopped_state(sPAPREnvironment *spapr,
                                         uint32_t token, uint32_t nargs,
                                         target_ulong args,
                                         uint32_t nret, target_ulong rets)
{
    target_ulong id;
    CPUState *env;

    if (nargs != 1 || nret != 2) {
        rtas_st(rets, 0, -3);
        return;
    }

    id = rtas_ld(args, 0);
    for (env = first_cpu; env; env = env->next_cpu) {
        if (env->cpu_index != id) {
            continue;
        }

        if (env->halted) {
            rtas_st(rets, 1, 0);
        } else {
            rtas_st(rets, 1, 2);
        }

        rtas_st(rets, 0, 0);
        return;
    }

    /* Didn't find a matching cpu */
    rtas_st(rets, 0, -3);
}

static void rtas_start_cpu(sPAPREnvironment *spapr,
                           uint32_t token, uint32_t nargs,
                           target_ulong args,
                           uint32_t nret, target_ulong rets)
{
    target_ulong id, start, r3;
    CPUState *env;

    if (nargs != 3 || nret != 1) {
        rtas_st(rets, 0, -3);
        return;
    }

    id = rtas_ld(args, 0);
    start = rtas_ld(args, 1);
    r3 = rtas_ld(args, 2);

    for (env = first_cpu; env; env = env->next_cpu) {
        if (env->cpu_index != id) {
            continue;
        }

        if (!env->halted) {
            rtas_st(rets, 0, -1);
            return;
        }

        env->msr = (1ULL << MSR_SF) | (1ULL << MSR_ME);
        env->nip = start;
        env->gpr[3] = r3;
        env->halted = 0;

        qemu_cpu_kick(env);

        rtas_st(rets, 0, 0);
        return;
    }

    /* Didn't find a matching cpu */
    rtas_st(rets, 0, -3);
}

static struct rtas_call {
    const char *name;
    spapr_rtas_fn fn;
} rtas_table[TOKEN_MAX];

struct rtas_call *rtas_next = rtas_table;

target_ulong spapr_rtas_call(sPAPREnvironment *spapr,
                             uint32_t token, uint32_t nargs, target_ulong args,
                             uint32_t nret, target_ulong rets)
{
    if ((token >= TOKEN_BASE)
        && ((token - TOKEN_BASE) < TOKEN_MAX)) {
        struct rtas_call *call = rtas_table + (token - TOKEN_BASE);

        if (call->fn) {
            call->fn(spapr, token, nargs, args, nret, rets);
            return H_SUCCESS;
        }
    }

    /* HACK: Some Linux early debug code uses RTAS display-character,
     * but assumes the token value is 0xa (which it is on some real
     * machines) without looking it up in the device tree.  This
     * special case makes this work */
    if (token == 0xa) {
        rtas_display_character(spapr, 0xa, nargs, args, nret, rets);
        return H_SUCCESS;
    }

    hcall_dprintf("Unknown RTAS token 0x%x\n", token);
    rtas_st(rets, 0, -3);
    return H_PARAMETER;
}

void spapr_rtas_register(const char *name, spapr_rtas_fn fn)
{
    assert(rtas_next < (rtas_table + TOKEN_MAX));

    rtas_next->name = name;
    rtas_next->fn = fn;

    rtas_next++;
}

int spapr_rtas_device_tree_setup(void *fdt, target_phys_addr_t rtas_addr,
                                 target_phys_addr_t rtas_size)
{
    int ret;
    int i;

    ret = fdt_add_mem_rsv(fdt, rtas_addr, rtas_size);
    if (ret < 0) {
        fprintf(stderr, "Couldn't add RTAS reserve entry: %s\n",
                fdt_strerror(ret));
        return ret;
    }

    ret = qemu_devtree_setprop_cell(fdt, "/rtas", "linux,rtas-base",
                                    rtas_addr);
    if (ret < 0) {
        fprintf(stderr, "Couldn't add linux,rtas-base property: %s\n",
                fdt_strerror(ret));
        return ret;
    }

    ret = qemu_devtree_setprop_cell(fdt, "/rtas", "linux,rtas-entry",
                                    rtas_addr);
    if (ret < 0) {
        fprintf(stderr, "Couldn't add linux,rtas-entry property: %s\n",
                fdt_strerror(ret));
        return ret;
    }

    ret = qemu_devtree_setprop_cell(fdt, "/rtas", "rtas-size",
                                    rtas_size);
    if (ret < 0) {
        fprintf(stderr, "Couldn't add rtas-size property: %s\n",
                fdt_strerror(ret));
        return ret;
    }

    for (i = 0; i < TOKEN_MAX; i++) {
        struct rtas_call *call = &rtas_table[i];

        if (!call->fn) {
            continue;
        }

        ret = qemu_devtree_setprop_cell(fdt, "/rtas", call->name,
                                        i + TOKEN_BASE);
        if (ret < 0) {
            fprintf(stderr, "Couldn't add rtas token for %s: %s\n",
                    call->name, fdt_strerror(ret));
            return ret;
        }

    }
    return 0;
}

static void register_core_rtas(void)
{
    spapr_rtas_register("display-character", rtas_display_character);
    spapr_rtas_register("get-time-of-day", rtas_get_time_of_day);
    spapr_rtas_register("set-time-of-day", rtas_set_time_of_day);
    spapr_rtas_register("power-off", rtas_power_off);
    spapr_rtas_register("query-cpu-stopped-state",
                        rtas_query_cpu_stopped_state);
    spapr_rtas_register("start-cpu", rtas_start_cpu);
}
device_init(register_core_rtas);
