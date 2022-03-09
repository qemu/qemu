#ifndef MIPS_CPU_H
#define MIPS_CPU_H

#include "cpu-qom.h"
#include "exec/cpu-defs.h"
#include "fpu/softfloat-types.h"
#include "hw/clock.h"
#include "mips-defs.h"

#define TCG_GUEST_DEFAULT_MO (0)

typedef struct CPUMIPSTLBContext CPUMIPSTLBContext;

/* MSA Context */
#define MSA_WRLEN (128)

typedef union wr_t wr_t;
union wr_t {
    int8_t  b[MSA_WRLEN / 8];
    int16_t h[MSA_WRLEN / 16];
    int32_t w[MSA_WRLEN / 32];
    int64_t d[MSA_WRLEN / 64];
};

typedef union fpr_t fpr_t;
union fpr_t {
    float64  fd;   /* ieee double precision */
    float32  fs[2];/* ieee single precision */
    uint64_t d;    /* binary double fixed-point */
    uint32_t w[2]; /* binary single fixed-point */
/* FPU/MSA register mapping is not tested on big-endian hosts. */
    wr_t     wr;   /* vector data */
};
/*
 *define FP_ENDIAN_IDX to access the same location
 * in the fpr_t union regardless of the host endianness
 */
#if defined(HOST_WORDS_BIGENDIAN)
#  define FP_ENDIAN_IDX 1
#else
#  define FP_ENDIAN_IDX 0
#endif

typedef struct CPUMIPSFPUContext CPUMIPSFPUContext;
struct CPUMIPSFPUContext {
    /* Floating point registers */
    fpr_t fpr[32];
    float_status fp_status;
    /* fpu implementation/revision register (fir) */
    uint32_t fcr0;
#define FCR0_FREP 29
#define FCR0_UFRP 28
#define FCR0_HAS2008 23
#define FCR0_F64 22
#define FCR0_L 21
#define FCR0_W 20
#define FCR0_3D 19
#define FCR0_PS 18
#define FCR0_D 17
#define FCR0_S 16
#define FCR0_PRID 8
#define FCR0_REV 0
    /* fcsr */
    uint32_t fcr31_rw_bitmask;
    uint32_t fcr31;
#define FCR31_FS 24
#define FCR31_ABS2008 19
#define FCR31_NAN2008 18
#define SET_FP_COND(num, env)     do { ((env).fcr31) |=                 \
                                       ((num) ? (1 << ((num) + 24)) :   \
                                                (1 << 23));             \
                                     } while (0)
#define CLEAR_FP_COND(num, env)   do { ((env).fcr31) &=                 \
                                       ~((num) ? (1 << ((num) + 24)) :  \
                                                 (1 << 23));            \
                                     } while (0)
#define GET_FP_COND(env)         ((((env).fcr31 >> 24) & 0xfe) |        \
                                 (((env).fcr31 >> 23) & 0x1))
#define GET_FP_CAUSE(reg)        (((reg) >> 12) & 0x3f)
#define GET_FP_ENABLE(reg)       (((reg) >>  7) & 0x1f)
#define GET_FP_FLAGS(reg)        (((reg) >>  2) & 0x1f)
#define SET_FP_CAUSE(reg, v)      do { (reg) = ((reg) & ~(0x3f << 12)) | \
                                               ((v & 0x3f) << 12);       \
                                     } while (0)
#define SET_FP_ENABLE(reg, v)     do { (reg) = ((reg) & ~(0x1f <<  7)) | \
                                               ((v & 0x1f) << 7);        \
                                     } while (0)
#define SET_FP_FLAGS(reg, v)      do { (reg) = ((reg) & ~(0x1f <<  2)) | \
                                               ((v & 0x1f) << 2);        \
                                     } while (0)
#define UPDATE_FP_FLAGS(reg, v)   do { (reg) |= ((v & 0x1f) << 2); } while (0)
#define FP_INEXACT        1
#define FP_UNDERFLOW      2
#define FP_OVERFLOW       4
#define FP_DIV0           8
#define FP_INVALID        16
#define FP_UNIMPLEMENTED  32
};

#define TARGET_INSN_START_EXTRA_WORDS 2

typedef struct CPUMIPSMVPContext CPUMIPSMVPContext;
struct CPUMIPSMVPContext {
    int32_t CP0_MVPControl;
#define CP0MVPCo_CPA    3
#define CP0MVPCo_STLB   2
#define CP0MVPCo_VPC    1
#define CP0MVPCo_EVP    0
    int32_t CP0_MVPConf0;
#define CP0MVPC0_M      31
#define CP0MVPC0_TLBS   29
#define CP0MVPC0_GS     28
#define CP0MVPC0_PCP    27
#define CP0MVPC0_PTLBE  16
#define CP0MVPC0_TCA    15
#define CP0MVPC0_PVPE   10
#define CP0MVPC0_PTC    0
    int32_t CP0_MVPConf1;
#define CP0MVPC1_CIM    31
#define CP0MVPC1_CIF    30
#define CP0MVPC1_PCX    20
#define CP0MVPC1_PCP2   10
#define CP0MVPC1_PCP1   0
};

typedef struct mips_def_t mips_def_t;

#define MIPS_SHADOW_SET_MAX 16
#define MIPS_TC_MAX 5
#define MIPS_FPU_MAX 1
#define MIPS_DSP_ACC 4
#define MIPS_KSCRATCH_NUM 6
#define MIPS_MAAR_MAX 16 /* Must be an even number. */


