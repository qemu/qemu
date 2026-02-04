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
    case 4: {
        QEMU_BUILD_BUG_ON(sizeof(*cregs->cache_mem_registers) != 4);

        if (offset == A_CXL_BI_RT_STATUS ||
            offset == A_CXL_BI_DECODER_STATUS) {
            int type;
            uint64_t started;

            type = (offset == A_CXL_BI_RT_STATUS) ?
                    CXL_BISTATE_RT : CXL_BISTATE_DECODER;
            started = cxl_cstate->bi_state[type].last_commit;

            if (started) {
                uint32_t *cache_mem = cregs->cache_mem_registers;
                uint32_t val = cache_mem[offset / 4];
                uint64_t now;
                int set;

                now = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
                /* arbitrary 100 ms to do the commit */
                set = !!(now >= started + 100);

                if (offset == A_CXL_BI_RT_STATUS) {
                    val = FIELD_DP32(val, CXL_BI_RT_STATUS, COMMITTED, set);
                } else {
                    val = FIELD_DP32(val, CXL_BI_DECODER_STATUS, COMMITTED,
                                     set);
                }
                stl_le_p((uint8_t *)cache_mem + offset, val);
            }
        }

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

static void bi_handler(CXLComponentState *cxl_cstate, hwaddr offset,
                            uint32_t value)
{
    ComponentRegisters *cregs = &cxl_cstate->crb;
    uint32_t sts, *cache_mem = cregs->cache_mem_registers;
    bool to_commit = false;
    int type = 0; /* Unused value - work around for compiler warning */

    switch (offset) {
    case A_CXL_BI_RT_CTRL:
        to_commit = FIELD_EX32(value, CXL_BI_RT_CTRL, COMMIT);
        if (to_commit) {
            sts = cxl_cache_mem_read_reg(cxl_cstate,
                                         R_CXL_BI_RT_STATUS, 4);
            sts = FIELD_DP32(sts, CXL_BI_RT_STATUS, COMMITTED, 0);
            stl_le_p((uint8_t *)cache_mem + R_CXL_BI_RT_STATUS, sts);
            type = CXL_BISTATE_RT;
        }
        break;
    case A_CXL_BI_DECODER_CTRL:
        to_commit = FIELD_EX32(value, CXL_BI_DECODER_CTRL, COMMIT);
        if (to_commit) {
            sts = cxl_cache_mem_read_reg(cxl_cstate,
                                         R_CXL_BI_DECODER_STATUS, 4);
            sts = FIELD_DP32(sts, CXL_BI_DECODER_STATUS, COMMITTED, 0);
            stl_le_p((uint8_t *)cache_mem + R_CXL_BI_DECODER_STATUS, sts);
            type = CXL_BISTATE_DECODER;
        }
        break;
    default:
        break;
    }

    if (to_commit) {
        cxl_cstate->bi_state[type].last_commit =
                qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
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
        } else if (offset == A_CXL_BI_RT_CTRL ||
                   offset == A_CXL_BI_DECODER_CTRL) {
            bi_handler(cxl_cstate, offset, value);
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
                            enum reg_type type, bool bi)
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
    if (type == CXL2_TYPE3_DEVICE) {
        ARRAY_FIELD_DP32(reg_state, CXL_HDM_DECODER_CAPABILITY, 3_6_12_WAY, 1);
        ARRAY_FIELD_DP32(reg_state, CXL_HDM_DECODER_CAPABILITY, 16_WAY, 1);
    } else {
        ARRAY_FIELD_DP32(reg_state, CXL_HDM_DECODER_CAPABILITY, 3_6_12_WAY, 0);
        ARRAY_FIELD_DP32(reg_state, CXL_HDM_DECODER_CAPABILITY, 16_WAY, 0);
    }
    ARRAY_FIELD_DP32(reg_state, CXL_HDM_DECODER_CAPABILITY, UIO, 0);
    ARRAY_FIELD_DP32(reg_state, CXL_HDM_DECODER_CAPABILITY,
                     UIO_DECODER_COUNT, 0);
    ARRAY_FIELD_DP32(reg_state, CXL_HDM_DECODER_CAPABILITY, MEMDATA_NXM_CAP, 0);
    ARRAY_FIELD_DP32(reg_state, CXL_HDM_DECODER_CAPABILITY,
                     SUPPORTED_COHERENCY_MODEL,
                     /* host+dev or Unknown */
                     type == CXL2_TYPE3_DEVICE && bi ? 3 : 0);
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

static void bi_rt_init_common(uint32_t *reg_state, uint32_t *write_msk)
{
    /* switch usp must commit the new BI-ID, timeout of 2secs */
    ARRAY_FIELD_DP32(reg_state, CXL_BI_RT_CAPABILITY, EXPLICIT_COMMIT, 1);

    ARRAY_FIELD_DP32(reg_state, CXL_BI_RT_CTRL, COMMIT, 0);
    write_msk[R_CXL_BI_RT_CTRL] = 0x1;

    ARRAY_FIELD_DP32(reg_state, CXL_BI_RT_STATUS, COMMITTED, 0);
    ARRAY_FIELD_DP32(reg_state, CXL_BI_RT_STATUS, ERR_NOT_COMMITTED, 0);
    ARRAY_FIELD_DP32(reg_state, CXL_BI_RT_STATUS, COMMIT_TMO_SCALE, 0x6);
    ARRAY_FIELD_DP32(reg_state, CXL_BI_RT_STATUS, COMMIT_TMO_BASE, 0x2);
}

static void bi_decoder_init_common(uint32_t *reg_state, uint32_t *write_msk,
                                   enum reg_type type)
{
    ARRAY_FIELD_DP32(reg_state, CXL_BI_DECODER_CAPABILITY, HDM_D, 0);
    /* switch dsp must commit the new BI-ID, timeout of 2secs */
    ARRAY_FIELD_DP32(reg_state, CXL_BI_DECODER_CAPABILITY, EXPLICIT_COMMIT,
                     (type != CXL2_ROOT_PORT && type != CXL2_TYPE3_DEVICE));

    ARRAY_FIELD_DP32(reg_state, CXL_BI_DECODER_CTRL, BI_FW, 0);
    ARRAY_FIELD_DP32(reg_state, CXL_BI_DECODER_CTRL, BI_ENABLE, 0);
    ARRAY_FIELD_DP32(reg_state, CXL_BI_DECODER_CTRL, COMMIT, 0);
    write_msk[R_CXL_BI_DECODER_CTRL] = 0x7;

    ARRAY_FIELD_DP32(reg_state, CXL_BI_DECODER_STATUS, COMMITTED, 0);
    ARRAY_FIELD_DP32(reg_state, CXL_BI_DECODER_STATUS, ERR_NOT_COMMITTED, 0);
    ARRAY_FIELD_DP32(reg_state, CXL_BI_DECODER_STATUS, COMMIT_TMO_SCALE, 0x6);
    ARRAY_FIELD_DP32(reg_state, CXL_BI_DECODER_STATUS, COMMIT_TMO_BASE, 0x2);
}

void cxl_component_register_init_common(uint32_t *reg_state,
                                        uint32_t *write_msk,
                                        enum reg_type type,
                                        bool bi)
{
    int caps = 0;

    memset(reg_state, 0, CXL2_COMPONENT_CM_REGION_SIZE);

    /* CXL Capability Header Register */
    ARRAY_FIELD_DP32(reg_state, CXL_CAPABILITY_HEADER, ID, 1);
    ARRAY_FIELD_DP32(reg_state, CXL_CAPABILITY_HEADER, VERSION,
        CXL_CAPABILITY_VERSION);
    ARRAY_FIELD_DP32(reg_state, CXL_CAPABILITY_HEADER, CACHE_MEM_VERSION, 1);

#define init_cap_reg(reg, id, version)                                        \
    do {                                                                      \
        int which = CXL_##reg##_CAP_HDR_IDX;                                  \
        if (CXL_##reg##_CAP_HDR_IDX > caps)                                   \
            caps = CXL_##reg##_CAP_HDR_IDX;                                   \
        reg_state[which] = FIELD_DP32(reg_state[which],                       \
                                      CXL_##reg##_CAPABILITY_HEADER, ID, id); \
        reg_state[which] =                                                    \
            FIELD_DP32(reg_state[which], CXL_##reg##_CAPABILITY_HEADER,       \
                       VERSION, version);                                     \
        reg_state[which] =                                                    \
            FIELD_DP32(reg_state[which], CXL_##reg##_CAPABILITY_HEADER, PTR,  \
                       CXL_##reg##_REGISTERS_OFFSET);                         \
    } while (0)

    /* CXL r3.2 8.2.4 Table 8-22 */
    switch (type) {
    case CXL2_ROOT_PORT:
    case CXL2_RC:
        /* + Extended Security, + Snoop */
        init_cap_reg(EXTSEC, 6, 1);
        init_cap_reg(SNOOP, 8, 1);
        /* fallthrough */
    case CXL2_UPSTREAM_PORT:
    case CXL2_TYPE3_DEVICE:
    case CXL2_LOGICAL_DEVICE:
        /* + HDM */
        init_cap_reg(HDM, 5, 1);
        hdm_init_common(reg_state, write_msk, type, bi);
        /* fallthrough */
    case CXL2_DOWNSTREAM_PORT:
    case CXL2_DEVICE:
        /* RAS, Link */
        if (type != CXL2_RC) {
            init_cap_reg(RAS, 2, 2);
            ras_init_common(reg_state, write_msk);
        }
        init_cap_reg(LINK, 4, 2);
        break;
    default:
        abort();
    }

    /* back invalidate */
    if (bi) {
        switch (type) {
        case CXL2_UPSTREAM_PORT:
            init_cap_reg(BI_RT, 11, CXL_BI_RT_CAP_VERSION);
            bi_rt_init_common(reg_state, write_msk);
            break;
        case CXL2_ROOT_PORT:
        case CXL2_DOWNSTREAM_PORT:
        case CXL2_TYPE3_DEVICE:
            init_cap_reg(BI_DECODER, 12, CXL_BI_DECODER_CAP_VERSION);
            bi_decoder_init_common(reg_state, write_msk, type);
            break;
        default:
            break;
        }
    }

    ARRAY_FIELD_DP32(reg_state, CXL_CAPABILITY_HEADER, ARRAY_SIZE, caps);
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
