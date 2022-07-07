#ifndef BLOCK_NVME_H
#define BLOCK_NVME_H

typedef struct QEMU_PACKED NvmeBar {
    uint64_t    cap;
    uint32_t    vs;
    uint32_t    intms;
    uint32_t    intmc;
    uint32_t    cc;
    uint8_t     rsvd24[4];
    uint32_t    csts;
    uint32_t    nssr;
    uint32_t    aqa;
    uint64_t    asq;
    uint64_t    acq;
    uint32_t    cmbloc;
    uint32_t    cmbsz;
    uint32_t    bpinfo;
    uint32_t    bprsel;
    uint64_t    bpmbl;
    uint64_t    cmbmsc;
    uint32_t    cmbsts;
    uint8_t     rsvd92[3492];
    uint32_t    pmrcap;
    uint32_t    pmrctl;
    uint32_t    pmrsts;
    uint32_t    pmrebs;
    uint32_t    pmrswtp;
    uint32_t    pmrmscl;
    uint32_t    pmrmscu;
    uint8_t     css[484];
} NvmeBar;

enum NvmeBarRegs {
    NVME_REG_CAP     = offsetof(NvmeBar, cap),
    NVME_REG_VS      = offsetof(NvmeBar, vs),
    NVME_REG_INTMS   = offsetof(NvmeBar, intms),
    NVME_REG_INTMC   = offsetof(NvmeBar, intmc),
    NVME_REG_CC      = offsetof(NvmeBar, cc),
    NVME_REG_CSTS    = offsetof(NvmeBar, csts),
    NVME_REG_NSSR    = offsetof(NvmeBar, nssr),
    NVME_REG_AQA     = offsetof(NvmeBar, aqa),
    NVME_REG_ASQ     = offsetof(NvmeBar, asq),
    NVME_REG_ACQ     = offsetof(NvmeBar, acq),
    NVME_REG_CMBLOC  = offsetof(NvmeBar, cmbloc),
    NVME_REG_CMBSZ   = offsetof(NvmeBar, cmbsz),
    NVME_REG_BPINFO  = offsetof(NvmeBar, bpinfo),
    NVME_REG_BPRSEL  = offsetof(NvmeBar, bprsel),
    NVME_REG_BPMBL   = offsetof(NvmeBar, bpmbl),
    NVME_REG_CMBMSC  = offsetof(NvmeBar, cmbmsc),
    NVME_REG_CMBSTS  = offsetof(NvmeBar, cmbsts),
    NVME_REG_PMRCAP  = offsetof(NvmeBar, pmrcap),
    NVME_REG_PMRCTL  = offsetof(NvmeBar, pmrctl),
    NVME_REG_PMRSTS  = offsetof(NvmeBar, pmrsts),
    NVME_REG_PMREBS  = offsetof(NvmeBar, pmrebs),
    NVME_REG_PMRSWTP = offsetof(NvmeBar, pmrswtp),
    NVME_REG_PMRMSCL = offsetof(NvmeBar, pmrmscl),
    NVME_REG_PMRMSCU = offsetof(NvmeBar, pmrmscu),
};

enum NvmeCapShift {
    CAP_MQES_SHIFT     = 0,
    CAP_CQR_SHIFT      = 16,
    CAP_AMS_SHIFT      = 17,
    CAP_TO_SHIFT       = 24,
    CAP_DSTRD_SHIFT    = 32,
    CAP_NSSRS_SHIFT    = 36,
    CAP_CSS_SHIFT      = 37,
    CAP_MPSMIN_SHIFT   = 48,
    CAP_MPSMAX_SHIFT   = 52,
    CAP_PMRS_SHIFT     = 56,
    CAP_CMBS_SHIFT     = 57,
};

enum NvmeCapMask {
    CAP_MQES_MASK      = 0xffff,
    CAP_CQR_MASK       = 0x1,
    CAP_AMS_MASK       = 0x3,
    CAP_TO_MASK        = 0xff,
    CAP_DSTRD_MASK     = 0xf,
    CAP_NSSRS_MASK     = 0x1,
    CAP_CSS_MASK       = 0xff,
    CAP_MPSMIN_MASK    = 0xf,
    CAP_MPSMAX_MASK    = 0xf,
    CAP_PMRS_MASK      = 0x1,
    CAP_CMBS_MASK      = 0x1,
};

#define NVME_CAP_MQES(cap)  (((cap) >> CAP_MQES_SHIFT)   & CAP_MQES_MASK)
#define NVME_CAP_CQR(cap)   (((cap) >> CAP_CQR_SHIFT)    & CAP_CQR_MASK)
#define NVME_CAP_AMS(cap)   (((cap) >> CAP_AMS_SHIFT)    & CAP_AMS_MASK)
#define NVME_CAP_TO(cap)    (((cap) >> CAP_TO_SHIFT)     & CAP_TO_MASK)
#define NVME_CAP_DSTRD(cap) (((cap) >> CAP_DSTRD_SHIFT)  & CAP_DSTRD_MASK)
#define NVME_CAP_NSSRS(cap) (((cap) >> CAP_NSSRS_SHIFT)  & CAP_NSSRS_MASK)
#define NVME_CAP_CSS(cap)   (((cap) >> CAP_CSS_SHIFT)    & CAP_CSS_MASK)
#define NVME_CAP_MPSMIN(cap)(((cap) >> CAP_MPSMIN_SHIFT) & CAP_MPSMIN_MASK)
#define NVME_CAP_MPSMAX(cap)(((cap) >> CAP_MPSMAX_SHIFT) & CAP_MPSMAX_MASK)
#define NVME_CAP_PMRS(cap)  (((cap) >> CAP_PMRS_SHIFT)   & CAP_PMRS_MASK)
#define NVME_CAP_CMBS(cap)  (((cap) >> CAP_CMBS_SHIFT)   & CAP_CMBS_MASK)

#define NVME_CAP_SET_MQES(cap, val)   \
    ((cap) |= (uint64_t)((val) & CAP_MQES_MASK)   << CAP_MQES_SHIFT)
#define NVME_CAP_SET_CQR(cap, val)    \
    ((cap) |= (uint64_t)((val) & CAP_CQR_MASK)    << CAP_CQR_SHIFT)
#define NVME_CAP_SET_AMS(cap, val)    \
    ((cap) |= (uint64_t)((val) & CAP_AMS_MASK)    << CAP_AMS_SHIFT)
#define NVME_CAP_SET_TO(cap, val)     \
    ((cap) |= (uint64_t)((val) & CAP_TO_MASK)     << CAP_TO_SHIFT)
#define NVME_CAP_SET_DSTRD(cap, val)  \
    ((cap) |= (uint64_t)((val) & CAP_DSTRD_MASK)  << CAP_DSTRD_SHIFT)
#define NVME_CAP_SET_NSSRS(cap, val)  \
    ((cap) |= (uint64_t)((val) & CAP_NSSRS_MASK)  << CAP_NSSRS_SHIFT)
#define NVME_CAP_SET_CSS(cap, val)    \
    ((cap) |= (uint64_t)((val) & CAP_CSS_MASK)    << CAP_CSS_SHIFT)
#define NVME_CAP_SET_MPSMIN(cap, val) \
    ((cap) |= (uint64_t)((val) & CAP_MPSMIN_MASK) << CAP_MPSMIN_SHIFT)
#define NVME_CAP_SET_MPSMAX(cap, val) \
    ((cap) |= (uint64_t)((val) & CAP_MPSMAX_MASK) << CAP_MPSMAX_SHIFT)
#define NVME_CAP_SET_PMRS(cap, val)   \
    ((cap) |= (uint64_t)((val) & CAP_PMRS_MASK)   << CAP_PMRS_SHIFT)
#define NVME_CAP_SET_CMBS(cap, val)   \
    ((cap) |= (uint64_t)((val) & CAP_CMBS_MASK)   << CAP_CMBS_SHIFT)

enum NvmeCapCss {
    NVME_CAP_CSS_NVM        = 1 << 0,
    NVME_CAP_CSS_CSI_SUPP   = 1 << 6,
    NVME_CAP_CSS_ADMIN_ONLY = 1 << 7,
};

enum NvmeCcShift {
    CC_EN_SHIFT     = 0,
    CC_CSS_SHIFT    = 4,
    CC_MPS_SHIFT    = 7,
    CC_AMS_SHIFT    = 11,
    CC_SHN_SHIFT    = 14,
    CC_IOSQES_SHIFT = 16,
    CC_IOCQES_SHIFT = 20,
};

enum NvmeCcMask {
    CC_EN_MASK      = 0x1,
    CC_CSS_MASK     = 0x7,
    CC_MPS_MASK     = 0xf,
    CC_AMS_MASK     = 0x7,
    CC_SHN_MASK     = 0x3,
    CC_IOSQES_MASK  = 0xf,
    CC_IOCQES_MASK  = 0xf,
};

#define NVME_CC_EN(cc)     ((cc >> CC_EN_SHIFT)     & CC_EN_MASK)
#define NVME_CC_CSS(cc)    ((cc >> CC_CSS_SHIFT)    & CC_CSS_MASK)
#define NVME_CC_MPS(cc)    ((cc >> CC_MPS_SHIFT)    & CC_MPS_MASK)
#define NVME_CC_AMS(cc)    ((cc >> CC_AMS_SHIFT)    & CC_AMS_MASK)
#define NVME_CC_SHN(cc)    ((cc >> CC_SHN_SHIFT)    & CC_SHN_MASK)
#define NVME_CC_IOSQES(cc) ((cc >> CC_IOSQES_SHIFT) & CC_IOSQES_MASK)
#define NVME_CC_IOCQES(cc) ((cc >> CC_IOCQES_SHIFT) & CC_IOCQES_MASK)

enum NvmeCcCss {
    NVME_CC_CSS_NVM        = 0x0,
    NVME_CC_CSS_CSI        = 0x6,
    NVME_CC_CSS_ADMIN_ONLY = 0x7,
};

#define NVME_SET_CC_EN(cc, val)     \
    (cc |= (uint32_t)((val) & CC_EN_MASK) << CC_EN_SHIFT)
#define NVME_SET_CC_CSS(cc, val)    \
    (cc |= (uint32_t)((val) & CC_CSS_MASK) << CC_CSS_SHIFT)
#define NVME_SET_CC_MPS(cc, val)    \
    (cc |= (uint32_t)((val) & CC_MPS_MASK) << CC_MPS_SHIFT)
#define NVME_SET_CC_AMS(cc, val)    \
    (cc |= (uint32_t)((val) & CC_AMS_MASK) << CC_AMS_SHIFT)
#define NVME_SET_CC_SHN(cc, val)    \
    (cc |= (uint32_t)((val) & CC_SHN_MASK) << CC_SHN_SHIFT)
