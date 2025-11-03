/*
 * QEMU IGVM configuration backend for guests
 *
 * Copyright (C) 2023-2024 SUSE
 *
 * Authors:
 *  Roy Hopkins <roy.hopkins@randomman.co.uk>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qemu/target-info-qapi.h"
#include "system/igvm.h"
#include "system/memory.h"
#include "system/address-spaces.h"
#include "hw/core/cpu.h"

#include <igvm/igvm.h>
#include <igvm/igvm_defs.h>

typedef struct QIgvmParameterData {
    QTAILQ_ENTRY(QIgvmParameterData) next;
    uint8_t *data;
    uint32_t size;
    uint32_t index;
} QIgvmParameterData;

/*
 * Some directives are specific to particular confidential computing platforms.
 * Define required types for each of those platforms here.
 */

/* SEV/SEV-ES/SEV-SNP */

/*
 * These structures are defined in "SEV Secure Nested Paging Firmware ABI
 * Specification" Rev 1.58, section 8.18.
 */
struct QEMU_PACKED sev_id_block {
    uint8_t ld[48];
    uint8_t family_id[16];
    uint8_t image_id[16];
    uint32_t version;
    uint32_t guest_svn;
    uint64_t policy;
};

struct QEMU_PACKED sev_id_authentication {
    uint32_t id_key_alg;
    uint32_t auth_key_algo;
    uint8_t reserved[56];
    uint8_t id_block_sig[512];
    uint8_t id_key[1028];
    uint8_t reserved2[60];
    uint8_t id_key_sig[512];
    uint8_t author_key[1028];
    uint8_t reserved3[892];
};

#define IGVM_SEV_ID_BLOCK_VERSION 1

/*
 * QIgvm contains the information required during processing
 * of a single IGVM file.
 */
typedef struct QIgvm {
    IgvmHandle file;
    ConfidentialGuestSupport *cgs;
    ConfidentialGuestSupportClass *cgsc;
    uint32_t compatibility_mask;
    unsigned current_header_index;
    QTAILQ_HEAD(, QIgvmParameterData) parameter_data;
    IgvmPlatformType platform_type;

    /*
     * SEV-SNP platforms can contain an ID block and authentication
     * that should be verified by the guest.
     */
    struct sev_id_block *id_block;
    struct sev_id_authentication *id_auth;

    /* Define the guest policy for SEV guests */
    uint64_t sev_policy;

    /* These variables keep track of contiguous page regions */
    IGVM_VHS_PAGE_DATA region_prev_page_data;
    uint64_t region_start;
    unsigned region_start_index;
    unsigned region_last_index;
    unsigned region_page_count;
} QIgvm;

static int qigvm_directive_page_data(QIgvm *ctx, const uint8_t *header_data,
                                     Error **errp);
static int qigvm_directive_vp_context(QIgvm *ctx, const uint8_t *header_data,
                                      Error **errp);
static int qigvm_directive_parameter_area(QIgvm *ctx,
                                          const uint8_t *header_data,
                                          Error **errp);
static int qigvm_directive_parameter_insert(QIgvm *ctx,
                                            const uint8_t *header_data,
                                            Error **errp);
static int qigvm_directive_memory_map(QIgvm *ctx, const uint8_t *header_data,
                                      Error **errp);
static int qigvm_directive_vp_count(QIgvm *ctx, const uint8_t *header_data,
                                    Error **errp);
static int qigvm_directive_environment_info(QIgvm *ctx,
                                            const uint8_t *header_data,
                                            Error **errp);
static int qigvm_directive_required_memory(QIgvm *ctx,
                                           const uint8_t *header_data,
                                           Error **errp);
static int qigvm_directive_snp_id_block(QIgvm *ctx, const uint8_t *header_data,
                                  Error **errp);
static int qigvm_initialization_guest_policy(QIgvm *ctx,
                                       const uint8_t *header_data,
                                       Error **errp);

struct QIGVMHandler {
    uint32_t type;
    uint32_t section;
    int (*handler)(QIgvm *ctx, const uint8_t *header_data, Error **errp);
};

