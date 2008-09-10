#include "exec.h"
#include "host-utils.h"
#include "helper.h"
#if !defined(CONFIG_USER_ONLY)
#include "softmmu_exec.h"
#endif /* !defined(CONFIG_USER_ONLY) */

//#define DEBUG_MMU
//#define DEBUG_MXCC
//#define DEBUG_UNALIGNED
//#define DEBUG_UNASSIGNED
//#define DEBUG_ASI

#ifdef DEBUG_MMU
#define DPRINTF_MMU(fmt, args...) \
do { printf("MMU: " fmt , ##args); } while (0)
#else
#define DPRINTF_MMU(fmt, args...) do {} while (0)
#endif

#ifdef DEBUG_MXCC
#define DPRINTF_MXCC(fmt, args...) \
do { printf("MXCC: " fmt , ##args); } while (0)
#else
#define DPRINTF_MXCC(fmt, args...) do {} while (0)
#endif

#ifdef DEBUG_ASI
#define DPRINTF_ASI(fmt, args...) \
do { printf("ASI: " fmt , ##args); } while (0)
#else
#define DPRINTF_ASI(fmt, args...) do {} while (0)
#endif

#ifdef TARGET_SPARC64
#ifndef TARGET_ABI32
#define AM_CHECK(env1) ((env1)->pstate & PS_AM)
#else
#define AM_CHECK(env1) (1)
#endif
#endif

static inline void address_mask(CPUState *env1, target_ulong *addr)
{
#ifdef TARGET_SPARC64
    if (AM_CHECK(env1))
        *addr &= 0xffffffffULL;
#endif
}

void raise_exception(int tt)
{
    env->exception_index = tt;
    cpu_loop_exit();
}

void helper_trap(target_ulong nb_trap)
{
    env->exception_index = TT_TRAP + (nb_trap & 0x7f);
    cpu_loop_exit();
}

void helper_trapcc(target_ulong nb_trap, target_ulong do_trap)
{
    if (do_trap) {
        env->exception_index = TT_TRAP + (nb_trap & 0x7f);
        cpu_loop_exit();
    }
}

static inline void set_cwp(int new_cwp)
{
    cpu_set_cwp(env, new_cwp);
}

void helper_check_align(target_ulong addr, uint32_t align)
{
    if (addr & align) {
#ifdef DEBUG_UNALIGNED
    printf("Unaligned access to 0x" TARGET_FMT_lx " from 0x" TARGET_FMT_lx
           "\n", addr, env->pc);
#endif
        raise_exception(TT_UNALIGNED);
    }
}

#define F_HELPER(name, p) void helper_f##name##p(void)

#define F_BINOP(name)                                           \
    float32 helper_f ## name ## s (float32 src1, float32 src2)  \
    {                                                           \
        return float32_ ## name (src1, src2, &env->fp_status);  \
    }                                                           \
    F_HELPER(name, d)                                           \
    {                                                           \
        DT0 = float64_ ## name (DT0, DT1, &env->fp_status);     \
    }                                                           \
    F_HELPER(name, q)                                           \
    {                                                           \
        QT0 = float128_ ## name (QT0, QT1, &env->fp_status);    \
    }

F_BINOP(add);
F_BINOP(sub);
F_BINOP(mul);
F_BINOP(div);
#undef F_BINOP

void helper_fsmuld(void)
{
    DT0 = float64_mul(float32_to_float64(FT0, &env->fp_status),
                      float32_to_float64(FT1, &env->fp_status),
                      &env->fp_status);
}

void helper_fdmulq(void)
{
    QT0 = float128_mul(float64_to_float128(DT0, &env->fp_status),
                       float64_to_float128(DT1, &env->fp_status),
                       &env->fp_status);
}

float32 helper_fnegs(float32 src)
{
    return float32_chs(src);
}

#ifdef TARGET_SPARC64
F_HELPER(neg, d)
{
    DT0 = float64_chs(DT1);
}

F_HELPER(neg, q)
{
    QT0 = float128_chs(QT1);
}
#endif

/* Integer to float conversion.  */
float32 helper_fitos(int32_t src)
{
    return int32_to_float32(src, &env->fp_status);
}

F_HELPER(ito, d)
{
    DT0 = int32_to_float64(*((int32_t *)&FT1), &env->fp_status);
}

F_HELPER(ito, q)
{
    QT0 = int32_to_float128(*((int32_t *)&FT1), &env->fp_status);
}

#ifdef TARGET_SPARC64
F_HELPER(xto, s)
{
    FT0 = int64_to_float32(*((int64_t *)&DT1), &env->fp_status);
}

F_HELPER(xto, d)
{
    DT0 = int64_to_float64(*((int64_t *)&DT1), &env->fp_status);
}

F_HELPER(xto, q)
{
    QT0 = int64_to_float128(*((int64_t *)&DT1), &env->fp_status);
}
#endif
#undef F_HELPER

/* floating point conversion */
void helper_fdtos(void)
{
    FT0 = float64_to_float32(DT1, &env->fp_status);
}

void helper_fstod(void)
{
    DT0 = float32_to_float64(FT1, &env->fp_status);
}

void helper_fqtos(void)
{
    FT0 = float128_to_float32(QT1, &env->fp_status);
}

void helper_fstoq(void)
{
    QT0 = float32_to_float128(FT1, &env->fp_status);
}

void helper_fqtod(void)
{
    DT0 = float128_to_float64(QT1, &env->fp_status);
}

void helper_fdtoq(void)
{
    QT0 = float64_to_float128(DT1, &env->fp_status);
}

/* Float to integer conversion.  */
int32_t helper_fstoi(float32 src)
{
    return float32_to_int32_round_to_zero(src, &env->fp_status);
}

void helper_fdtoi(void)
{
    *((int32_t *)&FT0) = float64_to_int32_round_to_zero(DT1, &env->fp_status);
}

void helper_fqtoi(void)
{
    *((int32_t *)&FT0) = float128_to_int32_round_to_zero(QT1, &env->fp_status);
}

#ifdef TARGET_SPARC64
void helper_fstox(void)
{
    *((int64_t *)&DT0) = float32_to_int64_round_to_zero(FT1, &env->fp_status);
}

void helper_fdtox(void)
{
    *((int64_t *)&DT0) = float64_to_int64_round_to_zero(DT1, &env->fp_status);
}

void helper_fqtox(void)
{
    *((int64_t *)&DT0) = float128_to_int64_round_to_zero(QT1, &env->fp_status);
}

void helper_faligndata(void)
{
    uint64_t tmp;

    tmp = (*((uint64_t *)&DT0)) << ((env->gsr & 7) * 8);
    /* on many architectures a shift of 64 does nothing */
    if ((env->gsr & 7) != 0) {
        tmp |= (*((uint64_t *)&DT1)) >> (64 - (env->gsr & 7) * 8);
    }
    *((uint64_t *)&DT0) = tmp;
}

void helper_movl_FT0_0(void)
{
    *((uint32_t *)&FT0) = 0;
}

void helper_movl_DT0_0(void)
{
    *((uint64_t *)&DT0) = 0;
}

void helper_movl_FT0_1(void)
{
    *((uint32_t *)&FT0) = 0xffffffff;
}

void helper_movl_DT0_1(void)
{
    *((uint64_t *)&DT0) = 0xffffffffffffffffULL;
}

void helper_fnot(void)
{
    *(uint64_t *)&DT0 = ~*(uint64_t *)&DT1;
}

void helper_fnots(void)
{
    *(uint32_t *)&FT0 = ~*(uint32_t *)&FT1;
}

void helper_fnor(void)
{
    *(uint64_t *)&DT0 = ~(*(uint64_t *)&DT0 | *(uint64_t *)&DT1);
}

void helper_fnors(void)
{
    *(uint32_t *)&FT0 = ~(*(uint32_t *)&FT0 | *(uint32_t *)&FT1);
}

void helper_for(void)
{
    *(uint64_t *)&DT0 |= *(uint64_t *)&DT1;
}

void helper_fors(void)
{
    *(uint32_t *)&FT0 |= *(uint32_t *)&FT1;
}

void helper_fxor(void)
{
    *(uint64_t *)&DT0 ^= *(uint64_t *)&DT1;
}

void helper_fxors(void)
{
    *(uint32_t *)&FT0 ^= *(uint32_t *)&FT1;
}

void helper_fand(void)
{
    *(uint64_t *)&DT0 &= *(uint64_t *)&DT1;
}

void helper_fands(void)
{
    *(uint32_t *)&FT0 &= *(uint32_t *)&FT1;
}

void helper_fornot(void)
{
    *(uint64_t *)&DT0 = *(uint64_t *)&DT0 | ~*(uint64_t *)&DT1;
}

void helper_fornots(void)
{
    *(uint32_t *)&FT0 = *(uint32_t *)&FT0 | ~*(uint32_t *)&FT1;
}

void helper_fandnot(void)
{
    *(uint64_t *)&DT0 = *(uint64_t *)&DT0 & ~*(uint64_t *)&DT1;
}

void helper_fandnots(void)
{
    *(uint32_t *)&FT0 = *(uint32_t *)&FT0 & ~*(uint32_t *)&FT1;
}

void helper_fnand(void)
{
    *(uint64_t *)&DT0 = ~(*(uint64_t *)&DT0 & *(uint64_t *)&DT1);
}

void helper_fnands(void)
{
    *(uint32_t *)&FT0 = ~(*(uint32_t *)&FT0 & *(uint32_t *)&FT1);
}

void helper_fxnor(void)
{
    *(uint64_t *)&DT0 ^= ~*(uint64_t *)&DT1;
}

void helper_fxnors(void)
{
    *(uint32_t *)&FT0 ^= ~*(uint32_t *)&FT1;
}

#ifdef WORDS_BIGENDIAN
#define VIS_B64(n) b[7 - (n)]
#define VIS_W64(n) w[3 - (n)]
#define VIS_SW64(n) sw[3 - (n)]
#define VIS_L64(n) l[1 - (n)]
#define VIS_B32(n) b[3 - (n)]
#define VIS_W32(n) w[1 - (n)]
#else
#define VIS_B64(n) b[n]
#define VIS_W64(n) w[n]
#define VIS_SW64(n) sw[n]
#define VIS_L64(n) l[n]
#define VIS_B32(n) b[n]
#define VIS_W32(n) w[n]
#endif

typedef union {
    uint8_t b[8];
    uint16_t w[4];
    int16_t sw[4];
    uint32_t l[2];
    float64 d;
} vis64;

typedef union {
    uint8_t b[4];
    uint16_t w[2];
    uint32_t l;
    float32 f;
} vis32;

void helper_fpmerge(void)
{
    vis64 s, d;

    s.d = DT0;
    d.d = DT1;

    // Reverse calculation order to handle overlap
    d.VIS_B64(7) = s.VIS_B64(3);
    d.VIS_B64(6) = d.VIS_B64(3);
    d.VIS_B64(5) = s.VIS_B64(2);
    d.VIS_B64(4) = d.VIS_B64(2);
    d.VIS_B64(3) = s.VIS_B64(1);
    d.VIS_B64(2) = d.VIS_B64(1);
    d.VIS_B64(1) = s.VIS_B64(0);
    //d.VIS_B64(0) = d.VIS_B64(0);

    DT0 = d.d;
}

void helper_fmul8x16(void)
{
    vis64 s, d;
    uint32_t tmp;

    s.d = DT0;
    d.d = DT1;

#define PMUL(r)                                                 \
    tmp = (int32_t)d.VIS_SW64(r) * (int32_t)s.VIS_B64(r);       \
    if ((tmp & 0xff) > 0x7f)                                    \
        tmp += 0x100;                                           \
    d.VIS_W64(r) = tmp >> 8;

    PMUL(0);
    PMUL(1);
    PMUL(2);
    PMUL(3);
#undef PMUL

    DT0 = d.d;
}

void helper_fmul8x16al(void)
{
    vis64 s, d;
    uint32_t tmp;

    s.d = DT0;
    d.d = DT1;

#define PMUL(r)                                                 \
    tmp = (int32_t)d.VIS_SW64(1) * (int32_t)s.VIS_B64(r);       \
    if ((tmp & 0xff) > 0x7f)                                    \
        tmp += 0x100;                                           \
    d.VIS_W64(r) = tmp >> 8;

    PMUL(0);
    PMUL(1);
    PMUL(2);
    PMUL(3);
#undef PMUL

    DT0 = d.d;
}

void helper_fmul8x16au(void)
{
    vis64 s, d;
    uint32_t tmp;

    s.d = DT0;
    d.d = DT1;

#define PMUL(r)                                                 \
    tmp = (int32_t)d.VIS_SW64(0) * (int32_t)s.VIS_B64(r);       \
    if ((tmp & 0xff) > 0x7f)                                    \
        tmp += 0x100;                                           \
    d.VIS_W64(r) = tmp >> 8;

    PMUL(0);
    PMUL(1);
    PMUL(2);
    PMUL(3);
#undef PMUL

    DT0 = d.d;
}

void helper_fmul8sux16(void)
{
    vis64 s, d;
    uint32_t tmp;

    s.d = DT0;
    d.d = DT1;

#define PMUL(r)                                                         \
    tmp = (int32_t)d.VIS_SW64(r) * ((int32_t)s.VIS_SW64(r) >> 8);       \
    if ((tmp & 0xff) > 0x7f)                                            \
        tmp += 0x100;                                                   \
    d.VIS_W64(r) = tmp >> 8;

    PMUL(0);
    PMUL(1);
    PMUL(2);
    PMUL(3);
#undef PMUL

    DT0 = d.d;
}

void helper_fmul8ulx16(void)
{
    vis64 s, d;
    uint32_t tmp;

    s.d = DT0;
    d.d = DT1;

#define PMUL(r)                                                         \
    tmp = (int32_t)d.VIS_SW64(r) * ((uint32_t)s.VIS_B64(r * 2));        \
    if ((tmp & 0xff) > 0x7f)                                            \
        tmp += 0x100;                                                   \
    d.VIS_W64(r) = tmp >> 8;

    PMUL(0);
    PMUL(1);
    PMUL(2);
    PMUL(3);
#undef PMUL

    DT0 = d.d;
}

void helper_fmuld8sux16(void)
{
    vis64 s, d;
    uint32_t tmp;

    s.d = DT0;
    d.d = DT1;

#define PMUL(r)                                                         \
    tmp = (int32_t)d.VIS_SW64(r) * ((int32_t)s.VIS_SW64(r) >> 8);       \
    if ((tmp & 0xff) > 0x7f)                                            \
        tmp += 0x100;                                                   \
    d.VIS_L64(r) = tmp;

    // Reverse calculation order to handle overlap
    PMUL(1);
    PMUL(0);
#undef PMUL

    DT0 = d.d;
}

void helper_fmuld8ulx16(void)
{
    vis64 s, d;
    uint32_t tmp;

    s.d = DT0;
    d.d = DT1;

#define PMUL(r)                                                         \
    tmp = (int32_t)d.VIS_SW64(r) * ((uint32_t)s.VIS_B64(r * 2));        \
    if ((tmp & 0xff) > 0x7f)                                            \
        tmp += 0x100;                                                   \
    d.VIS_L64(r) = tmp;

    // Reverse calculation order to handle overlap
    PMUL(1);
    PMUL(0);
#undef PMUL

    DT0 = d.d;
}

void helper_fexpand(void)
{
    vis32 s;
    vis64 d;

    s.l = (uint32_t)(*(uint64_t *)&DT0 & 0xffffffff);
    d.d = DT1;
    d.VIS_L64(0) = s.VIS_W32(0) << 4;
    d.VIS_L64(1) = s.VIS_W32(1) << 4;
    d.VIS_L64(2) = s.VIS_W32(2) << 4;
    d.VIS_L64(3) = s.VIS_W32(3) << 4;

    DT0 = d.d;
}

#define VIS_HELPER(name, F)                             \
    void name##16(void)                                 \
    {                                                   \
        vis64 s, d;                                     \
                                                        \
        s.d = DT0;                                      \
        d.d = DT1;                                      \
                                                        \
        d.VIS_W64(0) = F(d.VIS_W64(0), s.VIS_W64(0));   \
        d.VIS_W64(1) = F(d.VIS_W64(1), s.VIS_W64(1));   \
        d.VIS_W64(2) = F(d.VIS_W64(2), s.VIS_W64(2));   \
        d.VIS_W64(3) = F(d.VIS_W64(3), s.VIS_W64(3));   \
                                                        \
        DT0 = d.d;                                      \
    }                                                   \
                                                        \
    void name##16s(void)                                \
    {                                                   \
        vis32 s, d;                                     \
                                                        \
        s.f = FT0;                                      \
        d.f = FT1;                                      \
                                                        \
        d.VIS_W32(0) = F(d.VIS_W32(0), s.VIS_W32(0));   \
        d.VIS_W32(1) = F(d.VIS_W32(1), s.VIS_W32(1));   \
                                                        \
        FT0 = d.f;                                      \
    }                                                   \
                                                        \
    void name##32(void)                                 \
    {                                                   \
        vis64 s, d;                                     \
                                                        \
        s.d = DT0;                                      \
        d.d = DT1;                                      \
                                                        \
        d.VIS_L64(0) = F(d.VIS_L64(0), s.VIS_L64(0));   \
        d.VIS_L64(1) = F(d.VIS_L64(1), s.VIS_L64(1));   \
                                                        \
        DT0 = d.d;                                      \
    }                                                   \
                                                        \
    void name##32s(void)                                \
    {                                                   \
        vis32 s, d;                                     \
                                                        \
        s.f = FT0;                                      \
        d.f = FT1;                                      \
                                                        \
        d.l = F(d.l, s.l);                              \
                                                        \
        FT0 = d.f;                                      \
    }

#define FADD(a, b) ((a) + (b))
#define FSUB(a, b) ((a) - (b))
VIS_HELPER(helper_fpadd, FADD)
VIS_HELPER(helper_fpsub, FSUB)

#define VIS_CMPHELPER(name, F)                                        \
    void name##16(void)                                           \
    {                                                             \
        vis64 s, d;                                               \
                                                                  \
        s.d = DT0;                                                \
        d.d = DT1;                                                \
                                                                  \
        d.VIS_W64(0) = F(d.VIS_W64(0), s.VIS_W64(0))? 1: 0;       \
        d.VIS_W64(0) |= F(d.VIS_W64(1), s.VIS_W64(1))? 2: 0;      \
        d.VIS_W64(0) |= F(d.VIS_W64(2), s.VIS_W64(2))? 4: 0;      \
        d.VIS_W64(0) |= F(d.VIS_W64(3), s.VIS_W64(3))? 8: 0;      \
                                                                  \
        DT0 = d.d;                                                \
    }                                                             \
                                                                  \
    void name##32(void)                                           \
    {                                                             \
        vis64 s, d;                                               \
                                                                  \
        s.d = DT0;                                                \
        d.d = DT1;                                                \
                                                                  \
        d.VIS_L64(0) = F(d.VIS_L64(0), s.VIS_L64(0))? 1: 0;       \
        d.VIS_L64(0) |= F(d.VIS_L64(1), s.VIS_L64(1))? 2: 0;      \
                                                                  \
        DT0 = d.d;                                                \
    }

#define FCMPGT(a, b) ((a) > (b))
#define FCMPEQ(a, b) ((a) == (b))
#define FCMPLE(a, b) ((a) <= (b))
#define FCMPNE(a, b) ((a) != (b))

VIS_CMPHELPER(helper_fcmpgt, FCMPGT)
VIS_CMPHELPER(helper_fcmpeq, FCMPEQ)
VIS_CMPHELPER(helper_fcmple, FCMPLE)
VIS_CMPHELPER(helper_fcmpne, FCMPNE)
#endif

void helper_check_ieee_exceptions(void)
{
    target_ulong status;

    status = get_float_exception_flags(&env->fp_status);
    if (status) {
        /* Copy IEEE 754 flags into FSR */
        if (status & float_flag_invalid)
            env->fsr |= FSR_NVC;
        if (status & float_flag_overflow)
            env->fsr |= FSR_OFC;
        if (status & float_flag_underflow)
            env->fsr |= FSR_UFC;
        if (status & float_flag_divbyzero)
            env->fsr |= FSR_DZC;
        if (status & float_flag_inexact)
            env->fsr |= FSR_NXC;

        if ((env->fsr & FSR_CEXC_MASK) & ((env->fsr & FSR_TEM_MASK) >> 23)) {
            /* Unmasked exception, generate a trap */
            env->fsr |= FSR_FTT_IEEE_EXCP;
            raise_exception(TT_FP_EXCP);
        } else {
            /* Accumulate exceptions */
            env->fsr |= (env->fsr & FSR_CEXC_MASK) << 5;
        }
    }
}

void helper_clear_float_exceptions(void)
{
    set_float_exception_flags(0, &env->fp_status);
}

float32 helper_fabss(float32 src)
{
    return float32_abs(src);
}

#ifdef TARGET_SPARC64
void helper_fabsd(void)
{
    DT0 = float64_abs(DT1);
}

void helper_fabsq(void)
{
    QT0 = float128_abs(QT1);
}
#endif

float32 helper_fsqrts(float32 src)
{
    return float32_sqrt(src, &env->fp_status);
}

void helper_fsqrtd(void)
{
    DT0 = float64_sqrt(DT1, &env->fp_status);
}

void helper_fsqrtq(void)
{
    QT0 = float128_sqrt(QT1, &env->fp_status);
}

#define GEN_FCMP(name, size, reg1, reg2, FS, TRAP)                      \
    void glue(helper_, name) (void)                                     \
    {                                                                   \
        target_ulong new_fsr;                                           \
                                                                        \
        env->fsr &= ~((FSR_FCC1 | FSR_FCC0) << FS);                     \
        switch (glue(size, _compare) (reg1, reg2, &env->fp_status)) {   \
        case float_relation_unordered:                                  \
            new_fsr = (FSR_FCC1 | FSR_FCC0) << FS;                      \
            if ((env->fsr & FSR_NVM) || TRAP) {                         \
                env->fsr |= new_fsr;                                    \
                env->fsr |= FSR_NVC;                                    \
                env->fsr |= FSR_FTT_IEEE_EXCP;                          \
                raise_exception(TT_FP_EXCP);                            \
            } else {                                                    \
                env->fsr |= FSR_NVA;                                    \
            }                                                           \
            break;                                                      \
        case float_relation_less:                                       \
            new_fsr = FSR_FCC0 << FS;                                   \
            break;                                                      \
        case float_relation_greater:                                    \
            new_fsr = FSR_FCC1 << FS;                                   \
            break;                                                      \
        default:                                                        \
            new_fsr = 0;                                                \
            break;                                                      \
        }                                                               \
        env->fsr |= new_fsr;                                            \
    }
#define GEN_FCMPS(name, size, FS, TRAP)                                 \
    void glue(helper_, name)(float32 src1, float32 src2)                \
    {                                                                   \
        target_ulong new_fsr;                                           \
                                                                        \
        env->fsr &= ~((FSR_FCC1 | FSR_FCC0) << FS);                     \
        switch (glue(size, _compare) (src1, src2, &env->fp_status)) {   \
        case float_relation_unordered:                                  \
            new_fsr = (FSR_FCC1 | FSR_FCC0) << FS;                      \
            if ((env->fsr & FSR_NVM) || TRAP) {                         \
                env->fsr |= new_fsr;                                    \
                env->fsr |= FSR_NVC;                                    \
                env->fsr |= FSR_FTT_IEEE_EXCP;                          \
                raise_exception(TT_FP_EXCP);                            \
            } else {                                                    \
                env->fsr |= FSR_NVA;                                    \
            }                                                           \
            break;                                                      \
        case float_relation_less:                                       \
            new_fsr = FSR_FCC0 << FS;                                   \
            break;                                                      \
        case float_relation_greater:                                    \
            new_fsr = FSR_FCC1 << FS;                                   \
            break;                                                      \
        default:                                                        \
            new_fsr = 0;                                                \
            break;                                                      \
        }                                                               \
        env->fsr |= new_fsr;                                            \
    }

GEN_FCMPS(fcmps, float32, 0, 0);
GEN_FCMP(fcmpd, float64, DT0, DT1, 0, 0);

GEN_FCMPS(fcmpes, float32, 0, 1);
GEN_FCMP(fcmped, float64, DT0, DT1, 0, 1);

GEN_FCMP(fcmpq, float128, QT0, QT1, 0, 0);
GEN_FCMP(fcmpeq, float128, QT0, QT1, 0, 1);

#ifdef TARGET_SPARC64
GEN_FCMPS(fcmps_fcc1, float32, 22, 0);
GEN_FCMP(fcmpd_fcc1, float64, DT0, DT1, 22, 0);
GEN_FCMP(fcmpq_fcc1, float128, QT0, QT1, 22, 0);

GEN_FCMPS(fcmps_fcc2, float32, 24, 0);
GEN_FCMP(fcmpd_fcc2, float64, DT0, DT1, 24, 0);
GEN_FCMP(fcmpq_fcc2, float128, QT0, QT1, 24, 0);

GEN_FCMPS(fcmps_fcc3, float32, 26, 0);
GEN_FCMP(fcmpd_fcc3, float64, DT0, DT1, 26, 0);
GEN_FCMP(fcmpq_fcc3, float128, QT0, QT1, 26, 0);

GEN_FCMPS(fcmpes_fcc1, float32, 22, 1);
GEN_FCMP(fcmped_fcc1, float64, DT0, DT1, 22, 1);
GEN_FCMP(fcmpeq_fcc1, float128, QT0, QT1, 22, 1);

GEN_FCMPS(fcmpes_fcc2, float32, 24, 1);
GEN_FCMP(fcmped_fcc2, float64, DT0, DT1, 24, 1);
GEN_FCMP(fcmpeq_fcc2, float128, QT0, QT1, 24, 1);

GEN_FCMPS(fcmpes_fcc3, float32, 26, 1);
GEN_FCMP(fcmped_fcc3, float64, DT0, DT1, 26, 1);
GEN_FCMP(fcmpeq_fcc3, float128, QT0, QT1, 26, 1);
#endif
#undef GEN_FCMPS

#if !defined(TARGET_SPARC64) && !defined(CONFIG_USER_ONLY) && \
    defined(DEBUG_MXCC)
static void dump_mxcc(CPUState *env)
{
    printf("mxccdata: %016llx %016llx %016llx %016llx\n",
           env->mxccdata[0], env->mxccdata[1],
           env->mxccdata[2], env->mxccdata[3]);
    printf("mxccregs: %016llx %016llx %016llx %016llx\n"
           "          %016llx %016llx %016llx %016llx\n",
           env->mxccregs[0], env->mxccregs[1],
           env->mxccregs[2], env->mxccregs[3],
           env->mxccregs[4], env->mxccregs[5],
           env->mxccregs[6], env->mxccregs[7]);
}
#endif

#if (defined(TARGET_SPARC64) || !defined(CONFIG_USER_ONLY)) \
    && defined(DEBUG_ASI)
static void dump_asi(const char *txt, target_ulong addr, int asi, int size,
                     uint64_t r1)
{
    switch (size)
    {
    case 1:
        DPRINTF_ASI("%s "TARGET_FMT_lx " asi 0x%02x = %02" PRIx64 "\n", txt,
                    addr, asi, r1 & 0xff);
        break;
    case 2:
        DPRINTF_ASI("%s "TARGET_FMT_lx " asi 0x%02x = %04" PRIx64 "\n", txt,
                    addr, asi, r1 & 0xffff);
        break;
    case 4:
        DPRINTF_ASI("%s "TARGET_FMT_lx " asi 0x%02x = %08" PRIx64 "\n", txt,
                    addr, asi, r1 & 0xffffffff);
        break;
    case 8:
        DPRINTF_ASI("%s "TARGET_FMT_lx " asi 0x%02x = %016" PRIx64 "\n", txt,
                    addr, asi, r1);
        break;
    }
}
#endif

#ifndef TARGET_SPARC64
#ifndef CONFIG_USER_ONLY
uint64_t helper_ld_asi(target_ulong addr, int asi, int size, int sign)
{
    uint64_t ret = 0;
#if defined(DEBUG_MXCC) || defined(DEBUG_ASI)
    uint32_t last_addr = addr;
#endif

    helper_check_align(addr, size - 1);
    switch (asi) {
    case 2: /* SuperSparc MXCC registers */
        switch (addr) {
        case 0x01c00a00: /* MXCC control register */
            if (size == 8)
                ret = env->mxccregs[3];
            else
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr,
                             size);
            break;
        case 0x01c00a04: /* MXCC control register */
            if (size == 4)
                ret = env->mxccregs[3];
            else
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr,
                             size);
            break;
        case 0x01c00c00: /* Module reset register */
            if (size == 8) {
                ret = env->mxccregs[5];
                // should we do something here?
            } else
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr,
                             size);
            break;
        case 0x01c00f00: /* MBus port address register */
            if (size == 8)
                ret = env->mxccregs[7];
            else
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr,
                             size);
            break;
        default:
            DPRINTF_MXCC("%08x: unimplemented address, size: %d\n", addr,
                         size);
            break;
        }
        DPRINTF_MXCC("asi = %d, size = %d, sign = %d, "
                     "addr = %08x -> ret = %08x,"
                     "addr = %08x\n", asi, size, sign, last_addr, ret, addr);
#ifdef DEBUG_MXCC
        dump_mxcc(env);
#endif
        break;
    case 3: /* MMU probe */
        {
            int mmulev;

            mmulev = (addr >> 8) & 15;
            if (mmulev > 4)
                ret = 0;
            else
                ret = mmu_probe(env, addr, mmulev);
            DPRINTF_MMU("mmu_probe: 0x%08x (lev %d) -> 0x%08" PRIx64 "\n",
                        addr, mmulev, ret);
        }
        break;
    case 4: /* read MMU regs */
        {
            int reg = (addr >> 8) & 0x1f;

            ret = env->mmuregs[reg];
            if (reg == 3) /* Fault status cleared on read */
                env->mmuregs[3] = 0;
            else if (reg == 0x13) /* Fault status read */
                ret = env->mmuregs[3];
            else if (reg == 0x14) /* Fault address read */
                ret = env->mmuregs[4];
            DPRINTF_MMU("mmu_read: reg[%d] = 0x%08" PRIx64 "\n", reg, ret);
        }
        break;
    case 5: // Turbosparc ITLB Diagnostic
    case 6: // Turbosparc DTLB Diagnostic
    case 7: // Turbosparc IOTLB Diagnostic
        break;
    case 9: /* Supervisor code access */
        switch(size) {
        case 1:
            ret = ldub_code(addr);
            break;
        case 2:
            ret = lduw_code(addr);
            break;
        default:
        case 4:
            ret = ldl_code(addr);
            break;
        case 8:
            ret = ldq_code(addr);
            break;
        }
        break;
    case 0xa: /* User data access */
        switch(size) {
        case 1:
            ret = ldub_user(addr);
            break;
        case 2:
            ret = lduw_user(addr);
            break;
        default:
        case 4:
            ret = ldl_user(addr);
            break;
        case 8:
            ret = ldq_user(addr);
            break;
        }
        break;
    case 0xb: /* Supervisor data access */
        switch(size) {
        case 1:
            ret = ldub_kernel(addr);
            break;
        case 2:
            ret = lduw_kernel(addr);
            break;
        default:
        case 4:
            ret = ldl_kernel(addr);
            break;
        case 8:
            ret = ldq_kernel(addr);
            break;
        }
        break;
    case 0xc: /* I-cache tag */
    case 0xd: /* I-cache data */
    case 0xe: /* D-cache tag */
    case 0xf: /* D-cache data */
        break;
    case 0x20: /* MMU passthrough */
        switch(size) {
        case 1:
            ret = ldub_phys(addr);
            break;
        case 2:
            ret = lduw_phys(addr);
            break;
        default:
        case 4:
            ret = ldl_phys(addr);
            break;
        case 8:
            ret = ldq_phys(addr);
            break;
        }
        break;
    case 0x21 ... 0x2f: /* MMU passthrough, 0x100000000 to 0xfffffffff */
        switch(size) {
        case 1:
            ret = ldub_phys((target_phys_addr_t)addr
                            | ((target_phys_addr_t)(asi & 0xf) << 32));
            break;
        case 2:
            ret = lduw_phys((target_phys_addr_t)addr
                            | ((target_phys_addr_t)(asi & 0xf) << 32));
            break;
        default:
        case 4:
            ret = ldl_phys((target_phys_addr_t)addr
                           | ((target_phys_addr_t)(asi & 0xf) << 32));
            break;
        case 8:
            ret = ldq_phys((target_phys_addr_t)addr
                           | ((target_phys_addr_t)(asi & 0xf) << 32));
            break;
        }
        break;
    case 0x30: // Turbosparc secondary cache diagnostic
    case 0x31: // Turbosparc RAM snoop
    case 0x32: // Turbosparc page table descriptor diagnostic
    case 0x39: /* data cache diagnostic register */
        ret = 0;
        break;
    case 8: /* User code access, XXX */
    default:
        do_unassigned_access(addr, 0, 0, asi);
        ret = 0;
        break;
    }
    if (sign) {
        switch(size) {
        case 1:
            ret = (int8_t) ret;
            break;
        case 2:
            ret = (int16_t) ret;
            break;
        case 4:
            ret = (int32_t) ret;
            break;
        default:
            break;
        }
    }
#ifdef DEBUG_ASI
    dump_asi("read ", last_addr, asi, size, ret);
#endif
    return ret;
}

void helper_st_asi(target_ulong addr, uint64_t val, int asi, int size)
{
    helper_check_align(addr, size - 1);
    switch(asi) {
    case 2: /* SuperSparc MXCC registers */
        switch (addr) {
        case 0x01c00000: /* MXCC stream data register 0 */
            if (size == 8)
                env->mxccdata[0] = val;
            else
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr,
                             size);
            break;
        case 0x01c00008: /* MXCC stream data register 1 */
            if (size == 8)
                env->mxccdata[1] = val;
            else
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr,
                             size);
            break;
        case 0x01c00010: /* MXCC stream data register 2 */
            if (size == 8)
                env->mxccdata[2] = val;
            else
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr,
                             size);
            break;
        case 0x01c00018: /* MXCC stream data register 3 */
            if (size == 8)
                env->mxccdata[3] = val;
            else
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr,
                             size);
            break;
        case 0x01c00100: /* MXCC stream source */
            if (size == 8)
                env->mxccregs[0] = val;
            else
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr,
                             size);
            env->mxccdata[0] = ldq_phys((env->mxccregs[0] & 0xffffffffULL) +
                                        0);
            env->mxccdata[1] = ldq_phys((env->mxccregs[0] & 0xffffffffULL) +
                                        8);
            env->mxccdata[2] = ldq_phys((env->mxccregs[0] & 0xffffffffULL) +
                                        16);
            env->mxccdata[3] = ldq_phys((env->mxccregs[0] & 0xffffffffULL) +
                                        24);
            break;
        case 0x01c00200: /* MXCC stream destination */
            if (size == 8)
                env->mxccregs[1] = val;
            else
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr,
                             size);
            stq_phys((env->mxccregs[1] & 0xffffffffULL) +  0,
                     env->mxccdata[0]);
            stq_phys((env->mxccregs[1] & 0xffffffffULL) +  8,
                     env->mxccdata[1]);
            stq_phys((env->mxccregs[1] & 0xffffffffULL) + 16,
                     env->mxccdata[2]);
            stq_phys((env->mxccregs[1] & 0xffffffffULL) + 24,
                     env->mxccdata[3]);
            break;
        case 0x01c00a00: /* MXCC control register */
            if (size == 8)
                env->mxccregs[3] = val;
            else
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr,
                             size);
            break;
        case 0x01c00a04: /* MXCC control register */
            if (size == 4)
                env->mxccregs[3] = (env->mxccregs[0xa] & 0xffffffff00000000ULL)
                    | val;
            else
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr,
                             size);
            break;
        case 0x01c00e00: /* MXCC error register  */
            // writing a 1 bit clears the error
            if (size == 8)
                env->mxccregs[6] &= ~val;
            else
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr,
                             size);
            break;
        case 0x01c00f00: /* MBus port address register */
            if (size == 8)
                env->mxccregs[7] = val;
            else
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr,
                             size);
            break;
        default:
            DPRINTF_MXCC("%08x: unimplemented address, size: %d\n", addr,
                         size);
            break;
        }
        DPRINTF_MXCC("asi = %d, size = %d, addr = %08x, val = %08x\n", asi,
                     size, addr, val);
