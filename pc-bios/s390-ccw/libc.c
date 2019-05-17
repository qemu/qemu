/*
 * libc-style definitions and functions
 *
 * Copyright 2018 IBM Corp.
 * Author(s): Collin L. Walling <walling@linux.vnet.ibm.com>
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include "libc.h"
#include "s390-ccw.h"

/**
 * atoui:
 * @str: the string to be converted.
 *
 * Given a string @str, convert it to an integer. Leading spaces are
 * ignored. Any other non-numerical value will terminate the conversion
 * and return 0. This function only handles numbers between 0 and
 * UINT64_MAX inclusive.
 *
 * Returns: an integer converted from the string @str, or the number 0
 * if an error occurred.
 */
uint64_t atoui(const char *str)
{
    int val = 0;

    if (!str || !str[0]) {
        return 0;
    }

    while (*str == ' ') {
        str++;
    }

    while (*str) {
        if (!isdigit(*(unsigned char *)str)) {
            break;
        }
        val = val * 10 + *str - '0';
        str++;
    }

    return val;
}

/**
 * uitoa:
 * @num: an integer (base 10) to be converted.
 * @str: a pointer to a string to store the conversion.
 * @len: the length of the passed string.
 *
 * Given an integer @num, convert it to a string. The string @str must be
 * allocated beforehand. The resulting string will be null terminated and
 * returned. This function only handles numbers between 0 and UINT64_MAX
 * inclusive.
 *
 * Returns: the string @str of the converted integer @num
 */
char *uitoa(uint64_t num, char *str, size_t len)
{
    long num_idx = 1; /* account for NUL */
    uint64_t tmp = num;

    IPL_assert(str != NULL, "uitoa: no space allocated to store string");

    /* Count indices of num */
    while ((tmp /= 10) != 0) {
        num_idx++;
    }

    /* Check if we have enough space for num and NUL */
    IPL_assert(len > num_idx, "uitoa: array too small for conversion");

    str[num_idx--] = '\0';

    /* Convert int to string */
    while (num_idx >= 0) {
        str[num_idx--] = num % 10 + '0';
        num /= 10;
    }

    return str;
}
