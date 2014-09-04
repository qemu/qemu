/*
 *      PCI Class, Vendor and Device IDs
 *
 *      Please keep sorted.
 *
 *      Abbreviated version of linux/pci_ids.h
 *
 *      QEMU-specific definitions belong in pci.h
 */
#ifndef HW_PCI_IDS_H
#define HW_PCI_IDS_H 1

/* Device classes and subclasses */

#define PCI_BASE_CLASS_STORAGE           0x01
#define PCI_BASE_CLASS_NETWORK           0x02

#define PCI_CLASS_STORAGE_SCSI           0x0100
#define PCI_CLASS_STORAGE_IDE            0x0101
#define PCI_CLASS_STORAGE_RAID           0x0104
#define PCI_CLASS_STORAGE_SATA           0x0106
#define PCI_CLASS_STORAGE_EXPRESS        0x0108
#define PCI_CLASS_STORAGE_OTHER          0x0180

#define PCI_CLASS_NETWORK_ETHERNET       0x0200

#define PCI_CLASS_DISPLAY_VGA            0x0300
#define PCI_CLASS_DISPLAY_OTHER          0x0380

#define PCI_CLASS_MULTIMEDIA_AUDIO       0x0401

#define PCI_CLASS_MEMORY_RAM             0x0500

#define PCI_CLASS_SYSTEM_OTHER           0x0880

#define PCI_CLASS_SERIAL_USB             0x0c03
#define PCI_CLASS_SERIAL_SMBUS           0x0c05

#define PCI_CLASS_BRIDGE_HOST            0x0600
#define PCI_CLASS_BRIDGE_ISA             0x0601
#define PCI_CLASS_BRIDGE_PCI             0x0604
#define PCI_CLASS_BRIDGE_PCI_INF_SUB     0x01
#define PCI_CLASS_BRIDGE_OTHER           0x0680

#define PCI_CLASS_COMMUNICATION_SERIAL   0x0700
#define PCI_CLASS_COMMUNICATION_OTHER    0x0780

#define PCI_CLASS_PROCESSOR_CO           0x0b40
#define PCI_CLASS_PROCESSOR_POWERPC      0x0b20

#define PCI_CLASS_OTHERS                 0xff

/* Vendors and devices.  Sort key: vendor first, device next. */

#define PCI_VENDOR_ID_LSI_LOGIC          0x1000
#define PCI_DEVICE_ID_LSI_53C810         0x0001
#define PCI_DEVICE_ID_LSI_53C895A        0x0012
#define PCI_DEVICE_ID_LSI_SAS1078        0x0060

#define PCI_VENDOR_ID_DEC                0x1011
#define PCI_DEVICE_ID_DEC_21154          0x0026

#define PCI_VENDOR_ID_CIRRUS             0x1013

#define PCI_VENDOR_ID_IBM                0x1014

#define PCI_VENDOR_ID_AMD                0x1022
#define PCI_DEVICE_ID_AMD_LANCE          0x2000
#define PCI_DEVICE_ID_AMD_SCSI           0x2020

#define PCI_VENDOR_ID_TI                 0x104c

#define PCI_VENDOR_ID_MOTOROLA           0x1057
#define PCI_DEVICE_ID_MOTOROLA_MPC106    0x0002
#define PCI_DEVICE_ID_MOTOROLA_RAVEN     0x4801

#define PCI_VENDOR_ID_APPLE              0x106b
#define PCI_DEVICE_ID_APPLE_UNI_N_AGP    0x0020
#define PCI_DEVICE_ID_APPLE_U3_AGP       0x004b

#define PCI_VENDOR_ID_SUN                0x108e
#define PCI_DEVICE_ID_SUN_EBUS           0x1000
#define PCI_DEVICE_ID_SUN_SIMBA          0x5000
#define PCI_DEVICE_ID_SUN_SABRE          0xa000

#define PCI_VENDOR_ID_CMD                0x1095
#define PCI_DEVICE_ID_CMD_646            0x0646

#define PCI_VENDOR_ID_REALTEK            0x10ec
#define PCI_DEVICE_ID_REALTEK_8139       0x8139

