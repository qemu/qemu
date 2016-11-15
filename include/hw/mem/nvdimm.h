/*
 * Non-Volatile Dual In-line Memory Module Virtualization Implementation
 *
 * Copyright(C) 2015 Intel Corporation.
 *
 * Author:
 *  Xiao Guangrong <guangrong.xiao@linux.intel.com>
 *
 * NVDIMM specifications and some documents can be found at:
 * NVDIMM ACPI device and NFIT are introduced in ACPI 6:
 *      http://www.uefi.org/sites/default/files/resources/ACPI_6.0.pdf
 * NVDIMM Namespace specification:
 *      http://pmem.io/documents/NVDIMM_Namespace_Spec.pdf
 * DSM Interface Example:
 *      http://pmem.io/documents/NVDIMM_DSM_Interface_Example.pdf
 * Driver Writer's Guide:
 *      http://pmem.io/documents/NVDIMM_Driver_Writers_Guide.pdf
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_NVDIMM_H
#define QEMU_NVDIMM_H

#include "hw/mem/pc-dimm.h"
#include "hw/acpi/bios-linker-loader.h"

#define NVDIMM_DEBUG 0
#define nvdimm_debug(fmt, ...)                                \
    do {                                                      \
        if (NVDIMM_DEBUG) {                                   \
            fprintf(stderr, "nvdimm: " fmt, ## __VA_ARGS__);  \
        }                                                     \
    } while (0)

/*
 * The minimum label data size is required by NVDIMM Namespace
 * specification, see the chapter 2 Namespaces:
 *   "NVDIMMs following the NVDIMM Block Mode Specification use an area
 *    at least 128KB in size, which holds around 1000 labels."
 */
#define MIN_NAMESPACE_LABEL_SIZE      (128UL << 10)

#define TYPE_NVDIMM      "nvdimm"
#define NVDIMM(obj)      OBJECT_CHECK(NVDIMMDevice, (obj), TYPE_NVDIMM)
#define NVDIMM_CLASS(oc) OBJECT_CLASS_CHECK(NVDIMMClass, (oc), TYPE_NVDIMM)
#define NVDIMM_GET_CLASS(obj) OBJECT_GET_CLASS(NVDIMMClass, (obj), \
                                               TYPE_NVDIMM)
struct NVDIMMDevice {
    /* private */
    PCDIMMDevice parent_obj;

    /* public */

    /*
     * the size of label data in NVDIMM device which is presented to
     * guest via __DSM "Get Namespace Label Size" function.
     */
    uint64_t label_size;

    /*
     * the address of label data which is read by __DSM "Get Namespace
     * Label Data" function and written by __DSM "Set Namespace Label
     * Data" function.
     */
    void *label_data;

    /*
     * it's the PMEM region in NVDIMM device, which is presented to
     * guest via ACPI NFIT and _FIT method if NVDIMM hotplug is supported.
     */
    MemoryRegion nvdimm_mr;
};
typedef struct NVDIMMDevice NVDIMMDevice;

struct NVDIMMClass {
    /* private */
    PCDIMMDeviceClass parent_class;

    /* public */

    /* read @size bytes from NVDIMM label data at @offset into @buf. */
    void (*read_label_data)(NVDIMMDevice *nvdimm, void *buf,
                            uint64_t size, uint64_t offset);
    /* write @size bytes from @buf to NVDIMM label data at @offset. */
    void (*write_label_data)(NVDIMMDevice *nvdimm, const void *buf,
                             uint64_t size, uint64_t offset);
};
typedef struct NVDIMMClass NVDIMMClass;

#define NVDIMM_DSM_MEM_FILE     "etc/acpi/nvdimm-mem"

/*
 * 32 bits IO port starting from 0x0a18 in guest is reserved for
 * NVDIMM ACPI emulation.
 */
#define NVDIMM_ACPI_IO_BASE     0x0a18
#define NVDIMM_ACPI_IO_LEN      4

/*
 * NvdimmFitBuffer:
 * @fit: FIT structures for present NVDIMMs. It is updated when
 *   the NVDIMM device is plugged or unplugged.
 * @dirty: It allows OSPM to detect change and restart read in
 *   progress if there is any.
 */
struct NvdimmFitBuffer {
    GArray *fit;
    bool dirty;
};
typedef struct NvdimmFitBuffer NvdimmFitBuffer;

struct AcpiNVDIMMState {
    /* detect if NVDIMM support is enabled. */
    bool is_enabled;

    /* the data of the fw_cfg file NVDIMM_DSM_MEM_FILE. */
    GArray *dsm_mem;

    NvdimmFitBuffer fit_buf;

    /* the IO region used by OSPM to transfer control to QEMU. */
    MemoryRegion io_mr;
};
typedef struct AcpiNVDIMMState AcpiNVDIMMState;

void nvdimm_init_acpi_state(AcpiNVDIMMState *state, MemoryRegion *io,
                            FWCfgState *fw_cfg, Object *owner);
void nvdimm_build_acpi(GArray *table_offsets, GArray *table_data,
                       BIOSLinker *linker, AcpiNVDIMMState *state,
                       uint32_t ram_slots);
void nvdimm_plug(AcpiNVDIMMState *state);
void nvdimm_acpi_plug_cb(HotplugHandler *hotplug_dev, DeviceState *dev);
#endif
