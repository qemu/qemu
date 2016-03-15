/* Declarations for use for CPU state serialization.  */
#ifndef MIGRATION_CPU_H
#define MIGRATION_CPU_H

#if TARGET_LONG_BITS == 64
#define qemu_put_betl qemu_put_be64
#define qemu_get_betl qemu_get_be64
#define qemu_put_betls qemu_put_be64s
#define qemu_get_betls qemu_get_be64s
#define qemu_put_sbetl qemu_put_sbe64
#define qemu_get_sbetl qemu_get_sbe64
#define qemu_put_sbetls qemu_put_sbe64s
#define qemu_get_sbetls qemu_get_sbe64s

#define VMSTATE_UINTTL_V(_f, _s, _v)                                  \
    VMSTATE_UINT64_V(_f, _s, _v)
#define VMSTATE_UINTTL_EQUAL_V(_f, _s, _v)                            \
    VMSTATE_UINT64_EQUAL_V(_f, _s, _v)
#define VMSTATE_UINTTL_ARRAY_V(_f, _s, _n, _v)                        \
    VMSTATE_UINT64_ARRAY_V(_f, _s, _n, _v)
#define vmstate_info_uinttl vmstate_info_uint64
#else
#define qemu_put_betl qemu_put_be32
#define qemu_get_betl qemu_get_be32
#define qemu_put_betls qemu_put_be32s
#define qemu_get_betls qemu_get_be32s
#define qemu_put_sbetl qemu_put_sbe32
#define qemu_get_sbetl qemu_get_sbe32
#define qemu_put_sbetls qemu_put_sbe32s
#define qemu_get_sbetls qemu_get_sbe32s

#define VMSTATE_UINTTL_V(_f, _s, _v)                                  \
    VMSTATE_UINT32_V(_f, _s, _v)
#define VMSTATE_UINTTL_EQUAL_V(_f, _s, _v)                            \
    VMSTATE_UINT32_EQUAL_V(_f, _s, _v)
#define VMSTATE_UINTTL_ARRAY_V(_f, _s, _n, _v)                        \
    VMSTATE_UINT32_ARRAY_V(_f, _s, _n, _v)
#define vmstate_info_uinttl vmstate_info_uint32
#endif

#define VMSTATE_UINTTL(_f, _s)                                        \
    VMSTATE_UINTTL_V(_f, _s, 0)
#define VMSTATE_UINTTL_EQUAL(_f, _s)                                  \
    VMSTATE_UINTTL_EQUAL_V(_f, _s, 0)
#define VMSTATE_UINTTL_ARRAY(_f, _s, _n)                              \
    VMSTATE_UINTTL_ARRAY_V(_f, _s, _n, 0)

#endif