static struct QIGVMHandler handlers[] = {
    { IGVM_VHT_PAGE_DATA, IGVM_HEADER_SECTION_DIRECTIVE,
      qigvm_directive_page_data },
    { IGVM_VHT_VP_CONTEXT, IGVM_HEADER_SECTION_DIRECTIVE,
      qigvm_directive_vp_context },
    { IGVM_VHT_PARAMETER_AREA, IGVM_HEADER_SECTION_DIRECTIVE,
      qigvm_directive_parameter_area },
    { IGVM_VHT_PARAMETER_INSERT, IGVM_HEADER_SECTION_DIRECTIVE,
      qigvm_directive_parameter_insert },
    { IGVM_VHT_MEMORY_MAP, IGVM_HEADER_SECTION_DIRECTIVE,
      qigvm_directive_memory_map },
    { IGVM_VHT_VP_COUNT_PARAMETER, IGVM_HEADER_SECTION_DIRECTIVE,
      qigvm_directive_vp_count },
    { IGVM_VHT_ENVIRONMENT_INFO_PARAMETER, IGVM_HEADER_SECTION_DIRECTIVE,
      qigvm_directive_environment_info },
    { IGVM_VHT_REQUIRED_MEMORY, IGVM_HEADER_SECTION_DIRECTIVE,
      qigvm_directive_required_memory },
    { IGVM_VHT_SNP_ID_BLOCK, IGVM_HEADER_SECTION_DIRECTIVE,
      qigvm_directive_snp_id_block },
    { IGVM_VHT_GUEST_POLICY, IGVM_HEADER_SECTION_INITIALIZATION,
      qigvm_initialization_guest_policy },
};

static int qigvm_handler(QIgvm *ctx, uint32_t type, Error **errp)
{
    size_t handler;
    IgvmHandle header_handle;
    const uint8_t *header_data;
    int result;

    for (handler = 0; handler < G_N_ELEMENTS(handlers); handler++) {
        if (handlers[handler].type != type) {
            continue;
        }
        header_handle = igvm_get_header(ctx->file, handlers[handler].section,
                                        ctx->current_header_index);
        if (header_handle < 0) {
            error_setg(
                errp,
                "IGVM file is invalid: Failed to read directive header (code: %d)",
                (int)header_handle);
            return -1;
        }
        header_data = igvm_get_buffer(ctx->file, header_handle) +
                      sizeof(IGVM_VHS_VARIABLE_HEADER);
        result = handlers[handler].handler(ctx, header_data, errp);
        igvm_free_buffer(ctx->file, header_handle);
        return result;
    }
    error_setg(errp,
               "IGVM: Unknown header type encountered when processing file: "
               "(type 0x%X)",
               type);
    return -1;
}

static void *qigvm_prepare_memory(QIgvm *ctx, uint64_t addr, uint64_t size,
                                  int region_identifier, Error **errp)
{
    ERRP_GUARD();
    MemoryRegion *igvm_pages = NULL;
    Int128 gpa_region_size;
    MemoryRegionSection mrs =
        memory_region_find(get_system_memory(), addr, size);
    if (mrs.mr) {
        if (!memory_region_is_ram(mrs.mr)) {
            memory_region_unref(mrs.mr);
            error_setg(
                errp,
                "Processing of IGVM file failed: Could not prepare memory "
                "at address 0x%lX due to existing non-RAM region",
                addr);
            return NULL;
        }

        gpa_region_size = int128_make64(size);
        if (int128_lt(mrs.size, gpa_region_size)) {
            memory_region_unref(mrs.mr);
            error_setg(
                errp,
                "Processing of IGVM file failed: Could not prepare memory "
                "at address 0x%lX: region size exceeded",
                addr);
            return NULL;
        }
        return qemu_map_ram_ptr(mrs.mr->ram_block, mrs.offset_within_region);
    } else {
        /*
         * The region_identifier is the is the index of the IGVM directive that
         * contains the page with the lowest GPA in the region. This will
         * generate a unique region name.
         */
        g_autofree char *region_name =
            g_strdup_printf("igvm.%X", region_identifier);
        igvm_pages = g_new0(MemoryRegion, 1);
        if (ctx->cgs && ctx->cgs->require_guest_memfd) {
            if (!memory_region_init_ram_guest_memfd(igvm_pages, NULL,
                                                    region_name, size, errp)) {
                return NULL;
            }
        } else {
            if (!memory_region_init_ram(igvm_pages, NULL, region_name, size,
                                        errp)) {
                return NULL;
            }
        }
        memory_region_add_subregion(get_system_memory(), addr, igvm_pages);
        return memory_region_get_ram_ptr(igvm_pages);
    }
}

static int qigvm_type_to_cgs_type(IgvmPageDataType memory_type, bool unmeasured,
                                  bool zero)
{
    switch (memory_type) {
    case IGVM_PAGE_DATA_TYPE_NORMAL: {
        if (unmeasured) {
            return CGS_PAGE_TYPE_UNMEASURED;
        } else {
            return zero ? CGS_PAGE_TYPE_ZERO : CGS_PAGE_TYPE_NORMAL;
        }
    }
    case IGVM_PAGE_DATA_TYPE_SECRETS:
        return CGS_PAGE_TYPE_SECRETS;
    case IGVM_PAGE_DATA_TYPE_CPUID_DATA:
        return CGS_PAGE_TYPE_CPUID;
    case IGVM_PAGE_DATA_TYPE_CPUID_XF:
        return CGS_PAGE_TYPE_CPUID;
    default:
        return -1;
    }
}

