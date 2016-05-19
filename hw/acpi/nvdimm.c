/*
 * NVDIMM ACPI Implementation
 *
 * Copyright(C) 2015 Intel Corporation.
 *
 * Author:
 *  Xiao Guangrong <guangrong.xiao@linux.intel.com>
 *
 * NFIT is defined in ACPI 6.0: 5.2.25 NVDIMM Firmware Interface Table (NFIT)
 * and the DSM specification can be found at:
 *       http://pmem.io/documents/NVDIMM_DSM_Interface_Example.pdf
 *
 * Currently, it only supports PMEM Virtualization.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "qemu/osdep.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/bios-linker-loader.h"
#include "hw/nvram/fw_cfg.h"
#include "hw/mem/nvdimm.h"

static int nvdimm_plugged_device_list(Object *obj, void *opaque)
{
    GSList **list = opaque;

    if (object_dynamic_cast(obj, TYPE_NVDIMM)) {
        DeviceState *dev = DEVICE(obj);

        if (dev->realized) { /* only realized NVDIMMs matter */
            *list = g_slist_append(*list, DEVICE(obj));
        }
    }

    object_child_foreach(obj, nvdimm_plugged_device_list, opaque);
    return 0;
}

/*
 * inquire plugged NVDIMM devices and link them into the list which is
 * returned to the caller.
 *
 * Note: it is the caller's responsibility to free the list to avoid
 * memory leak.
 */
static GSList *nvdimm_get_plugged_device_list(void)
{
    GSList *list = NULL;

    object_child_foreach(qdev_get_machine(), nvdimm_plugged_device_list,
                         &list);
    return list;
}

#define NVDIMM_UUID_LE(a, b, c, d0, d1, d2, d3, d4, d5, d6, d7)             \
   { (a) & 0xff, ((a) >> 8) & 0xff, ((a) >> 16) & 0xff, ((a) >> 24) & 0xff, \
     (b) & 0xff, ((b) >> 8) & 0xff, (c) & 0xff, ((c) >> 8) & 0xff,          \
     (d0), (d1), (d2), (d3), (d4), (d5), (d6), (d7) }

/*
 * define Byte Addressable Persistent Memory (PM) Region according to
 * ACPI 6.0: 5.2.25.1 System Physical Address Range Structure.
 */
static const uint8_t nvdimm_nfit_spa_uuid[] =
      NVDIMM_UUID_LE(0x66f0d379, 0xb4f3, 0x4074, 0xac, 0x43, 0x0d, 0x33,
                     0x18, 0xb7, 0x8c, 0xdb);

/*
 * NVDIMM Firmware Interface Table
 * @signature: "NFIT"
 *
 * It provides information that allows OSPM to enumerate NVDIMM present in
 * the platform and associate system physical address ranges created by the
 * NVDIMMs.
 *
 * It is defined in ACPI 6.0: 5.2.25 NVDIMM Firmware Interface Table (NFIT)
 */
struct NvdimmNfitHeader {
    ACPI_TABLE_HEADER_DEF
    uint32_t reserved;
} QEMU_PACKED;
typedef struct NvdimmNfitHeader NvdimmNfitHeader;

/*
 * define NFIT structures according to ACPI 6.0: 5.2.25 NVDIMM Firmware
 * Interface Table (NFIT).
 */

/*
 * System Physical Address Range Structure
 *
 * It describes the system physical address ranges occupied by NVDIMMs and
 * the types of the regions.
 */
struct NvdimmNfitSpa {
    uint16_t type;
    uint16_t length;
    uint16_t spa_index;
    uint16_t flags;
    uint32_t reserved;
    uint32_t proximity_domain;
    uint8_t type_guid[16];
    uint64_t spa_base;
    uint64_t spa_length;
    uint64_t mem_attr;
} QEMU_PACKED;
typedef struct NvdimmNfitSpa NvdimmNfitSpa;

