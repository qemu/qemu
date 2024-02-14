/*
 * CXL Utility library for components
 *
 * Copyright(C) 2020 Intel Corporation.
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/pci/pci.h"
#include "hw/cxl/cxl.h"

/* CXL r3.1 Section 8.2.4.20.1 CXL HDM Decoder Capability Register */
int cxl_decoder_count_enc(int count)
{
    switch (count) {
    case 1: return 0x0;
    case 2: return 0x1;
    case 4: return 0x2;
    case 6: return 0x3;
    case 8: return 0x4;
    case 10: return 0x5;
    /* Switches and Host Bridges may have more than 10 decoders */
    case 12: return 0x6;
    case 14: return 0x7;
    case 16: return 0x8;
    case 20: return 0x9;
    case 24: return 0xa;
    case 28: return 0xb;
    case 32: return 0xc;
    }
    return 0;
}

int cxl_decoder_count_dec(int enc_cnt)
{
    switch (enc_cnt) {
    case 0x0: return 1;
    case 0x1: return 2;
    case 0x2: return 4;
    case 0x3: return 6;
    case 0x4: return 8;
    case 0x5: return 10;
    /* Switches and Host Bridges may have more than 10 decoders */
    case 0x6: return 12;
    case 0x7: return 14;
    case 0x8: return 16;
    case 0x9: return 20;
    case 0xa: return 24;
    case 0xb: return 28;
    case 0xc: return 32;
    }
    return 0;
}

hwaddr cxl_decode_ig(int ig)
{
    return 1ULL << (ig + 8);
}

static uint64_t cxl_cache_mem_read_reg(void *opaque, hwaddr offset,
                                       unsigned size)
{
    CXLComponentState *cxl_cstate = opaque;
    ComponentRegisters *cregs = &cxl_cstate->crb;

    switch (size) {
    case 4:
        if (cregs->special_ops && cregs->special_ops->read) {
            return cregs->special_ops->read(cxl_cstate, offset, 4);
        } else {
            QEMU_BUILD_BUG_ON(sizeof(*cregs->cache_mem_registers) != 4);
            return cregs->cache_mem_registers[offset / 4];
        }
    case 8:
        qemu_log_mask(LOG_UNIMP,
                      "CXL 8 byte cache mem registers not implemented\n");
        return 0;
    default:
        /*
         * In line with specification limitaions on access sizes, this
         * routine is not called with other sizes.
         */
        g_assert_not_reached();
    }
}

static void dumb_hdm_handler(CXLComponentState *cxl_cstate, hwaddr offset,
                             uint32_t value)
{
    ComponentRegisters *cregs = &cxl_cstate->crb;
    uint32_t *cache_mem = cregs->cache_mem_registers;
    bool should_commit = false;
    bool should_uncommit = false;

    switch (offset) {
    case A_CXL_HDM_DECODER0_CTRL:
    case A_CXL_HDM_DECODER1_CTRL:
    case A_CXL_HDM_DECODER2_CTRL:
    case A_CXL_HDM_DECODER3_CTRL:
        should_commit = FIELD_EX32(value, CXL_HDM_DECODER0_CTRL, COMMIT);
        should_uncommit = !should_commit;
        break;
    default:
        break;
    }

    if (should_commit) {
        value = FIELD_DP32(value, CXL_HDM_DECODER0_CTRL, ERR, 0);
        value = FIELD_DP32(value, CXL_HDM_DECODER0_CTRL, COMMITTED, 1);
    } else if (should_uncommit) {
        value = FIELD_DP32(value, CXL_HDM_DECODER0_CTRL, ERR, 0);
        value = FIELD_DP32(value, CXL_HDM_DECODER0_CTRL, COMMITTED, 0);
    }
    stl_le_p((uint8_t *)cache_mem + offset, value);
}