/*
 *     Summary of CP0 registers
 *     ========================
 *
 *
 *     Register 0        Register 1        Register 2        Register 3
 *     ----------        ----------        ----------        ----------
 *
 * 0   Index             Random            EntryLo0          EntryLo1
 * 1   MVPControl        VPEControl        TCStatus          GlobalNumber
 * 2   MVPConf0          VPEConf0          TCBind
 * 3   MVPConf1          VPEConf1          TCRestart
 * 4   VPControl         YQMask            TCHalt
 * 5                     VPESchedule       TCContext
 * 6                     VPEScheFBack      TCSchedule
 * 7                     VPEOpt            TCScheFBack       TCOpt
 *
 *
 *     Register 4        Register 5        Register 6        Register 7
 *     ----------        ----------        ----------        ----------
 *
 * 0   Context           PageMask          Wired             HWREna
 * 1   ContextConfig     PageGrain         SRSConf0
 * 2   UserLocal         SegCtl0           SRSConf1
 * 3   XContextConfig    SegCtl1           SRSConf2
 * 4   DebugContextID    SegCtl2           SRSConf3
 * 5   MemoryMapID       PWBase            SRSConf4
 * 6                     PWField           PWCtl
 * 7                     PWSize
 *
 *
 *     Register 8        Register 9        Register 10       Register 11
 *     ----------        ----------        -----------       -----------
 *
 * 0   BadVAddr          Count             EntryHi           Compare
 * 1   BadInstr
 * 2   BadInstrP
 * 3   BadInstrX
 * 4                                       GuestCtl1         GuestCtl0Ext
 * 5                                       GuestCtl2
 * 6                     SAARI             GuestCtl3
 * 7                     SAAR
 *
 *
 *     Register 12       Register 13       Register 14       Register 15
 *     -----------       -----------       -----------       -----------
 *
 * 0   Status            Cause             EPC               PRId
 * 1   IntCtl                                                EBase
 * 2   SRSCtl                              NestedEPC         CDMMBase
 * 3   SRSMap                                                CMGCRBase
 * 4   View_IPL          View_RIPL                           BEVVA
 * 5   SRSMap2           NestedExc
 * 6   GuestCtl0
 * 7   GTOffset
 *
 *
 *     Register 16       Register 17       Register 18       Register 19
 *     -----------       -----------       -----------       -----------
 *
 * 0   Config            LLAddr            WatchLo0          WatchHi
 * 1   Config1           MAAR              WatchLo1          WatchHi
 * 2   Config2           MAARI             WatchLo2          WatchHi
 * 3   Config3                             WatchLo3          WatchHi
 * 4   Config4                             WatchLo4          WatchHi
 * 5   Config5                             WatchLo5          WatchHi
 * 6   Config6                             WatchLo6          WatchHi
 * 7   Config7                             WatchLo7          WatchHi
 *
 *
 *     Register 20       Register 21       Register 22       Register 23
 *     -----------       -----------       -----------       -----------
 *
 * 0   XContext                                              Debug
 * 1                                                         TraceControl
 * 2                                                         TraceControl2
 * 3                                                         UserTraceData1
 * 4                                                         TraceIBPC
 * 5                                                         TraceDBPC
 * 6                                                         Debug2
 * 7
 *
 *
 *     Register 24       Register 25       Register 26       Register 27
 *     -----------       -----------       -----------       -----------
 *
 * 0   DEPC              PerfCnt            ErrCtl          CacheErr
 * 1                     PerfCnt
 * 2   TraceControl3     PerfCnt
 * 3   UserTraceData2    PerfCnt
 * 4                     PerfCnt
 * 5                     PerfCnt
 * 6                     PerfCnt
 * 7                     PerfCnt
 *
 *
 *     Register 28       Register 29       Register 30       Register 31
 *     -----------       -----------       -----------       -----------
 *
 * 0   DataLo            DataHi            ErrorEPC          DESAVE
 * 1   TagLo             TagHi
 * 2   DataLo1           DataHi1                             KScratch<n>
 * 3   TagLo1            TagHi1                              KScratch<n>
 * 4   DataLo2           DataHi2                             KScratch<n>
 * 5   TagLo2            TagHi2                              KScratch<n>
 * 6   DataLo3           DataHi3                             KScratch<n>
 * 7   TagLo3            TagHi3                              KScratch<n>
 *
 */
#define CP0_REGISTER_00     0
#define CP0_REGISTER_01     1
#define CP0_REGISTER_02     2
#define CP0_REGISTER_03     3
#define CP0_REGISTER_04     4
#define CP0_REGISTER_05     5
#define CP0_REGISTER_06     6
#define CP0_REGISTER_07     7
#define CP0_REGISTER_08     8
#define CP0_REGISTER_09     9
#define CP0_REGISTER_10    10
#define CP0_REGISTER_11    11
#define CP0_REGISTER_12    12
#define CP0_REGISTER_13    13
#define CP0_REGISTER_14    14
#define CP0_REGISTER_15    15
#define CP0_REGISTER_16    16
#define CP0_REGISTER_17    17
#define CP0_REGISTER_18    18
#define CP0_REGISTER_19    19
#define CP0_REGISTER_20    20
#define CP0_REGISTER_21    21
#define CP0_REGISTER_22    22
#define CP0_REGISTER_23    23
#define CP0_REGISTER_24    24
#define CP0_REGISTER_25    25
#define CP0_REGISTER_26    26
#define CP0_REGISTER_27    27
#define CP0_REGISTER_28    28
#define CP0_REGISTER_29    29
#define CP0_REGISTER_30    30
#define CP0_REGISTER_31    31


