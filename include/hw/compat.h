#ifndef HW_COMPAT_H
#define HW_COMPAT_H

#define HW_COMPAT_3_0 \
    /* empty */

#define HW_COMPAT_2_12 \
    {\
        .driver   = "migration",\
        .property = "decompress-error-check",\
        .value    = "off",\
    },{\
        .driver   = "hda-audio",\
        .property = "use-timer",\
        .value    = "false",\
    },{\
        .driver   = "cirrus-vga",\
        .property = "global-vmstate",\
        .value    = "true",\
    },{\
        .driver   = "VGA",\
        .property = "global-vmstate",\
        .value    = "true",\
    },{\
        .driver   = "vmware-svga",\
        .property = "global-vmstate",\
        .value    = "true",\
    },{\
        .driver   = "qxl-vga",\
        .property = "global-vmstate",\
        .value    = "true",\
    },

#define HW_COMPAT_2_11 \
    {\
        .driver   = "hpet",\
        .property = "hpet-offset-saved",\
        .value    = "false",\
    },{\
        .driver   = "virtio-blk-pci",\
        .property = "vectors",\
        .value    = "2",\
    },{\
        .driver   = "vhost-user-blk-pci",\
        .property = "vectors",\
        .value    = "2",\
    },{\
        .driver   = "e1000",\
        .property = "migrate_tso_props",\
        .value    = "off",\
    },

#define HW_COMPAT_2_10 \
    {\
        .driver   = "virtio-mouse-device",\
        .property = "wheel-axis",\
        .value    = "false",\
    },{\
        .driver   = "virtio-tablet-device",\
        .property = "wheel-axis",\
        .value    = "false",\
    },

#define HW_COMPAT_2_9 \
    {\
        .driver   = "pci-bridge",\
        .property = "shpc",\
        .value    = "off",\
    },{\
        .driver   = "intel-iommu",\
        .property = "pt",\
        .value    = "off",\
    },{\
        .driver   = "virtio-net-device",\
        .property = "x-mtu-bypass-backend",\
        .value    = "off",\
    },{\
        .driver   = "pcie-root-port",\
        .property = "x-migrate-msix",\
        .value    = "false",\
    },

#define HW_COMPAT_2_8 \
    {\
        .driver   = "fw_cfg_mem",\
        .property = "x-file-slots",\
        .value    = stringify(0x10),\
    },{\
        .driver   = "fw_cfg_io",\
        .property = "x-file-slots",\
        .value    = stringify(0x10),\
    },{\
        .driver   = "pflash_cfi01",\
        .property = "old-multiple-chip-handling",\
        .value    = "on",\
    },{\
        .driver   = "pci-bridge",\
        .property = "shpc",\
        .value    = "on",\
    },{\
        .driver   = TYPE_PCI_DEVICE,\
        .property = "x-pcie-extcap-init",\
        .value    = "off",\
    },{\
        .driver   = "virtio-pci",\
        .property = "x-pcie-deverr-init",\
        .value    = "off",\
    },{\
        .driver   = "virtio-pci",\
        .property = "x-pcie-lnkctl-init",\
        .value    = "off",\
    },{\
        .driver   = "virtio-pci",\
        .property = "x-pcie-pm-init",\
        .value    = "off",\
    },{\
        .driver   = "cirrus-vga",\
        .property = "vgamem_mb",\
        .value    = "8",\
    },{\
        .driver   = "isa-cirrus-vga",\
        .property = "vgamem_mb",\
        .value    = "8",\
    },

#define HW_COMPAT_2_7 \
    {\
        .driver   = "virtio-pci",\
        .property = "page-per-vq",\
        .value    = "on",\
    },{\
        .driver   = "virtio-serial-device",\
        .property = "emergency-write",\
        .value    = "off",\
    },{\
        .driver   = "ioapic",\
        .property = "version",\
        .value    = "0x11",\
    },{\
        .driver   = "intel-iommu",\
        .property = "x-buggy-eim",\
        .value    = "true",\
    },{\
        .driver   = "virtio-pci",\
        .property = "x-ignore-backend-features",\
        .value    = "on",\
    },

