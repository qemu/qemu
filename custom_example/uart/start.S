.global _start
_start:
    LDR sp, =0x8000      // Set stack pointer (must be valid RAM)
    BL main              // Branch to main
hang:
    B hang               // Safety loop after main returns