/* CP0 Register 00 */
#define CP0_REG00__INDEX           0
#define CP0_REG00__MVPCONTROL      1
#define CP0_REG00__MVPCONF0        2
#define CP0_REG00__MVPCONF1        3
#define CP0_REG00__VPCONTROL       4
/* CP0 Register 01 */
#define CP0_REG01__RANDOM          0
#define CP0_REG01__VPECONTROL      1
#define CP0_REG01__VPECONF0        2
#define CP0_REG01__VPECONF1        3
#define CP0_REG01__YQMASK          4
#define CP0_REG01__VPESCHEDULE     5
#define CP0_REG01__VPESCHEFBACK    6
#define CP0_REG01__VPEOPT          7
/* CP0 Register 02 */
#define CP0_REG02__ENTRYLO0        0
#define CP0_REG02__TCSTATUS        1
#define CP0_REG02__TCBIND          2
#define CP0_REG02__TCRESTART       3
#define CP0_REG02__TCHALT          4
#define CP0_REG02__TCCONTEXT       5
#define CP0_REG02__TCSCHEDULE      6
#define CP0_REG02__TCSCHEFBACK     7
/* CP0 Register 03 */
#define CP0_REG03__ENTRYLO1        0
#define CP0_REG03__GLOBALNUM       1
#define CP0_REG03__TCOPT           7
/* CP0 Register 04 */
#define CP0_REG04__CONTEXT         0
#define CP0_REG04__CONTEXTCONFIG   1
#define CP0_REG04__USERLOCAL       2
#define CP0_REG04__XCONTEXTCONFIG  3
#define CP0_REG04__DBGCONTEXTID    4
#define CP0_REG04__MMID            5
/* CP0 Register 05 */
#define CP0_REG05__PAGEMASK        0
#define CP0_REG05__PAGEGRAIN       1
#define CP0_REG05__SEGCTL0         2
#define CP0_REG05__SEGCTL1         3
#define CP0_REG05__SEGCTL2         4
#define CP0_REG05__PWBASE          5
#define CP0_REG05__PWFIELD         6
#define CP0_REG05__PWSIZE          7
/* CP0 Register 06 */
#define CP0_REG06__WIRED           0
#define CP0_REG06__SRSCONF0        1
#define CP0_REG06__SRSCONF1        2
#define CP0_REG06__SRSCONF2        3
#define CP0_REG06__SRSCONF3        4
#define CP0_REG06__SRSCONF4        5
#define CP0_REG06__PWCTL           6
/* CP0 Register 07 */
#define CP0_REG07__HWRENA          0
/* CP0 Register 08 */
#define CP0_REG08__BADVADDR        0
#define CP0_REG08__BADINSTR        1
#define CP0_REG08__BADINSTRP       2
#define CP0_REG08__BADINSTRX       3
/* CP0 Register 09 */
#define CP0_REG09__COUNT           0
#define CP0_REG09__SAARI           6
#define CP0_REG09__SAAR            7
/* CP0 Register 10 */
#define CP0_REG10__ENTRYHI         0
#define CP0_REG10__GUESTCTL1       4
#define CP0_REG10__GUESTCTL2       5
#define CP0_REG10__GUESTCTL3       6
/* CP0 Register 11 */
#define CP0_REG11__COMPARE         0
#define CP0_REG11__GUESTCTL0EXT    4
/* CP0 Register 12 */
#define CP0_REG12__STATUS          0
#define CP0_REG12__INTCTL          1
#define CP0_REG12__SRSCTL          2
#define CP0_REG12__SRSMAP          3
#define CP0_REG12__VIEW_IPL        4
#define CP0_REG12__SRSMAP2         5
#define CP0_REG12__GUESTCTL0       6
#define CP0_REG12__GTOFFSET        7
/* CP0 Register 13 */
#define CP0_REG13__CAUSE           0
#define CP0_REG13__VIEW_RIPL       4
#define CP0_REG13__NESTEDEXC       5
/* CP0 Register 14 */
#define CP0_REG14__EPC             0
#define CP0_REG14__NESTEDEPC       2
/* CP0 Register 15 */
#define CP0_REG15__PRID            0
#define CP0_REG15__EBASE           1
#define CP0_REG15__CDMMBASE        2
#define CP0_REG15__CMGCRBASE       3
#define CP0_REG15__BEVVA           4
/* CP0 Register 16 */
#define CP0_REG16__CONFIG          0
#define CP0_REG16__CONFIG1         1
#define CP0_REG16__CONFIG2         2
#define CP0_REG16__CONFIG3         3
#define CP0_REG16__CONFIG4         4
#define CP0_REG16__CONFIG5         5
#define CP0_REG16__CONFIG6         6
#define CP0_REG16__CONFIG7         7
/* CP0 Register 17 */
#define CP0_REG17__LLADDR          0
#define CP0_REG17__MAAR            1
#define CP0_REG17__MAARI           2
/* CP0 Register 18 */
#define CP0_REG18__WATCHLO0        0
#define CP0_REG18__WATCHLO1        1
#define CP0_REG18__WATCHLO2        2
#define CP0_REG18__WATCHLO3        3
#define CP0_REG18__WATCHLO4        4
#define CP0_REG18__WATCHLO5        5
#define CP0_REG18__WATCHLO6        6
#define CP0_REG18__WATCHLO7        7
/* CP0 Register 19 */
#define CP0_REG19__WATCHHI0        0
#define CP0_REG19__WATCHHI1        1
#define CP0_REG19__WATCHHI2        2
#define CP0_REG19__WATCHHI3        3
#define CP0_REG19__WATCHHI4        4
#define CP0_REG19__WATCHHI5        5
#define CP0_REG19__WATCHHI6        6
#define CP0_REG19__WATCHHI7        7
/* CP0 Register 20 */
#define CP0_REG20__XCONTEXT        0
/* CP0 Register 21 */
/* CP0 Register 22 */
/* CP0 Register 23 */
#define CP0_REG23__DEBUG           0
#define CP0_REG23__TRACECONTROL    1
#define CP0_REG23__TRACECONTROL2   2
#define CP0_REG23__USERTRACEDATA1  3
#define CP0_REG23__TRACEIBPC       4
#define CP0_REG23__TRACEDBPC       5
#define CP0_REG23__DEBUG2          6
/* CP0 Register 24 */
#define CP0_REG24__DEPC            0
/* CP0 Register 25 */
#define CP0_REG25__PERFCTL0        0
#define CP0_REG25__PERFCNT0        1
#define CP0_REG25__PERFCTL1        2
#define CP0_REG25__PERFCNT1        3
#define CP0_REG25__PERFCTL2        4
#define CP0_REG25__PERFCNT2        5
#define CP0_REG25__PERFCTL3        6
#define CP0_REG25__PERFCNT3        7
/* CP0 Register 26 */
#define CP0_REG26__ERRCTL          0
/* CP0 Register 27 */
#define CP0_REG27__CACHERR         0
/* CP0 Register 28 */
#define CP0_REG28__TAGLO           0
#define CP0_REG28__DATALO          1
#define CP0_REG28__TAGLO1          2
#define CP0_REG28__DATALO1         3
#define CP0_REG28__TAGLO2          4
#define CP0_REG28__DATALO2         5
#define CP0_REG28__TAGLO3          6
#define CP0_REG28__DATALO3         7
/* CP0 Register 29 */
#define CP0_REG29__TAGHI           0
#define CP0_REG29__DATAHI          1
#define CP0_REG29__TAGHI1          2
#define CP0_REG29__DATAHI1         3
#define CP0_REG29__TAGHI2          4
#define CP0_REG29__DATAHI2         5
#define CP0_REG29__TAGHI3          6
#define CP0_REG29__DATAHI3         7
/* CP0 Register 30 */
#define CP0_REG30__ERROREPC        0
/* CP0 Register 31 */
#define CP0_REG31__DESAVE          0
#define CP0_REG31__KSCRATCH1       2
#define CP0_REG31__KSCRATCH2       3
#define CP0_REG31__KSCRATCH3       4
#define CP0_REG31__KSCRATCH4       5
#define CP0_REG31__KSCRATCH5       6
#define CP0_REG31__KSCRATCH6       7


typedef struct TCState TCState;
struct TCState {
    target_ulong gpr[32];
#if defined(TARGET_MIPS64)
    /*
     * For CPUs using 128-bit GPR registers, we put the lower halves in gpr[])
     * and the upper halves in gpr_hi[].
     */
    uint64_t gpr_hi[32];
#endif /* TARGET_MIPS64 */
    target_ulong PC;
    target_ulong HI[MIPS_DSP_ACC];
    target_ulong LO[MIPS_DSP_ACC];
    target_ulong ACX[MIPS_DSP_ACC];
    target_ulong DSPControl;
    int32_t CP0_TCStatus;
#define CP0TCSt_TCU3    31
#define CP0TCSt_TCU2    30
#define CP0TCSt_TCU1    29
#define CP0TCSt_TCU0    28
#define CP0TCSt_TMX     27
#define CP0TCSt_RNST    23
#define CP0TCSt_TDS     21
#define CP0TCSt_DT      20
#define CP0TCSt_DA      15
#define CP0TCSt_A       13
#define CP0TCSt_TKSU    11
#define CP0TCSt_IXMT    10
#define CP0TCSt_TASID   0
    int32_t CP0_TCBind;
#define CP0TCBd_CurTC   21
#define CP0TCBd_TBE     17
#define CP0TCBd_CurVPE  0
    target_ulong CP0_TCHalt;
    target_ulong CP0_TCContext;
    target_ulong CP0_TCSchedule;
    target_ulong CP0_TCScheFBack;
    int32_t CP0_Debug_tcstatus;
    target_ulong CP0_UserLocal;

