#ifndef QEMU_TYPEDEFS_H
#define QEMU_TYPEDEFS_H

/* A load of opaque types so that device init declarations don't have to
   pull in all the real definitions.  */
typedef struct QEMUTimer QEMUTimer;
typedef struct QEMUTimerListGroup QEMUTimerListGroup;
typedef struct QEMUFile QEMUFile;
typedef struct QEMUBH QEMUBH;

typedef struct AioContext AioContext;

typedef struct Visitor Visitor;

struct Monitor;
typedef struct Monitor Monitor;
typedef struct MigrationParams MigrationParams;

typedef struct Property Property;
typedef struct PropertyInfo PropertyInfo;
typedef struct CompatProperty CompatProperty;
typedef struct DeviceState DeviceState;
typedef struct BusState BusState;
typedef struct BusClass BusClass;

typedef struct AddressSpace AddressSpace;
typedef struct MemoryRegion MemoryRegion;
typedef struct MemoryRegionSection MemoryRegionSection;
typedef struct MemoryListener MemoryListener;

typedef struct MemoryMappingList MemoryMappingList;

typedef struct QEMUMachine QEMUMachine;
typedef struct MachineClass MachineClass;
typedef struct MachineState MachineState;
typedef struct NICInfo NICInfo;
typedef struct HCIInfo HCIInfo;
typedef struct AudioState AudioState;
typedef struct BlockBackend BlockBackend;
typedef struct BlockDriverState BlockDriverState;
typedef struct DriveInfo DriveInfo;
typedef struct DisplayState DisplayState;
typedef struct DisplayChangeListener DisplayChangeListener;
typedef struct DisplaySurface DisplaySurface;
typedef struct PixelFormat PixelFormat;
typedef struct QemuConsole QemuConsole;
typedef struct CharDriverState CharDriverState;
typedef struct MACAddr MACAddr;
typedef struct NetClientState NetClientState;
typedef struct I2CBus I2CBus;
typedef struct ISABus ISABus;
typedef struct ISADevice ISADevice;
typedef struct SMBusDevice SMBusDevice;
typedef struct PCIHostState PCIHostState;
typedef struct PCIExpressHost PCIExpressHost;
typedef struct PCIBus PCIBus;
typedef struct PCIDevice PCIDevice;
typedef struct PCIExpressDevice PCIExpressDevice;
typedef struct PCIBridge PCIBridge;
typedef struct PCIEAERMsg PCIEAERMsg;
typedef struct PCIEAERLog PCIEAERLog;
typedef struct PCIEAERErr PCIEAERErr;
typedef struct PCIEPort PCIEPort;
typedef struct PCIESlot PCIESlot;
typedef struct MSIMessage MSIMessage;
typedef struct SerialState SerialState;
typedef struct PCMCIACardState PCMCIACardState;
typedef struct MouseTransformInfo MouseTransformInfo;
typedef struct uWireSlave uWireSlave;
typedef struct I2SCodec I2SCodec;
typedef struct SSIBus SSIBus;
typedef struct EventNotifier EventNotifier;
typedef struct VirtIODevice VirtIODevice;
typedef struct QEMUSGList QEMUSGList;
typedef struct QEMUSizedBuffer QEMUSizedBuffer;
typedef struct SHPCDevice SHPCDevice;
typedef struct FWCfgState FWCfgState;
typedef struct PcGuestInfo PcGuestInfo;
typedef struct Range Range;
typedef struct AdapterInfo AdapterInfo;

#endif /* QEMU_TYPEDEFS_H */
