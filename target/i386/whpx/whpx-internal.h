#ifndef TARGET_I386_WHPX_INTERNAL_H
#define TARGET_I386_WHPX_INTERNAL_H

#include <windows.h>
#include <winhvplatform.h>
#include <winhvemulation.h>

#include "hw/i386/apic.h"

typedef enum WhpxBreakpointState {
    WHPX_BP_CLEARED = 0,
    WHPX_BP_SET_PENDING,
    WHPX_BP_SET,
    WHPX_BP_CLEAR_PENDING,
} WhpxBreakpointState;

struct whpx_breakpoint {
    vaddr address;
    WhpxBreakpointState state;
    uint8_t original_instruction;
};

struct whpx_breakpoint_collection {
    int allocated, used;
    struct whpx_breakpoint data[0];
};

struct whpx_breakpoints {
    int original_address_count;
    vaddr *original_addresses;

    struct whpx_breakpoint_collection *breakpoints;
};

struct whpx_state {
    uint64_t mem_quota;
    WHV_PARTITION_HANDLE partition;
    uint64_t exception_exit_bitmap;
    int32_t running_cpus;
    struct whpx_breakpoints breakpoints;
    bool step_pending;

    bool kernel_irqchip_allowed;
    bool kernel_irqchip_required;
    bool apic_in_platform;
};

extern struct whpx_state whpx_global;
void whpx_apic_get(APICCommonState *s);

#define WHV_E_UNKNOWN_CAPABILITY 0x80370300L

/* This should eventually come from the Windows SDK */
#define WHV_E_UNKNOWN_PROPERTY 0x80370302

#define LIST_WINHVPLATFORM_FUNCTIONS(X) \
  X(HRESULT, WHvGetCapability, (WHV_CAPABILITY_CODE CapabilityCode, VOID* CapabilityBuffer, UINT32 CapabilityBufferSizeInBytes, UINT32* WrittenSizeInBytes)) \
  X(HRESULT, WHvCreatePartition, (WHV_PARTITION_HANDLE* Partition)) \
  X(HRESULT, WHvSetupPartition, (WHV_PARTITION_HANDLE Partition)) \
  X(HRESULT, WHvDeletePartition, (WHV_PARTITION_HANDLE Partition)) \
  X(HRESULT, WHvGetPartitionProperty, (WHV_PARTITION_HANDLE Partition, WHV_PARTITION_PROPERTY_CODE PropertyCode, VOID* PropertyBuffer, UINT32 PropertyBufferSizeInBytes, UINT32* WrittenSizeInBytes)) \
  X(HRESULT, WHvSetPartitionProperty, (WHV_PARTITION_HANDLE Partition, WHV_PARTITION_PROPERTY_CODE PropertyCode, const VOID* PropertyBuffer, UINT32 PropertyBufferSizeInBytes)) \
  X(HRESULT, WHvMapGpaRange, (WHV_PARTITION_HANDLE Partition, VOID* SourceAddress, WHV_GUEST_PHYSICAL_ADDRESS GuestAddress, UINT64 SizeInBytes, WHV_MAP_GPA_RANGE_FLAGS Flags)) \
  X(HRESULT, WHvUnmapGpaRange, (WHV_PARTITION_HANDLE Partition, WHV_GUEST_PHYSICAL_ADDRESS GuestAddress, UINT64 SizeInBytes)) \
  X(HRESULT, WHvTranslateGva, (WHV_PARTITION_HANDLE Partition, UINT32 VpIndex, WHV_GUEST_VIRTUAL_ADDRESS Gva, WHV_TRANSLATE_GVA_FLAGS TranslateFlags, WHV_TRANSLATE_GVA_RESULT* TranslationResult, WHV_GUEST_PHYSICAL_ADDRESS* Gpa)) \
  X(HRESULT, WHvCreateVirtualProcessor, (WHV_PARTITION_HANDLE Partition, UINT32 VpIndex, UINT32 Flags)) \
  X(HRESULT, WHvDeleteVirtualProcessor, (WHV_PARTITION_HANDLE Partition, UINT32 VpIndex)) \
  X(HRESULT, WHvRunVirtualProcessor, (WHV_PARTITION_HANDLE Partition, UINT32 VpIndex, VOID* ExitContext, UINT32 ExitContextSizeInBytes)) \
  X(HRESULT, WHvCancelRunVirtualProcessor, (WHV_PARTITION_HANDLE Partition, UINT32 VpIndex, UINT32 Flags)) \
  X(HRESULT, WHvGetVirtualProcessorRegisters, (WHV_PARTITION_HANDLE Partition, UINT32 VpIndex, const WHV_REGISTER_NAME* RegisterNames, UINT32 RegisterCount, WHV_REGISTER_VALUE* RegisterValues)) \
  X(HRESULT, WHvSetVirtualProcessorRegisters, (WHV_PARTITION_HANDLE Partition, UINT32 VpIndex, const WHV_REGISTER_NAME* RegisterNames, UINT32 RegisterCount, const WHV_REGISTER_VALUE* RegisterValues)) \