    int32_t msacsr;

#define MSACSR_FS       24
#define MSACSR_FS_MASK  (1 << MSACSR_FS)
#define MSACSR_NX       18
#define MSACSR_NX_MASK  (1 << MSACSR_NX)
#define MSACSR_CEF      2
#define MSACSR_CEF_MASK (0xffff << MSACSR_CEF)
#define MSACSR_RM       0
#define MSACSR_RM_MASK  (0x3 << MSACSR_RM)
#define MSACSR_MASK     (MSACSR_RM_MASK | MSACSR_CEF_MASK | MSACSR_NX_MASK | \
        MSACSR_FS_MASK)

    float_status msa_fp_status;

#define NUMBER_OF_MXU_REGISTERS 16
    target_ulong mxu_gpr[NUMBER_OF_MXU_REGISTERS - 1];
    target_ulong mxu_cr;
#define MXU_CR_LC       31
#define MXU_CR_RC       30
#define MXU_CR_BIAS     2
#define MXU_CR_RD_EN    1
#define MXU_CR_MXU_EN   0

};

struct MIPSITUState;
typedef struct CPUArchState {
    TCState active_tc;
    CPUMIPSFPUContext active_fpu;

    uint32_t current_tc;
    uint32_t current_fpu;

    uint32_t SEGBITS;
    uint32_t PABITS;
#if defined(TARGET_MIPS64)
# define PABITS_BASE 36
#else
# define PABITS_BASE 32
#endif
    target_ulong SEGMask;
    uint64_t PAMask;
#define PAMASK_BASE ((1ULL << PABITS_BASE) - 1)

    int32_t msair;
#define MSAIR_ProcID    8
#define MSAIR_Rev       0

/*
 * CP0 Register 0
 */
    int32_t CP0_Index;
    /* CP0_MVP* are per MVP registers. */
    int32_t CP0_VPControl;
#define CP0VPCtl_DIS    0
/*
 * CP0 Register 1
 */
    int32_t CP0_Random;
    int32_t CP0_VPEControl;
#define CP0VPECo_YSI    21
#define CP0VPECo_GSI    20
#define CP0VPECo_EXCPT  16
#define CP0VPECo_TE     15
#define CP0VPECo_TargTC 0
    int32_t CP0_VPEConf0;
#define CP0VPEC0_M      31
#define CP0VPEC0_XTC    21
#define CP0VPEC0_TCS    19
#define CP0VPEC0_SCS    18
#define CP0VPEC0_DSC    17
#define CP0VPEC0_ICS    16
#define CP0VPEC0_MVP    1
#define CP0VPEC0_VPA    0
    int32_t CP0_VPEConf1;
#define CP0VPEC1_NCX    20
#define CP0VPEC1_NCP2   10
#define CP0VPEC1_NCP1   0
    target_ulong CP0_YQMask;
    target_ulong CP0_VPESchedule;
    target_ulong CP0_VPEScheFBack;
    int32_t CP0_VPEOpt;
#define CP0VPEOpt_IWX7  15
#define CP0VPEOpt_IWX6  14
#define CP0VPEOpt_IWX5  13
#define CP0VPEOpt_IWX4  12
#define CP0VPEOpt_IWX3  11
#define CP0VPEOpt_IWX2  10
#define CP0VPEOpt_IWX1  9
#define CP0VPEOpt_IWX0  8
#define CP0VPEOpt_DWX7  7
#define CP0VPEOpt_DWX6  6
#define CP0VPEOpt_DWX5  5
#define CP0VPEOpt_DWX4  4
#define CP0VPEOpt_DWX3  3
#define CP0VPEOpt_DWX2  2
#define CP0VPEOpt_DWX1  1
#define CP0VPEOpt_DWX0  0
/*
 * CP0 Register 2
 */
    uint64_t CP0_EntryLo0;
/*
 * CP0 Register 3
 */
    uint64_t CP0_EntryLo1;
#if defined(TARGET_MIPS64)
# define CP0EnLo_RI 63
# define CP0EnLo_XI 62
#else
# define CP0EnLo_RI 31
# define CP0EnLo_XI 30
#endif
    int32_t CP0_GlobalNumber;
#define CP0GN_VPId 0
/*
 * CP0 Register 4
 */
    target_ulong CP0_Context;
    int32_t CP0_MemoryMapID;
/*
 * CP0 Register 5
 */
    int32_t CP0_PageMask;
#define CP0PM_MASK 13
    int32_t CP0_PageGrain_rw_bitmask;
    int32_t CP0_PageGrain;
#define CP0PG_RIE 31
#define CP0PG_XIE 30
#define CP0PG_ELPA 29
#define CP0PG_IEC 27
    target_ulong CP0_SegCtl0;
    target_ulong CP0_SegCtl1;
    target_ulong CP0_SegCtl2;
#define CP0SC_PA        9
#define CP0SC_PA_MASK   (0x7FULL << CP0SC_PA)
#define CP0SC_PA_1GMASK (0x7EULL << CP0SC_PA)
#define CP0SC_AM        4
#define CP0SC_AM_MASK   (0x7ULL << CP0SC_AM)
#define CP0SC_AM_UK     0ULL
#define CP0SC_AM_MK     1ULL
#define CP0SC_AM_MSK    2ULL
#define CP0SC_AM_MUSK   3ULL
#define CP0SC_AM_MUSUK  4ULL
#define CP0SC_AM_USK    5ULL
#define CP0SC_AM_UUSK   7ULL
#define CP0SC_EU        3
#define CP0SC_EU_MASK   (1ULL << CP0SC_EU)
#define CP0SC_C         0
#define CP0SC_C_MASK    (0x7ULL << CP0SC_C)
#define CP0SC_MASK      (CP0SC_C_MASK | CP0SC_EU_MASK | CP0SC_AM_MASK | \
                         CP0SC_PA_MASK)
#define CP0SC_1GMASK    (CP0SC_C_MASK | CP0SC_EU_MASK | CP0SC_AM_MASK | \
                         CP0SC_PA_1GMASK)
