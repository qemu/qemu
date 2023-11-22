/*
 * QEMU PowerPC XIVE2 internal structure definitions (POWER10)
 *
 * Copyright (c) 2019-2022, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#ifndef PPC_XIVE2_REGS_H
#define PPC_XIVE2_REGS_H

#include "qemu/bswap.h"

/*
 * Thread Interrupt Management Area (TIMA)
 *
 * In Gen1 mode (P9 compat mode) word 2 is the same. However in Gen2
 * mode (P10), the CAM line is slightly different as the VP space was
 * increased.
 */
#define   TM2_QW0W2_VU           PPC_BIT32(0)
#define   TM2_QW0W2_LOGIC_SERV   PPC_BITMASK32(4, 31)
#define   TM2_QW1W2_VO           PPC_BIT32(0)
#define   TM2_QW1W2_HO           PPC_BIT32(1)
#define   TM2_QW1W2_OS_CAM       PPC_BITMASK32(4, 31)
#define   TM2_QW2W2_VP           PPC_BIT32(0)
#define   TM2_QW2W2_HP           PPC_BIT32(1)
#define   TM2_QW2W2_POOL_CAM     PPC_BITMASK32(4, 31)
#define   TM2_QW3W2_VT           PPC_BIT32(0)
#define   TM2_QW3W2_HT           PPC_BIT32(1)
#define   TM2_QW3W2_LP           PPC_BIT32(6)
#define   TM2_QW3W2_LE           PPC_BIT32(7)

/*
 * Event Assignment Structure (EAS)
 */

typedef struct Xive2Eas {
        uint64_t       w;
#define EAS2_VALID                 PPC_BIT(0)
#define EAS2_END_BLOCK             PPC_BITMASK(4, 7) /* Destination EQ block# */
#define EAS2_END_INDEX             PPC_BITMASK(8, 31) /* Destination EQ index */
#define EAS2_MASKED                PPC_BIT(32) /* Masked                 */
#define EAS2_END_DATA              PPC_BITMASK(33, 63) /* written to the EQ */
} Xive2Eas;

#define xive2_eas_is_valid(eas)   (be64_to_cpu((eas)->w) & EAS2_VALID)
#define xive2_eas_is_masked(eas)  (be64_to_cpu((eas)->w) & EAS2_MASKED)

void xive2_eas_pic_print_info(Xive2Eas *eas, uint32_t lisn, Monitor *mon);

/*
 * Event Notifification Descriptor (END)
 */

