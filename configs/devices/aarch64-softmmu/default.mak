# Default configuration for aarch64-softmmu

# We support all the 32 bit boards so need all their config
include ../arm-softmmu/default.mak

# These are selected by default when TCG is enabled, uncomment them to
# keep out of the build.
# CONFIG_XLNX_ZYNQMP_ARM=n
# CONFIG_XLNX_VERSAL=n
# CONFIG_SBSA_REF=n
# CONFIG_NPCM8XX=n
CONFIG_VMAPPLE=n
