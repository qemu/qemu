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

#define QEMU_PACKED __attribute__((packed))
#define QEMU_ALIGNED(X) __attribute__((aligned(X)))

#ifndef glue
#define xglue(x, y) x ## y
#define glue(x, y) xglue(x, y)
#define stringify(s) tostring(s)
#define tostring(s) #s
#endif

/* Expands into an identifier stemN, where N is another number each time */
#define MAKE_IDENTIFIER(stem) glue(stem, __COUNTER__)

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

/*
 * Apple clang version 14 has a bug in its __builtin_subcll(); define
 * BUILTIN_SUBCLL_BROKEN for the offending versions so we can avoid it.
 * When a version of Apple clang which has this bug fixed is released
 * we can add an upper bound to this check.
 * See https://gitlab.com/qemu-project/qemu/-/issues/1631
 * and https://gitlab.com/qemu-project/qemu/-/issues/1659 for details.
 * The bug never made it into any upstream LLVM releases, only Apple ones.
 */
#if defined(__apple_build_version__) && __clang_major__ >= 14
#define BUILTIN_SUBCLL_BROKEN
#endif

#if __has_attribute(annotate)
#define QEMU_ANNOTATE(x) __attribute__((annotate(x)))
#else
#define QEMU_ANNOTATE(x)
#endif

#if __has_attribute(used)
# define QEMU_USED __attribute__((used))
#else
# define QEMU_USED
#endif

/*
 * http://clang.llvm.org/docs/ThreadSafetyAnalysis.html
 *
 * TSA is available since clang 3.6-ish.
 */
#ifdef __clang__
#  define TSA(x)   __attribute__((x))
#else
#  define TSA(x)   /* No TSA, make TSA attributes no-ops. */
#endif

/*
 * TSA_CAPABILITY() is used to annotate typedefs:
 *
 * typedef pthread_mutex_t TSA_CAPABILITY("mutex") tsa_mutex;
 */
#define TSA_CAPABILITY(x) TSA(capability(x))

/*
 * TSA_GUARDED_BY() is used to annotate global variables,
 * the data is guarded:
 *
 * Foo foo TSA_GUARDED_BY(mutex);
 */
#define TSA_GUARDED_BY(x) TSA(guarded_by(x))

/*
 * TSA_PT_GUARDED_BY() is used to annotate global pointers, the data
 * behind the pointer is guarded.
 *
 * Foo* ptr TSA_PT_GUARDED_BY(mutex);
 */
#define TSA_PT_GUARDED_BY(x) TSA(pt_guarded_by(x))

/*
 * The TSA_REQUIRES() is used to annotate functions: the caller of the
 * function MUST hold the resource, the function will NOT release it.
 *
 * More than one mutex may be specified, comma-separated.
 *
 * void Foo(void) TSA_REQUIRES(mutex);
 */
#define TSA_REQUIRES(...) TSA(requires_capability(__VA_ARGS__))
#define TSA_REQUIRES_SHARED(...) TSA(requires_shared_capability(__VA_ARGS__))

/*
 * TSA_EXCLUDES() is used to annotate functions: the caller of the
 * function MUST NOT hold resource, the function first acquires the
 * resource, and then releases it.
 *
 * More than one mutex may be specified, comma-separated.
 *
 * void Foo(void) TSA_EXCLUDES(mutex);
 */
#define TSA_EXCLUDES(...) TSA(locks_excluded(__VA_ARGS__))

/*
 * TSA_ACQUIRE() is used to annotate functions: the caller of the
 * function MUST NOT hold the resource, the function will acquire the
 * resource, but NOT release it.
 *
 * More than one mutex may be specified, comma-separated.
 *
 * void Foo(void) TSA_ACQUIRE(mutex);
 */
#define TSA_ACQUIRE(...) TSA(acquire_capability(__VA_ARGS__))
#define TSA_ACQUIRE_SHARED(...) TSA(acquire_shared_capability(__VA_ARGS__))

/*
 * TSA_RELEASE() is used to annotate functions: the caller of the
 * function MUST hold the resource, but the function will then release it.
 *
 * More than one mutex may be specified, comma-separated.
 *
 * void Foo(void) TSA_RELEASE(mutex);
 */
