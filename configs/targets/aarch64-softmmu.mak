TARGET_ARCH=aarch64
TARGET_BASE_ARCH=arm
TARGET_KVM_HAVE_GUEST_DEBUG=y
TARGET_XML_FILES= gdb-xml/aarch64-core.xml gdb-xml/aarch64-fpu.xml gdb-xml/arm-core.xml gdb-xml/arm-vfp.xml gdb-xml/arm-vfp3.xml gdb-xml/arm-vfp-sysregs.xml gdb-xml/arm-neon.xml gdb-xml/arm-m-profile.xml gdb-xml/arm-m-profile-mve.xml gdb-xml/aarch64-pauth.xml gdb-xml/aarch64-sme2.xml
# needed by boot.c
TARGET_NEED_FDT=y
TARGET_LONG_BITS=64