#define CP0SC0_MASK     (CP0SC_MASK | (CP0SC_MASK << 16))
#define CP0SC1_XAM      59
#define CP0SC1_XAM_MASK (0x7ULL << CP0SC1_XAM)
#define CP0SC1_MASK     (CP0SC_MASK | (CP0SC_MASK << 16) | CP0SC1_XAM_MASK)
#define CP0SC2_XR       56
#define CP0SC2_XR_MASK  (0xFFULL << CP0SC2_XR)
#define CP0SC2_MASK     (CP0SC_1GMASK | (CP0SC_1GMASK << 16) | CP0SC2_XR_MASK)
    target_ulong CP0_PWBase;
    target_ulong CP0_PWField;
#if defined(TARGET_MIPS64)
#define CP0PF_BDI  32    /* 37..32 */
#define CP0PF_GDI  24    /* 29..24 */
#define CP0PF_UDI  18    /* 23..18 */
#define CP0PF_MDI  12    /* 17..12 */
#define CP0PF_PTI  6     /* 11..6  */
#define CP0PF_PTEI 0     /*  5..0  */
#else
#define CP0PF_GDW  24    /* 29..24 */
#define CP0PF_UDW  18    /* 23..18 */
#define CP0PF_MDW  12    /* 17..12 */
#define CP0PF_PTW  6     /* 11..6  */
#define CP0PF_PTEW 0     /*  5..0  */
#endif
    target_ulong CP0_PWSize;
#if defined(TARGET_MIPS64)
#define CP0PS_BDW  32    /* 37..32 */
#endif
#define CP0PS_PS   30
#define CP0PS_GDW  24    /* 29..24 */
#define CP0PS_UDW  18    /* 23..18 */
#define CP0PS_MDW  12    /* 17..12 */
#define CP0PS_PTW  6     /* 11..6  */
#define CP0PS_PTEW 0     /*  5..0  */
/*
 * CP0 Register 6
 */
    int32_t CP0_Wired;
    int32_t CP0_PWCtl;
#define CP0PC_PWEN      31
#if defined(TARGET_MIPS64)
#define CP0PC_PWDIREXT  30
#define CP0PC_XK        28
#define CP0PC_XS        27
#define CP0PC_XU        26
#endif
#define CP0PC_DPH       7
#define CP0PC_HUGEPG    6
#define CP0PC_PSN       0     /*  5..0  */
    int32_t CP0_SRSConf0_rw_bitmask;
    int32_t CP0_SRSConf0;
#define CP0SRSC0_M      31
#define CP0SRSC0_SRS3   20
#define CP0SRSC0_SRS2   10
#define CP0SRSC0_SRS1   0
    int32_t CP0_SRSConf1_rw_bitmask;
    int32_t CP0_SRSConf1;
#define CP0SRSC1_M      31
#define CP0SRSC1_SRS6   20
#define CP0SRSC1_SRS5   10
#define CP0SRSC1_SRS4   0
    int32_t CP0_SRSConf2_rw_bitmask;
    int32_t CP0_SRSConf2;
#define CP0SRSC2_M      31
#define CP0SRSC2_SRS9   20
#define CP0SRSC2_SRS8   10
#define CP0SRSC2_SRS7   0
    int32_t CP0_SRSConf3_rw_bitmask;
    int32_t CP0_SRSConf3;
#define CP0SRSC3_M      31
#define CP0SRSC3_SRS12  20
#define CP0SRSC3_SRS11  10
#define CP0SRSC3_SRS10  0
    int32_t CP0_SRSConf4_rw_bitmask;
    int32_t CP0_SRSConf4;
#define CP0SRSC4_SRS15  20
#define CP0SRSC4_SRS14  10
#define CP0SRSC4_SRS13  0
/*
 * CP0 Register 7
 */
    int32_t CP0_HWREna;
/*
 * CP0 Register 8
 */
    target_ulong CP0_BadVAddr;
    uint32_t CP0_BadInstr;
    uint32_t CP0_BadInstrP;
    uint32_t CP0_BadInstrX;
/*
 * CP0 Register 9
 */
    int32_t CP0_Count;
    uint32_t CP0_SAARI;
#define CP0SAARI_TARGET 0    /*  5..0  */
    uint64_t CP0_SAAR[2];
#define CP0SAAR_BASE    12   /* 43..12 */
#define CP0SAAR_SIZE    1    /*  5..1  */
#define CP0SAAR_EN      0
/*
 * CP0 Register 10
 */
    target_ulong CP0_EntryHi;
#define CP0EnHi_EHINV 10
    target_ulong CP0_EntryHi_ASID_mask;
/*
 * CP0 Register 11
 */
    int32_t CP0_Compare;
/*
 * CP0 Register 12
 */
    int32_t CP0_Status;
#define CP0St_CU3   31
#define CP0St_CU2   30
#define CP0St_CU1   29
#define CP0St_CU0   28
#define CP0St_RP    27
#define CP0St_FR    26
#define CP0St_RE    25
#define CP0St_MX    24
#define CP0St_PX    23
#define CP0St_BEV   22
#define CP0St_TS    21
#define CP0St_SR    20
#define CP0St_NMI   19
#define CP0St_IM    8
#define CP0St_KX    7
#define CP0St_SX    6
#define CP0St_UX    5
#define CP0St_KSU   3
#define CP0St_ERL   2
#define CP0St_EXL   1
#define CP0St_IE    0
    int32_t CP0_IntCtl;
#define CP0IntCtl_IPTI 29
#define CP0IntCtl_IPPCI 26
#define CP0IntCtl_VS 5
    int32_t CP0_SRSCtl;
#define CP0SRSCtl_HSS 26
#define CP0SRSCtl_EICSS 18
#define CP0SRSCtl_ESS 12
#define CP0SRSCtl_PSS 6
#define CP0SRSCtl_CSS 0
    int32_t CP0_SRSMap;
#define CP0SRSMap_SSV7 28
#define CP0SRSMap_SSV6 24
#define CP0SRSMap_SSV5 20
#define CP0SRSMap_SSV4 16
#define CP0SRSMap_SSV3 12
#define CP0SRSMap_SSV2 8
#define CP0SRSMap_SSV1 4
#define CP0SRSMap_SSV0 0
/*
 * CP0 Register 13
 */
    int32_t CP0_Cause;
#define CP0Ca_BD   31
#define CP0Ca_TI   30
#define CP0Ca_CE   28
#define CP0Ca_DC   27
#define CP0Ca_PCI  26
#define CP0Ca_IV   23
#define CP0Ca_WP   22
#define CP0Ca_IP    8
#define CP0Ca_IP_mask 0x0000FF00
#define CP0Ca_EC    2
/*
 * CP0 Register 14
 */
    target_ulong CP0_EPC;
/*
 * CP0 Register 15
 */
    int32_t CP0_PRid;
    target_ulong CP0_EBase;
    target_ulong CP0_EBaseWG_rw_bitmask;
#define CP0EBase_WG 11
    target_ulong CP0_CMGCRBase;
/*
 * CP0 Register 16 (after Release 1)
 */
    int32_t CP0_Config0;
