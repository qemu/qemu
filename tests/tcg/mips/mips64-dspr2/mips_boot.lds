OUTPUT_ARCH(mips)
SECTIONS
{
    . = 0xffffffff80100000;
    . = ALIGN((1 << 13));
    .text :
    {
        *(.text)
        *(.rodata)
        *(.rodata.*)
    }

    __init_begin = .;
    . = ALIGN((1 << 12));
    .init.text : AT(ADDR(.init.text) - 0)
    {
        *(.init.text)
    }
    .init.data : AT(ADDR(.init.data) - 0)
    {
        *(.init.data)
    }
    . = ALIGN((1 << 12));
    __init_end = .;

    . = ALIGN((1 << 13));
    .data :
    {
        *(.data)
    }
}