#ifdef DEBUG_MXCC
        dump_mxcc(env);
#endif
        break;
    case 3: /* MMU flush */
        {
            int mmulev;

            mmulev = (addr >> 8) & 15;
            DPRINTF_MMU("mmu flush level %d\n", mmulev);
            switch (mmulev) {
            case 0: // flush page
                tlb_flush_page(env, addr & 0xfffff000);
                break;
            case 1: // flush segment (256k)
            case 2: // flush region (16M)
            case 3: // flush context (4G)
            case 4: // flush entire
                tlb_flush(env, 1);
                break;
            default:
                break;
            }
#ifdef DEBUG_MMU
            dump_mmu(env);
#endif
        }
        break;
    case 4: /* write MMU regs */
        {
            int reg = (addr >> 8) & 0x1f;
            uint32_t oldreg;

            oldreg = env->mmuregs[reg];
            switch(reg) {
            case 0: // Control Register
                env->mmuregs[reg] = (env->mmuregs[reg] & 0xff000000) |
                                    (val & 0x00ffffff);
                // Mappings generated during no-fault mode or MMU
                // disabled mode are invalid in normal mode
                if ((oldreg & (MMU_E | MMU_NF | env->def->mmu_bm)) !=
                    (env->mmuregs[reg] & (MMU_E | MMU_NF | env->def->mmu_bm)))
                    tlb_flush(env, 1);
                break;
            case 1: // Context Table Pointer Register
                env->mmuregs[reg] = val & env->def->mmu_ctpr_mask;
                break;
            case 2: // Context Register
                env->mmuregs[reg] = val & env->def->mmu_cxr_mask;
                if (oldreg != env->mmuregs[reg]) {
                    /* we flush when the MMU context changes because
                       QEMU has no MMU context support */
                    tlb_flush(env, 1);
                }
                break;
            case 3: // Synchronous Fault Status Register with Clear
            case 4: // Synchronous Fault Address Register
                break;
            case 0x10: // TLB Replacement Control Register
                env->mmuregs[reg] = val & env->def->mmu_trcr_mask;
                break;
            case 0x13: // Synchronous Fault Status Register with Read and Clear
                env->mmuregs[3] = val & env->def->mmu_sfsr_mask;
                break;
            case 0x14: // Synchronous Fault Address Register
                env->mmuregs[4] = val;
                break;
            default:
                env->mmuregs[reg] = val;
                break;
            }
            if (oldreg != env->mmuregs[reg]) {
                DPRINTF_MMU("mmu change reg[%d]: 0x%08x -> 0x%08x\n",
                            reg, oldreg, env->mmuregs[reg]);
            }
#ifdef DEBUG_MMU
            dump_mmu(env);
#endif
        }
        break;
    case 5: // Turbosparc ITLB Diagnostic
    case 6: // Turbosparc DTLB Diagnostic
    case 7: // Turbosparc IOTLB Diagnostic
        break;
    case 0xa: /* User data access */
        switch(size) {
        case 1:
            stb_user(addr, val);
            break;
        case 2:
            stw_user(addr, val);
            break;
        default:
        case 4:
            stl_user(addr, val);
            break;
        case 8:
            stq_user(addr, val);
            break;
        }
        break;
    case 0xb: /* Supervisor data access */
        switch(size) {
        case 1:
            stb_kernel(addr, val);
            break;
        case 2:
            stw_kernel(addr, val);
            break;
        default:
        case 4:
            stl_kernel(addr, val);
            break;
        case 8:
            stq_kernel(addr, val);
            break;
        }
        break;
    case 0xc: /* I-cache tag */
    case 0xd: /* I-cache data */
    case 0xe: /* D-cache tag */
    case 0xf: /* D-cache data */
    case 0x10: /* I/D-cache flush page */
    case 0x11: /* I/D-cache flush segment */
    case 0x12: /* I/D-cache flush region */
    case 0x13: /* I/D-cache flush context */
    case 0x14: /* I/D-cache flush user */
        break;
    case 0x17: /* Block copy, sta access */
        {
            // val = src
            // addr = dst
            // copy 32 bytes
            unsigned int i;
            uint32_t src = val & ~3, dst = addr & ~3, temp;

            for (i = 0; i < 32; i += 4, src += 4, dst += 4) {
                temp = ldl_kernel(src);
                stl_kernel(dst, temp);
            }
        }
        break;
    case 0x1f: /* Block fill, stda access */
        {
            // addr = dst
            // fill 32 bytes with val
            unsigned int i;
            uint32_t dst = addr & 7;

            for (i = 0; i < 32; i += 8, dst += 8)
                stq_kernel(dst, val);
        }
        break;
    case 0x20: /* MMU passthrough */
        {
            switch(size) {
            case 1:
                stb_phys(addr, val);
                break;
            case 2:
                stw_phys(addr, val);
                break;
            case 4:
            default:
                stl_phys(addr, val);
                break;
            case 8:
                stq_phys(addr, val);
                break;
            }
        }
        break;
    case 0x21 ... 0x2f: /* MMU passthrough, 0x100000000 to 0xfffffffff */
        {
            switch(size) {
            case 1:
                stb_phys((target_phys_addr_t)addr
                         | ((target_phys_addr_t)(asi & 0xf) << 32), val);
                break;
            case 2:
                stw_phys((target_phys_addr_t)addr
                         | ((target_phys_addr_t)(asi & 0xf) << 32), val);
                break;
            case 4:
            default:
                stl_phys((target_phys_addr_t)addr
                         | ((target_phys_addr_t)(asi & 0xf) << 32), val);
                break;
            case 8:
                stq_phys((target_phys_addr_t)addr
                         | ((target_phys_addr_t)(asi & 0xf) << 32), val);
                break;
            }
        }
        break;
    case 0x30: // store buffer tags or Turbosparc secondary cache diagnostic
    case 0x31: // store buffer data, Ross RT620 I-cache flush or
               // Turbosparc snoop RAM
    case 0x32: // store buffer control or Turbosparc page table
               // descriptor diagnostic
    case 0x36: /* I-cache flash clear */
    case 0x37: /* D-cache flash clear */
    case 0x38: /* breakpoint diagnostics */
    case 0x4c: /* breakpoint action */
        break;
    case 8: /* User code access, XXX */
    case 9: /* Supervisor code access, XXX */
    default:
        do_unassigned_access(addr, 1, 0, asi);
        break;
    }
