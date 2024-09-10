ENTRY(__start)

MEMORY {
    /* On virt machine RAM starts at 1 GiB. */

    /* Align text and rodata to the 1st 2 MiB chunk. */
    TXT (rx) : ORIGIN = 1 << 30, LENGTH = 2M
    /* Align r/w data to the 2nd 2 MiB chunk. */
    DAT (rw) : ORIGIN = (1 << 30) + 2M, LENGTH = 2M
}

SECTIONS {
    .text : {
        *(.text)
        *(.rodata)
    } >TXT
    .data : {
        *(.data)
        *(.bss)
    } >DAT
    /DISCARD/ : {
        *(.ARM.attributes)
    }
}