static void cxl_cache_mem_write_reg(void *opaque, hwaddr offset, uint64_t value,
                                    unsigned size)
{
    CXLComponentState *cxl_cstate = opaque;
    ComponentRegisters *cregs = &cxl_cstate->crb;
    uint32_t mask;

    switch (size) {
    case 4: {
        QEMU_BUILD_BUG_ON(sizeof(*cregs->cache_mem_regs_write_mask) != 4);
        QEMU_BUILD_BUG_ON(sizeof(*cregs->cache_mem_registers) != 4);
        mask = cregs->cache_mem_regs_write_mask[offset / 4];
        value &= mask;
        /* RO bits should remain constant. Done by reading existing value */
        value |= ~mask & cregs->cache_mem_registers[offset / 4];
        if (cregs->special_ops && cregs->special_ops->write) {
            cregs->special_ops->write(cxl_cstate, offset, value, size);
            return;
        }

        if (offset >= A_CXL_HDM_DECODER_CAPABILITY &&
            offset <= A_CXL_HDM_DECODER3_TARGET_LIST_HI) {
            dumb_hdm_handler(cxl_cstate, offset, value);
        } else {
            cregs->cache_mem_registers[offset / 4] = value;
        }
        return;
    }
    case 8:
        qemu_log_mask(LOG_UNIMP,
                      "CXL 8 byte cache mem registers not implemented\n");
        return;
    default:
        /*
         * In line with specification limitaions on access sizes, this
         * routine is not called with other sizes.
         */
        g_assert_not_reached();
    }
}

/*
 * CXL r3.1 Section 8.2.3: Component Register Layout and Definition
 *   The access restrictions specified in Section 8.2.2 also apply to CXL 2.0
 *   Component Registers.
 *
 * CXL r3.1 Section 8.2.2: Accessing Component Registers
 *   • A 32 bit register shall be accessed as a 4 Bytes quantity. Partial
 *   reads are not permitted.
 *   • A 64 bit register shall be accessed as a 8 Bytes quantity. Partial
 *   reads are not permitted.
 *
 * As of the spec defined today, only 4 byte registers exist.
 */
