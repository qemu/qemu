/* Macro file for Coccinelle
 *
 * Copyright (C) 2015 Red Hat, Inc.
 *
 * Authors:
 *  Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or, at your
 * option, any later version.  See the COPYING file in the top-level directory.
 */

/* Coccinelle only does limited parsing of headers, and chokes on some idioms
 * defined in compiler.h and queue.h.  Macros that Coccinelle must know about
 * in order to parse .c files must be in a separate macro file---which is
 * exactly what you're staring at now.
 *
 * To use this file, add the "--macro-file scripts/cocci-macro-file.h" to the
 * Coccinelle command line.
 */

/* From qemu/compiler.h */
#define QEMU_GNUC_PREREQ(maj, min) 1
#define QEMU_NORETURN __attribute__ ((__noreturn__))
#define QEMU_WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#define QEMU_SENTINEL __attribute__((sentinel))
#define QEMU_ARTIFICIAL __attribute__((always_inline, artificial))
#define QEMU_PACKED __attribute__((gcc_struct, packed))

#define cat(x,y) x ## y
#define cat2(x,y) cat(x,y)
#define QEMU_BUILD_BUG_ON(x) \
    typedef char cat2(qemu_build_bug_on__,__LINE__)[(x)?-1:1] __attribute__((unused));

#define GCC_FMT_ATTR(n, m) __attribute__((format(gnu_printf, n, m)))

#define xglue(x, y) x ## y
#define glue(x, y) xglue(x, y)
#define stringify(s)	tostring(s)
#define tostring(s)	#s

#define typeof_field(type, field) typeof(((type *)0)->field)
#define type_check(t1,t2) ((t1*)0 - (t2*)0)

/* From qemu/queue.h */

#define QLIST_HEAD(name, type)                                          \
struct name {                                                           \
        struct type *lh_first;  /* first element */                     \
}

#define QLIST_HEAD_INITIALIZER(head)                                    \
        { NULL }

#define QLIST_ENTRY(type)                                               \
struct {                                                                \
        struct type *le_next;   /* next element */                      \
        struct type **le_prev;  /* address of previous next element */  \
}

/*
 * Singly-linked List definitions.
 */
#define QSLIST_HEAD(name, type)                                          \
struct name {                                                           \
        struct type *slh_first; /* first element */                     \
}

#define QSLIST_HEAD_INITIALIZER(head)                                    \
        { NULL }

#define QSLIST_ENTRY(type)                                               \
struct {                                                                \
        struct type *sle_next;  /* next element */                      \
}

/*
 * Simple queue definitions.
 */
#define QSIMPLEQ_HEAD(name, type)                                       \
struct name {                                                           \
    struct type *sqh_first;    /* first element */                      \
    struct type **sqh_last;    /* addr of last next element */          \
}

#define QSIMPLEQ_HEAD_INITIALIZER(head)                                 \
    { NULL, &(head).sqh_first }

#define QSIMPLEQ_ENTRY(type)                                            \
struct {                                                                \
    struct type *sqe_next;    /* next element */                        \
}

/*
 * Tail queue definitions.
 */
#define Q_TAILQ_HEAD(name, type, qual)                                  \
struct name {                                                           \
        qual type *tqh_first;           /* first element */             \
        qual type *qual *tqh_last;      /* addr of last next element */ \
}
#define QTAILQ_HEAD(name, type)                                         \
struct name {                                                           \
        type *tqh_first;      /* first element */                       \
        type **tqh_last;      /* addr of last next element */           \
}

#define QTAILQ_HEAD_INITIALIZER(head)                                   \
        { NULL, &(head).tqh_first }

#define Q_TAILQ_ENTRY(type, qual)                                       \
struct {                                                                \
        qual type *tqe_next;            /* next element */              \
        qual type *qual *tqe_prev;      /* address of previous next element */\
}
#define QTAILQ_ENTRY(type)                                              \
struct {                                                                \
        type *tqe_next;       /* next element */                        \
        type **tqe_prev;      /* address of previous next element */    \
}
