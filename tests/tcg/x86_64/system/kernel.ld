PHDRS {
	text PT_LOAD FLAGS(5);          /* R_E */
	note PT_NOTE FLAGS(0);          /* ___ */
}

SECTIONS {
	. = 0x100000;

	.text : {
		__load_st = .;
		*(.head)
		*(.text)
	} :text

	.rodata : {
		*(.rodata)
	} :text

        /DISCARD/ : {
                *(.note.gnu*)
        }

        .notes :  {
               *(.note.*)
        } :note

        .data : {
		*(.data)
		__load_en = .;
	} :text

	.bss : {
		*(.bss)
		__bss_en = .;
	}
}
