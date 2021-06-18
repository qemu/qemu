/*
 * Linker script for linux hppa vdso.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

VERSION {
        /*
         * The kernel's vdso32.lds.S attempts to export
         *     __kernel_sigtramp_rt32
         *     __kernel_restart_syscall32
         * except that those symbols don't exist.  The actual symbols are
         *     __kernel_sigtramp_rt
         *     __kernel_restart_syscall
         * which means that nothing is exported at all.
         * QEMU handles syscall restart internally, so we don't
         * need to implement __kernel_restart_syscall at all.
         */
        LINUX_5.18 {
        local: *;
        };
}


PHDRS {
        phdr            PT_PHDR         FLAGS(4) PHDRS;
        load            PT_LOAD         FLAGS(7) FILEHDR PHDRS;
        dynamic         PT_DYNAMIC      FLAGS(4);
        note            PT_NOTE         FLAGS(4);
        eh_frame_hdr    PT_GNU_EH_FRAME;
}

SECTIONS {
        . = SIZEOF_HEADERS;

        /* The following, including the FILEHDRS and PHDRS, are modified
           when we relocate the binary.  We want them to be initially
           writable for the relocation; we'll force them read-only after.  */
        .note           : { *(.note*) }         :load :note
        .dynamic        : { *(.dynamic) }       :load :dynamic
        .dynsym         : { *(.dynsym) }        :load
        .data           : {
                /* There ought not be any real read-write data.
                   But since we manipulated the segment layout,
                   we have to put these sections somewhere.  */
                *(.data*)
                *(.sdata*)
                *(.got.plt) *(.got)
                *(.gnu.linkonce.d.*)
                *(.bss*)
                *(.dynbss*)
                *(.gnu.linkonce.b.*)
        }

        .rodata         : { *(.rodata) }
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
