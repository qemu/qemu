/*
 * ARM implementation of KVM and HVF hooks, 64 bit specific code
 *
 * Copyright Mian-M. Hamayun 2013, Virtual Open Systems
 * Copyright Alex Bennée 2014, Linaro
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "gdbstub/enums.h"

/* Maximum and current break/watch point counts */
int max_hw_bps, max_hw_wps;
GArray *hw_breakpoints, *hw_watchpoints;

/**
 * insert_hw_breakpoint()
 * @addr: address of breakpoint
 *
 * See ARM ARM D2.9.1 for details but here we are only going to create
 * simple un-linked breakpoints (i.e. we don't chain breakpoints
 * together to match address and context or vmid). The hardware is
 * capable of fancier matching but that will require exposing that
 * fanciness to GDB's interface
 *
 * DBGBCR<n>_EL1, Debug Breakpoint Control Registers
 *
 *  31  24 23  20 19   16 15 14  13  12   9 8   5 4    3 2   1  0
 * +------+------+-------+-----+----+------+-----+------+-----+---+
 * | RES0 |  BT  |  LBN  | SSC | HMC| RES0 | BAS | RES0 | PMC | E |
 * +------+------+-------+-----+----+------+-----+------+-----+---+
 *
 * BT: Breakpoint type (0 = unlinked address match)
 * LBN: Linked BP number (0 = unused)
 * SSC/HMC/PMC: Security, Higher and Priv access control (Table D-12)
 * BAS: Byte Address Select (RES1 for AArch64)
 * E: Enable bit
 *
 * DBGBVR<n>_EL1, Debug Breakpoint Value Registers
 *
 *  63  53 52       49 48       2  1 0
 * +------+-----------+----------+-----+
 * | RESS | VA[52:49] | VA[48:2] | 0 0 |
 * +------+-----------+----------+-----+
 *
 * Depending on the addressing mode bits the top bits of the register
 * are a sign extension of the highest applicable VA bit. Some
 * versions of GDB don't do it correctly so we ensure they are correct
 * here so future PC comparisons will work properly.
 */

int insert_hw_breakpoint(vaddr addr)
{
    HWBreakpoint brk = {
        .bcr = 0x1,                             /* BCR E=1, enable */
        .bvr = sextract64(addr, 0, 53)
    };

    if (cur_hw_bps >= max_hw_bps) {
        return -ENOBUFS;
    }

    brk.bcr = deposit32(brk.bcr, 1, 2, 0x3);   /* PMC = 11 */
    brk.bcr = deposit32(brk.bcr, 5, 4, 0xf);   /* BAS = RES1 */

    g_array_append_val(hw_breakpoints, brk);

    return 0;
}

/**
 * delete_hw_breakpoint()
 * @pc: address of breakpoint
 *
 * Delete a breakpoint and shuffle any above down
 */

int delete_hw_breakpoint(vaddr pc)
{
    int i;
    for (i = 0; i < hw_breakpoints->len; i++) {
        HWBreakpoint *brk = get_hw_bp(i);
        if (brk->bvr == pc) {
            g_array_remove_index(hw_breakpoints, i);
            return 0;
        }
    }
    return -ENOENT;
}

/**
 * insert_hw_watchpoint()
 * @addr: address of watch point
 * @len: size of area
 * @type: type of watch point
 *
 * See ARM ARM D2.10. As with the breakpoints we can do some advanced
 * stuff if we want to. The watch points can be linked with the break
 * points above to make them context aware. However for simplicity
 * currently we only deal with simple read/write watch points.
 *
 * D7.3.11 DBGWCR<n>_EL1, Debug Watchpoint Control Registers
 *
 *  31  29 28   24 23  21  20  19 16 15 14  13   12  5 4   3 2   1  0
 * +------+-------+------+----+-----+-----+-----+-----+-----+-----+---+
 * | RES0 |  MASK | RES0 | WT | LBN | SSC | HMC | BAS | LSC | PAC | E |
 * +------+-------+------+----+-----+-----+-----+-----+-----+-----+---+
 *
 * MASK: num bits addr mask (0=none,01/10=res,11=3 bits (8 bytes))
 * WT: 0 - unlinked, 1 - linked (not currently used)
 * LBN: Linked BP number (not currently used)
 * SSC/HMC/PAC: Security, Higher and Priv access control (Table D2-11)
 * BAS: Byte Address Select
 * LSC: Load/Store control (01: load, 10: store, 11: both)
 * E: Enable
 *
 * The bottom 2 bits of the value register are masked. Therefore to
 * break on any sizes smaller than an unaligned word you need to set
 * MASK=0, BAS=bit per byte in question. For larger regions (^2) you
 * need to ensure you mask the address as required and set BAS=0xff
 */