/*
 * Memory Device to System Physical Address Range Mapping Structure
 *
 * It enables identifying each NVDIMM region and the corresponding SPA
 * describing the memory interleave
 */
struct NvdimmNfitMemDev {
    uint16_t type;
    uint16_t length;
    uint32_t nfit_handle;
    uint16_t phys_id;
    uint16_t region_id;
    uint16_t spa_index;
    uint16_t dcr_index;
    uint64_t region_len;
    uint64_t region_offset;
    uint64_t region_dpa;
    uint16_t interleave_index;
    uint16_t interleave_ways;
    uint16_t flags;
    uint16_t reserved;
} QEMU_PACKED;
typedef struct NvdimmNfitMemDev NvdimmNfitMemDev;

/*
 * NVDIMM Control Region Structure
 *
 * It describes the NVDIMM and if applicable, Block Control Window.
 */
struct NvdimmNfitControlRegion {
    uint16_t type;
    uint16_t length;
    uint16_t dcr_index;
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t revision_id;
    uint16_t sub_vendor_id;
    uint16_t sub_device_id;
    uint16_t sub_revision_id;
    uint8_t reserved[6];
    uint32_t serial_number;
    uint16_t fic;
    uint16_t num_bcw;
    uint64_t bcw_size;
    uint64_t cmd_offset;
    uint64_t cmd_size;
    uint64_t status_offset;
    uint64_t status_size;
    uint16_t flags;
    uint8_t reserved2[6];
} QEMU_PACKED;
typedef struct NvdimmNfitControlRegion NvdimmNfitControlRegion;

/*
 * Module serial number is a unique number for each device. We use the
 * slot id of NVDIMM device to generate this number so that each device
 * associates with a different number.
 *
 * 0x123456 is a magic number we arbitrarily chose.
 */
static uint32_t nvdimm_slot_to_sn(int slot)
{
    return 0x123456 + slot;
}

/*
 * handle is used to uniquely associate nfit_memdev structure with NVDIMM
 * ACPI device - nfit_memdev.nfit_handle matches with the value returned
 * by ACPI device _ADR method.
 *
 * We generate the handle with the slot id of NVDIMM device and reserve
 * 0 for NVDIMM root device.
 */
static uint32_t nvdimm_slot_to_handle(int slot)
{
    return slot + 1;
}

/*
 * index uniquely identifies the structure, 0 is reserved which indicates
 * that the structure is not valid or the associated structure is not
 * present.
 *
 * Each NVDIMM device needs two indexes, one for nfit_spa and another for
 * nfit_dc which are generated by the slot id of NVDIMM device.
 */
static uint16_t nvdimm_slot_to_spa_index(int slot)
{
    return (slot + 1) << 1;
}

/* See the comments of nvdimm_slot_to_spa_index(). */
static uint32_t nvdimm_slot_to_dcr_index(int slot)
{
    return nvdimm_slot_to_spa_index(slot) + 1;
}

