# Default configuration for loongarch64-linux-user
TARGET_ARCH=loongarch64
TARGET_BASE_ARCH=loongarch
TARGET_XML_FILES=loongarch-base64.xml loongarch-fpu.xml loongarch-lsx.xml loongarch-lasx.xml
TARGET_SYSTBL=syscall.tbl
TARGET_SYSTBL_ABI=common,64
TARGET_LONG_BITS=64
TARGET_NOT_USING_LEGACY_NATIVE_ENDIAN_API=y
