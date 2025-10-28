/*
 * Firmware Assisted Dump in PSeries
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/ppc/spapr.h"
#include "qemu/units.h"
#include "system/cpus.h"
#include "system/hw_accel.h"
#include <math.h>

/*
 * Copy the ascii values for first 8 characters from a string into u64
 * variable at their respective indexes.
 * e.g.
 *  The string "FADMPINF" will be converted into 0x4641444d50494e46
 */
static uint64_t fadump_str_to_u64(const char *str)
{
    uint64_t val = 0;
    int i;

    for (i = 0; i < sizeof(val); i++) {
        val = (*str) ? (val << 8) | *str++ : val << 8;
    }
    return val;
}

/**
 * Get the identifier id for register entries of GPRs
 *
 * It gives the same id as 'fadump_str_to_u64' when the complete string id
 * of the GPR is given, ie.
 *
 *   fadump_str_to_u64("GPR05") == fadump_gpr_id_to_u64(5);
 *   fadump_str_to_u64("GPR12") == fadump_gpr_id_to_u64(12);
 *
 * And so on. Hence this can be implemented by creating a dynamic
 * string for each GPR, such as "GPR00", "GPR01", ... "GPR31"
 * Instead of allocating a string, an observation from the math of
 * 'fadump_str_to_u64' or from PAPR tells us that there's a pattern
 * in the identifier IDs, such that the first 4 bytes are affected only by
 * whether it is GPR0*, GPR1*, GPR2*, GPR3*.
 * Upper half of 5th byte is always 0x3. Lower half (nibble) of 5th byte
 * is the tens digit of the GPR id, ie. GPR ID / 10.
 * Upper half of 6th byte is always 0x3. Lower half (nibble) of 5th byte
 * is the ones digit of the GPR id, ie. GPR ID % 10
 *
 * For example, for GPR 29, the 5th and 6th byte will be 0x32 and 0x39
 */
static uint64_t fadump_gpr_id_to_u64(uint32_t gpr_id)
{
    uint64_t val = 0;

    /* Valid range of GPR id is only GPR0 to GPR31 */
    assert(gpr_id < 32);

    /* Below calculations set the 0th to 5th byte */
    if (gpr_id <= 9) {
        val = fadump_str_to_u64("GPR0");
    } else if (gpr_id <= 19) {
        val = fadump_str_to_u64("GPR1");
    } else if (gpr_id <= 29) {
        val = fadump_str_to_u64("GPR2");
    } else {
        val = fadump_str_to_u64("GPR3");
    }

    /* Set the 6th byte */
    val |= 0x30000000;
    val |= ((gpr_id % 10) << 24);

    return val;
}

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

/*
 * Copy the source region of given fadump section, to the destination
 * address mentioned in the region
 *
 * Also set the region's error flag, if the copy fails due to non-existent
 * address (MEMTX_DECODE_ERROR) or permission issues (MEMTX_ACCESS_ERROR)
 *
 * Returns true if successful copy
 *
 * Returns false in case of any other error, being treated as hardware
 * error for fadump purposes
 */