#define NVME_SET_CC_IOSQES(cc, val) \
    (cc |= (uint32_t)((val) & CC_IOSQES_MASK) << CC_IOSQES_SHIFT)
#define NVME_SET_CC_IOCQES(cc, val) \
    (cc |= (uint32_t)((val) & CC_IOCQES_MASK) << CC_IOCQES_SHIFT)

enum NvmeCstsShift {
    CSTS_RDY_SHIFT      = 0,
    CSTS_CFS_SHIFT      = 1,
    CSTS_SHST_SHIFT     = 2,
    CSTS_NSSRO_SHIFT    = 4,
};

enum NvmeCstsMask {
    CSTS_RDY_MASK   = 0x1,
    CSTS_CFS_MASK   = 0x1,
    CSTS_SHST_MASK  = 0x3,
    CSTS_NSSRO_MASK = 0x1,
};

enum NvmeCsts {
    NVME_CSTS_READY         = 1 << CSTS_RDY_SHIFT,
    NVME_CSTS_FAILED        = 1 << CSTS_CFS_SHIFT,
    NVME_CSTS_SHST_NORMAL   = 0 << CSTS_SHST_SHIFT,
    NVME_CSTS_SHST_PROGRESS = 1 << CSTS_SHST_SHIFT,
    NVME_CSTS_SHST_COMPLETE = 2 << CSTS_SHST_SHIFT,
    NVME_CSTS_NSSRO         = 1 << CSTS_NSSRO_SHIFT,
};

#define NVME_CSTS_RDY(csts)     ((csts >> CSTS_RDY_SHIFT)   & CSTS_RDY_MASK)
#define NVME_CSTS_CFS(csts)     ((csts >> CSTS_CFS_SHIFT)   & CSTS_CFS_MASK)
#define NVME_CSTS_SHST(csts)    ((csts >> CSTS_SHST_SHIFT)  & CSTS_SHST_MASK)
#define NVME_CSTS_NSSRO(csts)   ((csts >> CSTS_NSSRO_SHIFT) & CSTS_NSSRO_MASK)

enum NvmeAqaShift {
    AQA_ASQS_SHIFT  = 0,
    AQA_ACQS_SHIFT  = 16,
};

enum NvmeAqaMask {
    AQA_ASQS_MASK   = 0xfff,
    AQA_ACQS_MASK   = 0xfff,
};

#define NVME_AQA_ASQS(aqa) ((aqa >> AQA_ASQS_SHIFT) & AQA_ASQS_MASK)
#define NVME_AQA_ACQS(aqa) ((aqa >> AQA_ACQS_SHIFT) & AQA_ACQS_MASK)

enum NvmeCmblocShift {
    CMBLOC_BIR_SHIFT     = 0,
    CMBLOC_CQMMS_SHIFT   = 3,
    CMBLOC_CQPDS_SHIFT   = 4,
    CMBLOC_CDPMLS_SHIFT  = 5,
    CMBLOC_CDPCILS_SHIFT = 6,
    CMBLOC_CDMMMS_SHIFT  = 7,
    CMBLOC_CQDA_SHIFT    = 8,
    CMBLOC_OFST_SHIFT    = 12,
};

enum NvmeCmblocMask {
    CMBLOC_BIR_MASK     = 0x7,
    CMBLOC_CQMMS_MASK   = 0x1,
    CMBLOC_CQPDS_MASK   = 0x1,
    CMBLOC_CDPMLS_MASK  = 0x1,
    CMBLOC_CDPCILS_MASK = 0x1,
    CMBLOC_CDMMMS_MASK  = 0x1,
    CMBLOC_CQDA_MASK    = 0x1,
    CMBLOC_OFST_MASK    = 0xfffff,
};

#define NVME_CMBLOC_BIR(cmbloc) \
    ((cmbloc >> CMBLOC_BIR_SHIFT) & CMBLOC_BIR_MASK)
#define NVME_CMBLOC_CQMMS(cmbloc) \
    ((cmbloc >> CMBLOC_CQMMS_SHIFT) & CMBLOC_CQMMS_MASK)
#define NVME_CMBLOC_CQPDS(cmbloc) \
    ((cmbloc >> CMBLOC_CQPDS_SHIFT) & CMBLOC_CQPDS_MASK)
#define NVME_CMBLOC_CDPMLS(cmbloc) \
    ((cmbloc >> CMBLOC_CDPMLS_SHIFT) & CMBLOC_CDPMLS_MASK)
#define NVME_CMBLOC_CDPCILS(cmbloc) \
    ((cmbloc >> CMBLOC_CDPCILS_SHIFT) & CMBLOC_CDPCILS_MASK)
#define NVME_CMBLOC_CDMMMS(cmbloc) \
    ((cmbloc >> CMBLOC_CDMMMS_SHIFT) & CMBLOC_CDMMMS_MASK)
#define NVME_CMBLOC_CQDA(cmbloc) \
    ((cmbloc >> CMBLOC_CQDA_SHIFT) & CMBLOC_CQDA_MASK)
#define NVME_CMBLOC_OFST(cmbloc) \
    ((cmbloc >> CMBLOC_OFST_SHIFT) & CMBLOC_OFST_MASK)

#define NVME_CMBLOC_SET_BIR(cmbloc, val) \
    (cmbloc |= (uint64_t)(val & CMBLOC_BIR_MASK) << CMBLOC_BIR_SHIFT)
#define NVME_CMBLOC_SET_CQMMS(cmbloc, val) \
    (cmbloc |= (uint64_t)(val & CMBLOC_CQMMS_MASK) << CMBLOC_CQMMS_SHIFT)
#define NVME_CMBLOC_SET_CQPDS(cmbloc, val) \
    (cmbloc |= (uint64_t)(val & CMBLOC_CQPDS_MASK) << CMBLOC_CQPDS_SHIFT)
#define NVME_CMBLOC_SET_CDPMLS(cmbloc, val) \
    (cmbloc |= (uint64_t)(val & CMBLOC_CDPMLS_MASK) << CMBLOC_CDPMLS_SHIFT)
#define NVME_CMBLOC_SET_CDPCILS(cmbloc, val) \
    (cmbloc |= (uint64_t)(val & CMBLOC_CDPCILS_MASK) << CMBLOC_CDPCILS_SHIFT)
#define NVME_CMBLOC_SET_CDMMMS(cmbloc, val) \
    (cmbloc |= (uint64_t)(val & CMBLOC_CDMMMS_MASK) << CMBLOC_CDMMMS_SHIFT)
#define NVME_CMBLOC_SET_CQDA(cmbloc, val) \
    (cmbloc |= (uint64_t)(val & CMBLOC_CQDA_MASK) << CMBLOC_CQDA_SHIFT)
#define NVME_CMBLOC_SET_OFST(cmbloc, val) \
    (cmbloc |= (uint64_t)(val & CMBLOC_OFST_MASK) << CMBLOC_OFST_SHIFT)

#define NVME_CMBMSMC_SET_CRE (cmbmsc, val) \
    (cmbmsc |= (uint64_t)(val & CMBLOC_OFST_MASK) << CMBMSC_CRE_SHIFT)

enum NvmeCmbszShift {
    CMBSZ_SQS_SHIFT   = 0,
    CMBSZ_CQS_SHIFT   = 1,
    CMBSZ_LISTS_SHIFT = 2,
    CMBSZ_RDS_SHIFT   = 3,
    CMBSZ_WDS_SHIFT   = 4,
    CMBSZ_SZU_SHIFT   = 8,
    CMBSZ_SZ_SHIFT    = 12,
};

enum NvmeCmbszMask {
    CMBSZ_SQS_MASK   = 0x1,
    CMBSZ_CQS_MASK   = 0x1,
    CMBSZ_LISTS_MASK = 0x1,
    CMBSZ_RDS_MASK   = 0x1,
    CMBSZ_WDS_MASK   = 0x1,
    CMBSZ_SZU_MASK   = 0xf,
    CMBSZ_SZ_MASK    = 0xfffff,
};

#define NVME_CMBSZ_SQS(cmbsz)  ((cmbsz >> CMBSZ_SQS_SHIFT)   & CMBSZ_SQS_MASK)
#define NVME_CMBSZ_CQS(cmbsz)  ((cmbsz >> CMBSZ_CQS_SHIFT)   & CMBSZ_CQS_MASK)
#define NVME_CMBSZ_LISTS(cmbsz)((cmbsz >> CMBSZ_LISTS_SHIFT) & CMBSZ_LISTS_MASK)
#define NVME_CMBSZ_RDS(cmbsz)  ((cmbsz >> CMBSZ_RDS_SHIFT)   & CMBSZ_RDS_MASK)
#define NVME_CMBSZ_WDS(cmbsz)  ((cmbsz >> CMBSZ_WDS_SHIFT)   & CMBSZ_WDS_MASK)
#define NVME_CMBSZ_SZU(cmbsz)  ((cmbsz >> CMBSZ_SZU_SHIFT)   & CMBSZ_SZU_MASK)
#define NVME_CMBSZ_SZ(cmbsz)   ((cmbsz >> CMBSZ_SZ_SHIFT)    & CMBSZ_SZ_MASK)

#define NVME_CMBSZ_SET_SQS(cmbsz, val)   \
    (cmbsz |= (uint64_t)(val &  CMBSZ_SQS_MASK)  << CMBSZ_SQS_SHIFT)
#define NVME_CMBSZ_SET_CQS(cmbsz, val)   \
    (cmbsz |= (uint64_t)(val & CMBSZ_CQS_MASK) << CMBSZ_CQS_SHIFT)
#define NVME_CMBSZ_SET_LISTS(cmbsz, val) \
    (cmbsz |= (uint64_t)(val & CMBSZ_LISTS_MASK) << CMBSZ_LISTS_SHIFT)
#define NVME_CMBSZ_SET_RDS(cmbsz, val)   \
    (cmbsz |= (uint64_t)(val & CMBSZ_RDS_MASK) << CMBSZ_RDS_SHIFT)
#define NVME_CMBSZ_SET_WDS(cmbsz, val)   \
    (cmbsz |= (uint64_t)(val & CMBSZ_WDS_MASK) << CMBSZ_WDS_SHIFT)
#define NVME_CMBSZ_SET_SZU(cmbsz, val)   \
    (cmbsz |= (uint64_t)(val & CMBSZ_SZU_MASK) << CMBSZ_SZU_SHIFT)
#define NVME_CMBSZ_SET_SZ(cmbsz, val)    \
    (cmbsz |= (uint64_t)(val & CMBSZ_SZ_MASK) << CMBSZ_SZ_SHIFT)

#define NVME_CMBSZ_GETSIZE(cmbsz) \
    (NVME_CMBSZ_SZ(cmbsz) * (1 << (12 + 4 * NVME_CMBSZ_SZU(cmbsz))))

