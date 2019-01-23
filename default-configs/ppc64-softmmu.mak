# Default configuration for ppc64-softmmu

# Include all 32-bit boards
include ppc-softmmu.mak

# For PowerNV
CONFIG_POWERNV=y
CONFIG_IPMI=y
CONFIG_IPMI_LOCAL=y
CONFIG_IPMI_EXTERN=y
CONFIG_ISA_IPMI_BT=y

# For pSeries
CONFIG_PSERIES=y
CONFIG_VIRTIO_VGA=y
CONFIG_MEM_DEVICE=y
CONFIG_DIMM=y
CONFIG_SPAPR_RNG=y
