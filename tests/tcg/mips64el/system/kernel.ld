/*
 * Linker script for bare-metal R5900 tests
 *
 * Code runs from KSEG0 (unmapped, cached) and is loaded at the
 * corresponding physical address, in RAM mapped at zero.
 *
 * Author:
 *   Philippe Mathieu-Daudé
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

ENTRY(_start)

KSEG0 = 0x80000000;

SECTIONS
{
    . = KSEG0 + 0x100000;
    .text : AT(ADDR(.text) - KSEG0) {
        *(.text.boot)
        *(.text*)
    }
    .rodata : AT(ADDR(.rodata) - KSEG0) {
        *(.rodata*)
    }
    .data : AT(ADDR(.data) - KSEG0) {
        *(.data*)
        *(.sdata*)
    }
    .bss : AT(ADDR(.bss) - KSEG0) {
        *(.sbss*)
        *(.bss*)
        *(COMMON)
    }
    . = ALIGN(16) + 0x10000;
    _stack_top = .;

    /DISCARD/ : {
        *(.MIPS.abiflags)
        *(.reginfo)
        *(.pdr)
        *(.gnu.attributes)
        *(.comment)
    }
}
