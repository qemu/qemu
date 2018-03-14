/*
 * Copyright (c) 2018 Kevin Wolf <kwolf@redhat.com>
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

.section multiboot

#define MB_MAGIC 0x1badb002
#define MB_FLAGS 0x10000
#define MB_CHECKSUM -(MB_MAGIC + MB_FLAGS)

.align  4
.int    MB_MAGIC
.int    MB_FLAGS
.int    MB_CHECKSUM

#define LAST_BYTE_VALUE 0xa5

/*
 * Order of fields in the a.out kludge header fields:
 *
 * header_addr
 * load_addr
 * load_end_addr
 * bss_end_addr
 * entry_addr
 */
#if SCENARIO == 1
/* Well-behaved kernel file with explicit bss_end */
.int    0x100000
.int    0x100000
.int    data_end
.int    data_end
.int    _start
#elif SCENARIO == 2
/* Well-behaved kernel file with default bss_end */
.int    0x100000
.int    0x100000
.int    data_end
.int    0
.int    _start
#elif SCENARIO == 3
/* Well-behaved kernel file with default load_end */
.int    0x100000
.int    0x100000
.int    0
.int    0
.int    _start
#elif SCENARIO == 4
/* Well-behaved kernel file with load_end < data_end and bss > data_end */
#undef LAST_BYTE_VALUE
#define LAST_BYTE_VALUE 0
.int    0x100000
.int    0x100000
.int    code_end
.int    0x140000
.int    _start
#elif SCENARIO == 5
/* header < load */
.int    0x10000
.int    0x100000
.int    data_end
.int    data_end
.int    _start
#elif SCENARIO == 6
/* load_end < load */
.int    0x100000
.int    0x100000
.int    0x10000
.int    data_end
.int    _start
#elif SCENARIO == 7
/* header much larger than in reality with default load_end */
.int    0x80000000
.int    0x100000
.int    0
.int    data_end
.int    _start
#elif SCENARIO == 8
/* bss_end < load_end - load (regression test for CVE-2018-7550) */
.int    0x100000
.int    0x100000
.int    data_end
.int    code_end
.int    _start
#elif SCENARIO == 9
/* Default load_end_addr, load_addr + kernel_file_size > UINT32_MAX */
.int    0xfffff000
.int    0xfffff000
.int    0
.int    0xfffff001
.int    _start
#else
#error Invalid SCENARIO
#endif

.section .text
.global _start
_start:
    xor     %eax, %eax

    cmpb    $LAST_BYTE_VALUE, last_byte
    je      passed
    or      $0x1, %eax
passed:

    /* Test device exit */
    outl    %eax, $0xf4

    cli
    hlt
    jmp .
code_end:

#if SCENARIO != 8
.space 8192
#endif

last_byte:
.byte 0xa5
data_end:
