/*
 * ACPI implementation
 *
 * Copyright (c) 2006 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */
#include "sysemu.h"
#include "hw.h"
#include "pc.h"
#include "acpi.h"

struct acpi_table_header {
    uint16_t _length;         /* our length, not actual part of the hdr */
                              /* XXX why we have 2 length fields here? */
    char sig[4];              /* ACPI signature (4 ASCII characters) */
    uint32_t length;          /* Length of table, in bytes, including header */
    uint8_t revision;         /* ACPI Specification minor version # */
    uint8_t checksum;         /* To make sum of entire table == 0 */
    char oem_id[6];           /* OEM identification */
    char oem_table_id[8];     /* OEM table identification */
    uint32_t oem_revision;    /* OEM revision number */
    char asl_compiler_id[4];  /* ASL compiler vendor ID */
    uint32_t asl_compiler_revision; /* ASL compiler revision number */
} QEMU_PACKED;

#define ACPI_TABLE_HDR_SIZE sizeof(struct acpi_table_header)
#define ACPI_TABLE_PFX_SIZE sizeof(uint16_t)  /* size of the extra prefix */

static const char dfl_hdr[ACPI_TABLE_HDR_SIZE] =
    "\0\0"                   /* fake _length (2) */
    "QEMU\0\0\0\0\1\0"       /* sig (4), len(4), revno (1), csum (1) */
    "QEMUQEQEMUQEMU\1\0\0\0" /* OEM id (6), table (8), revno (4) */
    "QEMU\1\0\0\0"           /* ASL compiler ID (4), version (4) */
    ;

char *acpi_tables;
size_t acpi_tables_len;

static int acpi_checksum(const uint8_t *data, int len)
{
    int sum, i;
    sum = 0;
    for (i = 0; i < len; i++) {
        sum += data[i];
    }
    return (-sum) & 0xff;
}

/* like strncpy() but zero-fills the tail of destination */
static void strzcpy(char *dst, const char *src, size_t size)
{
    size_t len = strlen(src);
    if (len >= size) {
        len = size;
    } else {
      memset(dst + len, 0, size - len);
    }
    memcpy(dst, src, len);
}

