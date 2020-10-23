/*
 * Generic Virtual-Device Fuzzing Target Configs
 *
 * Copyright Red Hat Inc., 2020
 *
 * Authors:
 *  Alexander Bulekov   <alxndr@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef GENERIC_FUZZ_CONFIGS_H
#define GENERIC_FUZZ_CONFIGS_H

#include "qemu/osdep.h"

typedef struct generic_fuzz_config {
    const char *name, *args, *objects;
} generic_fuzz_config;

const generic_fuzz_config predefined_configs[] = {
    {
        .name = "virtio-net-pci-slirp",
        .args = "-M q35 -nodefaults "
        "-device virtio-net,netdev=net0 -netdev user,id=net0",
        .objects = "virtio*",
    },{
        .name = "virtio-blk",
        .args = "-machine q35 -device virtio-blk,drive=disk0 "
        "-drive file=null-co://,id=disk0,if=none,format=raw",
        .objects = "virtio*",
    },{
        .name = "virtio-scsi",
        .args = "-machine q35 -device virtio-scsi,num_queues=8 "
        "-device scsi-hd,drive=disk0 "
        "-drive file=null-co://,id=disk0,if=none,format=raw",
        .objects = "scsi* virtio*",
    },{
        .name = "virtio-gpu",
        .args = "-machine q35 -nodefaults -device virtio-gpu",
        .objects = "virtio*",
    },{
        .name = "virtio-vga",
        .args = "-machine q35 -nodefaults -device virtio-vga",
        .objects = "virtio*",
    },{
        .name = "virtio-rng",
        .args = "-machine q35 -nodefaults -device virtio-rng",
        .objects = "virtio*",
    },{
        .name = "virtio-balloon",
        .args = "-machine q35 -nodefaults -device virtio-balloon",
        .objects = "virtio*",
    },{
        .name = "virtio-serial",
        .args = "-machine q35 -nodefaults -device virtio-serial",
        .objects = "virtio*",
    },{
        .name = "virtio-mouse",
        .args = "-machine q35 -nodefaults -device virtio-mouse",
        .objects = "virtio*",
    },{
        .name = "e1000",
        .args = "-M q35 -nodefaults "
        "-device e1000,netdev=net0 -netdev user,id=net0",
        .objects = "e1000",
    },{
        .name = "e1000e",
        .args = "-M q35 -nodefaults "
        "-device e1000e,netdev=net0 -netdev user,id=net0",
        .objects = "e1000e",
    },{
        .name = "cirrus-vga",
        .args = "-machine q35 -nodefaults -device cirrus-vga",
        .objects = "cirrus*",
    },{
        .name = "bochs-display",
        .args = "-machine q35 -nodefaults -device bochs-display",
        .objects = "bochs*",
    },{
        .name = "intel-hda",
        .args = "-machine q35 -nodefaults -device intel-hda,id=hda0 "
        "-device hda-output,bus=hda0.0 -device hda-micro,bus=hda0.0 "
        "-device hda-duplex,bus=hda0.0",
        .objects = "intel-hda",
    },{
        .name = "ide-hd",
        .args = "-machine q35 -nodefaults "
        "-drive file=null-co://,if=none,format=raw,id=disk0 "
        "-device ide-hd,drive=disk0",
        .objects = "ahci*",
    },{
        .name = "floppy",
        .args = "-machine pc -nodefaults -device floppy,id=floppy0 "
        "-drive id=disk0,file=null-co://,file.read-zeroes=on,if=none "
        "-device floppy,drive=disk0,drive-type=288",
        .objects = "fd* floppy*",
    },{
        .name = "xhci",
        .args = "-machine q35 -nodefaults "
        "-drive file=null-co://,if=none,format=raw,id=disk0 "
        "-device qemu-xhci,id=xhci -device usb-tablet,bus=xhci.0 "
        "-device usb-bot -device usb-storage,drive=disk0 "
        "-chardev null,id=cd0 -chardev null,id=cd1 "
        "-device usb-braille,chardev=cd0 -device usb-ccid -device usb-ccid "
        "-device usb-kbd -device usb-mouse -device usb-serial,chardev=cd1 "
        "-device usb-tablet -device usb-wacom-tablet -device usb-audio",
        .objects = "*usb* *uhci* *xhci*",
    },{
        .name = "pc-i440fx",
        .args = "-machine pc",
        .objects = "*",
    },{
        .name = "pc-q35",
        .args = "-machine q35",
        .objects = "*",
    }
};

#endif