static bool do_preserve_region(FadumpSection *region)
{
    AddressSpace *default_as = &address_space_memory;
    MemTxResult io_result;
    MemTxAttrs attrs;
    uint64_t src_addr, src_len, dest_addr;
    uint64_t num_chunks;
    g_autofree void *copy_buffer = NULL;

    src_addr  = be64_to_cpu(region->source_address);
    src_len   = be64_to_cpu(region->source_len);
    dest_addr = be64_to_cpu(region->destination_address);

    /* Mark the memory transaction as privileged memory access */
    attrs.user = 0;
    attrs.memory = 1;

    /*
     * Optimisation: Skip copy if source and destination are same
     * (eg. param area)
     */
    if (src_addr == dest_addr) {
        region->bytes_dumped = cpu_to_be64(src_len);
        return true;
    }

#define FADUMP_CHUNK_SIZE  ((size_t)(32 * MiB))
    copy_buffer = g_try_malloc(FADUMP_CHUNK_SIZE);
    if (copy_buffer == NULL) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "FADump: Failed allocating memory (size: %zu) for copying"
            " reserved memory regions\n", FADUMP_CHUNK_SIZE);
        return false;
    }

    num_chunks = ceil((src_len * 1.0f) / FADUMP_CHUNK_SIZE);
    for (uint64_t chunk_id = 0; chunk_id < num_chunks; ++chunk_id) {
        /* Take minimum of bytes left to copy, and chunk size */
        uint64_t copy_len = MIN(
                        src_len - (chunk_id * FADUMP_CHUNK_SIZE),
                        FADUMP_CHUNK_SIZE
                    );

        /* Copy the source region to destination */
        io_result = address_space_read(default_as, src_addr, attrs,
                copy_buffer, copy_len);
        if ((io_result & MEMTX_DECODE_ERROR) ||
            (io_result & MEMTX_ACCESS_ERROR)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                "FADump: Failed to decode/access address in section: %d\n",
                region->source_data_type);

            /*
             * Invalid source address is not an hardware error, instead
             * wrong parameter from the kernel.
             * Return true to let caller know to continue reading other
             * sections
             */
            region->error_flags = FADUMP_ERROR_INVALID_SOURCE_ADDR;
            region->bytes_dumped = 0;
            return true;
        } else if (io_result != MEMTX_OK) {
            qemu_log_mask(LOG_GUEST_ERROR,
                "FADump: Failed to read source region in section: %d\n",
                region->source_data_type);

            return false;
        }

        io_result = address_space_write(default_as, dest_addr, attrs,
                copy_buffer, copy_len);
        if ((io_result & MEMTX_DECODE_ERROR) ||
            (io_result & MEMTX_ACCESS_ERROR)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                "FADump: Failed to decode/access address in section: %d\n",
                region->source_data_type);

            /*
             * Invalid destination address is not an hardware error,
             * instead wrong parameter from the kernel.
             * Return true to let caller know to continue reading other
             * sections
             */
            region->error_flags = FADUMP_ERROR_INVALID_DEST_ADDR;
            region->bytes_dumped = 0;
            return true;
        } else if (io_result != MEMTX_OK) {
            qemu_log_mask(LOG_GUEST_ERROR,
                "FADump: Failed to write destination in section: %d\n",
                region->source_data_type);

            return false;
        }

        src_addr += FADUMP_CHUNK_SIZE;
        dest_addr += FADUMP_CHUNK_SIZE;
    }
#undef FADUMP_CHUNK_SIZE

    /*
     * Considering address_space_write would have copied the
     * complete region
     */
    region->bytes_dumped = cpu_to_be64(src_len);
    return true;
}

/*
 * Populate the passed CPUs register entries, in the buffer starting at
 * the argument 'curr_reg_entry'
 *
 * The register entries is an array of pair of register id and register
 * value, as described in Table 591/592 in section "H.1 Register Save Area"
 * in PAPR v2.13
 *
 * Returns pointer just past this CPU's register entries, which can be used
 * as the start address for next CPU's register entries
 */
