/*
 * CXL CDAT Structure
 *
 * Copyright (C) 2021 Avery Design Systems, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/cxl/cxl.h"
#include "qapi/error.h"
#include "qemu/error-report.h"

static void cdat_len_check(CDATSubHeader *hdr, Error **errp)
{
    assert(hdr->length);
    assert(hdr->reserved == 0);

    switch (hdr->type) {
    case CDAT_TYPE_DSMAS:
        assert(hdr->length == sizeof(CDATDsmas));
        break;
    case CDAT_TYPE_DSLBIS:
        assert(hdr->length == sizeof(CDATDslbis));
        break;
    case CDAT_TYPE_DSMSCIS:
        assert(hdr->length == sizeof(CDATDsmscis));
        break;
    case CDAT_TYPE_DSIS:
        assert(hdr->length == sizeof(CDATDsis));
        break;
    case CDAT_TYPE_DSEMTS:
        assert(hdr->length == sizeof(CDATDsemts));
        break;
    case CDAT_TYPE_SSLBIS:
        assert(hdr->length >= sizeof(CDATSslbisHeader));
        assert((hdr->length - sizeof(CDATSslbisHeader)) %
               sizeof(CDATSslbe) == 0);
        break;
    default:
        error_setg(errp, "Type %d is reserved", hdr->type);
    }
}

static void ct3_build_cdat(CDATObject *cdat, Error **errp)
{
    g_autofree CDATTableHeader *cdat_header = NULL;
    g_autofree CDATEntry *cdat_st = NULL;
    uint8_t sum = 0;
    int ent, i;

    /* Use default table if fopen == NULL */
    assert(cdat->build_cdat_table);

    cdat_header = g_malloc0(sizeof(*cdat_header));
    if (!cdat_header) {
        error_setg(errp, "Failed to allocate CDAT header");
        return;
    }

    cdat->built_buf_len = cdat->build_cdat_table(&cdat->built_buf, cdat->private);

    if (!cdat->built_buf_len) {
        /* Build later as not all data available yet */
        cdat->to_update = true;
        return;
    }
    cdat->to_update = false;

    cdat_st = g_malloc0(sizeof(*cdat_st) * (cdat->built_buf_len + 1));
    if (!cdat_st) {
        error_setg(errp, "Failed to allocate CDAT entry array");
        return;
    }

    /* Entry 0 for CDAT header, starts with Entry 1 */
    for (ent = 1; ent < cdat->built_buf_len + 1; ent++) {
        CDATSubHeader *hdr = cdat->built_buf[ent - 1];
        uint8_t *buf = (uint8_t *)cdat->built_buf[ent - 1];

        cdat_st[ent].base = hdr;
        cdat_st[ent].length = hdr->length;

        cdat_header->length += hdr->length;
        for (i = 0; i < hdr->length; i++) {
            sum += buf[i];
        }
    }

    /* CDAT header */
    cdat_header->revision = CXL_CDAT_REV;
    /* For now, no runtime updates */
    cdat_header->sequence = 0;
    cdat_header->length += sizeof(CDATTableHeader);
    sum += cdat_header->revision + cdat_header->sequence +
        cdat_header->length;
    /* Sum of all bytes including checksum must be 0 */
    cdat_header->checksum = ~sum + 1;

    cdat_st[0].base = g_steal_pointer(&cdat_header);
    cdat_st[0].length = sizeof(*cdat_header);
    cdat->entry_len = 1 + cdat->built_buf_len;
    cdat->entry = g_steal_pointer(&cdat_st);
}

static void ct3_load_cdat(CDATObject *cdat, Error **errp)
{
    g_autofree CDATEntry *cdat_st = NULL;
    uint8_t sum = 0;
    int num_ent;
    int i = 0, ent = 1, file_size = 0;
    CDATSubHeader *hdr;
    FILE *fp = NULL;

    /* Read CDAT file and create its cache */
    fp = fopen(cdat->filename, "r");
    if (!fp) {
        error_setg(errp, "CDAT: Unable to open file");
        return;
    }

    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    cdat->buf = g_malloc0(file_size);

    if (fread(cdat->buf, file_size, 1, fp) == 0) {
        error_setg(errp, "CDAT: File read failed");
        return;
    }

    fclose(fp);

    if (file_size < sizeof(CDATTableHeader)) {
        error_setg(errp, "CDAT: File too short");
        return;
    }
    i = sizeof(CDATTableHeader);
    num_ent = 1;
    while (i < file_size) {
        hdr = (CDATSubHeader *)(cdat->buf + i);
        cdat_len_check(hdr, errp);
        i += hdr->length;
        num_ent++;
    }
    if (i != file_size) {
        error_setg(errp, "CDAT: File length mismatch");
        return;
    }

    cdat_st = g_malloc0(sizeof(*cdat_st) * num_ent);
    if (!cdat_st) {
        error_setg(errp, "CDAT: Failed to allocate entry array");
        return;
    }

    /* Set CDAT header, Entry = 0 */
    cdat_st[0].base = cdat->buf;
    cdat_st[0].length = sizeof(CDATTableHeader);
    i = 0;

    while (i < cdat_st[0].length) {
        sum += cdat->buf[i++];
    }

    /* Read CDAT structures */
    while (i < file_size) {
        hdr = (CDATSubHeader *)(cdat->buf + i);
        cdat_len_check(hdr, errp);

        cdat_st[ent].base = hdr;
        cdat_st[ent].length = hdr->length;

        while (cdat->buf + i <
               (uint8_t *)cdat_st[ent].base + cdat_st[ent].length) {
            assert(i < file_size);
            sum += cdat->buf[i++];
        }

        ent++;
    }

    if (sum != 0) {
        warn_report("CDAT: Found checksum mismatch in %s", cdat->filename);
    }
    cdat->entry_len = num_ent;
    cdat->entry = g_steal_pointer(&cdat_st);
}

void cxl_doe_cdat_init(CXLComponentState *cxl_cstate, Error **errp)
{
    CDATObject *cdat = &cxl_cstate->cdat;

    if (cdat->filename) {
        ct3_load_cdat(cdat, errp);
    } else {
        ct3_build_cdat(cdat, errp);
    }
}

void cxl_doe_cdat_update(CXLComponentState *cxl_cstate, Error **errp)
{
    CDATObject *cdat = &cxl_cstate->cdat;

    if (cdat->to_update) {
        ct3_build_cdat(cdat, errp);
    }
}

void cxl_doe_cdat_release(CXLComponentState *cxl_cstate)
{
    CDATObject *cdat = &cxl_cstate->cdat;

    free(cdat->entry);
    if (cdat->built_buf) {
        cdat->free_cdat_table(cdat->built_buf, cdat->built_buf_len,
                              cdat->private);
    }
    if (cdat->buf) {
        free(cdat->buf);
    }
}