/* XXX fixme: this function uses obsolete argument parsing interface */
int acpi_table_add(const char *t)
{
    char buf[1024], *p, *f;
    unsigned long val;
    size_t len, start, allen;
    bool has_header;
    int changed;
    int r;
    struct acpi_table_header hdr;

    r = 0;
    r |= get_param_value(buf, sizeof(buf), "data", t) ? 1 : 0;
    r |= get_param_value(buf, sizeof(buf), "file", t) ? 2 : 0;
    switch (r) {
    case 0:
        buf[0] = '\0';
        /* fallthrough for default behavior */
    case 1:
        has_header = false;
        break;
    case 2:
        has_header = true;
        break;
    default:
        fprintf(stderr, "acpitable: both data and file are specified\n");
        return -1;
    }

    if (!acpi_tables) {
        allen = sizeof(uint16_t);
        acpi_tables = g_malloc0(allen);
    } else {
        allen = acpi_tables_len;
    }

    start = allen;
    acpi_tables = g_realloc(acpi_tables, start + ACPI_TABLE_HDR_SIZE);
    allen += has_header ? ACPI_TABLE_PFX_SIZE : ACPI_TABLE_HDR_SIZE;

    /* now read in the data files, reallocating buffer as needed */

    for (f = strtok(buf, ":"); f; f = strtok(NULL, ":")) {
        int fd = open(f, O_RDONLY);

        if (fd < 0) {
            fprintf(stderr, "can't open file %s: %s\n", f, strerror(errno));
            return -1;
        }

        for (;;) {
            char data[8192];
            r = read(fd, data, sizeof(data));
            if (r == 0) {
                break;
            } else if (r > 0) {
                acpi_tables = g_realloc(acpi_tables, allen + r);
                memcpy(acpi_tables + allen, data, r);
                allen += r;
            } else if (errno != EINTR) {
                fprintf(stderr, "can't read file %s: %s\n",
                        f, strerror(errno));
                close(fd);
                return -1;
            }
        }

        close(fd);
    }

    /* now fill in the header fields */

    f = acpi_tables + start;   /* start of the table */
    changed = 0;

    /* copy the header to temp place to align the fields */
    memcpy(&hdr, has_header ? f : dfl_hdr, ACPI_TABLE_HDR_SIZE);

    /* length of the table minus our prefix */
    len = allen - start - ACPI_TABLE_PFX_SIZE;

    hdr._length = cpu_to_le16(len);

    if (get_param_value(buf, sizeof(buf), "sig", t)) {
        strzcpy(hdr.sig, buf, sizeof(hdr.sig));
        ++changed;
    }

    /* length of the table including header, in bytes */
    if (has_header) {
        /* check if actual length is correct */
        val = le32_to_cpu(hdr.length);
        if (val != len) {
            fprintf(stderr,
                "warning: acpitable has wrong length,"
                " header says %lu, actual size %zu bytes\n",
                val, len);
            ++changed;
        }
    }
    /* we may avoid putting length here if has_header is true */
    hdr.length = cpu_to_le32(len);

    if (get_param_value(buf, sizeof(buf), "rev", t)) {
        val = strtoul(buf, &p, 0);
        if (val > 255 || *p) {
            fprintf(stderr, "acpitable: \"rev=%s\" is invalid\n", buf);
            return -1;
        }
        hdr.revision = (uint8_t)val;
        ++changed;
    }

    if (get_param_value(buf, sizeof(buf), "oem_id", t)) {
        strzcpy(hdr.oem_id, buf, sizeof(hdr.oem_id));
        ++changed;
    }

    if (get_param_value(buf, sizeof(buf), "oem_table_id", t)) {
        strzcpy(hdr.oem_table_id, buf, sizeof(hdr.oem_table_id));
        ++changed;
    }

    if (get_param_value(buf, sizeof(buf), "oem_rev", t)) {
        val = strtol(buf, &p, 0);
        if (*p) {
            fprintf(stderr, "acpitable: \"oem_rev=%s\" is invalid\n", buf);
            return -1;
        }
        hdr.oem_revision = cpu_to_le32(val);
        ++changed;
    }

    if (get_param_value(buf, sizeof(buf), "asl_compiler_id", t)) {
        strzcpy(hdr.asl_compiler_id, buf, sizeof(hdr.asl_compiler_id));
        ++changed;
    }

    if (get_param_value(buf, sizeof(buf), "asl_compiler_rev", t)) {
        val = strtol(buf, &p, 0);
        if (*p) {
            fprintf(stderr, "acpitable: \"%s=%s\" is invalid\n",
                    "asl_compiler_rev", buf);
            return -1;
        }
        hdr.asl_compiler_revision = cpu_to_le32(val);
        ++changed;
    }

    if (!has_header && !changed) {
        fprintf(stderr, "warning: acpitable: no table headers are specified\n");
    }


    /* now calculate checksum of the table, complete with the header */
    /* we may as well leave checksum intact if has_header is true */
    /* alternatively there may be a way to set cksum to a given value */
    hdr.checksum = 0;    /* for checksum calculation */

    /* put header back */
    memcpy(f, &hdr, sizeof(hdr));

    if (changed || !has_header || 1) {
        ((struct acpi_table_header *)f)->checksum =
            acpi_checksum((uint8_t *)f + ACPI_TABLE_PFX_SIZE, len);
    }

    /* increase number of tables */
    (*(uint16_t *)acpi_tables) =
        cpu_to_le32(le32_to_cpu(*(uint16_t *)acpi_tables) + 1);

    acpi_tables_len = allen;
    return 0;

}

