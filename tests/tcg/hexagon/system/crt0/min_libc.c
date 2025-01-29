/*
 *  Copyright(c) 2024-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * Small cheat: take size_t, NULL, and other type/symbol definitions from the
 * hexagon toolchain. We cannot link with the libc, though, as the actual
 * implementation for functions like printf and open are defined for Linux, and
 * we are running on "bare metal".
 */
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

FILE *const stdout = (FILE *)1;

void exit(int code)
{
    asm volatile(
        "r2 = %0\n"
        "stop(r0)\n"
        :
        : "r"(code)
        : "r2");
    __builtin_unreachable();
}

/* The assert() macro will use this. */
void __assert_fail(const char *assertion, const char *file, int line,
                   const char *function)
{
    printf("ASSERT fail '%s' at file '%s' line %d function %s\n",
           assertion, file, line, function);
    exit(1);
}

void *memset(void *b, int c, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        ((unsigned char *)b)[i] = (unsigned char)c;
    }
    return b;
}

int memcmp(const void *p1, const void *p2, size_t n)
{
    const char *s1 = p1;
    const char *s2 = p2;
    for ( ; n && (*s1 == *s2); s1++, s2++, n--) {
        /* empty */
    }
    return n ? *(unsigned char *)s1 - *(unsigned char *)s2 : 0;
}

int bcmp(const void *s1, const void *s2, size_t n)
{
    return __builtin_bcmp(s1, s2, n);
}


#define HEX_SYS_WRITEC          0x03
#define HEX_SYS_WRITE0          0x04
#define HEX_SYS_GET_CMDLINE     0x15

/*
 * Macro flavors:
 * - DIRECT_SWI takes up to two args an put them at r1 and r2.
 * - SWI takes up to four args and puts them in an array, placing the
 *   array address at r1.
 */

static int swi_ret, swi_err, swi_args[4];
#define DO_SWI(CODE, ARG0, ARG1) \
    do { \
        asm volatile( \
                "r0 = %2\n" \
                "r1 = %3\n" \
                "r2 = %4\n" \
                "trap0(#0)\n" \
                "%0 = r0\n" \
                "%1 = r1\n" \
                : "=r"(swi_ret), "=r"(swi_err) \
                : "r"(CODE), "r"(ARG0), "r"(ARG1) \
                : "r0", "r1", "r2", "memory" \
                ); \
    } while (0)

#define SWI0(CODE) DO_SWI(CODE, swi_args, 0)
#define SWI1(CODE, ARG0) \
    do { swi_args[0] = (uint32_t)(ARG0); SWI0(CODE); } while (0)
#define SWI2(CODE, ARG0, ARG1) \
    do { swi_args[1] = (uint32_t)(ARG1); SWI1(CODE, ARG0); } while (0)
#define SWI3(CODE, ARG0, ARG1, ARG2) \
    do { swi_args[2] = (uint32_t)(ARG2); SWI2(CODE, ARG0, ARG1); } while (0)
#define SWI4(CODE, ARG0, ARG1, ARG2, ARG3) \
    do { swi_args[3] = (uint32_t)(ARG3); SWI3(CODE, ARG0, ARG1, ARG2); } while (0)

#define GET_MACRO_5(_1, _2, _3, _4, _5, NAME, ...) NAME
#define SWI(...) \
    ({ GET_MACRO_5(__VA_ARGS__, SWI4, SWI3, SWI2, SWI1, SWI0)(__VA_ARGS__); \
       swi_ret; })

#define DIRECT_SWI0(CODE) DO_SWI(CODE, 0, 0)
#define DIRECT_SWI1(CODE, ARG1) DO_SWI(CODE, ARG1, 0)
#define DIRECT_SWI2(CODE, ARG1, ARG2) DO_SWI(CODE, ARG1, ARG2)

#define GET_MACRO_3(_1, _2, _3, NAME, ...) NAME
#define DIRECT_SWI(...) \
    ({ GET_MACRO_3(__VA_ARGS__, DIRECT_SWI2, DIRECT_SWI1, DIRECT_SWI0)(__VA_ARGS__); \
       swi_ret; })

int puts(const char *str)
{
    DIRECT_SWI(HEX_SYS_WRITE0, str);
    DIRECT_SWI(HEX_SYS_WRITE0, "\n");
    return 0;
}

int fputs(const char *str, FILE *f)
{
    assert(f == stdout); /* Only stdout is supported. */
    DIRECT_SWI(HEX_SYS_WRITE0, str);
    return 0;
}

size_t fwrite(const void *ptr, size_t size, size_t nitems, FILE *f)
{
    assert(f == stdout); /* Only stdout is supported. */
    for (size_t i = 0; i < size * nitems; i++) {
        DIRECT_SWI(HEX_SYS_WRITEC, &ptr[i]);
    }
    return size * nitems;
}

