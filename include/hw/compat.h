#ifndef HW_COMPAT_H
#define HW_COMPAT_H

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
