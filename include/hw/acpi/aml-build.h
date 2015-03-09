#ifndef HW_ACPI_GEN_UTILS_H
#define HW_ACPI_GEN_UTILS_H

#include <stdint.h>
#include <glib.h>
#include "qemu/compiler.h"

typedef enum {
    AML_NO_OPCODE = 0,/* has only data */
    AML_OPCODE,       /* has opcode optionally followed by data */
    AML_PACKAGE,      /* has opcode and uses PkgLength for its length */
    AML_EXT_PACKAGE,  /* Same as AML_PACKAGE but also has 'ExOpPrefix' */
    AML_BUFFER,       /* data encoded as 'DefBuffer' */
    AML_RES_TEMPLATE, /* encoded as ResourceTemplate macro */
} AmlBlockFlags;

struct Aml {
    GArray *buf;

    /*< private >*/
    uint8_t op;
    AmlBlockFlags block_flags;
};
typedef struct Aml Aml;

typedef enum {
    aml_decode10 = 0,
    aml_decode16 = 1,
} AmlIODecode;

typedef enum {
    aml_any_acc = 0,
    aml_byte_acc = 1,
    aml_word_acc = 2,
    aml_dword_acc = 3,
    aml_qword_acc = 4,
    aml_buffer_acc = 5,
} AmlFieldFlags;

typedef enum {
    aml_system_memory = 0x00,
    aml_system_io = 0x01,
} AmlRegionSpace;

typedef enum {
    aml_memory_range = 0,
    aml_io_range = 1,
    aml_bus_number_range = 2,
} AmlResourceType;

typedef enum {
    aml_sub_decode = 1 << 1,
    aml_pos_decode = 0
} AmlDecode;

typedef enum {
    aml_max_fixed = 1 << 3,
    aml_max_not_fixed = 0,
} AmlMaxFixed;

typedef enum {
    aml_min_fixed = 1 << 2,
    aml_min_not_fixed = 0
} AmlMinFixed;

/*
 * ACPI 1.0b: Table 6-26 I/O Resource Flag (Resource Type = 1) Definitions
 * _RNG field definition
 */
typedef enum {
    aml_isa_only = 1,
    aml_non_isa_only = 2,
    aml_entire_range = 3,
} AmlISARanges;

/*
 * ACPI 1.0b: Table 6-25 Memory Resource Flag (Resource Type = 0) Definitions
 * _MEM field definition
 */
typedef enum {
    aml_non_cacheable = 0,
    aml_cacheable = 1,
    aml_write_combining = 2,
    aml_prefetchable = 3,
} AmlCacheble;

/*
 * ACPI 1.0b: Table 6-25 Memory Resource Flag (Resource Type = 0) Definitions
 * _RW field definition
 */
typedef enum {
    aml_ReadOnly = 0,
    aml_ReadWrite = 1,
} AmlReadAndWrite;

/**
 * init_aml_allocator:
 *
 * Called for initializing API allocator which allow to use
 * AML API.
 * Returns: toplevel container which accumulates all other
 * AML elements for a table.
 */
Aml *init_aml_allocator(void);

/**
 * free_aml_allocator:
 *
 * Releases all elements used by AML API, frees associated memory
 * and invalidates AML allocator. After this call @init_aml_allocator
 * should be called again if AML API is to be used again.
 */
void free_aml_allocator(void);

/**
 * aml_append:
 * @parent_ctx: context to which @child element is added
 * @child: element that is copied into @parent_ctx context
 *
 * Joins Aml elements together and helps to construct AML tables
 * Examle of usage:
 *   Aml *table = aml_def_block("SSDT", ...);
 *   Aml *sb = aml_scope("\_SB");
 *   Aml *dev = aml_device("PCI0");
 *
 *   aml_append(dev, aml_name_decl("HID", aml_eisaid("PNP0A03")));
 *   aml_append(sb, dev);
 *   aml_append(table, sb);
 */
void aml_append(Aml *parent_ctx, Aml *child);

/* non block AML object primitives */
Aml *aml_name(const char *name_format, ...) GCC_FMT_ATTR(1, 2);
Aml *aml_name_decl(const char *name, Aml *val);
Aml *aml_return(Aml *val);
Aml *aml_int(const uint64_t val);
Aml *aml_arg(int pos);
Aml *aml_store(Aml *val, Aml *target);
Aml *aml_and(Aml *arg1, Aml *arg2);
Aml *aml_notify(Aml *arg1, Aml *arg2);
Aml *aml_call1(const char *method, Aml *arg1);
Aml *aml_call2(const char *method, Aml *arg1, Aml *arg2);
Aml *aml_call3(const char *method, Aml *arg1, Aml *arg2, Aml *arg3);
Aml *aml_call4(const char *method, Aml *arg1, Aml *arg2, Aml *arg3, Aml *arg4);
Aml *aml_io(AmlIODecode dec, uint16_t min_base, uint16_t max_base,
            uint8_t aln, uint8_t len);
Aml *aml_operation_region(const char *name, AmlRegionSpace rs,
                          uint32_t offset, uint32_t len);
Aml *aml_irq_no_flags(uint8_t irq);
Aml *aml_named_field(const char *name, unsigned length);
Aml *aml_reserved_field(unsigned length);
Aml *aml_local(int num);
Aml *aml_string(const char *name_format, ...) GCC_FMT_ATTR(1, 2);
Aml *aml_equal(Aml *arg1, Aml *arg2);
Aml *aml_processor(uint8_t proc_id, uint32_t pblk_addr, uint8_t pblk_len,
                   const char *name_format, ...) GCC_FMT_ATTR(4, 5);
Aml *aml_eisaid(const char *str);
Aml *aml_word_bus_number(AmlMinFixed min_fixed, AmlMaxFixed max_fixed,
                         AmlDecode dec, uint16_t addr_gran,
                         uint16_t addr_min, uint16_t addr_max,
                         uint16_t addr_trans, uint16_t len);
Aml *aml_word_io(AmlMinFixed min_fixed, AmlMaxFixed max_fixed,
                 AmlDecode dec, AmlISARanges isa_ranges,
                 uint16_t addr_gran, uint16_t addr_min,
                 uint16_t addr_max, uint16_t addr_trans,
                 uint16_t len);
Aml *aml_dword_memory(AmlDecode dec, AmlMinFixed min_fixed,
                      AmlMaxFixed max_fixed, AmlCacheble cacheable,
                      AmlReadAndWrite read_and_write,
                      uint32_t addr_gran, uint32_t addr_min,
                      uint32_t addr_max, uint32_t addr_trans,
                      uint32_t len);
Aml *aml_qword_memory(AmlDecode dec, AmlMinFixed min_fixed,
                      AmlMaxFixed max_fixed, AmlCacheble cacheable,
                      AmlReadAndWrite read_and_write,
                      uint64_t addr_gran, uint64_t addr_min,
                      uint64_t addr_max, uint64_t addr_trans,
                      uint64_t len);

/* Block AML object primitives */
Aml *aml_scope(const char *name_format, ...) GCC_FMT_ATTR(1, 2);
Aml *aml_device(const char *name_format, ...) GCC_FMT_ATTR(1, 2);
Aml *aml_method(const char *name, int arg_count);
Aml *aml_if(Aml *predicate);
Aml *aml_package(uint8_t num_elements);
Aml *aml_buffer(void);
Aml *aml_resource_template(void);
Aml *aml_field(const char *name, AmlFieldFlags flags);
Aml *aml_varpackage(uint32_t num_elements);

#endif