#define CP0C0_M    31
#define CP0C0_K23  28    /* 30..28 */
#define CP0C0_KU   25    /* 27..25 */
#define CP0C0_MDU  20
#define CP0C0_MM   18
#define CP0C0_BM   16
#define CP0C0_Impl 16    /* 24..16 */
#define CP0C0_BE   15
#define CP0C0_AT   13    /* 14..13 */
#define CP0C0_AR   10    /* 12..10 */
#define CP0C0_MT   7     /*  9..7  */
#define CP0C0_VI   3
#define CP0C0_K0   0     /*  2..0  */
#define CP0C0_AR_LENGTH 3
/*
 * CP0 Register 16 (before Release 1)
 */
#define CP0C0_Impl 16    /* 24..16 */
#define CP0C0_IC   9     /* 11..9 */
#define CP0C0_DC   6     /*  8..6 */
#define CP0C0_IB   5
#define CP0C0_DB   4
    int32_t CP0_Config1;
#define CP0C1_M    31
#define CP0C1_MMU  25    /* 30..25 */
#define CP0C1_IS   22    /* 24..22 */
#define CP0C1_IL   19    /* 21..19 */
#define CP0C1_IA   16    /* 18..16 */
#define CP0C1_DS   13    /* 15..13 */
#define CP0C1_DL   10    /* 12..10 */
#define CP0C1_DA   7     /*  9..7  */
#define CP0C1_C2   6
#define CP0C1_MD   5
#define CP0C1_PC   4
#define CP0C1_WR   3
#define CP0C1_CA   2
#define CP0C1_EP   1
#define CP0C1_FP   0
    int32_t CP0_Config2;
#define CP0C2_M    31
#define CP0C2_TU   28    /* 30..28 */
#define CP0C2_TS   24    /* 27..24 */
#define CP0C2_TL   20    /* 23..20 */
#define CP0C2_TA   16    /* 19..16 */
#define CP0C2_SU   12    /* 15..12 */
#define CP0C2_SS   8     /* 11..8  */
#define CP0C2_SL   4     /*  7..4  */
#define CP0C2_SA   0     /*  3..0  */
    int32_t CP0_Config3;
#define CP0C3_M            31
#define CP0C3_BPG          30
#define CP0C3_CMGCR        29
#define CP0C3_MSAP         28
#define CP0C3_BP           27
#define CP0C3_BI           26
#define CP0C3_SC           25
#define CP0C3_PW           24
#define CP0C3_VZ           23
#define CP0C3_IPLV         21    /* 22..21 */
#define CP0C3_MMAR         18    /* 20..18 */
#define CP0C3_MCU          17
#define CP0C3_ISA_ON_EXC   16
#define CP0C3_ISA          14    /* 15..14 */
#define CP0C3_ULRI         13
#define CP0C3_RXI          12
#define CP0C3_DSP2P        11
#define CP0C3_DSPP         10
#define CP0C3_CTXTC        9
#define CP0C3_ITL          8
#define CP0C3_LPA          7
#define CP0C3_VEIC         6
#define CP0C3_VInt         5
#define CP0C3_SP           4
#define CP0C3_CDMM         3
#define CP0C3_MT           2
#define CP0C3_SM           1
#define CP0C3_TL           0
    int32_t CP0_Config4;
    int32_t CP0_Config4_rw_bitmask;
#define CP0C4_M            31
#define CP0C4_IE           29    /* 30..29 */
#define CP0C4_AE           28
#define CP0C4_VTLBSizeExt  24    /* 27..24 */
#define CP0C4_KScrExist    16
#define CP0C4_MMUExtDef    14
#define CP0C4_FTLBPageSize 8     /* 12..8  */
/* bit layout if MMUExtDef=1 */
#define CP0C4_MMUSizeExt   0     /*  7..0  */
/* bit layout if MMUExtDef=2 */
#define CP0C4_FTLBWays     4     /*  7..4  */
#define CP0C4_FTLBSets     0     /*  3..0  */
    int32_t CP0_Config5;
    int32_t CP0_Config5_rw_bitmask;
#define CP0C5_M            31
#define CP0C5_K            30
#define CP0C5_CV           29
#define CP0C5_EVA          28
#define CP0C5_MSAEn        27
#define CP0C5_PMJ          23    /* 25..23 */
#define CP0C5_WR2          22
#define CP0C5_NMS          21
#define CP0C5_ULS          20
#define CP0C5_XPA          19
#define CP0C5_CRCP         18
#define CP0C5_MI           17
#define CP0C5_GI           15    /* 16..15 */
#define CP0C5_CA2          14
#define CP0C5_XNP          13
#define CP0C5_DEC          11
#define CP0C5_L2C          10
#define CP0C5_UFE          9
#define CP0C5_FRE          8
#define CP0C5_VP           7
#define CP0C5_SBRI         6
#define CP0C5_MVH          5
#define CP0C5_LLB          4
#define CP0C5_MRP          3
#define CP0C5_UFR          2
#define CP0C5_NFExists     0
    int32_t CP0_Config6;
    int32_t CP0_Config6_rw_bitmask;
#define CP0C6_BPPASS          31
#define CP0C6_KPOS            24
#define CP0C6_KE              23
#define CP0C6_VTLBONLY        22
#define CP0C6_LASX            21
#define CP0C6_SSEN            20
#define CP0C6_DISDRTIME       19
#define CP0C6_PIXNUEN         18
#define CP0C6_SCRAND          17
#define CP0C6_LLEXCEN         16
#define CP0C6_DISVC           15
#define CP0C6_VCLRU           14
#define CP0C6_DCLRU           13
#define CP0C6_PIXUEN          12
#define CP0C6_DISBLKLYEN      11
#define CP0C6_UMEMUALEN       10
#define CP0C6_SFBEN           8
#define CP0C6_FLTINT          7
#define CP0C6_VLTINT          6
#define CP0C6_DISBTB          5
#define CP0C6_STPREFCTL       2
#define CP0C6_INSTPREF        1
#define CP0C6_DATAPREF        0
    int32_t CP0_Config7;
    int64_t CP0_Config7_rw_bitmask;
#define CP0C7_NAPCGEN       2
#define CP0C7_UNIMUEN       1
#define CP0C7_VFPUCGEN      0
    uint64_t CP0_LLAddr;
    uint64_t CP0_MAAR[MIPS_MAAR_MAX];
    int32_t CP0_MAARI;
    /* XXX: Maybe make LLAddr per-TC? */
/*
 * CP0 Register 17
 */
    target_ulong lladdr; /* LL virtual address compared against SC */
    target_ulong llval;
    uint64_t llval_wp;
    uint32_t llnewval_wp;
    uint64_t CP0_LLAddr_rw_bitmask;
    int CP0_LLAddr_shift;
/*
 * CP0 Register 18
 */
    target_ulong CP0_WatchLo[8];
