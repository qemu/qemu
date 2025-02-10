#ifndef HW_ACPI_AML_BUILD_H
#define HW_ACPI_AML_BUILD_H

#include "hw/acpi/acpi-defs.h"
#include "hw/acpi/bios-linker-loader.h"

#define ACPI_BUILD_APPNAME6 "BOCHS "
#define ACPI_BUILD_APPNAME8 "BXPC    "

#define ACPI_BUILD_TABLE_FILE "etc/acpi/tables"
#define ACPI_BUILD_RSDP_FILE "etc/acpi/rsdp"
#define ACPI_BUILD_TPMLOG_FILE "etc/tpm/log"
#define ACPI_BUILD_LOADER_FILE "etc/table-loader"

#define AML_NOTIFY_METHOD "NTFY"

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

typedef enum {
    AML_COMPATIBILITY = 0,
    AML_TYPEA = 1,
    AML_TYPEB = 2,
    AML_TYPEF = 3,
} AmlDmaType;

typedef enum {
    AML_NOTBUSMASTER = 0,
    AML_BUSMASTER = 1,
} AmlDmaBusMaster;

typedef enum {
    AML_TRANSFER8 = 0,
    AML_TRANSFER8_16 = 1,
    AML_TRANSFER16 = 2,
} AmlTransferSize;

typedef enum {
    AML_DECODE10 = 0,
    AML_DECODE16 = 1,
} AmlIODecode;

typedef enum {
    AML_ANY_ACC = 0,
    AML_BYTE_ACC = 1,
    AML_WORD_ACC = 2,
    AML_DWORD_ACC = 3,
    AML_QWORD_ACC = 4,
    AML_BUFFER_ACC = 5,
} AmlAccessType;

typedef enum {
    AML_NOLOCK = 0,
    AML_LOCK = 1,
} AmlLockRule;

typedef enum {
    AML_PRESERVE = 0,
    AML_WRITE_AS_ONES = 1,
    AML_WRITE_AS_ZEROS = 2,
} AmlUpdateRule;

typedef enum {
    AML_AS_SYSTEM_MEMORY = 0X00,
    AML_AS_SYSTEM_IO = 0X01,
    AML_AS_PCI_CONFIG = 0X02,
    AML_AS_EMBEDDED_CTRL = 0X03,
    AML_AS_SMBUS = 0X04,
    AML_AS_FFH = 0X7F,
} AmlAddressSpace;

typedef enum {
    AML_SYSTEM_MEMORY = 0X00,
    AML_SYSTEM_IO = 0X01,
    AML_PCI_CONFIG = 0X02,
} AmlRegionSpace;

typedef enum {
    AML_MEMORY_RANGE = 0,
    AML_IO_RANGE = 1,
    AML_BUS_NUMBER_RANGE = 2,
} AmlResourceType;

typedef enum {
    AML_SUB_DECODE = 1 << 1,
    AML_POS_DECODE = 0
} AmlDecode;

typedef enum {
    AML_MAX_FIXED = 1 << 3,
    AML_MAX_NOT_FIXED = 0,
} AmlMaxFixed;

typedef enum {
    AML_MIN_FIXED = 1 << 2,
    AML_MIN_NOT_FIXED = 0
} AmlMinFixed;

/*
 * ACPI 1.0b: Table 6-26 I/O Resource Flag (Resource Type = 1) Definitions
 * _RNG field definition
 */
typedef enum {
    AML_ISA_ONLY = 1,
    AML_NON_ISA_ONLY = 2,
    AML_ENTIRE_RANGE = 3,
} AmlISARanges;

/*
 * ACPI 1.0b: Table 6-25 Memory Resource Flag (Resource Type = 0) Definitions
 * _MEM field definition
 */
typedef enum {
    AML_NON_CACHEABLE = 0,
    AML_CACHEABLE = 1,
    AML_WRITE_COMBINING = 2,
    AML_PREFETCHABLE = 3,
} AmlCacheable;