#ifdef DEBUG_ASI
    dump_asi("write", addr, asi, size, val);
#endif
}

#endif /* CONFIG_USER_ONLY */
#else /* TARGET_SPARC64 */

#ifdef CONFIG_USER_ONLY
uint64_t helper_ld_asi(target_ulong addr, int asi, int size, int sign)
{
    uint64_t ret = 0;
#if defined(DEBUG_ASI)
    target_ulong last_addr = addr;
#endif

    if (asi < 0x80)
        raise_exception(TT_PRIV_ACT);

    helper_check_align(addr, size - 1);
    address_mask(env, &addr);

    switch (asi) {
    case 0x82: // Primary no-fault
    case 0x8a: // Primary no-fault LE
        if (page_check_range(addr, size, PAGE_READ) == -1) {
#ifdef DEBUG_ASI
            dump_asi("read ", last_addr, asi, size, ret);
#endif
            return 0;
        }
        // Fall through
    case 0x80: // Primary
    case 0x88: // Primary LE
        {
            switch(size) {
            case 1:
                ret = ldub_raw(addr);
                break;
            case 2:
                ret = lduw_raw(addr);
                break;
            case 4:
                ret = ldl_raw(addr);
                break;
            default:
            case 8:
                ret = ldq_raw(addr);
                break;
            }
        }
        break;
    case 0x83: // Secondary no-fault
    case 0x8b: // Secondary no-fault LE
        if (page_check_range(addr, size, PAGE_READ) == -1) {
#ifdef DEBUG_ASI
            dump_asi("read ", last_addr, asi, size, ret);
#endif
            return 0;
        }
        // Fall through
    case 0x81: // Secondary
    case 0x89: // Secondary LE
        // XXX
        break;
    default:
        break;
    }

    /* Convert from little endian */
    switch (asi) {
    case 0x88: // Primary LE
    case 0x89: // Secondary LE
    case 0x8a: // Primary no-fault LE
    case 0x8b: // Secondary no-fault LE
        switch(size) {
        case 2:
            ret = bswap16(ret);
            break;
        case 4:
            ret = bswap32(ret);
            break;
        case 8:
            ret = bswap64(ret);
            break;
        default:
            break;
        }
    default:
        break;
    }

    /* Convert to signed number */
    if (sign) {
        switch(size) {
        case 1:
            ret = (int8_t) ret;
            break;
        case 2:
            ret = (int16_t) ret;
            break;
        case 4:
            ret = (int32_t) ret;
            break;
        default:
            break;
        }
    }
#ifdef DEBUG_ASI
    dump_asi("read ", last_addr, asi, size, ret);
#endif
    return ret;
}