/* ACPI 6.0: 5.2.25.1 System Physical Address Range Structure */
static void
nvdimm_build_structure_spa(GArray *structures, DeviceState *dev)
{
    NvdimmNfitSpa *nfit_spa;
    uint64_t addr = object_property_get_int(OBJECT(dev), PC_DIMM_ADDR_PROP,
                                            NULL);
    uint64_t size = object_property_get_int(OBJECT(dev), PC_DIMM_SIZE_PROP,
                                            NULL);
    uint32_t node = object_property_get_int(OBJECT(dev), PC_DIMM_NODE_PROP,
                                            NULL);
    int slot = object_property_get_int(OBJECT(dev), PC_DIMM_SLOT_PROP,
                                            NULL);

    nfit_spa = acpi_data_push(structures, sizeof(*nfit_spa));

    nfit_spa->type = cpu_to_le16(0 /* System Physical Address Range
                                      Structure */);
    nfit_spa->length = cpu_to_le16(sizeof(*nfit_spa));
    nfit_spa->spa_index = cpu_to_le16(nvdimm_slot_to_spa_index(slot));

    /*
     * Control region is strict as all the device info, such as SN, index,
     * is associated with slot id.
     */
    nfit_spa->flags = cpu_to_le16(1 /* Control region is strictly for
                                       management during hot add/online
                                       operation */ |
                                  2 /* Data in Proximity Domain field is
                                       valid*/);

    /* NUMA node. */
    nfit_spa->proximity_domain = cpu_to_le32(node);
    /* the region reported as PMEM. */
    memcpy(nfit_spa->type_guid, nvdimm_nfit_spa_uuid,
           sizeof(nvdimm_nfit_spa_uuid));

    nfit_spa->spa_base = cpu_to_le64(addr);
    nfit_spa->spa_length = cpu_to_le64(size);

    /* It is the PMEM and can be cached as writeback. */
    nfit_spa->mem_attr = cpu_to_le64(0x8ULL /* EFI_MEMORY_WB */ |
                                     0x8000ULL /* EFI_MEMORY_NV */);
}

/*
 * ACPI 6.0: 5.2.25.2 Memory Device to System Physical Address Range Mapping
 * Structure
 */
static void
nvdimm_build_structure_memdev(GArray *structures, DeviceState *dev)
{
    NvdimmNfitMemDev *nfit_memdev;
    uint64_t addr = object_property_get_int(OBJECT(dev), PC_DIMM_ADDR_PROP,
                                            NULL);
    uint64_t size = object_property_get_int(OBJECT(dev), PC_DIMM_SIZE_PROP,
                                            NULL);
    int slot = object_property_get_int(OBJECT(dev), PC_DIMM_SLOT_PROP,
                                            NULL);
    uint32_t handle = nvdimm_slot_to_handle(slot);

    nfit_memdev = acpi_data_push(structures, sizeof(*nfit_memdev));

    nfit_memdev->type = cpu_to_le16(1 /* Memory Device to System Address
                                         Range Map Structure*/);
    nfit_memdev->length = cpu_to_le16(sizeof(*nfit_memdev));
    nfit_memdev->nfit_handle = cpu_to_le32(handle);

    /*
     * associate memory device with System Physical Address Range
     * Structure.
     */
    nfit_memdev->spa_index = cpu_to_le16(nvdimm_slot_to_spa_index(slot));
    /* associate memory device with Control Region Structure. */
    nfit_memdev->dcr_index = cpu_to_le16(nvdimm_slot_to_dcr_index(slot));

    /* The memory region on the device. */
    nfit_memdev->region_len = cpu_to_le64(size);
    nfit_memdev->region_dpa = cpu_to_le64(addr);

    /* Only one interleave for PMEM. */
    nfit_memdev->interleave_ways = cpu_to_le16(1);
}

/*
 * ACPI 6.0: 5.2.25.5 NVDIMM Control Region Structure.
 */
static void nvdimm_build_structure_dcr(GArray *structures, DeviceState *dev)
{
    NvdimmNfitControlRegion *nfit_dcr;
    int slot = object_property_get_int(OBJECT(dev), PC_DIMM_SLOT_PROP,
                                       NULL);
    uint32_t sn = nvdimm_slot_to_sn(slot);

    nfit_dcr = acpi_data_push(structures, sizeof(*nfit_dcr));

    nfit_dcr->type = cpu_to_le16(4 /* NVDIMM Control Region Structure */);
    nfit_dcr->length = cpu_to_le16(sizeof(*nfit_dcr));
    nfit_dcr->dcr_index = cpu_to_le16(nvdimm_slot_to_dcr_index(slot));

    /* vendor: Intel. */
    nfit_dcr->vendor_id = cpu_to_le16(0x8086);
    nfit_dcr->device_id = cpu_to_le16(1);

    /* The _DSM method is following Intel's DSM specification. */
    nfit_dcr->revision_id = cpu_to_le16(1 /* Current Revision supported
                                             in ACPI 6.0 is 1. */);
    nfit_dcr->serial_number = cpu_to_le32(sn);
    nfit_dcr->fic = cpu_to_le16(0x201 /* Format Interface Code. See Chapter
                                         2: NVDIMM Device Specific Method
                                         (DSM) in DSM Spec Rev1.*/);
}