/*
 * ACPI 1.0b: Table 6-25 Memory Resource Flag (Resource Type = 0) Definitions
 * _RW field definition
 */
typedef enum {
    AML_READ_ONLY = 0,
    AML_READ_WRITE = 1,
} AmlReadAndWrite;

/*
 * ACPI 5.0: Table 6-187 Extended Interrupt Descriptor Definition
 * Interrupt Vector Flags Bits[0] Consumer/Producer
 */
typedef enum {
    AML_CONSUMER_PRODUCER = 0,
    AML_CONSUMER = 1,
} AmlConsumerAndProducer;

/*
 * ACPI 5.0: Table 6-187 Extended Interrupt Descriptor Definition
 * _HE field definition
 */
typedef enum {
    AML_LEVEL = 0,
    AML_EDGE = 1,
} AmlLevelAndEdge;

/*
 * ACPI 5.0: Table 6-187 Extended Interrupt Descriptor Definition
 * _LL field definition
 */
typedef enum {
    AML_ACTIVE_HIGH = 0,
    AML_ACTIVE_LOW = 1,
} AmlActiveHighAndLow;

/*
 * ACPI 5.0: Table 6-187 Extended Interrupt Descriptor Definition
 * _SHR field definition
 */
typedef enum {
    AML_EXCLUSIVE = 0,
    AML_SHARED = 1,
    AML_EXCLUSIVE_AND_WAKE = 2,
    AML_SHARED_AND_WAKE = 3,
} AmlShared;

/* ACPI 1.0b: 16.2.5.2 Named Objects Encoding: MethodFlags */
typedef enum {
    AML_NOTSERIALIZED = 0,
    AML_SERIALIZED = 1,
} AmlSerializeFlag;

/*
 * ACPI 5.0: Table 6-189 GPIO Connection Descriptor Definition
 * GPIO Connection Type
 */
typedef enum {
    AML_INTERRUPT_CONNECTION = 0,
    AML_IO_CONNECTION = 1,
} AmlGpioConnectionType;

/*
 * ACPI 5.0: Table 6-189 GPIO Connection Descriptor Definition
 * _PPI field definition
 */
typedef enum {
    AML_PULL_DEFAULT = 0,
    AML_PULL_UP = 1,
    AML_PULL_DOWN = 2,
    AML_PULL_NONE = 3,
} AmlPinConfig;

typedef enum {
    MEM_AFFINITY_NOFLAGS      = 0,
    MEM_AFFINITY_ENABLED      = (1 << 0),
    MEM_AFFINITY_HOTPLUGGABLE = (1 << 1),
    MEM_AFFINITY_NON_VOLATILE = (1 << 2),
} MemoryAffinityFlags;

typedef
struct AcpiBuildTables {
    GArray *table_data;
    GArray *rsdp;
    GArray *tcpalog;
    GArray *vmgenid;
    GArray *hardware_errors;
    BIOSLinker *linker;
} AcpiBuildTables;

typedef
struct CrsRangeEntry {
    uint64_t base;
    uint64_t limit;
} CrsRangeEntry;

typedef
struct CrsRangeSet {
    GPtrArray *io_ranges;
    GPtrArray *mem_ranges;
    GPtrArray *mem_64bit_ranges;
} CrsRangeSet;


/*
 * ACPI 5.0: 6.4.3.8.2 Serial Bus Connection Descriptors
 * Serial Bus Type
 */
#define AML_SERIAL_BUS_TYPE_I2C  1
#define AML_SERIAL_BUS_TYPE_SPI  2
#define AML_SERIAL_BUS_TYPE_UART 3

/*
 * ACPI 5.0: 6.4.3.8.2 Serial Bus Connection Descriptors
 * General Flags
 */
