/*
 * Copyright (c) 2018 Virtuozzo International GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 *
 */

#ifndef PE_H
#define PE_H


#ifndef _WIN32
typedef struct IMAGE_DOS_HEADER {
    uint16_t  e_magic;      /* 0x00: MZ Header signature */
    uint16_t  e_cblp;       /* 0x02: Bytes on last page of file */
    uint16_t  e_cp;         /* 0x04: Pages in file */
    uint16_t  e_crlc;       /* 0x06: Relocations */
    uint16_t  e_cparhdr;    /* 0x08: Size of header in paragraphs */
    uint16_t  e_minalloc;   /* 0x0a: Minimum extra paragraphs needed */
    uint16_t  e_maxalloc;   /* 0x0c: Maximum extra paragraphs needed */
    uint16_t  e_ss;         /* 0x0e: Initial (relative) SS value */
    uint16_t  e_sp;         /* 0x10: Initial SP value */
    uint16_t  e_csum;       /* 0x12: Checksum */
    uint16_t  e_ip;         /* 0x14: Initial IP value */
    uint16_t  e_cs;         /* 0x16: Initial (relative) CS value */
    uint16_t  e_lfarlc;     /* 0x18: File address of relocation table */
    uint16_t  e_ovno;       /* 0x1a: Overlay number */
    uint16_t  e_res[4];     /* 0x1c: Reserved words */
    uint16_t  e_oemid;      /* 0x24: OEM identifier (for e_oeminfo) */
    uint16_t  e_oeminfo;    /* 0x26: OEM information; e_oemid specific */
    uint16_t  e_res2[10];   /* 0x28: Reserved words */
    uint32_t  e_lfanew;     /* 0x3c: Offset to extended header */
} __attribute__ ((packed)) IMAGE_DOS_HEADER;

typedef struct IMAGE_FILE_HEADER {
  uint16_t  Machine;
  uint16_t  NumberOfSections;
  uint32_t  TimeDateStamp;
  uint32_t  PointerToSymbolTable;
  uint32_t  NumberOfSymbols;
  uint16_t  SizeOfOptionalHeader;
  uint16_t  Characteristics;
} __attribute__ ((packed)) IMAGE_FILE_HEADER;

typedef struct IMAGE_DATA_DIRECTORY {
  uint32_t VirtualAddress;
  uint32_t Size;
} __attribute__ ((packed)) IMAGE_DATA_DIRECTORY;

#define IMAGE_NUMBEROF_DIRECTORY_ENTRIES 16

typedef struct IMAGE_OPTIONAL_HEADER64 {
  uint16_t  Magic; /* 0x20b */
  uint8_t   MajorLinkerVersion;
  uint8_t   MinorLinkerVersion;
  uint32_t  SizeOfCode;
  uint32_t  SizeOfInitializedData;
  uint32_t  SizeOfUninitializedData;
  uint32_t  AddressOfEntryPoint;
  uint32_t  BaseOfCode;
  uint64_t  ImageBase;
  uint32_t  SectionAlignment;
  uint32_t  FileAlignment;
  uint16_t  MajorOperatingSystemVersion;
  uint16_t  MinorOperatingSystemVersion;
  uint16_t  MajorImageVersion;
  uint16_t  MinorImageVersion;
  uint16_t  MajorSubsystemVersion;
  uint16_t  MinorSubsystemVersion;
  uint32_t  Win32VersionValue;
  uint32_t  SizeOfImage;
  uint32_t  SizeOfHeaders;
  uint32_t  CheckSum;
  uint16_t  Subsystem;
  uint16_t  DllCharacteristics;
  uint64_t  SizeOfStackReserve;
  uint64_t  SizeOfStackCommit;
  uint64_t  SizeOfHeapReserve;
  uint64_t  SizeOfHeapCommit;
  uint32_t  LoaderFlags;
  uint32_t  NumberOfRvaAndSizes;
  IMAGE_DATA_DIRECTORY DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];
} __attribute__ ((packed)) IMAGE_OPTIONAL_HEADER64;

typedef struct IMAGE_NT_HEADERS64 {
  uint32_t Signature;
  IMAGE_FILE_HEADER FileHeader;
  IMAGE_OPTIONAL_HEADER64 OptionalHeader;
} __attribute__ ((packed)) IMAGE_NT_HEADERS64;

typedef struct IMAGE_DEBUG_DIRECTORY {
  uint32_t Characteristics;
  uint32_t TimeDateStamp;
  uint16_t MajorVersion;
  uint16_t MinorVersion;
  uint32_t Type;
  uint32_t SizeOfData;
  uint32_t AddressOfRawData;
  uint32_t PointerToRawData;
} __attribute__ ((packed)) IMAGE_DEBUG_DIRECTORY;

#define IMAGE_DEBUG_TYPE_CODEVIEW   2
#endif

#define IMAGE_FILE_DEBUG_DIRECTORY  6

typedef struct guid_t {
    uint32_t a;
    uint16_t b;
    uint16_t c;
    uint8_t d[2];
    uint8_t  e[6];
} __attribute__ ((packed)) guid_t;

typedef struct OMFSignatureRSDS {
    char        Signature[4];
    guid_t      guid;
    uint32_t    age;
    char        name[];
} __attribute__ ((packed)) OMFSignatureRSDS;

#endif /* PE_H */