static const MemoryRegionOps cache_mem_ops = {
    .read = cxl_cache_mem_read_reg,
    .write = cxl_cache_mem_write_reg,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

void cxl_component_register_block_init(Object *obj,
                                       CXLComponentState *cxl_cstate,
                                       const char *type)
{
    ComponentRegisters *cregs = &cxl_cstate->crb;

    memory_region_init(&cregs->component_registers, obj, type,
                       CXL2_COMPONENT_BLOCK_SIZE);

    /* io registers controls link which we don't care about in QEMU */
    memory_region_init_io(&cregs->io, obj, NULL, NULL, ".io",
                          CXL2_COMPONENT_IO_REGION_SIZE);
    memory_region_init_io(&cregs->cache_mem, obj, &cache_mem_ops, cxl_cstate,
                          ".cache_mem", CXL2_COMPONENT_CM_REGION_SIZE);

    memory_region_add_subregion(&cregs->component_registers, 0, &cregs->io);
    memory_region_add_subregion(&cregs->component_registers,
                                CXL2_COMPONENT_IO_REGION_SIZE,
                                &cregs->cache_mem);
}

static void ras_init_common(uint32_t *reg_state, uint32_t *write_msk)
{
    /*
     * Error status is RW1C but given bits are not yet set, it can
     * be handled as RO.
     */
    stl_le_p(reg_state + R_CXL_RAS_UNC_ERR_STATUS, 0);
    stl_le_p(write_msk + R_CXL_RAS_UNC_ERR_STATUS, 0x1cfff);
    /* Bits 12-13 and 17-31 reserved in CXL 2.0 */
    stl_le_p(reg_state + R_CXL_RAS_UNC_ERR_MASK, 0x1cfff);
    stl_le_p(write_msk + R_CXL_RAS_UNC_ERR_MASK, 0x1cfff);
    stl_le_p(reg_state + R_CXL_RAS_UNC_ERR_SEVERITY, 0x1cfff);
    stl_le_p(write_msk + R_CXL_RAS_UNC_ERR_SEVERITY, 0x1cfff);
    stl_le_p(reg_state + R_CXL_RAS_COR_ERR_STATUS, 0);
    stl_le_p(write_msk + R_CXL_RAS_COR_ERR_STATUS, 0x7f);
    stl_le_p(reg_state + R_CXL_RAS_COR_ERR_MASK, 0x7f);
    stl_le_p(write_msk + R_CXL_RAS_COR_ERR_MASK, 0x7f);
    /* CXL switches and devices must set */
    stl_le_p(reg_state + R_CXL_RAS_ERR_CAP_CTRL, 0x200);
}

static void hdm_init_common(uint32_t *reg_state, uint32_t *write_msk,
                            enum reg_type type)
{
    int decoder_count = CXL_HDM_DECODER_COUNT;
    int hdm_inc = R_CXL_HDM_DECODER1_BASE_LO - R_CXL_HDM_DECODER0_BASE_LO;
    int i;

    ARRAY_FIELD_DP32(reg_state, CXL_HDM_DECODER_CAPABILITY, DECODER_COUNT,
                     cxl_decoder_count_enc(decoder_count));
    ARRAY_FIELD_DP32(reg_state, CXL_HDM_DECODER_CAPABILITY, TARGET_COUNT, 1);
    ARRAY_FIELD_DP32(reg_state, CXL_HDM_DECODER_CAPABILITY, INTERLEAVE_256B, 1);
    ARRAY_FIELD_DP32(reg_state, CXL_HDM_DECODER_CAPABILITY, INTERLEAVE_4K, 1);
    ARRAY_FIELD_DP32(reg_state, CXL_HDM_DECODER_CAPABILITY,
                     POISON_ON_ERR_CAP, 0);
    ARRAY_FIELD_DP32(reg_state, CXL_HDM_DECODER_CAPABILITY, 3_6_12_WAY, 0);
    ARRAY_FIELD_DP32(reg_state, CXL_HDM_DECODER_CAPABILITY, 16_WAY, 0);
    ARRAY_FIELD_DP32(reg_state, CXL_HDM_DECODER_CAPABILITY, UIO, 0);
    ARRAY_FIELD_DP32(reg_state, CXL_HDM_DECODER_CAPABILITY,
                     UIO_DECODER_COUNT, 0);
    ARRAY_FIELD_DP32(reg_state, CXL_HDM_DECODER_CAPABILITY, MEMDATA_NXM_CAP, 0);
    ARRAY_FIELD_DP32(reg_state, CXL_HDM_DECODER_CAPABILITY,
                     SUPPORTED_COHERENCY_MODEL, 0); /* Unknown */
    ARRAY_FIELD_DP32(reg_state, CXL_HDM_DECODER_GLOBAL_CONTROL,
                     HDM_DECODER_ENABLE, 0);
    write_msk[R_CXL_HDM_DECODER_GLOBAL_CONTROL] = 0x3;
    for (i = 0; i < decoder_count; i++) {
        write_msk[R_CXL_HDM_DECODER0_BASE_LO + i * hdm_inc] = 0xf0000000;
        write_msk[R_CXL_HDM_DECODER0_BASE_HI + i * hdm_inc] = 0xffffffff;
        write_msk[R_CXL_HDM_DECODER0_SIZE_LO + i * hdm_inc] = 0xf0000000;
        write_msk[R_CXL_HDM_DECODER0_SIZE_HI + i * hdm_inc] = 0xffffffff;
        write_msk[R_CXL_HDM_DECODER0_CTRL + i * hdm_inc] = 0x13ff;
        if (type == CXL2_DEVICE ||
            type == CXL2_TYPE3_DEVICE ||
            type == CXL2_LOGICAL_DEVICE) {
            write_msk[R_CXL_HDM_DECODER0_TARGET_LIST_LO + i * hdm_inc] =
                0xf0000000;
        } else {
            write_msk[R_CXL_HDM_DECODER0_TARGET_LIST_LO + i * hdm_inc] =
                0xffffffff;
        }
        write_msk[R_CXL_HDM_DECODER0_TARGET_LIST_HI + i * hdm_inc] = 0xffffffff;
    }
}

void cxl_component_register_init_common(uint32_t *reg_state,
                                        uint32_t *write_msk,
                                        enum reg_type type)
{
    int caps = 0;

    /*
     * In CXL 2.0 the capabilities required for each CXL component are such
     * that, with the ordering chosen here, a single number can be used to
     * define which capabilities should be provided.
     */
    switch (type) {
    case CXL2_DOWNSTREAM_PORT:
    case CXL2_DEVICE:
        /* RAS, Link */
        caps = 2;
        break;
    case CXL2_UPSTREAM_PORT:
    case CXL2_TYPE3_DEVICE:
    case CXL2_LOGICAL_DEVICE:
        /* + HDM */
        caps = 3;
        break;
    case CXL2_ROOT_PORT:
        /* + Extended Security, + Snoop */
        caps = 5;
        break;
    default:
        abort();
    }

    memset(reg_state, 0, CXL2_COMPONENT_CM_REGION_SIZE);

    /* CXL Capability Header Register */
    ARRAY_FIELD_DP32(reg_state, CXL_CAPABILITY_HEADER, ID, 1);
    ARRAY_FIELD_DP32(reg_state, CXL_CAPABILITY_HEADER, VERSION,
        CXL_CAPABILITY_VERSION);
    ARRAY_FIELD_DP32(reg_state, CXL_CAPABILITY_HEADER, CACHE_MEM_VERSION, 1);
    ARRAY_FIELD_DP32(reg_state, CXL_CAPABILITY_HEADER, ARRAY_SIZE, caps);

#define init_cap_reg(reg, id, version)                                        \
    do {                                                                      \
        int which = R_CXL_##reg##_CAPABILITY_HEADER;                          \
        reg_state[which] = FIELD_DP32(reg_state[which],                       \
                                      CXL_##reg##_CAPABILITY_HEADER, ID, id); \
        reg_state[which] =                                                    \
            FIELD_DP32(reg_state[which], CXL_##reg##_CAPABILITY_HEADER,       \
                       VERSION, version);                                     \
        reg_state[which] =                                                    \
            FIELD_DP32(reg_state[which], CXL_##reg##_CAPABILITY_HEADER, PTR,  \
                       CXL_##reg##_REGISTERS_OFFSET);                         \
    } while (0)

    init_cap_reg(RAS, 2, CXL_RAS_CAPABILITY_VERSION);
    ras_init_common(reg_state, write_msk);

    init_cap_reg(LINK, 4, CXL_LINK_CAPABILITY_VERSION);

    if (caps < 3) {
        return;
    }

    init_cap_reg(HDM, 5, CXL_HDM_CAPABILITY_VERSION);
    hdm_init_common(reg_state, write_msk, type);

    if (caps < 5) {
        return;
    }

    init_cap_reg(EXTSEC, 6, CXL_EXTSEC_CAP_VERSION);
    init_cap_reg(SNOOP, 8, CXL_SNOOP_CAP_VERSION);

#undef init_cap_reg
}

/*
 * Helper to creates a DVSEC header for a CXL entity. The caller is responsible
 * for tracking the valid offset.
 *
 * This function will build the DVSEC header on behalf of the caller and then
 * copy in the remaining data for the vendor specific bits.
 * It will also set up appropriate write masks.
 */
void cxl_component_create_dvsec(CXLComponentState *cxl,
                                enum reg_type cxl_dev_type, uint16_t length,
                                uint16_t type, uint8_t rev, uint8_t *body)
{
    PCIDevice *pdev = cxl->pdev;
    uint16_t offset = cxl->dvsec_offset;
    uint8_t *wmask = pdev->wmask;

    assert(offset >= PCI_CFG_SPACE_SIZE &&
           ((offset + length) < PCI_CFG_SPACE_EXP_SIZE));
    assert((length & 0xf000) == 0);
    assert((rev & ~0xf) == 0);

    /* Create the DVSEC in the MCFG space */
    pcie_add_capability(pdev, PCI_EXT_CAP_ID_DVSEC, 1, offset, length);
    pci_set_long(pdev->config + offset + PCIE_DVSEC_HEADER1_OFFSET,
                 (length << 20) | (rev << 16) | CXL_VENDOR_ID);
    pci_set_word(pdev->config + offset + PCIE_DVSEC_ID_OFFSET, type);
    memcpy(pdev->config + offset + sizeof(DVSECHeader),
           body + sizeof(DVSECHeader),
           length - sizeof(DVSECHeader));

    /* Configure write masks */
    switch (type) {
    case PCIE_CXL_DEVICE_DVSEC:
        /* Cntrl RW Lock - so needs explicit blocking when lock is set */
        wmask[offset + offsetof(CXLDVSECDevice, ctrl)] = 0xFD;
        wmask[offset + offsetof(CXLDVSECDevice, ctrl) + 1] = 0x4F;
        /* Status is RW1CS */
        wmask[offset + offsetof(CXLDVSECDevice, ctrl2)] = 0x0F;
       /* Lock is RW Once */
        wmask[offset + offsetof(CXLDVSECDevice, lock)] = 0x01;
        /* range1/2_base_high/low is RW Lock */
        wmask[offset + offsetof(CXLDVSECDevice, range1_base_hi)] = 0xFF;
        wmask[offset + offsetof(CXLDVSECDevice, range1_base_hi) + 1] = 0xFF;
        wmask[offset + offsetof(CXLDVSECDevice, range1_base_hi) + 2] = 0xFF;
        wmask[offset + offsetof(CXLDVSECDevice, range1_base_hi) + 3] = 0xFF;
        wmask[offset + offsetof(CXLDVSECDevice, range1_base_lo) + 3] = 0xF0;
        wmask[offset + offsetof(CXLDVSECDevice, range2_base_hi)] = 0xFF;
        wmask[offset + offsetof(CXLDVSECDevice, range2_base_hi) + 1] = 0xFF;
        wmask[offset + offsetof(CXLDVSECDevice, range2_base_hi) + 2] = 0xFF;
        wmask[offset + offsetof(CXLDVSECDevice, range2_base_hi) + 3] = 0xFF;
        wmask[offset + offsetof(CXLDVSECDevice, range2_base_lo) + 3] = 0xF0;
        break;
    case NON_CXL_FUNCTION_MAP_DVSEC:
        break; /* Not yet implemented */
    case EXTENSIONS_PORT_DVSEC:
        wmask[offset + offsetof(CXLDVSECPortExt, control)] = 0x0F;
        wmask[offset + offsetof(CXLDVSECPortExt, control) + 1] = 0x40;
        wmask[offset + offsetof(CXLDVSECPortExt, alt_bus_base)] = 0xFF;
        wmask[offset + offsetof(CXLDVSECPortExt, alt_bus_limit)] = 0xFF;
        wmask[offset + offsetof(CXLDVSECPortExt, alt_memory_base)] = 0xF0;
        wmask[offset + offsetof(CXLDVSECPortExt, alt_memory_base) + 1] = 0xFF;
        wmask[offset + offsetof(CXLDVSECPortExt, alt_memory_limit)] = 0xF0;
        wmask[offset + offsetof(CXLDVSECPortExt, alt_memory_limit) + 1] = 0xFF;
        wmask[offset + offsetof(CXLDVSECPortExt, alt_prefetch_base)] = 0xF0;
        wmask[offset + offsetof(CXLDVSECPortExt, alt_prefetch_base) + 1] = 0xFF;
        wmask[offset + offsetof(CXLDVSECPortExt, alt_prefetch_limit)] = 0xF0;
        wmask[offset + offsetof(CXLDVSECPortExt, alt_prefetch_limit) + 1] =
            0xFF;
        wmask[offset + offsetof(CXLDVSECPortExt, alt_prefetch_base_high)] =
            0xFF;
        wmask[offset + offsetof(CXLDVSECPortExt, alt_prefetch_base_high) + 1] =
            0xFF;
        wmask[offset + offsetof(CXLDVSECPortExt, alt_prefetch_base_high) + 2] =
            0xFF;
        wmask[offset + offsetof(CXLDVSECPortExt, alt_prefetch_base_high) + 3] =
            0xFF;
        wmask[offset + offsetof(CXLDVSECPortExt, alt_prefetch_limit_high)] =
            0xFF;
        wmask[offset + offsetof(CXLDVSECPortExt, alt_prefetch_limit_high) + 1] =
            0xFF;
        wmask[offset + offsetof(CXLDVSECPortExt, alt_prefetch_limit_high) + 2] =
            0xFF;
        wmask[offset + offsetof(CXLDVSECPortExt, alt_prefetch_limit_high) + 3] =
            0xFF;
        break;
    case GPF_PORT_DVSEC:
        wmask[offset + offsetof(CXLDVSECPortGPF, phase1_ctrl)] = 0x0F;
        wmask[offset + offsetof(CXLDVSECPortGPF, phase1_ctrl) + 1] = 0x0F;
        wmask[offset + offsetof(CXLDVSECPortGPF, phase2_ctrl)] = 0x0F;
        wmask[offset + offsetof(CXLDVSECPortGPF, phase2_ctrl) + 1] = 0x0F;
        break;
    case GPF_DEVICE_DVSEC:
        wmask[offset + offsetof(CXLDVSECDeviceGPF, phase2_duration)] = 0x0F;
        wmask[offset + offsetof(CXLDVSECDeviceGPF, phase2_duration) + 1] = 0x0F;
        wmask[offset + offsetof(CXLDVSECDeviceGPF, phase2_power)] = 0xFF;
        wmask[offset + offsetof(CXLDVSECDeviceGPF, phase2_power) + 1] = 0xFF;
        wmask[offset + offsetof(CXLDVSECDeviceGPF, phase2_power) + 2] = 0xFF;
        wmask[offset + offsetof(CXLDVSECDeviceGPF, phase2_power) + 3] = 0xFF;
        break;
    case PCIE_FLEXBUS_PORT_DVSEC:
        switch (cxl_dev_type) {
        case CXL2_ROOT_PORT:
            /* No MLD */
            wmask[offset + offsetof(CXLDVSECPortFlexBus, ctrl)] = 0xbd;
            break;
        case CXL2_DOWNSTREAM_PORT:
            wmask[offset + offsetof(CXLDVSECPortFlexBus, ctrl)] = 0xfd;
            break;
        default: /* Registers are RO for other component types */
            break;
        }
        /* There are rw1cs bits in the status register but never set */
        break;
    }

    /* Update state for future DVSEC additions */
    range_init_nofail(&cxl->dvsecs[type], cxl->dvsec_offset, length);
    cxl->dvsec_offset += length;
}

/* CXL r3.1 Section 8.2.4.20.7 CXL HDM Decoder n Control Register */
uint8_t cxl_interleave_ways_enc(int iw, Error **errp)
{
    switch (iw) {
    case 1: return 0x0;
    case 2: return 0x1;
    case 4: return 0x2;
    case 8: return 0x3;
    case 16: return 0x4;
    case 3: return 0x8;
    case 6: return 0x9;
    case 12: return 0xa;
    default:
        error_setg(errp, "Interleave ways: %d not supported", iw);
        return 0;
    }
}

int cxl_interleave_ways_dec(uint8_t iw_enc, Error **errp)
{
    switch (iw_enc) {
    case 0x0: return 1;
    case 0x1: return 2;
    case 0x2: return 4;
    case 0x3: return 8;
    case 0x4: return 16;
    case 0x8: return 3;
    case 0x9: return 6;
    case 0xa: return 12;
    default:
        error_setg(errp, "Encoded interleave ways: %d not supported", iw_enc);
        return 0;
    }
}

uint8_t cxl_interleave_granularity_enc(uint64_t gran, Error **errp)
{
    switch (gran) {
    case 256: return 0;
    case 512: return 1;
    case 1024: return 2;
    case 2048: return 3;
    case 4096: return 4;
    case 8192: return 5;
    case 16384: return 6;
    default:
        error_setg(errp, "Interleave granularity: %" PRIu64 " invalid", gran);
        return 0;
    }
}