/* Slave Mode */
#define AML_SERIAL_BUS_FLAG_MASTER_DEVICE       (1 << 0)
/* Consumer/Producer */
#define AML_SERIAL_BUS_FLAG_CONSUME_ONLY        (1 << 1)

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
 * Example of usage:
 *   Aml *table = aml_def_block("SSDT", ...);
 *   Aml *sb = aml_scope("\\_SB");
 *   Aml *dev = aml_device("PCI0");
 *
 *   aml_append(dev, aml_name_decl("HID", aml_eisaid("PNP0A03")));
 *   aml_append(sb, dev);
 *   aml_append(table, sb);
 */
void aml_append(Aml *parent_ctx, Aml *child);

/* non block AML object primitives */
Aml *aml_name(const char *name_format, ...) G_GNUC_PRINTF(1, 2);
Aml *aml_name_decl(const char *name, Aml *val);
Aml *aml_debug(void);
Aml *aml_return(Aml *val);
Aml *aml_int(const uint64_t val);
Aml *aml_arg(int pos);
Aml *aml_to_integer(Aml *arg);
Aml *aml_to_hexstring(Aml *src, Aml *dst);
Aml *aml_to_buffer(Aml *src, Aml *dst);
Aml *aml_to_decimalstring(Aml *src, Aml *dst);
Aml *aml_store(Aml *val, Aml *target);
Aml *aml_and(Aml *arg1, Aml *arg2, Aml *dst);
Aml *aml_or(Aml *arg1, Aml *arg2, Aml *dst);
Aml *aml_land(Aml *arg1, Aml *arg2);
Aml *aml_lor(Aml *arg1, Aml *arg2);
Aml *aml_shiftleft(Aml *arg1, Aml *count);
Aml *aml_shiftright(Aml *arg1, Aml *count, Aml *dst);
Aml *aml_lless(Aml *arg1, Aml *arg2);
Aml *aml_add(Aml *arg1, Aml *arg2, Aml *dst);
Aml *aml_subtract(Aml *arg1, Aml *arg2, Aml *dst);
Aml *aml_increment(Aml *arg);
Aml *aml_decrement(Aml *arg);
Aml *aml_index(Aml *arg1, Aml *idx);
Aml *aml_notify(Aml *arg1, Aml *arg2);
Aml *aml_break(void);
Aml *aml_call0(const char *method);
Aml *aml_call1(const char *method, Aml *arg1);
Aml *aml_call2(const char *method, Aml *arg1, Aml *arg2);
Aml *aml_call3(const char *method, Aml *arg1, Aml *arg2, Aml *arg3);
Aml *aml_call4(const char *method, Aml *arg1, Aml *arg2, Aml *arg3, Aml *arg4);
Aml *aml_call5(const char *method, Aml *arg1, Aml *arg2, Aml *arg3, Aml *arg4,
               Aml *arg5);
Aml *aml_call6(const char *method, Aml *arg1, Aml *arg2, Aml *arg3, Aml *arg4,
               Aml *arg5, Aml *arg6);
Aml *aml_gpio_int(AmlConsumerAndProducer con_and_pro,
                  AmlLevelAndEdge edge_level,
                  AmlActiveHighAndLow active_level, AmlShared shared,
                  AmlPinConfig pin_config, uint16_t debounce_timeout,
                  const uint32_t pin_list[], uint32_t pin_count,
                  const char *resource_source_name,
                  const uint8_t *vendor_data, uint16_t vendor_data_len);
Aml *aml_memory32_fixed(uint32_t addr, uint32_t size,
                        AmlReadAndWrite read_and_write);
Aml *aml_interrupt(AmlConsumerAndProducer con_and_pro,
                   AmlLevelAndEdge level_and_edge,
                   AmlActiveHighAndLow high_and_low, AmlShared shared,
                   uint32_t *irq_list, uint8_t irq_count);
Aml *aml_io(AmlIODecode dec, uint16_t min_base, uint16_t max_base,
            uint8_t aln, uint8_t len);
