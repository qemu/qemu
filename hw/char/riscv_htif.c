/*
 * QEMU RISC-V Host Target Interface (HTIF) Emulation
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
 *
 * This provides HTIF device emulation for QEMU. At the moment this allows
 * for identical copies of bbl/linux to run on both spike and QEMU.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "hw/sysbus.h"
#include "hw/char/riscv_htif.h"
#include "hw/char/serial.h"
#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "qemu/timer.h"
#include "qemu/error-report.h"

#define RISCV_DEBUG_HTIF 0
#define HTIF_DEBUG(fmt, ...)                                                   \
    do {                                                                       \
        if (RISCV_DEBUG_HTIF) {                                                \
            qemu_log_mask(LOG_TRACE, "%s: " fmt "\n", __func__, ##__VA_ARGS__);\
        }                                                                      \
    } while (0)

static uint64_t fromhost_addr, tohost_addr;
static int address_symbol_set;

void htif_symbol_callback(const char *st_name, int st_info, uint64_t st_value,
                          uint64_t st_size)
{
    if (strcmp("fromhost", st_name) == 0) {
        address_symbol_set |= 1;
        fromhost_addr = st_value;
        if (st_size != 8) {
            error_report("HTIF fromhost must be 8 bytes");
            exit(1);
        }
    } else if (strcmp("tohost", st_name) == 0) {
        address_symbol_set |= 2;
        tohost_addr = st_value;
        if (st_size != 8) {
            error_report("HTIF tohost must be 8 bytes");
            exit(1);
        }
    }
}

/*
 * Called by the char dev to see if HTIF is ready to accept input.
 */
static int htif_can_recv(void *opaque)
{
    return 1;
}

/*
 * Called by the char dev to supply input to HTIF console.
 * We assume that we will receive one character at a time.
 */
static void htif_recv(void *opaque, const uint8_t *buf, int size)
{
    HTIFState *htifstate = opaque;

    if (size != 1) {
        return;
    }

    /* TODO - we need to check whether mfromhost is zero which indicates
              the device is ready to receive. The current implementation
              will drop characters */

    uint64_t val_written = htifstate->pending_read;
    uint64_t resp = 0x100 | *buf;

    htifstate->env->mfromhost = (val_written >> 48 << 48) | (resp << 16 >> 16);
}

/*
 * Called by the char dev to supply special events to the HTIF console.
 * Not used for HTIF.
 */
static void htif_event(void *opaque, QEMUChrEvent event)
{

}

static int htif_be_change(void *opaque)
{
    HTIFState *s = opaque;

    qemu_chr_fe_set_handlers(&s->chr, htif_can_recv, htif_recv, htif_event,
        htif_be_change, s, NULL, true);

    return 0;
}

static void htif_handle_tohost_write(HTIFState *htifstate, uint64_t val_written)
{
    uint8_t device = val_written >> 56;
    uint8_t cmd = val_written >> 48;
    uint64_t payload = val_written & 0xFFFFFFFFFFFFULL;
    int resp = 0;

    HTIF_DEBUG("mtohost write: device: %d cmd: %d what: %02" PRIx64
        " -payload: %016" PRIx64 "\n", device, cmd, payload & 0xFF, payload);

    /*
     * Currently, there is a fixed mapping of devices:
     * 0: riscv-tests Pass/Fail Reporting Only (no syscall proxy)
     * 1: Console
     */
    if (unlikely(device == 0x0)) {
        /* frontend syscall handler, shutdown and exit code support */
        if (cmd == 0x0) {
            if (payload & 0x1) {
                /* exit code */
                int exit_code = payload >> 1;
                exit(exit_code);
            } else {
                qemu_log_mask(LOG_UNIMP, "pk syscall proxy not supported\n");
            }
        } else {
            qemu_log("HTIF device %d: unknown command\n", device);
        }
    } else if (likely(device == 0x1)) {
        /* HTIF Console */
        if (cmd == 0x0) {
            /* this should be a queue, but not yet implemented as such */
            htifstate->pending_read = val_written;
            htifstate->env->mtohost = 0; /* clear to indicate we read */
            return;
        } else if (cmd == 0x1) {
            qemu_chr_fe_write(&htifstate->chr, (uint8_t *)&payload, 1);
            resp = 0x100 | (uint8_t)payload;
        } else {
            qemu_log("HTIF device %d: unknown command\n", device);
        }
    } else {
        qemu_log("HTIF unknown device or command\n");
        HTIF_DEBUG("device: %d cmd: %d what: %02" PRIx64
            " payload: %016" PRIx64, device, cmd, payload & 0xFF, payload);
    }
    /*
     * - latest bbl does not set fromhost to 0 if there is a value in tohost
     * - with this code enabled, qemu hangs waiting for fromhost to go to 0
     * - with this code disabled, qemu works with bbl priv v1.9.1 and v1.10
     * - HTIF needs protocol documentation and a more complete state machine

        while (!htifstate->fromhost_inprogress &&
            htifstate->env->mfromhost != 0x0) {
        }
    */
    htifstate->env->mfromhost = (val_written >> 48 << 48) | (resp << 16 >> 16);
    htifstate->env->mtohost = 0; /* clear to indicate we read */
}

