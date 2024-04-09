/* SPDX-License-Identifier: GPL-2.0-or-later */
/* See https://gitlab.com/qemu-project/qemu/-/issues/2150 */

int main()
{
    asm volatile(
        "movi     v6.4s, #1\n"
        "movi     v7.4s, #0\n"
        "sub      v6.2d, v7.2d, v6.2d\n"
        : : : "v6", "v7");
    return 0;
}
