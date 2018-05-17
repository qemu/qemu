/*
 * Windows crashdump
 *
 * Copyright (c) 2018 Virtuozzo International GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

typedef struct WinDumpPhyMemRun64 {
    uint64_t BasePage;
    uint64_t PageCount;
} QEMU_PACKED WinDumpPhyMemRun64;

typedef struct WinDumpPhyMemDesc64 {
    uint32_t NumberOfRuns;
    uint32_t unused;
    uint64_t NumberOfPages;
    WinDumpPhyMemRun64 Run[43];
} QEMU_PACKED WinDumpPhyMemDesc64;

typedef struct WinDumpExceptionRecord {
    uint32_t ExceptionCode;
    uint32_t ExceptionFlags;
    uint64_t ExceptionRecord;
    uint64_t ExceptionAddress;
    uint32_t NumberParameters;
    uint32_t unused;
    uint64_t ExceptionInformation[15];
} QEMU_PACKED WinDumpExceptionRecord;

typedef struct WinDumpHeader64 {
    char Signature[4];
    char ValidDump[4];
    uint32_t MajorVersion;
    uint32_t MinorVersion;
    uint64_t DirectoryTableBase;
    uint64_t PfnDatabase;
    uint64_t PsLoadedModuleList;
    uint64_t PsActiveProcessHead;
    uint32_t MachineImageType;
    uint32_t NumberProcessors;
    union {
        struct {
            uint32_t BugcheckCode;
            uint32_t unused0;
            uint64_t BugcheckParameter1;
            uint64_t BugcheckParameter2;
            uint64_t BugcheckParameter3;
            uint64_t BugcheckParameter4;
        };
        uint8_t BugcheckData[40];
    };
    uint8_t VersionUser[32];
    uint64_t KdDebuggerDataBlock;
    union {
        WinDumpPhyMemDesc64 PhysicalMemoryBlock;
        uint8_t PhysicalMemoryBlockBuffer[704];
    };
    union {
        uint8_t ContextBuffer[3000];
    };
    WinDumpExceptionRecord Exception;
    uint32_t DumpType;
    uint32_t unused1;
    uint64_t RequiredDumpSpace;
    uint64_t SystemTime;
    char Comment[128];
    uint64_t SystemUpTime;
    uint32_t MiniDumpFields;
    uint32_t SecondaryDataState;
    uint32_t ProductType;
    uint32_t SuiteMask;
    uint32_t WriterStatus;
    uint8_t unused2;
    uint8_t KdSecondaryVersion;
    uint8_t reserved[4018];
} QEMU_PACKED WinDumpHeader64;

void create_win_dump(DumpState *s, Error **errp);

#define KDBG_OWNER_TAG_OFFSET64         0x10
#define KDBG_KI_BUGCHECK_DATA_OFFSET64  0x88
#define KDBG_MM_PFN_DATABASE_OFFSET64   0xC0

#define VMCOREINFO_ELF_NOTE_HDR_SIZE    24
