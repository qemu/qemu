/*
 * Windows crashdump definitions
 *
 * Copyright (c) 2018 Virtuozzo International GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_WIN_DUMP_DEFS_H
#define QEMU_WIN_DUMP_DEFS_H

typedef struct WinDumpPhyMemRun32 {
    uint32_t BasePage;
    uint32_t PageCount;
} QEMU_PACKED WinDumpPhyMemRun32;

typedef struct WinDumpPhyMemRun64 {
    uint64_t BasePage;
    uint64_t PageCount;
} QEMU_PACKED WinDumpPhyMemRun64;

typedef struct WinDumpPhyMemDesc32 {
    uint32_t NumberOfRuns;
    uint32_t NumberOfPages;
    WinDumpPhyMemRun32 Run[86];
} QEMU_PACKED WinDumpPhyMemDesc32;

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

typedef struct WinDumpHeader32 {
    char Signature[4];
    char ValidDump[4];
    uint32_t MajorVersion;
    uint32_t MinorVersion;
    uint32_t DirectoryTableBase;
    uint32_t PfnDatabase;
    uint32_t PsLoadedModuleList;
    uint32_t PsActiveProcessHead;
    uint32_t MachineImageType;
    uint32_t NumberProcessors;
    union {
        struct {
            uint32_t BugcheckCode;
            uint32_t BugcheckParameter1;
            uint32_t BugcheckParameter2;
            uint32_t BugcheckParameter3;
            uint32_t BugcheckParameter4;
        };
        uint8_t BugcheckData[20];
    };
    uint8_t VersionUser[32];
    uint32_t reserved0;
    uint32_t KdDebuggerDataBlock;
    union {
        WinDumpPhyMemDesc32 PhysicalMemoryBlock;
        uint8_t PhysicalMemoryBlockBuffer[700];
    };
    uint8_t reserved1[3200];
    uint32_t RequiredDumpSpace;
    uint8_t reserved2[92];
} QEMU_PACKED WinDumpHeader32;

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

typedef union WinDumpHeader {
    struct {
        char Signature[4];
        char ValidDump[4];
    };
    WinDumpHeader32 x32;
    WinDumpHeader64 x64;
} WinDumpHeader;

#define KDBG_OWNER_TAG_OFFSET64             0x10
#define KDBG_MM_PFN_DATABASE_OFFSET64       0xC0
#define KDBG_KI_BUGCHECK_DATA_OFFSET64      0x88
#define KDBG_KI_PROCESSOR_BLOCK_OFFSET64    0x218
#define KDBG_OFFSET_PRCB_CONTEXT_OFFSET64   0x338

#define KDBG_OWNER_TAG_OFFSET           KDBG_OWNER_TAG_OFFSET64
#define KDBG_MM_PFN_DATABASE_OFFSET     KDBG_MM_PFN_DATABASE_OFFSET64
#define KDBG_KI_BUGCHECK_DATA_OFFSET    KDBG_KI_BUGCHECK_DATA_OFFSET64
#define KDBG_KI_PROCESSOR_BLOCK_OFFSET  KDBG_KI_PROCESSOR_BLOCK_OFFSET64
#define KDBG_OFFSET_PRCB_CONTEXT_OFFSET KDBG_OFFSET_PRCB_CONTEXT_OFFSET64

#define VMCOREINFO_ELF_NOTE_HDR_SIZE    24
#define VMCOREINFO_WIN_DUMP_NOTE_SIZE64 (sizeof(WinDumpHeader64) + \
                                         VMCOREINFO_ELF_NOTE_HDR_SIZE)
#define VMCOREINFO_WIN_DUMP_NOTE_SIZE32 (sizeof(WinDumpHeader32) + \
                                         VMCOREINFO_ELF_NOTE_HDR_SIZE)

#define WIN_CTX_X64 0x00100000L
#define WIN_CTX_X86 0x00010000L

#define WIN_CTX_CTL 0x00000001L
#define WIN_CTX_INT 0x00000002L
#define WIN_CTX_SEG 0x00000004L
#define WIN_CTX_FP  0x00000008L
#define WIN_CTX_DBG 0x00000010L
#define WIN_CTX_EXT 0x00000020L

#define WIN_CTX64_FULL  (WIN_CTX_X64 | WIN_CTX_CTL | WIN_CTX_INT | WIN_CTX_FP)
#define WIN_CTX64_ALL   (WIN_CTX64_FULL | WIN_CTX_SEG | WIN_CTX_DBG)

#define WIN_CTX32_FULL (WIN_CTX_X86 | WIN_CTX_CTL | WIN_CTX_INT | WIN_CTX_SEG)
#define WIN_CTX32_ALL (WIN_CTX32_FULL | WIN_CTX_FP | WIN_CTX_DBG | WIN_CTX_EXT)

#define LIVE_SYSTEM_DUMP    0x00000161

typedef struct WinM128A {
    uint64_t low;
    int64_t high;
} QEMU_ALIGNED(16) WinM128A;

typedef struct WinContext32 {
    uint32_t ContextFlags;

    uint32_t Dr0;
    uint32_t Dr1;
    uint32_t Dr2;
    uint32_t Dr3;
    uint32_t Dr6;
    uint32_t Dr7;

    uint8_t  FloatSave[112];

    uint32_t SegGs;
    uint32_t SegFs;
    uint32_t SegEs;
    uint32_t SegDs;

    uint32_t Edi;
    uint32_t Esi;
    uint32_t Ebx;
    uint32_t Edx;
    uint32_t Ecx;
    uint32_t Eax;

    uint32_t Ebp;
    uint32_t Eip;
    uint32_t SegCs;
    uint32_t EFlags;
    uint32_t Esp;
    uint32_t SegSs;

    uint8_t ExtendedRegisters[512];
} QEMU_ALIGNED(16) WinContext32;

typedef struct WinContext64 {
    uint64_t PHome[6];

    uint32_t ContextFlags;
    uint32_t MxCsr;

    uint16_t SegCs;
    uint16_t SegDs;
    uint16_t SegEs;
    uint16_t SegFs;
    uint16_t SegGs;
    uint16_t SegSs;
    uint32_t EFlags;

    uint64_t Dr0;
    uint64_t Dr1;
    uint64_t Dr2;
    uint64_t Dr3;
    uint64_t Dr6;
    uint64_t Dr7;

    uint64_t Rax;
    uint64_t Rcx;
    uint64_t Rdx;
    uint64_t Rbx;
    uint64_t Rsp;
    uint64_t Rbp;
    uint64_t Rsi;
    uint64_t Rdi;
    uint64_t R8;
    uint64_t R9;
    uint64_t R10;
    uint64_t R11;
    uint64_t R12;
    uint64_t R13;
    uint64_t R14;
    uint64_t R15;

    uint64_t Rip;

    struct {
        uint16_t ControlWord;
        uint16_t StatusWord;
        uint8_t TagWord;
        uint8_t Reserved1;
        uint16_t ErrorOpcode;
        uint32_t ErrorOffset;
        uint16_t ErrorSelector;
        uint16_t Reserved2;
        uint32_t DataOffset;
        uint16_t DataSelector;
        uint16_t Reserved3;
        uint32_t MxCsr;
        uint32_t MxCsr_Mask;
        WinM128A FloatRegisters[8];
        WinM128A XmmRegisters[16];
        uint8_t Reserved4[96];
    } FltSave;

    WinM128A VectorRegister[26];
    uint64_t VectorControl;

    uint64_t DebugControl;
    uint64_t LastBranchToRip;
    uint64_t LastBranchFromRip;
    uint64_t LastExceptionToRip;
    uint64_t LastExceptionFromRip;
} QEMU_ALIGNED(16) WinContext64;

typedef union WinContext {
    WinContext32 x32;
    WinContext64 x64;
} WinContext;

#endif /* QEMU_WIN_DUMP_DEFS_H */