static void acpi_notify_wakeup(Notifier *notifier, void *data)
{
    ACPIREGS *ar = container_of(notifier, ACPIREGS, wakeup);
    WakeupReason *reason = data;

    switch (*reason) {
    case QEMU_WAKEUP_REASON_RTC:
        ar->pm1.evt.sts |=
            (ACPI_BITMASK_WAKE_STATUS | ACPI_BITMASK_RT_CLOCK_STATUS);
        break;
    case QEMU_WAKEUP_REASON_PMTIMER:
        ar->pm1.evt.sts |=
            (ACPI_BITMASK_WAKE_STATUS | ACPI_BITMASK_TIMER_STATUS);
        break;
    case QEMU_WAKEUP_REASON_OTHER:
    default:
        /* ACPI_BITMASK_WAKE_STATUS should be set on resume.
           Pretend that resume was caused by power button */
        ar->pm1.evt.sts |=
            (ACPI_BITMASK_WAKE_STATUS | ACPI_BITMASK_POWER_BUTTON_STATUS);
        break;
    }
}

/* ACPI PM1a EVT */
uint16_t acpi_pm1_evt_get_sts(ACPIREGS *ar)
{
    int64_t d = acpi_pm_tmr_get_clock();
    if (d >= ar->tmr.overflow_time) {
        ar->pm1.evt.sts |= ACPI_BITMASK_TIMER_STATUS;
    }
    return ar->pm1.evt.sts;
}

void acpi_pm1_evt_write_sts(ACPIREGS *ar, uint16_t val)
{
    uint16_t pm1_sts = acpi_pm1_evt_get_sts(ar);
    if (pm1_sts & val & ACPI_BITMASK_TIMER_STATUS) {
        /* if TMRSTS is reset, then compute the new overflow time */
        acpi_pm_tmr_calc_overflow_time(ar);
    }
    ar->pm1.evt.sts &= ~val;
}

void acpi_pm1_evt_write_en(ACPIREGS *ar, uint16_t val)
{
    ar->pm1.evt.en = val;
    qemu_system_wakeup_enable(QEMU_WAKEUP_REASON_RTC,
                              val & ACPI_BITMASK_RT_CLOCK_ENABLE);
    qemu_system_wakeup_enable(QEMU_WAKEUP_REASON_PMTIMER,
                              val & ACPI_BITMASK_TIMER_ENABLE);
}

void acpi_pm1_evt_power_down(ACPIREGS *ar)
{
    if (ar->pm1.evt.en & ACPI_BITMASK_POWER_BUTTON_ENABLE) {
        ar->pm1.evt.sts |= ACPI_BITMASK_POWER_BUTTON_STATUS;
        ar->tmr.update_sci(ar);
    }
}

void acpi_pm1_evt_reset(ACPIREGS *ar)
{
    ar->pm1.evt.sts = 0;
    ar->pm1.evt.en = 0;
    qemu_system_wakeup_enable(QEMU_WAKEUP_REASON_RTC, 0);
    qemu_system_wakeup_enable(QEMU_WAKEUP_REASON_PMTIMER, 0);
}

/* ACPI PM_TMR */
void acpi_pm_tmr_update(ACPIREGS *ar, bool enable)
{
    int64_t expire_time;

    /* schedule a timer interruption if needed */
    if (enable) {
        expire_time = muldiv64(ar->tmr.overflow_time, get_ticks_per_sec(),
                               PM_TIMER_FREQUENCY);
        qemu_mod_timer(ar->tmr.timer, expire_time);
    } else {
        qemu_del_timer(ar->tmr.timer);
    }
}

void acpi_pm_tmr_calc_overflow_time(ACPIREGS *ar)
{
    int64_t d = acpi_pm_tmr_get_clock();
    ar->tmr.overflow_time = (d + 0x800000LL) & ~0x7fffffLL;
}

uint32_t acpi_pm_tmr_get(ACPIREGS *ar)
{
    uint32_t d = acpi_pm_tmr_get_clock();
    return d & 0xffffff;
}