/*
 * CP0 Register 19
 */
    uint64_t CP0_WatchHi[8];
#define CP0WH_ASID 16
/*
 * CP0 Register 20
 */
    target_ulong CP0_XContext;
    int32_t CP0_Framemask;
/*
 * CP0 Register 23
 */
    int32_t CP0_Debug;
#define CP0DB_DBD  31
#define CP0DB_DM   30
#define CP0DB_LSNM 28
#define CP0DB_Doze 27
#define CP0DB_Halt 26
#define CP0DB_CNT  25
#define CP0DB_IBEP 24
#define CP0DB_DBEP 21
#define CP0DB_IEXI 20
#define CP0DB_VER  15
#define CP0DB_DEC  10
#define CP0DB_SSt  8
#define CP0DB_DINT 5
#define CP0DB_DIB  4
#define CP0DB_DDBS 3
#define CP0DB_DDBL 2
#define CP0DB_DBp  1
#define CP0DB_DSS  0
/*
 * CP0 Register 24
 */
    target_ulong CP0_DEPC;
/*
 * CP0 Register 25
 */
    int32_t CP0_Performance0;
/*
 * CP0 Register 26
 */
    int32_t CP0_ErrCtl;
#define CP0EC_WST 29
#define CP0EC_SPR 28
#define CP0EC_ITC 26
/*
 * CP0 Register 28
 */
    uint64_t CP0_TagLo;
    int32_t CP0_DataLo;
/*
 * CP0 Register 29
 */
    int32_t CP0_TagHi;
    int32_t CP0_DataHi;
/*
 * CP0 Register 30
 */
    target_ulong CP0_ErrorEPC;
/*
 * CP0 Register 31
 */
    int32_t CP0_DESAVE;
    target_ulong CP0_KScratch[MIPS_KSCRATCH_NUM];

    /* We waste some space so we can handle shadow registers like TCs. */
    TCState tcs[MIPS_SHADOW_SET_MAX];
    CPUMIPSFPUContext fpus[MIPS_FPU_MAX];
    /* QEMU */
    int error_code;
#define EXCP_TLB_NOMATCH   0x1
#define EXCP_INST_NOTAVAIL 0x2 /* No valid instruction word for BadInstr */
    uint32_t hflags;    /* CPU State */
    /* TMASK defines different execution modes */
#define MIPS_HFLAG_TMASK  0x1F5807FF
#define MIPS_HFLAG_MODE   0x00007 /* execution modes                    */
    /*
     * The KSU flags must be the lowest bits in hflags. The flag order
     * must be the same as defined for CP0 Status. This allows to use
     * the bits as the value of mmu_idx.
     */
#define MIPS_HFLAG_KSU    0x00003 /* kernel/supervisor/user mode mask   */
#define MIPS_HFLAG_UM     0x00002 /* user mode flag                     */
#define MIPS_HFLAG_SM     0x00001 /* supervisor mode flag               */
#define MIPS_HFLAG_KM     0x00000 /* kernel mode flag                   */
#define MIPS_HFLAG_DM     0x00004 /* Debug mode                         */
#define MIPS_HFLAG_64     0x00008 /* 64-bit instructions enabled        */
#define MIPS_HFLAG_CP0    0x00010 /* CP0 enabled                        */
#define MIPS_HFLAG_FPU    0x00020 /* FPU enabled                        */
#define MIPS_HFLAG_F64    0x00040 /* 64-bit FPU enabled                 */
    /*
     * True if the MIPS IV COP1X instructions can be used.  This also
     * controls the non-COP1X instructions RECIP.S, RECIP.D, RSQRT.S
     * and RSQRT.D.
     */
#define MIPS_HFLAG_COP1X  0x00080 /* COP1X instructions enabled         */
#define MIPS_HFLAG_RE     0x00100 /* Reversed endianness                */
#define MIPS_HFLAG_AWRAP  0x00200 /* 32-bit compatibility address wrapping */
#define MIPS_HFLAG_M16    0x00400 /* MIPS16 mode flag                   */
#define MIPS_HFLAG_M16_SHIFT 10
    /*
     * If translation is interrupted between the branch instruction and
     * the delay slot, record what type of branch it is so that we can
     * resume translation properly.  It might be possible to reduce
     * this from three bits to two.
     */
#define MIPS_HFLAG_BMASK_BASE  0x803800
#define MIPS_HFLAG_B      0x00800 /* Unconditional branch               */
#define MIPS_HFLAG_BC     0x01000 /* Conditional branch                 */
#define MIPS_HFLAG_BL     0x01800 /* Likely branch                      */
#define MIPS_HFLAG_BR     0x02000 /* branch to register (can't link TB) */
    /* Extra flags about the current pending branch.  */
#define MIPS_HFLAG_BMASK_EXT 0x7C000
#define MIPS_HFLAG_B16    0x04000 /* branch instruction was 16 bits     */
#define MIPS_HFLAG_BDS16  0x08000 /* branch requires 16-bit delay slot  */
#define MIPS_HFLAG_BDS32  0x10000 /* branch requires 32-bit delay slot  */
#define MIPS_HFLAG_BDS_STRICT  0x20000 /* Strict delay slot size */
#define MIPS_HFLAG_BX     0x40000 /* branch exchanges execution mode    */
#define MIPS_HFLAG_BMASK  (MIPS_HFLAG_BMASK_BASE | MIPS_HFLAG_BMASK_EXT)
    /* MIPS DSP resources access. */
#define MIPS_HFLAG_DSP    0x080000   /* Enable access to DSP resources.    */
#define MIPS_HFLAG_DSP_R2 0x100000   /* Enable access to DSP R2 resources. */
#define MIPS_HFLAG_DSP_R3 0x20000000 /* Enable access to DSP R3 resources. */
    /* Extra flag about HWREna register. */
#define MIPS_HFLAG_HWRENA_ULR 0x200000 /* ULR bit from HWREna is set. */
#define MIPS_HFLAG_SBRI  0x400000 /* R6 SDBBP causes RI excpt. in user mode */
#define MIPS_HFLAG_FBNSLOT 0x800000 /* Forbidden slot                   */
#define MIPS_HFLAG_MSA   0x1000000
#define MIPS_HFLAG_FRE   0x2000000 /* FRE enabled */
#define MIPS_HFLAG_ELPA  0x4000000
#define MIPS_HFLAG_ITC_CACHE  0x8000000 /* CACHE instr. operates on ITC tag */
#define MIPS_HFLAG_ERL   0x10000000 /* error level flag */
    target_ulong btarget;        /* Jump / branch target               */
    target_ulong bcond;          /* Branch condition (if needed)       */

    int SYNCI_Step; /* Address step size for SYNCI */
    int CCRes; /* Cycle count resolution/divisor */
    uint32_t CP0_Status_rw_bitmask; /* Read/write bits in CP0_Status */
    uint32_t CP0_TCStatus_rw_bitmask; /* Read/write bits in CP0_TCStatus */
    uint64_t insn_flags; /* Supported instruction set */
    int saarp;

    /* Fields up to this point are cleared by a CPU reset */
    struct {} end_reset_fields;

    /* Fields from here on are preserved across CPU reset. */
    CPUMIPSMVPContext *mvp;