static FadumpRegEntry *populate_cpu_reg_entries(CPUState *cpu,
        FadumpRegEntry *curr_reg_entry)
{
    CPUPPCState *env;
    PowerPCCPU *ppc_cpu;
    uint32_t num_regs_per_cpu = 0;

    ppc_cpu = POWERPC_CPU(cpu);
    env = cpu_env(cpu);
    num_regs_per_cpu = 0;

    /*
     * CPUSTRT and CPUEND register entries follow this format:
     *
     * 8 Bytes Reg ID (BE) | 4 Bytes (0x0) | 4 Bytes Logical CPU ID (BE)
     */
    curr_reg_entry->reg_id =
        cpu_to_be64(fadump_str_to_u64("CPUSTRT"));
    curr_reg_entry->reg_value = cpu_to_be64(
            ppc_cpu->vcpu_id & FADUMP_CPU_ID_MASK);
    ++curr_reg_entry;

#define REG_ENTRY(id, val)                             \
    do {                                               \
        curr_reg_entry->reg_id =                       \
            cpu_to_be64(fadump_str_to_u64(#id));       \
        curr_reg_entry->reg_value = cpu_to_be64(val);  \
        ++curr_reg_entry;                              \
        ++num_regs_per_cpu;                            \
    } while (0)

    REG_ENTRY(ACOP, env->spr[SPR_ACOP]);
    REG_ENTRY(AMR, env->spr[SPR_AMR]);
    REG_ENTRY(BESCR, env->spr[SPR_BESCR]);
    REG_ENTRY(CFAR, env->spr[SPR_CFAR]);
    REG_ENTRY(CIABR, env->spr[SPR_CIABR]);

    /* Save the condition register */
    REG_ENTRY(CR, ppc_get_cr(env));

    REG_ENTRY(CTR, env->spr[SPR_CTR]);
    REG_ENTRY(CTRL, env->spr[SPR_CTRL]);
    REG_ENTRY(DABR, env->spr[SPR_DABR]);
    REG_ENTRY(DABRX, env->spr[SPR_DABRX]);
    REG_ENTRY(DAR, env->spr[SPR_DAR]);
    REG_ENTRY(DAWR0, env->spr[SPR_DAWR0]);
    REG_ENTRY(DAWR1, env->spr[SPR_DAWR1]);
    REG_ENTRY(DAWRX0, env->spr[SPR_DAWRX0]);
    REG_ENTRY(DAWRX1, env->spr[SPR_DAWRX1]);
    REG_ENTRY(DPDES, env->spr[SPR_DPDES]);
    REG_ENTRY(DSCR, env->spr[SPR_DSCR]);
    REG_ENTRY(DSISR, env->spr[SPR_DSISR]);
    REG_ENTRY(EBBHR, env->spr[SPR_EBBHR]);
    REG_ENTRY(EBBRR, env->spr[SPR_EBBRR]);

    REG_ENTRY(FPSCR, env->fpscr);
    REG_ENTRY(FSCR, env->spr[SPR_FSCR]);

    /* Save the GPRs */
    for (int gpr_id = 0; gpr_id < 32; ++gpr_id) {
        curr_reg_entry->reg_id =
            cpu_to_be64(fadump_gpr_id_to_u64(gpr_id));
        curr_reg_entry->reg_value =
            cpu_to_be64(env->gpr[gpr_id]);
        ++curr_reg_entry;
        ++num_regs_per_cpu;
    }

    REG_ENTRY(IAMR, env->spr[SPR_IAMR]);
    REG_ENTRY(IC, env->spr[SPR_IC]);
    REG_ENTRY(LR, env->spr[SPR_LR]);

    REG_ENTRY(MSR, env->msr);
    REG_ENTRY(NIA, env->nip);   /* NIA */
    REG_ENTRY(PIR, env->spr[SPR_PIR]);
    REG_ENTRY(PSPB, env->spr[SPR_PSPB]);
    REG_ENTRY(PVR, env->spr[SPR_PVR]);
    REG_ENTRY(RPR, env->spr[SPR_RPR]);
    REG_ENTRY(SPURR, env->spr[SPR_SPURR]);
    REG_ENTRY(SRR0, env->spr[SPR_SRR0]);
    REG_ENTRY(SRR1, env->spr[SPR_SRR1]);
    REG_ENTRY(TAR, env->spr[SPR_TAR]);
    REG_ENTRY(TEXASR, env->spr[SPR_TEXASR]);
    REG_ENTRY(TFHAR, env->spr[SPR_TFHAR]);
    REG_ENTRY(TFIAR, env->spr[SPR_TFIAR]);
    REG_ENTRY(TIR, env->spr[SPR_TIR]);
    REG_ENTRY(UAMOR, env->spr[SPR_UAMOR]);
    REG_ENTRY(VRSAVE, env->spr[SPR_VRSAVE]);
    REG_ENTRY(VSCR, env->vscr);
    REG_ENTRY(VTB, env->spr[SPR_VTB]);
    REG_ENTRY(WORT, env->spr[SPR_WORT]);
    REG_ENTRY(XER, env->spr[SPR_XER]);

    /*
     * Ignoring transaction checkpoint and few other registers
     * mentioned in PAPR as not supported in QEMU
     */
#undef REG_ENTRY

    /* End the registers for this CPU with "CPUEND" reg entry */
    curr_reg_entry->reg_id =
        cpu_to_be64(fadump_str_to_u64("CPUEND"));
    curr_reg_entry->reg_value = cpu_to_be64(
            ppc_cpu->vcpu_id & FADUMP_CPU_ID_MASK);

    /*
     * Ensure number of register entries saved matches the expected
     * 'FADUMP_PER_CPU_REG_ENTRIES' count
     *
     * This will help catch an error if in future a new register entry
     * is added/removed while not modifying FADUMP_PER_CPU_REG_ENTRIES
     */
    assert(FADUMP_PER_CPU_REG_ENTRIES == num_regs_per_cpu + 2 /*CPUSTRT+CPUEND*/);

    ++curr_reg_entry;

    return curr_reg_entry;
}

/*
 * Populate the "Register Save Area"/CPU State as mentioned in section "H.1
 * Register Save Area" in PAPR v2.13
 *
 * It allocates the buffer for this region, then populates the register
 * entries
 *
 * Returns the pointer to the buffer (which should be deallocated by the
 * callers), and sets the size of this buffer in the argument
 * 'cpu_state_len'
 */
static void *get_cpu_state_data(uint64_t *cpu_state_len)
{
    FadumpRegSaveAreaHeader reg_save_hdr;
    g_autofree FadumpRegEntry *reg_entries = NULL;
    FadumpRegEntry *curr_reg_entry;
    CPUState *cpu;

    uint32_t num_reg_entries;
    uint32_t reg_entries_size;
    uint32_t num_cpus = 0;

    void *cpu_state_buffer = NULL;
    uint64_t offset = 0;

    CPU_FOREACH(cpu) {
        ++num_cpus;
    }

    reg_save_hdr.version = cpu_to_be32(0);
    reg_save_hdr.magic_number =
        cpu_to_be64(fadump_str_to_u64("REGSAVE"));

    /* Reg save area header is immediately followed by num cpus */
    reg_save_hdr.num_cpu_offset =
        cpu_to_be32(sizeof(FadumpRegSaveAreaHeader));

    num_reg_entries = num_cpus * FADUMP_PER_CPU_REG_ENTRIES;
    reg_entries_size = num_reg_entries * sizeof(FadumpRegEntry);

    reg_entries = g_new(FadumpRegEntry, num_reg_entries);

    /* Pointer to current CPU's registers */
    curr_reg_entry = reg_entries;

    /* Populate register entries for all CPUs */
    CPU_FOREACH(cpu) {
        cpu_synchronize_state(cpu);
        curr_reg_entry = populate_cpu_reg_entries(cpu, curr_reg_entry);
    }

    *cpu_state_len = 0;
    *cpu_state_len += sizeof(reg_save_hdr);     /* reg save header */
    *cpu_state_len += 0xc;                      /* padding as in PAPR */
    *cpu_state_len += sizeof(num_cpus);         /* num_cpus */
    *cpu_state_len += reg_entries_size;         /* reg entries */

    cpu_state_buffer = g_malloc(*cpu_state_len);

    memcpy(cpu_state_buffer + offset,
            &reg_save_hdr, sizeof(reg_save_hdr));
    offset += sizeof(reg_save_hdr);

    /* Write num_cpus */
    num_cpus = cpu_to_be32(num_cpus);
    memcpy(cpu_state_buffer + offset, &num_cpus, sizeof(num_cpus));
    offset += sizeof(num_cpus);

    /* Write the register entries */
    memcpy(cpu_state_buffer + offset, reg_entries, reg_entries_size);
    offset += reg_entries_size;

    return cpu_state_buffer;
}

/*
 * Save the CPU State Data (aka "Register Save Area") in given region
 *
 * Region argument is expected to be of CPU_STATE_DATA type
 *
 * Returns false only in case of Hardware Error, such as failure to
 * read/write a valid address.
 *
 * Otherwise, even in case of unsuccessful copy of CPU state data for reasons
 * such as invalid destination address or non-fatal error errors likely
 * caused due to invalid parameters, return true and set region->error_flags
 */
static bool do_populate_cpu_state(FadumpSection *region)
{
    uint64_t dest_addr = be64_to_cpu(region->destination_address);
    uint64_t cpu_state_len = 0;
    g_autofree void *cpu_state_buffer = NULL;
    AddressSpace *default_as = &address_space_memory;
    MemTxResult io_result;
    MemTxAttrs attrs;

    assert(region->source_data_type == cpu_to_be16(FADUMP_CPU_STATE_DATA));

    /* Mark the memory transaction as privileged memory access */
    attrs.user = 0;
    attrs.memory = 1;

    cpu_state_buffer  = get_cpu_state_data(&cpu_state_len);

    io_result = address_space_write(default_as, dest_addr, attrs,
            cpu_state_buffer, cpu_state_len);
    if ((io_result & MEMTX_DECODE_ERROR) ||
        (io_result & MEMTX_ACCESS_ERROR)) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "FADump: Failed to decode/access address in CPU State Region's"
            " destination address: 0x%016" PRIx64 "\n", dest_addr);

        /*
         * Invalid source address is not an hardware error, instead
         * wrong parameter from the kernel.
         * Return true to let caller know to continue reading other
         * sections
         */
        region->error_flags = FADUMP_ERROR_INVALID_SOURCE_ADDR;
        region->bytes_dumped = 0;
        return true;
    } else if (io_result != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "FADump: Failed to write CPU state region.\n");

        return false;
    }

    /*
     * Set bytes_dumped in cpu state region, so kernel knows platform have
     * exported it
     */
    region->bytes_dumped = cpu_to_be64(cpu_state_len);

    if (region->source_len != region->bytes_dumped) {
        /*
         * Log the error, but don't fail the dump collection here, let
         * kernel handle the mismatch
         */
        qemu_log_mask(LOG_GUEST_ERROR,
                "FADump: Mismatch in CPU State region's length exported:"
                " Kernel expected: 0x%" PRIx64 " bytes,"
                " QEMU exported: 0x%" PRIx64 " bytes\n",
                be64_to_cpu(region->source_len),
                be64_to_cpu(region->bytes_dumped));
    }

    return true;
}

