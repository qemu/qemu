#ifndef BSWAP_H
#define BSWAP_H

#undef  bswap16
#define bswap16(_x) __builtin_bswap16(_x)
#undef  bswap32
#define bswap32(_x) __builtin_bswap32(_x)
#undef  bswap64
#define bswap64(_x) __builtin_bswap64(_x)

static inline void bswap16s(uint16_t *s)
{
    *s = __builtin_bswap16(*s);
}

static inline void bswap32s(uint32_t *s)
{
    *s = __builtin_bswap32(*s);
}

static inline void bswap64s(uint64_t *s)
{
    *s = __builtin_bswap64(*s);
}

#if HOST_BIG_ENDIAN
#define be_bswap(v, size) (v)
#define le_bswap(v, size) glue(__builtin_bswap, size)(v)
#define be_bswaps(v, size)
#define le_bswaps(p, size) \
            do { *p = glue(__builtin_bswap, size)(*p); } while (0)
#else
#define le_bswap(v, size) (v)
#define be_bswap(v, size) glue(__builtin_bswap, size)(v)
#define le_bswaps(v, size)
#define be_bswaps(p, size) \
            do { *p = glue(__builtin_bswap, size)(*p); } while (0)
#endif

/**
 * Endianness conversion functions between host cpu and specified endianness.
 * (We list the complete set of prototypes produced by the macros below
 * to assist people who search the headers to find their definitions.)
 *
 * uint16_t le16_to_cpu(uint16_t v);
 * uint32_t le32_to_cpu(uint32_t v);
 * uint64_t le64_to_cpu(uint64_t v);
 * uint16_t be16_to_cpu(uint16_t v);
 * uint32_t be32_to_cpu(uint32_t v);
 * uint64_t be64_to_cpu(uint64_t v);
 *
 * Convert the value @v from the specified format to the native
 * endianness of the host CPU by byteswapping if necessary, and
 * return the converted value.
 *
 * uint16_t cpu_to_le16(uint16_t v);
 * uint32_t cpu_to_le32(uint32_t v);
 * uint64_t cpu_to_le64(uint64_t v);
 * uint16_t cpu_to_be16(uint16_t v);
 * uint32_t cpu_to_be32(uint32_t v);
 * uint64_t cpu_to_be64(uint64_t v);
 *
 * Convert the value @v from the native endianness of the host CPU to
 * the specified format by byteswapping if necessary, and return
 * the converted value.
 *
 * void le16_to_cpus(uint16_t *v);
 * void le32_to_cpus(uint32_t *v);
 * void le64_to_cpus(uint64_t *v);
 * void be16_to_cpus(uint16_t *v);
 * void be32_to_cpus(uint32_t *v);
 * void be64_to_cpus(uint64_t *v);
 *
 * Do an in-place conversion of the value pointed to by @v from the
 * specified format to the native endianness of the host CPU.
 *
 * void cpu_to_le16s(uint16_t *v);
 * void cpu_to_le32s(uint32_t *v);
 * void cpu_to_le64s(uint64_t *v);
 * void cpu_to_be16s(uint16_t *v);
 * void cpu_to_be32s(uint32_t *v);
 * void cpu_to_be64s(uint64_t *v);
 *
 * Do an in-place conversion of the value pointed to by @v from the
 * native endianness of the host CPU to the specified format.
 *
 * Both X_to_cpu() and cpu_to_X() perform the same operation; you
 * should use whichever one is better documenting of the function your
 * code is performing.
 *
 * Do not use these functions for conversion of values which are in guest
 * memory, since the data may not be sufficiently aligned for the host CPU's
 * load and store instructions. Instead you should use the ld*_p() and
 * st*_p() functions, which perform loads and stores of data of any
 * required size and endianness and handle possible misalignment.
 */

#define CPU_CONVERT(endian, size, type)\
static inline type endian ## size ## _to_cpu(type v)\
{\
    return glue(endian, _bswap)(v, size);\
}\
\
static inline type cpu_to_ ## endian ## size(type v)\
{\
    return glue(endian, _bswap)(v, size);\
}\
\
static inline void endian ## size ## _to_cpus(type *p)\
{\
    glue(endian, _bswaps)(p, size);\
}\
\
static inline void cpu_to_ ## endian ## size ## s(type *p)\
{\
    glue(endian, _bswaps)(p, size);\
}

