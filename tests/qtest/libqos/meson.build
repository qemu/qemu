libqos_srcs = files('../libqtest.c',
        'qgraph.c',
        'qos_external.c',
        'pci.c',
        'fw_cfg.c',
        'malloc.c',
        'libqos.c',
        'sdhci-cmd.c',

        # spapr
        'malloc-spapr.c',
        'libqos-spapr.c',
        'rtas.c',
        'pci-spapr.c',

        # pc
        'pci-pc.c',
        'malloc-pc.c',
        'libqos-pc.c',
        'ahci.c',

        # usb
        'usb.c',

        # qgraph devices:
        'e1000e.c',
        'i2c.c',
        'i2c-imx.c',
        'i2c-omap.c',
        'sdhci.c',
        'tpci200.c',
        'virtio.c',
        'virtio-9p.c',
        'virtio-balloon.c',
        'virtio-blk.c',
        'vhost-user-blk.c',
        'virtio-mmio.c',
        'virtio-net.c',
        'virtio-pci.c',
        'virtio-pci-modern.c',
        'virtio-rng.c',
        'virtio-scsi.c',
        'virtio-serial.c',
        'virtio-iommu.c',

        # qgraph machines:
        'aarch64-xlnx-zcu102-machine.c',
        'arm-imx25-pdk-machine.c',
        'arm-n800-machine.c',
        'arm-raspi2-machine.c',
        'arm-sabrelite-machine.c',
        'arm-smdkc210-machine.c',
        'arm-virt-machine.c',
        'arm-xilinx-zynq-a9-machine.c',
        'ppc64_pseries-machine.c',
        'x86_64_pc-machine.c',
)

libqos = static_library('qos', libqos_srcs + genh,
                        name_suffix: 'fa',
                        build_by_default: false)

qos = declare_dependency(link_whole: libqos)