/*
 * Preserve the memory locations registered for fadump
 *
 * Returns false only in case of RTAS_OUT_HW_ERROR, otherwise true
 */
static bool fadump_preserve_mem(SpaprMachineState *spapr)
{
    FadumpMemStruct *fdm = &spapr->registered_fdm;
    uint16_t dump_num_sections, data_type;

    assert(spapr->fadump_registered);

    /*
     * Handle all sections
     *
     * CPU State Data and HPTE regions are handled in their own cases
     *
     * RMR regions and any custom OS reserved regions such as parameter
     * save area, are handled by simply copying the source region to
     * destination address
     */
    dump_num_sections = be16_to_cpu(fdm->header.dump_num_sections);
    for (int i = 0; i < dump_num_sections; ++i) {
        data_type = be16_to_cpu(fdm->rgn[i].source_data_type);

        /* Reset error_flags & bytes_dumped for now */
        fdm->rgn[i].error_flags = 0;
        fdm->rgn[i].bytes_dumped = 0;

        /* If kernel did not request for the memory region, then skip it */
        if (be32_to_cpu(fdm->rgn[i].request_flag) != FADUMP_REQUEST_FLAG) {
            qemu_log_mask(LOG_UNIMP,
                "FADump: Skipping copying region as not requested\n");
            continue;
        }

        switch (data_type) {
        case FADUMP_CPU_STATE_DATA:
            if (!do_populate_cpu_state(&fdm->rgn[i])) {
                qemu_log_mask(LOG_GUEST_ERROR,
                    "FADump: Failed to store CPU State Data");
                fdm->header.dump_status_flag |=
                    cpu_to_be16(FADUMP_STATUS_DUMP_ERROR);

                return false;
            }

            break;
        case FADUMP_HPTE_REGION:
            /* TODO: Add hpte state data */
            break;
        case FADUMP_REAL_MODE_REGION:
        case FADUMP_PARAM_AREA:
            /* Copy the memory region from region's source to its destination */
            if (!do_preserve_region(&fdm->rgn[i])) {
                qemu_log_mask(LOG_GUEST_ERROR,
                    "FADump: Failed to preserve dump section: %d\n",
                    be16_to_cpu(fdm->rgn[i].source_data_type));
                fdm->header.dump_status_flag |=
                    cpu_to_be16(FADUMP_STATUS_DUMP_ERROR);
            }

            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                "FADump: Skipping unknown source data type: %d\n", data_type);

            fdm->rgn[i].error_flags =
                cpu_to_be16(FADUMP_ERROR_INVALID_DATA_TYPE);
        }
    }

    return true;
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
    if (!fadump_preserve_mem(spapr)) {
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