static bool qigvm_page_attrs_equal(IgvmHandle igvm, unsigned header_index,
                                   const IGVM_VHS_PAGE_DATA *page_1,
                                   const IGVM_VHS_PAGE_DATA *page_2)
{
    IgvmHandle data_handle1, data_handle2;

    /*
     * If one page has data and the other doesn't then this results in different
     * page types: NORMAL vs ZERO.
     */
    data_handle1 = igvm_get_header_data(igvm, IGVM_HEADER_SECTION_DIRECTIVE,
                                        header_index - 1);
    data_handle2 =
        igvm_get_header_data(igvm, IGVM_HEADER_SECTION_DIRECTIVE, header_index);
    if ((data_handle1 == IGVMAPI_NO_DATA ||
         data_handle2 == IGVMAPI_NO_DATA) &&
         data_handle1 != data_handle2) {
        return false;
    }
    return ((*(const uint32_t *)&page_1->flags ==
             *(const uint32_t *)&page_2->flags) &&
            (page_1->data_type == page_2->data_type) &&
            (page_1->compatibility_mask == page_2->compatibility_mask));
}

static int qigvm_process_mem_region(QIgvm *ctx, unsigned start_index,
                                    uint64_t gpa_start, unsigned page_count,
                                    const IgvmPageDataFlags *flags,
                                    const IgvmPageDataType page_type,
                                    Error **errp)
{
    uint8_t *region;
    IgvmHandle data_handle;
    const void *data;
    uint32_t data_size;
    unsigned page_index;
    bool zero = true;
    const uint64_t page_size = flags->is_2mb_page ? 0x200000 : 0x1000;
    int result;
    int cgs_page_type;

    region = qigvm_prepare_memory(ctx, gpa_start, page_count * page_size,
                                  start_index, errp);
    if (!region) {
        return -1;
    }

    for (page_index = 0; page_index < page_count; page_index++) {
        data_handle = igvm_get_header_data(
            ctx->file, IGVM_HEADER_SECTION_DIRECTIVE, page_index + start_index);
        if (data_handle == IGVMAPI_NO_DATA) {
            /* No data indicates a zero page */
            memset(&region[page_index * page_size], 0, page_size);
        } else if (data_handle < 0) {
            error_setg(
                errp,
                "IGVM file contains invalid page data for directive with "
                "index %d",
                page_index + start_index);
            return -1;
        } else {
            zero = false;
            data_size = igvm_get_buffer_size(ctx->file, data_handle);
            if (data_size < page_size) {
                memset(&region[page_index * page_size], 0, page_size);
            } else if (data_size > page_size) {
                error_setg(errp,
                           "IGVM file contains page data with invalid size for "
                           "directive with index %d",
                           page_index + start_index);
                return -1;
            }
            data = igvm_get_buffer(ctx->file, data_handle);
            memcpy(&region[page_index * page_size], data, data_size);
            igvm_free_buffer(ctx->file, data_handle);
        }
    }

    /*
     * If a confidential guest support object is provided then use it to set the
     * guest state.
     */
    if (ctx->cgs) {
        cgs_page_type =
            qigvm_type_to_cgs_type(page_type, flags->unmeasured, zero);
        if (cgs_page_type < 0) {
            error_setg(errp,
                       "Invalid page type in IGVM file. Directives: %d to %d, "
                       "page type: %d",
                       start_index, start_index + page_count, page_type);
            return -1;
        }

        result = ctx->cgsc->set_guest_state(
            gpa_start, region, page_size * page_count, cgs_page_type, 0, errp);
        if (result < 0) {
            return result;
        }
    }
    return 0;
}

