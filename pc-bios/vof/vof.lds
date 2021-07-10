OUTPUT_FORMAT("elf32-powerpc")
OUTPUT_ARCH(powerpc:common)

/* set the entry point */
ENTRY ( __start )

SECTIONS {
	__executable_start = .;

	.text : {
		*(.text)
	}

	__etext = .;

	. = ALIGN(8);

	.data : {
		*(.data)
		*(.rodata .rodata.*)
		*(.got1)
		*(.sdata)
		*(.opd)
	}

	/* FIXME bss at end ??? */

	. = ALIGN(8);
	__bss_start = .;
	.bss : {
		*(.sbss) *(.scommon)
		*(.dynbss)
		*(.bss)
	}

	. = ALIGN(8);
	__bss_end = .;
	__bss_size = (__bss_end - __bss_start);

	. = ALIGN(256);
	__toc_start = DEFINED (.TOC.) ? .TOC. : ADDR (.got) + 0x8000;
	.got :
	{
		 *(.toc .got)
	}
	. = ALIGN(8);
	__toc_end = .;
}
