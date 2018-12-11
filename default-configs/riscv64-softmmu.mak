# Default configuration for riscv-softmmu

include pci.mak

CONFIG_SERIAL=y
CONFIG_VIRTIO_MMIO=y

CONFIG_CADENCE=y

CONFIG_PCI_GENERIC=y

CONFIG_VGA=y
CONFIG_VGA_PCI=y
