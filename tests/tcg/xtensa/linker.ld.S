#include "core-isa.h"

#if XTENSA_HAVE_BE
OUTPUT_FORMAT("elf32-xtensa-be")
#else
OUTPUT_FORMAT("elf32-xtensa-le")
#endif
ENTRY(_start)

__DYNAMIC = 0;

MEMORY {
    ram : ORIGIN = XCHAL_VECBASE_RESET_VADDR, LENGTH = 0x08000000  /* 128M */
    rom : ORIGIN = XCHAL_RESET_VECTOR_VADDR, LENGTH = 0x00001000  /* 4k */
}

SECTIONS
{
    .init :
    {
        *(.init)
        *(.init.*)
    } > rom

    .vector :
    {
    . = XCHAL_WINDOW_OF4_VECOFS;
        *(.vector.window_overflow_4)
        *(.vector.window_overflow_4.*)
    . = XCHAL_WINDOW_UF4_VECOFS;
        *(.vector.window_underflow_4)
        *(.vector.window_underflow_4.*)
    . = XCHAL_WINDOW_OF8_VECOFS;
        *(.vector.window_overflow_8)
        *(.vector.window_overflow_8.*)
    . = XCHAL_WINDOW_UF8_VECOFS;
        *(.vector.window_underflow_8)
        *(.vector.window_underflow_8.*)
    . = XCHAL_WINDOW_OF12_VECOFS;
        *(.vector.window_overflow_12)
        *(.vector.window_overflow_12.*)
    . = XCHAL_WINDOW_UF12_VECOFS;
        *(.vector.window_underflow_12)
        *(.vector.window_underflow_12.*)

#if XCHAL_NUM_INTLEVELS + XCHAL_HAVE_NMI >= 2
    . = XCHAL_INTLEVEL2_VECOFS;
        *(.vector.level2)
        *(.vector.level2.*)
#endif
#if XCHAL_NUM_INTLEVELS + XCHAL_HAVE_NMI >= 3
    . = XCHAL_INTLEVEL3_VECOFS;
        *(.vector.level3)
        *(.vector.level3.*)
#endif
#if XCHAL_NUM_INTLEVELS + XCHAL_HAVE_NMI >= 4
    . = XCHAL_INTLEVEL4_VECOFS;
        *(.vector.level4)
        *(.vector.level4.*)
#endif
#if XCHAL_NUM_INTLEVELS + XCHAL_HAVE_NMI >= 5
    . = XCHAL_INTLEVEL5_VECOFS;
        *(.vector.level5)
        *(.vector.level5.*)
#endif
#if XCHAL_NUM_INTLEVELS + XCHAL_HAVE_NMI >= 6
    . = XCHAL_INTLEVEL6_VECOFS;
        *(.vector.level6)
        *(.vector.level6.*)
#endif
#if XCHAL_NUM_INTLEVELS + XCHAL_HAVE_NMI >= 7
    . = XCHAL_INTLEVEL7_VECOFS;
        *(.vector.level7)
        *(.vector.level7.*)
#endif

    . = XCHAL_KERNEL_VECOFS;
        *(.vector.kernel)
        *(.vector.kernel.*)
    . = XCHAL_USER_VECOFS;
        *(.vector.user)
        *(.vector.user.*)
    . = XCHAL_DOUBLEEXC_VECOFS;
        *(.vector.double)
        *(.vector.double.*)
    } > ram

    .text :
    {
        _ftext = .;
        *(.text .stub .text.* .gnu.linkonce.t.* .literal .literal.*)
        _etext = .;
    } > ram

    .rodata :
    {
        . = ALIGN(4);
        _frodata = .;
        *(.rodata .rodata.* .gnu.linkonce.r.*)
        *(.rodata1)
        _erodata = .;
    } > ram

    .data :
    {
        . = ALIGN(4);
        _fdata = .;
        *(.data .data.* .gnu.linkonce.d.*)
        *(.data1)
        _gp = ALIGN(16);
        *(.sdata .sdata.* .gnu.linkonce.s.*)
        _edata = .;
    } > ram

    .bss :
    {
        . = ALIGN(4);
        _fbss = .;
        *(.dynsbss)
        *(.sbss .sbss.* .gnu.linkonce.sb.*)
        *(.scommon)
        *(.dynbss)
        *(.bss .bss.* .gnu.linkonce.b.*)
        *(COMMON)
        _ebss = .;
        _end = .;
    } > ram
}

PROVIDE(_fstack = (ORIGIN(ram) & 0xf0000000) + LENGTH(ram) - 16);
