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

#ifndef QEMU_OPTIONS_H
#define QEMU_OPTIONS_H

enum QEMUOptionParType {
    OPT_FLAG,
    OPT_NUMBER,
    OPT_SIZE,
    OPT_STRING,
};

typedef struct QEMUOptionParameter {
    const char *name;
    enum QEMUOptionParType type;
    union {
        uint64_t n;
        char* s;
    } value;
    const char *help;
} QEMUOptionParameter;


const char *get_opt_name(char *buf, int buf_size, const char *p, char delim);
const char *get_opt_value(char *buf, int buf_size, const char *p);


/*
 * The following functions take a parameter list as input. This is a pointer to
 * the first element of a QEMUOptionParameter array which is terminated by an
 * entry with entry->name == NULL.
 */

QEMUOptionParameter *get_option_parameter(QEMUOptionParameter *list,
    const char *name);
int set_option_parameter(QEMUOptionParameter *list, const char *name,
    const char *value);
int set_option_parameter_int(QEMUOptionParameter *list, const char *name,
    uint64_t value);
QEMUOptionParameter *parse_option_parameters(const char *param,
    QEMUOptionParameter *list, QEMUOptionParameter *dest);
void free_option_parameters(QEMUOptionParameter *list);
void print_option_parameters(QEMUOptionParameter *list);
void print_option_help(QEMUOptionParameter *list);

#endif
