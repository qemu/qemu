#pragma once

/*
 * The first part of this file consists of hardware identifiers used by QEMU.
 * The default options for this are taken directly from what QEMU uses by default.
 * i.e. Change nothing and you'll basically look the same as a stock QEMU build.
 * As such... you probably should change these.
 * Tip: Try and use believable values (major companies and their products, serial number formatting, etc.)
 * Make sure serial numbers follow Luhn's algorithm!
 *
 */

/* HARDWARE IDENTIFIERS */

// hw/usb/dev-audio.c
#define USB_AUDIO_DEVICE_MANUFACTURER "RealTek"
#define USB_AUDIO_DEVICE_PRODUCT "RealTek USB Audio"
#define USB_AUDIO_DEVICE_SERIALNUMBER "1"
#define USB_AUDIO_DEVICE_INTERFACE "RealTek USB Audio Interface"

// hw/usb/dev-hid.c
#define USB_HID_DEVICE_MANUFACTURER "Asus"
#define USB_HID_DEVICE_MOUSE "Asus ROG Spatha X"
#define USB_HID_DEVICE_TABLET "Wacom STL320"
#define USB_HID_DEVICE_KEYBOARD "Corsair K70 RGB PRO"
#define USB_HID_DEVICE_SERIAL_MOUSE "4211159303728043015"
#define USB_HID_DEVICE_SERIAL_TABLET "4096074267034"
#define USB_HID_DEVICE_SERIAL_KEYBOARD "4797515588637"
#define USB_HID_DEVICE_VENDOR_ID_MOUSE 0x0627
#define USB_HID_DEVICE_VENDOR_ID_TABLET 0x0627
#define USB_HID_DEVICE_VENDOR_ID_KEYBOARD 0x0627
#define USB_HID_DEVICE_PRODUCT_ID_MOUSE 0x0001
#define USB_HID_DEVICE_PRODUCT_ID_TABLET 0x0001
#define USB_HID_DEVICE_PRODUCT_ID_KEYBOARD 0x0001

// hw/usb/dev-hub.c
#define USB_HUB_MANUFACTURER "Kingston"
#define USB_HUB_PRODUCT "Kingston USB 3.0 Hub"
#define USB_HUB_SERIALNUMBER "4790312813688"
#define USB_HUB_VENDOR_ID 0x0409
#define USB_HUB_PRODUCT_ID 0x55aa

// hw/usb/dev-network.c
/*
 * ! WARNING !
 * Changing these settings may break something on your guest system! (USB Networking)
 * ! WARNING !
 */
#define USB_NET_CDC_VENDOR_NUM 0x0525
#define USB_NET_CDC_PRODUCT_NUM 0xa4a1
#define USB_NET_RNDIS_VENDOR_NUM 0x0525
#define USB_NET_RNDIS_PRODUCT_NUM 0xa4a2
#define USB_NET_MANUFACTURER "QEMU"
#define USB_NET_PRODUCT "RNDIS/QEMU USB Network Device"
#define USB_NET_ETHERNET_ADDRESS "400102030405"
#define USB_NET_DATA_INTEFACE "QEMU USB Net Data Interface"
#define USB_NET_CONTROL_INTERFACE "QEMU USB Net Control Interface"
#define USB_NET_RNDIS_CONTROL_INTERFACE "QEMU USB Net RNDIS Control Interface"
#define USB_NET_CDC "QEMU USB Net CDC"
#define USB_NET_SUBSET "QEMU USB Net Subset"
#define USB_NET_RNDIS "QEMU USB Net RNDIS"
#define USB_NET_SERIALNUMBER "1"
#define USB_NET_OID_VENDOR_DESCRIPTION "QEMU USB RNDIS Net"
#define USB_NET_PRODUCT_DESCRIPTION "QEMU USB Network Interface"