Aml *aml_operation_region(const char *name, AmlRegionSpace rs,
                          Aml *offset, uint32_t len);
Aml *aml_irq_no_flags(uint8_t irq);
Aml *aml_named_field(const char *name, unsigned length);
Aml *aml_reserved_field(unsigned length);
Aml *aml_local(int num);
Aml *aml_string(const char *name_format, ...) G_GNUC_PRINTF(1, 2);
Aml *aml_lnot(Aml *arg);
Aml *aml_equal(Aml *arg1, Aml *arg2);
Aml *aml_lgreater(Aml *arg1, Aml *arg2);
Aml *aml_lgreater_equal(Aml *arg1, Aml *arg2);
Aml *aml_processor(uint8_t proc_id, uint32_t pblk_addr, uint8_t pblk_len,
                   const char *name_format, ...) G_GNUC_PRINTF(4, 5);
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
Aml *aml_dword_io(AmlMinFixed min_fixed, AmlMaxFixed max_fixed,
                 AmlDecode dec, AmlISARanges isa_ranges,
                 uint32_t addr_gran, uint32_t addr_min,
                 uint32_t addr_max, uint32_t addr_trans,
                 uint32_t len);
Aml *aml_dword_memory(AmlDecode dec, AmlMinFixed min_fixed,
                      AmlMaxFixed max_fixed, AmlCacheable cacheable,
                      AmlReadAndWrite read_and_write,
                      uint32_t addr_gran, uint32_t addr_min,
                      uint32_t addr_max, uint32_t addr_trans,
                      uint32_t len);
Aml *aml_qword_memory(AmlDecode dec, AmlMinFixed min_fixed,
                      AmlMaxFixed max_fixed, AmlCacheable cacheable,
                      AmlReadAndWrite read_and_write,
                      uint64_t addr_gran, uint64_t addr_min,
                      uint64_t addr_max, uint64_t addr_trans,
                      uint64_t len);
Aml *aml_dma(AmlDmaType typ, AmlDmaBusMaster bm, AmlTransferSize sz,
             uint8_t channel);
Aml *aml_sleep(uint64_t msec);
Aml *aml_i2c_serial_bus_device(uint16_t address, const char *resource_source);

/* Block AML object primitives */
Aml *aml_scope(const char *name_format, ...) G_GNUC_PRINTF(1, 2);
Aml *aml_device(const char *name_format, ...) G_GNUC_PRINTF(1, 2);
Aml *aml_method(const char *name, int arg_count, AmlSerializeFlag sflag);
Aml *aml_if(Aml *predicate);
Aml *aml_else(void);
Aml *aml_while(Aml *predicate);
Aml *aml_package(uint8_t num_elements);
Aml *aml_buffer(int buffer_size, uint8_t *byte_list);
Aml *aml_resource_template(void);
Aml *aml_field(const char *name, AmlAccessType type, AmlLockRule lock,
               AmlUpdateRule rule);
Aml *aml_mutex(const char *name, uint8_t sync_level);
Aml *aml_acquire(Aml *mutex, uint16_t timeout);
Aml *aml_release(Aml *mutex);
Aml *aml_alias(const char *source_object, const char *alias_object);
Aml *aml_create_field(Aml *srcbuf, Aml *bit_index, Aml *num_bits,
                      const char *name);
Aml *aml_create_dword_field(Aml *srcbuf, Aml *index, const char *name);
Aml *aml_create_qword_field(Aml *srcbuf, Aml *index, const char *name);
Aml *aml_varpackage(uint32_t num_elements);
Aml *aml_touuid(const char *uuid);
Aml *aml_unicode(const char *str);
Aml *aml_refof(Aml *arg);
Aml *aml_derefof(Aml *arg);
Aml *aml_sizeof(Aml *arg);
Aml *aml_concatenate(Aml *source1, Aml *source2, Aml *target);
Aml *aml_object_type(Aml *object);