#define TOHOST_OFFSET1 (htifstate->tohost_offset)
#define TOHOST_OFFSET2 (htifstate->tohost_offset + 4)
#define FROMHOST_OFFSET1 (htifstate->fromhost_offset)
#define FROMHOST_OFFSET2 (htifstate->fromhost_offset + 4)

/* CPU wants to read an HTIF register */
static uint64_t htif_mm_read(void *opaque, hwaddr addr, unsigned size)
{
    HTIFState *htifstate = opaque;
    if (addr == TOHOST_OFFSET1) {
        return htifstate->env->mtohost & 0xFFFFFFFF;
    } else if (addr == TOHOST_OFFSET2) {
        return (htifstate->env->mtohost >> 32) & 0xFFFFFFFF;
    } else if (addr == FROMHOST_OFFSET1) {
        return htifstate->env->mfromhost & 0xFFFFFFFF;
    } else if (addr == FROMHOST_OFFSET2) {
        return (htifstate->env->mfromhost >> 32) & 0xFFFFFFFF;
    } else {
        qemu_log("Invalid htif read: address %016" PRIx64 "\n",
            (uint64_t)addr);
        return 0;
    }
}

/* CPU wrote to an HTIF register */
static void htif_mm_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned size)
{
    HTIFState *htifstate = opaque;
    if (addr == TOHOST_OFFSET1) {
        if (htifstate->env->mtohost == 0x0) {
            htifstate->allow_tohost = 1;
            htifstate->env->mtohost = value & 0xFFFFFFFF;
        } else {
            htifstate->allow_tohost = 0;
        }
    } else if (addr == TOHOST_OFFSET2) {
        if (htifstate->allow_tohost) {
            htifstate->env->mtohost |= value << 32;
            htif_handle_tohost_write(htifstate, htifstate->env->mtohost);
        }
    } else if (addr == FROMHOST_OFFSET1) {
        htifstate->fromhost_inprogress = 1;
        htifstate->env->mfromhost = value & 0xFFFFFFFF;
    } else if (addr == FROMHOST_OFFSET2) {
        htifstate->env->mfromhost |= value << 32;
        htifstate->fromhost_inprogress = 0;
    } else {
        qemu_log("Invalid htif write: address %016" PRIx64 "\n",
            (uint64_t)addr);
    }
}

static const MemoryRegionOps htif_mm_ops = {
    .read = htif_mm_read,
    .write = htif_mm_write,
};

HTIFState *htif_mm_init(MemoryRegion *address_space, MemoryRegion *main_mem,
    CPURISCVState *env, Chardev *chr)
{
    uint64_t base = MIN(tohost_addr, fromhost_addr);
    uint64_t size = MAX(tohost_addr + 8, fromhost_addr + 8) - base;
    uint64_t tohost_offset = tohost_addr - base;
    uint64_t fromhost_offset = fromhost_addr - base;

    HTIFState *s = g_malloc0(sizeof(HTIFState));
    s->address_space = address_space;
    s->main_mem = main_mem;
    s->main_mem_ram_ptr = memory_region_get_ram_ptr(main_mem);
    s->env = env;
    s->tohost_offset = tohost_offset;
    s->fromhost_offset = fromhost_offset;
    s->pending_read = 0;
    s->allow_tohost = 0;
    s->fromhost_inprogress = 0;
    qemu_chr_fe_init(&s->chr, chr, &error_abort);
    qemu_chr_fe_set_handlers(&s->chr, htif_can_recv, htif_recv, htif_event,
        htif_be_change, s, NULL, true);
    if (address_symbol_set == 3) {
        memory_region_init_io(&s->mmio, NULL, &htif_mm_ops, s,
                              TYPE_HTIF_UART, size);
        memory_region_add_subregion_overlap(address_space, base,
                                            &s->mmio, 1);
    }

    return s;
}