int putchar(int c)
{
    DIRECT_SWI(HEX_SYS_WRITEC, &c);
    return c;
}

static char *num_to_s(uint64_t signed_num, uint64_t base)
{
    static char buffer[1024];
    char *bptr = buffer;
    uint64_t num;

    if (base == 16) {
        num = signed_num;
    } else if (base == 10) {
        if (signed_num < 0) {
            *bptr++ = '-';
            signed_num *= -1;
        }
        num = signed_num;
    } else {
        puts("fatal: num_to_s expects base 16 or 10");
        exit(1);
    }

    if (!num) {
        return "0";
    }

    uint64_t divider = 1;
    for (uint64_t n = num; n >= base; n /= base) {
        divider *= base;
    }

    while (num) {
        unsigned int digit = num / divider;
        if (digit) {
            num %= divider;
            divider /= base;
            if (digit >= 10) {
                *bptr++ = 'a' + (digit - 10);
            } else {
                *bptr++ = '0' + digit;
            }
            while (num < divider) {
                *bptr++ = '0';
                divider /= base;
            }
        } else {
            divider /= base;
        }
    }

    *bptr = '\0';
    return buffer;
}

static int advance_prefix(const char **str_ptr, char *prefix)
{
    const char *str = *str_ptr;
    while (*str && *str == *prefix) {
        str++;
        prefix++;
    }
    str--;
    if (!*prefix) {
        *str_ptr = str;
        return 1;
    }
    return 0;
}

static char *pad0(char *str, int n)
{
    static char buffer[1024];
    int len = strlen(str);
    assert(n < 1024);

    int i;
    for (i = 0; i < n - len; i++) {
        buffer[i] = '0';
    }
    strcpy(&buffer[i], str);
    return buffer;
}

/*
 * Very simple implementation. No error checking.
 * Supported formats are:
 * %d, %s, %c, %x, %016llx
 */
int printf(const char *format, ...)
{
    va_list ap;
    __builtin_va_start(ap, format);
    for (const char *ptr = format; *ptr; ptr++) {
        if (*ptr == '%') {
            ptr++;
            switch (*ptr) {
            case 'd':
            case 'x':
            case 'p':
            {
                int num = __builtin_va_arg(ap, int);
                fputs(num_to_s(num, *ptr == 'd' ? 10 : 16), stdout);
                break;
            }
            case 's':
                fputs(__builtin_va_arg(ap, char *), stdout);
                break;
            case 'c':
                putchar(__builtin_va_arg(ap, int));
                break;
            case '%':
                putchar('%');
                break;
            case '0':
                if (advance_prefix(&ptr, "016llx")) {
                    uint64_t num = __builtin_va_arg(ap, uint64_t);
                    fputs(pad0(num_to_s(num, 16), 16), stdout);
                    break;
                }
                /* else: fallthrough */
            default:
                fputs("fatal: unknown printf modifier '", stdout);
                putchar(*ptr);
                puts("'");
                exit(1);
            }
        } else {
            putchar(*ptr);
        }
    }
    __builtin_va_end(ap);
    return 1;
}

size_t strlen(const char *s)
{
    size_t len = 0;
    for ( ; *s; s++) {
        len++;
    }
    return len;
}

char *strcpy(char *dst, const char *src)
{
    int i;
    for (i = 0; src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
    return dst;
}

int strcmp(const char *s1, const char *s2)
{
    for ( ; *s1 && (*s1 == *s2); s1++, s2++) {
        /* empty */
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

char *strrchr(const char *s, int c)
{
    for (int i = strlen(s) - 1; i >= 0; i--) {
        if (s[i] == c) {
            return (char *)&s[i];
        }
    }
    return NULL;
}

#define MAX_ARGS 15
/*
 * Very simplistic implementation, using static buffers, and assuming no
 * args will contain spaces.
 */
static inline char **getcmdline(int *argc)
{
    static char *args[MAX_ARGS] = { NULL };
    char buf[4096];
    char *c;
    int id = 0;

    assert(!SWI(HEX_SYS_GET_CMDLINE, buf, sizeof(buf)));

    *argc = 1;
    for (c = buf; *c; c++) {
        if (*c == ' ' && *(c + 1)) {
            (*argc)++;
        }
    }
    assert(*argc <= MAX_ARGS);

    if (*argc == 0) {
        return args;
    }

    args[id++] = buf;
    for (c = buf; *c; c++) {
        if (*c == ' ') {
            *c = '\0';
            if (id < *argc) {
                args[id++] = c + 1;
            }
        }
    }
    return args;
}

int main(int argc, char **argv, char **envp);
void _start_main(void)
{
    int argc;
    char **argv = getcmdline(&argc);
    /* For now, we ignore envp */
    char *envp[] = { NULL };
    exit(main(argc, argv, envp));
    exit(1);
}