void build_append_int_noprefix(GArray *table, uint64_t value, int size);

typedef struct AcpiTable {
    const char *sig;
    const uint8_t rev;
    const char *oem_id;
    const char *oem_table_id;
    /* private vars tracking table state */
    GArray *array;
    unsigned table_offset;
} AcpiTable;

/**
 * acpi_table_begin:
 * initializes table header and keeps track of
 * table data/offsets
 * @desc: ACPI table descriptor
 * @array: blob where the ACPI table will be composed/stored.
 */
void acpi_table_begin(AcpiTable *desc, GArray *array);

/**
 * acpi_table_end:
 * sets actual table length and tells bios loader
 * where table is for the later initialization on
 * guest side.
 * @linker: reference to BIOSLinker object to use for the table
 * @table: ACPI table descriptor that was used with @acpi_table_begin
 * counterpart
 */
void acpi_table_end(BIOSLinker *linker, AcpiTable *table);

void *acpi_data_push(GArray *table_data, unsigned size);
unsigned acpi_data_len(GArray *table);
void acpi_add_table(GArray *table_offsets, GArray *table_data);
void acpi_build_tables_init(AcpiBuildTables *tables);
void acpi_build_tables_cleanup(AcpiBuildTables *tables, bool mfre);
void
build_rsdp(GArray *tbl, BIOSLinker *linker, AcpiRsdpData *rsdp_data);
void
build_rsdt(GArray *table_data, BIOSLinker *linker, GArray *table_offsets,
           const char *oem_id, const char *oem_table_id);
void
build_xsdt(GArray *table_data, BIOSLinker *linker, GArray *table_offsets,
           const char *oem_id, const char *oem_table_id);

int
build_append_named_dword(GArray *array, const char *name_format, ...)
G_GNUC_PRINTF(2, 3);

void build_append_gas(GArray *table, AmlAddressSpace as,
                      uint8_t bit_width, uint8_t bit_offset,
                      uint8_t access_width, uint64_t address);

static inline void
build_append_gas_from_struct(GArray *table, const struct AcpiGenericAddress *s)
{
    build_append_gas(table, s->space_id, s->bit_width, s->bit_offset,
                     s->access_width, s->address);
}

void crs_range_insert(GPtrArray *ranges, uint64_t base, uint64_t limit);
void crs_replace_with_free_ranges(GPtrArray *ranges,
                                         uint64_t start, uint64_t end);
void crs_range_set_init(CrsRangeSet *range_set);
void crs_range_set_free(CrsRangeSet *range_set);

Aml *build_crs(PCIHostState *host, CrsRangeSet *range_set, uint32_t io_offset,
               uint32_t mmio32_offset, uint64_t mmio64_offset,
               uint16_t bus_nr_offset);

void build_srat_memory(GArray *table_data, uint64_t base,
                       uint64_t len, int node, MemoryAffinityFlags flags);

void build_srat_pci_generic_initiator(GArray *table_data, uint32_t node,
                                      uint16_t segment, uint8_t bus,
                                      uint8_t devfn);

void build_srat_acpi_generic_port(GArray *table_data, uint32_t node,
                                  const char *hid, uint32_t uid);

void build_slit(GArray *table_data, BIOSLinker *linker, MachineState *ms,
                const char *oem_id, const char *oem_table_id);

void build_pptt(GArray *table_data, BIOSLinker *linker, MachineState *ms,
                const char *oem_id, const char *oem_table_id);

void build_fadt(GArray *tbl, BIOSLinker *linker, const AcpiFadtData *f,
                const char *oem_id, const char *oem_table_id);

void build_tpm2(GArray *table_data, BIOSLinker *linker, GArray *tcpalog,
                const char *oem_id, const char *oem_table_id);

void build_spcr(GArray *table_data, BIOSLinker *linker,
                const AcpiSpcrData *f, const uint8_t rev,
                const char *oem_id, const char *oem_table_id, const char *name);
#endif