static GArray *nvdimm_build_device_structure(GSList *device_list)
{
    GArray *structures = g_array_new(false, true /* clear */, 1);

    for (; device_list; device_list = device_list->next) {
        DeviceState *dev = device_list->data;

        /* build System Physical Address Range Structure. */
        nvdimm_build_structure_spa(structures, dev);

        /*
         * build Memory Device to System Physical Address Range Mapping
         * Structure.
         */
        nvdimm_build_structure_memdev(structures, dev);

        /* build NVDIMM Control Region Structure. */
        nvdimm_build_structure_dcr(structures, dev);
    }

    return structures;
}

static void nvdimm_build_nfit(GSList *device_list, GArray *table_offsets,
                              GArray *table_data, BIOSLinker *linker)
{
    GArray *structures = nvdimm_build_device_structure(device_list);
    unsigned int header;

    acpi_add_table(table_offsets, table_data);

    /* NFIT header. */
    header = table_data->len;
    acpi_data_push(table_data, sizeof(NvdimmNfitHeader));
    /* NVDIMM device structures. */
    g_array_append_vals(table_data, structures->data, structures->len);

    build_header(linker, table_data,
                 (void *)(table_data->data + header), "NFIT",
                 sizeof(NvdimmNfitHeader) + structures->len, 1, NULL, NULL);
    g_array_free(structures, true);
}

struct NvdimmDsmIn {
    uint32_t handle;
    uint32_t revision;
    uint32_t function;
    /* the remaining size in the page is used by arg3. */
    union {
        uint8_t arg3[4084];
    };
} QEMU_PACKED;
typedef struct NvdimmDsmIn NvdimmDsmIn;
QEMU_BUILD_BUG_ON(sizeof(NvdimmDsmIn) != 4096);

struct NvdimmDsmOut {
    /* the size of buffer filled by QEMU. */
    uint32_t len;
    uint8_t data[4092];
} QEMU_PACKED;
typedef struct NvdimmDsmOut NvdimmDsmOut;
QEMU_BUILD_BUG_ON(sizeof(NvdimmDsmOut) != 4096);

struct NvdimmDsmFunc0Out {
    /* the size of buffer filled by QEMU. */
     uint32_t len;
     uint32_t supported_func;
} QEMU_PACKED;
typedef struct NvdimmDsmFunc0Out NvdimmDsmFunc0Out;

struct NvdimmDsmFuncNoPayloadOut {
    /* the size of buffer filled by QEMU. */
     uint32_t len;
     uint32_t func_ret_status;
} QEMU_PACKED;
typedef struct NvdimmDsmFuncNoPayloadOut NvdimmDsmFuncNoPayloadOut;

static uint64_t
nvdimm_dsm_read(void *opaque, hwaddr addr, unsigned size)
{
    nvdimm_debug("BUG: we never read _DSM IO Port.\n");
    return 0;
}