#define HW_COMPAT_2_6 \
    {\
        .driver   = "virtio-mmio",\
        .property = "format_transport_address",\
        .value    = "off",\
    },{\
        .driver   = "virtio-pci",\
        .property = "disable-modern",\
        .value    = "on",\
    },{\
        .driver   = "virtio-pci",\
        .property = "disable-legacy",\
        .value    = "off",\
    },

#define HW_COMPAT_2_5 \
    {\
        .driver   = "isa-fdc",\
        .property = "fallback",\
        .value    = "144",\
    },{\
        .driver   = "pvscsi",\
        .property = "x-old-pci-configuration",\
        .value    = "on",\
    },{\
        .driver   = "pvscsi",\
        .property = "x-disable-pcie",\
        .value    = "on",\
    },\
    {\
        .driver   = "vmxnet3",\
        .property = "x-old-msi-offsets",\
        .value    = "on",\
    },{\
        .driver   = "vmxnet3",\
        .property = "x-disable-pcie",\
        .value    = "on",\
    },

#define HW_COMPAT_2_4 \
    {\
        .driver   = "virtio-blk-device",\
        .property = "scsi",\
        .value    = "true",\
    },{\
        .driver   = "e1000",\
        .property = "extra_mac_registers",\
        .value    = "off",\
    },{\
        .driver   = "virtio-pci",\
        .property = "x-disable-pcie",\
        .value    = "on",\
    },{\
        .driver   = "virtio-pci",\
        .property = "migrate-extra",\
        .value    = "off",\
    },{\
        .driver   = "fw_cfg_mem",\
        .property = "dma_enabled",\
        .value    = "off",\
    },{\
        .driver   = "fw_cfg_io",\
        .property = "dma_enabled",\
        .value    = "off",\
    },

#define HW_COMPAT_2_3 \
    {\
        .driver   = "virtio-blk-pci",\
        .property = "any_layout",\
        .value    = "off",\
    },{\
        .driver   = "virtio-balloon-pci",\
        .property = "any_layout",\
        .value    = "off",\
    },{\
        .driver   = "virtio-serial-pci",\
        .property = "any_layout",\
        .value    = "off",\
    },{\
        .driver   = "virtio-9p-pci",\
        .property = "any_layout",\
        .value    = "off",\
    },{\
        .driver   = "virtio-rng-pci",\
        .property = "any_layout",\
        .value    = "off",\
    },{\
        .driver   = TYPE_PCI_DEVICE,\
        .property = "x-pcie-lnksta-dllla",\
        .value    = "off",\
    },{\
        .driver   = "migration",\
        .property = "send-configuration",\
        .value    = "off",\
    },{\
        .driver   = "migration",\
        .property = "send-section-footer",\
        .value    = "off",\
    },{\
        .driver   = "migration",\
        .property = "store-global-state",\
        .value    = "off",\
    },

#define HW_COMPAT_2_2 \
    /* empty */

#define HW_COMPAT_2_1 \
    {\
        .driver   = "intel-hda",\
        .property = "old_msi_addr",\
        .value    = "on",\
    },{\
        .driver   = "VGA",\
        .property = "qemu-extended-regs",\
        .value    = "off",\
    },{\
        .driver   = "secondary-vga",\
        .property = "qemu-extended-regs",\
        .value    = "off",\
    },{\
        .driver   = "virtio-scsi-pci",\
        .property = "any_layout",\
        .value    = "off",\
    },{\
        .driver   = "usb-mouse",\
        .property = "usb_version",\
        .value    = stringify(1),\
    },{\
        .driver   = "usb-kbd",\
        .property = "usb_version",\
        .value    = stringify(1),\
    },{\
        .driver   = "virtio-pci",\
        .property = "virtio-pci-bus-master-bug-migration",\
        .value    = "on",\
    },

#endif /* HW_COMPAT_H */
