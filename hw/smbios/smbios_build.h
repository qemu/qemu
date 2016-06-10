/*
 * SMBIOS Support
 *
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 * Copyright (C) 2013 Red Hat, Inc.
 *
 * Authors:
 *  Alex Williamson <alex.williamson@hp.com>
 *  Markus Armbruster <armbru@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#ifndef QEMU_SMBIOS_BUILD_H
#define QEMU_SMBIOS_BUILD_H

bool smbios_skip_table(uint8_t type, bool required_table);

extern uint8_t *smbios_tables;
extern size_t smbios_tables_len;
extern unsigned smbios_table_max;
extern unsigned smbios_table_cnt;

#define SMBIOS_BUILD_TABLE_PRE(tbl_type, tbl_handle, tbl_required)        \
    struct smbios_type_##tbl_type *t;                                     \
    size_t t_off; /* table offset into smbios_tables */                   \
    int str_index = 0;                                                    \
    do {                                                                  \
        /* should we skip building this table ? */                        \
        if (smbios_skip_table(tbl_type, tbl_required)) {                  \
            return;                                                       \
        }                                                                 \
                                                                          \
        /* use offset of table t within smbios_tables */                  \
        /* (pointer must be updated after each realloc) */                \
        t_off = smbios_tables_len;                                        \
        smbios_tables_len += sizeof(*t);                                  \
        smbios_tables = g_realloc(smbios_tables, smbios_tables_len);      \
        t = (struct smbios_type_##tbl_type *)(smbios_tables + t_off);     \
                                                                          \
        t->header.type = tbl_type;                                        \
        t->header.length = sizeof(*t);                                    \
        t->header.handle = cpu_to_le16(tbl_handle);                       \
    } while (0)

#define SMBIOS_TABLE_SET_STR(tbl_type, field, value)                      \
    do {                                                                  \
        int len = (value != NULL) ? strlen(value) + 1 : 0;                \
        if (len > 1) {                                                    \
            smbios_tables = g_realloc(smbios_tables,                      \
                                      smbios_tables_len + len);           \
            memcpy(smbios_tables + smbios_tables_len, value, len);        \
            smbios_tables_len += len;                                     \
            /* update pointer post-realloc */                             \
            t = (struct smbios_type_##tbl_type *)(smbios_tables + t_off); \
            t->field = ++str_index;                                       \
        } else {                                                          \
            t->field = 0;                                                 \
        }                                                                 \
    } while (0)

#define SMBIOS_BUILD_TABLE_POST                                           \
    do {                                                                  \
        size_t term_cnt, t_size;                                          \
                                                                          \
        /* add '\0' terminator (add two if no strings defined) */         \
        term_cnt = (str_index == 0) ? 2 : 1;                              \
        smbios_tables = g_realloc(smbios_tables,                          \
                                  smbios_tables_len + term_cnt);          \
        memset(smbios_tables + smbios_tables_len, 0, term_cnt);           \
        smbios_tables_len += term_cnt;                                    \
                                                                          \
        /* update smbios max. element size */                             \
        t_size = smbios_tables_len - t_off;                               \
        if (t_size > smbios_table_max) {                                  \
            smbios_table_max = t_size;                                    \
        }                                                                 \
                                                                          \
        /* update smbios element count */                                 \
        smbios_table_cnt++;                                               \
    } while (0)

#endif /* QEMU_SMBIOS_BUILD_H */