/*
 * These are supplemental functions that may not be present
 * on all versions and are not critical for basic functionality.
 */
#define LIST_WINHVPLATFORM_FUNCTIONS_SUPPLEMENTAL(X) \
  X(HRESULT, WHvSuspendPartitionTime, (WHV_PARTITION_HANDLE Partition)) \
  X(HRESULT, WHvRequestInterrupt, (WHV_PARTITION_HANDLE Partition, \
        WHV_INTERRUPT_CONTROL* Interrupt, UINT32 InterruptControlSize)) \
  X(HRESULT, WHvGetVirtualProcessorInterruptControllerState2, \
        (WHV_PARTITION_HANDLE Partition, UINT32 VpIndex, PVOID State, \
         UINT32 StateSize, UINT32* WrittenSize)) \
  X(HRESULT, WHvSetVirtualProcessorInterruptControllerState2, \
        (WHV_PARTITION_HANDLE Partition, UINT32 VpIndex, PVOID State, \
         UINT32 StateSize)) \

#define LIST_WINHVEMULATION_FUNCTIONS(X) \
  X(HRESULT, WHvEmulatorCreateEmulator, (const WHV_EMULATOR_CALLBACKS* Callbacks, WHV_EMULATOR_HANDLE* Emulator)) \
  X(HRESULT, WHvEmulatorDestroyEmulator, (WHV_EMULATOR_HANDLE Emulator)) \
  X(HRESULT, WHvEmulatorTryIoEmulation, (WHV_EMULATOR_HANDLE Emulator, VOID* Context, const WHV_VP_EXIT_CONTEXT* VpContext, const WHV_X64_IO_PORT_ACCESS_CONTEXT* IoInstructionContext, WHV_EMULATOR_STATUS* EmulatorReturnStatus)) \
  X(HRESULT, WHvEmulatorTryMmioEmulation, (WHV_EMULATOR_HANDLE Emulator, VOID* Context, const WHV_VP_EXIT_CONTEXT* VpContext, const WHV_MEMORY_ACCESS_CONTEXT* MmioInstructionContext, WHV_EMULATOR_STATUS* EmulatorReturnStatus)) \

#define WHP_DEFINE_TYPE(return_type, function_name, signature) \
    typedef return_type (WINAPI *function_name ## _t) signature;

#define WHP_DECLARE_MEMBER(return_type, function_name, signature) \
    function_name ## _t function_name;

/* Define function typedef */
LIST_WINHVPLATFORM_FUNCTIONS(WHP_DEFINE_TYPE)
LIST_WINHVEMULATION_FUNCTIONS(WHP_DEFINE_TYPE)
LIST_WINHVPLATFORM_FUNCTIONS_SUPPLEMENTAL(WHP_DEFINE_TYPE)

struct WHPDispatch {
    LIST_WINHVPLATFORM_FUNCTIONS(WHP_DECLARE_MEMBER)
    LIST_WINHVEMULATION_FUNCTIONS(WHP_DECLARE_MEMBER)
    LIST_WINHVPLATFORM_FUNCTIONS_SUPPLEMENTAL(WHP_DECLARE_MEMBER)
};

extern struct WHPDispatch whp_dispatch;

bool init_whp_dispatch(void);

typedef enum WHPFunctionList {
    WINHV_PLATFORM_FNS_DEFAULT,
    WINHV_EMULATION_FNS_DEFAULT,
    WINHV_PLATFORM_FNS_SUPPLEMENTAL
} WHPFunctionList;

#endif /* TARGET_I386_WHPX_INTERNAL_H */
