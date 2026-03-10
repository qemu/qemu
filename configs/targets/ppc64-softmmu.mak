TARGET_ARCH=ppc64
TARGET_BASE_ARCH=ppc
TARGET_BIG_ENDIAN=y
TARGET_KVM_HAVE_GUEST_DEBUG=y
TARGET_XML_FILES= power64-core.xml power-fpu.xml power-altivec.xml power-spe.xml power-vsx.xml
# all boards require libfdt
TARGET_NEED_FDT=y
TARGET_LONG_BITS=64
