/*
 * Common Option ROM Functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright Novell Inc, 2009
 *   Authors: Alexander Graf <agraf@suse.de>
 */


#define FW_CFG_KERNEL_ADDR      0x07
#define FW_CFG_KERNEL_SIZE      0x08
#define FW_CFG_KERNEL_CMDLINE   0x09
#define FW_CFG_INITRD_ADDR      0x0a
#define FW_CFG_INITRD_SIZE      0x0b
#define FW_CFG_KERNEL_ENTRY     0x10
#define FW_CFG_KERNEL_DATA      0x11
#define FW_CFG_INITRD_DATA      0x12
#define FW_CFG_CMDLINE_ADDR     0x13
#define FW_CFG_CMDLINE_SIZE     0x14
#define FW_CFG_CMDLINE_DATA     0x15
#define FW_CFG_SETUP_ADDR       0x16
#define FW_CFG_SETUP_SIZE       0x17
#define FW_CFG_SETUP_DATA       0x18

#define BIOS_CFG_IOPORT_CFG    0x510
#define BIOS_CFG_IOPORT_DATA   0x511

#define FW_CFG_DMA_CTL_ERROR   0x01
#define FW_CFG_DMA_CTL_READ    0x02
#define FW_CFG_DMA_CTL_SKIP    0x04
#define FW_CFG_DMA_CTL_SELECT  0x08
#define FW_CFG_DMA_CTL_WRITE   0x10

#define FW_CFG_DMA_SIGNATURE 0x51454d5520434647ULL /* "QEMU CFG" */

#define BIOS_CFG_DMA_ADDR_HIGH  0x514
#define BIOS_CFG_DMA_ADDR_LOW   0x518

/* Break the translation block flow so -d cpu shows us values */
#define DEBUG_HERE              \
        jmp         1f;         \
        1:

/*
 * Read a variable from the fw_cfg device.
 * Clobbers: %edx
 * Out: %eax
 */
.macro read_fw VAR
        mov         $\VAR, %ax
        mov         $BIOS_CFG_IOPORT_CFG, %dx
        outw        %ax, (%dx)
        mov         $BIOS_CFG_IOPORT_DATA, %dx
        inb         (%dx), %al
        shl         $8, %eax
        inb         (%dx), %al
        shl         $8, %eax
        inb         (%dx), %al
        shl         $8, %eax
        inb         (%dx), %al
        bswap       %eax
.endm


/*
 * Read data from the fw_cfg device using DMA.
 * Clobbers: %edx, %eax, ADDR, SIZE, memory[%esp-16] to memory[%esp]
 */
.macro read_fw_dma VAR, SIZE, ADDR
        /* Address */
        bswapl      \ADDR
        pushl       \ADDR

        /* We only support 32 bit target addresses */
        xorl        %eax, %eax
        pushl       %eax
        mov         $BIOS_CFG_DMA_ADDR_HIGH, %dx
        outl        %eax, (%dx)

        /* Size */
        bswapl      \SIZE
        pushl       \SIZE

        /* Control */
        movl        $(\VAR << 16) | (FW_CFG_DMA_CTL_READ | FW_CFG_DMA_CTL_SELECT), %eax
        bswapl      %eax
        pushl       %eax

        movl        %esp, %eax /* Address of the struct we generated */
        bswapl      %eax
        mov         $BIOS_CFG_DMA_ADDR_LOW, %dx
        outl        %eax, (%dx) /* Initiate DMA */

1:      mov         (%esp), %eax /* Wait for completion */
        bswapl      %eax
        testl       $~FW_CFG_DMA_CTL_ERROR, %eax
        jnz         1b
        addl        $16, %esp
.endm


/*
 * Read a blob from the fw_cfg device using DMA
 * Requires _ADDR, _SIZE and _DATA values for the parameter.
 *
 * Clobbers: %eax, %edx, %es, %ecx, %edi and adresses %esp-20 to %esp
 */