static int qigvm_process_mem_page(QIgvm *ctx,
                                  const IGVM_VHS_PAGE_DATA *page_data,
                                  Error **errp)
{
    if (page_data) {
        if (ctx->region_page_count == 0) {
            ctx->region_start = page_data->gpa;
            ctx->region_start_index = ctx->current_header_index;
        } else {
            if (!qigvm_page_attrs_equal(ctx->file, ctx->current_header_index,
                                        page_data,
                                        &ctx->region_prev_page_data) ||
                ((ctx->region_prev_page_data.gpa +
                  (ctx->region_prev_page_data.flags.is_2mb_page ? 0x200000 :
                                                                  0x1000)) !=
                 page_data->gpa) ||
                (ctx->region_last_index != (ctx->current_header_index - 1))) {
                /* End of current region */
                if (qigvm_process_mem_region(
                        ctx, ctx->region_start_index, ctx->region_start,
                        ctx->region_page_count,
                        &ctx->region_prev_page_data.flags,
                        ctx->region_prev_page_data.data_type, errp) < 0) {
                    return -1;
                }
                ctx->region_page_count = 0;
                ctx->region_start = page_data->gpa;
                ctx->region_start_index = ctx->current_header_index;
            }
        }
        memcpy(&ctx->region_prev_page_data, page_data,
               sizeof(ctx->region_prev_page_data));
        ctx->region_last_index = ctx->current_header_index;
        ctx->region_page_count++;
    } else {
        if (ctx->region_page_count > 0) {
            if (qigvm_process_mem_region(
                    ctx, ctx->region_start_index, ctx->region_start,
                    ctx->region_page_count, &ctx->region_prev_page_data.flags,
                    ctx->region_prev_page_data.data_type, errp) < 0) {
                return -1;
            }
            ctx->region_page_count = 0;
        }
    }
    return 0;
}

static int qigvm_directive_page_data(QIgvm *ctx, const uint8_t *header_data,
                                     Error **errp)
{
    const IGVM_VHS_PAGE_DATA *page_data =
        (const IGVM_VHS_PAGE_DATA *)header_data;
    if (page_data->compatibility_mask & ctx->compatibility_mask) {
        return qigvm_process_mem_page(ctx, page_data, errp);
    }
    return 0;
}

static int qigvm_directive_vp_context(QIgvm *ctx, const uint8_t *header_data,
                                      Error **errp)
{
    const IGVM_VHS_VP_CONTEXT *vp_context =
        (const IGVM_VHS_VP_CONTEXT *)header_data;
    IgvmHandle data_handle;
    uint8_t *data;
    int result;

    if (!(vp_context->compatibility_mask & ctx->compatibility_mask)) {
        return 0;
    }

    data_handle = igvm_get_header_data(ctx->file, IGVM_HEADER_SECTION_DIRECTIVE,
                                       ctx->current_header_index);
    if (data_handle < 0) {
        error_setg(errp, "Invalid VP context in IGVM file. Error code: %X",
                   data_handle);
        return -1;
    }

    data = (uint8_t *)igvm_get_buffer(ctx->file, data_handle);

    if (ctx->cgs) {
        result = ctx->cgsc->set_guest_state(
            vp_context->gpa, data, igvm_get_buffer_size(ctx->file, data_handle),
            CGS_PAGE_TYPE_VMSA, vp_context->vp_index, errp);
    } else if (target_arch() == SYS_EMU_TARGET_X86_64) {
        result = qigvm_x86_set_vp_context(data, vp_context->vp_index, errp);
    } else {
        error_setg(
            errp,
            "A VP context is present in the IGVM file but is not supported "
            "by the current system.");
        result = -1;
    }

    igvm_free_buffer(ctx->file, data_handle);
    if (result < 0) {
        return result;
    }
    return 0;
}

static int qigvm_directive_parameter_area(QIgvm *ctx,
                                          const uint8_t *header_data,
                                          Error **errp)
{
    const IGVM_VHS_PARAMETER_AREA *param_area =
        (const IGVM_VHS_PARAMETER_AREA *)header_data;
    QIgvmParameterData *param_entry;

    param_entry = g_new0(QIgvmParameterData, 1);
    param_entry->size = param_area->number_of_bytes;
    param_entry->index = param_area->parameter_area_index;
    param_entry->data = g_malloc0(param_entry->size);

    QTAILQ_INSERT_TAIL(&ctx->parameter_data, param_entry, next);
    return 0;
}

static int qigvm_directive_parameter_insert(QIgvm *ctx,
                                            const uint8_t *header_data,
                                            Error **errp)
{
    const IGVM_VHS_PARAMETER_INSERT *param =
        (const IGVM_VHS_PARAMETER_INSERT *)header_data;
    QIgvmParameterData *param_entry;
    int result;
    void *region;

    if (!(param->compatibility_mask & ctx->compatibility_mask)) {
        return 0;
    }

    QTAILQ_FOREACH(param_entry, &ctx->parameter_data, next)
    {
        if (param_entry->index == param->parameter_area_index) {
            region = qigvm_prepare_memory(ctx, param->gpa, param_entry->size,
                                          ctx->current_header_index, errp);
            if (!region) {
                return -1;
            }
            memcpy(region, param_entry->data, param_entry->size);
            g_free(param_entry->data);
            param_entry->data = NULL;

            /*
             * If a confidential guest support object is provided then use it to
             * set the guest state.
             */
            if (ctx->cgs) {
                result = ctx->cgsc->set_guest_state(param->gpa, region,
                                                    param_entry->size,
                                                    CGS_PAGE_TYPE_UNMEASURED, 0,
                                                    errp);
                if (result < 0) {
                    return -1;
                }
            }
        }
    }
    return 0;
}

