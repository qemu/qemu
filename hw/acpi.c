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
 */
#include "sysemu.h"
#include "hw.h"
#include "pc.h"
#include "acpi.h"

struct acpi_table_header
{
    char signature [4];    /* ACPI signature (4 ASCII characters) */
    uint32_t length;          /* Length of table, in bytes, including header */
    uint8_t revision;         /* ACPI Specification minor version # */
    uint8_t checksum;         /* To make sum of entire table == 0 */
    char oem_id [6];       /* OEM identification */
    char oem_table_id [8]; /* OEM table identification */
    uint32_t oem_revision;    /* OEM revision number */
    char asl_compiler_id [4]; /* ASL compiler vendor ID */
    uint32_t asl_compiler_revision; /* ASL compiler revision number */
} __attribute__((packed));

char *acpi_tables;
size_t acpi_tables_len;

static int acpi_checksum(const uint8_t *data, int len)
{
    int sum, i;
    sum = 0;
    for(i = 0; i < len; i++)
        sum += data[i];
    return (-sum) & 0xff;
}

int acpi_table_add(const char *t)
{
    static const char *dfl_id = "QEMUQEMU";
    char buf[1024], *p, *f;
    struct acpi_table_header acpi_hdr;
    unsigned long val;
    uint32_t length;
    struct acpi_table_header *acpi_hdr_p;
    size_t off;

    memset(&acpi_hdr, 0, sizeof(acpi_hdr));
  
    if (get_param_value(buf, sizeof(buf), "sig", t)) {
        strncpy(acpi_hdr.signature, buf, 4);
    } else {
        strncpy(acpi_hdr.signature, dfl_id, 4);
    }
    if (get_param_value(buf, sizeof(buf), "rev", t)) {
        val = strtoul(buf, &p, 10);
        if (val > 255 || *p != '\0')
            goto out;
    } else {
        val = 1;
    }
    acpi_hdr.revision = (int8_t)val;

    if (get_param_value(buf, sizeof(buf), "oem_id", t)) {
        strncpy(acpi_hdr.oem_id, buf, 6);
    } else {
        strncpy(acpi_hdr.oem_id, dfl_id, 6);
    }

    if (get_param_value(buf, sizeof(buf), "oem_table_id", t)) {
        strncpy(acpi_hdr.oem_table_id, buf, 8);
    } else {
        strncpy(acpi_hdr.oem_table_id, dfl_id, 8);
    }

    if (get_param_value(buf, sizeof(buf), "oem_rev", t)) {
        val = strtol(buf, &p, 10);
        if(*p != '\0')
            goto out;
    } else {
        val = 1;
    }
    acpi_hdr.oem_revision = cpu_to_le32(val);

    if (get_param_value(buf, sizeof(buf), "asl_compiler_id", t)) {
        strncpy(acpi_hdr.asl_compiler_id, buf, 4);
    } else {
        strncpy(acpi_hdr.asl_compiler_id, dfl_id, 4);
    }

    if (get_param_value(buf, sizeof(buf), "asl_compiler_rev", t)) {
        val = strtol(buf, &p, 10);
        if(*p != '\0')
            goto out;
    } else {
        val = 1;
    }
    acpi_hdr.asl_compiler_revision = cpu_to_le32(val);
    
    if (!get_param_value(buf, sizeof(buf), "data", t)) {
         buf[0] = '\0';
    }

    length = sizeof(acpi_hdr);

    f = buf;
    while (buf[0]) {
        struct stat s;
        char *n = strchr(f, ':');
        if (n)
            *n = '\0';
        if(stat(f, &s) < 0) {
            fprintf(stderr, "Can't stat file '%s': %s\n", f, strerror(errno));
            goto out;
        }
        length += s.st_size;
        if (!n)
            break;
        *n = ':';
        f = n + 1;
    }

    if (!acpi_tables) {
        acpi_tables_len = sizeof(uint16_t);
        acpi_tables = qemu_mallocz(acpi_tables_len);
    }
    acpi_tables = qemu_realloc(acpi_tables,
                               acpi_tables_len + sizeof(uint16_t) + length);
    p = acpi_tables + acpi_tables_len;
    acpi_tables_len += sizeof(uint16_t) + length;

    *(uint16_t*)p = cpu_to_le32(length);
    p += sizeof(uint16_t);
    memcpy(p, &acpi_hdr, sizeof(acpi_hdr));
    off = sizeof(acpi_hdr);

    f = buf;
    while (buf[0]) {
        struct stat s;
        int fd;
        char *n = strchr(f, ':');
        if (n)
            *n = '\0';
        fd = open(f, O_RDONLY);

        if(fd < 0)
            goto out;
        if(fstat(fd, &s) < 0) {
            close(fd);
            goto out;
        }

        /* off < length is necessary because file size can be changed
           under our foot */
        while(s.st_size && off < length) {
            int r;
            r = read(fd, p + off, s.st_size);
            if (r > 0) {
                off += r;
                s.st_size -= r;
            } else if ((r < 0 && errno != EINTR) || r == 0) {
                close(fd);
                goto out;
            }
        }

        close(fd);
        if (!n)
            break;
        f = n + 1;
    }
    if (off < length) {
        /* don't pass random value in process to guest */
        memset(p + off, 0, length - off);
    }

    acpi_hdr_p = (struct acpi_table_header*)p;
    acpi_hdr_p->length = cpu_to_le32(length);
    acpi_hdr_p->checksum = acpi_checksum((uint8_t*)p, length);
    /* increase number of tables */
    (*(uint16_t*)acpi_tables) =
	    cpu_to_le32(le32_to_cpu(*(uint16_t*)acpi_tables) + 1);
    return 0;
out:
    if (acpi_tables) {
        qemu_free(acpi_tables);
        acpi_tables = NULL;
    }
    return -1;
}

