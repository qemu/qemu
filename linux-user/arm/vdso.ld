/*
 * Linker script for linux arm replacement vdso.
 *
 * Copyright 2023 Linaro, Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

VERSION {
        LINUX_2.6 {
        global:
                __vdso_clock_gettime;
                __vdso_gettimeofday;
                __vdso_clock_getres;
                __vdso_clock_gettime64;

        local: *;
        };
}


PHDRS {
        phdr            PT_PHDR         FLAGS(4) PHDRS;
        load            PT_LOAD         FLAGS(7) FILEHDR PHDRS; /* FLAGS=RWX */
        dynamic         PT_DYNAMIC      FLAGS(4);
        eh_frame_hdr    PT_GNU_EH_FRAME;
        note            PT_NOTE         FLAGS(4);
}

SECTIONS {
        . = SIZEOF_HEADERS;

        /*
         * The following, including the FILEHDRS and PHDRS, are modified
         * when we relocate the binary.  We want them to be initially
         * writable for the relocation; we'll force them read-only after.
         */
        .note           : { *(.note*) }         :load :note
        .dynamic        : { *(.dynamic) }       :load :dynamic
        .dynsym         : { *(.dynsym) }        :load
        /*
         * There ought not be any real read-write data.
         * But since we manipulated the segment layout,
         * we have to put these sections somewhere.
         */
        .data           : {
                *(.data*)
                *(.sdata*)
                *(.got.plt) *(.got)
                *(.gnu.linkonce.d.*)
                *(.bss*)
                *(.dynbss*)
                *(.gnu.linkonce.b.*)
        }

        .rodata         : { *(.rodata*) }
        .hash           : { *(.hash) }
        .gnu.hash       : { *(.gnu.hash) }
        .dynstr         : { *(.dynstr) }
        .gnu.version    : { *(.gnu.version) }
        .gnu.version_d  : { *(.gnu.version_d) }
        .gnu.version_r  : { *(.gnu.version_r) }
        .eh_frame_hdr   : { *(.eh_frame_hdr) }  :load :eh_frame_hdr
        .eh_frame       : { *(.eh_frame) }      :load

        .text           : { *(.text*) }         :load
}