static int qigvm_cmp_mm_entry(const void *a, const void *b)
{
    const IGVM_VHS_MEMORY_MAP_ENTRY *entry_a =
        (const IGVM_VHS_MEMORY_MAP_ENTRY *)a;
    const IGVM_VHS_MEMORY_MAP_ENTRY *entry_b =
        (const IGVM_VHS_MEMORY_MAP_ENTRY *)b;
    if (entry_a->starting_gpa_page_number < entry_b->starting_gpa_page_number) {
        return -1;
    } else if (entry_a->starting_gpa_page_number >
               entry_b->starting_gpa_page_number) {
        return 1;
    } else {
        return 0;
    }
}

static int qigvm_directive_memory_map(QIgvm *ctx, const uint8_t *header_data,
                                      Error **errp)
{
    const IGVM_VHS_PARAMETER *param = (const IGVM_VHS_PARAMETER *)header_data;
    int (*get_mem_map_entry)(int index, ConfidentialGuestMemoryMapEntry *entry,
                             Error **errp) = NULL;
    QIgvmParameterData *param_entry;
    int max_entry_count;
    int entry = 0;
    IGVM_VHS_MEMORY_MAP_ENTRY *mm_entry;
    ConfidentialGuestMemoryMapEntry cgmm_entry;
    int retval = 0;

    if (ctx->cgs && ctx->cgsc->get_mem_map_entry) {
        get_mem_map_entry = ctx->cgsc->get_mem_map_entry;

    } else if (target_arch() == SYS_EMU_TARGET_X86_64) {
        get_mem_map_entry = qigvm_x86_get_mem_map_entry;

    } else {
        error_setg(errp,
                   "IGVM file contains a memory map but this is not supported "
                   "by the current system.");
        return -1;
    }

    /* Find the parameter area that should hold the memory map */
    QTAILQ_FOREACH(param_entry, &ctx->parameter_data, next)
    {
        if (param_entry->index == param->parameter_area_index) {
            max_entry_count =
                param_entry->size / sizeof(IGVM_VHS_MEMORY_MAP_ENTRY);
            mm_entry = (IGVM_VHS_MEMORY_MAP_ENTRY *)param_entry->data;

            retval = get_mem_map_entry(entry, &cgmm_entry, errp);
            while (retval == 0) {
                if (entry >= max_entry_count) {
                    error_setg(
                        errp,
                        "IGVM: guest memory map size exceeds parameter area defined in IGVM file");
                    return -1;
                }
                mm_entry[entry].starting_gpa_page_number = cgmm_entry.gpa >> 12;
                mm_entry[entry].number_of_pages = cgmm_entry.size >> 12;

                switch (cgmm_entry.type) {
                case CGS_MEM_RAM:
                    mm_entry[entry].entry_type =
                        IGVM_MEMORY_MAP_ENTRY_TYPE_MEMORY;
                    break;
                case CGS_MEM_RESERVED:
                    mm_entry[entry].entry_type =
                        IGVM_MEMORY_MAP_ENTRY_TYPE_PLATFORM_RESERVED;
                    break;
                case CGS_MEM_ACPI:
                    mm_entry[entry].entry_type =
                        IGVM_MEMORY_MAP_ENTRY_TYPE_PLATFORM_RESERVED;
                    break;
                case CGS_MEM_NVS:
                    mm_entry[entry].entry_type =
                        IGVM_MEMORY_MAP_ENTRY_TYPE_PERSISTENT;
                    break;
                case CGS_MEM_UNUSABLE:
                    mm_entry[entry].entry_type =
                        IGVM_MEMORY_MAP_ENTRY_TYPE_PLATFORM_RESERVED;
                    break;
                }
                retval = get_mem_map_entry(++entry, &cgmm_entry, errp);
            }
            if (retval < 0) {
                return retval;
            }
            /* The entries need to be sorted */
            qsort(mm_entry, entry, sizeof(IGVM_VHS_MEMORY_MAP_ENTRY),
                  qigvm_cmp_mm_entry);

            break;
        }
    }
    return 0;
}