/* ACPI PM1a EVT */
uint16_t acpi_pm1_evt_get_sts(ACPIPM1EVT *pm1, int64_t overflow_time)
{
    int64_t d = acpi_pm_tmr_get_clock();
    if (d >= overflow_time) {
        pm1->sts |= ACPI_BITMASK_TIMER_STATUS;
    }
    return pm1->sts;
}

void acpi_pm1_evt_write_sts(ACPIPM1EVT *pm1, ACPIPMTimer *tmr, uint16_t val)
{
    uint16_t pm1_sts = acpi_pm1_evt_get_sts(pm1, tmr->overflow_time);
    if (pm1_sts & val & ACPI_BITMASK_TIMER_STATUS) {
        /* if TMRSTS is reset, then compute the new overflow time */
        acpi_pm_tmr_calc_overflow_time(tmr);
    }
    pm1->sts &= ~val;
}

void acpi_pm1_evt_power_down(ACPIPM1EVT *pm1, ACPIPMTimer *tmr)
{
    if (!pm1) {
        qemu_system_shutdown_request();
    } else if (pm1->en & ACPI_BITMASK_POWER_BUTTON_ENABLE) {
        pm1->sts |= ACPI_BITMASK_POWER_BUTTON_STATUS;
        tmr->update_sci(tmr);
    }
}

void acpi_pm1_evt_reset(ACPIPM1EVT *pm1)
{
    pm1->sts = 0;
    pm1->en = 0;
}

/* ACPI PM_TMR */
void acpi_pm_tmr_update(ACPIPMTimer *tmr, bool enable)
{
    int64_t expire_time;

    /* schedule a timer interruption if needed */
    if (enable) {
        expire_time = muldiv64(tmr->overflow_time, get_ticks_per_sec(),
                               PM_TIMER_FREQUENCY);
        qemu_mod_timer(tmr->timer, expire_time);
    } else {
        qemu_del_timer(tmr->timer);
    }
}

void acpi_pm_tmr_calc_overflow_time(ACPIPMTimer *tmr)
{
    int64_t d = acpi_pm_tmr_get_clock();
    tmr->overflow_time = (d + 0x800000LL) & ~0x7fffffLL;
}

uint32_t acpi_pm_tmr_get(ACPIPMTimer *tmr)
{
    uint32_t d = acpi_pm_tmr_get_clock();;
    return d & 0xffffff;
}

static void acpi_pm_tmr_timer(void *opaque)
{
    ACPIPMTimer *tmr = opaque;
    tmr->update_sci(tmr);
}

