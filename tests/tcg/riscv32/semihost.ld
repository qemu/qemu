/* SPDX-License-Identifier: GPL-2.0-or-later */

ENTRY(_start)

SECTIONS
{
    /* virt machine, RAM starts at 2gb */
    . = 0x80000000;
    .text : {
        *(.text)
    }
    .rodata : {
        *(.rodata)
    }
    /* align r/w section to next 2mb */
    . = ALIGN(1 << 21);
    .data : {
        *(.data)
    }
    .bss : {
        *(.bss)
    }
}