void helper_st_asi(target_ulong addr, target_ulong val, int asi, int size)
{
#ifdef DEBUG_ASI
    dump_asi("write", addr, asi, size, val);
#endif
    if (asi < 0x80)
        raise_exception(TT_PRIV_ACT);

    helper_check_align(addr, size - 1);
    address_mask(env, &addr);

    /* Convert to little endian */
    switch (asi) {
    case 0x88: // Primary LE
    case 0x89: // Secondary LE
        switch(size) {
        case 2:
            addr = bswap16(addr);
            break;
        case 4:
            addr = bswap32(addr);
            break;
        case 8:
            addr = bswap64(addr);
            break;
        default:
            break;
        }
    default:
        break;
    }

    switch(asi) {
    case 0x80: // Primary
    case 0x88: // Primary LE
        {
            switch(size) {
            case 1:
                stb_raw(addr, val);
                break;
            case 2:
                stw_raw(addr, val);
                break;
            case 4:
                stl_raw(addr, val);
                break;
            case 8:
            default:
                stq_raw(addr, val);
                break;
            }
        }
        break;
    case 0x81: // Secondary
    case 0x89: // Secondary LE
        // XXX
        return;

    case 0x82: // Primary no-fault, RO
    case 0x83: // Secondary no-fault, RO
    case 0x8a: // Primary no-fault LE, RO
    case 0x8b: // Secondary no-fault LE, RO
    default:
        do_unassigned_access(addr, 1, 0, 1);
        return;
    }
}