// hw/usb/dev-storage.c
// note: this file is specifically for *usb* storage devices. SATA drives and whatnot are coming up soon though.
#define USB_STORAGE_MANUFACTURER "Kingston"
#define USB_STORAGE_PRODUCT "Kingston DataTraveller"
#define USB_STORAGE_SERIALNUMBER "4117394840571"
#define USB_STORAGE_CONFIG_FULL "Full speed config (usb 1.1)"     // I'm not sure what to do with these.
#define USB_STORAGE_CONFIG_HIGH "High speed config (usb 2.0)"     // Maybe slightly tinker with the generation?
#define USB_STORAGE_CONFIG_SUPER "Super speed config (usb 3.0)"   // (usb 3.0 -> 3.1 ? I dunno.)
#define USB_STORAGE_VENDOR_ID 0x46f4 // This is literally just the CRC16() of "QEMU". Definitely change this one ;D
#define USB_STORAGE_PRODUCT_ID 0x0001
#define USB_STORAGE_PRODUCT_DESCRIPTION "QEMU USB MSD"

// hw/scsi/scsi-disk.c
#define SCSI_DISK_VENDOR "Seagate"
#define SCSI_HARDDISK_PRODUCT "Seagate ST2000DMZ08"
#define SCSI_CDROM_PRODUCT "Seagate CDROM"

// hw/scsi/scsi-bus.c
// the sizing of these variables matter!! (values are memcpy'd with a specific size.)
// make sure your values FIT THE SIZE EXACTLY OR BAD THINGS WILL PROBABLY HAPPEN
#define SCSI_BUS_IDENTIFY_VENDOR "KNGS    "  // 8 bytes!!
#define SCSI_BUS_IDENTIFY_PRODUCT "KNGS TARGET     " // 16 bytes!!

// hw/ide/core.c
#define IDE_DISK_VENDOR "Kingston"
#define IDE_DVD_ROM_PRODUCT "Kingston DVD-ROM"
#define IDE_MICRODRIVE_PRODUCT "Kingston MICRODRIVE"
#define IDE_HARDDISK_PRODUCT "Kingston SA400S37"
// These SMART attributes are hardcoded into QEMU, and are worth changing.
#define IDE_SMART_DATA_READ_ERROR_READ { 0x01, 0x03, 0x00, 0x64, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06}
#define IDE_SMART_DATA_SPINUP { 0x03, 0x03, 0x00, 0x64, 0x64, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
#define IDE_SMART_DATA_STARTSTOP_COUNT { 0x04, 0x02, 0x00, 0x64, 0x64, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14}
#define IDE_SMART_DATA_REMAPPED_SECTORS_COUNT { 0x05, 0x03, 0x00, 0x64, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x24}
#define IDE_SMART_DATA_POWERONHOURS_COUNT { 0x09, 0x03, 0x00, 0x64, 0x64, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
#define IDE_SMART_DATA_POWERCYCLE_COUNT { 0x0c, 0x03, 0x00, 0x64, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
#define IDE_SMART_DATA_TEMPERATURE { 190,  0x03, 0x00, 0x45, 0x45, 0x1f, 0x00, 0x1f, 0x1f, 0x00, 0x00, 0x32}

#define IDE_SERIALNUMBER_FORMAT_STRING "KS%05d"

// block/bochs.c
#define BLOCK_BOCHS_HEADER_MAGIC "Reals Virtual HD Image" // 32 bytes!

/* FEATURE TOGGLES */
// change `true` to `false` to disable a feature.

// i386/kvm/kvm.c
#define KVM_EXPOSE_HYPERVISOR_STRING false  // Whether QEMU should return KVMKVMKVM upon receiving CPUID 0x40000000
#define KVM_SET_HYPERVISOR_FLAG false // Whether QEMU should set the hypervisor bitflag in CPUID. (31st bit)

// hw/smbios/smbios.c
/*
 * If true, sets bios_characteristics_extension_bytes[1] to 0x14
 * SMBIOS reference: https://ia803400.us.archive.org/26/items/specs-smbios/specs-smbios.pdf (page 30, specifically)
 */
#define SMBIOS_SET_BIOS_EXTENSIONS_VM_BIT false