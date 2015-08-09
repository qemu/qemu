#ifndef HW_COMPAT_H
#define HW_COMPAT_H

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