enum NvmeCmbmscShift {
    CMBMSC_CRE_SHIFT  = 0,
    CMBMSC_CMSE_SHIFT = 1,
    CMBMSC_CBA_SHIFT  = 12,
};

enum NvmeCmbmscMask {
    CMBMSC_CRE_MASK  = 0x1,
    CMBMSC_CMSE_MASK = 0x1,
    CMBMSC_CBA_MASK  = ((1ULL << 52) - 1),
};

#define NVME_CMBMSC_CRE(cmbmsc) \
    ((cmbmsc >> CMBMSC_CRE_SHIFT)  & CMBMSC_CRE_MASK)
#define NVME_CMBMSC_CMSE(cmbmsc) \
    ((cmbmsc >> CMBMSC_CMSE_SHIFT) & CMBMSC_CMSE_MASK)
#define NVME_CMBMSC_CBA(cmbmsc) \
    ((cmbmsc >> CMBMSC_CBA_SHIFT) & CMBMSC_CBA_MASK)


#define NVME_CMBMSC_SET_CRE(cmbmsc, val)  \
    (cmbmsc |= (uint64_t)(val & CMBMSC_CRE_MASK) << CMBMSC_CRE_SHIFT)
#define NVME_CMBMSC_SET_CMSE(cmbmsc, val) \
    (cmbmsc |= (uint64_t)(val & CMBMSC_CMSE_MASK) << CMBMSC_CMSE_SHIFT)
#define NVME_CMBMSC_SET_CBA(cmbmsc, val) \
    (cmbmsc |= (uint64_t)(val & CMBMSC_CBA_MASK) << CMBMSC_CBA_SHIFT)

enum NvmeCmbstsShift {
    CMBSTS_CBAI_SHIFT = 0,
};
enum NvmeCmbstsMask {
    CMBSTS_CBAI_MASK = 0x1,
};

#define NVME_CMBSTS_CBAI(cmbsts) \
    ((cmbsts >> CMBSTS_CBAI_SHIFT) & CMBSTS_CBAI_MASK)

#define NVME_CMBSTS_SET_CBAI(cmbsts, val)  \
    (cmbsts |= (uint64_t)(val & CMBSTS_CBAI_MASK) << CMBSTS_CBAI_SHIFT)

enum NvmePmrcapShift {
    PMRCAP_RDS_SHIFT      = 3,
    PMRCAP_WDS_SHIFT      = 4,
    PMRCAP_BIR_SHIFT      = 5,
    PMRCAP_PMRTU_SHIFT    = 8,
    PMRCAP_PMRWBM_SHIFT   = 10,
    PMRCAP_PMRTO_SHIFT    = 16,
    PMRCAP_CMSS_SHIFT     = 24,
};

enum NvmePmrcapMask {
    PMRCAP_RDS_MASK      = 0x1,
    PMRCAP_WDS_MASK      = 0x1,
    PMRCAP_BIR_MASK      = 0x7,
    PMRCAP_PMRTU_MASK    = 0x3,
    PMRCAP_PMRWBM_MASK   = 0xf,
    PMRCAP_PMRTO_MASK    = 0xff,
    PMRCAP_CMSS_MASK     = 0x1,
};

#define NVME_PMRCAP_RDS(pmrcap)    \
    ((pmrcap >> PMRCAP_RDS_SHIFT)   & PMRCAP_RDS_MASK)
#define NVME_PMRCAP_WDS(pmrcap)    \
    ((pmrcap >> PMRCAP_WDS_SHIFT)   & PMRCAP_WDS_MASK)
#define NVME_PMRCAP_BIR(pmrcap)    \
    ((pmrcap >> PMRCAP_BIR_SHIFT)   & PMRCAP_BIR_MASK)
#define NVME_PMRCAP_PMRTU(pmrcap)    \
    ((pmrcap >> PMRCAP_PMRTU_SHIFT)   & PMRCAP_PMRTU_MASK)
#define NVME_PMRCAP_PMRWBM(pmrcap)    \
    ((pmrcap >> PMRCAP_PMRWBM_SHIFT)   & PMRCAP_PMRWBM_MASK)
#define NVME_PMRCAP_PMRTO(pmrcap)    \
    ((pmrcap >> PMRCAP_PMRTO_SHIFT)   & PMRCAP_PMRTO_MASK)
#define NVME_PMRCAP_CMSS(pmrcap)    \
    ((pmrcap >> PMRCAP_CMSS_SHIFT)   & PMRCAP_CMSS_MASK)

#define NVME_PMRCAP_SET_RDS(pmrcap, val)   \
    (pmrcap |= (uint64_t)(val & PMRCAP_RDS_MASK) << PMRCAP_RDS_SHIFT)
#define NVME_PMRCAP_SET_WDS(pmrcap, val)   \
    (pmrcap |= (uint64_t)(val & PMRCAP_WDS_MASK) << PMRCAP_WDS_SHIFT)
#define NVME_PMRCAP_SET_BIR(pmrcap, val)   \
    (pmrcap |= (uint64_t)(val & PMRCAP_BIR_MASK) << PMRCAP_BIR_SHIFT)
#define NVME_PMRCAP_SET_PMRTU(pmrcap, val)   \
    (pmrcap |= (uint64_t)(val & PMRCAP_PMRTU_MASK) << PMRCAP_PMRTU_SHIFT)
#define NVME_PMRCAP_SET_PMRWBM(pmrcap, val)   \
    (pmrcap |= (uint64_t)(val & PMRCAP_PMRWBM_MASK) << PMRCAP_PMRWBM_SHIFT)
#define NVME_PMRCAP_SET_PMRTO(pmrcap, val)   \
    (pmrcap |= (uint64_t)(val & PMRCAP_PMRTO_MASK) << PMRCAP_PMRTO_SHIFT)
#define NVME_PMRCAP_SET_CMSS(pmrcap, val)   \
    (pmrcap |= (uint64_t)(val & PMRCAP_CMSS_MASK) << PMRCAP_CMSS_SHIFT)

enum NvmePmrctlShift {
    PMRCTL_EN_SHIFT   = 0,
};

enum NvmePmrctlMask {
    PMRCTL_EN_MASK   = 0x1,
};

#define NVME_PMRCTL_EN(pmrctl)  ((pmrctl >> PMRCTL_EN_SHIFT)   & PMRCTL_EN_MASK)

#define NVME_PMRCTL_SET_EN(pmrctl, val)   \
    (pmrctl |= (uint64_t)(val & PMRCTL_EN_MASK) << PMRCTL_EN_SHIFT)

enum NvmePmrstsShift {
    PMRSTS_ERR_SHIFT    = 0,
    PMRSTS_NRDY_SHIFT   = 8,
    PMRSTS_HSTS_SHIFT   = 9,
    PMRSTS_CBAI_SHIFT   = 12,
};

enum NvmePmrstsMask {
    PMRSTS_ERR_MASK    = 0xff,
    PMRSTS_NRDY_MASK   = 0x1,
    PMRSTS_HSTS_MASK   = 0x7,
    PMRSTS_CBAI_MASK   = 0x1,
};

#define NVME_PMRSTS_ERR(pmrsts)     \
    ((pmrsts >> PMRSTS_ERR_SHIFT)   & PMRSTS_ERR_MASK)
#define NVME_PMRSTS_NRDY(pmrsts)    \
    ((pmrsts >> PMRSTS_NRDY_SHIFT)   & PMRSTS_NRDY_MASK)
#define NVME_PMRSTS_HSTS(pmrsts)    \
    ((pmrsts >> PMRSTS_HSTS_SHIFT)   & PMRSTS_HSTS_MASK)
#define NVME_PMRSTS_CBAI(pmrsts)    \
    ((pmrsts >> PMRSTS_CBAI_SHIFT)   & PMRSTS_CBAI_MASK)

#define NVME_PMRSTS_SET_ERR(pmrsts, val)   \
    (pmrsts |= (uint64_t)(val & PMRSTS_ERR_MASK) << PMRSTS_ERR_SHIFT)
#define NVME_PMRSTS_SET_NRDY(pmrsts, val)   \
    (pmrsts |= (uint64_t)(val & PMRSTS_NRDY_MASK) << PMRSTS_NRDY_SHIFT)
#define NVME_PMRSTS_SET_HSTS(pmrsts, val)   \
    (pmrsts |= (uint64_t)(val & PMRSTS_HSTS_MASK) << PMRSTS_HSTS_SHIFT)
#define NVME_PMRSTS_SET_CBAI(pmrsts, val)   \
    (pmrsts |= (uint64_t)(val & PMRSTS_CBAI_MASK) << PMRSTS_CBAI_SHIFT)

enum NvmePmrebsShift {
    PMREBS_PMRSZU_SHIFT   = 0,
    PMREBS_RBB_SHIFT      = 4,
    PMREBS_PMRWBZ_SHIFT   = 8,
};

enum NvmePmrebsMask {
    PMREBS_PMRSZU_MASK   = 0xf,
    PMREBS_RBB_MASK      = 0x1,
    PMREBS_PMRWBZ_MASK   = 0xffffff,
};

#define NVME_PMREBS_PMRSZU(pmrebs)  \
    ((pmrebs >> PMREBS_PMRSZU_SHIFT)   & PMREBS_PMRSZU_MASK)
#define NVME_PMREBS_RBB(pmrebs)     \
    ((pmrebs >> PMREBS_RBB_SHIFT)   & PMREBS_RBB_MASK)
#define NVME_PMREBS_PMRWBZ(pmrebs)  \
    ((pmrebs >> PMREBS_PMRWBZ_SHIFT)   & PMREBS_PMRWBZ_MASK)

#define NVME_PMREBS_SET_PMRSZU(pmrebs, val)   \
    (pmrebs |= (uint64_t)(val & PMREBS_PMRSZU_MASK) << PMREBS_PMRSZU_SHIFT)
#define NVME_PMREBS_SET_RBB(pmrebs, val)   \
    (pmrebs |= (uint64_t)(val & PMREBS_RBB_MASK) << PMREBS_RBB_SHIFT)
#define NVME_PMREBS_SET_PMRWBZ(pmrebs, val)   \
    (pmrebs |= (uint64_t)(val & PMREBS_PMRWBZ_MASK) << PMREBS_PMRWBZ_SHIFT)

enum NvmePmrswtpShift {
    PMRSWTP_PMRSWTU_SHIFT   = 0,
    PMRSWTP_PMRSWTV_SHIFT   = 8,
};

enum NvmePmrswtpMask {
    PMRSWTP_PMRSWTU_MASK   = 0xf,
    PMRSWTP_PMRSWTV_MASK   = 0xffffff,
};

