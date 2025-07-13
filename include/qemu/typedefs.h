#ifndef QEMU_TYPEDEFS_H
#define QEMU_TYPEDEFS_H

/*
 * This header is for selectively avoiding #include just to get a
 * typedef name.
 *
 * Declaring a typedef name in its "obvious" place can result in
 * inclusion cycles, in particular for complete struct and union
 * types that need more types for their members.  It can also result
 * in headers pulling in many more headers, slowing down builds.
 *
 * You can break such cycles and unwanted dependencies by declaring
 * the typedef name here.
 *
 * For struct types used in only a few headers, judicious use of the
 * struct tag instead of the typedef name is commonly preferable.
 */

/*
 * Incomplete struct types
 * Please keep this list in case-insensitive alphabetical order.
 */
typedef struct AccelCPUState AccelCPUState;
typedef struct AccelOpsClass AccelOpsClass;
typedef struct AccelState AccelState;
typedef struct AddressSpace AddressSpace;
typedef struct AioContext AioContext;
typedef struct Aml Aml;
typedef struct ArchCPU ArchCPU;
typedef struct BdrvDirtyBitmap BdrvDirtyBitmap;
typedef struct BdrvDirtyBitmapIter BdrvDirtyBitmapIter;
typedef struct BlockBackend BlockBackend;
typedef struct BlockBackendRootState BlockBackendRootState;
typedef struct BlockDriverState BlockDriverState;
typedef struct BusClass BusClass;
typedef struct BusState BusState;
typedef struct Chardev Chardev;
typedef struct Clock Clock;
typedef struct ConfidentialGuestSupport ConfidentialGuestSupport;
typedef struct CPUArchState CPUArchState;
typedef struct CPUPluginState CPUPluginState;
typedef struct CPUState CPUState;
typedef struct CPUTLBEntryFull CPUTLBEntryFull;
typedef struct DeviceState DeviceState;
typedef struct DirtyBitmapSnapshot DirtyBitmapSnapshot;
typedef struct DisasContextBase DisasContextBase;
typedef struct DisplayChangeListener DisplayChangeListener;
typedef struct DriveInfo DriveInfo;
typedef struct DumpState DumpState;
typedef struct Error Error;
typedef struct EventNotifier EventNotifier;
typedef struct FlatView FlatView;
typedef struct FWCfgState FWCfgState;
typedef struct HostMemoryBackend HostMemoryBackend;
typedef struct I2CBus I2CBus;
typedef struct I2SCodec I2SCodec;
typedef struct IOMMUMemoryRegion IOMMUMemoryRegion;
typedef struct ISABus ISABus;
typedef struct ISADevice ISADevice;
typedef struct IsaDma IsaDma;
typedef struct JSONWriter JSONWriter;
typedef struct MACAddr MACAddr;
typedef struct MachineClass MachineClass;
typedef struct MachineState MachineState;
typedef struct MemoryListener MemoryListener;
typedef struct MemoryMappingList MemoryMappingList;
typedef struct MemoryRegion MemoryRegion;
typedef struct MemoryRegionCache MemoryRegionCache;
typedef struct MemoryRegionSection MemoryRegionSection;
typedef struct MigrationIncomingState MigrationIncomingState;
typedef struct MigrationState MigrationState;
typedef struct Monitor Monitor;
typedef struct MSIMessage MSIMessage;
typedef struct NetClientState NetClientState;
typedef struct NetFilterState NetFilterState;
typedef struct NICInfo NICInfo;
typedef struct Object Object;
typedef struct ObjectClass ObjectClass;
typedef struct PCIBridge PCIBridge;
typedef struct PCIBus PCIBus;
typedef struct PCIDevice PCIDevice;
typedef struct PCIEPort PCIEPort;
typedef struct PCIESlot PCIESlot;
typedef struct PCIExpressDevice PCIExpressDevice;
typedef struct PCIExpressHost PCIExpressHost;
typedef struct PCIHostDeviceAddress PCIHostDeviceAddress;
typedef struct PCIHostState PCIHostState;
typedef struct Property Property;
typedef struct PropertyInfo PropertyInfo;
typedef struct QBool QBool;
typedef struct QDict QDict;
typedef struct QEMUBH QEMUBH;
typedef struct QemuConsole QemuConsole;
typedef struct QEMUCursor QEMUCursor;
typedef struct QEMUFile QEMUFile;
typedef struct QemuMutex QemuMutex;
typedef struct QemuOpts QemuOpts;
typedef struct QemuOptsList QemuOptsList;
typedef struct QEMUSGList QEMUSGList;
typedef struct QemuSpin QemuSpin;
typedef struct QEMUTimer QEMUTimer;
typedef struct QEMUTimerListGroup QEMUTimerListGroup;
typedef struct QList QList;
typedef struct QNull QNull;
typedef struct QNum QNum;
typedef struct QObject QObject;
typedef struct QString QString;
typedef struct RAMBlock RAMBlock;
typedef struct Range Range;
typedef struct ReservedRegion ReservedRegion;
typedef struct SaveCompletePrecopyThreadData SaveCompletePrecopyThreadData;
typedef struct SHPCDevice SHPCDevice;
typedef struct SSIBus SSIBus;
typedef struct TCGCPUOps TCGCPUOps;
typedef struct TCGHelperInfo TCGHelperInfo;
typedef struct TaskState TaskState;
typedef struct TranslationBlock TranslationBlock;
typedef struct VirtIODevice VirtIODevice;
typedef struct Visitor Visitor;
typedef struct VMChangeStateEntry VMChangeStateEntry;
typedef struct VMStateDescription VMStateDescription;

/*
 * Pointer types
 * Such typedefs should be limited to cases where the typedef's users
 * are oblivious of its "pointer-ness".
 * Please keep this list in case-insensitive alphabetical order.
 */
typedef struct IRQState *qemu_irq;

/*
 * Function types
 */
typedef void (*qemu_irq_handler)(void *opaque, int n, int level);
typedef bool (*MigrationLoadThread)(void *opaque, bool *should_quit,
                                    Error **errp);
typedef bool (*SaveCompletePrecopyThreadHandler)(SaveCompletePrecopyThreadData *d,
                                                 Error **errp);

#endif /* QEMU_TYPEDEFS_H */