CPU_CONVERT(be, 16, uint16_t)
CPU_CONVERT(be, 32, uint32_t)
CPU_CONVERT(be, 64, uint64_t)

CPU_CONVERT(le, 16, uint16_t)
CPU_CONVERT(le, 32, uint32_t)
CPU_CONVERT(le, 64, uint64_t)

/*
 * Same as cpu_to_le{16,32,64}, except that gcc will figure the result is
 * a compile-time constant if you pass in a constant.  So this can be
 * used to initialize static variables.
 */
#if HOST_BIG_ENDIAN
# define const_le64(_x)                          \
    ((((_x) & 0x00000000000000ffU) << 56) |      \
     (((_x) & 0x000000000000ff00U) << 40) |      \
     (((_x) & 0x0000000000ff0000U) << 24) |      \
     (((_x) & 0x00000000ff000000U) <<  8) |      \
     (((_x) & 0x000000ff00000000U) >>  8) |      \
     (((_x) & 0x0000ff0000000000U) >> 24) |      \
     (((_x) & 0x00ff000000000000U) >> 40) |      \
     (((_x) & 0xff00000000000000U) >> 56))
# define const_le32(_x)                          \
    ((((_x) & 0x000000ffU) << 24) |              \
     (((_x) & 0x0000ff00U) <<  8) |              \
     (((_x) & 0x00ff0000U) >>  8) |              \
     (((_x) & 0xff000000U) >> 24))
# define const_le16(_x)                          \
    ((((_x) & 0x00ff) << 8) |                    \
     (((_x) & 0xff00) >> 8))
#else
# define const_le64(_x) (_x)
# define const_le32(_x) (_x)
# define const_le16(_x) (_x)
#endif

/* unaligned/endian-independent pointer access */

/*
 * the generic syntax is:
 *
 * load: ld{type}{sign}{size}_{endian}_p(ptr)
 *
 * store: st{type}{size}_{endian}_p(ptr, val)
 *
 * Note there are small differences with the softmmu access API!
 *
 * type is:
 * (empty): integer access
 *   f    : float access
 *
 * sign is:
 * (empty): for 32 or 64 bit sizes (including floats and doubles)
 *   u    : unsigned
 *   s    : signed
 *
 * size is:
 *   b: 8 bits
 *   w: 16 bits
 *   l: 32 bits
 *   q: 64 bits
 *
 * endian is:
 *   he   : host endian
 *   be   : big endian
 *   le   : little endian
 *   te   : target endian
 * (except for byte accesses, which have no endian infix).
 *
 * The target endian accessors are obviously only available to source
 * files which are built per-target; they are defined in cpu-all.h.
 *
 * In all cases these functions take a host pointer.
 * For accessors that take a guest address rather than a
 * host address, see the cpu_{ld,st}_* accessors defined in
 * cpu_ldst.h.
 *
 * For cases where the size to be used is not fixed at compile time,
 * there are
 *  stn_{endian}_p(ptr, sz, val)
 * which stores @val to @ptr as an @endian-order number @sz bytes in size
 * and
 *  ldn_{endian}_p(ptr, sz)
 * which loads @sz bytes from @ptr as an unsigned @endian-order number
 * and returns it in a uint64_t.
 */

static inline int ldub_p(const void *ptr)
{
    return *(uint8_t *)ptr;
}

static inline int ldsb_p(const void *ptr)
{
    return *(int8_t *)ptr;
}

static inline void stb_p(void *ptr, uint8_t v)
{
    *(uint8_t *)ptr = v;
}

/*
 * Any compiler worth its salt will turn these memcpy into native unaligned
 * operations.  Thus we don't need to play games with packed attributes, or
 * inline byte-by-byte stores.
 * Some compilation environments (eg some fortify-source implementations)
 * may intercept memcpy() in a way that defeats the compiler optimization,
 * though, so we use __builtin_memcpy() to give ourselves the best chance
 * of good performance.
 */

static inline int lduw_he_p(const void *ptr)
{
    uint16_t r;
    __builtin_memcpy(&r, ptr, sizeof(r));
    return r;
}

static inline int ldsw_he_p(const void *ptr)
{
    int16_t r;
    __builtin_memcpy(&r, ptr, sizeof(r));
    return r;
}

static inline void stw_he_p(void *ptr, uint16_t v)
{
    __builtin_memcpy(ptr, &v, sizeof(v));
}

