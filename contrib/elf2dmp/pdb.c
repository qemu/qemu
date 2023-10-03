/*
 * Copyright (c) 2018 Virtuozzo International GmbH
 *
 * Based on source of Wine project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "qemu/osdep.h"

#include "pdb.h"
#include "err.h"

static uint32_t pdb_get_file_size(const struct pdb_reader *r, unsigned idx)
{
    if (idx >= r->ds.toc->num_files) {
        return 0;
    }

    return r->ds.toc->file_size[idx];
}

static pdb_seg *get_seg_by_num(struct pdb_reader *r, size_t n)
{
    size_t i = 0;
    char *ptr;

    for (ptr = r->segs; (ptr < r->segs + r->segs_size); ) {
        i++;
        ptr += 8;
        if (i == n) {
            break;
        }
        ptr += sizeof(pdb_seg);
    }

    return (pdb_seg *)ptr;
}

uint64_t pdb_find_public_v3_symbol(struct pdb_reader *r, const char *name)
{
    size_t size = pdb_get_file_size(r, r->symbols->gsym_file);
    int length;
    const union codeview_symbol *sym;
    const uint8_t *root = r->modimage;
    size_t i;

    for (i = 0; i < size; i += length) {
        sym = (const void *)(root + i);
        length = sym->generic.len + 2;

        if (!sym->generic.id || length < 4) {
            break;
        }

        if (sym->generic.id == S_PUB_V3 &&
                !strcmp(name, sym->public_v3.name)) {
            pdb_seg *segment = get_seg_by_num(r, sym->public_v3.segment);
            uint32_t sect_rva = segment->dword[1];
            uint64_t rva = sect_rva + sym->public_v3.offset;

            printf("%s: 0x%016x(%d:\'%.8s\') + 0x%08x = 0x%09"PRIx64"\n", name,
                    sect_rva, sym->public_v3.segment,
                    ((char *)segment - 8), sym->public_v3.offset, rva);
            return rva;
        }
    }

    return 0;
}

uint64_t pdb_resolve(uint64_t img_base, struct pdb_reader *r, const char *name)
{
    uint64_t rva = pdb_find_public_v3_symbol(r, name);

    if (!rva) {
        return 0;
    }

    return img_base + rva;
}

static void pdb_reader_ds_exit(struct pdb_reader *r)
{
    g_free(r->ds.toc);
}

static void pdb_exit_symbols(struct pdb_reader *r)
{
    g_free(r->modimage);
    g_free(r->symbols);
}

static void pdb_exit_segments(struct pdb_reader *r)
{
    g_free(r->segs);
}

static void *pdb_ds_read(const PDB_DS_HEADER *header,
        const uint32_t *block_list, int size)
{
    int i, nBlocks;
    uint8_t *buffer;

    if (!size) {
        return NULL;
    }

    nBlocks = (size + header->block_size - 1) / header->block_size;

    buffer = g_malloc(nBlocks * header->block_size);

    for (i = 0; i < nBlocks; i++) {
        memcpy(buffer + i * header->block_size, (const char *)header +
                block_list[i] * header->block_size, header->block_size);
    }

    return buffer;
}

static void *pdb_ds_read_file(struct pdb_reader* r, uint32_t file_number)
{
    const uint32_t *block_list;
    uint32_t block_size;
    const uint32_t *file_size;
    size_t i;

    if (!r->ds.toc || file_number >= r->ds.toc->num_files) {
        return NULL;
    }

    file_size = r->ds.toc->file_size;
    r->file_used[file_number / 32] |= 1 << (file_number % 32);

    if (file_size[file_number] == 0 || file_size[file_number] == 0xFFFFFFFF) {
        return NULL;
    }

    block_list = file_size + r->ds.toc->num_files;
    block_size = r->ds.header->block_size;

    for (i = 0; i < file_number; i++) {
        block_list += (file_size[i] + block_size - 1) / block_size;
    }

    return pdb_ds_read(r->ds.header, block_list, file_size[file_number]);
}

static int pdb_init_segments(struct pdb_reader *r)
{
    unsigned stream_idx = r->segments;

    r->segs = pdb_ds_read_file(r, stream_idx);
    if (!r->segs) {
        return 1;
    }

    r->segs_size = pdb_get_file_size(r, stream_idx);
    if (!r->segs_size) {
        return 1;
    }

    return 0;
}

static int pdb_init_symbols(struct pdb_reader *r)
{
    int err = 0;
    PDB_SYMBOLS *symbols;

    symbols = pdb_ds_read_file(r, 3);
    if (!symbols) {
        return 1;
    }

    r->symbols = symbols;

    r->segments = *(uint16_t *)((const char *)symbols + sizeof(PDB_SYMBOLS) +
            symbols->module_size + symbols->offset_size +
            symbols->hash_size + symbols->srcmodule_size +
            symbols->pdbimport_size + symbols->unknown2_size +
            offsetof(PDB_STREAM_INDEXES, segments));

    /* Read global symbol table */
    r->modimage = pdb_ds_read_file(r, symbols->gsym_file);
    if (!r->modimage) {
        err = 1;
        goto out_symbols;
    }

    return 0;

out_symbols:
    g_free(symbols);

    return err;
}

static int pdb_reader_ds_init(struct pdb_reader *r, PDB_DS_HEADER *hdr)
{
    if (hdr->block_size == 0) {
        return 1;
    }

    memset(r->file_used, 0, sizeof(r->file_used));
    r->ds.header = hdr;
    r->ds.toc = pdb_ds_read(hdr, (uint32_t *)((uint8_t *)hdr +
                hdr->toc_page * hdr->block_size), hdr->toc_size);

    if (!r->ds.toc) {
        return 1;
    }

    return 0;
}

static int pdb_reader_init(struct pdb_reader *r, void *data)
{
    int err = 0;
    const char pdb7[] = "Microsoft C/C++ MSF 7.00";

    if (memcmp(data, pdb7, sizeof(pdb7) - 1)) {
        return 1;
    }

    if (pdb_reader_ds_init(r, data)) {
        return 1;
    }

    r->ds.root = pdb_ds_read_file(r, 1);
    if (!r->ds.root) {
        err = 1;
        goto out_ds;
    }

    if (pdb_init_symbols(r)) {
        err = 1;
        goto out_root;
    }

    if (pdb_init_segments(r)) {
        err = 1;
        goto out_sym;
    }

    return 0;

out_sym:
    pdb_exit_symbols(r);
out_root:
    g_free(r->ds.root);
out_ds:
    pdb_reader_ds_exit(r);

    return err;
}

static void pdb_reader_exit(struct pdb_reader *r)
{
    pdb_exit_segments(r);
    pdb_exit_symbols(r);
    g_free(r->ds.root);
    pdb_reader_ds_exit(r);
}

int pdb_init_from_file(const char *name, struct pdb_reader *reader)
{
    GError *gerr = NULL;
    int err = 0;
    void *map;

    reader->gmf = g_mapped_file_new(name, TRUE, &gerr);
    if (gerr) {
        eprintf("Failed to map PDB file \'%s\'\n", name);
        g_error_free(gerr);
        return 1;
    }

    reader->file_size = g_mapped_file_get_length(reader->gmf);
    map = g_mapped_file_get_contents(reader->gmf);
    if (pdb_reader_init(reader, map)) {
        err = 1;
        goto out_unmap;
    }

    return 0;

out_unmap:
    g_mapped_file_unref(reader->gmf);

    return err;
}

void pdb_exit(struct pdb_reader *reader)
{
    g_mapped_file_unref(reader->gmf);
    pdb_reader_exit(reader);
}
