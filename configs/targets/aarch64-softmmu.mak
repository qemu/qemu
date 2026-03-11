TARGET_ARCH=aarch64
TARGET_BASE_ARCH=arm
TARGET_KVM_HAVE_GUEST_DEBUG=y
TARGET_XML_FILES= aarch64-core.xml aarch64-fpu.xml arm-core.xml arm-vfp.xml arm-vfp3.xml arm-vfp-sysregs.xml arm-neon.xml arm-m-profile.xml arm-m-profile-mve.xml aarch64-pauth.xml aarch64-sme2.xml
# needed by boot.c
TARGET_NEED_FDT=y
TARGET_LONG_BITS=64