static void
nvdimm_dsm_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    NvdimmDsmIn *in;
    hwaddr dsm_mem_addr = val;

    nvdimm_debug("dsm memory address %#" HWADDR_PRIx ".\n", dsm_mem_addr);

    /*
     * The DSM memory is mapped to guest address space so an evil guest
     * can change its content while we are doing DSM emulation. Avoid
     * this by copying DSM memory to QEMU local memory.
     */
    in = g_new(NvdimmDsmIn, 1);
    cpu_physical_memory_read(dsm_mem_addr, in, sizeof(*in));

    le32_to_cpus(&in->revision);
    le32_to_cpus(&in->function);
    le32_to_cpus(&in->handle);

    nvdimm_debug("Revision %#x Handler %#x Function %#x.\n", in->revision,
                 in->handle, in->function);

    /*
     * function 0 is called to inquire which functions are supported by
     * OSPM
     */
    if (in->function == 0) {
        NvdimmDsmFunc0Out func0 = {
            .len = cpu_to_le32(sizeof(func0)),
             /* No function supported other than function 0 */
            .supported_func = cpu_to_le32(0),
        };
        cpu_physical_memory_write(dsm_mem_addr, &func0, sizeof func0);
    } else {
        /* No function except function 0 is supported yet. */
        NvdimmDsmFuncNoPayloadOut out = {
            .len = cpu_to_le32(sizeof(out)),
            .func_ret_status = cpu_to_le32(1)  /* Not Supported */,
        };
        cpu_physical_memory_write(dsm_mem_addr, &out, sizeof(out));
    }

    g_free(in);
}

