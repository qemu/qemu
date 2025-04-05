TARGET_ARCH=loongarch64
TARGET_BASE_ARCH=loongarch
TARGET_KVM_HAVE_GUEST_DEBUG=y
TARGET_XML_FILES= gdb-xml/loongarch-base32.xml gdb-xml/loongarch-base64.xml gdb-xml/loongarch-fpu.xml gdb-xml/loongarch-lsx.xml gdb-xml/loongarch-lasx.xml
# all boards require libfdt
TARGET_NEED_FDT=y
TARGET_LONG_BITS=64
