/* SPDX-License-Identifier: GPL-2.0-or-later */
/* See https://gitlab.com/qemu-project/qemu/-/work_items/3391 */

int main()
{
    int data = 0;

    /* Ensure that ignored segment override prefixes are actually ignored */
    asm volatile (
        "wrgsbase %0\n\t"
        ".byte 0x65, 0x26\n\t" /* prefixes: GS + ES */
        "movb $0, 0\n\t"
        ".byte 0x65, 0x2E\n\t" /* prefixes: GS + CS */
        "movb $0, 0\n\t"
        ".byte 0x65, 0x36\n\t" /* prefixes: GS + SS */
        "movb $0, 0\n\t"
        ".byte 0x65, 0x3E\n\t" /* prefixes: GS + DS */
        "movb $0, 0\n\t"
        :
        : "r" (&data)
        : "memory"
    );

    return 0;
}