#define NVME_PMRSWTP_PMRSWTU(pmrswtp)   \
    ((pmrswtp >> PMRSWTP_PMRSWTU_SHIFT)   & PMRSWTP_PMRSWTU_MASK)
#define NVME_PMRSWTP_PMRSWTV(pmrswtp)   \
    ((pmrswtp >> PMRSWTP_PMRSWTV_SHIFT)   & PMRSWTP_PMRSWTV_MASK)

#define NVME_PMRSWTP_SET_PMRSWTU(pmrswtp, val)   \
    (pmrswtp |= (uint64_t)(val & PMRSWTP_PMRSWTU_MASK) << PMRSWTP_PMRSWTU_SHIFT)
#define NVME_PMRSWTP_SET_PMRSWTV(pmrswtp, val)   \
    (pmrswtp |= (uint64_t)(val & PMRSWTP_PMRSWTV_MASK) << PMRSWTP_PMRSWTV_SHIFT)

enum NvmePmrmsclShift {
    PMRMSCL_CMSE_SHIFT   = 1,
    PMRMSCL_CBA_SHIFT    = 12,
};

enum NvmePmrmsclMask {
    PMRMSCL_CMSE_MASK   = 0x1,
    PMRMSCL_CBA_MASK    = 0xfffff,
};

#define NVME_PMRMSCL_CMSE(pmrmscl)    \
    ((pmrmscl >> PMRMSCL_CMSE_SHIFT)   & PMRMSCL_CMSE_MASK)
#define NVME_PMRMSCL_CBA(pmrmscl)     \
    ((pmrmscl >> PMRMSCL_CBA_SHIFT)   & PMRMSCL_CBA_MASK)

#define NVME_PMRMSCL_SET_CMSE(pmrmscl, val)   \
    (pmrmscl |= (uint32_t)(val & PMRMSCL_CMSE_MASK) << PMRMSCL_CMSE_SHIFT)
#define NVME_PMRMSCL_SET_CBA(pmrmscl, val)   \
    (pmrmscl |= (uint32_t)(val & PMRMSCL_CBA_MASK) << PMRMSCL_CBA_SHIFT)

enum NvmeSglDescriptorType {
    NVME_SGL_DESCR_TYPE_DATA_BLOCK          = 0x0,
    NVME_SGL_DESCR_TYPE_BIT_BUCKET          = 0x1,
    NVME_SGL_DESCR_TYPE_SEGMENT             = 0x2,
    NVME_SGL_DESCR_TYPE_LAST_SEGMENT        = 0x3,
    NVME_SGL_DESCR_TYPE_KEYED_DATA_BLOCK    = 0x4,

    NVME_SGL_DESCR_TYPE_VENDOR_SPECIFIC     = 0xf,
};

enum NvmeSglDescriptorSubtype {
    NVME_SGL_DESCR_SUBTYPE_ADDRESS = 0x0,
};

typedef struct QEMU_PACKED NvmeSglDescriptor {
    uint64_t addr;
    uint32_t len;
    uint8_t  rsvd[3];
    uint8_t  type;
} NvmeSglDescriptor;

#define NVME_SGL_TYPE(type)     ((type >> 4) & 0xf)
#define NVME_SGL_SUBTYPE(type)  (type & 0xf)

typedef union NvmeCmdDptr {
    struct {
        uint64_t    prp1;
        uint64_t    prp2;
    };

    NvmeSglDescriptor sgl;
} NvmeCmdDptr;

enum NvmePsdt {
    NVME_PSDT_PRP                 = 0x0,
    NVME_PSDT_SGL_MPTR_CONTIGUOUS = 0x1,
    NVME_PSDT_SGL_MPTR_SGL        = 0x2,
};

typedef struct QEMU_PACKED NvmeCmd {
    uint8_t     opcode;
    uint8_t     flags;
    uint16_t    cid;
    uint32_t    nsid;
    uint64_t    res1;
    uint64_t    mptr;
    NvmeCmdDptr dptr;
    uint32_t    cdw10;
    uint32_t    cdw11;
    uint32_t    cdw12;
    uint32_t    cdw13;
    uint32_t    cdw14;
    uint32_t    cdw15;
} NvmeCmd;

#define NVME_CMD_FLAGS_FUSE(flags) (flags & 0x3)
#define NVME_CMD_FLAGS_PSDT(flags) ((flags >> 6) & 0x3)

enum NvmeAdminCommands {
    NVME_ADM_CMD_DELETE_SQ      = 0x00,
    NVME_ADM_CMD_CREATE_SQ      = 0x01,
    NVME_ADM_CMD_GET_LOG_PAGE   = 0x02,
    NVME_ADM_CMD_DELETE_CQ      = 0x04,
    NVME_ADM_CMD_CREATE_CQ      = 0x05,
    NVME_ADM_CMD_IDENTIFY       = 0x06,
    NVME_ADM_CMD_ABORT          = 0x08,
    NVME_ADM_CMD_SET_FEATURES   = 0x09,
    NVME_ADM_CMD_GET_FEATURES   = 0x0a,
    NVME_ADM_CMD_ASYNC_EV_REQ   = 0x0c,
    NVME_ADM_CMD_ACTIVATE_FW    = 0x10,
    NVME_ADM_CMD_DOWNLOAD_FW    = 0x11,
    NVME_ADM_CMD_NS_ATTACHMENT  = 0x15,
    NVME_ADM_CMD_VIRT_MNGMT     = 0x1c,
    NVME_ADM_CMD_DBBUF_CONFIG   = 0x7c,
    NVME_ADM_CMD_FORMAT_NVM     = 0x80,
    NVME_ADM_CMD_SECURITY_SEND  = 0x81,
    NVME_ADM_CMD_SECURITY_RECV  = 0x82,
};

enum NvmeIoCommands {
    NVME_CMD_FLUSH              = 0x00,
    NVME_CMD_WRITE              = 0x01,
    NVME_CMD_READ               = 0x02,
    NVME_CMD_WRITE_UNCOR        = 0x04,
    NVME_CMD_COMPARE            = 0x05,
    NVME_CMD_WRITE_ZEROES       = 0x08,
    NVME_CMD_DSM                = 0x09,
    NVME_CMD_VERIFY             = 0x0c,
    NVME_CMD_COPY               = 0x19,
    NVME_CMD_ZONE_MGMT_SEND     = 0x79,
    NVME_CMD_ZONE_MGMT_RECV     = 0x7a,
    NVME_CMD_ZONE_APPEND        = 0x7d,
};

typedef struct QEMU_PACKED NvmeDeleteQ {
    uint8_t     opcode;
    uint8_t     flags;
    uint16_t    cid;
    uint32_t    rsvd1[9];
    uint16_t    qid;
    uint16_t    rsvd10;
    uint32_t    rsvd11[5];
} NvmeDeleteQ;

typedef struct QEMU_PACKED NvmeCreateCq {
    uint8_t     opcode;
    uint8_t     flags;
    uint16_t    cid;
    uint32_t    rsvd1[5];
    uint64_t    prp1;
    uint64_t    rsvd8;
    uint16_t    cqid;
    uint16_t    qsize;
    uint16_t    cq_flags;
    uint16_t    irq_vector;
    uint32_t    rsvd12[4];
} NvmeCreateCq;

#define NVME_CQ_FLAGS_PC(cq_flags)  (cq_flags & 0x1)
#define NVME_CQ_FLAGS_IEN(cq_flags) ((cq_flags >> 1) & 0x1)

enum NvmeFlagsCq {
    NVME_CQ_PC          = 1,
    NVME_CQ_IEN         = 2,
};

typedef struct QEMU_PACKED NvmeCreateSq {
    uint8_t     opcode;
    uint8_t     flags;
    uint16_t    cid;
    uint32_t    rsvd1[5];
    uint64_t    prp1;
    uint64_t    rsvd8;
    uint16_t    sqid;
    uint16_t    qsize;
    uint16_t    sq_flags;
    uint16_t    cqid;
    uint32_t    rsvd12[4];
} NvmeCreateSq;

#define NVME_SQ_FLAGS_PC(sq_flags)      (sq_flags & 0x1)
#define NVME_SQ_FLAGS_QPRIO(sq_flags)   ((sq_flags >> 1) & 0x3)

enum NvmeFlagsSq {
    NVME_SQ_PC          = 1,

    NVME_SQ_PRIO_URGENT = 0,
    NVME_SQ_PRIO_HIGH   = 1,
    NVME_SQ_PRIO_NORMAL = 2,
    NVME_SQ_PRIO_LOW    = 3,
};

typedef struct QEMU_PACKED NvmeIdentify {
    uint8_t     opcode;
    uint8_t     flags;
    uint16_t    cid;
    uint32_t    nsid;
    uint64_t    rsvd2[2];
    uint64_t    prp1;
    uint64_t    prp2;
    uint8_t     cns;
    uint8_t     rsvd10;
    uint16_t    ctrlid;
    uint16_t    nvmsetid;
    uint8_t     rsvd11;
    uint8_t     csi;
    uint32_t    rsvd12[4];
} NvmeIdentify;

typedef struct QEMU_PACKED NvmeRwCmd {
    uint8_t     opcode;
    uint8_t     flags;
    uint16_t    cid;
    uint32_t    nsid;
    uint32_t    cdw2;
    uint32_t    cdw3;
    uint64_t    mptr;
    NvmeCmdDptr dptr;
    uint64_t    slba;
    uint16_t    nlb;
    uint16_t    control;
    uint32_t    dsmgmt;
    uint32_t    reftag;
    uint16_t    apptag;
    uint16_t    appmask;
} NvmeRwCmd;

enum {
    NVME_RW_LR                  = 1 << 15,
    NVME_RW_FUA                 = 1 << 14,
    NVME_RW_DSM_FREQ_UNSPEC     = 0,
    NVME_RW_DSM_FREQ_TYPICAL    = 1,
    NVME_RW_DSM_FREQ_RARE       = 2,
    NVME_RW_DSM_FREQ_READS      = 3,
    NVME_RW_DSM_FREQ_WRITES     = 4,
    NVME_RW_DSM_FREQ_RW         = 5,
    NVME_RW_DSM_FREQ_ONCE       = 6,
    NVME_RW_DSM_FREQ_PREFETCH   = 7,
    NVME_RW_DSM_FREQ_TEMP       = 8,
    NVME_RW_DSM_LATENCY_NONE    = 0 << 4,
    NVME_RW_DSM_LATENCY_IDLE    = 1 << 4,
    NVME_RW_DSM_LATENCY_NORM    = 2 << 4,
    NVME_RW_DSM_LATENCY_LOW     = 3 << 4,
    NVME_RW_DSM_SEQ_REQ         = 1 << 6,
    NVME_RW_DSM_COMPRESSED      = 1 << 7,
    NVME_RW_PIREMAP             = 1 << 9,
    NVME_RW_PRINFO_PRACT        = 1 << 13,
    NVME_RW_PRINFO_PRCHK_GUARD  = 1 << 12,
    NVME_RW_PRINFO_PRCHK_APP    = 1 << 11,
    NVME_RW_PRINFO_PRCHK_REF    = 1 << 10,
    NVME_RW_PRINFO_PRCHK_MASK   = 7 << 10,
};

