#ifndef HW_ACPI_GEN_UTILS_H
#define HW_ACPI_GEN_UTILS_H

#include <stdint.h>
#include <glib.h>
#include "qemu/compiler.h"

typedef enum {
    AML_NO_OPCODE = 0,/* has only data */
    AML_OPCODE,       /* has opcode optionally followed by data */
    AML_PACKAGE,      /* has opcode and uses PkgLength for its length */
    AML_EXT_PACKAGE,  /* ame as AML_PACKAGE but also has 'ExOpPrefix' */
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

/* Block AML object primitives */
Aml *aml_scope(const char *name_format, ...) GCC_FMT_ATTR(1, 2);
Aml *aml_device(const char *name_format, ...) GCC_FMT_ATTR(1, 2);
Aml *aml_method(const char *name, int arg_count);
Aml *aml_if(Aml *predicate);

/* other helpers */
GArray *build_alloc_array(void);
void build_free_array(GArray *array);
void build_prepend_byte(GArray *array, uint8_t val);
void build_append_byte(GArray *array, uint8_t val);
void build_append_array(GArray *array, GArray *val);

void GCC_FMT_ATTR(2, 3)
build_append_namestring(GArray *array, const char *format, ...);

void build_prepend_package_length(GArray *package);
void build_package(GArray *package, uint8_t op);
void build_append_int(GArray *table, uint64_t value);
void build_extop_package(GArray *package, uint8_t op);

#endif