#else /* CONFIG_USER_ONLY */

uint64_t helper_ld_asi(target_ulong addr, int asi, int size, int sign)
{
    uint64_t ret = 0;
#if defined(DEBUG_ASI)
    target_ulong last_addr = addr;
#endif

    if ((asi < 0x80 && (env->pstate & PS_PRIV) == 0)
        || ((env->def->features & CPU_FEATURE_HYPV)
            && asi >= 0x30 && asi < 0x80
            && !(env->hpstate & HS_PRIV)))
        raise_exception(TT_PRIV_ACT);

    helper_check_align(addr, size - 1);
    switch (asi) {
    case 0x82: // Primary no-fault
    case 0x8a: // Primary no-fault LE
        if (cpu_get_phys_page_debug(env, addr) == -1ULL) {
#ifdef DEBUG_ASI
            dump_asi("read ", last_addr, asi, size, ret);
#endif
            return 0;
        }
        // Fall through
    case 0x10: // As if user primary
    case 0x18: // As if user primary LE
    case 0x80: // Primary
    case 0x88: // Primary LE
        if ((asi & 0x80) && (env->pstate & PS_PRIV)) {
            if ((env->def->features & CPU_FEATURE_HYPV)
                && env->hpstate & HS_PRIV) {
                switch(size) {
                case 1:
                    ret = ldub_hypv(addr);
                    break;
                case 2:
                    ret = lduw_hypv(addr);
                    break;
                case 4:
                    ret = ldl_hypv(addr);
                    break;
                default:
                case 8:
                    ret = ldq_hypv(addr);
                    break;
                }
            } else {
                switch(size) {
                case 1:
                    ret = ldub_kernel(addr);
                    break;
                case 2:
                    ret = lduw_kernel(addr);
                    break;
                case 4:
                    ret = ldl_kernel(addr);
                    break;
                default:
                case 8:
                    ret = ldq_kernel(addr);
                    break;
                }
            }
        } else {
            switch(size) {
            case 1:
                ret = ldub_user(addr);
                break;
            case 2:
                ret = lduw_user(addr);
                break;
            case 4:
                ret = ldl_user(addr);
                break;
            default:
            case 8:
                ret = ldq_user(addr);
                break;
            }
        }
        break;
    case 0x14: // Bypass
    case 0x15: // Bypass, non-cacheable
    case 0x1c: // Bypass LE
    case 0x1d: // Bypass, non-cacheable LE
        {
            switch(size) {
            case 1:
                ret = ldub_phys(addr);
                break;
            case 2:
                ret = lduw_phys(addr);
                break;
            case 4:
                ret = ldl_phys(addr);
                break;
            default:
            case 8:
                ret = ldq_phys(addr);
                break;
            }
            break;
        }
    case 0x24: // Nucleus quad LDD 128 bit atomic
    case 0x2c: // Nucleus quad LDD 128 bit atomic LE
        //  Only ldda allowed
        raise_exception(TT_ILL_INSN);
        return 0;
    case 0x83: // Secondary no-fault
    case 0x8b: // Secondary no-fault LE
        if (cpu_get_phys_page_debug(env, addr) == -1ULL) {
#ifdef DEBUG_ASI
            dump_asi("read ", last_addr, asi, size, ret);
#endif
            return 0;
        }
        // Fall through
    case 0x04: // Nucleus
    case 0x0c: // Nucleus Little Endian (LE)
    case 0x11: // As if user secondary
    case 0x19: // As if user secondary LE
    case 0x4a: // UPA config
    case 0x81: // Secondary
    case 0x89: // Secondary LE
        // XXX
        break;
    case 0x45: // LSU
        ret = env->lsu;
        break;
    case 0x50: // I-MMU regs
        {
            int reg = (addr >> 3) & 0xf;

            ret = env->immuregs[reg];
            break;
        }
    case 0x51: // I-MMU 8k TSB pointer
    case 0x52: // I-MMU 64k TSB pointer
        // XXX
        break;
    case 0x55: // I-MMU data access
        {
            int reg = (addr >> 3) & 0x3f;

            ret = env->itlb_tte[reg];
            break;
        }
    case 0x56: // I-MMU tag read
        {
            int reg = (addr >> 3) & 0x3f;

            ret = env->itlb_tag[reg];
            break;
        }
    case 0x58: // D-MMU regs
        {
            int reg = (addr >> 3) & 0xf;

            ret = env->dmmuregs[reg];
            break;
        }
    case 0x5d: // D-MMU data access
        {
            int reg = (addr >> 3) & 0x3f;

            ret = env->dtlb_tte[reg];
            break;
        }
    case 0x5e: // D-MMU tag read
        {
            int reg = (addr >> 3) & 0x3f;

            ret = env->dtlb_tag[reg];
            break;
        }
    case 0x46: // D-cache data
    case 0x47: // D-cache tag access
    case 0x4b: // E-cache error enable
    case 0x4c: // E-cache asynchronous fault status
    case 0x4d: // E-cache asynchronous fault address
    case 0x4e: // E-cache tag data
    case 0x66: // I-cache instruction access
    case 0x67: // I-cache tag access
    case 0x6e: // I-cache predecode
    case 0x6f: // I-cache LRU etc.
    case 0x76: // E-cache tag
    case 0x7e: // E-cache tag
        break;
    case 0x59: // D-MMU 8k TSB pointer
    case 0x5a: // D-MMU 64k TSB pointer
    case 0x5b: // D-MMU data pointer
    case 0x48: // Interrupt dispatch, RO
    case 0x49: // Interrupt data receive
    case 0x7f: // Incoming interrupt vector, RO
        // XXX
        break;
    case 0x54: // I-MMU data in, WO
    case 0x57: // I-MMU demap, WO
    case 0x5c: // D-MMU data in, WO
    case 0x5f: // D-MMU demap, WO
    case 0x77: // Interrupt vector, WO
    default:
        do_unassigned_access(addr, 0, 0, 1);
        ret = 0;
        break;
    }

    /* Convert from little endian */
    switch (asi) {
    case 0x0c: // Nucleus Little Endian (LE)
    case 0x18: // As if user primary LE
    case 0x19: // As if user secondary LE
    case 0x1c: // Bypass LE
    case 0x1d: // Bypass, non-cacheable LE
    case 0x88: // Primary LE
    case 0x89: // Secondary LE
    case 0x8a: // Primary no-fault LE
    case 0x8b: // Secondary no-fault LE
        switch(size) {
        case 2:
            ret = bswap16(ret);
            break;
        case 4:
            ret = bswap32(ret);
            break;
        case 8:
            ret = bswap64(ret);
            break;
        default:
            break;
        }
    default:
        break;
    }

    /* Convert to signed number */
    if (sign) {
        switch(size) {
        case 1:
            ret = (int8_t) ret;
            break;
        case 2:
            ret = (int16_t) ret;
            break;
        case 4:
            ret = (int32_t) ret;
            break;
        default:
            break;
        }
    }
#ifdef DEBUG_ASI
    dump_asi("read ", last_addr, asi, size, ret);
#endif
    return ret;
}

void helper_st_asi(target_ulong addr, target_ulong val, int asi, int size)
{
#ifdef DEBUG_ASI
    dump_asi("write", addr, asi, size, val);
#endif
    if ((asi < 0x80 && (env->pstate & PS_PRIV) == 0)
        || ((env->def->features & CPU_FEATURE_HYPV)
            && asi >= 0x30 && asi < 0x80
            && !(env->hpstate & HS_PRIV)))
        raise_exception(TT_PRIV_ACT);

    helper_check_align(addr, size - 1);
    /* Convert to little endian */
    switch (asi) {
    case 0x0c: // Nucleus Little Endian (LE)
    case 0x18: // As if user primary LE
    case 0x19: // As if user secondary LE
    case 0x1c: // Bypass LE
    case 0x1d: // Bypass, non-cacheable LE
    case 0x88: // Primary LE
    case 0x89: // Secondary LE
        switch(size) {
        case 2:
            addr = bswap16(addr);
            break;
        case 4:
            addr = bswap32(addr);
            break;
        case 8:
            addr = bswap64(addr);
            break;
        default:
            break;
        }
    default:
        break;
    }

    switch(asi) {
    case 0x10: // As if user primary
    case 0x18: // As if user primary LE
    case 0x80: // Primary
    case 0x88: // Primary LE
        if ((asi & 0x80) && (env->pstate & PS_PRIV)) {
            if ((env->def->features & CPU_FEATURE_HYPV)
                && env->hpstate & HS_PRIV) {
                switch(size) {
                case 1:
                    stb_hypv(addr, val);
                    break;
                case 2:
                    stw_hypv(addr, val);
                    break;
                case 4:
                    stl_hypv(addr, val);
                    break;
                case 8:
                default:
                    stq_hypv(addr, val);
                    break;
                }
            } else {
                switch(size) {
                case 1:
                    stb_kernel(addr, val);
                    break;
                case 2:
                    stw_kernel(addr, val);
                    break;
                case 4:
                    stl_kernel(addr, val);
                    break;
                case 8:
                default:
                    stq_kernel(addr, val);
                    break;
                }
            }
        } else {
            switch(size) {
            case 1:
                stb_user(addr, val);
                break;
            case 2:
                stw_user(addr, val);
                break;
            case 4:
                stl_user(addr, val);
                break;
            case 8:
            default:
                stq_user(addr, val);
                break;
            }
        }
        break;
    case 0x14: // Bypass
    case 0x15: // Bypass, non-cacheable
    case 0x1c: // Bypass LE
    case 0x1d: // Bypass, non-cacheable LE
        {
            switch(size) {
            case 1:
                stb_phys(addr, val);
                break;
            case 2:
                stw_phys(addr, val);
                break;
            case 4:
                stl_phys(addr, val);
                break;
            case 8:
            default:
                stq_phys(addr, val);
                break;
            }
        }
        return;
    case 0x24: // Nucleus quad LDD 128 bit atomic
    case 0x2c: // Nucleus quad LDD 128 bit atomic LE
        //  Only ldda allowed
        raise_exception(TT_ILL_INSN);
        return;
    case 0x04: // Nucleus
    case 0x0c: // Nucleus Little Endian (LE)
    case 0x11: // As if user secondary
    case 0x19: // As if user secondary LE
    case 0x4a: // UPA config
    case 0x81: // Secondary
    case 0x89: // Secondary LE
        // XXX
        return;
    case 0x45: // LSU
        {
            uint64_t oldreg;

            oldreg = env->lsu;
            env->lsu = val & (DMMU_E | IMMU_E);
            // Mappings generated during D/I MMU disabled mode are
            // invalid in normal mode
            if (oldreg != env->lsu) {
                DPRINTF_MMU("LSU change: 0x%" PRIx64 " -> 0x%" PRIx64 "\n",
                            oldreg, env->lsu);
#ifdef DEBUG_MMU
                dump_mmu(env);
#endif
                tlb_flush(env, 1);
            }
            return;
        }
    case 0x50: // I-MMU regs
        {
            int reg = (addr >> 3) & 0xf;
            uint64_t oldreg;

            oldreg = env->immuregs[reg];
            switch(reg) {
            case 0: // RO
            case 4:
                return;
            case 1: // Not in I-MMU
            case 2:
            case 7:
            case 8:
                return;
            case 3: // SFSR
                if ((val & 1) == 0)
                    val = 0; // Clear SFSR
                break;
            case 5: // TSB access
            case 6: // Tag access
            default:
                break;
            }
            env->immuregs[reg] = val;
            if (oldreg != env->immuregs[reg]) {
                DPRINTF_MMU("mmu change reg[%d]: 0x%08" PRIx64 " -> 0x%08"
                            PRIx64 "\n", reg, oldreg, env->immuregs[reg]);
            }
#ifdef DEBUG_MMU
            dump_mmu(env);
#endif
            return;
        }
    case 0x54: // I-MMU data in
        {
            unsigned int i;

            // Try finding an invalid entry
            for (i = 0; i < 64; i++) {
                if ((env->itlb_tte[i] & 0x8000000000000000ULL) == 0) {
                    env->itlb_tag[i] = env->immuregs[6];
                    env->itlb_tte[i] = val;
                    return;
                }
            }
            // Try finding an unlocked entry
            for (i = 0; i < 64; i++) {
                if ((env->itlb_tte[i] & 0x40) == 0) {
                    env->itlb_tag[i] = env->immuregs[6];
                    env->itlb_tte[i] = val;
                    return;
                }
            }
            // error state?
            return;
        }
    case 0x55: // I-MMU data access
        {
            unsigned int i = (addr >> 3) & 0x3f;

            env->itlb_tag[i] = env->immuregs[6];
            env->itlb_tte[i] = val;
            return;
        }
    case 0x57: // I-MMU demap
        // XXX
        return;
    case 0x58: // D-MMU regs
        {
            int reg = (addr >> 3) & 0xf;
            uint64_t oldreg;

            oldreg = env->dmmuregs[reg];
            switch(reg) {
            case 0: // RO
            case 4:
                return;
            case 3: // SFSR
                if ((val & 1) == 0) {
                    val = 0; // Clear SFSR, Fault address
                    env->dmmuregs[4] = 0;
                }
                env->dmmuregs[reg] = val;
                break;
            case 1: // Primary context
            case 2: // Secondary context
            case 5: // TSB access
            case 6: // Tag access
            case 7: // Virtual Watchpoint
            case 8: // Physical Watchpoint
            default:
                break;
            }
            env->dmmuregs[reg] = val;
            if (oldreg != env->dmmuregs[reg]) {
                DPRINTF_MMU("mmu change reg[%d]: 0x%08" PRIx64 " -> 0x%08"
                            PRIx64 "\n", reg, oldreg, env->dmmuregs[reg]);
            }
#ifdef DEBUG_MMU
            dump_mmu(env);
#endif
            return;
        }
    case 0x5c: // D-MMU data in
        {
            unsigned int i;

            // Try finding an invalid entry
            for (i = 0; i < 64; i++) {
                if ((env->dtlb_tte[i] & 0x8000000000000000ULL) == 0) {
                    env->dtlb_tag[i] = env->dmmuregs[6];
                    env->dtlb_tte[i] = val;
                    return;
                }
            }
            // Try finding an unlocked entry
            for (i = 0; i < 64; i++) {
                if ((env->dtlb_tte[i] & 0x40) == 0) {
                    env->dtlb_tag[i] = env->dmmuregs[6];
                    env->dtlb_tte[i] = val;
                    return;
                }
            }
            // error state?
            return;
        }
    case 0x5d: // D-MMU data access
        {
            unsigned int i = (addr >> 3) & 0x3f;

            env->dtlb_tag[i] = env->dmmuregs[6];
            env->dtlb_tte[i] = val;
            return;
        }
    case 0x5f: // D-MMU demap
    case 0x49: // Interrupt data receive
        // XXX
        return;
    case 0x46: // D-cache data
    case 0x47: // D-cache tag access
    case 0x4b: // E-cache error enable
    case 0x4c: // E-cache asynchronous fault status
    case 0x4d: // E-cache asynchronous fault address
    case 0x4e: // E-cache tag data
    case 0x66: // I-cache instruction access
    case 0x67: // I-cache tag access
    case 0x6e: // I-cache predecode
    case 0x6f: // I-cache LRU etc.
    case 0x76: // E-cache tag
    case 0x7e: // E-cache tag
        return;
    case 0x51: // I-MMU 8k TSB pointer, RO
    case 0x52: // I-MMU 64k TSB pointer, RO
    case 0x56: // I-MMU tag read, RO
    case 0x59: // D-MMU 8k TSB pointer, RO
    case 0x5a: // D-MMU 64k TSB pointer, RO
    case 0x5b: // D-MMU data pointer, RO
    case 0x5e: // D-MMU tag read, RO
    case 0x48: // Interrupt dispatch, RO
    case 0x7f: // Incoming interrupt vector, RO
    case 0x82: // Primary no-fault, RO
    case 0x83: // Secondary no-fault, RO
    case 0x8a: // Primary no-fault LE, RO
    case 0x8b: // Secondary no-fault LE, RO
    default:
        do_unassigned_access(addr, 1, 0, 1);
        return;
    }
}
#endif /* CONFIG_USER_ONLY */