#define NVME_RW_PRINFO(control) ((control >> 10) & 0xf)

enum {
    NVME_PRINFO_PRACT       = 1 << 3,
    NVME_PRINFO_PRCHK_GUARD = 1 << 2,
    NVME_PRINFO_PRCHK_APP   = 1 << 1,
    NVME_PRINFO_PRCHK_REF   = 1 << 0,
    NVME_PRINFO_PRCHK_MASK  = 7 << 0,
};

typedef struct QEMU_PACKED NvmeDsmCmd {
    uint8_t     opcode;
    uint8_t     flags;
    uint16_t    cid;
    uint32_t    nsid;
    uint64_t    rsvd2[2];
    NvmeCmdDptr dptr;
    uint32_t    nr;
    uint32_t    attributes;
    uint32_t    rsvd12[4];
} NvmeDsmCmd;

enum {
    NVME_DSMGMT_IDR = 1 << 0,
    NVME_DSMGMT_IDW = 1 << 1,
    NVME_DSMGMT_AD  = 1 << 2,
};

typedef struct QEMU_PACKED NvmeDsmRange {
    uint32_t    cattr;
    uint32_t    nlb;
    uint64_t    slba;
} NvmeDsmRange;

enum {
    NVME_COPY_FORMAT_0 = 0x0,
    NVME_COPY_FORMAT_1 = 0x1,
};

typedef struct QEMU_PACKED NvmeCopyCmd {
    uint8_t     opcode;
    uint8_t     flags;
    uint16_t    cid;
    uint32_t    nsid;
    uint32_t    cdw2;
    uint32_t    cdw3;
    uint32_t    rsvd2[2];
    NvmeCmdDptr dptr;
    uint64_t    sdlba;
    uint8_t     nr;
    uint8_t     control[3];
    uint16_t    rsvd13;
    uint16_t    dspec;
    uint32_t    reftag;
    uint16_t    apptag;
    uint16_t    appmask;
} NvmeCopyCmd;

typedef struct QEMU_PACKED NvmeCopySourceRangeFormat0 {
    uint8_t  rsvd0[8];
    uint64_t slba;
    uint16_t nlb;
    uint8_t  rsvd18[6];
    uint32_t reftag;
    uint16_t apptag;
    uint16_t appmask;
} NvmeCopySourceRangeFormat0;

typedef struct QEMU_PACKED NvmeCopySourceRangeFormat1 {
    uint8_t  rsvd0[8];
    uint64_t slba;
    uint16_t nlb;
    uint8_t  rsvd18[8];
    uint8_t  sr[10];
    uint16_t apptag;
    uint16_t appmask;
} NvmeCopySourceRangeFormat1;

enum NvmeAsyncEventRequest {
    NVME_AER_TYPE_ERROR                     = 0,
    NVME_AER_TYPE_SMART                     = 1,
    NVME_AER_TYPE_NOTICE                    = 2,
    NVME_AER_TYPE_IO_SPECIFIC               = 6,
    NVME_AER_TYPE_VENDOR_SPECIFIC           = 7,
    NVME_AER_INFO_ERR_INVALID_DB_REGISTER   = 0,
    NVME_AER_INFO_ERR_INVALID_DB_VALUE      = 1,
    NVME_AER_INFO_ERR_DIAG_FAIL             = 2,
    NVME_AER_INFO_ERR_PERS_INTERNAL_ERR     = 3,
    NVME_AER_INFO_ERR_TRANS_INTERNAL_ERR    = 4,
    NVME_AER_INFO_ERR_FW_IMG_LOAD_ERR       = 5,
    NVME_AER_INFO_SMART_RELIABILITY         = 0,
    NVME_AER_INFO_SMART_TEMP_THRESH         = 1,
    NVME_AER_INFO_SMART_SPARE_THRESH        = 2,
    NVME_AER_INFO_NOTICE_NS_ATTR_CHANGED    = 0,
};

typedef struct QEMU_PACKED NvmeAerResult {
    uint8_t event_type;
    uint8_t event_info;
    uint8_t log_page;
    uint8_t resv;
} NvmeAerResult;

typedef struct QEMU_PACKED NvmeZonedResult {
    uint64_t slba;
} NvmeZonedResult;

typedef struct QEMU_PACKED NvmeCqe {
    uint32_t    result;
    uint32_t    dw1;
    uint16_t    sq_head;
    uint16_t    sq_id;
    uint16_t    cid;
    uint16_t    status;
} NvmeCqe;

enum NvmeStatusCodes {
    NVME_SUCCESS                = 0x0000,
    NVME_INVALID_OPCODE         = 0x0001,
    NVME_INVALID_FIELD          = 0x0002,
    NVME_CID_CONFLICT           = 0x0003,
    NVME_DATA_TRAS_ERROR        = 0x0004,
    NVME_POWER_LOSS_ABORT       = 0x0005,
    NVME_INTERNAL_DEV_ERROR     = 0x0006,
    NVME_CMD_ABORT_REQ          = 0x0007,
    NVME_CMD_ABORT_SQ_DEL       = 0x0008,
    NVME_CMD_ABORT_FAILED_FUSE  = 0x0009,
    NVME_CMD_ABORT_MISSING_FUSE = 0x000a,
    NVME_INVALID_NSID           = 0x000b,
    NVME_CMD_SEQ_ERROR          = 0x000c,
    NVME_INVALID_SGL_SEG_DESCR  = 0x000d,
    NVME_INVALID_NUM_SGL_DESCRS = 0x000e,
    NVME_DATA_SGL_LEN_INVALID   = 0x000f,
    NVME_MD_SGL_LEN_INVALID     = 0x0010,
    NVME_SGL_DESCR_TYPE_INVALID = 0x0011,
    NVME_INVALID_USE_OF_CMB     = 0x0012,
    NVME_INVALID_PRP_OFFSET     = 0x0013,
    NVME_CMD_SET_CMB_REJECTED   = 0x002b,
    NVME_INVALID_CMD_SET        = 0x002c,
    NVME_LBA_RANGE              = 0x0080,
    NVME_CAP_EXCEEDED           = 0x0081,
    NVME_NS_NOT_READY           = 0x0082,
    NVME_NS_RESV_CONFLICT       = 0x0083,
    NVME_FORMAT_IN_PROGRESS     = 0x0084,
    NVME_INVALID_CQID           = 0x0100,
    NVME_INVALID_QID            = 0x0101,
    NVME_MAX_QSIZE_EXCEEDED     = 0x0102,
    NVME_ACL_EXCEEDED           = 0x0103,
    NVME_RESERVED               = 0x0104,
    NVME_AER_LIMIT_EXCEEDED     = 0x0105,
    NVME_INVALID_FW_SLOT        = 0x0106,
    NVME_INVALID_FW_IMAGE       = 0x0107,
    NVME_INVALID_IRQ_VECTOR     = 0x0108,
    NVME_INVALID_LOG_ID         = 0x0109,
    NVME_INVALID_FORMAT         = 0x010a,
    NVME_FW_REQ_RESET           = 0x010b,
    NVME_INVALID_QUEUE_DEL      = 0x010c,
    NVME_FID_NOT_SAVEABLE       = 0x010d,
    NVME_FEAT_NOT_CHANGEABLE    = 0x010e,
    NVME_FEAT_NOT_NS_SPEC       = 0x010f,
    NVME_FW_REQ_SUSYSTEM_RESET  = 0x0110,
    NVME_NS_ALREADY_ATTACHED    = 0x0118,
    NVME_NS_PRIVATE             = 0x0119,
    NVME_NS_NOT_ATTACHED        = 0x011a,
    NVME_NS_CTRL_LIST_INVALID   = 0x011c,
    NVME_INVALID_CTRL_ID        = 0x011f,
    NVME_INVALID_SEC_CTRL_STATE = 0x0120,
    NVME_INVALID_NUM_RESOURCES  = 0x0121,
    NVME_INVALID_RESOURCE_ID    = 0x0122,
    NVME_CONFLICTING_ATTRS      = 0x0180,
    NVME_INVALID_PROT_INFO      = 0x0181,
    NVME_WRITE_TO_RO            = 0x0182,
    NVME_CMD_SIZE_LIMIT         = 0x0183,
    NVME_INVALID_ZONE_OP        = 0x01b6,
    NVME_NOZRWA                 = 0x01b7,
    NVME_ZONE_BOUNDARY_ERROR    = 0x01b8,
    NVME_ZONE_FULL              = 0x01b9,
    NVME_ZONE_READ_ONLY         = 0x01ba,
    NVME_ZONE_OFFLINE           = 0x01bb,
    NVME_ZONE_INVALID_WRITE     = 0x01bc,
    NVME_ZONE_TOO_MANY_ACTIVE   = 0x01bd,
    NVME_ZONE_TOO_MANY_OPEN     = 0x01be,
    NVME_ZONE_INVAL_TRANSITION  = 0x01bf,
    NVME_WRITE_FAULT            = 0x0280,
    NVME_UNRECOVERED_READ       = 0x0281,
    NVME_E2E_GUARD_ERROR        = 0x0282,
    NVME_E2E_APP_ERROR          = 0x0283,
    NVME_E2E_REF_ERROR          = 0x0284,
    NVME_CMP_FAILURE            = 0x0285,
    NVME_ACCESS_DENIED          = 0x0286,
    NVME_DULB                   = 0x0287,
    NVME_E2E_STORAGE_TAG_ERROR  = 0x0288,
    NVME_MORE                   = 0x2000,
    NVME_DNR                    = 0x4000,
    NVME_NO_COMPLETE            = 0xffff,
};

typedef struct QEMU_PACKED NvmeFwSlotInfoLog {
    uint8_t     afi;
    uint8_t     reserved1[7];
    uint8_t     frs1[8];
    uint8_t     frs2[8];
    uint8_t     frs3[8];
    uint8_t     frs4[8];
    uint8_t     frs5[8];
    uint8_t     frs6[8];
    uint8_t     frs7[8];
    uint8_t     reserved2[448];
} NvmeFwSlotInfoLog;

typedef struct QEMU_PACKED NvmeErrorLog {
    uint64_t    error_count;
    uint16_t    sqid;
    uint16_t    cid;
    uint16_t    status_field;
    uint16_t    param_error_location;
    uint64_t    lba;
    uint32_t    nsid;
    uint8_t     vs;
    uint8_t     resv[35];
} NvmeErrorLog;