typedef struct Xive2End {
        uint32_t       w0;
#define END2_W0_VALID              PPC_BIT32(0) /* "v" bit */
#define END2_W0_ENQUEUE            PPC_BIT32(5) /* "q" bit */
#define END2_W0_UCOND_NOTIFY       PPC_BIT32(6) /* "n" bit */
#define END2_W0_SILENT_ESCALATE    PPC_BIT32(7) /* "s" bit */
#define END2_W0_BACKLOG            PPC_BIT32(8) /* "b" bit */
#define END2_W0_PRECL_ESC_CTL      PPC_BIT32(9) /* "p" bit */
#define END2_W0_UNCOND_ESCALATE    PPC_BIT32(10) /* "u" bit */
#define END2_W0_ESCALATE_CTL       PPC_BIT32(11) /* "e" bit */
#define END2_W0_ADAPTIVE_ESC       PPC_BIT32(12) /* "a" bit */
#define END2_W0_ESCALATE_END       PPC_BIT32(13) /* "N" bit */
#define END2_W0_FIRMWARE1          PPC_BIT32(16) /* Owned by FW */
#define END2_W0_FIRMWARE2          PPC_BIT32(17) /* Owned by FW */
#define END2_W0_AEC_SIZE           PPC_BITMASK32(18, 19)
#define END2_W0_AEG_SIZE           PPC_BITMASK32(20, 23)
#define END2_W0_EQ_VG_PREDICT      PPC_BITMASK32(24, 31) /* Owned by HW */
        uint32_t       w1;
#define END2_W1_ESn                PPC_BITMASK32(0, 1)
#define END2_W1_ESn_P              PPC_BIT32(0)
#define END2_W1_ESn_Q              PPC_BIT32(1)
#define END2_W1_ESe                PPC_BITMASK32(2, 3)
#define END2_W1_ESe_P              PPC_BIT32(2)
#define END2_W1_ESe_Q              PPC_BIT32(3)
#define END2_W1_GEN_FLIPPED        PPC_BIT32(8)
#define END2_W1_GENERATION         PPC_BIT32(9)
#define END2_W1_PAGE_OFF           PPC_BITMASK32(10, 31)
        uint32_t       w2;
#define END2_W2_RESERVED           PPC_BITMASK32(4, 7)
#define END2_W2_EQ_ADDR_HI         PPC_BITMASK32(8, 31)
        uint32_t       w3;
#define END2_W3_EQ_ADDR_LO         PPC_BITMASK32(0, 24)
#define END2_W3_QSIZE              PPC_BITMASK32(28, 31)
        uint32_t       w4;
#define END2_W4_END_BLOCK          PPC_BITMASK32(4, 7)
#define END2_W4_ESC_END_INDEX      PPC_BITMASK32(8, 31)
#define END2_W4_ESB_BLOCK          PPC_BITMASK32(0, 3)
#define END2_W4_ESC_ESB_INDEX      PPC_BITMASK32(4, 31)
        uint32_t       w5;
#define END2_W5_ESC_END_DATA       PPC_BITMASK32(1, 31)
        uint32_t       w6;
#define END2_W6_FORMAT_BIT         PPC_BIT32(0)
#define END2_W6_IGNORE             PPC_BIT32(1)
#define END2_W6_VP_BLOCK           PPC_BITMASK32(4, 7)
#define END2_W6_VP_OFFSET          PPC_BITMASK32(8, 31)
#define END2_W6_VP_OFFSET_GEN1     PPC_BITMASK32(13, 31)
        uint32_t       w7;
#define END2_W7_TOPO               PPC_BITMASK32(0, 3) /* Owned by HW */
#define END2_W7_F0_PRIORITY        PPC_BITMASK32(8, 15)
#define END2_W7_F1_LOG_SERVER_ID   PPC_BITMASK32(4, 31)
} Xive2End;

#define xive2_end_is_valid(end)    (be32_to_cpu((end)->w0) & END2_W0_VALID)
#define xive2_end_is_enqueue(end)  (be32_to_cpu((end)->w0) & END2_W0_ENQUEUE)
#define xive2_end_is_notify(end)                \
    (be32_to_cpu((end)->w0) & END2_W0_UCOND_NOTIFY)
#define xive2_end_is_backlog(end)  (be32_to_cpu((end)->w0) & END2_W0_BACKLOG)
#define xive2_end_is_escalate(end)                      \
    (be32_to_cpu((end)->w0) & END2_W0_ESCALATE_CTL)
#define xive2_end_is_uncond_escalation(end)              \
    (be32_to_cpu((end)->w0) & END2_W0_UNCOND_ESCALATE)
#define xive2_end_is_silent_escalation(end)              \
    (be32_to_cpu((end)->w0) & END2_W0_SILENT_ESCALATE)
#define xive2_end_is_escalate_end(end)              \
    (be32_to_cpu((end)->w0) & END2_W0_ESCALATE_END)
#define xive2_end_is_firmware1(end)              \
    (be32_to_cpu((end)->w0) & END2_W0_FIRMWARE1)
#define xive2_end_is_firmware2(end)              \
    (be32_to_cpu((end)->w0) & END2_W0_FIRMWARE2)

static inline uint64_t xive2_end_qaddr(Xive2End *end)
{
    return ((uint64_t) be32_to_cpu(end->w2) & END2_W2_EQ_ADDR_HI) << 32 |
        (be32_to_cpu(end->w3) & END2_W3_EQ_ADDR_LO);
}