static int qigvm_directive_vp_count(QIgvm *ctx, const uint8_t *header_data,
                                    Error **errp)
{
    const IGVM_VHS_PARAMETER *param = (const IGVM_VHS_PARAMETER *)header_data;
    QIgvmParameterData *param_entry;
    uint32_t *vp_count;
    CPUState *cpu;

    QTAILQ_FOREACH(param_entry, &ctx->parameter_data, next)
    {
        if (param_entry->index == param->parameter_area_index) {
            vp_count = (uint32_t *)(param_entry->data + param->byte_offset);
            *vp_count = 0;
            CPU_FOREACH(cpu)
            {
                (*vp_count)++;
            }
            break;
        }
    }
    return 0;
}

static int qigvm_directive_environment_info(QIgvm *ctx,
                                            const uint8_t *header_data,
                                            Error **errp)
{
    const IGVM_VHS_PARAMETER *param = (const IGVM_VHS_PARAMETER *)header_data;
    QIgvmParameterData *param_entry;
    IgvmEnvironmentInfo *environmental_state;

    QTAILQ_FOREACH(param_entry, &ctx->parameter_data, next)
    {
        if (param_entry->index == param->parameter_area_index) {
            environmental_state =
                (IgvmEnvironmentInfo *)(param_entry->data + param->byte_offset);
            environmental_state->memory_is_shared = 1;
            break;
        }
    }
    return 0;
}

static int qigvm_directive_required_memory(QIgvm *ctx,
                                           const uint8_t *header_data,
                                           Error **errp)
{
    const IGVM_VHS_REQUIRED_MEMORY *mem =
        (const IGVM_VHS_REQUIRED_MEMORY *)header_data;
    uint8_t *region;
    int result;

    if (!(mem->compatibility_mask & ctx->compatibility_mask)) {
        return 0;
    }

    region = qigvm_prepare_memory(ctx, mem->gpa, mem->number_of_bytes,
                                  ctx->current_header_index, errp);
    if (!region) {
        return -1;
    }
    if (ctx->cgs) {
        result =
            ctx->cgsc->set_guest_state(mem->gpa, region, mem->number_of_bytes,
                                       CGS_PAGE_TYPE_REQUIRED_MEMORY, 0, errp);
        if (result < 0) {
            return result;
        }
    }
    return 0;
}

static int qigvm_directive_snp_id_block(QIgvm *ctx, const uint8_t *header_data,
                                  Error **errp)
{
    const IGVM_VHS_SNP_ID_BLOCK *igvm_id =
        (const IGVM_VHS_SNP_ID_BLOCK *)header_data;

    if (!(igvm_id->compatibility_mask & ctx->compatibility_mask)) {
        return 0;
    }

    if (ctx->id_block) {
        error_setg(errp, "IGVM: Multiple ID blocks encountered "
                            "in IGVM file.");
        return -1;
    }
    ctx->id_block = g_new0(struct sev_id_block, 1);
    ctx->id_auth = g_new0(struct sev_id_authentication, 1);

    memcpy(ctx->id_block->family_id, igvm_id->family_id,
            sizeof(ctx->id_block->family_id));
    memcpy(ctx->id_block->image_id, igvm_id->image_id,
            sizeof(ctx->id_block->image_id));
    ctx->id_block->guest_svn = igvm_id->guest_svn;
    ctx->id_block->version = IGVM_SEV_ID_BLOCK_VERSION;
    memcpy(ctx->id_block->ld, igvm_id->ld, sizeof(ctx->id_block->ld));

    ctx->id_auth->id_key_alg = igvm_id->id_key_algorithm;
    assert(sizeof(igvm_id->id_key_signature) <=
           sizeof(ctx->id_auth->id_block_sig));
    memcpy(ctx->id_auth->id_block_sig, &igvm_id->id_key_signature,
           sizeof(igvm_id->id_key_signature));

    ctx->id_auth->auth_key_algo = igvm_id->author_key_algorithm;
    assert(sizeof(igvm_id->author_key_signature) <=
           sizeof(ctx->id_auth->id_key_sig));
    memcpy(ctx->id_auth->id_key_sig, &igvm_id->author_key_signature,
           sizeof(igvm_id->author_key_signature));

    /*
     * SEV and IGVM public key structure population are slightly different.
     * See SEV Secure Nested Paging Firmware ABI Specification, Chapter 10.
     */
    *((uint32_t *)ctx->id_auth->id_key) = igvm_id->id_public_key.curve;
    memcpy(&ctx->id_auth->id_key[4], &igvm_id->id_public_key.qx, 72);
    memcpy(&ctx->id_auth->id_key[76], &igvm_id->id_public_key.qy, 72);

    *((uint32_t *)ctx->id_auth->author_key) =
        igvm_id->author_public_key.curve;
    memcpy(&ctx->id_auth->author_key[4], &igvm_id->author_public_key.qx,
            72);
    memcpy(&ctx->id_auth->author_key[76], &igvm_id->author_public_key.qy,
            72);

    return 0;
}