#define TSA_RELEASE(...) TSA(release_capability(__VA_ARGS__))
#define TSA_RELEASE_SHARED(...) TSA(release_shared_capability(__VA_ARGS__))

/*
 * TSA_NO_TSA is used to annotate functions.  Use only when you need to.
 *
 * void Foo(void) TSA_NO_TSA;
 */
#define TSA_NO_TSA TSA(no_thread_safety_analysis)

/*
 * TSA_ASSERT() is used to annotate functions: This function will assert that
 * the lock is held. When it returns, the caller of the function is assumed to
 * already hold the resource.
 *
 * More than one mutex may be specified, comma-separated.
 */
#define TSA_ASSERT(...) TSA(assert_capability(__VA_ARGS__))
#define TSA_ASSERT_SHARED(...) TSA(assert_shared_capability(__VA_ARGS__))

/*
 * Ugly CPP trick that is like "defined FOO", but also works in C
 * code.  Useful to replace #ifdef with "if" statements; assumes
 * the symbol was defined with Meson's "config.set()", so it is empty
 * if defined.
 */
#define IS_ENABLED(x)                  IS_EMPTY(x)

#define IS_EMPTY_JUNK_                 junk,
#define IS_EMPTY(value)                IS_EMPTY_(IS_EMPTY_JUNK_##value)

/* Expands to either SECOND_ARG(junk, 1, 0) or SECOND_ARG(IS_EMPTY_JUNK_CONFIG_FOO 1, 0)  */
#define SECOND_ARG(first, second, ...) second
#define IS_EMPTY_(junk_maybecomma)     SECOND_ARG(junk_maybecomma 1, 0)

#ifndef __cplusplus
/*
 * Useful in macros that need to declare temporary variables.  For example,
 * the variable that receives the old value of an atomically-accessed
 * variable must be non-qualified, because atomic builtins return values
 * through a pointer-type argument as in __atomic_load(&var, &old, MODEL).
 *
 * This macro has to handle types smaller than int manually, because of
 * implicit promotion.  int and larger types, as well as pointers, can be
 * converted to a non-qualified type just by applying a binary operator.
 */
#define typeof_strip_qual(expr)                                                    \
  typeof(                                                                          \
    __builtin_choose_expr(                                                         \
      __builtin_types_compatible_p(typeof(expr), bool) ||                          \
        __builtin_types_compatible_p(typeof(expr), const bool) ||                  \
        __builtin_types_compatible_p(typeof(expr), volatile bool) ||               \
        __builtin_types_compatible_p(typeof(expr), const volatile bool),           \
        (bool)1,                                                                   \
    __builtin_choose_expr(                                                         \
      __builtin_types_compatible_p(typeof(expr), signed char) ||                   \
        __builtin_types_compatible_p(typeof(expr), const signed char) ||           \
        __builtin_types_compatible_p(typeof(expr), volatile signed char) ||        \
        __builtin_types_compatible_p(typeof(expr), const volatile signed char),    \
        (signed char)1,                                                            \
    __builtin_choose_expr(                                                         \
      __builtin_types_compatible_p(typeof(expr), unsigned char) ||                 \
        __builtin_types_compatible_p(typeof(expr), const unsigned char) ||         \
        __builtin_types_compatible_p(typeof(expr), volatile unsigned char) ||      \
        __builtin_types_compatible_p(typeof(expr), const volatile unsigned char),  \
        (unsigned char)1,                                                          \
    __builtin_choose_expr(                                                         \
      __builtin_types_compatible_p(typeof(expr), signed short) ||                  \
        __builtin_types_compatible_p(typeof(expr), const signed short) ||          \
        __builtin_types_compatible_p(typeof(expr), volatile signed short) ||       \
        __builtin_types_compatible_p(typeof(expr), const volatile signed short),   \
        (signed short)1,                                                           \
    __builtin_choose_expr(                                                         \
      __builtin_types_compatible_p(typeof(expr), unsigned short) ||                \
        __builtin_types_compatible_p(typeof(expr), const unsigned short) ||        \
        __builtin_types_compatible_p(typeof(expr), volatile unsigned short) ||     \
        __builtin_types_compatible_p(typeof(expr), const volatile unsigned short), \
        (unsigned short)1,                                                         \
      (expr)+0))))))
#endif

#endif /* COMPILER_H */