#ifdef USE_FW_CFG_DMA
#define read_fw_blob_dma(var)                           \
        read_fw         var ## _SIZE;                   \
        mov             %eax, %ecx;                     \
        read_fw         var ## _ADDR;                   \
        mov             %eax, %edi ;                    \
        read_fw_dma     var ## _DATA, %ecx, %edi
#else
#define read_fw_blob_dma(var) read_fw_blob(var)
#endif

#define read_fw_blob_pre(var)                           \
        read_fw         var ## _SIZE;                   \
        mov             %eax, %ecx;                     \
        mov             $var ## _DATA, %ax;             \
        mov             $BIOS_CFG_IOPORT_CFG, %edx;     \
        outw            %ax, (%dx);                     \
        mov             $BIOS_CFG_IOPORT_DATA, %dx;     \
        cld

/*
 * Read a blob from the fw_cfg device.
 * Requires _ADDR, _SIZE and _DATA values for the parameter.
 *
 * Clobbers: %eax, %edx, %es, %ecx, %edi
 */
#define read_fw_blob(var)                               \
        read_fw         var ## _ADDR;                   \
        mov             %eax, %edi;                     \
        read_fw_blob_pre(var);                          \
        /* old as(1) doesn't like this insn so emit the bytes instead: \
        rep insb        (%dx), %es:(%edi);              \
        */                                              \
        .dc.b           0xf3,0x6c

/*
 * Read a blob from the fw_cfg device in forced addr32 mode.
 * Requires _ADDR, _SIZE and _DATA values for the parameter.
 *
 * Clobbers: %eax, %edx, %es, %ecx, %edi
 */
#define read_fw_blob_addr32(var)                        \
        read_fw         var ## _ADDR;                   \
        mov             %eax, %edi;                     \
        read_fw_blob_pre(var);                          \
        /* old as(1) doesn't like this insn so emit the bytes instead: \
        addr32 rep insb (%dx), %es:(%edi);              \
        */                                              \
        .dc.b           0x67,0xf3,0x6c

/*
 * Read a blob from the fw_cfg device in forced addr32 mode, address is in %edi.
 * Requires _SIZE and _DATA values for the parameter.
 *
 * Clobbers: %eax, %edx, %edi, %es, %ecx
 */
#define read_fw_blob_addr32_edi(var)                    \
        read_fw_blob_pre(var);                          \
        /* old as(1) doesn't like this insn so emit the bytes instead: \
        addr32 rep insb (%dx), %es:(%edi);              \
        */                                              \
        .dc.b           0x67,0xf3,0x6c

#define OPTION_ROM_START                                \
    .code16;                                            \
    .text;                                              \
        .global         _start;                         \
    _start:;                                            \
        .short          0xaa55;                         \
        .byte           (_end - _start) / 512;

#define BOOT_ROM_START                                  \
        OPTION_ROM_START                                \
        lret;                                           \
        .org            0x18;                           \
        .short          0;                              \
        .short          _pnph;                          \
    _pnph:                                              \
        .ascii          "$PnP";                         \
        .byte           0x01;                           \
        .byte           ( _pnph_len / 16 );             \
        .short          0x0000;                         \
        .byte           0x00;                           \
        .byte           0x00;                           \
        .long           0x00000000;                     \
        .short          _manufacturer;                  \
        .short          _product;                       \
        .long           0x00000000;                     \
        .short          0x0000;                         \
        .short          0x0000;                         \
        .short          _bev;                           \
        .short          0x0000;                         \
        .short          0x0000;                         \
        .equ            _pnph_len, . - _pnph;           \
    _bev:;                                              \
        /* DS = CS */                                   \
        movw            %cs, %ax;                       \
        movw            %ax, %ds;

#define OPTION_ROM_END                                  \
        .byte           0;                              \
        .align          512, 0;                         \
    _end:

#define BOOT_ROM_END                                    \
    _manufacturer:;                                     \
        .asciz "QEMU";                                  \
    _product:;                                          \
        .asciz BOOT_ROM_PRODUCT;                        \
        OPTION_ROM_END