static const MemoryRegionOps nvdimm_dsm_ops = {
    .read = nvdimm_dsm_read,
    .write = nvdimm_dsm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

void nvdimm_init_acpi_state(AcpiNVDIMMState *state, MemoryRegion *io,
                            FWCfgState *fw_cfg, Object *owner)
{
    memory_region_init_io(&state->io_mr, owner, &nvdimm_dsm_ops, state,
                          "nvdimm-acpi-io", NVDIMM_ACPI_IO_LEN);
    memory_region_add_subregion(io, NVDIMM_ACPI_IO_BASE, &state->io_mr);

    state->dsm_mem = g_array_new(false, true /* clear */, 1);
    acpi_data_push(state->dsm_mem, sizeof(NvdimmDsmIn));
    fw_cfg_add_file(fw_cfg, NVDIMM_DSM_MEM_FILE, state->dsm_mem->data,
                    state->dsm_mem->len);
}

#define NVDIMM_COMMON_DSM      "NCAL"
#define NVDIMM_ACPI_MEM_ADDR   "MEMA"

static void nvdimm_build_common_dsm(Aml *dev)
{
    Aml *method, *ifctx, *function, *dsm_mem, *unpatched, *result_size;
    uint8_t byte_list[1];

    method = aml_method(NVDIMM_COMMON_DSM, 4, AML_SERIALIZED);
    function = aml_arg(2);
    dsm_mem = aml_name(NVDIMM_ACPI_MEM_ADDR);

    /*
     * do not support any method if DSM memory address has not been
     * patched.
     */
    unpatched = aml_if(aml_equal(dsm_mem, aml_int(0x0)));

    /*
     * function 0 is called to inquire what functions are supported by
     * OSPM
     */
    ifctx = aml_if(aml_equal(function, aml_int(0)));
    byte_list[0] = 0 /* No function Supported */;
    aml_append(ifctx, aml_return(aml_buffer(1, byte_list)));
    aml_append(unpatched, ifctx);

    /* No function is supported yet. */
    byte_list[0] = 1 /* Not Supported */;
    aml_append(unpatched, aml_return(aml_buffer(1, byte_list)));
    aml_append(method, unpatched);

    /*
     * The HDLE indicates the DSM function is issued from which device,
     * it is not used at this time as no function is supported yet.
     * Currently we make it always be 0 for all the devices and will set
     * the appropriate value once real function is implemented.
     */
    aml_append(method, aml_store(aml_int(0x0), aml_name("HDLE")));
    aml_append(method, aml_store(aml_arg(1), aml_name("REVS")));
    aml_append(method, aml_store(aml_arg(2), aml_name("FUNC")));

    /*
     * tell QEMU about the real address of DSM memory, then QEMU
     * gets the control and fills the result in DSM memory.
     */
    aml_append(method, aml_store(dsm_mem, aml_name("NTFI")));

    result_size = aml_local(1);
    aml_append(method, aml_store(aml_name("RLEN"), result_size));
    aml_append(method, aml_store(aml_shiftleft(result_size, aml_int(3)),
                                 result_size));
    aml_append(method, aml_create_field(aml_name("ODAT"), aml_int(0),
                                        result_size, "OBUF"));
    aml_append(method, aml_concatenate(aml_buffer(0, NULL), aml_name("OBUF"),
                                       aml_arg(6)));
    aml_append(method, aml_return(aml_arg(6)));
    aml_append(dev, method);
}

static void nvdimm_build_device_dsm(Aml *dev)
{
    Aml *method;

    method = aml_method("_DSM", 4, AML_NOTSERIALIZED);
    aml_append(method, aml_return(aml_call4(NVDIMM_COMMON_DSM, aml_arg(0),
                                  aml_arg(1), aml_arg(2), aml_arg(3))));
    aml_append(dev, method);
}

static void nvdimm_build_nvdimm_devices(GSList *device_list, Aml *root_dev)
{
    for (; device_list; device_list = device_list->next) {
        DeviceState *dev = device_list->data;
        int slot = object_property_get_int(OBJECT(dev), PC_DIMM_SLOT_PROP,
                                           NULL);
        uint32_t handle = nvdimm_slot_to_handle(slot);
        Aml *nvdimm_dev;

        nvdimm_dev = aml_device("NV%02X", slot);

        /*
         * ACPI 6.0: 9.20 NVDIMM Devices:
         *
         * _ADR object that is used to supply OSPM with unique address
         * of the NVDIMM device. This is done by returning the NFIT Device
         * handle that is used to identify the associated entries in ACPI
         * table NFIT or _FIT.
         */
        aml_append(nvdimm_dev, aml_name_decl("_ADR", aml_int(handle)));

        nvdimm_build_device_dsm(nvdimm_dev);
        aml_append(root_dev, nvdimm_dev);
    }
}

static void nvdimm_build_ssdt(GSList *device_list, GArray *table_offsets,
                              GArray *table_data, BIOSLinker *linker)
{
    Aml *ssdt, *sb_scope, *dev, *field;
    int mem_addr_offset, nvdimm_ssdt;

    acpi_add_table(table_offsets, table_data);

    ssdt = init_aml_allocator();
    acpi_data_push(ssdt->buf, sizeof(AcpiTableHeader));

    sb_scope = aml_scope("\\_SB");

    dev = aml_device("NVDR");

    /*
     * ACPI 6.0: 9.20 NVDIMM Devices:
     *
     * The ACPI Name Space device uses _HID of ACPI0012 to identify the root
     * NVDIMM interface device. Platform firmware is required to contain one
     * such device in _SB scope if NVDIMMs support is exposed by platform to
     * OSPM.
     * For each NVDIMM present or intended to be supported by platform,
     * platform firmware also exposes an ACPI Namespace Device under the
     * root device.
     */
    aml_append(dev, aml_name_decl("_HID", aml_string("ACPI0012")));

    /* map DSM memory and IO into ACPI namespace. */
    aml_append(dev, aml_operation_region("NPIO", AML_SYSTEM_IO,
               aml_int(NVDIMM_ACPI_IO_BASE), NVDIMM_ACPI_IO_LEN));
    aml_append(dev, aml_operation_region("NRAM", AML_SYSTEM_MEMORY,
               aml_name(NVDIMM_ACPI_MEM_ADDR), sizeof(NvdimmDsmIn)));

    /*
     * DSM notifier:
     * NTFI: write the address of DSM memory and notify QEMU to emulate
     *       the access.
     *
     * It is the IO port so that accessing them will cause VM-exit, the
     * control will be transferred to QEMU.
     */
    field = aml_field("NPIO", AML_DWORD_ACC, AML_NOLOCK, AML_PRESERVE);
    aml_append(field, aml_named_field("NTFI",
               sizeof(uint32_t) * BITS_PER_BYTE));
    aml_append(dev, field);

    /*
     * DSM input:
     * HDLE: store device's handle, it's zero if the _DSM call happens
     *       on NVDIMM Root Device.
     * REVS: store the Arg1 of _DSM call.
     * FUNC: store the Arg2 of _DSM call.
     * ARG3: store the Arg3 of _DSM call.
     *
     * They are RAM mapping on host so that these accesses never cause
     * VM-EXIT.
     */
    field = aml_field("NRAM", AML_DWORD_ACC, AML_NOLOCK, AML_PRESERVE);
    aml_append(field, aml_named_field("HDLE",
               sizeof(typeof_field(NvdimmDsmIn, handle)) * BITS_PER_BYTE));
    aml_append(field, aml_named_field("REVS",
               sizeof(typeof_field(NvdimmDsmIn, revision)) * BITS_PER_BYTE));
    aml_append(field, aml_named_field("FUNC",
               sizeof(typeof_field(NvdimmDsmIn, function)) * BITS_PER_BYTE));
    aml_append(field, aml_named_field("ARG3",
               (sizeof(NvdimmDsmIn) - offsetof(NvdimmDsmIn, arg3)) * BITS_PER_BYTE));
    aml_append(dev, field);

    /*
     * DSM output:
     * RLEN: the size of the buffer filled by QEMU.
     * ODAT: the buffer QEMU uses to store the result.
     *
     * Since the page is reused by both input and out, the input data
     * will be lost after storing new result into ODAT so we should fetch
     * all the input data before writing the result.
     */
    field = aml_field("NRAM", AML_DWORD_ACC, AML_NOLOCK, AML_PRESERVE);
    aml_append(field, aml_named_field("RLEN",
               sizeof(typeof_field(NvdimmDsmOut, len)) * BITS_PER_BYTE));
    aml_append(field, aml_named_field("ODAT",
               (sizeof(NvdimmDsmOut) - offsetof(NvdimmDsmOut, data)) * BITS_PER_BYTE));
    aml_append(dev, field);

    nvdimm_build_common_dsm(dev);
    nvdimm_build_device_dsm(dev);

    nvdimm_build_nvdimm_devices(device_list, dev);

    aml_append(sb_scope, dev);
    aml_append(ssdt, sb_scope);

    nvdimm_ssdt = table_data->len;

    /* copy AML table into ACPI tables blob and patch header there */
    g_array_append_vals(table_data, ssdt->buf->data, ssdt->buf->len);
    mem_addr_offset = build_append_named_dword(table_data,
                                               NVDIMM_ACPI_MEM_ADDR);

    bios_linker_loader_alloc(linker, NVDIMM_DSM_MEM_FILE, sizeof(NvdimmDsmIn),
                             false /* high memory */);
    bios_linker_loader_add_pointer(linker, ACPI_BUILD_TABLE_FILE,
                                   NVDIMM_DSM_MEM_FILE, table_data,
                                   table_data->data + mem_addr_offset,
                                   sizeof(uint32_t));
    build_header(linker, table_data,
        (void *)(table_data->data + nvdimm_ssdt),
        "SSDT", table_data->len - nvdimm_ssdt, 1, NULL, "NVDIMM");
    free_aml_allocator();
}

void nvdimm_build_acpi(GArray *table_offsets, GArray *table_data,
                       BIOSLinker *linker)
{
    GSList *device_list;

    /* no NVDIMM device is plugged. */
    device_list = nvdimm_get_plugged_device_list();
    if (!device_list) {
        return;
    }
    nvdimm_build_nfit(device_list, table_offsets, table_data, linker);
    nvdimm_build_ssdt(device_list, table_offsets, table_data, linker);
    g_slist_free(device_list);
}
