/*
 * Copyright (c) 2018 Virtuozzo International GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 *
 */

#ifndef KDBG_H
#define KDBG_H

typedef struct DBGKD_GET_VERSION64 {
    uint16_t  MajorVersion;
    uint16_t  MinorVersion;
    uint8_t   ProtocolVersion;
    uint8_t   KdSecondaryVersion;
    uint16_t  Flags;
    uint16_t  MachineType;
    uint8_t   MaxPacketType;
    uint8_t   MaxStateChange;
    uint8_t   MaxManipulate;
    uint8_t   Simulation;
    uint16_t  Unused[1];
    uint64_t KernBase;
    uint64_t PsLoadedModuleList;
    uint64_t DebuggerDataList;
} DBGKD_GET_VERSION64;

typedef struct DBGKD_DEBUG_DATA_HEADER64 {
    struct LIST_ENTRY64 {
       struct LIST_ENTRY64 *Flink;
       struct LIST_ENTRY64 *Blink;
    } List;
    uint32_t           OwnerTag;
    uint32_t           Size;
} DBGKD_DEBUG_DATA_HEADER64;

typedef struct KDDEBUGGER_DATA64 {
    DBGKD_DEBUG_DATA_HEADER64 Header;

    uint64_t KernBase;
    uint64_t BreakpointWithStatus;
    uint64_t SavedContext;
    uint16_t ThCallbackStack;
    uint16_t NextCallback;
    uint16_t FramePointer;
    uint16_t PaeEnabled:1;
    uint64_t KiCallUserMode;
    uint64_t KeUserCallbackDispatcher;
    uint64_t PsLoadedModuleList;
    uint64_t PsActiveProcessHead;
    uint64_t PspCidTable;
    uint64_t ExpSystemResourcesList;
    uint64_t ExpPagedPoolDescriptor;
    uint64_t ExpNumberOfPagedPools;
    uint64_t KeTimeIncrement;
    uint64_t KeBugCheckCallbackListHead;
    uint64_t KiBugcheckData;
    uint64_t IopErrorLogListHead;
    uint64_t ObpRootDirectoryObject;
    uint64_t ObpTypeObjectType;
    uint64_t MmSystemCacheStart;
    uint64_t MmSystemCacheEnd;
    uint64_t MmSystemCacheWs;
    uint64_t MmPfnDatabase;
    uint64_t MmSystemPtesStart;
    uint64_t MmSystemPtesEnd;
    uint64_t MmSubsectionBase;
    uint64_t MmNumberOfPagingFiles;
    uint64_t MmLowestPhysicalPage;
    uint64_t MmHighestPhysicalPage;
    uint64_t MmNumberOfPhysicalPages;
    uint64_t MmMaximumNonPagedPoolInBytes;
    uint64_t MmNonPagedSystemStart;
    uint64_t MmNonPagedPoolStart;
    uint64_t MmNonPagedPoolEnd;
    uint64_t MmPagedPoolStart;
    uint64_t MmPagedPoolEnd;
    uint64_t MmPagedPoolInformation;
    uint64_t MmPageSize;
    uint64_t MmSizeOfPagedPoolInBytes;
    uint64_t MmTotalCommitLimit;
    uint64_t MmTotalCommittedPages;
    uint64_t MmSharedCommit;
    uint64_t MmDriverCommit;
    uint64_t MmProcessCommit;
    uint64_t MmPagedPoolCommit;
    uint64_t MmExtendedCommit;
    uint64_t MmZeroedPageListHead;
    uint64_t MmFreePageListHead;
    uint64_t MmStandbyPageListHead;
    uint64_t MmModifiedPageListHead;
    uint64_t MmModifiedNoWritePageListHead;
    uint64_t MmAvailablePages;
    uint64_t MmResidentAvailablePages;
    uint64_t PoolTrackTable;
    uint64_t NonPagedPoolDescriptor;
    uint64_t MmHighestUserAddress;
    uint64_t MmSystemRangeStart;
    uint64_t MmUserProbeAddress;
    uint64_t KdPrintCircularBuffer;
    uint64_t KdPrintCircularBufferEnd;
    uint64_t KdPrintWritePointer;
    uint64_t KdPrintRolloverCount;
    uint64_t MmLoadedUserImageList;

    /* NT 5.1 Addition */

    uint64_t NtBuildLab;
    uint64_t KiNormalSystemCall;

    /* NT 5.0 hotfix addition */

    uint64_t KiProcessorBlock;
    uint64_t MmUnloadedDrivers;
    uint64_t MmLastUnloadedDriver;
    uint64_t MmTriageActionTaken;
    uint64_t MmSpecialPoolTag;
    uint64_t KernelVerifier;
    uint64_t MmVerifierData;
    uint64_t MmAllocatedNonPagedPool;
    uint64_t MmPeakCommitment;
    uint64_t MmTotalCommitLimitMaximum;
    uint64_t CmNtCSDVersion;

    /* NT 5.1 Addition */

    uint64_t MmPhysicalMemoryBlock;
    uint64_t MmSessionBase;
    uint64_t MmSessionSize;
    uint64_t MmSystemParentTablePage;

    /* Server 2003 addition */

    uint64_t MmVirtualTranslationBase;
    uint16_t OffsetKThreadNextProcessor;
    uint16_t OffsetKThreadTeb;
    uint16_t OffsetKThreadKernelStack;
    uint16_t OffsetKThreadInitialStack;
    uint16_t OffsetKThreadApcProcess;
    uint16_t OffsetKThreadState;
    uint16_t OffsetKThreadBStore;
    uint16_t OffsetKThreadBStoreLimit;
    uint16_t SizeEProcess;
    uint16_t OffsetEprocessPeb;
    uint16_t OffsetEprocessParentCID;
    uint16_t OffsetEprocessDirectoryTableBase;
    uint16_t SizePrcb;
    uint16_t OffsetPrcbDpcRoutine;
    uint16_t OffsetPrcbCurrentThread;
    uint16_t OffsetPrcbMhz;
    uint16_t OffsetPrcbCpuType;
    uint16_t OffsetPrcbVendorString;
    uint16_t OffsetPrcbProcStateContext;
    uint16_t OffsetPrcbNumber;
    uint16_t SizeEThread;
    uint64_t KdPrintCircularBufferPtr;
    uint64_t KdPrintBufferSize;
    uint64_t KeLoaderBlock;
    uint16_t SizePcr;
    uint16_t OffsetPcrSelfPcr;
    uint16_t OffsetPcrCurrentPrcb;
    uint16_t OffsetPcrContainedPrcb;
    uint16_t OffsetPcrInitialBStore;
    uint16_t OffsetPcrBStoreLimit;
    uint16_t OffsetPcrInitialStack;
    uint16_t OffsetPcrStackLimit;
    uint16_t OffsetPrcbPcrPage;
    uint16_t OffsetPrcbProcStateSpecialReg;
    uint16_t GdtR0Code;
    uint16_t GdtR0Data;
    uint16_t GdtR0Pcr;
    uint16_t GdtR3Code;
    uint16_t GdtR3Data;
    uint16_t GdtR3Teb;
    uint16_t GdtLdt;
    uint16_t GdtTss;
    uint16_t Gdt64R3CmCode;
    uint16_t Gdt64R3CmTeb;
    uint64_t IopNumTriageDumpDataBlocks;
    uint64_t IopTriageDumpDataBlocks;

    /* Longhorn addition */

    uint64_t VfCrashDataBlock;
    uint64_t MmBadPagesDetected;
    uint64_t MmZeroedPageSingleBitErrorsDetected;

    /* Windows 7 addition */

    uint64_t EtwpDebuggerData;
    uint16_t OffsetPrcbContext;
} KDDEBUGGER_DATA64;

#endif /* KDBG_H */
