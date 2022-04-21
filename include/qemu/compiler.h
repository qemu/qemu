/* compiler.h: macros to abstract away compiler specifics
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef COMPILER_H
#define COMPILER_H

#define HOST_BIG_ENDIAN (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)

/* HOST_LONG_BITS is the size of a native pointer in bits. */
#define HOST_LONG_BITS (__SIZEOF_POINTER__ * 8)

#if defined __clang_analyzer__ || defined __COVERITY__
#define QEMU_STATIC_ANALYSIS 1
#endif

#ifdef __cplusplus
#define QEMU_EXTERN_C extern "C"
#else
#define QEMU_EXTERN_C extern
#endif

#if defined(_WIN32) && (defined(__x86_64__) || defined(__i386__))
# define QEMU_PACKED __attribute__((gcc_struct, packed))
#else
# define QEMU_PACKED __attribute__((packed))
#endif

#define QEMU_ALIGNED(X) __attribute__((aligned(X)))

#ifndef glue
#define xglue(x, y) x ## y
#define glue(x, y) xglue(x, y)
#define stringify(s)	tostring(s)
#define tostring(s)	#s
#endif

#ifndef likely
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x)   __builtin_expect(!!(x), 0)
#endif

#ifndef container_of
#define container_of(ptr, type, member) ({                      \
        const typeof(((type *) 0)->member) *__mptr = (ptr);     \
        (type *) ((char *) __mptr - offsetof(type, member));})
#endif

#define sizeof_field(type, field) sizeof(((type *)0)->field)

/*
 * Calculate the number of bytes up to and including the given 'field' of
 * 'container'.
 */
#define endof(container, field) \
    (offsetof(container, field) + sizeof_field(container, field))

/* Convert from a base type to a parent type, with compile time checking.  */
#define DO_UPCAST(type, field, dev) ( __extension__ ( { \
    char __attribute__((unused)) offset_must_be_zero[ \
        -offsetof(type, field)]; \
    container_of(dev, type, field);}))

#define typeof_field(type, field) typeof(((type *)0)->field)
#define type_check(t1,t2) ((t1*)0 - (t2*)0)

#define QEMU_BUILD_BUG_ON_STRUCT(x) \
    struct { \
        int:(x) ? -1 : 1; \
    }

#define QEMU_BUILD_BUG_MSG(x, msg) _Static_assert(!(x), msg)

#define QEMU_BUILD_BUG_ON(x) QEMU_BUILD_BUG_MSG(x, "not expecting: " #x)

#define QEMU_BUILD_BUG_ON_ZERO(x) (sizeof(QEMU_BUILD_BUG_ON_STRUCT(x)) - \
                                   sizeof(QEMU_BUILD_BUG_ON_STRUCT(x)))

#if !defined(__clang__) && defined(_WIN32)
/*
 * Map __printf__ to __gnu_printf__ because we want standard format strings even
 * when MinGW or GLib include files use __printf__.
 */
# define __printf__ __gnu_printf__
#endif

#ifndef __has_warning
#define __has_warning(x) 0 /* compatibility with non-clang compilers */
#endif

#ifndef __has_feature
#define __has_feature(x) 0 /* compatibility with non-clang compilers */
#endif

#ifndef __has_builtin
#define __has_builtin(x) 0 /* compatibility with non-clang compilers */
#endif

#if __has_builtin(__builtin_assume_aligned) || !defined(__clang__)
#define HAS_ASSUME_ALIGNED
#endif

#ifndef __has_attribute
#define __has_attribute(x) 0 /* compatibility with older GCC */
#endif

#if defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer)
# define QEMU_SANITIZE_ADDRESS 1
#endif

#if defined(__SANITIZE_THREAD__) || __has_feature(thread_sanitizer)
# define QEMU_SANITIZE_THREAD 1
#endif

/*
 * GCC doesn't provide __has_attribute() until GCC 5, but we know all the GCC
 * versions we support have the "flatten" attribute. Clang may not have the
 * "flatten" attribute but always has __has_attribute() to check for it.
 */
#if __has_attribute(flatten) || !defined(__clang__)
# define QEMU_FLATTEN __attribute__((flatten))
#else
# define QEMU_FLATTEN
#endif

/*
 * If __attribute__((error)) is present, use it to produce an error at
 * compile time.  Otherwise, one must wait for the linker to diagnose
 * the missing symbol.
 */
#if __has_attribute(error)
# define QEMU_ERROR(X) __attribute__((error(X)))
#else
# define QEMU_ERROR(X)
#endif

/*
 * The nonstring variable attribute specifies that an object or member
 * declaration with type array of char or pointer to char is intended
 * to store character arrays that do not necessarily contain a terminating
 * NUL character. This is useful in detecting uses of such arrays or pointers
 * with functions that expect NUL-terminated strings, and to avoid warnings
 * when such an array or pointer is used as an argument to a bounded string
 * manipulation function such as strncpy.
 */
#if __has_attribute(nonstring)
# define QEMU_NONSTRING __attribute__((nonstring))
#else
# define QEMU_NONSTRING
#endif

/*
 * Forced inlining may be desired to encourage constant propagation
 * of function parameters.  However, it can also make debugging harder,
 * so disable it for a non-optimizing build.
 */
#if defined(__OPTIMIZE__)
#define QEMU_ALWAYS_INLINE  __attribute__((always_inline))
#else
#define QEMU_ALWAYS_INLINE
#endif

/**
 * In most cases, normal "fallthrough" comments are good enough for
 * switch-case statements, but sometimes the compiler has problems
 * with those. In that case you can use QEMU_FALLTHROUGH instead.
 */
#if __has_attribute(fallthrough)
# define QEMU_FALLTHROUGH __attribute__((fallthrough))
#else
# define QEMU_FALLTHROUGH do {} while (0) /* fallthrough */
#endif

#ifdef CONFIG_CFI
/*
 * If CFI is enabled, use an attribute to disable cfi-icall on the following
 * function
 */
#define QEMU_DISABLE_CFI __attribute__((no_sanitize("cfi-icall")))
#else
/* If CFI is not enabled, use an empty define to not change the behavior */
#define QEMU_DISABLE_CFI
#endif

#endif /* COMPILER_H */
