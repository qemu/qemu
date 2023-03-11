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


typedef struct generic_fuzz_config {
    const char *name, *args, *objects;
    gchar* (*argfunc)(void); /* Result must be freeable by g_free() */
} generic_fuzz_config;

static inline gchar *generic_fuzzer_virtio_9p_args(void){
    g_autofree char *tmpdir = g_dir_make_tmp("qemu-fuzz.XXXXXX", NULL);
    g_assert_nonnull(tmpdir);

    return g_strdup_printf("-machine q35 -nodefaults "
    "-device virtio-9p,fsdev=hshare,mount_tag=hshare "
    "-fsdev local,id=hshare,path=%s,security_model=mapped-xattr,"
    "writeout=immediate,fmode=0600,dmode=0700", tmpdir);
}

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
        .name = "virtio-9p",
        .argfunc = generic_fuzzer_virtio_9p_args,
        .objects = "virtio*",
    },{
        .name = "virtio-9p-synth",
        .args = "-machine q35 -nodefaults "
        "-device virtio-9p,fsdev=hshare,mount_tag=hshare "
        "-fsdev synth,id=hshare",
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
        .name = "igb",
        .args = "-M q35 -nodefaults "
        "-device igb,netdev=net0 -netdev user,id=net0",
        .objects = "igb",
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
        .args = "-machine pc -nodefaults "
        "-drive file=null-co://,if=none,format=raw,id=disk0 "
        "-device ide-hd,drive=disk0",
        .objects = "*ide*",
    },{
        .name = "ide-atapi",
        .args = "-machine pc -nodefaults "
        "-drive file=null-co://,if=none,format=raw,id=disk0 "
        "-device ide-cd,drive=disk0",
        .objects = "*ide*",
    },{
        .name = "ahci-hd",
        .args = "-machine q35 -nodefaults "
        "-drive file=null-co://,if=none,format=raw,id=disk0 "
        "-device ide-hd,drive=disk0",
        .objects = "*ahci*",
    },{
        .name = "ahci-atapi",
        .args = "-machine q35 -nodefaults "
        "-drive file=null-co://,if=none,format=raw,id=disk0 "
        "-device ide-cd,drive=disk0",
        .objects = "*ahci*",
    },{
        .name = "floppy",
        .args = "-machine pc -nodefaults -device floppy,id=floppy0 "
        "-drive id=disk0,file=null-co://,file.read-zeroes=on,if=none,format=raw "
        "-device floppy,drive=disk0,drive-type=288",
        .objects = "fd* floppy* i8257",
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
    },{
        .name = "vmxnet3",
        .args = "-machine q35 -nodefaults "
        "-device vmxnet3,netdev=net0 -netdev user,id=net0",
        .objects = "vmxnet3"
    },{
        .name = "ne2k_pci",
        .args = "-machine q35 -nodefaults "
        "-device ne2k_pci,netdev=net0 -netdev user,id=net0",
        .objects = "ne2k*"
    },{
        .name = "pcnet",
        .args = "-machine q35 -nodefaults "
        "-device pcnet,netdev=net0 -netdev user,id=net0",
        .objects = "pcnet"
    },{
        .name = "rtl8139",
        .args = "-machine q35 -nodefaults "
        "-device rtl8139,netdev=net0 -netdev user,id=net0",
        .objects = "rtl8139"
    },{
        .name = "i82550",
        .args = "-machine q35 -nodefaults "
        "-device i82550,netdev=net0 -netdev user,id=net0",
        .objects = "i8255*"
    },{
        .name = "sdhci-v3",
        .args = "-nodefaults -device sdhci-pci,sd-spec-version=3 "
        "-device sd-card,drive=mydrive "
        "-drive if=none,index=0,file=null-co://,format=raw,id=mydrive -nographic",
        .objects = "sd*"
    },{
        .name = "ehci",
        .args = "-machine q35 -nodefaults "
        "-device ich9-usb-ehci1,bus=pcie.0,addr=1d.7,"
        "multifunction=on,id=ich9-ehci-1 "
        "-device ich9-usb-uhci1,bus=pcie.0,addr=1d.0,"
        "multifunction=on,masterbus=ich9-ehci-1.0,firstport=0 "
        "-device ich9-usb-uhci2,bus=pcie.0,addr=1d.1,"
        "multifunction=on,masterbus=ich9-ehci-1.0,firstport=2 "
        "-device ich9-usb-uhci3,bus=pcie.0,addr=1d.2,"
        "multifunction=on,masterbus=ich9-ehci-1.0,firstport=4 "
        "-drive if=none,id=usbcdrom,media=cdrom "
        "-device usb-tablet,bus=ich9-ehci-1.0,port=1,usb_version=1 "
        "-device usb-storage,bus=ich9-ehci-1.0,port=2,drive=usbcdrom",
        .objects = "*usb* *hci*",
    },{
        .name = "ohci",
        .args = "-machine q35 -nodefaults  -device pci-ohci -device usb-kbd",
        .objects = "*usb* *ohci*",
    },{
        .name = "megaraid",
        .args = "-machine q35 -nodefaults -device megasas -device scsi-cd,drive=null0 "
        "-blockdev driver=null-co,read-zeroes=on,node-name=null0",
        .objects = "megasas*",
    },{
        .name = "am53c974",
        .args = "-device am53c974,id=scsi -device scsi-hd,drive=disk0 "
                 "-drive id=disk0,if=none,file=null-co://,format=raw "
                 "-nodefaults",
        .objects = "*esp* *scsi* *am53c974*",
    },{
        .name = "ac97",
        .args = "-machine q35 -nodefaults "
        "-device ac97,audiodev=snd0 -audiodev none,id=snd0 -nodefaults",
        .objects = "ac97*",
    },{
        .name = "cs4231a",
        .args = "-machine q35 -nodefaults "
        "-device cs4231a,audiodev=snd0 -audiodev none,id=snd0 -nodefaults",
        .objects = "cs4231a* i8257*",
    },{
        .name = "es1370",
        .args = "-machine q35 -nodefaults "
        "-device es1370,audiodev=snd0 -audiodev none,id=snd0 -nodefaults",
        .objects = "es1370*",
    },{
        .name = "sb16",
        .args = "-machine q35 -nodefaults "
        "-device sb16,audiodev=snd0 -audiodev none,id=snd0 -nodefaults",
        .objects = "sb16* i8257*",
    },{
        .name = "parallel",
        .args = "-machine q35 -nodefaults "
        "-parallel file:/dev/null",
        .objects = "parallel*",
    }
};

#endif
