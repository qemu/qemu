
typedef unsigned long va_list;

#define ACC    4
#define __read(source)                    \
({ va_list __res;                    \
    __asm__ __volatile__(                \
        "move\t%0, " #source "\n\t"        \
        : "=r" (__res));            \
    __res;                        \
})

enum format_type {
    FORMAT_TYPE_NONE,
    FORMAT_TYPE_HEX,
    FORMAT_TYPE_ULONG,
    FORMAT_TYPE_FLOAT
};

struct printf_spec {
    char    type;
};

static int format_decode(char *fmt, struct printf_spec *spec)
{
    char *start = fmt;

    for (; *fmt ; ++fmt) {
        if (*fmt == '%') {
            break;
        }
    }

    switch (*++fmt) {
    case 'x':
        spec->type = FORMAT_TYPE_HEX;
        break;

    case 'd':
        spec->type = FORMAT_TYPE_ULONG;
        break;

    case 'f':
        spec->type = FORMAT_TYPE_FLOAT;
        break;

    default:
        spec->type = FORMAT_TYPE_NONE;
    }

    return ++fmt - start;
}

void *memcpy(void *dest, void *src, int n)
{
    int i;
    char *s = src;
    char *d = dest;

    for (i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

char *number(char *buf, va_list num)
{
    int i;
    char *str = buf;
    static char digits[16] = "0123456789abcdef";
    str = str + sizeof(num) * 2;

    for (i = 0; i < sizeof(num) * 2; i++) {
        *--str = digits[num & 15];
        num >>= 4;
    }

    return buf + sizeof(num) * 2;
}

char *__number(char *buf, va_list num)
{
    int i;
    va_list mm = num;
    char *str = buf;

    if (!num) {
        *str++ = '0';
        return str;
    }

    for (i = 0; mm; mm = mm/10, i++) {
        /* Do nothing. */
    }

    str = str + i;

    while (num) {
        *--str = num % 10 + 48;
        num = num / 10;
    }

    return str + i;
}

va_list modf(va_list args, va_list *integer, va_list *num)
{
    int i;
    double dot_v = 0;
    va_list E, DOT, DOT_V;

    if (!args) {
        return 0;
    }

    for (i = 0, args = args << 1 >> 1; i < 52; i++) {
        if ((args >> i) & 0x1) {
            break;
        }
    }

    *integer = 0;

    if ((args >> 56 != 0x3f) || (args >> 52 == 0x3ff)) {
        E = (args >> 52) - 1023;
        DOT = 52 - E - i;
        DOT_V = args << (12 + E) >> (12 + E) >> i;
        *integer = ((args << 12 >> 12) >> (i + DOT)) | (1 << E);
    } else {
        E = ~((args >> 52) - 1023) + 1;
        DOT_V = args << 12 >> 12;

        dot_v += 1.0 / (1 << E);

        for (i = 1; i <= 16; i++) {
            if ((DOT_V >> (52 - i)) & 0x1) {
                dot_v += 1.0 / (1 << E + i);
            }
        }

        for (i = 1, E = 0; i <= ACC; i++) {
            dot_v *= 10;
            if (!(va_list)dot_v) {
                E++;
            }
    }

    *num = E;

    return dot_v;
    }

    if (args & 0xf) {
        for (i = 1; i <= 16; i++) {
            if ((DOT_V >> (DOT - i)) & 0x1) {
                dot_v += 1.0 / (1 << i);
            }
        }

        for (i = 1, E = 0; i <= ACC; i++) {
            dot_v *= 10;
            if (!(va_list)dot_v) {
                E++;
            }
        }

        *num = E;

        return dot_v;
    } else if (DOT) {
        for (i = 1; i <= DOT; i++) {
            if ((DOT_V >> (DOT - i)) & 0x1) {
                dot_v += 1.0 / (1 << i);
            }
        }

        for (i = 1; i <= ACC; i++) {
            dot_v = dot_v * 10;
        }

    return dot_v;
    }

    return 0;
}

int vsnprintf(char *buf, int size, char *fmt, va_list args)
{
    char *str, *mm;
    struct printf_spec spec = {0};

    str = mm = buf;

    while (*fmt) {
        char *old_fmt = fmt;
        int read = format_decode(fmt, &spec);

        fmt += read;

        switch (spec.type) {
        case FORMAT_TYPE_NONE: {
            memcpy(str, old_fmt, read);
            str += read;
            break;
        }
        case FORMAT_TYPE_HEX: {
            memcpy(str, old_fmt, read);
            str = number(str + read, args);
            for (; *mm ; ++mm) {
                if (*mm == '%') {
                    *mm = '0';
                break;
                }
            }
        break;
        }
        case FORMAT_TYPE_ULONG: {
            memcpy(str, old_fmt, read - 2);
            str = __number(str + read - 2, args);
            break;
        }
        case FORMAT_TYPE_FLOAT: {
            va_list integer, dot_v, num;
            dot_v = modf(args, &integer, &num);
            memcpy(str, old_fmt, read - 2);
            str += read - 2;
            if ((args >> 63 & 0x1)) {
                *str++ = '-';
            }
            str = __number(str, integer);
            if (dot_v) {
                *str++ = '.';
                while (num--) {
                    *str++ = '0';
                }
                str = __number(str, dot_v);
            }
            break;
        }
        }
    }
    *str = '\0';

    return str - buf;
}

static void serial_out(char *str)
{
    while (*str) {
        *(char *)0xffffffffb80003f8 = *str++;
    }
}

int vprintf(char *fmt, va_list args)
{
    int printed_len = 0;
    static char printf_buf[512];
    printed_len = vsnprintf(printf_buf, sizeof(printf_buf), fmt, args);
    serial_out(printf_buf);
    return printed_len;
}

int printf(char *fmt, ...)
{
    return vprintf(fmt, __read($5));
}