int insert_hw_watchpoint(vaddr addr, vaddr len, int type)
{
    HWWatchpoint wp = {
        .wcr = R_DBGWCR_E_MASK, /* E=1, enable */
        .wvr = addr & (~0x7ULL),
        .details = { .vaddr = addr, .len = len }
    };

    if (cur_hw_wps >= max_hw_wps) {
        return -ENOBUFS;
    }

    /*
     * HMC=0 SSC=0 PAC=3 will hit EL0 or EL1, any security state,
     * valid whether EL3 is implemented or not
     */
    wp.wcr = FIELD_DP64(wp.wcr, DBGWCR, PAC, 3);

    switch (type) {
    case GDB_WATCHPOINT_READ:
        wp.wcr = FIELD_DP64(wp.wcr, DBGWCR, LSC, 1);
        wp.details.flags = BP_MEM_READ;
        break;
    case GDB_WATCHPOINT_WRITE:
        wp.wcr = FIELD_DP64(wp.wcr, DBGWCR, LSC, 2);
        wp.details.flags = BP_MEM_WRITE;
        break;
    case GDB_WATCHPOINT_ACCESS:
        wp.wcr = FIELD_DP64(wp.wcr, DBGWCR, LSC, 3);
        wp.details.flags = BP_MEM_ACCESS;
        break;
    default:
        g_assert_not_reached();
    }
    if (len <= 8) {
        /* we align the address and set the bits in BAS */
        int off = addr & 0x7;
        int bas = (1 << len) - 1;

        wp.wcr = deposit32(wp.wcr, 5 + off, 8 - off, bas);
    } else {
        /* For ranges above 8 bytes we need to be a power of 2 */
        if (is_power_of_2(len)) {
            int bits = ctz64(len);

            wp.wvr &= ~((1 << bits) - 1);
            wp.wcr = FIELD_DP64(wp.wcr, DBGWCR, MASK, bits);
            wp.wcr = FIELD_DP64(wp.wcr, DBGWCR, BAS, 0xff);
        } else {
            return -ENOBUFS;
        }
    }

    g_array_append_val(hw_watchpoints, wp);
    return 0;
}

bool check_watchpoint_in_range(int i, vaddr addr)
{
    HWWatchpoint *wp = get_hw_wp(i);
    uint64_t addr_top, addr_bottom = wp->wvr;
    int bas = extract32(wp->wcr, 5, 8);
    int mask = extract32(wp->wcr, 24, 4);

    if (mask) {
        addr_top = addr_bottom + (1 << mask);
    } else {
        /*
         * BAS must be contiguous but can offset against the base
         * address in DBGWVR
         */
        addr_bottom = addr_bottom + ctz32(bas);
        addr_top = addr_bottom + clo32(bas);
    }

    if (addr >= addr_bottom && addr <= addr_top) {
        return true;
    }

    return false;
}

/**
 * delete_hw_watchpoint()
 * @addr: address of breakpoint
 *
 * Delete a breakpoint and shuffle any above down
 */

int delete_hw_watchpoint(vaddr addr, vaddr len, int type)
{
    int i;
    for (i = 0; i < cur_hw_wps; i++) {
        if (check_watchpoint_in_range(i, addr)) {
            g_array_remove_index(hw_watchpoints, i);
            return 0;
        }
    }
    return -ENOENT;
}

bool find_hw_breakpoint(CPUState *cpu, vaddr pc)
{
    int i;

    for (i = 0; i < cur_hw_bps; i++) {
        HWBreakpoint *bp = get_hw_bp(i);
        if (bp->bvr == pc) {
            return true;
        }
    }
    return false;
}

CPUWatchpoint *find_hw_watchpoint(CPUState *cpu, vaddr addr)
{
    int i;

    for (i = 0; i < cur_hw_wps; i++) {
        if (check_watchpoint_in_range(i, addr)) {
            return &get_hw_wp(i)->details;
        }
    }
    return NULL;
}