void xive2_end_pic_print_info(Xive2End *end, uint32_t end_idx, Monitor *mon);
void xive2_end_queue_pic_print_info(Xive2End *end, uint32_t width,
                                    Monitor *mon);
void xive2_end_eas_pic_print_info(Xive2End *end, uint32_t end_idx,
                                   Monitor *mon);

/*
 * Notification Virtual Processor (NVP)
 */
typedef struct Xive2Nvp {
        uint32_t       w0;
#define NVP2_W0_VALID              PPC_BIT32(0)
#define NVP2_W0_HW                 PPC_BIT32(7)
#define NVP2_W0_ESC_END            PPC_BIT32(25) /* 'N' bit 0:ESB  1:END */
        uint32_t       w1;
#define NVP2_W1_CO                 PPC_BIT32(13)
#define NVP2_W1_CO_PRIV            PPC_BITMASK32(14, 15)
#define NVP2_W1_CO_THRID_VALID     PPC_BIT32(16)
#define NVP2_W1_CO_THRID           PPC_BITMASK32(17, 31)
        uint32_t       w2;
#define NVP2_W2_CPPR               PPC_BITMASK32(0, 7)
#define NVP2_W2_IPB                PPC_BITMASK32(8, 15)
#define NVP2_W2_LSMFB              PPC_BITMASK32(16, 23)
        uint32_t       w3;
        uint32_t       w4;
#define NVP2_W4_ESC_ESB_BLOCK      PPC_BITMASK32(0, 3)  /* N:0 */
#define NVP2_W4_ESC_ESB_INDEX      PPC_BITMASK32(4, 31) /* N:0 */
#define NVP2_W4_ESC_END_BLOCK      PPC_BITMASK32(4, 7)  /* N:1 */
#define NVP2_W4_ESC_END_INDEX      PPC_BITMASK32(8, 31) /* N:1 */
        uint32_t       w5;
#define NVP2_W5_PSIZE              PPC_BITMASK32(0, 1)
#define NVP2_W5_VP_END_BLOCK       PPC_BITMASK32(4, 7)
#define NVP2_W5_VP_END_INDEX       PPC_BITMASK32(8, 31)
        uint32_t       w6;
        uint32_t       w7;
} Xive2Nvp;

#define xive2_nvp_is_valid(nvp)    (be32_to_cpu((nvp)->w0) & NVP2_W0_VALID)
#define xive2_nvp_is_hw(nvp)       (be32_to_cpu((nvp)->w0) & NVP2_W0_HW)
#define xive2_nvp_is_co(nvp)       (be32_to_cpu((nvp)->w1) & NVP2_W1_CO)

/*
 * The VP number space in a block is defined by the END2_W6_VP_OFFSET
 * field of the XIVE END. When running in Gen1 mode (P9 compat mode),
 * the VP space is reduced to (1 << 19) VPs per block
 */
#define XIVE2_NVP_SHIFT              24
#define XIVE2_NVP_COUNT              (1 << XIVE2_NVP_SHIFT)

static inline uint32_t xive2_nvp_cam_line(uint8_t nvp_blk, uint32_t nvp_idx)
{
    return (nvp_blk << XIVE2_NVP_SHIFT) | nvp_idx;
}

static inline uint32_t xive2_nvp_idx(uint32_t cam_line)
{
    return cam_line & ((1 << XIVE2_NVP_SHIFT) - 1);
}

static inline uint32_t xive2_nvp_blk(uint32_t cam_line)
{
    return (cam_line >> XIVE2_NVP_SHIFT) & 0xf;
}

/*
 * Notification Virtual Group or Crowd (NVG/NVC)
 */
typedef struct Xive2Nvgc {
        uint32_t        w0;
#define NVGC2_W0_VALID             PPC_BIT32(0)
        uint32_t        w1;
        uint32_t        w2;
        uint32_t        w3;
        uint32_t        w4;
        uint32_t        w5;
        uint32_t        w6;
        uint32_t        w7;
} Xive2Nvgc;

#endif /* PPC_XIVE2_REGS_H */