static int qigvm_initialization_guest_policy(QIgvm *ctx,
                                       const uint8_t *header_data, Error **errp)
{
    const IGVM_VHS_GUEST_POLICY *guest =
        (const IGVM_VHS_GUEST_POLICY *)header_data;

    if (guest->compatibility_mask & ctx->compatibility_mask) {
        ctx->sev_policy = guest->policy;
    }
    return 0;
}

static int qigvm_supported_platform_compat_mask(QIgvm *ctx, Error **errp)
{
    int32_t header_count;
    unsigned header_index;
    IgvmHandle header_handle;
    IGVM_VHS_SUPPORTED_PLATFORM *platform;
    uint32_t compatibility_mask_sev = 0;
    uint32_t compatibility_mask_sev_es = 0;
    uint32_t compatibility_mask_sev_snp = 0;
    uint32_t compatibility_mask = 0;

    header_count = igvm_header_count(ctx->file, IGVM_HEADER_SECTION_PLATFORM);
    if (header_count < 0) {
        error_setg(errp,
                   "Invalid platform header count in IGVM file. Error code: %X",
                   header_count);
        return -1;
    }

    for (header_index = 0; header_index < (unsigned)header_count;
         header_index++) {
        IgvmVariableHeaderType typ = igvm_get_header_type(
            ctx->file, IGVM_HEADER_SECTION_PLATFORM, header_index);
        if (typ == IGVM_VHT_SUPPORTED_PLATFORM) {
            header_handle = igvm_get_header(
                ctx->file, IGVM_HEADER_SECTION_PLATFORM, header_index);
            if (header_handle < 0) {
                error_setg(errp,
                           "Invalid platform header in IGVM file. "
                           "Index: %d, Error code: %X",
                           header_index, header_handle);
                return -1;
            }
            platform =
                (IGVM_VHS_SUPPORTED_PLATFORM *)(igvm_get_buffer(ctx->file,
                                                                header_handle) +
                                                sizeof(
                                                    IGVM_VHS_VARIABLE_HEADER));
            if ((platform->platform_type == IGVM_PLATFORM_TYPE_SEV_ES) &&
                ctx->cgs) {
                if (ctx->cgsc->check_support(
                        CGS_PLATFORM_SEV_ES, platform->platform_version,
                        platform->highest_vtl, platform->shared_gpa_boundary)) {
                    compatibility_mask_sev_es = platform->compatibility_mask;
                }
            } else if ((platform->platform_type == IGVM_PLATFORM_TYPE_SEV) &&
                       ctx->cgs) {
                if (ctx->cgsc->check_support(
                        CGS_PLATFORM_SEV, platform->platform_version,
                        platform->highest_vtl, platform->shared_gpa_boundary)) {
                    compatibility_mask_sev = platform->compatibility_mask;
                }
            } else if ((platform->platform_type ==
                        IGVM_PLATFORM_TYPE_SEV_SNP) &&
                       ctx->cgs) {
                if (ctx->cgsc->check_support(
                        CGS_PLATFORM_SEV_SNP, platform->platform_version,
                        platform->highest_vtl, platform->shared_gpa_boundary)) {
                    compatibility_mask_sev_snp = platform->compatibility_mask;
                }
            } else if (platform->platform_type == IGVM_PLATFORM_TYPE_NATIVE) {
                compatibility_mask = platform->compatibility_mask;
            }
            igvm_free_buffer(ctx->file, header_handle);
        }
    }
    /* Choose the strongest supported isolation technology */
    if (compatibility_mask_sev_snp != 0) {
        ctx->compatibility_mask = compatibility_mask_sev_snp;
        ctx->platform_type = IGVM_PLATFORM_TYPE_SEV_SNP;
    } else if (compatibility_mask_sev_es != 0) {
        ctx->compatibility_mask = compatibility_mask_sev_es;
        ctx->platform_type = IGVM_PLATFORM_TYPE_SEV_ES;
    } else if (compatibility_mask_sev != 0) {
        ctx->compatibility_mask = compatibility_mask_sev;
        ctx->platform_type = IGVM_PLATFORM_TYPE_SEV;
    } else if (compatibility_mask != 0) {
        ctx->compatibility_mask = compatibility_mask;
        ctx->platform_type = IGVM_PLATFORM_TYPE_NATIVE;
    } else {
        error_setg(
            errp,
            "IGVM file does not describe a compatible supported platform");
        return -1;
    }
    return 0;
}

