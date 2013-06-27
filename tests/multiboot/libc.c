/*
 * Copyright (c) 2013 Kevin Wolf <kwolf@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "libc.h"

static void print_char(char c)
{
    outb(0xe9, c);
}

static void print_str(char *s)
{
    while (*s) {
        print_char(*s++);
    }
}

static void print_num(uint64_t value, int base)
{
    char digits[] = "0123456789abcdef";
    char buf[32] = { 0 };
    int i = sizeof(buf) - 2;

    do {
        buf[i--] = digits[value % base];
        value /= base;
    } while (value);

    print_str(&buf[i + 1]);
}

void printf(const char *fmt, ...)
{
    va_list ap;
    uint64_t val;
    char *str;
    int base;
    int has_long;
    int alt_form;

    va_start(ap, fmt);

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            print_char(*fmt);
            continue;
        }
        fmt++;

        if (*fmt == '#') {
            fmt++;
            alt_form = 1;
        } else {
            alt_form = 0;
        }

        if (*fmt == 'l') {
            fmt++;
            if (*fmt == 'l') {
                fmt++;
                has_long = 2;
            } else {
                has_long = 1;
            }
        } else {
            has_long = 0;
        }

        switch (*fmt) {
        case 'x':
        case 'p':
            base = 16;
            goto convert_number;
        case 'd':
        case 'i':
        case 'u':
            base = 10;
            goto convert_number;
        case 'o':
            base = 8;
            goto convert_number;

        convert_number:
            switch (has_long) {
            case 0:
                val = va_arg(ap, unsigned int);
                break;
            case 1:
                val = va_arg(ap, unsigned long);
                break;
            case 2:
                val = va_arg(ap, unsigned long long);
                break;
            }

            if (alt_form && base == 16) {
                print_str("0x");
            }

            print_num(val, base);
            break;

        case 's':
            str = va_arg(ap, char*);
            print_str(str);
            break;
        case '%':
            print_char(*fmt);
            break;
        default:
            print_char('%');
            print_char(*fmt);
            break;
        }
    }

    va_end(ap);
}