void helper_ldda_asi(target_ulong addr, int asi, int rd)
{
    if ((asi < 0x80 && (env->pstate & PS_PRIV) == 0)
        || ((env->def->features & CPU_FEATURE_HYPV)
            && asi >= 0x30 && asi < 0x80
            && !(env->hpstate & HS_PRIV)))
        raise_exception(TT_PRIV_ACT);

    switch (asi) {
    case 0x24: // Nucleus quad LDD 128 bit atomic
    case 0x2c: // Nucleus quad LDD 128 bit atomic LE
        helper_check_align(addr, 0xf);
        if (rd == 0) {
            env->gregs[1] = ldq_kernel(addr + 8);
            if (asi == 0x2c)
                bswap64s(&env->gregs[1]);
        } else if (rd < 8) {
            env->gregs[rd] = ldq_kernel(addr);
            env->gregs[rd + 1] = ldq_kernel(addr + 8);
            if (asi == 0x2c) {
                bswap64s(&env->gregs[rd]);
                bswap64s(&env->gregs[rd + 1]);
            }
        } else {
            env->regwptr[rd] = ldq_kernel(addr);
            env->regwptr[rd + 1] = ldq_kernel(addr + 8);
            if (asi == 0x2c) {
                bswap64s(&env->regwptr[rd]);
                bswap64s(&env->regwptr[rd + 1]);
            }
        }
        break;
    default:
        helper_check_align(addr, 0x3);
        if (rd == 0)
            env->gregs[1] = helper_ld_asi(addr + 4, asi, 4, 0);
        else if (rd < 8) {
            env->gregs[rd] = helper_ld_asi(addr, asi, 4, 0);
            env->gregs[rd + 1] = helper_ld_asi(addr + 4, asi, 4, 0);
        } else {
            env->regwptr[rd] = helper_ld_asi(addr, asi, 4, 0);
            env->regwptr[rd + 1] = helper_ld_asi(addr + 4, asi, 4, 0);
        }
        break;
    }
}

void helper_ldf_asi(target_ulong addr, int asi, int size, int rd)
{
    unsigned int i;
    target_ulong val;

    helper_check_align(addr, 3);
    switch (asi) {
    case 0xf0: // Block load primary
    case 0xf1: // Block load secondary
    case 0xf8: // Block load primary LE
    case 0xf9: // Block load secondary LE
        if (rd & 7) {
            raise_exception(TT_ILL_INSN);
            return;
        }
        helper_check_align(addr, 0x3f);
        for (i = 0; i < 16; i++) {
            *(uint32_t *)&env->fpr[rd++] = helper_ld_asi(addr, asi & 0x8f, 4,
                                                         0);
            addr += 4;
        }

        return;
    default:
        break;
    }

    val = helper_ld_asi(addr, asi, size, 0);
    switch(size) {
    default:
    case 4:
        *((uint32_t *)&env->fpr[rd]) = val;
        break;
    case 8:
        *((int64_t *)&DT0) = val;
        break;
    case 16:
        // XXX
        break;
    }
}

void helper_stf_asi(target_ulong addr, int asi, int size, int rd)
{
    unsigned int i;
    target_ulong val = 0;

    helper_check_align(addr, 3);
    switch (asi) {
    case 0xf0: // Block store primary
    case 0xf1: // Block store secondary
    case 0xf8: // Block store primary LE
    case 0xf9: // Block store secondary LE
        if (rd & 7) {
            raise_exception(TT_ILL_INSN);
            return;
        }
        helper_check_align(addr, 0x3f);
        for (i = 0; i < 16; i++) {
            val = *(uint32_t *)&env->fpr[rd++];
            helper_st_asi(addr, val, asi & 0x8f, 4);
            addr += 4;
        }

        return;
    default:
        break;
    }

    switch(size) {
    default:
    case 4:
        val = *((uint32_t *)&env->fpr[rd]);
        break;
    case 8:
        val = *((int64_t *)&DT0);
        break;
    case 16:
        // XXX
        break;
    }
    helper_st_asi(addr, val, asi, size);
}

target_ulong helper_cas_asi(target_ulong addr, target_ulong val1,
                            target_ulong val2, uint32_t asi)
{
    target_ulong ret;

    val1 &= 0xffffffffUL;
    ret = helper_ld_asi(addr, asi, 4, 0);
    ret &= 0xffffffffUL;
    if (val1 == ret)
        helper_st_asi(addr, val2 & 0xffffffffUL, asi, 4);
    return ret;
}

target_ulong helper_casx_asi(target_ulong addr, target_ulong val1,
                             target_ulong val2, uint32_t asi)
{
    target_ulong ret;

    ret = helper_ld_asi(addr, asi, 8, 0);
    if (val1 == ret)
        helper_st_asi(addr, val2, asi, 8);
    return ret;
}
#endif /* TARGET_SPARC64 */

#ifndef TARGET_SPARC64
void helper_rett(void)
{
    unsigned int cwp;

    if (env->psret == 1)
        raise_exception(TT_ILL_INSN);

    env->psret = 1;
    cwp = cpu_cwp_inc(env, env->cwp + 1) ;
    if (env->wim & (1 << cwp)) {
        raise_exception(TT_WIN_UNF);
    }
    set_cwp(cwp);
    env->psrs = env->psrps;
}
#endif

target_ulong helper_udiv(target_ulong a, target_ulong b)
{
    uint64_t x0;
    uint32_t x1;

    x0 = (a & 0xffffffff) | ((int64_t) (env->y) << 32);
    x1 = b;

    if (x1 == 0) {
        raise_exception(TT_DIV_ZERO);
    }

    x0 = x0 / x1;
    if (x0 > 0xffffffff) {
        env->cc_src2 = 1;
        return 0xffffffff;
    } else {
        env->cc_src2 = 0;
        return x0;
    }
}

target_ulong helper_sdiv(target_ulong a, target_ulong b)
{
    int64_t x0;
    int32_t x1;

    x0 = (a & 0xffffffff) | ((int64_t) (env->y) << 32);
    x1 = b;

    if (x1 == 0) {
        raise_exception(TT_DIV_ZERO);
    }

    x0 = x0 / x1;
    if ((int32_t) x0 != x0) {
        env->cc_src2 = 1;
        return x0 < 0? 0x80000000: 0x7fffffff;
    } else {
        env->cc_src2 = 0;
        return x0;
    }
}

uint64_t helper_pack64(target_ulong high, target_ulong low)
{
    return ((uint64_t)high << 32) | (uint64_t)(low & 0xffffffff);
}

void helper_stdf(target_ulong addr, int mem_idx)
{
    helper_check_align(addr, 7);
#if !defined(CONFIG_USER_ONLY)
    switch (mem_idx) {
    case 0:
        stfq_user(addr, DT0);
        break;
    case 1:
        stfq_kernel(addr, DT0);
        break;
#ifdef TARGET_SPARC64
    case 2:
        stfq_hypv(addr, DT0);
        break;
#endif
    default:
        break;
    }
#else
    address_mask(env, &addr);
    stfq_raw(addr, DT0);
#endif
}

void helper_lddf(target_ulong addr, int mem_idx)
{
    helper_check_align(addr, 7);
#if !defined(CONFIG_USER_ONLY)
    switch (mem_idx) {
    case 0:
        DT0 = ldfq_user(addr);
        break;
    case 1:
        DT0 = ldfq_kernel(addr);
        break;
#ifdef TARGET_SPARC64
    case 2:
        DT0 = ldfq_hypv(addr);
        break;
#endif
    default:
        break;
    }
#else
    address_mask(env, &addr);
    DT0 = ldfq_raw(addr);
#endif
}

