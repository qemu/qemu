/*
 * Firmware Assisted Dump in PSeries
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/ppc/spapr.h"
#include "system/cpus.h"

/*
 * Handle the "FADUMP_CMD_REGISTER" command in 'ibm,configure-kernel-dump'
 *
 * Note: Any changes made by the kernel to the fadump memory struct won't
 * reflect in QEMU after the 'ibm,configure-kernel-dump' RTAS call has returned,
 * as we store the passed fadump memory structure passed during fadump
 * registration.
 * Kernel has to invalidate & re-register fadump, if it intends to make any
 * changes to the fadump memory structure
 *
 * Returns:
 *  * RTAS_OUT_SUCCESS: On successful registration
 *  * RTAS_OUT_PARAM_ERROR: If parameters are not correct, eg. too many
 *                          sections, invalid memory addresses that we are
 *                          unable to read, etc
 *  * RTAS_OUT_DUMP_ALREADY_REGISTERED: Dump already registered
 *  * RTAS_OUT_HW_ERROR: Misc issue such as memory access failures
 */
uint32_t do_fadump_register(SpaprMachineState *spapr, target_ulong args)
{
    FadumpSectionHeader header;
    FadumpSection regions[FADUMP_MAX_SECTIONS] = {0};
    target_ulong fdm_addr = rtas_ld(args, 1);
    target_ulong fdm_size = rtas_ld(args, 2);
    AddressSpace *default_as = &address_space_memory;
    MemTxResult io_result;
    MemTxAttrs attrs;
    uint64_t next_section_addr;
    uint16_t dump_num_sections;

    /* Mark the memory transaction as privileged memory access */
    attrs.user = 0;
    attrs.memory = 1;

    if (spapr->fadump_registered) {
        /* FADump already registered */
        return RTAS_OUT_DUMP_ALREADY_REGISTERED;
    }

    if (spapr->fadump_dump_active) {
        return RTAS_OUT_DUMP_ACTIVE;
    }

    if (fdm_size < sizeof(FadumpSectionHeader)) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "FADump: Header size is invalid: " TARGET_FMT_lu "\n", fdm_size);
        return RTAS_OUT_PARAM_ERROR;
    }

    /* Ensure fdm_addr points to a valid RMR-memory/RMA-memory buffer */
    if ((fdm_addr <= 0) || ((fdm_addr + fdm_size) > spapr->rma_size)) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "FADump: Invalid fdm address: " TARGET_FMT_lu "\n", fdm_addr);
        return RTAS_OUT_PARAM_ERROR;
    }

    /* Try to read the passed fadump header */
    io_result = address_space_read(default_as, fdm_addr, attrs,
            &header, sizeof(header));
    if (io_result != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "FADump: Unable to read fdm: " TARGET_FMT_lu "\n", fdm_addr);

        return RTAS_OUT_HW_ERROR;
    }

    /* Verify that we understand the fadump header version */
    if (header.dump_format_version != cpu_to_be32(FADUMP_VERSION)) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "FADump: Unknown fadump header version: 0x%x\n",
            header.dump_format_version);
        return RTAS_OUT_PARAM_ERROR;
    }

    /* Reset dump status flags */
    header.dump_status_flag = 0;

    dump_num_sections = be16_to_cpu(header.dump_num_sections);

    if (dump_num_sections > FADUMP_MAX_SECTIONS) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "FADump: Too many sections: %d sections\n", dump_num_sections);
        return RTAS_OUT_PARAM_ERROR;
    }

    next_section_addr =
        fdm_addr +
        be32_to_cpu(header.offset_first_dump_section);

    for (int i = 0; i < dump_num_sections; ++i) {
        /* Read the fadump section from memory */
        io_result = address_space_read(default_as, next_section_addr, attrs,
                &regions[i], sizeof(regions[i]));
        if (io_result != MEMTX_OK) {
            qemu_log_mask(LOG_UNIMP,
                "FADump: Unable to read fadump %dth section\n", i);
            return RTAS_OUT_PARAM_ERROR;
        }

        next_section_addr += sizeof(regions[i]);
    }

    spapr->fadump_registered = true;
    spapr->fadump_dump_active = false;

    /* Store the registered fadump memory struct */
    spapr->registered_fdm.header = header;
    for (int i = 0; i < dump_num_sections; ++i) {
        spapr->registered_fdm.rgn[i] = regions[i];
    }

    return RTAS_OUT_SUCCESS;
}

/* Preserve the memory locations registered for fadump */
static bool fadump_preserve_mem(void)
{
    /*
     * TODO: Implement preserving memory regions requested during fadump
     * registration
     */
    return false;
}

/*
 * Trigger a fadump boot, ie. next boot will be a crashkernel/fadump boot
 * with fadump dump active.
 *
 * This is triggered by ibm,os-term RTAS call, if fadump was registered.
 *
 * It preserves the memory and sets 'FADUMP_STATUS_DUMP_TRIGGERED' as
 * fadump status, which can be used later to add the "ibm,kernel-dump"
 * device tree node as presence of 'FADUMP_STATUS_DUMP_TRIGGERED' signifies
 * next boot as fadump boot in our case
 */
void trigger_fadump_boot(SpaprMachineState *spapr, target_ulong spapr_retcode)
{
    FadumpSectionHeader *header = &spapr->registered_fdm.header;

    pause_all_vcpus();

    /* Preserve the memory locations registered for fadump */
    if (!fadump_preserve_mem()) {
        /* Failed to preserve the registered memory regions */
        rtas_st(spapr_retcode, 0, RTAS_OUT_HW_ERROR);

        /* Cause a reboot */
        qemu_system_guest_panicked(NULL);
        return;
    }

    /*
     * Mark next boot as fadump boot
     *
     * Note: These is some bit of assumption involved here, as PAPR doesn't
     * specify any use of the dump status flags, nor does the kernel use it
     *
     * But from description in Table 136 in PAPR v2.13, it looks like:
     *   FADUMP_STATUS_DUMP_TRIGGERED
     *      = Dump was triggered by the previous system boot (PAPR says)
     *      = Next boot will be a fadump boot (Assumed)
     *
     *   FADUMP_STATUS_DUMP_PERFORMED
     *      = Dump performed (Set to 0 by caller of the
     *        ibm,configure-kernel-dump call) (PAPR says)
     *      = Firmware has performed the copying/dump of requested regions
     *        (Assumed)
     *      = Dump is active for the next boot (Assumed)
     */
    header->dump_status_flag = cpu_to_be16(
            FADUMP_STATUS_DUMP_TRIGGERED |  /* Next boot will be fadump boot */
            FADUMP_STATUS_DUMP_PERFORMED    /* Dump is active */
    );

    /* Reset fadump_registered for next boot */
    spapr->fadump_registered = false;
    spapr->fadump_dump_active = true;

    /*
     * Then do a guest reset
     *
     * Requirement:
     * GUEST_RESET is expected to NOT clear the memory, as is the case when
     * this is merged
     */
    qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);

    rtas_st(spapr_retcode, 0, RTAS_OUT_SUCCESS);
}
