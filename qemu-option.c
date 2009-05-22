/*
 * Commandline option parsing functions
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2009 Kevin Wolf <kwolf@redhat.com>
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

#include <stdio.h>
#include <string.h>

#include "qemu-common.h"
#include "qemu-option.h"

/*
 * Extracts the name of an option from the parameter string (p points at the
 * first byte of the option name)
 *
 * The option name is delimited by delim (usually , or =) or the string end
 * and is copied into buf. If the option name is longer than buf_size, it is
 * truncated. buf is always zero terminated.
 *
 * The return value is the position of the delimiter/zero byte after the option
 * name in p.
 */
const char *get_opt_name(char *buf, int buf_size, const char *p, char delim)
{
    char *q;

    q = buf;
    while (*p != '\0' && *p != delim) {
        if (q && (q - buf) < buf_size - 1)
            *q++ = *p;
        p++;
    }
    if (q)
        *q = '\0';

    return p;
}

/*
 * Extracts the value of an option from the parameter string p (p points at the
 * first byte of the option value)
 *
 * This function is comparable to get_opt_name with the difference that the
 * delimiter is fixed to be comma which starts a new option. To specify an
 * option value that contains commas, double each comma.
 */
const char *get_opt_value(char *buf, int buf_size, const char *p)
{
    char *q;

    q = buf;
    while (*p != '\0') {
        if (*p == ',') {
            if (*(p + 1) != ',')
                break;
            p++;
        }
        if (q && (q - buf) < buf_size - 1)
            *q++ = *p;
        p++;
    }
    if (q)
        *q = '\0';

    return p;
}

/*
 * Searches an option list for an option with the given name
 */
QEMUOptionParameter *get_option_parameter(QEMUOptionParameter *list,
    const char *name)
{
    while (list && list->name) {
        if (!strcmp(list->name, name)) {
            return list;
        }
        list++;
    }

    return NULL;
}

/*
 * Sets the value of a parameter in a given option list. The parsing of the
 * value depends on the type of option:
 *
 * OPT_FLAG (uses value.n):
 *      If no value is given, the flag is set to 1.
 *      Otherwise the value must be "on" (set to 1) or "off" (set to 0)
 *
 * OPT_STRING (uses value.s):
 *      value is strdup()ed and assigned as option value
 *
 * OPT_SIZE (uses value.n):
 *      The value is converted to an integer. Suffixes for kilobytes etc. are
 *      allowed (powers of 1024).
 *
 * Returns 0 on succes, -1 in error cases
 */
int set_option_parameter(QEMUOptionParameter *list, const char *name,
    const char *value)
{
    // Find a matching parameter
    list = get_option_parameter(list, name);
    if (list == NULL) {
        fprintf(stderr, "Unknown option '%s'\n", name);
        return -1;
    }

    // Process parameter
    switch (list->type) {
    case OPT_FLAG:
        if (value != NULL) {
            if (!strcmp(value, "on")) {
                list->value.n = 1;
            } else if (!strcmp(value, "off")) {
                list->value.n = 0;
            } else {
                fprintf(stderr, "Option '%s': Use 'on' or 'off'\n", name);
                return -1;
            }
        } else {
            list->value.n = 1;
        }
        break;

    case OPT_STRING:
        if (value != NULL) {
            list->value.s = strdup(value);
        } else {
            fprintf(stderr, "Option '%s' needs a parameter\n", name);
            return -1;
        }
        break;

    case OPT_SIZE:
        if (value != NULL) {
            double sizef = strtod(value, (char**) &value);

            switch (*value) {
            case 'T':
                sizef *= 1024;
            case 'G':
                sizef *= 1024;
            case 'M':
                sizef *= 1024;
            case 'K':
            case 'k':
                sizef *= 1024;
            case 'b':
            case '\0':
                list->value.n = (uint64_t) sizef;
                break;
            default:
                fprintf(stderr, "Option '%s' needs size as parameter\n", name);
                fprintf(stderr, "You may use k, M, G or T suffixes for "
                    "kilobytes, megabytes, gigabytes and terabytes.\n");
                return -1;
            }
        } else {
            fprintf(stderr, "Option '%s' needs a parameter\n", name);
            return -1;
        }
        break;
    default:
        fprintf(stderr, "Bug: Option '%s' has an unknown type\n", name);
        return -1;
    }

    return 0;
}