void helper_ldqf(target_ulong addr, int mem_idx)
{
    // XXX add 128 bit load
    CPU_QuadU u;

    helper_check_align(addr, 7);
#if !defined(CONFIG_USER_ONLY)
    switch (mem_idx) {
    case 0:
        u.ll.upper = ldq_user(addr);
        u.ll.lower = ldq_user(addr + 8);
        QT0 = u.q;
        break;
    case 1:
        u.ll.upper = ldq_kernel(addr);
        u.ll.lower = ldq_kernel(addr + 8);
        QT0 = u.q;
        break;
#ifdef TARGET_SPARC64
    case 2:
        u.ll.upper = ldq_hypv(addr);
        u.ll.lower = ldq_hypv(addr + 8);
        QT0 = u.q;
        break;
#endif
    default:
        break;
    }
#else
    address_mask(env, &addr);
    u.ll.upper = ldq_raw(addr);
    u.ll.lower = ldq_raw((addr + 8) & 0xffffffffULL);
    QT0 = u.q;
#endif
}

void helper_stqf(target_ulong addr, int mem_idx)
{
    // XXX add 128 bit store
    CPU_QuadU u;

    helper_check_align(addr, 7);
#if !defined(CONFIG_USER_ONLY)
    switch (mem_idx) {
    case 0:
        u.q = QT0;
        stq_user(addr, u.ll.upper);
        stq_user(addr + 8, u.ll.lower);
        break;
    case 1:
        u.q = QT0;
        stq_kernel(addr, u.ll.upper);
        stq_kernel(addr + 8, u.ll.lower);
        break;
#ifdef TARGET_SPARC64
    case 2:
        u.q = QT0;
        stq_hypv(addr, u.ll.upper);
        stq_hypv(addr + 8, u.ll.lower);
        break;
#endif
    default:
        break;
    }
#else
    u.q = QT0;
    address_mask(env, &addr);
    stq_raw(addr, u.ll.upper);
    stq_raw((addr + 8) & 0xffffffffULL, u.ll.lower);
#endif
}

static inline void set_fsr(void)
{
    int rnd_mode;

    switch (env->fsr & FSR_RD_MASK) {
    case FSR_RD_NEAREST:
        rnd_mode = float_round_nearest_even;
        break;
    default:
    case FSR_RD_ZERO:
        rnd_mode = float_round_to_zero;
        break;
    case FSR_RD_POS:
        rnd_mode = float_round_up;
        break;
    case FSR_RD_NEG:
        rnd_mode = float_round_down;
        break;
    }
    set_float_rounding_mode(rnd_mode, &env->fp_status);
}

void helper_ldfsr(uint32_t new_fsr)
{
    env->fsr = (new_fsr & FSR_LDFSR_MASK) | (env->fsr & FSR_LDFSR_OLDMASK);
    set_fsr();
}

#ifdef TARGET_SPARC64
void helper_ldxfsr(uint64_t new_fsr)
{
    env->fsr = (new_fsr & FSR_LDXFSR_MASK) | (env->fsr & FSR_LDXFSR_OLDMASK);
    set_fsr();
}
#endif

void helper_debug(void)
{
    env->exception_index = EXCP_DEBUG;
    cpu_loop_exit();
}

#ifndef TARGET_SPARC64
/* XXX: use another pointer for %iN registers to avoid slow wrapping
   handling ? */
void helper_save(void)
{
    uint32_t cwp;

    cwp = cpu_cwp_dec(env, env->cwp - 1);
    if (env->wim & (1 << cwp)) {
        raise_exception(TT_WIN_OVF);
    }
    set_cwp(cwp);
}

void helper_restore(void)
{
    uint32_t cwp;

    cwp = cpu_cwp_inc(env, env->cwp + 1);
    if (env->wim & (1 << cwp)) {
        raise_exception(TT_WIN_UNF);
    }
    set_cwp(cwp);
}

void helper_wrpsr(target_ulong new_psr)
{
    if ((new_psr & PSR_CWP) >= env->nwindows)
        raise_exception(TT_ILL_INSN);
    else
        PUT_PSR(env, new_psr);
}

target_ulong helper_rdpsr(void)
{
    return GET_PSR(env);
}

#else
/* XXX: use another pointer for %iN registers to avoid slow wrapping
   handling ? */
void helper_save(void)
{
    uint32_t cwp;

    cwp = cpu_cwp_dec(env, env->cwp - 1);
    if (env->cansave == 0) {
        raise_exception(TT_SPILL | (env->otherwin != 0 ?
                                    (TT_WOTHER | ((env->wstate & 0x38) >> 1)):
                                    ((env->wstate & 0x7) << 2)));
    } else {
        if (env->cleanwin - env->canrestore == 0) {
            // XXX Clean windows without trap
            raise_exception(TT_CLRWIN);
        } else {
            env->cansave--;
            env->canrestore++;
            set_cwp(cwp);
        }
    }
}

void helper_restore(void)
{
    uint32_t cwp;

    cwp = cpu_cwp_inc(env, env->cwp + 1);
    if (env->canrestore == 0) {
        raise_exception(TT_FILL | (env->otherwin != 0 ?
                                   (TT_WOTHER | ((env->wstate & 0x38) >> 1)):
                                   ((env->wstate & 0x7) << 2)));
    } else {
        env->cansave++;
        env->canrestore--;
        set_cwp(cwp);
    }
}

void helper_flushw(void)
{
    if (env->cansave != env->nwindows - 2) {
        raise_exception(TT_SPILL | (env->otherwin != 0 ?
                                    (TT_WOTHER | ((env->wstate & 0x38) >> 1)):
                                    ((env->wstate & 0x7) << 2)));
    }
}

void helper_saved(void)
{
    env->cansave++;
    if (env->otherwin == 0)
        env->canrestore--;
    else
        env->otherwin--;
}

void helper_restored(void)
{
    env->canrestore++;
    if (env->cleanwin < env->nwindows - 1)
        env->cleanwin++;
    if (env->otherwin == 0)
        env->cansave--;
    else
        env->otherwin--;
}

target_ulong helper_rdccr(void)
{
    return GET_CCR(env);
}

void helper_wrccr(target_ulong new_ccr)
{
    PUT_CCR(env, new_ccr);
}

// CWP handling is reversed in V9, but we still use the V8 register
// order.
target_ulong helper_rdcwp(void)
{
    return GET_CWP64(env);
}

void helper_wrcwp(target_ulong new_cwp)
{
    PUT_CWP64(env, new_cwp);
}

// This function uses non-native bit order
#define GET_FIELD(X, FROM, TO)                                  \
    ((X) >> (63 - (TO)) & ((1ULL << ((TO) - (FROM) + 1)) - 1))

// This function uses the order in the manuals, i.e. bit 0 is 2^0
#define GET_FIELD_SP(X, FROM, TO)               \
    GET_FIELD(X, 63 - (TO), 63 - (FROM))

target_ulong helper_array8(target_ulong pixel_addr, target_ulong cubesize)
{
    return (GET_FIELD_SP(pixel_addr, 60, 63) << (17 + 2 * cubesize)) |
        (GET_FIELD_SP(pixel_addr, 39, 39 + cubesize - 1) << (17 + cubesize)) |
        (GET_FIELD_SP(pixel_addr, 17 + cubesize - 1, 17) << 17) |
        (GET_FIELD_SP(pixel_addr, 56, 59) << 13) |
        (GET_FIELD_SP(pixel_addr, 35, 38) << 9) |
        (GET_FIELD_SP(pixel_addr, 13, 16) << 5) |
        (((pixel_addr >> 55) & 1) << 4) |
        (GET_FIELD_SP(pixel_addr, 33, 34) << 2) |
        GET_FIELD_SP(pixel_addr, 11, 12);
}

target_ulong helper_alignaddr(target_ulong addr, target_ulong offset)
{
    uint64_t tmp;

    tmp = addr + offset;
    env->gsr &= ~7ULL;
    env->gsr |= tmp & 7ULL;
    return tmp & ~7ULL;
}

target_ulong helper_popc(target_ulong val)
{
    return ctpop64(val);
}

static inline uint64_t *get_gregset(uint64_t pstate)
{
    switch (pstate) {
    default:
    case 0:
        return env->bgregs;
    case PS_AG:
        return env->agregs;
    case PS_MG:
        return env->mgregs;
    case PS_IG:
        return env->igregs;
    }
}

static inline void change_pstate(uint64_t new_pstate)
{
    uint64_t pstate_regs, new_pstate_regs;
    uint64_t *src, *dst;

    pstate_regs = env->pstate & 0xc01;
    new_pstate_regs = new_pstate & 0xc01;
    if (new_pstate_regs != pstate_regs) {
        // Switch global register bank
        src = get_gregset(new_pstate_regs);
        dst = get_gregset(pstate_regs);
        memcpy32(dst, env->gregs);
        memcpy32(env->gregs, src);
    }
    env->pstate = new_pstate;
}

void helper_wrpstate(target_ulong new_state)
{
    if (!(env->def->features & CPU_FEATURE_GL))
        change_pstate(new_state & 0xf3f);
}

void helper_done(void)
{
    env->pc = env->tsptr->tpc;
    env->npc = env->tsptr->tnpc + 4;
    PUT_CCR(env, env->tsptr->tstate >> 32);
    env->asi = (env->tsptr->tstate >> 24) & 0xff;
    change_pstate((env->tsptr->tstate >> 8) & 0xf3f);
    PUT_CWP64(env, env->tsptr->tstate & 0xff);
    env->tl--;
    env->tsptr = &env->ts[env->tl & MAXTL_MASK];
}

void helper_retry(void)
{
    env->pc = env->tsptr->tpc;
    env->npc = env->tsptr->tnpc;
    PUT_CCR(env, env->tsptr->tstate >> 32);
    env->asi = (env->tsptr->tstate >> 24) & 0xff;
    change_pstate((env->tsptr->tstate >> 8) & 0xf3f);
    PUT_CWP64(env, env->tsptr->tstate & 0xff);
    env->tl--;
    env->tsptr = &env->ts[env->tl & MAXTL_MASK];
}
#endif

void helper_flush(target_ulong addr)
{
    addr &= ~7;
    tb_invalidate_page_range(addr, addr + 8);
}

#ifdef TARGET_SPARC64
#ifdef DEBUG_PCALL
static const char * const excp_names[0x80] = {
    [TT_TFAULT] = "Instruction Access Fault",
    [TT_TMISS] = "Instruction Access MMU Miss",
    [TT_CODE_ACCESS] = "Instruction Access Error",
    [TT_ILL_INSN] = "Illegal Instruction",
    [TT_PRIV_INSN] = "Privileged Instruction",
    [TT_NFPU_INSN] = "FPU Disabled",
    [TT_FP_EXCP] = "FPU Exception",
    [TT_TOVF] = "Tag Overflow",
    [TT_CLRWIN] = "Clean Windows",
    [TT_DIV_ZERO] = "Division By Zero",
    [TT_DFAULT] = "Data Access Fault",
    [TT_DMISS] = "Data Access MMU Miss",
    [TT_DATA_ACCESS] = "Data Access Error",
    [TT_DPROT] = "Data Protection Error",
    [TT_UNALIGNED] = "Unaligned Memory Access",
    [TT_PRIV_ACT] = "Privileged Action",
    [TT_EXTINT | 0x1] = "External Interrupt 1",
    [TT_EXTINT | 0x2] = "External Interrupt 2",
    [TT_EXTINT | 0x3] = "External Interrupt 3",
    [TT_EXTINT | 0x4] = "External Interrupt 4",
    [TT_EXTINT | 0x5] = "External Interrupt 5",
    [TT_EXTINT | 0x6] = "External Interrupt 6",
    [TT_EXTINT | 0x7] = "External Interrupt 7",
    [TT_EXTINT | 0x8] = "External Interrupt 8",
    [TT_EXTINT | 0x9] = "External Interrupt 9",
    [TT_EXTINT | 0xa] = "External Interrupt 10",
    [TT_EXTINT | 0xb] = "External Interrupt 11",
    [TT_EXTINT | 0xc] = "External Interrupt 12",
    [TT_EXTINT | 0xd] = "External Interrupt 13",
    [TT_EXTINT | 0xe] = "External Interrupt 14",
    [TT_EXTINT | 0xf] = "External Interrupt 15",
};
#endif