#define PCI_VENDOR_ID_XILINX             0x10ee

#define PCI_VENDOR_ID_VIA                0x1106
#define PCI_DEVICE_ID_VIA_ISA_BRIDGE     0x0686
#define PCI_DEVICE_ID_VIA_IDE            0x0571
#define PCI_DEVICE_ID_VIA_UHCI           0x3038
#define PCI_DEVICE_ID_VIA_ACPI           0x3057
#define PCI_DEVICE_ID_VIA_AC97           0x3058
#define PCI_DEVICE_ID_VIA_MC97           0x3068

#define PCI_VENDOR_ID_MARVELL            0x11ab

#define PCI_VENDOR_ID_ENSONIQ            0x1274
#define PCI_DEVICE_ID_ENSONIQ_ES1370     0x5000

#define PCI_VENDOR_ID_FREESCALE          0x1957
#define PCI_DEVICE_ID_MPC8533E           0x0030

#define PCI_VENDOR_ID_INTEL              0x8086
#define PCI_DEVICE_ID_INTEL_82378        0x0484
#define PCI_DEVICE_ID_INTEL_82441        0x1237
#define PCI_DEVICE_ID_INTEL_82801AA_5    0x2415
#define PCI_DEVICE_ID_INTEL_82801BA_11   0x244e
#define PCI_DEVICE_ID_INTEL_82801D       0x24CD
#define PCI_DEVICE_ID_INTEL_ESB_9        0x25ab
#define PCI_DEVICE_ID_INTEL_82371SB_0    0x7000
#define PCI_DEVICE_ID_INTEL_82371SB_1    0x7010
#define PCI_DEVICE_ID_INTEL_82371SB_2    0x7020
#define PCI_DEVICE_ID_INTEL_82371AB_0    0x7110
#define PCI_DEVICE_ID_INTEL_82371AB      0x7111
#define PCI_DEVICE_ID_INTEL_82371AB_2    0x7112
#define PCI_DEVICE_ID_INTEL_82371AB_3    0x7113

#define PCI_DEVICE_ID_INTEL_ICH9_0       0x2910
#define PCI_DEVICE_ID_INTEL_ICH9_1       0x2917
#define PCI_DEVICE_ID_INTEL_ICH9_2       0x2912
#define PCI_DEVICE_ID_INTEL_ICH9_3       0x2913
#define PCI_DEVICE_ID_INTEL_ICH9_4       0x2914
#define PCI_DEVICE_ID_INTEL_ICH9_5       0x2919
#define PCI_DEVICE_ID_INTEL_ICH9_6       0x2930
#define PCI_DEVICE_ID_INTEL_ICH9_7       0x2916
#define PCI_DEVICE_ID_INTEL_ICH9_8       0x2918

#define PCI_DEVICE_ID_INTEL_82801I_UHCI1 0x2934
#define PCI_DEVICE_ID_INTEL_82801I_UHCI2 0x2935
#define PCI_DEVICE_ID_INTEL_82801I_UHCI3 0x2936
#define PCI_DEVICE_ID_INTEL_82801I_UHCI4 0x2937
#define PCI_DEVICE_ID_INTEL_82801I_UHCI5 0x2938
#define PCI_DEVICE_ID_INTEL_82801I_UHCI6 0x2939
#define PCI_DEVICE_ID_INTEL_82801I_EHCI1 0x293a
#define PCI_DEVICE_ID_INTEL_82801I_EHCI2 0x293c
#define PCI_DEVICE_ID_INTEL_82599_SFP_VF 0x10ed

#define PCI_DEVICE_ID_INTEL_Q35_MCH      0x29c0

#define PCI_VENDOR_ID_XEN                0x5853
#define PCI_DEVICE_ID_XEN_PLATFORM       0x0001

#define PCI_VENDOR_ID_NEC                0x1033
#define PCI_DEVICE_ID_NEC_UPD720200      0x0194

#define PCI_VENDOR_ID_TEWS               0x1498
#define PCI_DEVICE_ID_TEWS_TPCI200       0x30C8

#endif
