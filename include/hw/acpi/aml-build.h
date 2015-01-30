#ifndef HW_ACPI_GEN_UTILS_H
#define HW_ACPI_GEN_UTILS_H

#include <stdint.h>
#include <glib.h>
#include "qemu/compiler.h"

GArray *build_alloc_array(void);
void build_free_array(GArray *array);
void build_prepend_byte(GArray *array, uint8_t val);
void build_append_byte(GArray *array, uint8_t val);
void build_append_array(GArray *array, GArray *val);

void GCC_FMT_ATTR(2, 3)
build_append_namestring(GArray *array, const char *format, ...);

void build_prepend_package_length(GArray *package);
void build_package(GArray *package, uint8_t op);
void build_append_value(GArray *table, uint32_t value, int size);
void build_append_int(GArray *table, uint32_t value);
void build_extop_package(GArray *package, uint8_t op);

#endif