static void acpi_pm_tmr_timer(void *opaque)
{
    ACPIREGS *ar = opaque;
    qemu_system_wakeup_request(QEMU_WAKEUP_REASON_PMTIMER);
    ar->tmr.update_sci(ar);
}

void acpi_pm_tmr_init(ACPIREGS *ar, acpi_update_sci_fn update_sci)
{
    ar->tmr.update_sci = update_sci;
    ar->tmr.timer = qemu_new_timer_ns(vm_clock, acpi_pm_tmr_timer, ar);
}

void acpi_pm_tmr_reset(ACPIREGS *ar)
{
    ar->tmr.overflow_time = 0;
    qemu_del_timer(ar->tmr.timer);
}

/* ACPI PM1aCNT */
void acpi_pm1_cnt_init(ACPIREGS *ar)
{
    ar->wakeup.notify = acpi_notify_wakeup;
    qemu_register_wakeup_notifier(&ar->wakeup);
}

void acpi_pm1_cnt_write(ACPIREGS *ar, uint16_t val)
{
    ar->pm1.cnt.cnt = val & ~(ACPI_BITMASK_SLEEP_ENABLE);

    if (val & ACPI_BITMASK_SLEEP_ENABLE) {
        /* change suspend type */
        uint16_t sus_typ = (val >> 10) & 7;
        switch(sus_typ) {
        case 0: /* soft power off */
            qemu_system_shutdown_request();
            break;
        case 1:
            qemu_system_suspend_request();
            break;
        default:
            break;
        }
    }
}

void acpi_pm1_cnt_update(ACPIREGS *ar,
                         bool sci_enable, bool sci_disable)
{
    /* ACPI specs 3.0, 4.7.2.5 */
    if (sci_enable) {
        ar->pm1.cnt.cnt |= ACPI_BITMASK_SCI_ENABLE;
    } else if (sci_disable) {
        ar->pm1.cnt.cnt &= ~ACPI_BITMASK_SCI_ENABLE;
    }
}

void acpi_pm1_cnt_reset(ACPIREGS *ar)
{
    ar->pm1.cnt.cnt = 0;
}

/* ACPI GPE */
void acpi_gpe_init(ACPIREGS *ar, uint8_t len)
{
    ar->gpe.len = len;
    ar->gpe.sts = g_malloc0(len / 2);
    ar->gpe.en = g_malloc0(len / 2);
}

void acpi_gpe_blk(ACPIREGS *ar, uint32_t blk)
{
    ar->gpe.blk = blk;
}

void acpi_gpe_reset(ACPIREGS *ar)
{
    memset(ar->gpe.sts, 0, ar->gpe.len / 2);
    memset(ar->gpe.en, 0, ar->gpe.len / 2);
}

static uint8_t *acpi_gpe_ioport_get_ptr(ACPIREGS *ar, uint32_t addr)
{
    uint8_t *cur = NULL;

    if (addr < ar->gpe.len / 2) {
        cur = ar->gpe.sts + addr;
    } else if (addr < ar->gpe.len) {
        cur = ar->gpe.en + addr - ar->gpe.len / 2;
    } else {
        abort();
    }

    return cur;
}

void acpi_gpe_ioport_writeb(ACPIREGS *ar, uint32_t addr, uint32_t val)
{
    uint8_t *cur;

    addr -= ar->gpe.blk;
    cur = acpi_gpe_ioport_get_ptr(ar, addr);
    if (addr < ar->gpe.len / 2) {
        /* GPE_STS */
        *cur = (*cur) & ~val;
    } else if (addr < ar->gpe.len) {
        /* GPE_EN */
        *cur = val;
    } else {
        abort();
    }
}

uint32_t acpi_gpe_ioport_readb(ACPIREGS *ar, uint32_t addr)
{
    uint8_t *cur;
    uint32_t val;

    addr -= ar->gpe.blk;
    cur = acpi_gpe_ioport_get_ptr(ar, addr);
    val = 0;
    if (cur != NULL) {
        val = *cur;
    }

    return val;
}
