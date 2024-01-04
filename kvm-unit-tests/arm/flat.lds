
SECTIONS
{
    .text : { *(.init) *(.text) *(.text.*) }
    . = ALIGN(64K);
    etext = .;
    .data : {
        *(.data)
    }
    . = ALIGN(16);
    .rodata : { *(.rodata) }
    . = ALIGN(16);
    .bss : { *(.bss) }
    . = ALIGN(64K);
    edata = .;
    . += 64K;
    . = ALIGN(64K);
    /*
     * stack depth is 16K for arm and PAGE_SIZE for arm64, see THREAD_SIZE
     * sp must be 16 byte aligned for arm64, and 8 byte aligned for arm
     * sp must always be strictly less than the true stacktop
     */
    stackptr = . - 16;
    stacktop = .;
}

ENTRY(start)