#if !defined(CONFIG_USER_ONLY)
    CPUMIPSTLBContext *tlb;
    void *irq[8];
    struct MIPSITUState *itu;
    MemoryRegion *itc_tag; /* ITC Configuration Tags */
#endif

    const mips_def_t *cpu_model;
    QEMUTimer *timer; /* Internal timer */
    target_ulong exception_base; /* ExceptionBase input to the core */
    uint64_t cp0_count_ns; /* CP0_Count clock period (in nanoseconds) */
} CPUMIPSState;

/**
 * MIPSCPU:
 * @env: #CPUMIPSState
 * @clock: this CPU input clock (may be connected
 *         to an output clock from another device).
 *
 * A MIPS CPU.
 */
struct ArchCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    Clock *clock;
    CPUNegativeOffsetState neg;
    CPUMIPSState env;
};


void mips_cpu_list(void);

#define cpu_list mips_cpu_list

extern void cpu_wrdsp(uint32_t rs, uint32_t mask_num, CPUMIPSState *env);
extern uint32_t cpu_rddsp(uint32_t mask_num, CPUMIPSState *env);

/*
 * MMU modes definitions. We carefully match the indices with our
 * hflags layout.
 */
#define MMU_USER_IDX 2

static inline int hflags_mmu_index(uint32_t hflags)
{
    if (hflags & MIPS_HFLAG_ERL) {
        return 3; /* ERL */
    } else {
        return hflags & MIPS_HFLAG_KSU;
    }
}

static inline int cpu_mmu_index(CPUMIPSState *env, bool ifetch)
{
    return hflags_mmu_index(env->hflags);
}

#include "exec/cpu-all.h"

/* Exceptions */
enum {
    EXCP_NONE          = -1,
    EXCP_RESET         = 0,
    EXCP_SRESET,
    EXCP_DSS,
    EXCP_DINT,
    EXCP_DDBL,
    EXCP_DDBS,
    EXCP_NMI,
    EXCP_MCHECK,
    EXCP_EXT_INTERRUPT, /* 8 */
    EXCP_DFWATCH,
    EXCP_DIB,
    EXCP_IWATCH,
    EXCP_AdEL,
    EXCP_AdES,
    EXCP_TLBF,
    EXCP_IBE,
    EXCP_DBp, /* 16 */
    EXCP_SYSCALL,
    EXCP_BREAK,
    EXCP_CpU,
    EXCP_RI,
    EXCP_OVERFLOW,
    EXCP_TRAP,
    EXCP_FPE,
    EXCP_DWATCH, /* 24 */
    EXCP_LTLBL,
    EXCP_TLBL,
    EXCP_TLBS,
    EXCP_DBE,
    EXCP_THREAD,
    EXCP_MDMX,
    EXCP_C2E,
    EXCP_CACHE, /* 32 */
    EXCP_DSPDIS,
    EXCP_MSADIS,
    EXCP_MSAFPE,
    EXCP_TLBXI,
    EXCP_TLBRI,

    EXCP_LAST = EXCP_TLBRI,
};

/*
 * This is an internally generated WAKE request line.
 * It is driven by the CPU itself. Raised when the MT
 * block wants to wake a VPE from an inactive state and
 * cleared when VPE goes from active to inactive.
 */
#define CPU_INTERRUPT_WAKE CPU_INTERRUPT_TGT_INT_0

#define MIPS_CPU_TYPE_SUFFIX "-" TYPE_MIPS_CPU
#define MIPS_CPU_TYPE_NAME(model) model MIPS_CPU_TYPE_SUFFIX
#define CPU_RESOLVING_TYPE TYPE_MIPS_CPU

bool cpu_type_supports_cps_smp(const char *cpu_type);
bool cpu_supports_isa(const CPUMIPSState *env, uint64_t isa_mask);
bool cpu_type_supports_isa(const char *cpu_type, uint64_t isa);

/* Check presence of MSA implementation */
static inline bool ase_msa_available(CPUMIPSState *env)
{
    return env->CP0_Config3 & (1 << CP0C3_MSAP);
}

/* Check presence of multi-threading ASE implementation */
static inline bool ase_mt_available(CPUMIPSState *env)
{
    return env->CP0_Config3 & (1 << CP0C3_MT);
}

static inline bool cpu_type_is_64bit(const char *cpu_type)
{
    return cpu_type_supports_isa(cpu_type, CPU_MIPS64);
}

void cpu_set_exception_base(int vp_index, target_ulong address);

/* addr.c */
uint64_t cpu_mips_kseg0_to_phys(void *opaque, uint64_t addr);
uint64_t cpu_mips_phys_to_kseg0(void *opaque, uint64_t addr);

uint64_t cpu_mips_kvm_um_phys_to_kseg0(void *opaque, uint64_t addr);
uint64_t cpu_mips_kseg1_to_phys(void *opaque, uint64_t addr);
uint64_t cpu_mips_phys_to_kseg1(void *opaque, uint64_t addr);
bool mips_um_ksegs_enabled(void);
void mips_um_ksegs_enable(void);

#if !defined(CONFIG_USER_ONLY)

/* mips_int.c */
void cpu_mips_soft_irq(CPUMIPSState *env, int irq, int level);

/* mips_itu.c */
void itc_reconfigure(struct MIPSITUState *tag);

#endif /* !CONFIG_USER_ONLY */

/* helper.c */
target_ulong exception_resume_pc(CPUMIPSState *env);

static inline void cpu_get_tb_cpu_state(CPUMIPSState *env, target_ulong *pc,
                                        target_ulong *cs_base, uint32_t *flags)
{
    *pc = env->active_tc.PC;
    *cs_base = 0;
    *flags = env->hflags & (MIPS_HFLAG_TMASK | MIPS_HFLAG_BMASK |
                            MIPS_HFLAG_HWRENA_ULR);
}

/**
 * mips_cpu_create_with_clock:
 * @typename: a MIPS CPU type.
 * @cpu_refclk: this cpu input clock (an output clock of another device)
 *
 * Instantiates a MIPS CPU, set the input clock of the CPU to @cpu_refclk,
 * then realizes the CPU.
 *
 * Returns: A #CPUState or %NULL if an error occurred.
 */
MIPSCPU *mips_cpu_create_with_clock(const char *cpu_type, Clock *cpu_refclk);

#endif /* MIPS_CPU_H */