typedef struct QEMU_PACKED NvmeSmartLog {
    uint8_t     critical_warning;
    uint16_t    temperature;
    uint8_t     available_spare;
    uint8_t     available_spare_threshold;
    uint8_t     percentage_used;
    uint8_t     reserved1[26];
    uint64_t    data_units_read[2];
    uint64_t    data_units_written[2];
    uint64_t    host_read_commands[2];
    uint64_t    host_write_commands[2];
    uint64_t    controller_busy_time[2];
    uint64_t    power_cycles[2];
    uint64_t    power_on_hours[2];
    uint64_t    unsafe_shutdowns[2];
    uint64_t    media_errors[2];
    uint64_t    number_of_error_log_entries[2];
    uint8_t     reserved2[320];
} NvmeSmartLog;

#define NVME_SMART_WARN_MAX     6
enum NvmeSmartWarn {
    NVME_SMART_SPARE                  = 1 << 0,
    NVME_SMART_TEMPERATURE            = 1 << 1,
    NVME_SMART_RELIABILITY            = 1 << 2,
    NVME_SMART_MEDIA_READ_ONLY        = 1 << 3,
    NVME_SMART_FAILED_VOLATILE_MEDIA  = 1 << 4,
    NVME_SMART_PMR_UNRELIABLE         = 1 << 5,
};

typedef struct NvmeEffectsLog {
    uint32_t    acs[256];
    uint32_t    iocs[256];
    uint8_t     resv[2048];
} NvmeEffectsLog;

enum {
    NVME_CMD_EFF_CSUPP      = 1 << 0,
    NVME_CMD_EFF_LBCC       = 1 << 1,
    NVME_CMD_EFF_NCC        = 1 << 2,
    NVME_CMD_EFF_NIC        = 1 << 3,
    NVME_CMD_EFF_CCC        = 1 << 4,
    NVME_CMD_EFF_CSE_MASK   = 3 << 16,
    NVME_CMD_EFF_UUID_SEL   = 1 << 19,
};

enum NvmeLogIdentifier {
    NVME_LOG_ERROR_INFO     = 0x01,
    NVME_LOG_SMART_INFO     = 0x02,
    NVME_LOG_FW_SLOT_INFO   = 0x03,
    NVME_LOG_CHANGED_NSLIST = 0x04,
    NVME_LOG_CMD_EFFECTS    = 0x05,
};

typedef struct QEMU_PACKED NvmePSD {
    uint16_t    mp;
    uint16_t    reserved;
    uint32_t    enlat;
    uint32_t    exlat;
    uint8_t     rrt;
    uint8_t     rrl;
    uint8_t     rwt;
    uint8_t     rwl;
    uint8_t     resv[16];
} NvmePSD;

#define NVME_CONTROLLER_LIST_SIZE 2048
#define NVME_IDENTIFY_DATA_SIZE 4096

enum NvmeIdCns {
    NVME_ID_CNS_NS                    = 0x00,
    NVME_ID_CNS_CTRL                  = 0x01,
    NVME_ID_CNS_NS_ACTIVE_LIST        = 0x02,
    NVME_ID_CNS_NS_DESCR_LIST         = 0x03,
    NVME_ID_CNS_CS_NS                 = 0x05,
    NVME_ID_CNS_CS_CTRL               = 0x06,
    NVME_ID_CNS_CS_NS_ACTIVE_LIST     = 0x07,
    NVME_ID_CNS_NS_PRESENT_LIST       = 0x10,
    NVME_ID_CNS_NS_PRESENT            = 0x11,
    NVME_ID_CNS_NS_ATTACHED_CTRL_LIST = 0x12,
    NVME_ID_CNS_CTRL_LIST             = 0x13,
    NVME_ID_CNS_PRIMARY_CTRL_CAP      = 0x14,
    NVME_ID_CNS_SECONDARY_CTRL_LIST   = 0x15,
    NVME_ID_CNS_CS_NS_PRESENT_LIST    = 0x1a,
    NVME_ID_CNS_CS_NS_PRESENT         = 0x1b,
    NVME_ID_CNS_IO_COMMAND_SET        = 0x1c,
};

typedef struct QEMU_PACKED NvmeIdCtrl {
    uint16_t    vid;
    uint16_t    ssvid;
    uint8_t     sn[20];
    uint8_t     mn[40];
    uint8_t     fr[8];
    uint8_t     rab;
    uint8_t     ieee[3];
    uint8_t     cmic;
    uint8_t     mdts;
    uint16_t    cntlid;
    uint32_t    ver;
    uint32_t    rtd3r;
    uint32_t    rtd3e;
    uint32_t    oaes;
    uint32_t    ctratt;
    uint8_t     rsvd100[11];
    uint8_t     cntrltype;
    uint8_t     fguid[16];
    uint8_t     rsvd128[128];
    uint16_t    oacs;
    uint8_t     acl;
    uint8_t     aerl;
    uint8_t     frmw;
    uint8_t     lpa;
    uint8_t     elpe;
    uint8_t     npss;
    uint8_t     avscc;
    uint8_t     apsta;
    uint16_t    wctemp;
    uint16_t    cctemp;
    uint16_t    mtfa;
    uint32_t    hmpre;
    uint32_t    hmmin;
    uint8_t     tnvmcap[16];
    uint8_t     unvmcap[16];
    uint32_t    rpmbs;
    uint16_t    edstt;
    uint8_t     dsto;
    uint8_t     fwug;
    uint16_t    kas;
    uint16_t    hctma;
    uint16_t    mntmt;
    uint16_t    mxtmt;
    uint32_t    sanicap;
    uint8_t     rsvd332[180];
    uint8_t     sqes;
    uint8_t     cqes;
    uint16_t    maxcmd;
    uint32_t    nn;
    uint16_t    oncs;
    uint16_t    fuses;
    uint8_t     fna;
    uint8_t     vwc;
    uint16_t    awun;
    uint16_t    awupf;
    uint8_t     nvscc;
    uint8_t     rsvd531;
    uint16_t    acwu;
    uint16_t    ocfs;
    uint32_t    sgls;
    uint8_t     rsvd540[228];
    uint8_t     subnqn[256];
    uint8_t     rsvd1024[1024];
    NvmePSD     psd[32];
    uint8_t     vs[1024];
} NvmeIdCtrl;

typedef struct NvmeIdCtrlZoned {
    uint8_t     zasl;
    uint8_t     rsvd1[4095];
} NvmeIdCtrlZoned;

typedef struct NvmeIdCtrlNvm {
    uint8_t     vsl;
    uint8_t     wzsl;
    uint8_t     wusl;
    uint8_t     dmrl;
    uint32_t    dmrsl;
    uint64_t    dmsl;
    uint8_t     rsvd16[4080];
} NvmeIdCtrlNvm;

enum NvmeIdCtrlOaes {
    NVME_OAES_NS_ATTR   = 1 << 8,
};

enum NvmeIdCtrlCtratt {
    NVME_CTRATT_ELBAS   = 1 << 15,
};

enum NvmeIdCtrlOacs {
    NVME_OACS_SECURITY  = 1 << 0,
    NVME_OACS_FORMAT    = 1 << 1,
    NVME_OACS_FW        = 1 << 2,
    NVME_OACS_NS_MGMT   = 1 << 3,
    NVME_OACS_DBBUF     = 1 << 8,
};

enum NvmeIdCtrlOncs {
    NVME_ONCS_COMPARE       = 1 << 0,
    NVME_ONCS_WRITE_UNCORR  = 1 << 1,
    NVME_ONCS_DSM           = 1 << 2,
    NVME_ONCS_WRITE_ZEROES  = 1 << 3,
    NVME_ONCS_FEATURES      = 1 << 4,
    NVME_ONCS_RESRVATIONS   = 1 << 5,
    NVME_ONCS_TIMESTAMP     = 1 << 6,
    NVME_ONCS_VERIFY        = 1 << 7,
    NVME_ONCS_COPY          = 1 << 8,
};

enum NvmeIdCtrlOcfs {
    NVME_OCFS_COPY_FORMAT_0 = 1 << NVME_COPY_FORMAT_0,
    NVME_OCFS_COPY_FORMAT_1 = 1 << NVME_COPY_FORMAT_1,
};

enum NvmeIdctrlVwc {
    NVME_VWC_PRESENT                    = 1 << 0,
    NVME_VWC_NSID_BROADCAST_NO_SUPPORT  = 0 << 1,
    NVME_VWC_NSID_BROADCAST_RESERVED    = 1 << 1,
    NVME_VWC_NSID_BROADCAST_CTRL_SPEC   = 2 << 1,
    NVME_VWC_NSID_BROADCAST_SUPPORT     = 3 << 1,
};

enum NvmeIdCtrlFrmw {
    NVME_FRMW_SLOT1_RO = 1 << 0,
};

enum NvmeIdCtrlLpa {
    NVME_LPA_NS_SMART = 1 << 0,
    NVME_LPA_CSE      = 1 << 1,
    NVME_LPA_EXTENDED = 1 << 2,
};

enum NvmeIdCtrlCmic {
    NVME_CMIC_MULTI_CTRL    = 1 << 1,
};

enum NvmeNsAttachmentOperation {
    NVME_NS_ATTACHMENT_ATTACH = 0x0,
    NVME_NS_ATTACHMENT_DETACH = 0x1,
};

#define NVME_CTRL_SQES_MIN(sqes) ((sqes) & 0xf)
#define NVME_CTRL_SQES_MAX(sqes) (((sqes) >> 4) & 0xf)
#define NVME_CTRL_CQES_MIN(cqes) ((cqes) & 0xf)
#define NVME_CTRL_CQES_MAX(cqes) (((cqes) >> 4) & 0xf)

#define NVME_CTRL_SGLS_SUPPORT_MASK        (0x3 <<  0)
#define NVME_CTRL_SGLS_SUPPORT_NO_ALIGN    (0x1 <<  0)
#define NVME_CTRL_SGLS_SUPPORT_DWORD_ALIGN (0x1 <<  1)
#define NVME_CTRL_SGLS_KEYED               (0x1 <<  2)
#define NVME_CTRL_SGLS_BITBUCKET           (0x1 << 16)
#define NVME_CTRL_SGLS_MPTR_CONTIGUOUS     (0x1 << 17)
#define NVME_CTRL_SGLS_EXCESS_LENGTH       (0x1 << 18)
#define NVME_CTRL_SGLS_MPTR_SGL            (0x1 << 19)
#define NVME_CTRL_SGLS_ADDR_OFFSET         (0x1 << 20)

