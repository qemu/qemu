OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start);
SECTIONS
{
        . = 0x00040000;
        .text     : { *(.text)    } 
        .rodata    : { *(.rodata) }
        . = ALIGN(4096);
        .data     : { *(.data)    } 
        __bss_start = . ;
        .bss      : { *(.bss) *(COMMON) }
        _end = . ;
        /DISCARD/ : { *(.stab) 
                     *(.stabstr)
                     *(.comment)
                     *(.note)    
                  }
}