/*
 * Sets the given parameter to an integer instead of a string.
 * This function cannot be used to set string options.
 *
 * Returns 0 on success, -1 in error cases
 */
int set_option_parameter_int(QEMUOptionParameter *list, const char *name,
    uint64_t value)
{
    // Find a matching parameter
    list = get_option_parameter(list, name);
    if (list == NULL) {
        fprintf(stderr, "Unknown option '%s'\n", name);
        return -1;
    }

    // Process parameter
    switch (list->type) {
    case OPT_FLAG:
    case OPT_NUMBER:
    case OPT_SIZE:
        list->value.n = value;
        break;

    default:
        return -1;
    }

    return 0;
}

/*
 * Frees a option list. If it contains strings, the strings are freed as well.
 */
void free_option_parameters(QEMUOptionParameter *list)
{
    QEMUOptionParameter *cur = list;

    while (cur && cur->name) {
        if (cur->type == OPT_STRING) {
            free(cur->value.s);
        }
        cur++;
    }

    free(list);
}

/*
 * Parses a parameter string (param) into an option list (dest).
 *
 * list is the templace is. If dest is NULL, a new copy of list is created for
 * it. If list is NULL, this function fails.
 *
 * A parameter string consists of one or more parameters, separated by commas.
 * Each parameter consists of its name and possibly of a value. In the latter
 * case, the value is delimited by an = character. To specify a value which
 * contains commas, double each comma so it won't be recognized as the end of
 * the parameter.
 *
 * For more details of the parsing see above.
 *
 * Returns a pointer to the first element of dest (or the newly allocated copy)
 * or NULL in error cases
 */
QEMUOptionParameter *parse_option_parameters(const char *param,
    QEMUOptionParameter *list, QEMUOptionParameter *dest)
{
    QEMUOptionParameter *cur;
    QEMUOptionParameter *allocated = NULL;
    char name[256];
    char value[256];
    char *param_delim, *value_delim;
    char next_delim;
    size_t num_options;

    if (list == NULL) {
        return NULL;
    }

    if (dest == NULL) {
        // Count valid options
        num_options = 0;
        cur = list;
        while (cur->name) {
            num_options++;
            cur++;
        }

        // Create a copy of the option list to fill in values
        dest = qemu_mallocz((num_options + 1) * sizeof(QEMUOptionParameter));
        allocated = dest;
        memcpy(dest, list, (num_options + 1) * sizeof(QEMUOptionParameter));
    }

    while (*param) {

        // Find parameter name and value in the string
        param_delim = strchr(param, ',');
        value_delim = strchr(param, '=');

        if (value_delim && (value_delim < param_delim || !param_delim)) {
            next_delim = '=';
        } else {
            next_delim = ',';
            value_delim = NULL;
        }

        param = get_opt_name(name, sizeof(name), param, next_delim);
        if (value_delim) {
            param = get_opt_value(value, sizeof(value), param + 1);
        }
        if (*param != '\0') {
            param++;
        }

        // Set the parameter
        if (set_option_parameter(dest, name, value_delim ? value : NULL)) {
            goto fail;
        }
    }

    return dest;

fail:
    // Only free the list if it was newly allocated
    free_option_parameters(allocated);
    return NULL;
}

/*
 * Prints all options of a list that have a value to stdout
 */
void print_option_parameters(QEMUOptionParameter *list)
{
    while (list && list->name) {
        switch (list->type) {
            case OPT_STRING:
                 if (list->value.s != NULL) {
                     printf("%s='%s' ", list->name, list->value.s);
                 }
                break;
            case OPT_FLAG:
                printf("%s=%s ", list->name, list->value.n ? "on" : "off");
                break;
            case OPT_SIZE:
            case OPT_NUMBER:
                printf("%s=%" PRId64 " ", list->name, list->value.n);
                break;
            default:
                printf("%s=(unkown type) ", list->name);
                break;
        }
        list++;
    }
}