void acpi_pm_tmr_init(ACPIPMTimer *tmr, acpi_update_sci_fn update_sci)
{
    tmr->update_sci = update_sci;
    tmr->timer = qemu_new_timer_ns(vm_clock, acpi_pm_tmr_timer, tmr);
}

void acpi_pm_tmr_reset(ACPIPMTimer *tmr)
{
    tmr->overflow_time = 0;
    qemu_del_timer(tmr->timer);
}

/* ACPI PM1aCNT */
void acpi_pm1_cnt_init(ACPIPM1CNT *pm1_cnt, qemu_irq cmos_s3)
{
    pm1_cnt->cmos_s3 = cmos_s3;
}

void acpi_pm1_cnt_write(ACPIPM1EVT *pm1a, ACPIPM1CNT *pm1_cnt, uint16_t val)
{
    pm1_cnt->cnt = val & ~(ACPI_BITMASK_SLEEP_ENABLE);

    if (val & ACPI_BITMASK_SLEEP_ENABLE) {
        /* change suspend type */
        uint16_t sus_typ = (val >> 10) & 7;
        switch(sus_typ) {
        case 0: /* soft power off */
            qemu_system_shutdown_request();
            break;
        case 1:
            /* ACPI_BITMASK_WAKE_STATUS should be set on resume.
               Pretend that resume was caused by power button */
            pm1a->sts |=
                (ACPI_BITMASK_WAKE_STATUS | ACPI_BITMASK_POWER_BUTTON_STATUS);
            qemu_system_reset_request();
            qemu_irq_raise(pm1_cnt->cmos_s3);
        default:
            break;
        }
    }
}

void acpi_pm1_cnt_update(ACPIPM1CNT *pm1_cnt,
                         bool sci_enable, bool sci_disable)
{
    /* ACPI specs 3.0, 4.7.2.5 */
    if (sci_enable) {
        pm1_cnt->cnt |= ACPI_BITMASK_SCI_ENABLE;
    } else if (sci_disable) {
        pm1_cnt->cnt &= ~ACPI_BITMASK_SCI_ENABLE;
    }
}

void acpi_pm1_cnt_reset(ACPIPM1CNT *pm1_cnt)
{
    pm1_cnt->cnt = 0;
    if (pm1_cnt->cmos_s3) {
        qemu_irq_lower(pm1_cnt->cmos_s3);
    }
}

/* ACPI GPE */
void acpi_gpe_init(ACPIGPE *gpe, uint8_t len)
{
    gpe->len = len;
    gpe->sts = qemu_mallocz(len / 2);
    gpe->en = qemu_mallocz(len / 2);
}

void acpi_gpe_blk(ACPIGPE *gpe, uint32_t blk)
{
    gpe->blk = blk;
}

void acpi_gpe_reset(ACPIGPE *gpe)
{
    memset(gpe->sts, 0, gpe->len / 2);
    memset(gpe->en, 0, gpe->len / 2);
}

static uint8_t *acpi_gpe_ioport_get_ptr(ACPIGPE *gpe, uint32_t addr)
{
    uint8_t *cur = NULL;

    if (addr < gpe->len / 2) {
        cur = gpe->sts + addr;
    } else if (addr < gpe->len) {
        cur = gpe->en + addr - gpe->len / 2;
    } else {
        abort();
    }

    return cur;
}

void acpi_gpe_ioport_writeb(ACPIGPE *gpe, uint32_t addr, uint32_t val)
{
    uint8_t *cur;

    addr -= gpe->blk;
    cur = acpi_gpe_ioport_get_ptr(gpe, addr);
    if (addr < gpe->len / 2) {
        /* GPE_STS */
        *cur = (*cur) & ~val;
    } else if (addr < gpe->len) {
        /* GPE_EN */
        *cur = val;
    } else {
        abort();
    }
}

uint32_t acpi_gpe_ioport_readb(ACPIGPE *gpe, uint32_t addr)
{
    uint8_t *cur;
    uint32_t val;

    addr -= gpe->blk;
    cur = acpi_gpe_ioport_get_ptr(gpe, addr);
    val = 0;
    if (cur != NULL) {
        val = *cur;
    }

    return val;
}
