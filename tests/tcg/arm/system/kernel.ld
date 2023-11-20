ENTRY(__start)

SECTIONS
{
    /* virt machine, RAM starts at 1gb */
    . = (1 << 30);
    .text : {
        *(.text)
    }
    .rodata : {
        *(.rodata)
    }
    /* align r/w section to next 2mb */
    . = ALIGN(1 << 21);
    .data : {
        *(.data)
    }
    .bss : {
        *(.bss)
    }
    /DISCARD/ : {
        *(.ARM.attributes)
    }
}