#define NVME_ARB_AB(arb)    (arb & 0x7)
#define NVME_ARB_AB_NOLIMIT 0x7
#define NVME_ARB_LPW(arb)   ((arb >> 8) & 0xff)
#define NVME_ARB_MPW(arb)   ((arb >> 16) & 0xff)
#define NVME_ARB_HPW(arb)   ((arb >> 24) & 0xff)

#define NVME_INTC_THR(intc)     (intc & 0xff)
#define NVME_INTC_TIME(intc)    ((intc >> 8) & 0xff)

#define NVME_INTVC_NOCOALESCING (0x1 << 16)

#define NVME_TEMP_THSEL(temp)  ((temp >> 20) & 0x3)
#define NVME_TEMP_THSEL_OVER   0x0
#define NVME_TEMP_THSEL_UNDER  0x1

#define NVME_TEMP_TMPSEL(temp)     ((temp >> 16) & 0xf)
#define NVME_TEMP_TMPSEL_COMPOSITE 0x0

#define NVME_TEMP_TMPTH(temp) (temp & 0xffff)

#define NVME_AEC_SMART(aec)         (aec & 0xff)
#define NVME_AEC_NS_ATTR(aec)       ((aec >> 8) & 0x1)
#define NVME_AEC_FW_ACTIVATION(aec) ((aec >> 9) & 0x1)

#define NVME_ERR_REC_TLER(err_rec)  (err_rec & 0xffff)
#define NVME_ERR_REC_DULBE(err_rec) (err_rec & 0x10000)

enum NvmeFeatureIds {
    NVME_ARBITRATION                = 0x1,
    NVME_POWER_MANAGEMENT           = 0x2,
    NVME_LBA_RANGE_TYPE             = 0x3,
    NVME_TEMPERATURE_THRESHOLD      = 0x4,
    NVME_ERROR_RECOVERY             = 0x5,
    NVME_VOLATILE_WRITE_CACHE       = 0x6,
    NVME_NUMBER_OF_QUEUES           = 0x7,
    NVME_INTERRUPT_COALESCING       = 0x8,
    NVME_INTERRUPT_VECTOR_CONF      = 0x9,
    NVME_WRITE_ATOMICITY            = 0xa,
    NVME_ASYNCHRONOUS_EVENT_CONF    = 0xb,
    NVME_TIMESTAMP                  = 0xe,
    NVME_HOST_BEHAVIOR_SUPPORT      = 0x16,
    NVME_COMMAND_SET_PROFILE        = 0x19,
    NVME_SOFTWARE_PROGRESS_MARKER   = 0x80,
    NVME_FID_MAX                    = 0x100,
};

typedef enum NvmeFeatureCap {
    NVME_FEAT_CAP_SAVE      = 1 << 0,
    NVME_FEAT_CAP_NS        = 1 << 1,
    NVME_FEAT_CAP_CHANGE    = 1 << 2,
} NvmeFeatureCap;

typedef enum NvmeGetFeatureSelect {
    NVME_GETFEAT_SELECT_CURRENT = 0x0,
    NVME_GETFEAT_SELECT_DEFAULT = 0x1,
    NVME_GETFEAT_SELECT_SAVED   = 0x2,
    NVME_GETFEAT_SELECT_CAP     = 0x3,
} NvmeGetFeatureSelect;

#define NVME_GETSETFEAT_FID_MASK 0xff
#define NVME_GETSETFEAT_FID(dw10) (dw10 & NVME_GETSETFEAT_FID_MASK)

#define NVME_GETFEAT_SELECT_SHIFT 8
#define NVME_GETFEAT_SELECT_MASK  0x7
#define NVME_GETFEAT_SELECT(dw10) \
    ((dw10 >> NVME_GETFEAT_SELECT_SHIFT) & NVME_GETFEAT_SELECT_MASK)

#define NVME_SETFEAT_SAVE_SHIFT 31
#define NVME_SETFEAT_SAVE_MASK  0x1
#define NVME_SETFEAT_SAVE(dw10) \
    ((dw10 >> NVME_SETFEAT_SAVE_SHIFT) & NVME_SETFEAT_SAVE_MASK)

typedef struct QEMU_PACKED NvmeRangeType {
    uint8_t     type;
    uint8_t     attributes;
    uint8_t     rsvd2[14];
    uint64_t    slba;
    uint64_t    nlb;
    uint8_t     guid[16];
    uint8_t     rsvd48[16];
} NvmeRangeType;

typedef struct NvmeHostBehaviorSupport {
    uint8_t     acre;
    uint8_t     etdas;
    uint8_t     lbafee;
    uint8_t     rsvd3[509];
} NvmeHostBehaviorSupport;

typedef struct QEMU_PACKED NvmeLBAF {
    uint16_t    ms;
    uint8_t     ds;
    uint8_t     rp;
} NvmeLBAF;

typedef struct QEMU_PACKED NvmeLBAFE {
    uint64_t    zsze;
    uint8_t     zdes;
    uint8_t     rsvd9[7];
} NvmeLBAFE;

#define NVME_NSID_BROADCAST 0xffffffff
#define NVME_MAX_NLBAF 64

typedef struct QEMU_PACKED NvmeIdNs {
    uint64_t    nsze;
    uint64_t    ncap;
    uint64_t    nuse;
    uint8_t     nsfeat;
    uint8_t     nlbaf;
    uint8_t     flbas;
    uint8_t     mc;
    uint8_t     dpc;
    uint8_t     dps;
    uint8_t     nmic;
    uint8_t     rescap;
    uint8_t     fpi;
    uint8_t     dlfeat;
    uint16_t    nawun;
    uint16_t    nawupf;
    uint16_t    nacwu;
    uint16_t    nabsn;
    uint16_t    nabo;
    uint16_t    nabspf;
    uint16_t    noiob;
    uint8_t     nvmcap[16];
    uint16_t    npwg;
    uint16_t    npwa;
    uint16_t    npdg;
    uint16_t    npda;
    uint16_t    nows;
    uint16_t    mssrl;
    uint32_t    mcl;
    uint8_t     msrc;
    uint8_t     rsvd81[23];
    uint8_t     nguid[16];
    uint64_t    eui64;
    NvmeLBAF    lbaf[NVME_MAX_NLBAF];
    uint8_t     vs[3712];
} NvmeIdNs;

#define NVME_ID_NS_NVM_ELBAF_PIF(elbaf) (((elbaf) >> 7) & 0x3)

typedef struct QEMU_PACKED NvmeIdNsNvm {
    uint64_t    lbstm;
    uint8_t     pic;
    uint8_t     rsvd9[3];
    uint32_t    elbaf[NVME_MAX_NLBAF];
    uint8_t     rsvd268[3828];
} NvmeIdNsNvm;

typedef struct QEMU_PACKED NvmeIdNsDescr {
    uint8_t nidt;
    uint8_t nidl;
    uint8_t rsvd2[2];
} NvmeIdNsDescr;

enum NvmeNsIdentifierLength {
    NVME_NIDL_EUI64             = 8,
    NVME_NIDL_NGUID             = 16,
    NVME_NIDL_UUID              = 16,
    NVME_NIDL_CSI               = 1,
};

enum NvmeNsIdentifierType {
    NVME_NIDT_EUI64             = 0x01,
    NVME_NIDT_NGUID             = 0x02,
    NVME_NIDT_UUID              = 0x03,
    NVME_NIDT_CSI               = 0x04,
};

enum NvmeIdNsNmic {
    NVME_NMIC_NS_SHARED         = 1 << 0,
};

enum NvmeCsi {
    NVME_CSI_NVM                = 0x00,
    NVME_CSI_ZONED              = 0x02,
};

#define NVME_SET_CSI(vec, csi) (vec |= (uint8_t)(1 << (csi)))

typedef struct QEMU_PACKED NvmeIdNsZoned {
    uint16_t    zoc;
    uint16_t    ozcs;
    uint32_t    mar;
    uint32_t    mor;
    uint32_t    rrl;
    uint32_t    frl;
    uint8_t     rsvd12[24];
    uint32_t    numzrwa;
    uint16_t    zrwafg;
    uint16_t    zrwas;
    uint8_t     zrwacap;
    uint8_t     rsvd53[2763];
    NvmeLBAFE   lbafe[16];
    uint8_t     rsvd3072[768];
    uint8_t     vs[256];
} NvmeIdNsZoned;

enum NvmeIdNsZonedOzcs {
    NVME_ID_NS_ZONED_OZCS_RAZB    = 1 << 0,
    NVME_ID_NS_ZONED_OZCS_ZRWASUP = 1 << 1,
};

enum NvmeIdNsZonedZrwacap {
    NVME_ID_NS_ZONED_ZRWACAP_EXPFLUSHSUP = 1 << 0,
};

/*Deallocate Logical Block Features*/
#define NVME_ID_NS_DLFEAT_GUARD_CRC(dlfeat)       ((dlfeat) & 0x10)
#define NVME_ID_NS_DLFEAT_WRITE_ZEROES(dlfeat)    ((dlfeat) & 0x08)

#define NVME_ID_NS_DLFEAT_READ_BEHAVIOR(dlfeat)     ((dlfeat) & 0x7)
#define NVME_ID_NS_DLFEAT_READ_BEHAVIOR_UNDEFINED   0
#define NVME_ID_NS_DLFEAT_READ_BEHAVIOR_ZEROES      1
#define NVME_ID_NS_DLFEAT_READ_BEHAVIOR_ONES        2


#define NVME_ID_NS_NSFEAT_THIN(nsfeat)      ((nsfeat & 0x1))
#define NVME_ID_NS_NSFEAT_DULBE(nsfeat)     ((nsfeat >> 2) & 0x1)
#define NVME_ID_NS_FLBAS_EXTENDED(flbas)    ((flbas >> 4) & 0x1)
#define NVME_ID_NS_FLBAS_INDEX(flbas)       ((flbas & 0xf))
#define NVME_ID_NS_MC_SEPARATE(mc)          ((mc >> 1) & 0x1)
#define NVME_ID_NS_MC_EXTENDED(mc)          ((mc & 0x1))
#define NVME_ID_NS_DPC_LAST_EIGHT(dpc)      ((dpc >> 4) & 0x1)
#define NVME_ID_NS_DPC_FIRST_EIGHT(dpc)     ((dpc >> 3) & 0x1)
#define NVME_ID_NS_DPC_TYPE_3(dpc)          ((dpc >> 2) & 0x1)
#define NVME_ID_NS_DPC_TYPE_2(dpc)          ((dpc >> 1) & 0x1)
#define NVME_ID_NS_DPC_TYPE_1(dpc)          ((dpc & 0x1))
#define NVME_ID_NS_DPC_TYPE_MASK            0x7