static int qigvm_handle_policy(QIgvm *ctx, Error **errp)
{
    if (ctx->platform_type == IGVM_PLATFORM_TYPE_SEV_SNP) {
        int id_block_len = 0;
        int id_auth_len = 0;
        if (ctx->id_block) {
            ctx->id_block->policy = ctx->sev_policy;
            id_block_len = sizeof(struct sev_id_block);
            id_auth_len = sizeof(struct sev_id_authentication);
        }
        return ctx->cgsc->set_guest_policy(GUEST_POLICY_SEV, ctx->sev_policy,
                                          ctx->id_block, id_block_len,
                                          ctx->id_auth, id_auth_len, errp);
    }
    return 0;
}

static IgvmHandle qigvm_file_init(char *filename, Error **errp)
{
    IgvmHandle igvm;
    g_autofree uint8_t *buf = NULL;
    unsigned long len;
    g_autoptr(GError) gerr = NULL;

    if (!g_file_get_contents(filename, (gchar **)&buf, &len, &gerr)) {
        error_setg(errp, "Unable to load %s: %s", filename, gerr->message);
        return -1;
    }

    igvm = igvm_new_from_binary(buf, len);
    if (igvm < 0) {
        error_setg(errp, "Unable to parse IGVM file %s: %d", filename, igvm);
        return -1;
    }
    return igvm;
}

int qigvm_process_file(IgvmCfg *cfg, ConfidentialGuestSupport *cgs,
                       bool onlyVpContext, Error **errp)
{
    int32_t header_count;
    QIgvmParameterData *parameter;
    int retval = -1;
    QIgvm ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.file = qigvm_file_init(cfg->filename, errp);
    if (ctx.file < 0) {
        return -1;
    }

    /*
     * The ConfidentialGuestSupport object is optional and allows a confidential
     * guest platform to perform extra processing, such as page measurement, on
     * IGVM directives.
     */
    ctx.cgs = cgs;
    ctx.cgsc = cgs ? CONFIDENTIAL_GUEST_SUPPORT_GET_CLASS(cgs) : NULL;

    /*
     * Check that the IGVM file provides configuration for the current
     * platform
     */
    if (qigvm_supported_platform_compat_mask(&ctx, errp) < 0) {
        goto cleanup;
    }

    header_count = igvm_header_count(ctx.file, IGVM_HEADER_SECTION_DIRECTIVE);
    if (header_count <= 0) {
        error_setg(
            errp, "Invalid directive header count in IGVM file. Error code: %X",
            header_count);
        goto cleanup;
    }

    QTAILQ_INIT(&ctx.parameter_data);

    for (ctx.current_header_index = 0;
         ctx.current_header_index < (unsigned)header_count;
         ctx.current_header_index++) {
        IgvmVariableHeaderType type = igvm_get_header_type(
            ctx.file, IGVM_HEADER_SECTION_DIRECTIVE, ctx.current_header_index);
        if (!onlyVpContext || (type == IGVM_VHT_VP_CONTEXT)) {
            if (qigvm_handler(&ctx, type, errp) < 0) {
                goto cleanup_parameters;
            }
        }
    }

    /*
     * If only processing the VP context then we don't need to process
     * any more of the file.
     */
    if (onlyVpContext) {
        retval = 0;
        goto cleanup_parameters;
    }

    header_count =
        igvm_header_count(ctx.file, IGVM_HEADER_SECTION_INITIALIZATION);
    if (header_count < 0) {
        error_setg(
            errp,
            "Invalid initialization header count in IGVM file. Error code: %X",
            header_count);
        goto cleanup_parameters;
    }

    for (ctx.current_header_index = 0;
         ctx.current_header_index < (unsigned)header_count;
         ctx.current_header_index++) {
        IgvmVariableHeaderType type =
            igvm_get_header_type(ctx.file, IGVM_HEADER_SECTION_INITIALIZATION,
                                 ctx.current_header_index);
        if (qigvm_handler(&ctx, type, errp) < 0) {
            goto cleanup_parameters;
        }
    }

    /*
     * Contiguous pages of data with compatible flags are grouped together in
     * order to reduce the number of memory regions we create. Make sure the
     * last group is processed with this call.
     */
    retval = qigvm_process_mem_page(&ctx, NULL, errp);

    if (retval == 0) {
        retval = qigvm_handle_policy(&ctx, errp);
    }

cleanup_parameters:
    QTAILQ_FOREACH(parameter, &ctx.parameter_data, next)
    {
        g_free(parameter->data);
        parameter->data = NULL;
    }
    g_free(ctx.id_block);
    g_free(ctx.id_auth);

cleanup:
    igvm_free(ctx.file);

    return retval;
}
