SECTIONS {
	. = 0x100000;

	.text : {
		__load_st = .;
		*(.head)
		*(.text)
	}

	.rodata : {
		*(.rodata)
	}

	.data : {
		*(.data)
		__load_en = .;
	}

	.bss : {
		*(.bss)
		__bss_en = .;
	}
}
