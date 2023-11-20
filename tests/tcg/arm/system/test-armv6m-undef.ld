ENTRY(exc_reset_thumb)

SECTIONS
{
    . = 0x0;
    .text : {
        *(.text)
    }
    .data : {
        *(.data)
    }
    .rodata : {
        *(.rodata)
    }
    .bss : {
        *(.bss)
    }
    /DISCARD/ : {
        *(.ARM.attributes)
    }
}
