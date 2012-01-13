OUTPUT_FORMAT("elf32-xtensa-le")
ENTRY(_start)

__DYNAMIC = 0;

MEMORY {
	ram : ORIGIN = 0xd0000000, LENGTH = 0x08000000  /* 128M */
	rom : ORIGIN = 0xfe000000, LENGTH = 0x00001000  /* 4k */
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
    . = 0x00000000;
        *(.vector.window_overflow_4)
        *(.vector.window_overflow_4.*)
    . = 0x00000040;
        *(.vector.window_underflow_4)
        *(.vector.window_underflow_4.*)
    . = 0x00000080;
        *(.vector.window_overflow_8)
        *(.vector.window_overflow_8.*)
    . = 0x000000c0;
        *(.vector.window_underflow_8)
        *(.vector.window_underflow_8.*)
    . = 0x00000100;
        *(.vector.window_overflow_12)
        *(.vector.window_overflow_12.*)
    . = 0x00000140;
        *(.vector.window_underflow_12)
        *(.vector.window_underflow_12.*)

    . = 0x00000180;
        *(.vector.level2)
        *(.vector.level2.*)
    . = 0x000001c0;
        *(.vector.level3)
        *(.vector.level3.*)
    . = 0x00000200;
        *(.vector.level4)
        *(.vector.level4.*)
    . = 0x00000240;
        *(.vector.level5)
        *(.vector.level5.*)
    . = 0x00000280;
        *(.vector.level6)
        *(.vector.level6.*)
    . = 0x000002c0;
        *(.vector.level7)
        *(.vector.level7.*)

    . = 0x00000300;
        *(.vector.kernel)
        *(.vector.kernel.*)
    . = 0x00000340;
        *(.vector.user)
        *(.vector.user.*)
    . = 0x000003c0;
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

PROVIDE(_fstack = ORIGIN(ram) + LENGTH(ram) - 4);
