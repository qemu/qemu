/*
 * Link script for the Nios2 10m50-ghrd board.
 *
 * Copyright Linaro Ltd 2022
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

MEMORY
{
  tpf (rx)  : ORIGIN = 0xc0000000, LENGTH = 1K
  ram (rwx) : ORIGIN = 0xc8000000, LENGTH = 128M
}

PHDRS
{
  RAM PT_LOAD;
}

ENTRY(_start)
EXTERN(_start)
EXTERN(_interrupt)
EXTERN(_fast_tlb_miss)

SECTIONS
{
    /* Begin at the (hardcoded) _interrupt entry point. */
    .text 0xc8000120 : {
        *(.text.intr)
        *(.text .text.* .gnu.linkonce.t.*)
    } >ram :RAM

    .rodata : ALIGN(4) {
        *(.rodata .rodata.* .gnu.linkonce.r.*)
    } > ram :RAM

    .eh_frame_hdr : ALIGN (4) {
        KEEP (*(.eh_frame_hdr))
        *(.eh_frame_entry .eh_frame_entry.*)
    } >ram :RAM
    .eh_frame : ALIGN (4) {
        KEEP (*(.eh_frame)) *(.eh_frame.*)
    } >ram :RAM

    .data : ALIGN(4) {
        *(.shdata)
        *(.data .data.* .gnu.linkonce.d.*)
    } >ram :RAM

    HIDDEN (_gp = ALIGN(16) + 0x7ff0);
    PROVIDE_HIDDEN (gp = _gp);
    .got : ALIGN(4) {
        *(.got.plt) *(.igot.plt) *(.got) *(.igot)
    } >ram :RAM

    .sdata : ALIGN(4) {
        *(.sdata .sdata.* .gnu.linkonce.s.*)
    } >ram :RAM

    .bss : ALIGN(4) {
         __bss_start = ABSOLUTE(.);
        *(.sbss .sbss.* .gnu.linkonce.sb.*)
        *(.scommon)
        *(.bss .bss.* .gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = ABSOLUTE(.);
    } >ram :RAM

    __stack = ORIGIN(ram) + LENGTH(ram);
}
