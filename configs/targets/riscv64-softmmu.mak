TARGET_ARCH=riscv64
TARGET_BASE_ARCH=riscv
TARGET_KVM_HAVE_GUEST_DEBUG=y
TARGET_XML_FILES= riscv-64bit-cpu.xml riscv-32bit-fpu.xml riscv-64bit-fpu.xml riscv-64bit-virtual.xml riscv-32bit-cpu.xml riscv-32bit-virtual.xml
# needed by boot.c
TARGET_NEED_FDT=y
TARGET_LONG_BITS=64
TARGET_NOT_USING_LEGACY_LDST_PHYS_API=y
