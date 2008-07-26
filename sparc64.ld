OUTPUT_FORMAT("elf64-sparc", "elf64-sparc",
              "elf64-sparc")
OUTPUT_ARCH(sparc:v9)
SEARCH_DIR(/lib64); SEARCH_DIR(/lib); SEARCH_DIR(/usr/lib64); SEARCH_DIR(/usr/lib); SEARCH_DIR(/usr/local/lib64); SEARCH_DIR(/usr/local/lib); SEARCH_DIR(/usr/alpha-unknown-linux-gnu/lib);
ENTRY(_start)
SECTIONS
{
  /* Read-only sections, merged into text segment: */
  . = 0x60000000 + SIZEOF_HEADERS;
  .interp     : { *(.interp)    }
  .hash          : { *(.hash)           }
  .dynsym        : { *(.dynsym)         }
  .dynstr        : { *(.dynstr)         }
  .gnu.version   : { *(.gnu.version)    }
  .gnu.version_d   : { *(.gnu.version_d)        }
  .gnu.version_r   : { *(.gnu.version_r)        }
  .rel.text      :
    { *(.rel.text) *(.rel.gnu.linkonce.t*) }
  .rela.text     :
    { *(.rela.text) *(.rela.gnu.linkonce.t*) }
  .rel.data      :
    { *(.rel.data) *(.rel.gnu.linkonce.d*) }
  .rela.data     :
    { *(.rela.data) *(.rela.gnu.linkonce.d*) }
  .rel.rodata    :
    { *(.rel.rodata) *(.rel.gnu.linkonce.r*) }
  .rela.rodata   :
    { *(.rela.rodata) *(.rela.gnu.linkonce.r*) }
  .rel.got       : { *(.rel.got)                }
  .rela.got      : { *(.rela.got)               }
  .rel.ctors     : { *(.rel.ctors)      }
  .rela.ctors    : { *(.rela.ctors)     }
  .rel.dtors     : { *(.rel.dtors)      }
  .rela.dtors    : { *(.rela.dtors)     }
  .rel.init      : { *(.rel.init)       }
  .rela.init     : { *(.rela.init)      }
  .rel.fini      : { *(.rel.fini)       }
  .rela.fini     : { *(.rela.fini)      }
  .rel.bss       : { *(.rel.bss)                }
  .rela.bss      : { *(.rela.bss)               }
  .rel.plt       : { *(.rel.plt)                }
  .rela.plt      : { *(.rela.plt)               }
  .init          : { *(.init)   } =0x47ff041f
  .text      :
  {
    *(.text)
    /* .gnu.warning sections are handled specially by elf32.em.  */
    *(.gnu.warning)
    *(.gnu.linkonce.t*)
  } =0x47ff041f
  _etext = .;
  PROVIDE (etext = .);
  .fini      : { *(.fini)    } =0x47ff041f
  .rodata    : { *(.rodata) *(.gnu.linkonce.r*) }
  .rodata1   : { *(.rodata1) }
  . = ALIGN(64 / 8);
  PROVIDE (__preinit_array_start = .);
  .preinit_array     : { *(.preinit_array) }
  PROVIDE (__preinit_array_end = .);
  PROVIDE (__init_array_start = .);
  .init_array     : { *(.init_array) }
  PROVIDE (__init_array_end = .);
  PROVIDE (__fini_array_start = .);
  .fini_array     : { *(.fini_array) }
  PROVIDE (__fini_array_end = .);
  .reginfo : { *(.reginfo) }
  /* Adjust the address for the data segment.  We want to adjust up to
     the same address within the page on the next page up.  */
  . = ALIGN(0x100000) + (. & (0x100000 - 1));
  .data    :
  {
    *(.gen_code)
    *(.data)
    *(.gnu.linkonce.d*)
    CONSTRUCTORS
  }
  .data1   : { *(.data1) }
  .ctors         :
  {
    *(.ctors)
  }
  .dtors         :
  {
    *(.dtors)
  }
  .plt      : { *(.plt) }
  .got           : { *(.got.plt) *(.got) }
  .dynamic       : { *(.dynamic) }
  /* We want the small data sections together, so single-instruction offsets
     can access them all, and initialized data all before uninitialized, so
     we can shorten the on-disk segment size.  */
  .sdata     : { *(.sdata) }
  _edata  =  .;
  PROVIDE (edata = .);
  __bss_start = .;
  .sbss      : { *(.sbss) *(.scommon) }
  .bss       :
  {
   *(.dynbss)
   *(.bss)
   *(COMMON)
  }
  _end = . ;
  PROVIDE (end = .);
  /* Stabs debugging sections.  */
  .stab 0 : { *(.stab) }
  .stabstr 0 : { *(.stabstr) }
  .stab.excl 0 : { *(.stab.excl) }
  .stab.exclstr 0 : { *(.stab.exclstr) }
  .stab.index 0 : { *(.stab.index) }
  .stab.indexstr 0 : { *(.stab.indexstr) }
  .comment 0 : { *(.comment) }
  /* DWARF debug sections.
     Symbols in the DWARF debugging sections are relative to the beginning
     of the section so we begin them at 0.  */
  /* DWARF 1 */
  .debug          0 : { *(.debug) }
  .line           0 : { *(.line) }
  /* GNU DWARF 1 extensions */
  .debug_srcinfo  0 : { *(.debug_srcinfo) }
  .debug_sfnames  0 : { *(.debug_sfnames) }
  /* DWARF 1.1 and DWARF 2 */
  .debug_aranges  0 : { *(.debug_aranges) }
  .debug_pubnames 0 : { *(.debug_pubnames) }
  /* DWARF 2 */
  .debug_info     0 : { *(.debug_info) }
  .debug_abbrev   0 : { *(.debug_abbrev) }
  .debug_line     0 : { *(.debug_line) }
  .debug_frame    0 : { *(.debug_frame) }
  .debug_str      0 : { *(.debug_str) }
  .debug_loc      0 : { *(.debug_loc) }
  .debug_macinfo  0 : { *(.debug_macinfo) }
  /* SGI/MIPS DWARF 2 extensions */
  .debug_weaknames 0 : { *(.debug_weaknames) }
  .debug_funcnames 0 : { *(.debug_funcnames) }
  .debug_typenames 0 : { *(.debug_typenames) }
  .debug_varnames  0 : { *(.debug_varnames) }
  /* These must appear regardless of  .  */
}