enum NvmeIdNsDps {
    NVME_ID_NS_DPS_TYPE_NONE   = 0,
    NVME_ID_NS_DPS_TYPE_1      = 1,
    NVME_ID_NS_DPS_TYPE_2      = 2,
    NVME_ID_NS_DPS_TYPE_3      = 3,
    NVME_ID_NS_DPS_TYPE_MASK   = 0x7,
    NVME_ID_NS_DPS_FIRST_EIGHT = 8,
};

enum NvmeIdNsFlbas {
    NVME_ID_NS_FLBAS_EXTENDED = 1 << 4,
};

enum NvmeIdNsMc {
    NVME_ID_NS_MC_EXTENDED = 1 << 0,
    NVME_ID_NS_MC_SEPARATE = 1 << 1,
};

#define NVME_ID_NS_DPS_TYPE(dps) (dps & NVME_ID_NS_DPS_TYPE_MASK)

enum NvmePIFormat {
    NVME_PI_GUARD_16                 = 0,
    NVME_PI_GUARD_64                 = 2,
};

typedef union NvmeDifTuple {
    struct {
        uint16_t guard;
        uint16_t apptag;
        uint32_t reftag;
    } g16;

    struct {
        uint64_t guard;
        uint16_t apptag;
        uint8_t  sr[6];
    } g64;
} NvmeDifTuple;

enum NvmeZoneAttr {
    NVME_ZA_FINISHED_BY_CTLR         = 1 << 0,
    NVME_ZA_FINISH_RECOMMENDED       = 1 << 1,
    NVME_ZA_RESET_RECOMMENDED        = 1 << 2,
    NVME_ZA_ZRWA_VALID               = 1 << 3,
    NVME_ZA_ZD_EXT_VALID             = 1 << 7,
};

typedef struct QEMU_PACKED NvmeZoneReportHeader {
    uint64_t    nr_zones;
    uint8_t     rsvd[56];
} NvmeZoneReportHeader;

enum NvmeZoneReceiveAction {
    NVME_ZONE_REPORT                 = 0,
    NVME_ZONE_REPORT_EXTENDED        = 1,
};

enum NvmeZoneReportType {
    NVME_ZONE_REPORT_ALL             = 0,
    NVME_ZONE_REPORT_EMPTY           = 1,
    NVME_ZONE_REPORT_IMPLICITLY_OPEN = 2,
    NVME_ZONE_REPORT_EXPLICITLY_OPEN = 3,
    NVME_ZONE_REPORT_CLOSED          = 4,
    NVME_ZONE_REPORT_FULL            = 5,
    NVME_ZONE_REPORT_READ_ONLY       = 6,
    NVME_ZONE_REPORT_OFFLINE         = 7,
};

enum NvmeZoneType {
    NVME_ZONE_TYPE_RESERVED          = 0x00,
    NVME_ZONE_TYPE_SEQ_WRITE         = 0x02,
};

typedef struct QEMU_PACKED NvmeZoneSendCmd {
    uint8_t     opcode;
    uint8_t     flags;
    uint16_t    cid;
    uint32_t    nsid;
    uint32_t    rsvd8[4];
    NvmeCmdDptr dptr;
    uint64_t    slba;
    uint32_t    rsvd48;
    uint8_t     zsa;
    uint8_t     zsflags;
    uint8_t     rsvd54[2];
    uint32_t    rsvd56[2];
} NvmeZoneSendCmd;

enum NvmeZoneSendAction {
    NVME_ZONE_ACTION_RSD             = 0x00,
    NVME_ZONE_ACTION_CLOSE           = 0x01,
    NVME_ZONE_ACTION_FINISH          = 0x02,
    NVME_ZONE_ACTION_OPEN            = 0x03,
    NVME_ZONE_ACTION_RESET           = 0x04,
    NVME_ZONE_ACTION_OFFLINE         = 0x05,
    NVME_ZONE_ACTION_SET_ZD_EXT      = 0x10,
    NVME_ZONE_ACTION_ZRWA_FLUSH      = 0x11,
};

enum {
    NVME_ZSFLAG_SELECT_ALL = 1 << 0,
    NVME_ZSFLAG_ZRWA_ALLOC = 1 << 1,
};

typedef struct QEMU_PACKED NvmeZoneDescr {
    uint8_t     zt;
    uint8_t     zs;
    uint8_t     za;
    uint8_t     rsvd3[5];
    uint64_t    zcap;
    uint64_t    zslba;
    uint64_t    wp;
    uint8_t     rsvd32[32];
} NvmeZoneDescr;

typedef enum NvmeZoneState {
    NVME_ZONE_STATE_RESERVED         = 0x00,
    NVME_ZONE_STATE_EMPTY            = 0x01,
    NVME_ZONE_STATE_IMPLICITLY_OPEN  = 0x02,
    NVME_ZONE_STATE_EXPLICITLY_OPEN  = 0x03,
    NVME_ZONE_STATE_CLOSED           = 0x04,
    NVME_ZONE_STATE_READ_ONLY        = 0x0d,
    NVME_ZONE_STATE_FULL             = 0x0e,
    NVME_ZONE_STATE_OFFLINE          = 0x0f,
} NvmeZoneState;

typedef struct QEMU_PACKED NvmePriCtrlCap {
    uint16_t    cntlid;
    uint16_t    portid;
    uint8_t     crt;
    uint8_t     rsvd5[27];
    uint32_t    vqfrt;
    uint32_t    vqrfa;
    uint16_t    vqrfap;
    uint16_t    vqprt;
    uint16_t    vqfrsm;
    uint16_t    vqgran;
    uint8_t     rsvd48[16];
    uint32_t    vifrt;
    uint32_t    virfa;
    uint16_t    virfap;
    uint16_t    viprt;
    uint16_t    vifrsm;
    uint16_t    vigran;
    uint8_t     rsvd80[4016];
} NvmePriCtrlCap;

typedef enum NvmePriCtrlCapCrt {
    NVME_CRT_VQ             = 1 << 0,
    NVME_CRT_VI             = 1 << 1,
} NvmePriCtrlCapCrt;

typedef struct QEMU_PACKED NvmeSecCtrlEntry {
    uint16_t    scid;
    uint16_t    pcid;
    uint8_t     scs;
    uint8_t     rsvd5[3];
    uint16_t    vfn;
    uint16_t    nvq;
    uint16_t    nvi;
    uint8_t     rsvd14[18];
} NvmeSecCtrlEntry;

typedef struct QEMU_PACKED NvmeSecCtrlList {
    uint8_t             numcntl;
    uint8_t             rsvd1[31];
    NvmeSecCtrlEntry    sec[127];
} NvmeSecCtrlList;

typedef enum NvmeVirtMngmtAction {
    NVME_VIRT_MNGMT_ACTION_PRM_ALLOC    = 0x01,
    NVME_VIRT_MNGMT_ACTION_SEC_OFFLINE  = 0x07,
    NVME_VIRT_MNGMT_ACTION_SEC_ASSIGN   = 0x08,
    NVME_VIRT_MNGMT_ACTION_SEC_ONLINE   = 0x09,
} NvmeVirtMngmtAction;

typedef enum NvmeVirtualResourceType {
    NVME_VIRT_RES_QUEUE         = 0x00,
    NVME_VIRT_RES_INTERRUPT     = 0x01,
} NvmeVirtualResourceType;

static inline void _nvme_check_size(void)
{
    QEMU_BUILD_BUG_ON(sizeof(NvmeBar) != 4096);
    QEMU_BUILD_BUG_ON(sizeof(NvmeAerResult) != 4);
    QEMU_BUILD_BUG_ON(sizeof(NvmeZonedResult) != 8);
    QEMU_BUILD_BUG_ON(sizeof(NvmeCqe) != 16);
    QEMU_BUILD_BUG_ON(sizeof(NvmeDsmRange) != 16);
    QEMU_BUILD_BUG_ON(sizeof(NvmeCopySourceRangeFormat0) != 32);
    QEMU_BUILD_BUG_ON(sizeof(NvmeCopySourceRangeFormat1) != 40);
    QEMU_BUILD_BUG_ON(sizeof(NvmeCmd) != 64);
    QEMU_BUILD_BUG_ON(sizeof(NvmeDeleteQ) != 64);
    QEMU_BUILD_BUG_ON(sizeof(NvmeCreateCq) != 64);
    QEMU_BUILD_BUG_ON(sizeof(NvmeCreateSq) != 64);
    QEMU_BUILD_BUG_ON(sizeof(NvmeIdentify) != 64);
    QEMU_BUILD_BUG_ON(sizeof(NvmeRwCmd) != 64);
    QEMU_BUILD_BUG_ON(sizeof(NvmeDsmCmd) != 64);
    QEMU_BUILD_BUG_ON(sizeof(NvmeCopyCmd) != 64);
    QEMU_BUILD_BUG_ON(sizeof(NvmeRangeType) != 64);
    QEMU_BUILD_BUG_ON(sizeof(NvmeHostBehaviorSupport) != 512);
    QEMU_BUILD_BUG_ON(sizeof(NvmeErrorLog) != 64);
    QEMU_BUILD_BUG_ON(sizeof(NvmeFwSlotInfoLog) != 512);
    QEMU_BUILD_BUG_ON(sizeof(NvmeSmartLog) != 512);
    QEMU_BUILD_BUG_ON(sizeof(NvmeEffectsLog) != 4096);
    QEMU_BUILD_BUG_ON(sizeof(NvmeIdCtrl) != 4096);
    QEMU_BUILD_BUG_ON(sizeof(NvmeIdCtrlZoned) != 4096);
    QEMU_BUILD_BUG_ON(sizeof(NvmeIdCtrlNvm) != 4096);
    QEMU_BUILD_BUG_ON(sizeof(NvmeLBAF) != 4);
    QEMU_BUILD_BUG_ON(sizeof(NvmeLBAFE) != 16);
    QEMU_BUILD_BUG_ON(sizeof(NvmeIdNs) != 4096);
    QEMU_BUILD_BUG_ON(sizeof(NvmeIdNsNvm) != 4096);
    QEMU_BUILD_BUG_ON(sizeof(NvmeIdNsZoned) != 4096);
    QEMU_BUILD_BUG_ON(sizeof(NvmeSglDescriptor) != 16);
    QEMU_BUILD_BUG_ON(sizeof(NvmeIdNsDescr) != 4);
    QEMU_BUILD_BUG_ON(sizeof(NvmeZoneDescr) != 64);
    QEMU_BUILD_BUG_ON(sizeof(NvmeDifTuple) != 16);
    QEMU_BUILD_BUG_ON(sizeof(NvmePriCtrlCap) != 4096);
    QEMU_BUILD_BUG_ON(sizeof(NvmeSecCtrlEntry) != 32);
    QEMU_BUILD_BUG_ON(sizeof(NvmeSecCtrlList) != 4096);
}
#endif
