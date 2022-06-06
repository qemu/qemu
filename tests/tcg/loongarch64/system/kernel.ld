ENTRY(_start)

SECTIONS
{
    /* Linux kernel legacy start address.  */
    . = 0x9000000000200000;
    _text = .;
    .text : {
        *(.text)
    }
    .rodata : {
        *(.rodata)
    }
    _etext = .;

    . = ALIGN(8192);
    _data = .;
    .got : {
        *(.got)
    }
    .data : {
	*(.sdata)
        *(.data)
    }
    _edata = .;
    .bss : {
        *(.bss)
    }
    _end = .;
}
