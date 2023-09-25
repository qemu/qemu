/*
 * Copyright (c) 2018 Virtuozzo International GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 *
 */

#ifndef PDB_H
#define PDB_H


#ifndef _WIN32
typedef struct GUID {
    unsigned int Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char Data4[8];
} GUID;
#endif

struct PDB_FILE {
    uint32_t size;
    uint32_t unknown;
};

typedef struct PDB_DS_HEADER {
    char signature[32];
    uint32_t block_size;
    uint32_t unknown1;
    uint32_t num_pages;
    uint32_t toc_size;
    uint32_t unknown2;
    uint32_t toc_page;
} PDB_DS_HEADER;

typedef struct PDB_DS_TOC {
    uint32_t num_files;
    uint32_t file_size[1];
} PDB_DS_TOC;

typedef struct PDB_DS_ROOT {
    uint32_t Version;
    uint32_t TimeDateStamp;
    uint32_t Age;
    GUID guid;
    uint32_t cbNames;
    char names[1];
} PDB_DS_ROOT;

typedef struct PDB_TYPES_OLD {
    uint32_t version;
    uint16_t first_index;
    uint16_t last_index;
    uint32_t type_size;
    uint16_t file;
    uint16_t pad;
} PDB_TYPES_OLD;

typedef struct PDB_TYPES {
    uint32_t version;
    uint32_t type_offset;
    uint32_t first_index;
    uint32_t last_index;
    uint32_t type_size;
    uint16_t file;
    uint16_t pad;
    uint32_t hash_size;
    uint32_t hash_base;
    uint32_t hash_offset;
    uint32_t hash_len;
    uint32_t search_offset;
    uint32_t search_len;
    uint32_t unknown_offset;
    uint32_t unknown_len;
} PDB_TYPES;

typedef struct PDB_SYMBOL_RANGE {
    uint16_t segment;
    uint16_t pad1;
    uint32_t offset;
    uint32_t size;
    uint32_t characteristics;
    uint16_t index;
    uint16_t pad2;
} PDB_SYMBOL_RANGE;

typedef struct PDB_SYMBOL_RANGE_EX {
    uint16_t segment;
    uint16_t pad1;
    uint32_t offset;
    uint32_t size;
    uint32_t characteristics;
    uint16_t index;
    uint16_t pad2;
    uint32_t timestamp;
    uint32_t unknown;
} PDB_SYMBOL_RANGE_EX;

typedef struct PDB_SYMBOL_FILE {
    uint32_t unknown1;
    PDB_SYMBOL_RANGE range;
    uint16_t flag;
    uint16_t file;
    uint32_t symbol_size;
    uint32_t lineno_size;
    uint32_t unknown2;
    uint32_t nSrcFiles;
    uint32_t attribute;
    char filename[1];
} PDB_SYMBOL_FILE;

typedef struct PDB_SYMBOL_FILE_EX {
    uint32_t unknown1;
    PDB_SYMBOL_RANGE_EX range;
    uint16_t flag;
    uint16_t file;
    uint32_t symbol_size;
    uint32_t lineno_size;
    uint32_t unknown2;
    uint32_t nSrcFiles;
    uint32_t attribute;
    uint32_t reserved[2];
    char filename[1];
} PDB_SYMBOL_FILE_EX;

typedef struct PDB_SYMBOL_SOURCE {
    uint16_t nModules;
    uint16_t nSrcFiles;
    uint16_t table[1];
} PDB_SYMBOL_SOURCE;

typedef struct PDB_SYMBOL_IMPORT {
    uint32_t unknown1;
    uint32_t unknown2;
    uint32_t TimeDateStamp;
    uint32_t Age;
    char filename[1];
} PDB_SYMBOL_IMPORT;

typedef struct PDB_SYMBOLS_OLD {
    uint16_t hash1_file;
    uint16_t hash2_file;
    uint16_t gsym_file;
    uint16_t pad;
    uint32_t module_size;
    uint32_t offset_size;
    uint32_t hash_size;
    uint32_t srcmodule_size;
} PDB_SYMBOLS_OLD;

typedef struct PDB_SYMBOLS {
    uint32_t signature;
    uint32_t version;
    uint32_t unknown;
    uint32_t hash1_file;
    uint32_t hash2_file;
    uint16_t gsym_file;
    uint16_t unknown1;
    uint32_t module_size;
    uint32_t offset_size;
    uint32_t hash_size;
    uint32_t srcmodule_size;
    uint32_t pdbimport_size;
    uint32_t resvd0;
    uint32_t stream_index_size;
    uint32_t unknown2_size;
    uint16_t resvd3;
    uint16_t machine;
    uint32_t resvd4;
} PDB_SYMBOLS;

typedef struct {
    uint16_t FPO;
    uint16_t unk0;
    uint16_t unk1;
    uint16_t unk2;
    uint16_t unk3;
    uint16_t segments;
} PDB_STREAM_INDEXES_OLD;

typedef struct {
    uint16_t FPO;
    uint16_t unk0;
    uint16_t unk1;
    uint16_t unk2;
    uint16_t unk3;
    uint16_t segments;
    uint16_t unk4;
    uint16_t unk5;
    uint16_t unk6;
    uint16_t FPO_EXT;
    uint16_t unk7;
} PDB_STREAM_INDEXES;

union codeview_symbol {
    struct {
        int16_t len;
        int16_t id;
    } generic;

    struct {
        int16_t len;
        int16_t id;
        uint32_t symtype;
        uint32_t offset;
        uint16_t segment;
        char name[1];
    } public_v3;
};

#define S_PUB_V3        0x110E

typedef struct pdb_seg {
    uint32_t dword[8];
} __attribute__ ((packed)) pdb_seg;

#define IMAGE_FILE_MACHINE_I386 0x014c
#define IMAGE_FILE_MACHINE_AMD64 0x8664

struct pdb_reader {
    GMappedFile *gmf;
    size_t file_size;
    struct {
        PDB_DS_HEADER *header;
        PDB_DS_TOC *toc;
        PDB_DS_ROOT *root;
    } ds;
    uint32_t file_used[1024];
    PDB_SYMBOLS *symbols;
    uint16_t segments;
    uint8_t *modimage;
    char *segs;
    size_t segs_size;
};

int pdb_init_from_file(const char *name, struct pdb_reader *reader);
void pdb_exit(struct pdb_reader *reader);
uint64_t pdb_resolve(uint64_t img_base, struct pdb_reader *r, const char *name);
uint64_t pdb_find_public_v3_symbol(struct pdb_reader *reader, const char *name);

#endif /* PDB_H */
