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
#include "hw/char/riscv_htif.h"
#include "hw/char/serial.h"
#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "qemu/timer.h"
#include "qemu/error-report.h"
#include "exec/tswap.h"

#define RISCV_DEBUG_HTIF 0
#define HTIF_DEBUG(fmt, ...)                                                   \
    do {                                                                       \
        if (RISCV_DEBUG_HTIF) {                                                \
            qemu_log_mask(LOG_TRACE, "%s: " fmt "\n", __func__, ##__VA_ARGS__);\
        }                                                                      \
    } while (0)

#define HTIF_DEV_SHIFT          56
#define HTIF_CMD_SHIFT          48

#define HTIF_DEV_SYSTEM         0
#define HTIF_DEV_CONSOLE        1

#define HTIF_SYSTEM_CMD_SYSCALL 0
#define HTIF_CONSOLE_CMD_GETC   0
#define HTIF_CONSOLE_CMD_PUTC   1

/* PK system call number */
#define PK_SYS_WRITE            64

static uint64_t fromhost_addr, tohost_addr;

void htif_symbol_callback(const char *st_name, int st_info, uint64_t st_value,
                          uint64_t st_size)
{
    if (strcmp("fromhost", st_name) == 0) {
        fromhost_addr = st_value;
        if (st_size != 8) {
            error_report("HTIF fromhost must be 8 bytes");
            exit(1);
        }
    } else if (strcmp("tohost", st_name) == 0) {
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
    HTIFState *s = opaque;

    if (size != 1) {
        return;
    }

    /*
     * TODO - we need to check whether mfromhost is zero which indicates
     *        the device is ready to receive. The current implementation
     *        will drop characters
     */

    uint64_t val_written = s->pending_read;
    uint64_t resp = 0x100 | *buf;

    s->fromhost = (val_written >> 48 << 48) | (resp << 16 >> 16);
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

/*
 * See below the tohost register format.
 *
 * Bits 63:56 indicate the "device".
 * Bits 55:48 indicate the "command".
 *
 * Device 0 is the syscall device, which is used to emulate Unixy syscalls.
 * It only implements command 0, which has two subfunctions:
 * - If bit 0 is clear, then bits 47:0 represent a pointer to a struct
 *   describing the syscall.
 * - If bit 1 is set, then bits 47:1 represent an exit code, with a zero
 *   value indicating success and other values indicating failure.
 *
 * Device 1 is the blocking character device.
 * - Command 0 reads a character
 * - Command 1 writes a character from the 8 LSBs of tohost
 *
 * For RV32, the tohost register is zero-extended, so only device=0 and
 * command=0 (i.e. HTIF syscalls/exit codes) are supported.
 */
static void htif_handle_tohost_write(HTIFState *s, uint64_t val_written)
{
    uint8_t device = val_written >> HTIF_DEV_SHIFT;
    uint8_t cmd = val_written >> HTIF_CMD_SHIFT;
    uint64_t payload = val_written & 0xFFFFFFFFFFFFULL;
    int resp = 0;

    HTIF_DEBUG("mtohost write: device: %d cmd: %d what: %02" PRIx64
        " -payload: %016" PRIx64 "\n", device, cmd, payload & 0xFF, payload);

    /*
     * Currently, there is a fixed mapping of devices:
     * 0: riscv-tests Pass/Fail Reporting Only (no syscall proxy)
     * 1: Console
     */
    if (unlikely(device == HTIF_DEV_SYSTEM)) {
        /* frontend syscall handler, shutdown and exit code support */
        if (cmd == HTIF_SYSTEM_CMD_SYSCALL) {
            if (payload & 0x1) {
                /* exit code */
                int exit_code = payload >> 1;
                exit(exit_code);
            } else {
                uint64_t syscall[8];
                cpu_physical_memory_read(payload, syscall, sizeof(syscall));
                if (tswap64(syscall[0]) == PK_SYS_WRITE &&
                    tswap64(syscall[1]) == HTIF_DEV_CONSOLE &&
                    tswap64(syscall[3]) == HTIF_CONSOLE_CMD_PUTC) {
                    uint8_t ch;
                    cpu_physical_memory_read(tswap64(syscall[2]), &ch, 1);
                    qemu_chr_fe_write(&s->chr, &ch, 1);
                    resp = 0x100 | (uint8_t)payload;
                } else {
                    qemu_log_mask(LOG_UNIMP,
                                  "pk syscall proxy not supported\n");
                }
            }
        } else {
            qemu_log("HTIF device %d: unknown command\n", device);
        }
    } else if (likely(device == HTIF_DEV_CONSOLE)) {
        /* HTIF Console */
        if (cmd == HTIF_CONSOLE_CMD_GETC) {
            /* this should be a queue, but not yet implemented as such */
            s->pending_read = val_written;
            s->tohost = 0; /* clear to indicate we read */
            return;
        } else if (cmd == HTIF_CONSOLE_CMD_PUTC) {
            uint8_t ch = (uint8_t)payload;
            qemu_chr_fe_write(&s->chr, &ch, 1);
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
     * Latest bbl does not set fromhost to 0 if there is a value in tohost.
     * With this code enabled, qemu hangs waiting for fromhost to go to 0.
     * With this code disabled, qemu works with bbl priv v1.9.1 and v1.10.
     * HTIF needs protocol documentation and a more complete state machine.
     *
     *  while (!s->fromhost_inprogress &&
     *      s->fromhost != 0x0) {
     *  }
     */
    s->fromhost = (val_written >> 48 << 48) | (resp << 16 >> 16);
    s->tohost = 0; /* clear to indicate we read */
}

#define TOHOST_OFFSET1      (s->tohost_offset)
#define TOHOST_OFFSET2      (s->tohost_offset + 4)
#define FROMHOST_OFFSET1    (s->fromhost_offset)
#define FROMHOST_OFFSET2    (s->fromhost_offset + 4)

/* CPU wants to read an HTIF register */
static uint64_t htif_mm_read(void *opaque, hwaddr addr, unsigned size)
{
    HTIFState *s = opaque;
    if (addr == TOHOST_OFFSET1) {
        return s->tohost & 0xFFFFFFFF;
    } else if (addr == TOHOST_OFFSET2) {
        return (s->tohost >> 32) & 0xFFFFFFFF;
    } else if (addr == FROMHOST_OFFSET1) {
        return s->fromhost & 0xFFFFFFFF;
    } else if (addr == FROMHOST_OFFSET2) {
        return (s->fromhost >> 32) & 0xFFFFFFFF;
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
    HTIFState *s = opaque;
    if (addr == TOHOST_OFFSET1) {
        if (s->tohost == 0x0) {
            s->allow_tohost = 1;
            s->tohost = value & 0xFFFFFFFF;
        } else {
            s->allow_tohost = 0;
        }
    } else if (addr == TOHOST_OFFSET2) {
        if (s->allow_tohost) {
            s->tohost |= value << 32;
            htif_handle_tohost_write(s, s->tohost);
        }
    } else if (addr == FROMHOST_OFFSET1) {
        s->fromhost_inprogress = 1;
        s->fromhost = value & 0xFFFFFFFF;
    } else if (addr == FROMHOST_OFFSET2) {
        s->fromhost |= value << 32;
        s->fromhost_inprogress = 0;
    } else {
        qemu_log("Invalid htif write: address %016" PRIx64 "\n",
            (uint64_t)addr);
    }
}

static const MemoryRegionOps htif_mm_ops = {
    .read = htif_mm_read,
    .write = htif_mm_write,
};

HTIFState *htif_mm_init(MemoryRegion *address_space, Chardev *chr,
                        uint64_t nonelf_base, bool custom_base)
{
    uint64_t base, size, tohost_offset, fromhost_offset;

    if (custom_base) {
        fromhost_addr = nonelf_base;
        tohost_addr = nonelf_base + 8;
    } else {
        if (!fromhost_addr || !tohost_addr) {
            error_report("Invalid HTIF fromhost or tohost address");
            exit(1);
        }
    }

    base = MIN(tohost_addr, fromhost_addr);
    size = MAX(tohost_addr + 8, fromhost_addr + 8) - base;
    tohost_offset = tohost_addr - base;
    fromhost_offset = fromhost_addr - base;

    HTIFState *s = g_new0(HTIFState, 1);
    s->tohost_offset = tohost_offset;
    s->fromhost_offset = fromhost_offset;
    s->pending_read = 0;
    s->allow_tohost = 0;
    s->fromhost_inprogress = 0;
    qemu_chr_fe_init(&s->chr, chr, &error_abort);
    qemu_chr_fe_set_handlers(&s->chr, htif_can_recv, htif_recv, htif_event,
        htif_be_change, s, NULL, true);

    memory_region_init_io(&s->mmio, NULL, &htif_mm_ops, s,
                          TYPE_HTIF_UART, size);
    memory_region_add_subregion_overlap(address_space, base,
                                        &s->mmio, 1);

    return s;
}
