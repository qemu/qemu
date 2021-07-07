#
# A minimal version of the config that only supports only a few
# virtual machines. This avoids bringing in any of numerous legacy
# features from the 32bit platform (although virt still supports 32bit
# itself)
#

CONFIG_ARM_VIRT=y
CONFIG_SBSA_REF=y
