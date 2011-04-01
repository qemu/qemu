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
