/* Default linker script, for normal executables */
OUTPUT_FORMAT("elf32-tricore")
OUTPUT_ARCH(tricore)
ENTRY(_start)

/* the internal ram description */
MEMORY
{
  text_ram (rx!p): org = 0x80000000, len = 15K
  data_ram (w!xp): org = 0xd0000000, len = 130K
}
/*
 * Define the sizes of the user and system stacks.
 */
__USTACK_SIZE = DEFINED (__USTACK_SIZE) ? __USTACK_SIZE : 1K ;
/*
 * Define the start address and the size of the context save area.
 */
__CSA_BEGIN =  0xd0000000 ;
__CSA_SIZE =  8k ;
__CSA_END = __CSA_BEGIN + __CSA_SIZE ;

SECTIONS
{
  .text  :
  {
    *(.text)
    . = ALIGN(8);
  } > text_ram

  .rodata :
  {
    *(.rodata)
    *(.rodata1)
  } > data_ram

  .data :
  {
    . = ALIGN(8) ;
    *(.data)
    *(.data.*)
    . = ALIGN(8) ;
    __USTACK = . + __USTACK_SIZE -768;

  } > data_ram
  /*
   * Allocate space for BSS sections.
   */
  .bss  :
  {
    BSS_BASE = . ;
    *(.bss)
    *(COMMON)
    . = ALIGN(8) ;
  } > data_ram
  /* Make sure CSA, stack and heap addresses are properly aligned.  */
  _. = ASSERT ((__CSA_BEGIN & 0x3f) == 0 , "illegal CSA start address") ;
  _. = ASSERT ((__CSA_SIZE & 0x3f) == 0 , "illegal CSA size") ;

}
