/*
 * Firmware Assisted Dump in PSeries
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/ppc/spapr.h"

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