static inline int ldl_he_p(const void *ptr)
{
    int32_t r;
    __builtin_memcpy(&r, ptr, sizeof(r));
    return r;
}

static inline void stl_he_p(void *ptr, uint32_t v)
{
    __builtin_memcpy(ptr, &v, sizeof(v));
}

static inline uint64_t ldq_he_p(const void *ptr)
{
    uint64_t r;
    __builtin_memcpy(&r, ptr, sizeof(r));
    return r;
}

static inline void stq_he_p(void *ptr, uint64_t v)
{
    __builtin_memcpy(ptr, &v, sizeof(v));
}

static inline int lduw_le_p(const void *ptr)
{
    return (uint16_t)le_bswap(lduw_he_p(ptr), 16);
}

static inline int ldsw_le_p(const void *ptr)
{
    return (int16_t)le_bswap(lduw_he_p(ptr), 16);
}

static inline int ldl_le_p(const void *ptr)
{
    return le_bswap(ldl_he_p(ptr), 32);
}

static inline uint64_t ldq_le_p(const void *ptr)
{
    return le_bswap(ldq_he_p(ptr), 64);
}

static inline void stw_le_p(void *ptr, uint16_t v)
{
    stw_he_p(ptr, le_bswap(v, 16));
}

static inline void stl_le_p(void *ptr, uint32_t v)
{
    stl_he_p(ptr, le_bswap(v, 32));
}

static inline void stq_le_p(void *ptr, uint64_t v)
{
    stq_he_p(ptr, le_bswap(v, 64));
}

static inline int lduw_be_p(const void *ptr)
{
    return (uint16_t)be_bswap(lduw_he_p(ptr), 16);
}

static inline int ldsw_be_p(const void *ptr)
{
    return (int16_t)be_bswap(lduw_he_p(ptr), 16);
}

static inline int ldl_be_p(const void *ptr)
{
    return be_bswap(ldl_he_p(ptr), 32);
}

static inline uint64_t ldq_be_p(const void *ptr)
{
    return be_bswap(ldq_he_p(ptr), 64);
}

static inline void stw_be_p(void *ptr, uint16_t v)
{
    stw_he_p(ptr, be_bswap(v, 16));
}

static inline void stl_be_p(void *ptr, uint32_t v)
{
    stl_he_p(ptr, be_bswap(v, 32));
}

static inline void stq_be_p(void *ptr, uint64_t v)
{
    stq_he_p(ptr, be_bswap(v, 64));
}

static inline unsigned long leul_to_cpu(unsigned long v)
{
#if HOST_LONG_BITS == 32
    return le_bswap(v, 32);
#elif HOST_LONG_BITS == 64
    return le_bswap(v, 64);
#else
# error Unknown sizeof long
#endif
}

/* Store v to p as a sz byte value in host order */
#define DO_STN_LDN_P(END) \
    static inline void stn_## END ## _p(void *ptr, int sz, uint64_t v)  \
    {                                                                   \
        switch (sz) {                                                   \
        case 1:                                                         \
            stb_p(ptr, v);                                              \
            break;                                                      \
        case 2:                                                         \
            stw_ ## END ## _p(ptr, v);                                  \
            break;                                                      \
        case 4:                                                         \
            stl_ ## END ## _p(ptr, v);                                  \
            break;                                                      \
        case 8:                                                         \
            stq_ ## END ## _p(ptr, v);                                  \
            break;                                                      \
        default:                                                        \
            g_assert_not_reached();                                     \
        }                                                               \
    }                                                                   \
    static inline uint64_t ldn_## END ## _p(const void *ptr, int sz)    \
    {                                                                   \
        switch (sz) {                                                   \
        case 1:                                                         \
            return ldub_p(ptr);                                         \
        case 2:                                                         \
            return lduw_ ## END ## _p(ptr);                             \
        case 4:                                                         \
            return (uint32_t)ldl_ ## END ## _p(ptr);                    \
        case 8:                                                         \
            return ldq_ ## END ## _p(ptr);                              \
        default:                                                        \
            g_assert_not_reached();                                     \
        }                                                               \
    }

DO_STN_LDN_P(he)
DO_STN_LDN_P(le)
DO_STN_LDN_P(be)

#undef DO_STN_LDN_P

#undef le_bswap
#undef be_bswap
#undef le_bswaps
#undef be_bswaps

#endif /* BSWAP_H */
