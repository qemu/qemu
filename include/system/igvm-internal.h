/*
 * QEMU IGVM private data structures
 *
 * Everything which depends on igvm library headers goes here.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_IGVM_INTERNAL_H
#define QEMU_IGVM_INTERNAL_H

#include "qemu/queue.h"
#include "qemu/typedefs.h"
#include "qom/object.h"
#include "hw/core/boards.h"
#include "hw/core/resettable.h"

#include "system/confidential-guest-support.h"
#include <igvm/igvm.h>

struct IgvmCfg {
    ObjectClass parent_class;

    /*
     * filename: Filename that specifies a file that contains the configuration
     *           of the guest in Independent Guest Virtual Machine (IGVM)
     *           format.
     */
    char *filename;
    IgvmHandle file;
    ResettableState reset_state;
};

typedef struct QIgvmParameterData {
    QTAILQ_ENTRY(QIgvmParameterData) next;
    uint8_t *data;
    uint32_t size;
    uint32_t index;
} QIgvmParameterData;

/*
 * QIgvm contains the information required during processing of a single IGVM
 * file.
 */
typedef struct QIgvm {
    IgvmHandle file;
    MachineState *machine_state;
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

IgvmHandle qigvm_file_init(char *filename, Error **errp);

QIgvmParameterData*
qigvm_find_param_entry(QIgvm *igvm, uint32_t parameter_area_index);

/*
 *  IGVM parameter handlers
 */
int qigvm_directive_madt(QIgvm *ctx, const uint8_t *header_data, Error **errp);

#endif
