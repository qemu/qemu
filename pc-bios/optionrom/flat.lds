SECTIONS
{
  . = 0;
  .text : { *(.text) *(.text.$) }
}
ENTRY(_start)