void do_interrupt(CPUState *env)
{
    int intno = env->exception_index;

#ifdef DEBUG_PCALL
    if (loglevel & CPU_LOG_INT) {
        static int count;
        const char *name;

        if (intno < 0 || intno >= 0x180)
            name = "Unknown";
        else if (intno >= 0x100)
            name = "Trap Instruction";
        else if (intno >= 0xc0)
            name = "Window Fill";
        else if (intno >= 0x80)
            name = "Window Spill";
        else {
            name = excp_names[intno];
            if (!name)
                name = "Unknown";
        }

        fprintf(logfile, "%6d: %s (v=%04x) pc=%016" PRIx64 " npc=%016" PRIx64
                " SP=%016" PRIx64 "\n",
                count, name, intno,
                env->pc,
                env->npc, env->regwptr[6]);
        cpu_dump_state(env, logfile, fprintf, 0);
#if 0
        {
            int i;
            uint8_t *ptr;

            fprintf(logfile, "       code=");
            ptr = (uint8_t *)env->pc;
            for(i = 0; i < 16; i++) {
                fprintf(logfile, " %02x", ldub(ptr + i));
            }
            fprintf(logfile, "\n");
        }
#endif
        count++;
    }
#endif
#if !defined(CONFIG_USER_ONLY)
    if (env->tl >= env->maxtl) {
        cpu_abort(env, "Trap 0x%04x while trap level (%d) >= MAXTL (%d),"
                  " Error state", env->exception_index, env->tl, env->maxtl);
        return;
    }
#endif
    if (env->tl < env->maxtl - 1) {
        env->tl++;
    } else {
        env->pstate |= PS_RED;
        if (env->tl < env->maxtl)
            env->tl++;
    }
    env->tsptr = &env->ts[env->tl & MAXTL_MASK];
    env->tsptr->tstate = ((uint64_t)GET_CCR(env) << 32) |
        ((env->asi & 0xff) << 24) | ((env->pstate & 0xf3f) << 8) |
        GET_CWP64(env);
    env->tsptr->tpc = env->pc;
    env->tsptr->tnpc = env->npc;
    env->tsptr->tt = intno;
    if (!(env->def->features & CPU_FEATURE_GL)) {
        switch (intno) {
        case TT_IVEC:
            change_pstate(PS_PEF | PS_PRIV | PS_IG);
            break;
        case TT_TFAULT:
        case TT_TMISS:
        case TT_DFAULT:
        case TT_DMISS:
        case TT_DPROT:
            change_pstate(PS_PEF | PS_PRIV | PS_MG);
            break;
        default:
            change_pstate(PS_PEF | PS_PRIV | PS_AG);
            break;
        }
    }
    if (intno == TT_CLRWIN)
        cpu_set_cwp(env, cpu_cwp_dec(env, env->cwp - 1));
    else if ((intno & 0x1c0) == TT_SPILL)
        cpu_set_cwp(env, cpu_cwp_dec(env, env->cwp - env->cansave - 2));
    else if ((intno & 0x1c0) == TT_FILL)
        cpu_set_cwp(env, cpu_cwp_inc(env, env->cwp + 1));
    env->tbr &= ~0x7fffULL;
    env->tbr |= ((env->tl > 1) ? 1 << 14 : 0) | (intno << 5);
    env->pc = env->tbr;
    env->npc = env->pc + 4;
    env->exception_index = 0;
}
#else
#ifdef DEBUG_PCALL
static const char * const excp_names[0x80] = {
    [TT_TFAULT] = "Instruction Access Fault",
    [TT_ILL_INSN] = "Illegal Instruction",
    [TT_PRIV_INSN] = "Privileged Instruction",
    [TT_NFPU_INSN] = "FPU Disabled",
    [TT_WIN_OVF] = "Window Overflow",
    [TT_WIN_UNF] = "Window Underflow",
    [TT_UNALIGNED] = "Unaligned Memory Access",
    [TT_FP_EXCP] = "FPU Exception",
    [TT_DFAULT] = "Data Access Fault",
    [TT_TOVF] = "Tag Overflow",
    [TT_EXTINT | 0x1] = "External Interrupt 1",
    [TT_EXTINT | 0x2] = "External Interrupt 2",
    [TT_EXTINT | 0x3] = "External Interrupt 3",
    [TT_EXTINT | 0x4] = "External Interrupt 4",
    [TT_EXTINT | 0x5] = "External Interrupt 5",
    [TT_EXTINT | 0x6] = "External Interrupt 6",
    [TT_EXTINT | 0x7] = "External Interrupt 7",
    [TT_EXTINT | 0x8] = "External Interrupt 8",
    [TT_EXTINT | 0x9] = "External Interrupt 9",
    [TT_EXTINT | 0xa] = "External Interrupt 10",
    [TT_EXTINT | 0xb] = "External Interrupt 11",
    [TT_EXTINT | 0xc] = "External Interrupt 12",
    [TT_EXTINT | 0xd] = "External Interrupt 13",
    [TT_EXTINT | 0xe] = "External Interrupt 14",
    [TT_EXTINT | 0xf] = "External Interrupt 15",
    [TT_TOVF] = "Tag Overflow",
    [TT_CODE_ACCESS] = "Instruction Access Error",
    [TT_DATA_ACCESS] = "Data Access Error",
    [TT_DIV_ZERO] = "Division By Zero",
    [TT_NCP_INSN] = "Coprocessor Disabled",
};
#endif

void do_interrupt(CPUState *env)
{
    int cwp, intno = env->exception_index;

#ifdef DEBUG_PCALL
    if (loglevel & CPU_LOG_INT) {
        static int count;
        const char *name;

        if (intno < 0 || intno >= 0x100)
            name = "Unknown";
        else if (intno >= 0x80)
            name = "Trap Instruction";
        else {
            name = excp_names[intno];
            if (!name)
                name = "Unknown";
        }

        fprintf(logfile, "%6d: %s (v=%02x) pc=%08x npc=%08x SP=%08x\n",
                count, name, intno,
                env->pc,
                env->npc, env->regwptr[6]);
        cpu_dump_state(env, logfile, fprintf, 0);
#if 0
        {
            int i;
            uint8_t *ptr;

            fprintf(logfile, "       code=");
            ptr = (uint8_t *)env->pc;
            for(i = 0; i < 16; i++) {
                fprintf(logfile, " %02x", ldub(ptr + i));
            }
            fprintf(logfile, "\n");
        }
#endif
        count++;
    }
#endif
#if !defined(CONFIG_USER_ONLY)
    if (env->psret == 0) {
        cpu_abort(env, "Trap 0x%02x while interrupts disabled, Error state",
                  env->exception_index);
        return;
    }
#endif
    env->psret = 0;
    cwp = cpu_cwp_dec(env, env->cwp - 1);
    cpu_set_cwp(env, cwp);
    env->regwptr[9] = env->pc;
    env->regwptr[10] = env->npc;
    env->psrps = env->psrs;
    env->psrs = 1;
    env->tbr = (env->tbr & TBR_BASE_MASK) | (intno << 4);
    env->pc = env->tbr;
    env->npc = env->pc + 4;
    env->exception_index = 0;
}
#endif

#if !defined(CONFIG_USER_ONLY)

static void do_unaligned_access(target_ulong addr, int is_write, int is_user,
                                void *retaddr);

#define MMUSUFFIX _mmu
#define ALIGNED_ONLY

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"

/* XXX: make it generic ? */
static void cpu_restore_state2(void *retaddr)
{
    TranslationBlock *tb;
    unsigned long pc;

    if (retaddr) {
        /* now we have a real cpu fault */
        pc = (unsigned long)retaddr;
        tb = tb_find_pc(pc);
        if (tb) {
            /* the PC is inside the translated code. It means that we have
               a virtual CPU fault */
            cpu_restore_state(tb, env, pc, (void *)(long)env->cond);
        }
    }
}

static void do_unaligned_access(target_ulong addr, int is_write, int is_user,
                                void *retaddr)
{
#ifdef DEBUG_UNALIGNED
    printf("Unaligned access to 0x" TARGET_FMT_lx " from 0x" TARGET_FMT_lx
           "\n", addr, env->pc);
#endif
    cpu_restore_state2(retaddr);
    raise_exception(TT_UNALIGNED);
}

/* try to fill the TLB and return an exception if error. If retaddr is
   NULL, it means that the function was called in C code (i.e. not
   from generated code or from helper.c) */
/* XXX: fix it to restore all registers */
void tlb_fill(target_ulong addr, int is_write, int mmu_idx, void *retaddr)
{
    int ret;
    CPUState *saved_env;

    /* XXX: hack to restore env in all cases, even if not called from
       generated code */
    saved_env = env;
    env = cpu_single_env;

    ret = cpu_sparc_handle_mmu_fault(env, addr, is_write, mmu_idx, 1);
    if (ret) {
        cpu_restore_state2(retaddr);
        cpu_loop_exit();
    }
    env = saved_env;
}

#endif

#ifndef TARGET_SPARC64
void do_unassigned_access(target_phys_addr_t addr, int is_write, int is_exec,
                          int is_asi)
{
    CPUState *saved_env;

    /* XXX: hack to restore env in all cases, even if not called from
       generated code */
    saved_env = env;
    env = cpu_single_env;
#ifdef DEBUG_UNASSIGNED
    if (is_asi)
        printf("Unassigned mem %s access to " TARGET_FMT_plx
               " asi 0x%02x from " TARGET_FMT_lx "\n",
               is_exec ? "exec" : is_write ? "write" : "read", addr, is_asi,
               env->pc);
    else
        printf("Unassigned mem %s access to " TARGET_FMT_plx " from "
               TARGET_FMT_lx "\n",
               is_exec ? "exec" : is_write ? "write" : "read", addr, env->pc);
#endif
    if (env->mmuregs[3]) /* Fault status register */
        env->mmuregs[3] = 1; /* overflow (not read before another fault) */
    if (is_asi)
        env->mmuregs[3] |= 1 << 16;
    if (env->psrs)
        env->mmuregs[3] |= 1 << 5;
    if (is_exec)
        env->mmuregs[3] |= 1 << 6;
    if (is_write)
        env->mmuregs[3] |= 1 << 7;
    env->mmuregs[3] |= (5 << 2) | 2;
    env->mmuregs[4] = addr; /* Fault address register */
    if ((env->mmuregs[0] & MMU_E) && !(env->mmuregs[0] & MMU_NF)) {
        if (is_exec)
            raise_exception(TT_CODE_ACCESS);
        else
            raise_exception(TT_DATA_ACCESS);
    }
    env = saved_env;
}
#else
void do_unassigned_access(target_phys_addr_t addr, int is_write, int is_exec,
                          int is_asi)
{
#ifdef DEBUG_UNASSIGNED
    CPUState *saved_env;

    /* XXX: hack to restore env in all cases, even if not called from
       generated code */
    saved_env = env;
    env = cpu_single_env;
    printf("Unassigned mem access to " TARGET_FMT_plx " from " TARGET_FMT_lx
           "\n", addr, env->pc);
    env = saved_env;
#endif
    if (is_exec)
        raise_exception(TT_CODE_ACCESS);
    else
        raise_exception(TT_DATA_ACCESS);
}
#endif

