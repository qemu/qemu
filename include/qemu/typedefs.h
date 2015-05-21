#ifndef QEMU_TYPEDEFS_H
#define QEMU_TYPEDEFS_H

/* A load of opaque types so that device init declarations don't have to
   pull in all the real definitions.  */
struct Monitor;

/* Please keep this list in alphabetical order */
typedef struct AdapterInfo AdapterInfo;
typedef struct AddressSpace AddressSpace;
typedef struct AioContext AioContext;
typedef struct AudioState AudioState;
typedef struct BlockBackend BlockBackend;
typedef struct BlockDriverState BlockDriverState;
typedef struct BusClass BusClass;
typedef struct BusState BusState;
typedef struct CharDriverState CharDriverState;
typedef struct CompatProperty CompatProperty;
typedef struct DeviceState DeviceState;
typedef struct DeviceListener DeviceListener;
typedef struct DisplayChangeListener DisplayChangeListener;
typedef struct DisplayState DisplayState;
typedef struct DisplaySurface DisplaySurface;
typedef struct DriveInfo DriveInfo;
typedef struct EventNotifier EventNotifier;
typedef struct FWCfgIoState FWCfgIoState;
typedef struct FWCfgMemState FWCfgMemState;
typedef struct FWCfgState FWCfgState;
typedef struct HCIInfo HCIInfo;
typedef struct I2CBus I2CBus;
typedef struct I2SCodec I2SCodec;
typedef struct ISABus ISABus;
typedef struct ISADevice ISADevice;
typedef struct LoadStateEntry LoadStateEntry;
typedef struct MACAddr MACAddr;
typedef struct MachineClass MachineClass;
typedef struct MachineState MachineState;
typedef struct MemoryListener MemoryListener;
typedef struct MemoryMappingList MemoryMappingList;
typedef struct MemoryRegion MemoryRegion;
typedef struct MemoryRegionSection MemoryRegionSection;
typedef struct MigrationIncomingState MigrationIncomingState;
typedef struct MigrationParams MigrationParams;
typedef struct Monitor Monitor;
typedef struct MouseTransformInfo MouseTransformInfo;
typedef struct MSIMessage MSIMessage;
typedef struct NetClientState NetClientState;
typedef struct NICInfo NICInfo;
typedef struct PcGuestInfo PcGuestInfo;
typedef struct PCIBridge PCIBridge;
typedef struct PCIBus PCIBus;
typedef struct PCIDevice PCIDevice;
typedef struct PCIEAERErr PCIEAERErr;
typedef struct PCIEAERLog PCIEAERLog;
typedef struct PCIEAERMsg PCIEAERMsg;
typedef struct PCIEPort PCIEPort;
typedef struct PCIESlot PCIESlot;
typedef struct PCIExpressDevice PCIExpressDevice;
typedef struct PCIExpressHost PCIExpressHost;
typedef struct PCIHostState PCIHostState;
typedef struct PCMCIACardState PCMCIACardState;
typedef struct PixelFormat PixelFormat;
typedef struct PropertyInfo PropertyInfo;
typedef struct Property Property;
typedef struct QEMUBH QEMUBH;
typedef struct QemuConsole QemuConsole;
typedef struct QEMUFile QEMUFile;
typedef struct QEMUMachine QEMUMachine;
typedef struct QEMUSGList QEMUSGList;
typedef struct QEMUSizedBuffer QEMUSizedBuffer;
typedef struct QEMUTimerListGroup QEMUTimerListGroup;
typedef struct QEMUTimer QEMUTimer;
typedef struct Range Range;
typedef struct SerialState SerialState;
typedef struct SHPCDevice SHPCDevice;
typedef struct SMBusDevice SMBusDevice;
typedef struct SSIBus SSIBus;
typedef struct uWireSlave uWireSlave;
typedef struct VirtIODevice VirtIODevice;
typedef struct Visitor Visitor;

#endif /* QEMU_TYPEDEFS_H */
