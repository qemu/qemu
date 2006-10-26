/////////////////////////////////////////////////////////////////////////
// $Id: rombios.c,v 1.174 2006/10/17 16:48:05 vruppert Exp $
/////////////////////////////////////////////////////////////////////////
//
//  Copyright (C) 2002  MandrakeSoft S.A.
//
//    MandrakeSoft S.A.
//    43, rue d'Aboukir
//    75002 Paris - France
//    http://www.linux-mandrake.com/
//    http://www.mandrakesoft.com/
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA

// ROM BIOS for use with Bochs/Plex86/QEMU emulation environment


// ROM BIOS compatability entry points:
// ===================================
// $e05b ; POST Entry Point
// $e2c3 ; NMI Handler Entry Point
// $e3fe ; INT 13h Fixed Disk Services Entry Point
// $e401 ; Fixed Disk Parameter Table
// $e6f2 ; INT 19h Boot Load Service Entry Point
// $e6f5 ; Configuration Data Table
// $e729 ; Baud Rate Generator Table
// $e739 ; INT 14h Serial Communications Service Entry Point
// $e82e ; INT 16h Keyboard Service Entry Point
// $e987 ; INT 09h Keyboard Service Entry Point
// $ec59 ; INT 13h Diskette Service Entry Point
// $ef57 ; INT 0Eh Diskette Hardware ISR Entry Point
// $efc7 ; Diskette Controller Parameter Table
// $efd2 ; INT 17h Printer Service Entry Point
// $f045 ; INT 10 Functions 0-Fh Entry Point
// $f065 ; INT 10h Video Support Service Entry Point
// $f0a4 ; MDA/CGA Video Parameter Table (INT 1Dh)
// $f841 ; INT 12h Memory Size Service Entry Point
// $f84d ; INT 11h Equipment List Service Entry Point
// $f859 ; INT 15h System Services Entry Point
// $fa6e ; Character Font for 320x200 & 640x200 Graphics (lower 128 characters)
// $fe6e ; INT 1Ah Time-of-day Service Entry Point
// $fea5 ; INT 08h System Timer ISR Entry Point
// $fef3 ; Initial Interrupt Vector Offsets Loaded by POST
// $ff53 ; IRET Instruction for Dummy Interrupt Handler
// $ff54 ; INT 05h Print Screen Service Entry Point
// $fff0 ; Power-up Entry Point
// $fff5 ; ASCII Date ROM was built - 8 characters in MM/DD/YY
// $fffe ; System Model ID

// NOTES for ATA/ATAPI driver (cbbochs@free.fr)
//   Features
//     - supports up to 4 ATA interfaces
//     - device/geometry detection
//     - 16bits/32bits device access
//     - pchs/lba access
//     - datain/dataout/packet command support
//
// NOTES for El-Torito Boot (cbbochs@free.fr)
//   - CD-ROM booting is only available if ATA/ATAPI Driver is available
//   - Current code is only able to boot mono-session cds 
//   - Current code can not boot and emulate a hard-disk
//     the bios will panic otherwise
//   - Current code also use memory in EBDA segement. 
//   - I used cmos byte 0x3D to store extended information on boot-device
//   - Code has to be modified modified to handle multiple cdrom drives
//   - Here are the cdrom boot failure codes:
//       1 : no atapi device found
//       2 : no atapi cdrom found
//       3 : can not read cd - BRVD
//       4 : cd is not eltorito (BRVD)
//       5 : cd is not eltorito (ISO TAG)
//       6 : cd is not eltorito (ELTORITO TAG)
//       7 : can not read cd - boot catalog
//       8 : boot catalog : bad header
//       9 : boot catalog : bad platform
//      10 : boot catalog : bad signature
//      11 : boot catalog : bootable flag not set
//      12 : can not read cd - boot image
//
//   ATA driver
//   - EBDA segment. 
//     I used memory starting at 0x121 in the segment
//   - the translation policy is defined in cmos regs 0x39 & 0x3a
//
// TODO :
//
//   int74 
//     - needs to be reworked.  Uses direct [bp] offsets. (?)
//
//   int13:
//     - f04 (verify sectors) isn't complete  (?)
//     - f02/03/04 should set current cyl,etc in BDA  (?)
//     - rewrite int13_relocated & clean up int13 entry code
//
//   NOTES:
//   - NMI access (bit7 of addr written to 70h)
//
//   ATA driver
//   - should handle the "don't detect" bit (cmos regs 0x3b & 0x3c)
//   - could send the multiple-sector read/write commands
//
//   El-Torito
//   - Emulate a Hard-disk (currently only diskette can be emulated) see "FIXME ElTorito Harddisk"
//   - Implement remaining int13_cdemu functions (as defined by El-Torito specs)
//   - cdrom drive is hardcoded to ide 0 device 1 in several places. see "FIXME ElTorito Hardcoded"
//   - int13 Fix DL when emulating a cd. In that case DL is decremented before calling real int13.
//     This is ok. But DL should be reincremented afterwards. 
//   - Fix all "FIXME ElTorito Various"
//   - should be able to boot any cdrom instead of the first one
//
//   BCC Bug: find a generic way to handle the bug of #asm after an "if"  (fixed in 0.16.7)

#include "rombios.h"

#define DEBUG_ATA          0
#define DEBUG_INT13_HD     0
#define DEBUG_INT13_CD     0
#define DEBUG_INT13_ET     0
#define DEBUG_INT13_FL     0
#define DEBUG_INT15        0
#define DEBUG_INT16        0
#define DEBUG_INT1A        0
#define DEBUG_INT74        0
#define DEBUG_APM          0

#define BX_CPU           3
#define BX_USE_PS2_MOUSE 1
#define BX_CALL_INT15_4F 1
#define BX_USE_EBDA      1
#define BX_SUPPORT_FLOPPY 1
#define BX_FLOPPY_ON_CNT 37   /* 2 seconds */
#define BX_PCIBIOS       1
#define BX_APM           1

#define BX_USE_ATADRV    1
#define BX_ELTORITO_BOOT 1

#define BX_MAX_ATA_INTERFACES   4
#define BX_MAX_ATA_DEVICES      (BX_MAX_ATA_INTERFACES*2)

#define BX_VIRTUAL_PORTS 1 /* normal output to Bochs ports */
#define BX_DEBUG_SERIAL  0 /* output to COM1 */

   /* model byte 0xFC = AT */
#define SYS_MODEL_ID     0xFC
#define SYS_SUBMODEL_ID  0x00
#define BIOS_REVISION    1
#define BIOS_CONFIG_TABLE 0xe6f5

#ifndef BIOS_BUILD_DATE
#  define BIOS_BUILD_DATE "06/23/99"
#endif

  // 1K of base memory used for Extended Bios Data Area (EBDA)
  // EBDA is used for PS/2 mouse support, and IDE BIOS, etc.
#define EBDA_SEG           0x9FC0
#define EBDA_SIZE          1              // In KiB
#define BASE_MEM_IN_K   (640 - EBDA_SIZE)

  // Define the application NAME
#if defined(BX_QEMU)
#  define BX_APPNAME "QEMU"
#elif defined(PLEX86)
#  define BX_APPNAME "Plex86"
#else
#  define BX_APPNAME "Bochs"
#endif

  // Sanity Checks
#if BX_USE_ATADRV && BX_CPU<3
#    error The ATA/ATAPI Driver can only to be used with a 386+ cpu
#endif
#if BX_USE_ATADRV && !BX_USE_EBDA
#    error ATA/ATAPI Driver can only be used if EBDA is available
#endif
#if BX_ELTORITO_BOOT && !BX_USE_ATADRV
#    error El-Torito Boot can only be use if ATA/ATAPI Driver is available
#endif
#if BX_PCIBIOS && BX_CPU<3
#    error PCI BIOS can only be used with 386+ cpu
#endif
#if BX_APM && BX_CPU<3
#    error APM BIOS can only be used with 386+ cpu
#endif

// define this if you want to make PCIBIOS working on a specific bridge only
// undef enables PCIBIOS when at least one PCI device is found
// i440FX is emulated by Bochs and QEMU
#define PCI_FIXED_HOST_BRIDGE 0x12378086 ;; i440FX PCI bridge

// #20  is dec 20
// #$20 is hex 20 = 32
// #0x20 is hex 20 = 32
// LDA  #$20
// JSR  $E820
// LDD  .i,S
// JSR  $C682
// mov al, #$20

// all hex literals should be prefixed with '0x'
//   grep "#[0-9a-fA-F][0-9a-fA-F]" rombios.c
// no mov SEG-REG, #value, must mov register into seg-reg
//   grep -i "mov[ ]*.s" rombios.c

// This is for compiling with gcc2 and gcc3
#define ASM_START #asm
#define ASM_END #endasm

ASM_START
.rom

.org 0x0000

#if BX_CPU >= 3
use16 386
#else
use16 286
#endif

MACRO HALT
  ;; the HALT macro is called with the line number of the HALT call.
  ;; The line number is then sent to the PANIC_PORT, causing Bochs/Plex 
  ;; to print a BX_PANIC message.  This will normally halt the simulation
  ;; with a message such as "BIOS panic at rombios.c, line 4091".
  ;; However, users can choose to make panics non-fatal and continue.
#if BX_VIRTUAL_PORTS
  mov dx,#PANIC_PORT
  mov ax,#?1
  out dx,ax
#else
  mov dx,#0x80
  mov ax,#?1
  out dx,al
#endif
MEND

MACRO JMP_AP
  db 0xea
  dw ?2
  dw ?1
MEND

MACRO SET_INT_VECTOR
  mov ax, ?3
  mov ?1*4, ax
  mov ax, ?2
  mov ?1*4+2, ax
MEND

ASM_END

typedef unsigned char  Bit8u;
typedef unsigned short Bit16u;
typedef unsigned short bx_bool;
typedef unsigned long  Bit32u;

#if BX_USE_ATADRV

  void memsetb(seg,offset,value,count);
  void memcpyb(dseg,doffset,sseg,soffset,count);
  void memcpyd(dseg,doffset,sseg,soffset,count);
  
  // memset of count bytes
    void 
  memsetb(seg,offset,value,count)
    Bit16u seg;
    Bit16u offset;
    Bit16u value;
    Bit16u count;
  {
  ASM_START
    push bp
    mov  bp, sp
  
      push ax
      push cx
      push es
      push di
  
      mov  cx, 10[bp] ; count
      cmp  cx, #0x00
      je   memsetb_end
      mov  ax, 4[bp] ; segment
      mov  es, ax
      mov  ax, 6[bp] ; offset
      mov  di, ax
      mov  al, 8[bp] ; value
      cld
      rep
       stosb
  
  memsetb_end:
      pop di
      pop es
      pop cx
      pop ax
  
    pop bp
  ASM_END
  }
  
#if 0 
  // memcpy of count bytes
    void 
  memcpyb(dseg,doffset,sseg,soffset,count)
    Bit16u dseg;
    Bit16u doffset;
    Bit16u sseg;
    Bit16u soffset;
    Bit16u count;
  {
  ASM_START
    push bp
    mov  bp, sp
  
      push ax
      push cx
      push es
      push di
      push ds
      push si
  
      mov  cx, 12[bp] ; count
      cmp  cx, #0x0000
      je   memcpyb_end
      mov  ax, 4[bp] ; dsegment
      mov  es, ax
      mov  ax, 6[bp] ; doffset
      mov  di, ax
      mov  ax, 8[bp] ; ssegment
      mov  ds, ax
      mov  ax, 10[bp] ; soffset
      mov  si, ax
      cld
      rep
       movsb
  
  memcpyb_end:
      pop si
      pop ds
      pop di
      pop es
      pop cx
      pop ax
  
    pop bp
  ASM_END
  }

  // memcpy of count dword
    void 
  memcpyd(dseg,doffset,sseg,soffset,count)
    Bit16u dseg;
    Bit16u doffset;
    Bit16u sseg;
    Bit16u soffset;
    Bit16u count;
  {
  ASM_START
    push bp
    mov  bp, sp
  
      push ax
      push cx
      push es
      push di
      push ds
      push si
  
      mov  cx, 12[bp] ; count
      cmp  cx, #0x0000
      je   memcpyd_end
      mov  ax, 4[bp] ; dsegment
      mov  es, ax
      mov  ax, 6[bp] ; doffset
      mov  di, ax
      mov  ax, 8[bp] ; ssegment
      mov  ds, ax
      mov  ax, 10[bp] ; soffset
      mov  si, ax
      cld
      rep
       movsd
  
  memcpyd_end:
      pop si
      pop ds
      pop di
      pop es
      pop cx
      pop ax
  
    pop bp
  ASM_END
  }
#endif
#endif //BX_USE_ATADRV

  // read_dword and write_dword functions
  static Bit32u         read_dword();
  static void           write_dword();
  
    Bit32u
  read_dword(seg, offset)
    Bit16u seg;
    Bit16u offset;
  {
  ASM_START
    push bp
    mov  bp, sp
  
      push bx
      push ds
      mov  ax, 4[bp] ; segment
      mov  ds, ax
      mov  bx, 6[bp] ; offset
      mov  ax, [bx]
      inc  bx
      inc  bx
      mov  dx, [bx]
      ;; ax = return value (word)
      ;; dx = return value (word)
      pop  ds
      pop  bx
  
    pop  bp
  ASM_END
  }
  
    void
  write_dword(seg, offset, data)
    Bit16u seg;
    Bit16u offset;
    Bit32u data;
  {
  ASM_START
    push bp
    mov  bp, sp
  
      push ax
      push bx
      push ds
      mov  ax, 4[bp] ; segment
      mov  ds, ax
      mov  bx, 6[bp] ; offset
      mov  ax, 8[bp] ; data word
      mov  [bx], ax  ; write data word
      inc  bx
      inc  bx
      mov  ax, 10[bp] ; data word
      mov  [bx], ax  ; write data word
      pop  ds
      pop  bx
      pop  ax
  
    pop  bp
  ASM_END
  }
  
  // Bit32u (unsigned long) and long helper functions
  ASM_START
  
  ;; and function
  landl:
  landul:
    SEG SS 
      and ax,[di]
    SEG SS 
      and bx,2[di]
    ret
  
  ;; add function
  laddl:
  laddul:
    SEG SS 
      add ax,[di]
    SEG SS 
      adc bx,2[di]
    ret
  
  ;; cmp function
  lcmpl:
  lcmpul:
    and eax, #0x0000FFFF
    shl ebx, #16
    add eax, ebx
    shr ebx, #16
    SEG SS
      cmp eax, dword ptr [di]
    ret
  
  ;; sub function
  lsubl:
  lsubul:
    SEG SS
    sub ax,[di]
    SEG SS
    sbb bx,2[di]
    ret
  
  ;; mul function
  lmull:
  lmulul:
    and eax, #0x0000FFFF
    shl ebx, #16
    add eax, ebx
    SEG SS
    mul eax, dword ptr [di]
    mov ebx, eax
    shr ebx, #16
    ret
  
  ;; dec function
  ldecl:
  ldecul:
    SEG SS
    dec dword ptr [bx]
    ret
  
  ;; or function
  lorl:
  lorul:
    SEG SS
    or  ax,[di]
    SEG SS
    or  bx,2[di]
    ret
  
  ;; inc function
  lincl:
  lincul:
    SEG SS
    inc dword ptr [bx]
    ret
  
  ;; tst function
  ltstl:
  ltstul:
    and eax, #0x0000FFFF
    shl ebx, #16
    add eax, ebx
    shr ebx, #16
    test eax, eax
    ret
  
  ;; sr function
  lsrul:
    mov  cx,di
    jcxz lsr_exit
    and  eax, #0x0000FFFF
    shl  ebx, #16
    add  eax, ebx
  lsr_loop:
    shr  eax, #1
    loop lsr_loop
    mov  ebx, eax
    shr  ebx, #16
  lsr_exit:
    ret
  
  ;; sl function
  lsll:
  lslul:
    mov  cx,di
    jcxz lsl_exit
    and  eax, #0x0000FFFF
    shl  ebx, #16
    add  eax, ebx
  lsl_loop: 
    shl  eax, #1
    loop lsl_loop
    mov  ebx, eax
    shr  ebx, #16
  lsl_exit:
    ret
  
  idiv_:
    cwd
    idiv bx
    ret

  idiv_u:
    xor dx,dx
    div bx
    ret

  ldivul:
    and  eax, #0x0000FFFF
    shl  ebx, #16
    add  eax, ebx
    xor  edx, edx
    SEG SS
    mov  bx,  2[di]
    shl  ebx, #16
    SEG SS
    mov  bx,  [di]
    div  ebx
    mov  ebx, eax
    shr  ebx, #16
    ret

  ASM_END

// for access to RAM area which is used by interrupt vectors
// and BIOS Data Area

typedef struct {
  unsigned char filler1[0x400];
  unsigned char filler2[0x6c];
  Bit16u ticks_low;
  Bit16u ticks_high;
  Bit8u  midnight_flag;
  } bios_data_t;

#define BiosData ((bios_data_t  *) 0)

#if BX_USE_ATADRV
  typedef struct {
    Bit16u heads;      // # heads
    Bit16u cylinders;  // # cylinders
    Bit16u spt;        // # sectors / track
    } chs_t;

  // DPTE definition
  typedef struct {
    Bit16u iobase1;
    Bit16u iobase2;
    Bit8u  prefix;
    Bit8u  unused;
    Bit8u  irq;
    Bit8u  blkcount;
    Bit8u  dma;
    Bit8u  pio;
    Bit16u options;
    Bit16u reserved;
    Bit8u  revision;
    Bit8u  checksum;
    } dpte_t;
 
  typedef struct {
    Bit8u  iface;        // ISA or PCI
    Bit16u iobase1;      // IO Base 1
    Bit16u iobase2;      // IO Base 2
    Bit8u  irq;          // IRQ
    } ata_channel_t;

  typedef struct {
    Bit8u  type;         // Detected type of ata (ata/atapi/none/unknown)
    Bit8u  device;       // Detected type of attached devices (hd/cd/none)
    Bit8u  removable;    // Removable device flag
    Bit8u  lock;         // Locks for removable devices
    // Bit8u  lba_capable;  // LBA capable flag - always yes for bochs devices
    Bit8u  mode;         // transfert mode : PIO 16/32 bits - IRQ - ISADMA - PCIDMA
    Bit16u blksize;      // block size

    Bit8u  translation;  // type of translation
    chs_t  lchs;         // Logical CHS
    chs_t  pchs;         // Physical CHS

    Bit32u sectors;      // Total sectors count
    } ata_device_t;

  typedef struct {
    // ATA channels info
    ata_channel_t channels[BX_MAX_ATA_INTERFACES];

    // ATA devices info
    ata_device_t  devices[BX_MAX_ATA_DEVICES];
    //
    // map between (bios hd id - 0x80) and ata channels
    Bit8u  hdcount, hdidmap[BX_MAX_ATA_DEVICES];                

    // map between (bios cd id - 0xE0) and ata channels
    Bit8u  cdcount, cdidmap[BX_MAX_ATA_DEVICES];                

    // Buffer for DPTE table
    dpte_t dpte;

    // Count of transferred sectors and bytes
    Bit16u trsfsectors;
    Bit32u trsfbytes;

    } ata_t;
  
#if BX_ELTORITO_BOOT
  // ElTorito Device Emulation data 
  typedef struct {
    Bit8u  active;
    Bit8u  media;
    Bit8u  emulated_drive;
    Bit8u  controller_index;
    Bit16u device_spec;
    Bit32u ilba;
    Bit16u buffer_segment;
    Bit16u load_segment;
    Bit16u sector_count;
    
    // Virtual device
    chs_t  vdevice;
    } cdemu_t;
#endif // BX_ELTORITO_BOOT
  
  // for access to EBDA area
  //     The EBDA structure should conform to 
  //     http://www.frontiernet.net/~fys/rombios.htm document
  //     I made the ata and cdemu structs begin at 0x121 in the EBDA seg
  typedef struct {
    unsigned char filler1[0x3D];

    // FDPT - Can be splitted in data members if needed
    unsigned char fdpt0[0x10];
    unsigned char fdpt1[0x10];

    unsigned char filler2[0xC4];

    // ATA Driver data
    ata_t   ata;

#if BX_ELTORITO_BOOT
    // El Torito Emulation data
    cdemu_t cdemu;
#endif // BX_ELTORITO_BOOT

    } ebda_data_t;
  
  #define EbdaData ((ebda_data_t *) 0)

  // for access to the int13ext structure
  typedef struct {
    Bit8u  size;
    Bit8u  reserved;
    Bit16u count;
    Bit16u offset;
    Bit16u segment;
    Bit32u lba1;
    Bit32u lba2;
    } int13ext_t;
 
  #define Int13Ext ((int13ext_t *) 0)

  // Disk Physical Table definition
  typedef struct {
    Bit16u  size;
    Bit16u  infos;
    Bit32u  cylinders;
    Bit32u  heads;
    Bit32u  spt;
    Bit32u  sector_count1;
    Bit32u  sector_count2;
    Bit16u  blksize;
    Bit16u  dpte_segment;
    Bit16u  dpte_offset;
    Bit16u  key;
    Bit8u   dpi_length;
    Bit8u   reserved1;
    Bit16u  reserved2;
    Bit8u   host_bus[4];
    Bit8u   iface_type[8];
    Bit8u   iface_path[8];
    Bit8u   device_path[8];
    Bit8u   reserved3;
    Bit8u   checksum;
    } dpt_t;
 
  #define Int13DPT ((dpt_t *) 0)

#endif // BX_USE_ATADRV

typedef struct {
  union {
    struct {
      Bit16u di, si, bp, sp;
      Bit16u bx, dx, cx, ax;
      } r16;
    struct {
      Bit16u filler[4];
      Bit8u  bl, bh, dl, dh, cl, ch, al, ah;
      } r8;
    } u;
  } pusha_regs_t;

typedef struct {
 union {
  struct {
    Bit32u edi, esi, ebp, esp;
    Bit32u ebx, edx, ecx, eax;
    } r32;
  struct {
    Bit16u di, filler1, si, filler2, bp, filler3, sp, filler4;
    Bit16u bx, filler5, dx, filler6, cx, filler7, ax, filler8;
    } r16;
  struct {
    Bit32u filler[4];
    Bit8u  bl, bh; 
    Bit16u filler1;
    Bit8u  dl, dh; 
    Bit16u filler2;
    Bit8u  cl, ch;
    Bit16u filler3;
    Bit8u  al, ah;
    Bit16u filler4;
    } r8;
  } u;
} pushad_regs_t;

typedef struct {
  union {
    struct {
      Bit16u flags;
      } r16;
    struct {
      Bit8u  flagsl;
      Bit8u  flagsh;
      } r8;
    } u;
  } flags_t;

#define SetCF(x)   x.u.r8.flagsl |= 0x01
#define SetZF(x)   x.u.r8.flagsl |= 0x40
#define ClearCF(x) x.u.r8.flagsl &= 0xfe
#define ClearZF(x) x.u.r8.flagsl &= 0xbf
#define GetCF(x)   (x.u.r8.flagsl & 0x01)

typedef struct {
  Bit16u ip;
  Bit16u cs;
  flags_t flags;
  } iret_addr_t;



static Bit8u          inb();
static Bit8u          inb_cmos();
static void           outb();
static void           outb_cmos();
static Bit16u         inw();
static void           outw();
static void           init_rtc();
static bx_bool        rtc_updating();

static Bit8u          read_byte();
static Bit16u         read_word();
static void           write_byte();
static void           write_word();
static void           bios_printf();

static Bit8u          inhibit_mouse_int_and_events();
static void           enable_mouse_int_and_events();
static Bit8u          send_to_mouse_ctrl();
static Bit8u          get_mouse_data();
static void           set_kbd_command_byte();

static void           int09_function();
static void           int13_harddisk();
static void           int13_cdrom();
static void           int13_cdemu();
static void           int13_eltorito();
static void           int13_diskette_function();
static void           int14_function();
static void           int15_function();
static void           int16_function();
static void           int17_function();
static Bit32u         int19_function();
static void           int1a_function();
static void           int70_function();
static void           int74_function();
static Bit16u         get_CS();
static Bit16u         get_SS();
static unsigned int   enqueue_key();
static unsigned int   dequeue_key();
static void           get_hd_geometry();
static void           set_diskette_ret_status();
static void           set_diskette_current_cyl();
static void           determine_floppy_media();
static bx_bool        floppy_drive_exists();
static bx_bool        floppy_drive_recal();
static bx_bool        floppy_media_known();
static bx_bool        floppy_media_sense();
static bx_bool        set_enable_a20();
static void           debugger_on();
static void           debugger_off();
static void           keyboard_init();
static void           keyboard_panic();
static void           shutdown_status_panic();
static void           nmi_handler_msg();

static void           print_bios_banner();
static void           print_boot_device();
static void           print_boot_failure();
static void           print_cdromboot_failure();

# if BX_USE_ATADRV

// ATA / ATAPI driver
void   ata_init();
void   ata_detect();
void   ata_reset();

Bit16u ata_cmd_non_data();
Bit16u ata_cmd_data_in();
Bit16u ata_cmd_data_out();
Bit16u ata_cmd_packet();

Bit16u atapi_get_sense();
Bit16u atapi_is_ready();
Bit16u atapi_is_cdrom();

#endif // BX_USE_ATADRV

#if BX_ELTORITO_BOOT

void   cdemu_init();
Bit8u  cdemu_isactive();
Bit8u  cdemu_emulated_drive();

Bit16u cdrom_boot();

#endif // BX_ELTORITO_BOOT

static char bios_cvs_version_string[] = "$Revision: 1.174 $ $Date: 2006/10/17 16:48:05 $";

#define BIOS_COPYRIGHT_STRING "(c) 2002 MandrakeSoft S.A. Written by Kevin Lawton & the Bochs team."

#if DEBUG_ATA
#  define BX_DEBUG_ATA(a...) BX_DEBUG(a)
#else
#  define BX_DEBUG_ATA(a...)
#endif
#if DEBUG_INT13_HD
#  define BX_DEBUG_INT13_HD(a...) BX_DEBUG(a)
#else
#  define BX_DEBUG_INT13_HD(a...)
#endif
#if DEBUG_INT13_CD
#  define BX_DEBUG_INT13_CD(a...) BX_DEBUG(a)
#else
#  define BX_DEBUG_INT13_CD(a...)
#endif
#if DEBUG_INT13_ET
#  define BX_DEBUG_INT13_ET(a...) BX_DEBUG(a)
#else
#  define BX_DEBUG_INT13_ET(a...)
#endif
#if DEBUG_INT13_FL
#  define BX_DEBUG_INT13_FL(a...) BX_DEBUG(a)
#else
#  define BX_DEBUG_INT13_FL(a...)
#endif
#if DEBUG_INT15
#  define BX_DEBUG_INT15(a...) BX_DEBUG(a)
#else
#  define BX_DEBUG_INT15(a...)
#endif
#if DEBUG_INT16
#  define BX_DEBUG_INT16(a...) BX_DEBUG(a)
#else
#  define BX_DEBUG_INT16(a...)
#endif
#if DEBUG_INT1A
#  define BX_DEBUG_INT1A(a...) BX_DEBUG(a)
#else
#  define BX_DEBUG_INT1A(a...)
#endif
#if DEBUG_INT74
#  define BX_DEBUG_INT74(a...) BX_DEBUG(a)
#else
#  define BX_DEBUG_INT74(a...)
#endif

#define SET_AL(val8) AX = ((AX & 0xff00) | (val8))
#define SET_BL(val8) BX = ((BX & 0xff00) | (val8))
#define SET_CL(val8) CX = ((CX & 0xff00) | (val8))
#define SET_DL(val8) DX = ((DX & 0xff00) | (val8))
#define SET_AH(val8) AX = ((AX & 0x00ff) | ((val8) << 8))
#define SET_BH(val8) BX = ((BX & 0x00ff) | ((val8) << 8))
#define SET_CH(val8) CX = ((CX & 0x00ff) | ((val8) << 8))
#define SET_DH(val8) DX = ((DX & 0x00ff) | ((val8) << 8))

#define GET_AL() ( AX & 0x00ff )
#define GET_BL() ( BX & 0x00ff )
#define GET_CL() ( CX & 0x00ff )
#define GET_DL() ( DX & 0x00ff )
#define GET_AH() ( AX >> 8 )
#define GET_BH() ( BX >> 8 )
#define GET_CH() ( CX >> 8 )
#define GET_DH() ( DX >> 8 )

#define GET_ELDL() ( ELDX & 0x00ff )
#define GET_ELDH() ( ELDX >> 8 )

#define SET_CF()     FLAGS |= 0x0001
#define CLEAR_CF()   FLAGS &= 0xfffe
#define GET_CF()     (FLAGS & 0x0001)

#define SET_ZF()     FLAGS |= 0x0040
#define CLEAR_ZF()   FLAGS &= 0xffbf
#define GET_ZF()     (FLAGS & 0x0040)

#define UNSUPPORTED_FUNCTION 0x86

#define none 0
#define MAX_SCAN_CODE 0x58

static struct {
  Bit16u normal;
  Bit16u shift;
  Bit16u control;
  Bit16u alt;
  Bit8u lock_flags;
  } scan_to_scanascii[MAX_SCAN_CODE + 1] = {
      {   none,   none,   none,   none, none },
      { 0x011b, 0x011b, 0x011b, 0x0100, none }, /* escape */
      { 0x0231, 0x0221,   none, 0x7800, none }, /* 1! */
      { 0x0332, 0x0340, 0x0300, 0x7900, none }, /* 2@ */
      { 0x0433, 0x0423,   none, 0x7a00, none }, /* 3# */
      { 0x0534, 0x0524,   none, 0x7b00, none }, /* 4$ */
      { 0x0635, 0x0625,   none, 0x7c00, none }, /* 5% */
      { 0x0736, 0x075e, 0x071e, 0x7d00, none }, /* 6^ */
      { 0x0837, 0x0826,   none, 0x7e00, none }, /* 7& */
      { 0x0938, 0x092a,   none, 0x7f00, none }, /* 8* */
      { 0x0a39, 0x0a28,   none, 0x8000, none }, /* 9( */
      { 0x0b30, 0x0b29,   none, 0x8100, none }, /* 0) */
      { 0x0c2d, 0x0c5f, 0x0c1f, 0x8200, none }, /* -_ */
      { 0x0d3d, 0x0d2b,   none, 0x8300, none }, /* =+ */
      { 0x0e08, 0x0e08, 0x0e7f,   none, none }, /* backspace */
      { 0x0f09, 0x0f00,   none,   none, none }, /* tab */
      { 0x1071, 0x1051, 0x1011, 0x1000, 0x40 }, /* Q */
      { 0x1177, 0x1157, 0x1117, 0x1100, 0x40 }, /* W */
      { 0x1265, 0x1245, 0x1205, 0x1200, 0x40 }, /* E */
      { 0x1372, 0x1352, 0x1312, 0x1300, 0x40 }, /* R */
      { 0x1474, 0x1454, 0x1414, 0x1400, 0x40 }, /* T */
      { 0x1579, 0x1559, 0x1519, 0x1500, 0x40 }, /* Y */
      { 0x1675, 0x1655, 0x1615, 0x1600, 0x40 }, /* U */
      { 0x1769, 0x1749, 0x1709, 0x1700, 0x40 }, /* I */
      { 0x186f, 0x184f, 0x180f, 0x1800, 0x40 }, /* O */
      { 0x1970, 0x1950, 0x1910, 0x1900, 0x40 }, /* P */
      { 0x1a5b, 0x1a7b, 0x1a1b,   none, none }, /* [{ */
      { 0x1b5d, 0x1b7d, 0x1b1d,   none, none }, /* ]} */
      { 0x1c0d, 0x1c0d, 0x1c0a,   none, none }, /* Enter */
      {   none,   none,   none,   none, none }, /* L Ctrl */
      { 0x1e61, 0x1e41, 0x1e01, 0x1e00, 0x40 }, /* A */
      { 0x1f73, 0x1f53, 0x1f13, 0x1f00, 0x40 }, /* S */
      { 0x2064, 0x2044, 0x2004, 0x2000, 0x40 }, /* D */
      { 0x2166, 0x2146, 0x2106, 0x2100, 0x40 }, /* F */
      { 0x2267, 0x2247, 0x2207, 0x2200, 0x40 }, /* G */
      { 0x2368, 0x2348, 0x2308, 0x2300, 0x40 }, /* H */
      { 0x246a, 0x244a, 0x240a, 0x2400, 0x40 }, /* J */
      { 0x256b, 0x254b, 0x250b, 0x2500, 0x40 }, /* K */
      { 0x266c, 0x264c, 0x260c, 0x2600, 0x40 }, /* L */
      { 0x273b, 0x273a,   none,   none, none }, /* ;: */
      { 0x2827, 0x2822,   none,   none, none }, /* '" */
      { 0x2960, 0x297e,   none,   none, none }, /* `~ */
      {   none,   none,   none,   none, none }, /* L shift */
      { 0x2b5c, 0x2b7c, 0x2b1c,   none, none }, /* |\ */
      { 0x2c7a, 0x2c5a, 0x2c1a, 0x2c00, 0x40 }, /* Z */
      { 0x2d78, 0x2d58, 0x2d18, 0x2d00, 0x40 }, /* X */
      { 0x2e63, 0x2e43, 0x2e03, 0x2e00, 0x40 }, /* C */
      { 0x2f76, 0x2f56, 0x2f16, 0x2f00, 0x40 }, /* V */
      { 0x3062, 0x3042, 0x3002, 0x3000, 0x40 }, /* B */
      { 0x316e, 0x314e, 0x310e, 0x3100, 0x40 }, /* N */
      { 0x326d, 0x324d, 0x320d, 0x3200, 0x40 }, /* M */
      { 0x332c, 0x333c,   none,   none, none }, /* ,< */
      { 0x342e, 0x343e,   none,   none, none }, /* .> */
      { 0x352f, 0x353f,   none,   none, none }, /* /? */
      {   none,   none,   none,   none, none }, /* R Shift */
      { 0x372a, 0x372a,   none,   none, none }, /* * */
      {   none,   none,   none,   none, none }, /* L Alt */
      { 0x3920, 0x3920, 0x3920, 0x3920, none }, /* space */
      {   none,   none,   none,   none, none }, /* caps lock */
      { 0x3b00, 0x5400, 0x5e00, 0x6800, none }, /* F1 */
      { 0x3c00, 0x5500, 0x5f00, 0x6900, none }, /* F2 */
      { 0x3d00, 0x5600, 0x6000, 0x6a00, none }, /* F3 */
      { 0x3e00, 0x5700, 0x6100, 0x6b00, none }, /* F4 */
      { 0x3f00, 0x5800, 0x6200, 0x6c00, none }, /* F5 */
      { 0x4000, 0x5900, 0x6300, 0x6d00, none }, /* F6 */
      { 0x4100, 0x5a00, 0x6400, 0x6e00, none }, /* F7 */
      { 0x4200, 0x5b00, 0x6500, 0x6f00, none }, /* F8 */
      { 0x4300, 0x5c00, 0x6600, 0x7000, none }, /* F9 */
      { 0x4400, 0x5d00, 0x6700, 0x7100, none }, /* F10 */
      {   none,   none,   none,   none, none }, /* Num Lock */
      {   none,   none,   none,   none, none }, /* Scroll Lock */
      { 0x4700, 0x4737, 0x7700,   none, 0x20 }, /* 7 Home */
      { 0x4800, 0x4838,   none,   none, 0x20 }, /* 8 UP */
      { 0x4900, 0x4939, 0x8400,   none, 0x20 }, /* 9 PgUp */
      { 0x4a2d, 0x4a2d,   none,   none, none }, /* - */
      { 0x4b00, 0x4b34, 0x7300,   none, 0x20 }, /* 4 Left */
      { 0x4c00, 0x4c35,   none,   none, 0x20 }, /* 5 */
      { 0x4d00, 0x4d36, 0x7400,   none, 0x20 }, /* 6 Right */
      { 0x4e2b, 0x4e2b,   none,   none, none }, /* + */
      { 0x4f00, 0x4f31, 0x7500,   none, 0x20 }, /* 1 End */
      { 0x5000, 0x5032,   none,   none, 0x20 }, /* 2 Down */
      { 0x5100, 0x5133, 0x7600,   none, 0x20 }, /* 3 PgDn */
      { 0x5200, 0x5230,   none,   none, 0x20 }, /* 0 Ins */
      { 0x5300, 0x532e,   none,   none, 0x20 }, /* Del */
      {   none,   none,   none,   none, none },
      {   none,   none,   none,   none, none },
      { 0x565c, 0x567c,   none,   none, none }, /* \| */    
      { 0x5700, 0x5700,   none,   none, none }, /* F11 */
      { 0x5800, 0x5800,   none,   none, none }  /* F12 */
      };

  Bit8u
inb(port)
  Bit16u port;
{
ASM_START
  push bp
  mov  bp, sp

    push dx
    mov  dx, 4[bp]
    in   al, dx
    pop  dx

  pop  bp
ASM_END
}

#if BX_USE_ATADRV
  Bit16u
inw(port)
  Bit16u port;
{
ASM_START
  push bp
  mov  bp, sp

    push dx
    mov  dx, 4[bp]
    in   ax, dx
    pop  dx

  pop  bp
ASM_END
}
#endif

  void
outb(port, val)
  Bit16u port;
  Bit8u  val;
{
ASM_START
  push bp
  mov  bp, sp

    push ax
    push dx
    mov  dx, 4[bp]
    mov  al, 6[bp]
    out  dx, al
    pop  dx
    pop  ax

  pop  bp
ASM_END
}

#if BX_USE_ATADRV
  void
outw(port, val)
  Bit16u port;
  Bit16u  val;
{
ASM_START
  push bp
  mov  bp, sp

    push ax
    push dx
    mov  dx, 4[bp]
    mov  ax, 6[bp]
    out  dx, ax
    pop  dx
    pop  ax

  pop  bp
ASM_END
}
#endif

  void
outb_cmos(cmos_reg, val)
  Bit8u cmos_reg;
  Bit8u val;
{
ASM_START
  push bp
  mov  bp, sp

    mov  al, 4[bp] ;; cmos_reg
    out  0x70, al
    mov  al, 6[bp] ;; val
    out  0x71, al

  pop  bp
ASM_END
}

  Bit8u
inb_cmos(cmos_reg)
  Bit8u cmos_reg;
{
ASM_START
  push bp
  mov  bp, sp

    mov  al, 4[bp] ;; cmos_reg
    out 0x70, al
    in  al, 0x71

  pop  bp
ASM_END
}

  void
init_rtc()
{
  outb_cmos(0x0a, 0x26);
  outb_cmos(0x0b, 0x02);
  inb_cmos(0x0c);
  inb_cmos(0x0d);
}

  bx_bool
rtc_updating()
{
  // This function checks to see if the update-in-progress bit
  // is set in CMOS Status Register A.  If not, it returns 0.
  // If it is set, it tries to wait until there is a transition
  // to 0, and will return 0 if such a transition occurs.  A 1
  // is returned only after timing out.  The maximum period
  // that this bit should be set is constrained to 244useconds.
  // The count I use below guarantees coverage or more than
  // this time, with any reasonable IPS setting.

  Bit16u count;

  count = 25000;
  while (--count != 0) {
    if ( (inb_cmos(0x0a) & 0x80) == 0 )
      return(0);
    }
  return(1); // update-in-progress never transitioned to 0
}


  Bit8u
read_byte(seg, offset)
  Bit16u seg;
  Bit16u offset;
{
ASM_START
  push bp
  mov  bp, sp

    push bx
    push ds
    mov  ax, 4[bp] ; segment
    mov  ds, ax
    mov  bx, 6[bp] ; offset
    mov  al, [bx]
    ;; al = return value (byte)
    pop  ds
    pop  bx

  pop  bp
ASM_END
}

  Bit16u
read_word(seg, offset)
  Bit16u seg;
  Bit16u offset;
{
ASM_START
  push bp
  mov  bp, sp

    push bx
    push ds
    mov  ax, 4[bp] ; segment
    mov  ds, ax
    mov  bx, 6[bp] ; offset
    mov  ax, [bx]
    ;; ax = return value (word)
    pop  ds
    pop  bx

  pop  bp
ASM_END
}

  void
write_byte(seg, offset, data)
  Bit16u seg;
  Bit16u offset;
  Bit8u data;
{
ASM_START
  push bp
  mov  bp, sp

    push ax
    push bx
    push ds
    mov  ax, 4[bp] ; segment
    mov  ds, ax
    mov  bx, 6[bp] ; offset
    mov  al, 8[bp] ; data byte
    mov  [bx], al  ; write data byte
    pop  ds
    pop  bx
    pop  ax

  pop  bp
ASM_END
}

  void
write_word(seg, offset, data)
  Bit16u seg;
  Bit16u offset;
  Bit16u data;
{
ASM_START
  push bp
  mov  bp, sp

    push ax
    push bx
    push ds
    mov  ax, 4[bp] ; segment
    mov  ds, ax
    mov  bx, 6[bp] ; offset
    mov  ax, 8[bp] ; data word
    mov  [bx], ax  ; write data word
    pop  ds
    pop  bx
    pop  ax

  pop  bp
ASM_END
}

  Bit16u
get_CS()
{
ASM_START
  mov  ax, cs
ASM_END
}

  Bit16u
get_SS()
{
ASM_START
  mov  ax, ss
ASM_END
}

#if BX_DEBUG_SERIAL
/* serial debug port*/
#define BX_DEBUG_PORT 0x03f8

/* data */
#define UART_RBR 0x00
#define UART_THR 0x00

/* control */
#define UART_IER 0x01
#define UART_IIR 0x02
#define UART_FCR 0x02
#define UART_LCR 0x03
#define UART_MCR 0x04
#define UART_DLL 0x00
#define UART_DLM 0x01

/* status */
#define UART_LSR 0x05
#define UART_MSR 0x06
#define UART_SCR 0x07

int uart_can_tx_byte(base_port)
    Bit16u base_port;
{
    return inb(base_port + UART_LSR) & 0x20;
}

void uart_wait_to_tx_byte(base_port)
    Bit16u base_port;
{
    while (!uart_can_tx_byte(base_port));
}

void uart_wait_until_sent(base_port)
    Bit16u base_port;
{
    while (!(inb(base_port + UART_LSR) & 0x40));
}

void uart_tx_byte(base_port, data)
    Bit16u base_port;
    Bit8u data;
{
    uart_wait_to_tx_byte(base_port);
    outb(base_port + UART_THR, data);
    uart_wait_until_sent(base_port);
}
#endif

  void
wrch(c)
  Bit8u  c;
{
  ASM_START
  push bp
  mov  bp, sp

  push bx
  mov  ah, #0x0e
  mov  al, 4[bp]
  xor  bx,bx
  int  #0x10
  pop  bx

  pop  bp
  ASM_END
}
 
  void
send(action, c)
  Bit16u action;
  Bit8u  c;
{
#if BX_DEBUG_SERIAL
  if (c == '\n') uart_tx_byte(BX_DEBUG_PORT, '\r');
  uart_tx_byte(BX_DEBUG_PORT, c);
#endif
#if BX_VIRTUAL_PORTS
  if (action & BIOS_PRINTF_DEBUG) outb(DEBUG_PORT, c);
  if (action & BIOS_PRINTF_INFO) outb(INFO_PORT, c);
#endif
  if (action & BIOS_PRINTF_SCREEN) {
    if (c == '\n') wrch('\r');
    wrch(c);
  }
}

  void
put_int(action, val, width, neg)
  Bit16u action;
  short val, width;
  bx_bool neg;
{
  short nval = val / 10;
  if (nval)
    put_int(action, nval, width - 1, neg);
  else {
    while (--width > 0) send(action, ' ');
    if (neg) send(action, '-');
  }
  send(action, val - (nval * 10) + '0');
}

  void
put_uint(action, val, width, neg)
  Bit16u action;
  unsigned short val;
  short width;
  bx_bool neg;
{
  unsigned short nval = val / 10;
  if (nval)
    put_uint(action, nval, width - 1, neg);
  else {
    while (--width > 0) send(action, ' ');
    if (neg) send(action, '-');
  }
  send(action, val - (nval * 10) + '0');
}

  void
put_luint(action, val, width, neg)
  Bit16u action;
  unsigned long val;
  short width;
  bx_bool neg;
{
  unsigned long nval = val / 10;
  if (nval)
    put_luint(action, nval, width - 1, neg);
  else {
    while (--width > 0) send(action, ' ');
    if (neg) send(action, '-');
  }
  send(action, val - (nval * 10) + '0');
}

//--------------------------------------------------------------------------
// bios_printf()
//   A compact variable argument printf function which prints its output via
//   an I/O port so that it can be logged by Bochs/Plex.  
//   Currently, only %x is supported (or %02x, %04x, etc).
//
//   Supports %[format_width][format]
//   where format can be d,x,c,s
//--------------------------------------------------------------------------
  void
bios_printf(action, s)
  Bit16u action;
  Bit8u *s;
{
  Bit8u c, format_char;
  bx_bool  in_format;
  short i;
  Bit16u  *arg_ptr;
  Bit16u   arg_seg, arg, nibble, hibyte, shift_count, format_width;

  arg_ptr = &s;
  arg_seg = get_SS();

  in_format = 0;
  format_width = 0;

  if ((action & BIOS_PRINTF_DEBHALT) == BIOS_PRINTF_DEBHALT) {
#if BX_VIRTUAL_PORTS
    outb(PANIC_PORT2, 0x00);
#endif
    bios_printf (BIOS_PRINTF_SCREEN, "FATAL: ");
  }

  while (c = read_byte(get_CS(), s)) {
    if ( c == '%' ) {
      in_format = 1;
      format_width = 0;
      }
    else if (in_format) {
      if ( (c>='0') && (c<='9') ) {
        format_width = (format_width * 10) + (c - '0');
        }
      else {
        arg_ptr++; // increment to next arg
        arg = read_word(arg_seg, arg_ptr);
        if (c == 'x') {
          if (format_width == 0)
            format_width = 4;
          for (i=format_width-1; i>=0; i--) {
            nibble = (arg >> (4 * i)) & 0x000f;
            send (action, (nibble<=9)? (nibble+'0') : (nibble-10+'A'));
            }
          }
        else if (c == 'u') {
          put_uint(action, arg, format_width, 0);
          }
        else if (c == 'l') {
          s++;
          arg_ptr++; /* increment to next arg */
          hibyte = read_word(arg_seg, arg_ptr);
          put_luint(action, ((Bit32u) hibyte << 16) | arg, format_width, 0);
          }
        else if (c == 'd') {
          if (arg & 0x8000)
            put_int(action, -arg, format_width - 1, 1);
          else
            put_int(action, arg, format_width, 0);
          }
        else if (c == 's') {
          bios_printf(action & (~BIOS_PRINTF_HALT), arg);
          }
        else if (c == 'c') {
          send(action, arg);
          }
        else
          BX_PANIC("bios_printf: unknown format\n");
          in_format = 0;
        }
      }
    else {
      send(action, c);
      }
    s ++;
    }

  if (action & BIOS_PRINTF_HALT) {
    // freeze in a busy loop.  
ASM_START
    cli
 halt2_loop:
    hlt
    jmp halt2_loop
ASM_END
    }
}

//--------------------------------------------------------------------------
// keyboard_init
//--------------------------------------------------------------------------
// this file is based on LinuxBIOS implementation of keyboard.c
// could convert to #asm to gain space
  void
keyboard_init()
{
    Bit16u max;

    /* ------------------- Flush buffers ------------------------*/
    /* Wait until buffer is empty */
    max=0xffff;
    while ( (inb(0x64) & 0x02) && (--max>0)) outb(0x80, 0x00);

    /* flush incoming keys */
    max=0x2000;
    while (--max > 0) {
        outb(0x80, 0x00);
        if (inb(0x64) & 0x01) {
            inb(0x60);
            max = 0x2000;
            }
        }
  
    // Due to timer issues, and if the IPS setting is > 15000000, 
    // the incoming keys might not be flushed here. That will
    // cause a panic a few lines below.  See sourceforge bug report :
    // [ 642031 ] FATAL: Keyboard RESET error:993

    /* ------------------- controller side ----------------------*/
    /* send cmd = 0xAA, self test 8042 */
    outb(0x64, 0xaa);

    /* Wait until buffer is empty */
    max=0xffff;
    while ( (inb(0x64) & 0x02) && (--max>0)) outb(0x80, 0x00);
    if (max==0x0) keyboard_panic(00);

    /* Wait for data */
    max=0xffff;
    while ( ((inb(0x64) & 0x01) == 0) && (--max>0) ) outb(0x80, 0x01);
    if (max==0x0) keyboard_panic(01);

    /* read self-test result, 0x55 should be returned from 0x60 */
    if ((inb(0x60) != 0x55)){
        keyboard_panic(991);
    }

    /* send cmd = 0xAB, keyboard interface test */
    outb(0x64,0xab);

    /* Wait until buffer is empty */
    max=0xffff;
    while ((inb(0x64) & 0x02) && (--max>0)) outb(0x80, 0x10);
    if (max==0x0) keyboard_panic(10);

    /* Wait for data */
    max=0xffff;
    while ( ((inb(0x64) & 0x01) == 0) && (--max>0) ) outb(0x80, 0x11);
    if (max==0x0) keyboard_panic(11);

    /* read keyboard interface test result, */
    /* 0x00 should be returned form 0x60 */
    if ((inb(0x60) != 0x00)) {
        keyboard_panic(992);
    }

    /* Enable Keyboard clock */
    outb(0x64,0xae);
    outb(0x64,0xa8);

    /* ------------------- keyboard side ------------------------*/
    /* reset kerboard and self test  (keyboard side) */
    outb(0x60, 0xff);

    /* Wait until buffer is empty */
    max=0xffff;
    while ((inb(0x64) & 0x02) && (--max>0)) outb(0x80, 0x20);
    if (max==0x0) keyboard_panic(20);

    /* Wait for data */
    max=0xffff;
    while ( ((inb(0x64) & 0x01) == 0) && (--max>0) ) outb(0x80, 0x21);
    if (max==0x0) keyboard_panic(21);

    /* keyboard should return ACK */
    if ((inb(0x60) != 0xfa)) {
        keyboard_panic(993);
    }

    /* Wait for data */
    max=0xffff;
    while ( ((inb(0x64) & 0x01) == 0) && (--max>0) ) outb(0x80, 0x31);
    if (max==0x0) keyboard_panic(31);

    if ((inb(0x60) != 0xaa)) {
        keyboard_panic(994);
    }

    /* Disable keyboard */
    outb(0x60, 0xf5);

    /* Wait until buffer is empty */
    max=0xffff;
    while ((inb(0x64) & 0x02) && (--max>0)) outb(0x80, 0x40);
    if (max==0x0) keyboard_panic(40);

    /* Wait for data */
    max=0xffff;
    while ( ((inb(0x64) & 0x01) == 0) && (--max>0) ) outb(0x80, 0x41);
    if (max==0x0) keyboard_panic(41);

    /* keyboard should return ACK */
    if ((inb(0x60) != 0xfa)) {
        keyboard_panic(995);
    }

    /* Write Keyboard Mode */
    outb(0x64, 0x60);

    /* Wait until buffer is empty */
    max=0xffff;
    while ((inb(0x64) & 0x02) && (--max>0)) outb(0x80, 0x50);
    if (max==0x0) keyboard_panic(50);

    /* send cmd: scan code convert, disable mouse, enable IRQ 1 */
    outb(0x60, 0x61);

    /* Wait until buffer is empty */
    max=0xffff;
    while ((inb(0x64) & 0x02) && (--max>0)) outb(0x80, 0x60);
    if (max==0x0) keyboard_panic(60);

    /* Enable keyboard */
    outb(0x60, 0xf4);

    /* Wait until buffer is empty */
    max=0xffff;
    while ((inb(0x64) & 0x02) && (--max>0)) outb(0x80, 0x70);
    if (max==0x0) keyboard_panic(70);

    /* Wait for data */
    max=0xffff;
    while ( ((inb(0x64) & 0x01) == 0) && (--max>0) ) outb(0x80, 0x71);
    if (max==0x0) keyboard_panic(70);

    /* keyboard should return ACK */
    if ((inb(0x60) != 0xfa)) {
        keyboard_panic(996);
    }

    outb(0x80, 0x77);
}

//--------------------------------------------------------------------------
// keyboard_panic
//--------------------------------------------------------------------------
  void
keyboard_panic(status)
  Bit16u status;
{
  // If you're getting a 993 keyboard panic here, 
  // please see the comment in keyboard_init
  
  BX_PANIC("Keyboard error:%u\n",status);
}

//--------------------------------------------------------------------------
// shutdown_status_panic
//   called when the shutdown statsu is not implemented, displays the status
//--------------------------------------------------------------------------
  void
shutdown_status_panic(status)
  Bit16u status;
{
  BX_PANIC("Unimplemented shutdown status: %02x\n",(Bit8u)status);
}

//--------------------------------------------------------------------------
// print_bios_banner
//   displays a the bios version
//--------------------------------------------------------------------------
void
print_bios_banner()
{
  printf(BX_APPNAME" BIOS - build: %s\n%s\nOptions: ",
    BIOS_BUILD_DATE, bios_cvs_version_string);
  printf(
#if BX_APM
  "apmbios "
#endif
#if BX_PCIBIOS
  "pcibios "
#endif
#if BX_ELTORITO_BOOT
  "eltorito "
#endif
#if BX_ROMBIOS32
  "rombios32 "
#endif
  "\n\n");
}

//--------------------------------------------------------------------------
// print_boot_device
//   displays the boot device
//--------------------------------------------------------------------------

static char drivetypes[][10]={"Floppy","Hard Disk","CD-Rom"};

void
print_boot_device(cdboot, drive)
  Bit8u cdboot; Bit16u drive;
{
  Bit8u i;

  // cdboot contains 0 if floppy/harddisk, 1 otherwise
  // drive contains real/emulated boot drive

  if(cdboot)i=2;                    // CD-Rom
  else if((drive&0x0080)==0x00)i=0; // Floppy
  else if((drive&0x0080)==0x80)i=1; // Hard drive
  else return;
  
  printf("Booting from %s...\n",drivetypes[i]);
}

//--------------------------------------------------------------------------
// print_boot_failure
//   displays the reason why boot failed
//--------------------------------------------------------------------------
  void
print_boot_failure(cdboot, drive, reason, lastdrive)
  Bit8u cdboot; Bit8u drive; Bit8u lastdrive;
{
  Bit16u drivenum = drive&0x7f;

  // cdboot: 1 if boot from cd, 0 otherwise
  // drive : drive number
  // reason: 0 signature check failed, 1 read error
  // lastdrive: 1 boot drive is the last one in boot sequence
 
  if (cdboot)
    bios_printf(BIOS_PRINTF_INFO | BIOS_PRINTF_SCREEN, "Boot from %s failed\n",drivetypes[2]);
  else if (drive & 0x80)
    bios_printf(BIOS_PRINTF_INFO | BIOS_PRINTF_SCREEN, "Boot from %s %d failed\n", drivetypes[1],drivenum);
  else
    bios_printf(BIOS_PRINTF_INFO | BIOS_PRINTF_SCREEN, "Boot from %s %d failed\n", drivetypes[0],drivenum);

  if (lastdrive==1) {
    if (reason==0)
      BX_PANIC("Not a bootable disk\n");
    else
      BX_PANIC("Could not read the boot disk\n");
  }
}

//--------------------------------------------------------------------------
// print_cdromboot_failure
//   displays the reason why boot failed
//--------------------------------------------------------------------------
  void
print_cdromboot_failure( code )
  Bit16u code;
{
  bios_printf(BIOS_PRINTF_SCREEN | BIOS_PRINTF_INFO, "CDROM boot failure code : %04x\n",code);
  
  return;
}

void
nmi_handler_msg()
{
  BX_PANIC("NMI Handler called\n");
}

void
int18_panic_msg()
{
  BX_PANIC("INT18: BOOT FAILURE\n");
}

void
log_bios_start()
{
#if BX_DEBUG_SERIAL
  outb(BX_DEBUG_PORT+UART_LCR, 0x03); /* setup for serial logging: 8N1 */
#endif
  BX_INFO("%s\n", bios_cvs_version_string);
}

  bx_bool
set_enable_a20(val)
  bx_bool val;
{
  Bit8u  oldval;

  // Use PS2 System Control port A to set A20 enable

  // get current setting first
  oldval = inb(0x92);

  // change A20 status
  if (val)
    outb(0x92, oldval | 0x02);
  else
    outb(0x92, oldval & 0xfd);

  return((oldval & 0x02) != 0);
}

  void
debugger_on()
{
  outb(0xfedc, 0x01);
}

  void
debugger_off()
{
  outb(0xfedc, 0x00);
}

#if BX_USE_ATADRV

// ---------------------------------------------------------------------------
// Start of ATA/ATAPI Driver
// ---------------------------------------------------------------------------

// Global defines -- ATA register and register bits.
// command block & control block regs
#define ATA_CB_DATA  0   // data reg         in/out pio_base_addr1+0
#define ATA_CB_ERR   1   // error            in     pio_base_addr1+1
#define ATA_CB_FR    1   // feature reg         out pio_base_addr1+1
#define ATA_CB_SC    2   // sector count     in/out pio_base_addr1+2
#define ATA_CB_SN    3   // sector number    in/out pio_base_addr1+3
#define ATA_CB_CL    4   // cylinder low     in/out pio_base_addr1+4
#define ATA_CB_CH    5   // cylinder high    in/out pio_base_addr1+5
#define ATA_CB_DH    6   // device head      in/out pio_base_addr1+6
#define ATA_CB_STAT  7   // primary status   in     pio_base_addr1+7
#define ATA_CB_CMD   7   // command             out pio_base_addr1+7
#define ATA_CB_ASTAT 6   // alternate status in     pio_base_addr2+6
#define ATA_CB_DC    6   // device control      out pio_base_addr2+6
#define ATA_CB_DA    7   // device address   in     pio_base_addr2+7

#define ATA_CB_ER_ICRC 0x80    // ATA Ultra DMA bad CRC
#define ATA_CB_ER_BBK  0x80    // ATA bad block
#define ATA_CB_ER_UNC  0x40    // ATA uncorrected error
#define ATA_CB_ER_MC   0x20    // ATA media change
#define ATA_CB_ER_IDNF 0x10    // ATA id not found
#define ATA_CB_ER_MCR  0x08    // ATA media change request
#define ATA_CB_ER_ABRT 0x04    // ATA command aborted
#define ATA_CB_ER_NTK0 0x02    // ATA track 0 not found
#define ATA_CB_ER_NDAM 0x01    // ATA address mark not found

#define ATA_CB_ER_P_SNSKEY 0xf0   // ATAPI sense key (mask)
#define ATA_CB_ER_P_MCR    0x08   // ATAPI Media Change Request
#define ATA_CB_ER_P_ABRT   0x04   // ATAPI command abort
#define ATA_CB_ER_P_EOM    0x02   // ATAPI End of Media
#define ATA_CB_ER_P_ILI    0x01   // ATAPI Illegal Length Indication

// ATAPI Interrupt Reason bits in the Sector Count reg (CB_SC)
#define ATA_CB_SC_P_TAG    0xf8   // ATAPI tag (mask)
#define ATA_CB_SC_P_REL    0x04   // ATAPI release
#define ATA_CB_SC_P_IO     0x02   // ATAPI I/O
#define ATA_CB_SC_P_CD     0x01   // ATAPI C/D

// bits 7-4 of the device/head (CB_DH) reg
#define ATA_CB_DH_DEV0 0xa0    // select device 0
#define ATA_CB_DH_DEV1 0xb0    // select device 1

// status reg (CB_STAT and CB_ASTAT) bits
#define ATA_CB_STAT_BSY  0x80  // busy
#define ATA_CB_STAT_RDY  0x40  // ready
#define ATA_CB_STAT_DF   0x20  // device fault
#define ATA_CB_STAT_WFT  0x20  // write fault (old name)
#define ATA_CB_STAT_SKC  0x10  // seek complete
#define ATA_CB_STAT_SERV 0x10  // service
#define ATA_CB_STAT_DRQ  0x08  // data request
#define ATA_CB_STAT_CORR 0x04  // corrected
#define ATA_CB_STAT_IDX  0x02  // index
#define ATA_CB_STAT_ERR  0x01  // error (ATA)
#define ATA_CB_STAT_CHK  0x01  // check (ATAPI)

// device control reg (CB_DC) bits
#define ATA_CB_DC_HD15   0x08  // bit should always be set to one
#define ATA_CB_DC_SRST   0x04  // soft reset
#define ATA_CB_DC_NIEN   0x02  // disable interrupts

// Most mandtory and optional ATA commands (from ATA-3),
#define ATA_CMD_CFA_ERASE_SECTORS            0xC0
#define ATA_CMD_CFA_REQUEST_EXT_ERR_CODE     0x03
#define ATA_CMD_CFA_TRANSLATE_SECTOR         0x87
#define ATA_CMD_CFA_WRITE_MULTIPLE_WO_ERASE  0xCD
#define ATA_CMD_CFA_WRITE_SECTORS_WO_ERASE   0x38
#define ATA_CMD_CHECK_POWER_MODE1            0xE5
#define ATA_CMD_CHECK_POWER_MODE2            0x98
#define ATA_CMD_DEVICE_RESET                 0x08
#define ATA_CMD_EXECUTE_DEVICE_DIAGNOSTIC    0x90
#define ATA_CMD_FLUSH_CACHE                  0xE7
#define ATA_CMD_FORMAT_TRACK                 0x50
#define ATA_CMD_IDENTIFY_DEVICE              0xEC
#define ATA_CMD_IDENTIFY_DEVICE_PACKET       0xA1
#define ATA_CMD_IDENTIFY_PACKET_DEVICE       0xA1
#define ATA_CMD_IDLE1                        0xE3
#define ATA_CMD_IDLE2                        0x97
#define ATA_CMD_IDLE_IMMEDIATE1              0xE1
#define ATA_CMD_IDLE_IMMEDIATE2              0x95
#define ATA_CMD_INITIALIZE_DRIVE_PARAMETERS  0x91
#define ATA_CMD_INITIALIZE_DEVICE_PARAMETERS 0x91
#define ATA_CMD_NOP                          0x00
#define ATA_CMD_PACKET                       0xA0
#define ATA_CMD_READ_BUFFER                  0xE4
#define ATA_CMD_READ_DMA                     0xC8
#define ATA_CMD_READ_DMA_QUEUED              0xC7
#define ATA_CMD_READ_MULTIPLE                0xC4
#define ATA_CMD_READ_SECTORS                 0x20
#define ATA_CMD_READ_VERIFY_SECTORS          0x40
#define ATA_CMD_RECALIBRATE                  0x10
#define ATA_CMD_SEEK                         0x70
#define ATA_CMD_SET_FEATURES                 0xEF
#define ATA_CMD_SET_MULTIPLE_MODE            0xC6
#define ATA_CMD_SLEEP1                       0xE6
#define ATA_CMD_SLEEP2                       0x99
#define ATA_CMD_STANDBY1                     0xE2
#define ATA_CMD_STANDBY2                     0x96
#define ATA_CMD_STANDBY_IMMEDIATE1           0xE0
#define ATA_CMD_STANDBY_IMMEDIATE2           0x94
#define ATA_CMD_WRITE_BUFFER                 0xE8
#define ATA_CMD_WRITE_DMA                    0xCA
#define ATA_CMD_WRITE_DMA_QUEUED             0xCC
#define ATA_CMD_WRITE_MULTIPLE               0xC5
#define ATA_CMD_WRITE_SECTORS                0x30
#define ATA_CMD_WRITE_VERIFY                 0x3C

#define ATA_IFACE_NONE    0x00
#define ATA_IFACE_ISA     0x00
#define ATA_IFACE_PCI     0x01

#define ATA_TYPE_NONE     0x00
#define ATA_TYPE_UNKNOWN  0x01
#define ATA_TYPE_ATA      0x02
#define ATA_TYPE_ATAPI    0x03

#define ATA_DEVICE_NONE  0x00
#define ATA_DEVICE_HD    0xFF
#define ATA_DEVICE_CDROM 0x05

#define ATA_MODE_NONE    0x00
#define ATA_MODE_PIO16   0x00
#define ATA_MODE_PIO32   0x01
#define ATA_MODE_ISADMA  0x02
#define ATA_MODE_PCIDMA  0x03
#define ATA_MODE_USEIRQ  0x10

#define ATA_TRANSLATION_NONE  0
#define ATA_TRANSLATION_LBA   1
#define ATA_TRANSLATION_LARGE 2
#define ATA_TRANSLATION_RECHS 3

#define ATA_DATA_NO      0x00
#define ATA_DATA_IN      0x01
#define ATA_DATA_OUT     0x02
  
// ---------------------------------------------------------------------------
// ATA/ATAPI driver : initialization
// ---------------------------------------------------------------------------
void ata_init( )
{
  Bit16u ebda_seg=read_word(0x0040,0x000E);
  Bit8u  channel, device;

  // Channels info init. 
  for (channel=0; channel<BX_MAX_ATA_INTERFACES; channel++) {
    write_byte(ebda_seg,&EbdaData->ata.channels[channel].iface,ATA_IFACE_NONE);
    write_word(ebda_seg,&EbdaData->ata.channels[channel].iobase1,0x0);
    write_word(ebda_seg,&EbdaData->ata.channels[channel].iobase2,0x0);
    write_byte(ebda_seg,&EbdaData->ata.channels[channel].irq,0);
    }

  // Devices info init. 
  for (device=0; device<BX_MAX_ATA_DEVICES; device++) {
    write_byte(ebda_seg,&EbdaData->ata.devices[device].type,ATA_TYPE_NONE);
    write_byte(ebda_seg,&EbdaData->ata.devices[device].device,ATA_DEVICE_NONE);
    write_byte(ebda_seg,&EbdaData->ata.devices[device].removable,0);
    write_byte(ebda_seg,&EbdaData->ata.devices[device].lock,0);
    write_byte(ebda_seg,&EbdaData->ata.devices[device].mode,ATA_MODE_NONE);
    write_word(ebda_seg,&EbdaData->ata.devices[device].blksize,0);
    write_byte(ebda_seg,&EbdaData->ata.devices[device].translation,ATA_TRANSLATION_NONE);
    write_word(ebda_seg,&EbdaData->ata.devices[device].lchs.heads,0);
    write_word(ebda_seg,&EbdaData->ata.devices[device].lchs.cylinders,0);
    write_word(ebda_seg,&EbdaData->ata.devices[device].lchs.spt,0);
    write_word(ebda_seg,&EbdaData->ata.devices[device].pchs.heads,0);
    write_word(ebda_seg,&EbdaData->ata.devices[device].pchs.cylinders,0);
    write_word(ebda_seg,&EbdaData->ata.devices[device].pchs.spt,0);
    
    write_dword(ebda_seg,&EbdaData->ata.devices[device].sectors,0L);
    }

  // hdidmap  and cdidmap init. 
  for (device=0; device<BX_MAX_ATA_DEVICES; device++) {
    write_byte(ebda_seg,&EbdaData->ata.hdidmap[device],BX_MAX_ATA_DEVICES);
    write_byte(ebda_seg,&EbdaData->ata.cdidmap[device],BX_MAX_ATA_DEVICES);
    }

  write_byte(ebda_seg,&EbdaData->ata.hdcount,0);
  write_byte(ebda_seg,&EbdaData->ata.cdcount,0);
}

// ---------------------------------------------------------------------------
// ATA/ATAPI driver : device detection
// ---------------------------------------------------------------------------

void ata_detect( )
{
  Bit16u ebda_seg=read_word(0x0040,0x000E);
  Bit8u  hdcount, cdcount, device, type;
  Bit8u  buffer[0x0200];

#if BX_MAX_ATA_INTERFACES > 0
  write_byte(ebda_seg,&EbdaData->ata.channels[0].iface,ATA_IFACE_ISA);
  write_word(ebda_seg,&EbdaData->ata.channels[0].iobase1,0x1f0);
  write_word(ebda_seg,&EbdaData->ata.channels[0].iobase2,0x3f0);
  write_byte(ebda_seg,&EbdaData->ata.channels[0].irq,14);
#endif
#if BX_MAX_ATA_INTERFACES > 1
  write_byte(ebda_seg,&EbdaData->ata.channels[1].iface,ATA_IFACE_ISA);
  write_word(ebda_seg,&EbdaData->ata.channels[1].iobase1,0x170);
  write_word(ebda_seg,&EbdaData->ata.channels[1].iobase2,0x370);
  write_byte(ebda_seg,&EbdaData->ata.channels[1].irq,15);
#endif
#if BX_MAX_ATA_INTERFACES > 2
  write_byte(ebda_seg,&EbdaData->ata.channels[2].iface,ATA_IFACE_ISA);
  write_word(ebda_seg,&EbdaData->ata.channels[2].iobase1,0x1e8);
  write_word(ebda_seg,&EbdaData->ata.channels[2].iobase2,0x3e0);
  write_byte(ebda_seg,&EbdaData->ata.channels[2].irq,12);
#endif
#if BX_MAX_ATA_INTERFACES > 3
  write_byte(ebda_seg,&EbdaData->ata.channels[3].iface,ATA_IFACE_ISA);
  write_word(ebda_seg,&EbdaData->ata.channels[3].iobase1,0x168);
  write_word(ebda_seg,&EbdaData->ata.channels[3].iobase2,0x360);
  write_byte(ebda_seg,&EbdaData->ata.channels[3].irq,11);
#endif
#if BX_MAX_ATA_INTERFACES > 4
#error Please fill the ATA interface informations
#endif

  // Device detection
  hdcount=cdcount=0;
  
  for(device=0; device<BX_MAX_ATA_DEVICES; device++) {
    Bit16u iobase1, iobase2;
    Bit8u  channel, slave, shift;
    Bit8u  sc, sn, cl, ch, st;

    channel = device / 2;
    slave = device % 2;

    iobase1 =read_word(ebda_seg,&EbdaData->ata.channels[channel].iobase1);
    iobase2 =read_word(ebda_seg,&EbdaData->ata.channels[channel].iobase2);

    // Disable interrupts
    outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15 | ATA_CB_DC_NIEN);

    // Look for device
    outb(iobase1+ATA_CB_DH, slave ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0);
    outb(iobase1+ATA_CB_SC, 0x55);
    outb(iobase1+ATA_CB_SN, 0xaa);
    outb(iobase1+ATA_CB_SC, 0xaa);
    outb(iobase1+ATA_CB_SN, 0x55);
    outb(iobase1+ATA_CB_SC, 0x55);
    outb(iobase1+ATA_CB_SN, 0xaa);

    // If we found something
    sc = inb(iobase1+ATA_CB_SC);
    sn = inb(iobase1+ATA_CB_SN);

    if ( (sc == 0x55) && (sn == 0xaa) ) {
      write_byte(ebda_seg,&EbdaData->ata.devices[device].type,ATA_TYPE_UNKNOWN);
    
      // reset the channel
      ata_reset(device);
      
      // check for ATA or ATAPI
      outb(iobase1+ATA_CB_DH, slave ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0);
      sc = inb(iobase1+ATA_CB_SC);
      sn = inb(iobase1+ATA_CB_SN);
      if ((sc==0x01) && (sn==0x01)) {
        cl = inb(iobase1+ATA_CB_CL);
        ch = inb(iobase1+ATA_CB_CH);
        st = inb(iobase1+ATA_CB_STAT);

        if ((cl==0x14) && (ch==0xeb)) {
          write_byte(ebda_seg,&EbdaData->ata.devices[device].type,ATA_TYPE_ATAPI);
        } else if ((cl==0x00) && (ch==0x00) && (st!=0x00)) {
          write_byte(ebda_seg,&EbdaData->ata.devices[device].type,ATA_TYPE_ATA);
        } else if ((cl==0xff) && (ch==0xff)) {
          write_byte(ebda_seg,&EbdaData->ata.devices[device].type,ATA_TYPE_NONE);
        }
      }
    }

    type=read_byte(ebda_seg,&EbdaData->ata.devices[device].type);
    
    // Now we send a IDENTIFY command to ATA device 
    if(type == ATA_TYPE_ATA) {
      Bit32u sectors;
      Bit16u cylinders, heads, spt, blksize;
      Bit8u  translation, removable, mode;

      //Temporary values to do the transfer
      write_byte(ebda_seg,&EbdaData->ata.devices[device].device,ATA_DEVICE_HD);
      write_byte(ebda_seg,&EbdaData->ata.devices[device].mode, ATA_MODE_PIO16);

      if (ata_cmd_data_in(device,ATA_CMD_IDENTIFY_DEVICE, 1, 0, 0, 0, 0L, get_SS(),buffer) !=0 )
        BX_PANIC("ata-detect: Failed to detect ATA device\n");

      removable = (read_byte(get_SS(),buffer+0) & 0x80) ? 1 : 0;
      mode      = read_byte(get_SS(),buffer+96) ? ATA_MODE_PIO32 : ATA_MODE_PIO16;
      blksize   = read_word(get_SS(),buffer+10);
      
      cylinders = read_word(get_SS(),buffer+(1*2)); // word 1
      heads     = read_word(get_SS(),buffer+(3*2)); // word 3
      spt       = read_word(get_SS(),buffer+(6*2)); // word 6

      sectors   = read_dword(get_SS(),buffer+(60*2)); // word 60 and word 61

      write_byte(ebda_seg,&EbdaData->ata.devices[device].device,ATA_DEVICE_HD);
      write_byte(ebda_seg,&EbdaData->ata.devices[device].removable, removable);
      write_byte(ebda_seg,&EbdaData->ata.devices[device].mode, mode);
      write_word(ebda_seg,&EbdaData->ata.devices[device].blksize, blksize);
      write_word(ebda_seg,&EbdaData->ata.devices[device].pchs.heads, heads);
      write_word(ebda_seg,&EbdaData->ata.devices[device].pchs.cylinders, cylinders);
      write_word(ebda_seg,&EbdaData->ata.devices[device].pchs.spt, spt);
      write_dword(ebda_seg,&EbdaData->ata.devices[device].sectors, sectors);
      BX_INFO("ata%d-%d: PCHS=%u/%d/%d translation=", channel, slave,cylinders, heads, spt);

      translation = inb_cmos(0x39 + channel/2);
      for (shift=device%4; shift>0; shift--) translation >>= 2;
      translation &= 0x03;

      write_byte(ebda_seg,&EbdaData->ata.devices[device].translation, translation);

      switch (translation) {
        case ATA_TRANSLATION_NONE:
          BX_INFO("none");
          break;
        case ATA_TRANSLATION_LBA:
          BX_INFO("lba");
          break;
        case ATA_TRANSLATION_LARGE:
          BX_INFO("large");
          break;
        case ATA_TRANSLATION_RECHS:
          BX_INFO("r-echs");
          break;
        }
      switch (translation) {
        case ATA_TRANSLATION_NONE:
          break;
        case ATA_TRANSLATION_LBA:
          spt = 63;
          sectors /= 63;
          heads = sectors / 1024;
          if (heads>128) heads = 255;
          else if (heads>64) heads = 128;
          else if (heads>32) heads = 64;
          else if (heads>16) heads = 32;
          else heads=16;
          cylinders = sectors / heads;
          break;
        case ATA_TRANSLATION_RECHS:
          // Take care not to overflow
          if (heads==16) {
            if(cylinders>61439) cylinders=61439;
            heads=15;
            cylinders = (Bit16u)((Bit32u)(cylinders)*16/15);
            }
          // then go through the large bitshift process
        case ATA_TRANSLATION_LARGE:
          while(cylinders > 1024) {
            cylinders >>= 1;
            heads <<= 1;

            // If we max out the head count
            if (heads > 127) break;
          }
          break;
        }
      // clip to 1024 cylinders in lchs
      if (cylinders > 1024) cylinders=1024;
      BX_INFO(" LCHS=%d/%d/%d\n", cylinders, heads, spt);

      write_word(ebda_seg,&EbdaData->ata.devices[device].lchs.heads, heads);
      write_word(ebda_seg,&EbdaData->ata.devices[device].lchs.cylinders, cylinders);
      write_word(ebda_seg,&EbdaData->ata.devices[device].lchs.spt, spt);
 
      // fill hdidmap 
      write_byte(ebda_seg,&EbdaData->ata.hdidmap[hdcount], device);
      hdcount++;
      }
    
    // Now we send a IDENTIFY command to ATAPI device
    if(type == ATA_TYPE_ATAPI) {
 
      Bit8u  type, removable, mode;
      Bit16u blksize;

      //Temporary values to do the transfer
      write_byte(ebda_seg,&EbdaData->ata.devices[device].device,ATA_DEVICE_CDROM);
      write_byte(ebda_seg,&EbdaData->ata.devices[device].mode, ATA_MODE_PIO16);

      if (ata_cmd_data_in(device,ATA_CMD_IDENTIFY_DEVICE_PACKET, 1, 0, 0, 0, 0L, get_SS(),buffer) != 0)
        BX_PANIC("ata-detect: Failed to detect ATAPI device\n");

      type      = read_byte(get_SS(),buffer+1) & 0x1f;
      removable = (read_byte(get_SS(),buffer+0) & 0x80) ? 1 : 0;
      mode      = read_byte(get_SS(),buffer+96) ? ATA_MODE_PIO32 : ATA_MODE_PIO16;
      blksize   = 2048;

      write_byte(ebda_seg,&EbdaData->ata.devices[device].device, type);
      write_byte(ebda_seg,&EbdaData->ata.devices[device].removable, removable);
      write_byte(ebda_seg,&EbdaData->ata.devices[device].mode, mode);
      write_word(ebda_seg,&EbdaData->ata.devices[device].blksize, blksize);

      // fill cdidmap 
      write_byte(ebda_seg,&EbdaData->ata.cdidmap[cdcount], device);
      cdcount++;
      }
  
      {
      Bit32u sizeinmb;
      Bit16u ataversion;
      Bit8u  c, i, version, model[41];
      
      switch (type) {
        case ATA_TYPE_ATA:
          sizeinmb = read_dword(ebda_seg,&EbdaData->ata.devices[device].sectors);
          sizeinmb >>= 11;
        case ATA_TYPE_ATAPI:
          // Read ATA/ATAPI version
          ataversion=((Bit16u)(read_byte(get_SS(),buffer+161))<<8)|read_byte(get_SS(),buffer+160);
          for(version=15;version>0;version--) { 
            if((ataversion&(1<<version))!=0)
            break;
            }

          // Read model name
          for(i=0;i<20;i++){
            write_byte(get_SS(),model+(i*2),read_byte(get_SS(),buffer+(i*2)+54+1));
            write_byte(get_SS(),model+(i*2)+1,read_byte(get_SS(),buffer+(i*2)+54));
            }

          // Reformat
          write_byte(get_SS(),model+40,0x00);
          for(i=39;i>0;i--){
            if(read_byte(get_SS(),model+i)==0x20)
              write_byte(get_SS(),model+i,0x00);
            else break;
            }
          break;
        }

      switch (type) {
        case ATA_TYPE_ATA:
          printf("ata%d %s: ",channel,slave?" slave":"master");
          i=0; while(c=read_byte(get_SS(),model+i++)) printf("%c",c);
          printf(" ATA-%d Hard-Disk (%lu MBytes)\n", version, sizeinmb);
          break;
        case ATA_TYPE_ATAPI:
          printf("ata%d %s: ",channel,slave?" slave":"master");
          i=0; while(c=read_byte(get_SS(),model+i++)) printf("%c",c);
          if(read_byte(ebda_seg,&EbdaData->ata.devices[device].device)==ATA_DEVICE_CDROM)
            printf(" ATAPI-%d CD-Rom/DVD-Rom\n",version);
          else
            printf(" ATAPI-%d Device\n",version);
          break;
        case ATA_TYPE_UNKNOWN:
          printf("ata%d %s: Unknown device\n",channel,slave?" slave":"master");
          break;
        }
      }
    }

  // Store the devices counts
  write_byte(ebda_seg,&EbdaData->ata.hdcount, hdcount);
  write_byte(ebda_seg,&EbdaData->ata.cdcount, cdcount);
  write_byte(0x40,0x75, hdcount);
 
  printf("\n");

  // FIXME : should use bios=cmos|auto|disable bits
  // FIXME : should know about translation bits
  // FIXME : move hard_drive_post here 
  
}

// ---------------------------------------------------------------------------
// ATA/ATAPI driver : software reset 
// ---------------------------------------------------------------------------
// ATA-3
// 8.2.1 Software reset - Device 0

void   ata_reset(device)
Bit16u device;
{
  Bit16u ebda_seg=read_word(0x0040,0x000E);
  Bit16u iobase1, iobase2;
  Bit8u  channel, slave, sn, sc; 
  Bit16u max;

  channel = device / 2;
  slave = device % 2;

  iobase1 = read_word(ebda_seg, &EbdaData->ata.channels[channel].iobase1);
  iobase2 = read_word(ebda_seg, &EbdaData->ata.channels[channel].iobase2);

  // Reset

// 8.2.1 (a) -- set SRST in DC
  outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15 | ATA_CB_DC_NIEN | ATA_CB_DC_SRST);

// 8.2.1 (b) -- wait for BSY
  max=0xff;
  while(--max>0) {
    Bit8u status = inb(iobase1+ATA_CB_STAT);
    if ((status & ATA_CB_STAT_BSY) != 0) break;
  }

// 8.2.1 (f) -- clear SRST
  outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15 | ATA_CB_DC_NIEN);

  if (read_byte(ebda_seg,&EbdaData->ata.devices[device].type) != ATA_TYPE_NONE) {

// 8.2.1 (g) -- check for sc==sn==0x01
    // select device
    outb(iobase1+ATA_CB_DH, slave?ATA_CB_DH_DEV1:ATA_CB_DH_DEV0);
    sc = inb(iobase1+ATA_CB_SC);
    sn = inb(iobase1+ATA_CB_SN);

    if ( (sc==0x01) && (sn==0x01) ) {

// 8.2.1 (h) -- wait for not BSY
      max=0xff;
      while(--max>0) {
        Bit8u status = inb(iobase1+ATA_CB_STAT);
        if ((status & ATA_CB_STAT_BSY) == 0) break;
        }
      }
    }

// 8.2.1 (i) -- wait for DRDY
  max=0xfff;
  while(--max>0) {
    Bit8u status = inb(iobase1+ATA_CB_STAT);
      if ((status & ATA_CB_STAT_RDY) != 0) break;
  }

  // Enable interrupts
  outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
}

// ---------------------------------------------------------------------------
// ATA/ATAPI driver : execute a non data command 
// ---------------------------------------------------------------------------

Bit16u ata_cmd_non_data()
{return 0;}

// ---------------------------------------------------------------------------
// ATA/ATAPI driver : execute a data-in command
// ---------------------------------------------------------------------------
      // returns
      // 0 : no error
      // 1 : BUSY bit set
      // 2 : read error
      // 3 : expected DRQ=1
      // 4 : no sectors left to read/verify
      // 5 : more sectors to read/verify
      // 6 : no sectors left to write
      // 7 : more sectors to write
Bit16u ata_cmd_data_in(device, command, count, cylinder, head, sector, lba, segment, offset)
Bit16u device, command, count, cylinder, head, sector, segment, offset;
Bit32u lba;
{
  Bit16u ebda_seg=read_word(0x0040,0x000E);
  Bit16u iobase1, iobase2, blksize;
  Bit8u  channel, slave;
  Bit8u  status, current, mode;

  channel = device / 2;
  slave   = device % 2;

  iobase1 = read_word(ebda_seg, &EbdaData->ata.channels[channel].iobase1);
  iobase2 = read_word(ebda_seg, &EbdaData->ata.channels[channel].iobase2);
  mode    = read_byte(ebda_seg, &EbdaData->ata.devices[device].mode);
  blksize = 0x200; // was = read_word(ebda_seg, &EbdaData->ata.devices[device].blksize);
  if (mode == ATA_MODE_PIO32) blksize>>=2;
  else blksize>>=1;

  // sector will be 0 only on lba access. Convert to lba-chs
  if (sector == 0) {
    sector = (Bit16u) (lba & 0x000000ffL);
    lba >>= 8;
    cylinder = (Bit16u) (lba & 0x0000ffffL);
    lba >>= 16;
    head = ((Bit16u) (lba & 0x0000000fL)) | 0x40;
    }

  // Reset count of transferred data
  write_word(ebda_seg, &EbdaData->ata.trsfsectors,0);
  write_dword(ebda_seg, &EbdaData->ata.trsfbytes,0L);
  current = 0;

  status = inb(iobase1 + ATA_CB_STAT);
  if (status & ATA_CB_STAT_BSY) return 1;

  outb(iobase2 + ATA_CB_DC, ATA_CB_DC_HD15 | ATA_CB_DC_NIEN);
  outb(iobase1 + ATA_CB_FR, 0x00);
  outb(iobase1 + ATA_CB_SC, count);
  outb(iobase1 + ATA_CB_SN, sector);
  outb(iobase1 + ATA_CB_CL, cylinder & 0x00ff);
  outb(iobase1 + ATA_CB_CH, cylinder >> 8);
  outb(iobase1 + ATA_CB_DH, (slave ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0) | (Bit8u) head );
  outb(iobase1 + ATA_CB_CMD, command);

  while (1) {
    status = inb(iobase1 + ATA_CB_STAT);
    if ( !(status & ATA_CB_STAT_BSY) ) break;
    }

  if (status & ATA_CB_STAT_ERR) {
    BX_DEBUG_ATA("ata_cmd_data_in : read error\n");
    return 2;
    } else if ( !(status & ATA_CB_STAT_DRQ) ) {
    BX_DEBUG_ATA("ata_cmd_data_in : DRQ not set (status %02x)\n", (unsigned) status);
    return 3;
  }

  // FIXME : move seg/off translation here

ASM_START
        sti  ;; enable higher priority interrupts
ASM_END

  while (1) {

ASM_START
        push bp
        mov  bp, sp
        mov  di, _ata_cmd_data_in.offset + 2[bp]  
        mov  ax, _ata_cmd_data_in.segment + 2[bp] 
        mov  cx, _ata_cmd_data_in.blksize + 2[bp] 

        ;; adjust if there will be an overrun. 2K max sector size
        cmp   di, #0xf800 ;; 
        jbe   ata_in_no_adjust

ata_in_adjust:
        sub   di, #0x0800 ;; sub 2 kbytes from offset
        add   ax, #0x0080 ;; add 2 Kbytes to segment

ata_in_no_adjust:
        mov   es, ax      ;; segment in es

        mov   dx, _ata_cmd_data_in.iobase1 + 2[bp] ;; ATA data read port

        mov  ah, _ata_cmd_data_in.mode + 2[bp] 
        cmp  ah, #ATA_MODE_PIO32
        je   ata_in_32

ata_in_16:
        rep
          insw ;; CX words transfered from port(DX) to ES:[DI]
        jmp ata_in_done

ata_in_32:
        rep
          insd ;; CX dwords transfered from port(DX) to ES:[DI]

ata_in_done:
        mov  _ata_cmd_data_in.offset + 2[bp], di
        mov  _ata_cmd_data_in.segment + 2[bp], es
        pop  bp
ASM_END

    current++;
    write_word(ebda_seg, &EbdaData->ata.trsfsectors,current);
    count--;
    status = inb(iobase1 + ATA_CB_STAT);
    if (count == 0) {
      if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ | ATA_CB_STAT_ERR) ) 
          != ATA_CB_STAT_RDY ) {
        BX_DEBUG_ATA("ata_cmd_data_in : no sectors left (status %02x)\n", (unsigned) status);
        return 4;
        }
      break;
      }
    else {
      if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ | ATA_CB_STAT_ERR) ) 
          != (ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ) ) {
        BX_DEBUG_ATA("ata_cmd_data_in : more sectors left (status %02x)\n", (unsigned) status);
        return 5;
      }
      continue;
    }
  }
  // Enable interrupts
  outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
  return 0;
}

// ---------------------------------------------------------------------------
// ATA/ATAPI driver : execute a data-out command
// ---------------------------------------------------------------------------
      // returns
      // 0 : no error
      // 1 : BUSY bit set
      // 2 : read error
      // 3 : expected DRQ=1
      // 4 : no sectors left to read/verify
      // 5 : more sectors to read/verify
      // 6 : no sectors left to write
      // 7 : more sectors to write
Bit16u ata_cmd_data_out(device, command, count, cylinder, head, sector, lba, segment, offset)
Bit16u device, command, count, cylinder, head, sector, segment, offset;
Bit32u lba;
{
  Bit16u ebda_seg=read_word(0x0040,0x000E);
  Bit16u iobase1, iobase2, blksize;
  Bit8u  channel, slave;
  Bit8u  status, current, mode;

  channel = device / 2;
  slave   = device % 2;

  iobase1 = read_word(ebda_seg, &EbdaData->ata.channels[channel].iobase1);
  iobase2 = read_word(ebda_seg, &EbdaData->ata.channels[channel].iobase2);
  mode    = read_byte(ebda_seg, &EbdaData->ata.devices[device].mode);
  blksize = 0x200; // was = read_word(ebda_seg, &EbdaData->ata.devices[device].blksize);
  if (mode == ATA_MODE_PIO32) blksize>>=2;
  else blksize>>=1;

  // sector will be 0 only on lba access. Convert to lba-chs
  if (sector == 0) {
    sector = (Bit16u) (lba & 0x000000ffL);
    lba >>= 8;
    cylinder = (Bit16u) (lba & 0x0000ffffL);
    lba >>= 16;
    head = ((Bit16u) (lba & 0x0000000fL)) | 0x40;
    }

  // Reset count of transferred data
  write_word(ebda_seg, &EbdaData->ata.trsfsectors,0);
  write_dword(ebda_seg, &EbdaData->ata.trsfbytes,0L);
  current = 0;

  status = inb(iobase1 + ATA_CB_STAT);
  if (status & ATA_CB_STAT_BSY) return 1;

  outb(iobase2 + ATA_CB_DC, ATA_CB_DC_HD15 | ATA_CB_DC_NIEN);
  outb(iobase1 + ATA_CB_FR, 0x00);
  outb(iobase1 + ATA_CB_SC, count);
  outb(iobase1 + ATA_CB_SN, sector);
  outb(iobase1 + ATA_CB_CL, cylinder & 0x00ff);
  outb(iobase1 + ATA_CB_CH, cylinder >> 8);
  outb(iobase1 + ATA_CB_DH, (slave ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0) | (Bit8u) head );
  outb(iobase1 + ATA_CB_CMD, command);

  while (1) {
    status = inb(iobase1 + ATA_CB_STAT);
    if ( !(status & ATA_CB_STAT_BSY) ) break;
    }

  if (status & ATA_CB_STAT_ERR) {
    BX_DEBUG_ATA("ata_cmd_data_out : read error\n");
    return 2;
    } else if ( !(status & ATA_CB_STAT_DRQ) ) {
    BX_DEBUG_ATA("ata_cmd_data_out : DRQ not set (status %02x)\n", (unsigned) status);
    return 3;
    }

  // FIXME : move seg/off translation here

ASM_START
        sti  ;; enable higher priority interrupts
ASM_END

  while (1) {

ASM_START
        push bp
        mov  bp, sp
        mov  si, _ata_cmd_data_out.offset + 2[bp]  
        mov  ax, _ata_cmd_data_out.segment + 2[bp] 
        mov  cx, _ata_cmd_data_out.blksize + 2[bp] 

        ;; adjust if there will be an overrun. 2K max sector size
        cmp   si, #0xf800 ;; 
        jbe   ata_out_no_adjust

ata_out_adjust:
        sub   si, #0x0800 ;; sub 2 kbytes from offset
        add   ax, #0x0080 ;; add 2 Kbytes to segment

ata_out_no_adjust:
        mov   es, ax      ;; segment in es

        mov   dx, _ata_cmd_data_out.iobase1 + 2[bp] ;; ATA data write port

        mov  ah, _ata_cmd_data_out.mode + 2[bp] 
        cmp  ah, #ATA_MODE_PIO32
        je   ata_out_32

ata_out_16:
        seg ES
        rep
          outsw ;; CX words transfered from port(DX) to ES:[SI]
        jmp ata_out_done

ata_out_32:
        seg ES
        rep
          outsd ;; CX dwords transfered from port(DX) to ES:[SI]

ata_out_done:
        mov  _ata_cmd_data_out.offset + 2[bp], si
        mov  _ata_cmd_data_out.segment + 2[bp], es
        pop  bp
ASM_END

    current++;
    write_word(ebda_seg, &EbdaData->ata.trsfsectors,current);
    count--;
    status = inb(iobase1 + ATA_CB_STAT);
    if (count == 0) {
      if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DF | ATA_CB_STAT_DRQ | ATA_CB_STAT_ERR) ) 
          != ATA_CB_STAT_RDY ) {
        BX_DEBUG_ATA("ata_cmd_data_out : no sectors left (status %02x)\n", (unsigned) status);
        return 6;
        }
      break;
      }
    else {
      if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ | ATA_CB_STAT_ERR) ) 
          != (ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ) ) {
        BX_DEBUG_ATA("ata_cmd_data_out : more sectors left (status %02x)\n", (unsigned) status);
        return 7;
      }
      continue;
    }
  }
  // Enable interrupts
  outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
  return 0;
}

// ---------------------------------------------------------------------------
// ATA/ATAPI driver : execute a packet command
// ---------------------------------------------------------------------------
      // returns
      // 0 : no error
      // 1 : error in parameters
      // 2 : BUSY bit set
      // 3 : error
      // 4 : not ready
Bit16u ata_cmd_packet(device, cmdlen, cmdseg, cmdoff, header, length, inout, bufseg, bufoff)
Bit8u  cmdlen,inout;
Bit16u device,cmdseg, cmdoff, bufseg, bufoff;
Bit16u header;
Bit32u length;
{
  Bit16u ebda_seg=read_word(0x0040,0x000E);
  Bit16u iobase1, iobase2;
  Bit16u lcount, lbefore, lafter, count;
  Bit8u  channel, slave;
  Bit8u  status, mode, lmode;
  Bit32u total, transfer;

  channel = device / 2;
  slave = device % 2;

  // Data out is not supported yet
  if (inout == ATA_DATA_OUT) {
    BX_INFO("ata_cmd_packet: DATA_OUT not supported yet\n");
    return 1;
    }

  // The header length must be even
  if (header & 1) {
    BX_DEBUG_ATA("ata_cmd_packet : header must be even (%04x)\n",header);
    return 1;
    }

  iobase1 = read_word(ebda_seg, &EbdaData->ata.channels[channel].iobase1);
  iobase2 = read_word(ebda_seg, &EbdaData->ata.channels[channel].iobase2);
  mode    = read_byte(ebda_seg, &EbdaData->ata.devices[device].mode);
  transfer= 0L;

  if (cmdlen < 12) cmdlen=12;
  if (cmdlen > 12) cmdlen=16;
  cmdlen>>=1;

  // Reset count of transferred data
  write_word(ebda_seg, &EbdaData->ata.trsfsectors,0);
  write_dword(ebda_seg, &EbdaData->ata.trsfbytes,0L);

  status = inb(iobase1 + ATA_CB_STAT);
  if (status & ATA_CB_STAT_BSY) return 2;

  outb(iobase2 + ATA_CB_DC, ATA_CB_DC_HD15 | ATA_CB_DC_NIEN);
  // outb(iobase1 + ATA_CB_FR, 0x00);
  // outb(iobase1 + ATA_CB_SC, 0x00);
  // outb(iobase1 + ATA_CB_SN, 0x00);
  outb(iobase1 + ATA_CB_CL, 0xfff0 & 0x00ff);
  outb(iobase1 + ATA_CB_CH, 0xfff0 >> 8);
  outb(iobase1 + ATA_CB_DH, slave ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0);
  outb(iobase1 + ATA_CB_CMD, ATA_CMD_PACKET);

  // Device should ok to receive command
  while (1) {
    status = inb(iobase1 + ATA_CB_STAT);
    if ( !(status & ATA_CB_STAT_BSY) ) break;
    }

  if (status & ATA_CB_STAT_ERR) {
    BX_DEBUG_ATA("ata_cmd_packet : error, status is %02x\n",status);
    return 3;
    } else if ( !(status & ATA_CB_STAT_DRQ) ) {
    BX_DEBUG_ATA("ata_cmd_packet : DRQ not set (status %02x)\n", (unsigned) status);
    return 4;
    }

  // Normalize address
  cmdseg += (cmdoff / 16);
  cmdoff %= 16;

  // Send command to device
ASM_START
      sti  ;; enable higher priority interrupts
 
      push bp
      mov  bp, sp
    
      mov  si, _ata_cmd_packet.cmdoff + 2[bp]  
      mov  ax, _ata_cmd_packet.cmdseg + 2[bp] 
      mov  cx, _ata_cmd_packet.cmdlen + 2[bp] 
      mov  es, ax      ;; segment in es

      mov  dx, _ata_cmd_packet.iobase1 + 2[bp] ;; ATA data write port

      seg ES
      rep
        outsw ;; CX words transfered from port(DX) to ES:[SI]

      pop  bp
ASM_END

  if (inout == ATA_DATA_NO) {
    status = inb(iobase1 + ATA_CB_STAT);
    }
  else {
  while (1) {

      status = inb(iobase1 + ATA_CB_STAT);

      // Check if command completed
      if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_DRQ) ) ==0 ) break;

      if (status & ATA_CB_STAT_ERR) {
        BX_DEBUG_ATA("ata_cmd_packet : error (status %02x)\n",status);
        return 3;
      }

      // Device must be ready to send data
      if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ | ATA_CB_STAT_ERR) ) 
            != (ATA_CB_STAT_RDY | ATA_CB_STAT_DRQ) ) {
        BX_DEBUG_ATA("ata_cmd_packet : not ready (status %02x)\n", status);
        return 4;
        }

      // Normalize address
      bufseg += (bufoff / 16);
      bufoff %= 16;
    
      // Get the byte count
      lcount =  ((Bit16u)(inb(iobase1 + ATA_CB_CH))<<8)+inb(iobase1 + ATA_CB_CL);

      // adjust to read what we want
      if(header>lcount) {
         lbefore=lcount;
         header-=lcount;
         lcount=0;
         }
      else {
        lbefore=header;
        header=0;
        lcount-=lbefore;
        }

      if(lcount>length) {
        lafter=lcount-length;
        lcount=length;
        length=0;
        }
      else {
        lafter=0;
        length-=lcount;
        }

      // Save byte count
      count = lcount;

      BX_DEBUG_ATA("Trying to read %04x bytes (%04x %04x %04x) ",lbefore+lcount+lafter,lbefore,lcount,lafter);
      BX_DEBUG_ATA("to 0x%04x:0x%04x\n",bufseg,bufoff);

      // If counts not dividable by 4, use 16bits mode
      lmode = mode;
      if (lbefore & 0x03) lmode=ATA_MODE_PIO16;
      if (lcount  & 0x03) lmode=ATA_MODE_PIO16;
      if (lafter  & 0x03) lmode=ATA_MODE_PIO16;

      // adds an extra byte if count are odd. before is always even
      if (lcount & 0x01) {
        lcount+=1;
        if ((lafter > 0) && (lafter & 0x01)) {
          lafter-=1;
          }
        }

      if (lmode == ATA_MODE_PIO32) {
        lcount>>=2; lbefore>>=2; lafter>>=2;
        }
      else {
        lcount>>=1; lbefore>>=1; lafter>>=1;
        }

       ;  // FIXME bcc bug

ASM_START
        push bp
        mov  bp, sp

        mov  dx, _ata_cmd_packet.iobase1 + 2[bp] ;; ATA data read port

        mov  cx, _ata_cmd_packet.lbefore + 2[bp] 
        jcxz ata_packet_no_before

        mov  ah, _ata_cmd_packet.lmode + 2[bp] 
        cmp  ah, #ATA_MODE_PIO32
        je   ata_packet_in_before_32

ata_packet_in_before_16:
        in   ax, dx
        loop ata_packet_in_before_16
        jmp  ata_packet_no_before

ata_packet_in_before_32:
        push eax
ata_packet_in_before_32_loop:
        in   eax, dx
        loop ata_packet_in_before_32_loop
        pop  eax

ata_packet_no_before:
        mov  cx, _ata_cmd_packet.lcount + 2[bp] 
        jcxz ata_packet_after

        mov  di, _ata_cmd_packet.bufoff + 2[bp]  
        mov  ax, _ata_cmd_packet.bufseg + 2[bp] 
        mov  es, ax

        mov  ah, _ata_cmd_packet.lmode + 2[bp] 
        cmp  ah, #ATA_MODE_PIO32
        je   ata_packet_in_32

ata_packet_in_16:
        rep
          insw ;; CX words transfered tp port(DX) to ES:[DI]
        jmp ata_packet_after

ata_packet_in_32:
        rep
          insd ;; CX dwords transfered to port(DX) to ES:[DI]

ata_packet_after:
        mov  cx, _ata_cmd_packet.lafter + 2[bp] 
        jcxz ata_packet_done

        mov  ah, _ata_cmd_packet.lmode + 2[bp] 
        cmp  ah, #ATA_MODE_PIO32
        je   ata_packet_in_after_32

ata_packet_in_after_16:
        in   ax, dx
        loop ata_packet_in_after_16
        jmp  ata_packet_done

ata_packet_in_after_32:
        push eax
ata_packet_in_after_32_loop:
        in   eax, dx
        loop ata_packet_in_after_32_loop
        pop  eax

ata_packet_done:
        pop  bp
ASM_END

      // Compute new buffer address
      bufoff += count;

      // Save transferred bytes count
      transfer += count;
      write_dword(ebda_seg, &EbdaData->ata.trsfbytes,transfer);
      }
    }

  // Final check, device must be ready
  if ( (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_RDY | ATA_CB_STAT_DF | ATA_CB_STAT_DRQ | ATA_CB_STAT_ERR) ) 
         != ATA_CB_STAT_RDY ) {
    BX_DEBUG_ATA("ata_cmd_packet : not ready (status %02x)\n", (unsigned) status);
    return 4;
    }

  // Enable interrupts
  outb(iobase2+ATA_CB_DC, ATA_CB_DC_HD15);
  return 0;
}

// ---------------------------------------------------------------------------
// End of ATA/ATAPI Driver
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Start of ATA/ATAPI generic functions
// ---------------------------------------------------------------------------

  Bit16u 
atapi_get_sense(device)
  Bit16u device;
{
  Bit8u  atacmd[12];
  Bit8u  buffer[16];
  Bit8u i;

  memsetb(get_SS(),atacmd,0,12);

  // Request SENSE 
  atacmd[0]=0x03;    
  atacmd[4]=0x20;    
  if (ata_cmd_packet(device, 12, get_SS(), atacmd, 0, 16L, ATA_DATA_IN, get_SS(), buffer) != 0)
    return 0x0002;

  if ((buffer[0] & 0x7e) == 0x70) {
    return (((Bit16u)buffer[2]&0x0f)*0x100)+buffer[12];
    }

  return 0;
}

  Bit16u 
atapi_is_ready(device)
  Bit16u device;
{
  Bit8u  atacmd[12];
  Bit8u  buffer[];

  memsetb(get_SS(),atacmd,0,12);
 
  // Test Unit Ready
  if (ata_cmd_packet(device, 12, get_SS(), atacmd, 0, 0L, ATA_DATA_NO, get_SS(), buffer) != 0)
    return 0x000f;

  if (atapi_get_sense(device) !=0 ) {
    memsetb(get_SS(),atacmd,0,12);

    // try to send Test Unit Ready again
    if (ata_cmd_packet(device, 12, get_SS(), atacmd, 0, 0L, ATA_DATA_NO, get_SS(), buffer) != 0)
      return 0x000f;

    return atapi_get_sense(device);
    }
  return 0;
}

  Bit16u 
atapi_is_cdrom(device)
  Bit8u device;
{
  Bit16u ebda_seg=read_word(0x0040,0x000E);

  if (device >= BX_MAX_ATA_DEVICES)
    return 0;

  if (read_byte(ebda_seg,&EbdaData->ata.devices[device].type) != ATA_TYPE_ATAPI)
    return 0;

  if (read_byte(ebda_seg,&EbdaData->ata.devices[device].device) != ATA_DEVICE_CDROM)
    return 0;

  return 1;
}

// ---------------------------------------------------------------------------
// End of ATA/ATAPI generic functions
// ---------------------------------------------------------------------------

#endif // BX_USE_ATADRV

#if BX_ELTORITO_BOOT

// ---------------------------------------------------------------------------
// Start of El-Torito boot functions
// ---------------------------------------------------------------------------

  void
cdemu_init()
{
  Bit16u ebda_seg=read_word(0x0040,0x000E);

  // the only important data is this one for now
  write_byte(ebda_seg,&EbdaData->cdemu.active,0x00);
}

  Bit8u
cdemu_isactive()
{
  Bit16u ebda_seg=read_word(0x0040,0x000E);

  return(read_byte(ebda_seg,&EbdaData->cdemu.active));
}

  Bit8u
cdemu_emulated_drive()
{
  Bit16u ebda_seg=read_word(0x0040,0x000E);

  return(read_byte(ebda_seg,&EbdaData->cdemu.emulated_drive));
}

static char isotag[6]="CD001";
static char eltorito[24]="EL TORITO SPECIFICATION";
//
// Returns ah: emulated drive, al: error code
//
  Bit16u 
cdrom_boot()
{
  Bit16u ebda_seg=read_word(0x0040,0x000E);
  Bit8u  atacmd[12], buffer[2048];
  Bit32u lba;
  Bit16u boot_segment, nbsectors, i, error;
  Bit8u  device;

  // Find out the first cdrom
  for (device=0; device<BX_MAX_ATA_DEVICES;device++) {
    if (atapi_is_cdrom(device)) break;
    }
  
  // if not found
  if(device >= BX_MAX_ATA_DEVICES) return 2;

  // Read the Boot Record Volume Descriptor
  memsetb(get_SS(),atacmd,0,12);
  atacmd[0]=0x28;                      // READ command
  atacmd[7]=(0x01 & 0xff00) >> 8;      // Sectors
  atacmd[8]=(0x01 & 0x00ff);           // Sectors
  atacmd[2]=(0x11 & 0xff000000) >> 24; // LBA
  atacmd[3]=(0x11 & 0x00ff0000) >> 16;
  atacmd[4]=(0x11 & 0x0000ff00) >> 8;
  atacmd[5]=(0x11 & 0x000000ff);
  if((error = ata_cmd_packet(device, 12, get_SS(), atacmd, 0, 2048L, ATA_DATA_IN, get_SS(), buffer)) != 0)
    return 3;

  // Validity checks
  if(buffer[0]!=0)return 4;
  for(i=0;i<5;i++){
    if(buffer[1+i]!=read_byte(0xf000,&isotag[i]))return 5;
   }
  for(i=0;i<23;i++)
    if(buffer[7+i]!=read_byte(0xf000,&eltorito[i]))return 6;
  
  // ok, now we calculate the Boot catalog address
  lba=buffer[0x4A]*0x1000000+buffer[0x49]*0x10000+buffer[0x48]*0x100+buffer[0x47];

  // And we read the Boot Catalog
  memsetb(get_SS(),atacmd,0,12);
  atacmd[0]=0x28;                      // READ command
  atacmd[7]=(0x01 & 0xff00) >> 8;      // Sectors
  atacmd[8]=(0x01 & 0x00ff);           // Sectors
  atacmd[2]=(lba & 0xff000000) >> 24;  // LBA
  atacmd[3]=(lba & 0x00ff0000) >> 16;
  atacmd[4]=(lba & 0x0000ff00) >> 8;
  atacmd[5]=(lba & 0x000000ff);
  if((error = ata_cmd_packet(device, 12, get_SS(), atacmd, 0, 2048L, ATA_DATA_IN, get_SS(), buffer)) != 0)
    return 7;
 
  // Validation entry
  if(buffer[0x00]!=0x01)return 8;   // Header
  if(buffer[0x01]!=0x00)return 9;   // Platform
  if(buffer[0x1E]!=0x55)return 10;  // key 1
  if(buffer[0x1F]!=0xAA)return 10;  // key 2

  // Initial/Default Entry
  if(buffer[0x20]!=0x88)return 11; // Bootable

  write_byte(ebda_seg,&EbdaData->cdemu.media,buffer[0x21]);
  if(buffer[0x21]==0){
    // FIXME ElTorito Hardcoded. cdrom is hardcoded as device 0xE0. 
    // Win2000 cd boot needs to know it booted from cd
    write_byte(ebda_seg,&EbdaData->cdemu.emulated_drive,0xE0);
    } 
  else if(buffer[0x21]<4)
    write_byte(ebda_seg,&EbdaData->cdemu.emulated_drive,0x00);
  else
    write_byte(ebda_seg,&EbdaData->cdemu.emulated_drive,0x80);

  write_byte(ebda_seg,&EbdaData->cdemu.controller_index,device/2);
  write_byte(ebda_seg,&EbdaData->cdemu.device_spec,device%2);

  boot_segment=buffer[0x23]*0x100+buffer[0x22];
  if(boot_segment==0x0000)boot_segment=0x07C0;

  write_word(ebda_seg,&EbdaData->cdemu.load_segment,boot_segment);
  write_word(ebda_seg,&EbdaData->cdemu.buffer_segment,0x0000);
  
  nbsectors=buffer[0x27]*0x100+buffer[0x26];
  write_word(ebda_seg,&EbdaData->cdemu.sector_count,nbsectors);

  lba=buffer[0x2B]*0x1000000+buffer[0x2A]*0x10000+buffer[0x29]*0x100+buffer[0x28];
  write_dword(ebda_seg,&EbdaData->cdemu.ilba,lba);

  // And we read the image in memory
  memsetb(get_SS(),atacmd,0,12);
  atacmd[0]=0x28;                      // READ command
  atacmd[7]=((1+(nbsectors-1)/4) & 0xff00) >> 8;      // Sectors
  atacmd[8]=((1+(nbsectors-1)/4) & 0x00ff);           // Sectors
  atacmd[2]=(lba & 0xff000000) >> 24;  // LBA
  atacmd[3]=(lba & 0x00ff0000) >> 16;
  atacmd[4]=(lba & 0x0000ff00) >> 8;
  atacmd[5]=(lba & 0x000000ff);
  if((error = ata_cmd_packet(device, 12, get_SS(), atacmd, 0, nbsectors*512L, ATA_DATA_IN, boot_segment,0)) != 0)
    return 12;

  // Remember the media type
  switch(read_byte(ebda_seg,&EbdaData->cdemu.media)) {
    case 0x01:  // 1.2M floppy
      write_word(ebda_seg,&EbdaData->cdemu.vdevice.spt,15);
      write_word(ebda_seg,&EbdaData->cdemu.vdevice.cylinders,80);
      write_word(ebda_seg,&EbdaData->cdemu.vdevice.heads,2);
      break;
    case 0x02:  // 1.44M floppy
      write_word(ebda_seg,&EbdaData->cdemu.vdevice.spt,18);
      write_word(ebda_seg,&EbdaData->cdemu.vdevice.cylinders,80);
      write_word(ebda_seg,&EbdaData->cdemu.vdevice.heads,2);
      break;
    case 0x03:  // 2.88M floppy
      write_word(ebda_seg,&EbdaData->cdemu.vdevice.spt,36);
      write_word(ebda_seg,&EbdaData->cdemu.vdevice.cylinders,80);
      write_word(ebda_seg,&EbdaData->cdemu.vdevice.heads,2);
      break;
    case 0x04:  // Harddrive
      write_word(ebda_seg,&EbdaData->cdemu.vdevice.spt,read_byte(boot_segment,446+6)&0x3f);
      write_word(ebda_seg,&EbdaData->cdemu.vdevice.cylinders,
	      (read_byte(boot_segment,446+6)<<2) + read_byte(boot_segment,446+7) + 1);
      write_word(ebda_seg,&EbdaData->cdemu.vdevice.heads,read_byte(boot_segment,446+5) + 1);
      break;
   }

  if(read_byte(ebda_seg,&EbdaData->cdemu.media)!=0) {
    // Increase bios installed hardware number of devices
    if(read_byte(ebda_seg,&EbdaData->cdemu.emulated_drive)==0x00)
      write_byte(0x40,0x10,read_byte(0x40,0x10)|0x41);
    else
      write_byte(ebda_seg, &EbdaData->ata.hdcount, read_byte(ebda_seg, &EbdaData->ata.hdcount) + 1);
   }

  
  // everything is ok, so from now on, the emulation is active
  if(read_byte(ebda_seg,&EbdaData->cdemu.media)!=0)
    write_byte(ebda_seg,&EbdaData->cdemu.active,0x01);

  // return the boot drive + no error
  return (read_byte(ebda_seg,&EbdaData->cdemu.emulated_drive)*0x100)+0;
}

// ---------------------------------------------------------------------------
// End of El-Torito boot functions
// ---------------------------------------------------------------------------
#endif // BX_ELTORITO_BOOT

  void
int14_function(regs, ds, iret_addr)
  pusha_regs_t regs; // regs pushed from PUSHA instruction
  Bit16u ds; // previous DS:, DS set to 0x0000 by asm wrapper
  iret_addr_t  iret_addr; // CS,IP,Flags pushed from original INT call
{
  Bit16u addr,timer,val16;
  Bit8u timeout;

  ASM_START
  sti
  ASM_END

  addr = read_word(0x0040, (regs.u.r16.dx << 1));
  timeout = read_byte(0x0040, 0x007C + regs.u.r16.dx);
  if ((regs.u.r16.dx < 4) && (addr > 0)) {
    switch (regs.u.r8.ah) {
      case 0:
        outb(addr+3, inb(addr+3) | 0x80);
        if (regs.u.r8.al & 0xE0 == 0) {
          outb(addr, 0x17);
          outb(addr+1, 0x04);
        } else {
          val16 = 0x600 >> ((regs.u.r8.al & 0xE0) >> 5);
          outb(addr, val16 & 0xFF);
          outb(addr+1, val16 >> 8);
        }
        outb(addr+3, regs.u.r8.al & 0x1F);
        regs.u.r8.ah = inb(addr+5);
        regs.u.r8.al = inb(addr+6);
        ClearCF(iret_addr.flags);
        break;
      case 1:
        timer = read_word(0x0040, 0x006C);
        while (((inb(addr+5) & 0x60) != 0x60) && (timeout)) {
          val16 = read_word(0x0040, 0x006C);
          if (val16 != timer) {
            timer = val16;
            timeout--;
            }
          }
        if (timeout) outb(addr, regs.u.r8.al);
        regs.u.r8.ah = inb(addr+5);
        if (!timeout) regs.u.r8.ah |= 0x80;
        ClearCF(iret_addr.flags);
        break;
      case 2:
        timer = read_word(0x0040, 0x006C);
        while (((inb(addr+5) & 0x01) == 0) && (timeout)) {
          val16 = read_word(0x0040, 0x006C);
          if (val16 != timer) {
            timer = val16;
            timeout--;
            }
          }
        if (timeout) {
          regs.u.r8.ah = 0;
          regs.u.r8.al = inb(addr);
        } else {
          regs.u.r8.ah = inb(addr+5);
          }
        ClearCF(iret_addr.flags);
        break;
      case 3:
        regs.u.r8.ah = inb(addr+5);
        regs.u.r8.al = inb(addr+6);
        ClearCF(iret_addr.flags);
        break;
      default:
        SetCF(iret_addr.flags); // Unsupported
      }
  } else {
    SetCF(iret_addr.flags); // Unsupported
    }
}

  void
int15_function(regs, ES, DS, FLAGS)
  pusha_regs_t regs; // REGS pushed via pusha
  Bit16u ES, DS, FLAGS;
{
  Bit16u ebda_seg=read_word(0x0040,0x000E);
  bx_bool prev_a20_enable;
  Bit16u  base15_00;
  Bit8u   base23_16;
  Bit16u  ss;
  Bit16u  CX,DX;

  Bit16u bRegister;
  Bit8u irqDisable;

BX_DEBUG_INT15("int15 AX=%04x\n",regs.u.r16.ax);

  switch (regs.u.r8.ah) {
    case 0x24: /* A20 Control */
      switch (regs.u.r8.al) {
        case 0x00:
          set_enable_a20(0);
          CLEAR_CF();
          regs.u.r8.ah = 0;
          break;
        case 0x01:
          set_enable_a20(1);
          CLEAR_CF();
          regs.u.r8.ah = 0;
          break;
        case 0x02:
          regs.u.r8.al = (inb(0x92) >> 1) & 0x01;
          CLEAR_CF();
          regs.u.r8.ah = 0;
          break;
        case 0x03:
          CLEAR_CF();
          regs.u.r8.ah = 0;
          regs.u.r16.bx = 3;
          break;
        default:
          BX_INFO("int15: Func 24h, subfunc %02xh, A20 gate control not supported\n", (unsigned) regs.u.r8.al);
          SET_CF();
          regs.u.r8.ah = UNSUPPORTED_FUNCTION;
      }
      break;

    case 0x41:
      SET_CF();
      regs.u.r8.ah = UNSUPPORTED_FUNCTION;
      break;

    case 0x4f:
      /* keyboard intercept */
#if BX_CPU < 2
      regs.u.r8.ah = UNSUPPORTED_FUNCTION;
#else
      // nop
#endif
      SET_CF();
      break;

    case 0x52:    // removable media eject
      CLEAR_CF();
      regs.u.r8.ah = 0;  // "ok ejection may proceed"
      break;

    case 0x83: {
      if( regs.u.r8.al == 0 ) {
        // Set Interval requested.
        if( ( read_byte( 0x40, 0xA0 ) & 1 ) == 0 ) {
          // Interval not already set.
          write_byte( 0x40, 0xA0, 1 );  // Set status byte.
          write_word( 0x40, 0x98, ES ); // Byte location, segment
          write_word( 0x40, 0x9A, regs.u.r16.bx ); // Byte location, offset
          write_word( 0x40, 0x9C, regs.u.r16.dx ); // Low word, delay
          write_word( 0x40, 0x9E, regs.u.r16.cx ); // High word, delay.
          CLEAR_CF( );
          irqDisable = inb( 0xA1 );
          outb( 0xA1, irqDisable & 0xFE );
          bRegister = inb_cmos( 0xB );  // Unmask IRQ8 so INT70 will get through.
          outb_cmos( 0xB, bRegister | 0x40 ); // Turn on the Periodic Interrupt timer
        } else {
          // Interval already set.
          BX_DEBUG_INT15("int15: Func 83h, failed, already waiting.\n" );
          SET_CF();
          regs.u.r8.ah = UNSUPPORTED_FUNCTION;
        }
      } else if( regs.u.r8.al == 1 ) {
        // Clear Interval requested
        write_byte( 0x40, 0xA0, 0 );  // Clear status byte
        CLEAR_CF( );
        bRegister = inb_cmos( 0xB );
        outb_cmos( 0xB, bRegister & ~0x40 );  // Turn off the Periodic Interrupt timer
      } else {
        BX_DEBUG_INT15("int15: Func 83h, failed.\n" );
        SET_CF();
        regs.u.r8.ah = UNSUPPORTED_FUNCTION;
        regs.u.r8.al--;
      }

      break;
    }

    case 0x87:
#if BX_CPU < 3
#  error "Int15 function 87h not supported on < 80386"
#endif
      // +++ should probably have descriptor checks
      // +++ should have exception handlers

 // turn off interrupts
ASM_START
  cli
ASM_END

      prev_a20_enable = set_enable_a20(1); // enable A20 line

      // 128K max of transfer on 386+ ???
      // source == destination ???

      // ES:SI points to descriptor table
      // offset   use     initially  comments
      // ==============================================
      // 00..07   Unused  zeros      Null descriptor
      // 08..0f   GDT     zeros      filled in by BIOS
      // 10..17   source  ssssssss   source of data
      // 18..1f   dest    dddddddd   destination of data
      // 20..27   CS      zeros      filled in by BIOS
      // 28..2f   SS      zeros      filled in by BIOS

      //es:si
      //eeee0
      //0ssss
      //-----

// check for access rights of source & dest here

      // Initialize GDT descriptor
      base15_00 = (ES << 4) + regs.u.r16.si;
      base23_16 = ES >> 12;
      if (base15_00 < (ES<<4))
        base23_16++;
      write_word(ES, regs.u.r16.si+0x08+0, 47);       // limit 15:00 = 6 * 8bytes/descriptor
      write_word(ES, regs.u.r16.si+0x08+2, base15_00);// base 15:00
      write_byte(ES, regs.u.r16.si+0x08+4, base23_16);// base 23:16
      write_byte(ES, regs.u.r16.si+0x08+5, 0x93);     // access
      write_word(ES, regs.u.r16.si+0x08+6, 0x0000);   // base 31:24/reserved/limit 19:16

      // Initialize CS descriptor
      write_word(ES, regs.u.r16.si+0x20+0, 0xffff);// limit 15:00 = normal 64K limit
      write_word(ES, regs.u.r16.si+0x20+2, 0x0000);// base 15:00
      write_byte(ES, regs.u.r16.si+0x20+4, 0x000f);// base 23:16
      write_byte(ES, regs.u.r16.si+0x20+5, 0x9b);  // access
      write_word(ES, regs.u.r16.si+0x20+6, 0x0000);// base 31:24/reserved/limit 19:16

      // Initialize SS descriptor
      ss = get_SS();
      base15_00 = ss << 4;
      base23_16 = ss >> 12;
      write_word(ES, regs.u.r16.si+0x28+0, 0xffff);   // limit 15:00 = normal 64K limit
      write_word(ES, regs.u.r16.si+0x28+2, base15_00);// base 15:00
      write_byte(ES, regs.u.r16.si+0x28+4, base23_16);// base 23:16
      write_byte(ES, regs.u.r16.si+0x28+5, 0x93);     // access
      write_word(ES, regs.u.r16.si+0x28+6, 0x0000);   // base 31:24/reserved/limit 19:16

      CX = regs.u.r16.cx;
ASM_START
      // Compile generates locals offset info relative to SP.
      // Get CX (word count) from stack.
      mov  bx, sp
      SEG SS
        mov  cx, _int15_function.CX [bx]

      // since we need to set SS:SP, save them to the BDA
      // for future restore
      push eax
      xor eax, eax
      mov ds, ax
      mov 0x0469, ss
      mov 0x0467, sp

      SEG ES
        lgdt [si + 0x08]
      SEG CS
        lidt [pmode_IDT_info]
      ;;  perhaps do something with IDT here

      ;; set PE bit in CR0
      mov  eax, cr0
      or   al, #0x01
      mov  cr0, eax
      ;; far jump to flush CPU queue after transition to protected mode
      JMP_AP(0x0020, protected_mode)

protected_mode:
      ;; GDT points to valid descriptor table, now load SS, DS, ES
      mov  ax, #0x28 ;; 101 000 = 5th descriptor in table, TI=GDT, RPL=00
      mov  ss, ax
      mov  ax, #0x10 ;; 010 000 = 2nd descriptor in table, TI=GDT, RPL=00
      mov  ds, ax
      mov  ax, #0x18 ;; 011 000 = 3rd descriptor in table, TI=GDT, RPL=00
      mov  es, ax
      xor  si, si
      xor  di, di
      cld
      rep
        movsw  ;; move CX words from DS:SI to ES:DI

      ;; make sure DS and ES limits are 64KB
      mov ax, #0x28
      mov ds, ax
      mov es, ax

      ;; reset PG bit in CR0 ???
      mov  eax, cr0
      and  al, #0xFE
      mov  cr0, eax

      ;; far jump to flush CPU queue after transition to real mode
      JMP_AP(0xf000, real_mode)

real_mode:
      ;; restore IDT to normal real-mode defaults
      SEG CS
        lidt [rmode_IDT_info]

      // restore SS:SP from the BDA
      xor ax, ax
      mov ds, ax
      mov ss, 0x0469
      mov sp, 0x0467
      pop eax
ASM_END

      set_enable_a20(prev_a20_enable);

 // turn back on interrupts
ASM_START
  sti
ASM_END

      regs.u.r8.ah = 0;
      CLEAR_CF();
      break;


    case 0x88:
      // Get the amount of extended memory (above 1M)
#if BX_CPU < 2
      regs.u.r8.ah = UNSUPPORTED_FUNCTION;
      SET_CF();
#else
      regs.u.r8.al = inb_cmos(0x30);
      regs.u.r8.ah = inb_cmos(0x31);

      // According to Ralf Brown's interrupt the limit should be 15M,
      // but real machines mostly return max. 63M.
      if(regs.u.r16.ax > 0xffc0)
        regs.u.r16.ax = 0xffc0;

      CLEAR_CF();
#endif
      break;

    case 0x90:
      /* Device busy interrupt.  Called by Int 16h when no key available */
      break;

    case 0x91:
      /* Interrupt complete.  Called by Int 16h when key becomes available */
      break;

    case 0xbf:
      BX_INFO("*** int 15h function AH=bf not yet supported!\n");
      SET_CF();
      regs.u.r8.ah = UNSUPPORTED_FUNCTION;
      break;

    case 0xC0:
#if 0
      SET_CF();
      regs.u.r8.ah = UNSUPPORTED_FUNCTION;
      break;
#endif
      CLEAR_CF();
      regs.u.r8.ah = 0;
      regs.u.r16.bx =  BIOS_CONFIG_TABLE;
      ES = 0xF000;
      break;

    case 0xc1:
      ES = ebda_seg;
      CLEAR_CF();
      break;

    case 0xd8:
      bios_printf(BIOS_PRINTF_DEBUG, "EISA BIOS not present\n");
      SET_CF();
      regs.u.r8.ah = UNSUPPORTED_FUNCTION;
      break;

    default:
      BX_INFO("*** int 15h function AX=%04x, BX=%04x not yet supported!\n",
        (unsigned) regs.u.r16.ax, (unsigned) regs.u.r16.bx);
      SET_CF();
      regs.u.r8.ah = UNSUPPORTED_FUNCTION;
      break;
    }
}

#if BX_USE_PS2_MOUSE
  void
int15_function_mouse(regs, ES, DS, FLAGS)
  pusha_regs_t regs; // REGS pushed via pusha
  Bit16u ES, DS, FLAGS;
{
  Bit16u ebda_seg=read_word(0x0040,0x000E);
  Bit8u  mouse_flags_1, mouse_flags_2;
  Bit16u mouse_driver_seg;
  Bit16u mouse_driver_offset;
  Bit8u  comm_byte, prev_command_byte;
  Bit8u  ret, mouse_data1, mouse_data2, mouse_data3;

BX_DEBUG_INT15("int15 AX=%04x\n",regs.u.r16.ax);

  switch (regs.u.r8.ah) {
    case 0xC2:
      // Return Codes status in AH
      // =========================
      // 00: success
      // 01: invalid subfunction (AL > 7)
      // 02: invalid input value (out of allowable range)
      // 03: interface error
      // 04: resend command received from mouse controller,
      //     device driver should attempt command again
      // 05: cannot enable mouse, since no far call has been installed
      // 80/86: mouse service not implemented

      switch (regs.u.r8.al) {
        case 0: // Disable/Enable Mouse
BX_DEBUG_INT15("case 0:\n");
          switch (regs.u.r8.bh) {
            case 0: // Disable Mouse
BX_DEBUG_INT15("case 0: disable mouse\n");
              inhibit_mouse_int_and_events(); // disable IRQ12 and packets
              ret = send_to_mouse_ctrl(0xF5); // disable mouse command
              if (ret == 0) {
                ret = get_mouse_data(&mouse_data1);
                if ( (ret == 0) || (mouse_data1 == 0xFA) ) {
                  CLEAR_CF();
                  regs.u.r8.ah = 0;
                  return;
                  }
                }

              // error
              SET_CF();
              regs.u.r8.ah = ret;
              return;
              break;

            case 1: // Enable Mouse
BX_DEBUG_INT15("case 1: enable mouse\n");
              mouse_flags_2 = read_byte(ebda_seg, 0x0027);
              if ( (mouse_flags_2 & 0x80) == 0 ) {
                BX_DEBUG_INT15("INT 15h C2 Enable Mouse, no far call handler\n");
                SET_CF();  // error
                regs.u.r8.ah = 5; // no far call installed
                return;
                }
              inhibit_mouse_int_and_events(); // disable IRQ12 and packets
              ret = send_to_mouse_ctrl(0xF4); // enable mouse command
              if (ret == 0) {
                ret = get_mouse_data(&mouse_data1);
                if ( (ret == 0) && (mouse_data1 == 0xFA) ) {
                  enable_mouse_int_and_events(); // turn IRQ12 and packet generation on
                  CLEAR_CF();
                  regs.u.r8.ah = 0;
                  return;
                  }
                }
              SET_CF();
              regs.u.r8.ah = ret;
              return;

            default: // invalid subfunction
              BX_DEBUG_INT15("INT 15h C2 AL=0, BH=%02x\n", (unsigned) regs.u.r8.bh);
              SET_CF();  // error
              regs.u.r8.ah = 1; // invalid subfunction
              return;
            }
          break;

        case 1: // Reset Mouse
        case 5: // Initialize Mouse
BX_DEBUG_INT15("case 1 or 5:\n");
          if (regs.u.r8.al == 5) {
            if (regs.u.r8.bh != 3) {
              SET_CF();
              regs.u.r8.ah = 0x02; // invalid input
              return;
            }
            mouse_flags_2 = read_byte(ebda_seg, 0x0027);
            mouse_flags_2 = (mouse_flags_2 & 0x00) | regs.u.r8.bh;
            mouse_flags_1 = 0x00;
            write_byte(ebda_seg, 0x0026, mouse_flags_1);
            write_byte(ebda_seg, 0x0027, mouse_flags_2);
          }

          inhibit_mouse_int_and_events(); // disable IRQ12 and packets
          ret = send_to_mouse_ctrl(0xFF); // reset mouse command
          if (ret == 0) {
            ret = get_mouse_data(&mouse_data3);
            // if no mouse attached, it will return RESEND
            if (mouse_data3 == 0xfe) {
              SET_CF();
              return;
            }
            if (mouse_data3 != 0xfa)
              BX_PANIC("Mouse reset returned %02x (should be ack)\n", (unsigned)mouse_data3);
            if ( ret == 0 ) {
              ret = get_mouse_data(&mouse_data1);
              if ( ret == 0 ) {
                ret = get_mouse_data(&mouse_data2);
                if ( ret == 0 ) {
                  // turn IRQ12 and packet generation on
                  enable_mouse_int_and_events();
                  CLEAR_CF();
                  regs.u.r8.ah = 0;
                  regs.u.r8.bl = mouse_data1;
                  regs.u.r8.bh = mouse_data2;
                  return;
                  }
                }
              }
            }

          // error
          SET_CF();
          regs.u.r8.ah = ret;
          return;

        case 2: // Set Sample Rate
BX_DEBUG_INT15("case 2:\n");
          switch (regs.u.r8.bh) {
            case 0: mouse_data1 = 10; break; //  10 reports/sec
            case 1: mouse_data1 = 20; break; //  20 reports/sec
            case 2: mouse_data1 = 40; break; //  40 reports/sec
            case 3: mouse_data1 = 60; break; //  60 reports/sec
            case 4: mouse_data1 = 80; break; //  80 reports/sec
            case 5: mouse_data1 = 100; break; // 100 reports/sec (default)
            case 6: mouse_data1 = 200; break; // 200 reports/sec
            default: mouse_data1 = 0;
          }
          if (mouse_data1 > 0) {
            ret = send_to_mouse_ctrl(0xF3); // set sample rate command
            if (ret == 0) {
              ret = get_mouse_data(&mouse_data2);
              ret = send_to_mouse_ctrl(mouse_data1);
              ret = get_mouse_data(&mouse_data2);
              CLEAR_CF();
              regs.u.r8.ah = 0;
            } else {
              // error
              SET_CF();
              regs.u.r8.ah = UNSUPPORTED_FUNCTION;
            }
          } else {
            // error
            SET_CF();
            regs.u.r8.ah = UNSUPPORTED_FUNCTION;
          }
          break;

        case 3: // Set Resolution
BX_DEBUG_INT15("case 3:\n");
          // BX:
          //      0 =  25 dpi, 1 count  per millimeter
          //      1 =  50 dpi, 2 counts per millimeter
          //      2 = 100 dpi, 4 counts per millimeter
          //      3 = 200 dpi, 8 counts per millimeter
          CLEAR_CF();
          regs.u.r8.ah = 0;
          break;

        case 4: // Get Device ID
BX_DEBUG_INT15("case 4:\n");
          inhibit_mouse_int_and_events(); // disable IRQ12 and packets
          ret = send_to_mouse_ctrl(0xF2); // get mouse ID command
          if (ret == 0) {
            ret = get_mouse_data(&mouse_data1);
            ret = get_mouse_data(&mouse_data2);
            CLEAR_CF();
            regs.u.r8.ah = 0;
            regs.u.r8.bh = mouse_data2;
          } else {
            // error
            SET_CF();
            regs.u.r8.ah = UNSUPPORTED_FUNCTION;
          }
          break;

        case 6: // Return Status & Set Scaling Factor...
BX_DEBUG_INT15("case 6:\n");
          switch (regs.u.r8.bh) {
            case 0: // Return Status
              comm_byte = inhibit_mouse_int_and_events(); // disable IRQ12 and packets
              ret = send_to_mouse_ctrl(0xE9); // get mouse info command
              if (ret == 0) {
                ret = get_mouse_data(&mouse_data1);
                if (mouse_data1 != 0xfa)
                  BX_PANIC("Mouse status returned %02x (should be ack)\n", (unsigned)mouse_data1);
                if (ret == 0) {
                  ret = get_mouse_data(&mouse_data1);
                  if ( ret == 0 ) {
                    ret = get_mouse_data(&mouse_data2);
                    if ( ret == 0 ) {
                      ret = get_mouse_data(&mouse_data3);
                      if ( ret == 0 ) {
                        CLEAR_CF();
                        regs.u.r8.ah = 0;
                        regs.u.r8.bl = mouse_data1;
                        regs.u.r8.cl = mouse_data2;
                        regs.u.r8.dl = mouse_data3;
                        set_kbd_command_byte(comm_byte); // restore IRQ12 and serial enable
                        return;
                        }
                      }
                    }
                  }
                }

              // error
              SET_CF();
              regs.u.r8.ah = ret;
              set_kbd_command_byte(comm_byte); // restore IRQ12 and serial enable
              return;

            case 1: // Set Scaling Factor to 1:1
            case 2: // Set Scaling Factor to 2:1
              comm_byte = inhibit_mouse_int_and_events(); // disable IRQ12 and packets
              if (regs.u.r8.bh == 1) {
                ret = send_to_mouse_ctrl(0xE6);
              } else {
                ret = send_to_mouse_ctrl(0xE7);
              }
              if (ret == 0) {
                get_mouse_data(&mouse_data1);
                ret = (mouse_data1 != 0xFA);
              }
              if (ret == 0) {
                CLEAR_CF();
                regs.u.r8.ah = 0;
              } else {
                // error
                SET_CF();
                regs.u.r8.ah = UNSUPPORTED_FUNCTION;
              }
              set_kbd_command_byte(comm_byte); // restore IRQ12 and serial enable
              break;

            default:
              BX_PANIC("INT 15h C2 AL=6, BH=%02x\n", (unsigned) regs.u.r8.bh);
            }
          break;

        case 7: // Set Mouse Handler Address
BX_DEBUG_INT15("case 7:\n");
          mouse_driver_seg = ES;
          mouse_driver_offset = regs.u.r16.bx;
          write_word(ebda_seg, 0x0022, mouse_driver_offset);
          write_word(ebda_seg, 0x0024, mouse_driver_seg);
          mouse_flags_2 = read_byte(ebda_seg, 0x0027);
          if (mouse_driver_offset == 0 && mouse_driver_seg == 0) {
            /* remove handler */
            if ( (mouse_flags_2 & 0x80) != 0 ) {
              mouse_flags_2 &= ~0x80;
              inhibit_mouse_int_and_events(); // disable IRQ12 and packets
              }
            }
          else {
            /* install handler */
            mouse_flags_2 |= 0x80;
            }
          write_byte(ebda_seg, 0x0027, mouse_flags_2);
          CLEAR_CF();
          regs.u.r8.ah = 0;
          break;

        default:
BX_DEBUG_INT15("case default:\n");
          regs.u.r8.ah = 1; // invalid function
          SET_CF();
        }
      break;

    default:
      BX_INFO("*** int 15h function AX=%04x, BX=%04x not yet supported!\n",
        (unsigned) regs.u.r16.ax, (unsigned) regs.u.r16.bx);
      SET_CF();
      regs.u.r8.ah = UNSUPPORTED_FUNCTION;
      break;
    }
}
#endif


void set_e820_range(ES, DI, start, end, type)
     Bit16u ES; 
     Bit16u DI;
     Bit32u start;
     Bit32u end; 
     Bit16u type;
{
    write_word(ES, DI, start);
    write_word(ES, DI+2, start >> 16);
    write_word(ES, DI+4, 0x00);
    write_word(ES, DI+6, 0x00);
    
    end -= start;
    write_word(ES, DI+8, end);
    write_word(ES, DI+10, end >> 16);
    write_word(ES, DI+12, 0x0000);
    write_word(ES, DI+14, 0x0000);
    
    write_word(ES, DI+16, type);
    write_word(ES, DI+18, 0x0);
}

  void
int15_function32(regs, ES, DS, FLAGS)
  pushad_regs_t regs; // REGS pushed via pushad
  Bit16u ES, DS, FLAGS;
{
  Bit32u  extended_memory_size=0; // 64bits long
  Bit16u  CX,DX;

BX_DEBUG_INT15("int15 AX=%04x\n",regs.u.r16.ax);

  switch (regs.u.r8.ah) {
    case 0x86:
      // Wait for CX:DX microseconds. currently using the 
      // refresh request port 0x61 bit4, toggling every 15usec 

      CX = regs.u.r16.cx;
      DX = regs.u.r16.dx;

ASM_START
      sti

      ;; Get the count in eax
      mov  bx, sp
      SEG SS
        mov  ax, _int15_function.CX [bx]
      shl  eax, #16
      SEG SS
        mov  ax, _int15_function.DX [bx]

      ;; convert to numbers of 15usec ticks
      mov ebx, #15
      xor edx, edx
      div eax, ebx
      mov ecx, eax

      ;; wait for ecx number of refresh requests
      in al, #0x61
      and al,#0x10
      mov ah, al

      or ecx, ecx
      je int1586_tick_end
int1586_tick:
      in al, #0x61
      and al,#0x10
      cmp al, ah
      je  int1586_tick
      mov ah, al
      dec ecx
      jnz int1586_tick
int1586_tick_end:
ASM_END

      break;

    case 0xe8:
        switch(regs.u.r8.al)
        {
         case 0x20: // coded by osmaker aka K.J.
            if(regs.u.r32.edx == 0x534D4150)
            {
                extended_memory_size = inb_cmos(0x35);
                extended_memory_size <<= 8;
                extended_memory_size |= inb_cmos(0x34);
                extended_memory_size *= 64;
                // greater than EFF00000???
                if(extended_memory_size > 0x3bc000) {
                    extended_memory_size = 0x3bc000; // everything after this is reserved memory until we get to 0x100000000
                }
                extended_memory_size *= 1024;
                extended_memory_size += (16L * 1024 * 1024);
                
                if(extended_memory_size <= (16L * 1024 * 1024)) {
                    extended_memory_size = inb_cmos(0x31);
                    extended_memory_size <<= 8;
                    extended_memory_size |= inb_cmos(0x30);
                    extended_memory_size *= 1024;
                }

                switch(regs.u.r16.bx)
                {
                    case 0:
                        set_e820_range(ES, regs.u.r16.di, 
                                       0x0000000L, 0x0009fc00L, 1);
                        regs.u.r32.ebx = 1;
                        regs.u.r32.eax = 0x534D4150;
                        regs.u.r32.ecx = 0x14;
                        CLEAR_CF();
                        return;
                        break;
                    case 1:
                        set_e820_range(ES, regs.u.r16.di, 
                                       0x0009fc00L, 0x000a0000L, 2);
                        regs.u.r32.ebx = 2;
                        regs.u.r32.eax = 0x534D4150;
                        regs.u.r32.ecx = 0x14;
                        CLEAR_CF();
                        return;
                        break;
                    case 2:
                        set_e820_range(ES, regs.u.r16.di, 
                                       0x000e8000L, 0x00100000L, 2);
                        regs.u.r32.ebx = 3;
                        regs.u.r32.eax = 0x534D4150;
                        regs.u.r32.ecx = 0x14;
                        CLEAR_CF();
                        return;
                        break;
                    case 3:
                        set_e820_range(ES, regs.u.r16.di, 
                                       0x00100000L, 
                                       extended_memory_size - ACPI_DATA_SIZE, 1);
                        regs.u.r32.ebx = 4;
                        regs.u.r32.eax = 0x534D4150;
                        regs.u.r32.ecx = 0x14;
                        CLEAR_CF();
                        return;
                        break;
                    case 4:
                        set_e820_range(ES, regs.u.r16.di, 
                                       extended_memory_size - ACPI_DATA_SIZE, 
                                       extended_memory_size, 3); // ACPI RAM
                        regs.u.r32.ebx = 5;
                        regs.u.r32.eax = 0x534D4150;
                        regs.u.r32.ecx = 0x14;
                        CLEAR_CF();
                        return;
                        break;
                    case 5:
                        /* 256KB BIOS area at the end of 4 GB */
                        set_e820_range(ES, regs.u.r16.di, 
                                       0xfffc0000L, 0x00000000L, 2);
                        regs.u.r32.ebx = 0;
                        regs.u.r32.eax = 0x534D4150;
                        regs.u.r32.ecx = 0x14;
                        CLEAR_CF();
                        return;
                    default:  /* AX=E820, DX=534D4150, BX unrecognized */
                        goto int15_unimplemented;
                        break;
                }
	    } else {
	      // if DX != 0x534D4150)
	      goto int15_unimplemented;
	    }
            break;

        case 0x01: 
          // do we have any reason to fail here ?
          CLEAR_CF();

          // my real system sets ax and bx to 0
          // this is confirmed by Ralph Brown list
          // but syslinux v1.48 is known to behave 
          // strangely if ax is set to 0
          // regs.u.r16.ax = 0;
          // regs.u.r16.bx = 0;

          // Get the amount of extended memory (above 1M)
          regs.u.r8.cl = inb_cmos(0x30);
          regs.u.r8.ch = inb_cmos(0x31);
          
          // limit to 15M
          if(regs.u.r16.cx > 0x3c00)
          {
            regs.u.r16.cx = 0x3c00;
          }

          // Get the amount of extended memory above 16M in 64k blocs
          regs.u.r8.dl = inb_cmos(0x34);
          regs.u.r8.dh = inb_cmos(0x35);

          // Set configured memory equal to extended memory
          regs.u.r16.ax = regs.u.r16.cx;
          regs.u.r16.bx = regs.u.r16.dx;
          break;
	default:  /* AH=0xE8?? but not implemented */
	  goto int15_unimplemented;
       }
       break;
    int15_unimplemented:
       // fall into the default
    default:
      BX_INFO("*** int 15h function AX=%04x, BX=%04x not yet supported!\n",
        (unsigned) regs.u.r16.ax, (unsigned) regs.u.r16.bx);
      SET_CF();
      regs.u.r8.ah = UNSUPPORTED_FUNCTION;
      break;
    }
}

  void
int16_function(DI, SI, BP, SP, BX, DX, CX, AX, FLAGS)
  Bit16u DI, SI, BP, SP, BX, DX, CX, AX, FLAGS;
{
  Bit8u scan_code, ascii_code, shift_flags, led_flags, count;
  Bit16u kbd_code, max;

  BX_DEBUG_INT16("int16: AX=%04x BX=%04x CX=%04x DX=%04x \n", AX, BX, CX, DX);

  shift_flags = read_byte(0x0040, 0x17);
  led_flags = read_byte(0x0040, 0x97);
  if ((((shift_flags >> 4) & 0x07) ^ (led_flags & 0x07)) != 0) {
ASM_START
    cli
ASM_END
    outb(0x60, 0xed);
    while ((inb(0x64) & 0x01) == 0) outb(0x80, 0x21);
    if ((inb(0x60) == 0xfa)) {
      led_flags &= 0xf8;
      led_flags |= ((shift_flags >> 4) & 0x07);
      outb(0x60, led_flags & 0x07);
      while ((inb(0x64) & 0x01) == 0) outb(0x80, 0x21);
      inb(0x60);
      write_byte(0x0040, 0x97, led_flags);
    }
ASM_START
    sti
ASM_END
  }

  switch (GET_AH()) {
    case 0x00: /* read keyboard input */

      if ( !dequeue_key(&scan_code, &ascii_code, 1) ) {
        BX_PANIC("KBD: int16h: out of keyboard input\n");
        }
      if (scan_code !=0 && ascii_code == 0xF0) ascii_code = 0;
      else if (ascii_code == 0xE0) ascii_code = 0;
      AX = (scan_code << 8) | ascii_code;
      break;

    case 0x01: /* check keyboard status */
      if ( !dequeue_key(&scan_code, &ascii_code, 0) ) {
        SET_ZF();
        return;
        }
      if (scan_code !=0 && ascii_code == 0xF0) ascii_code = 0;
      else if (ascii_code == 0xE0) ascii_code = 0;
      AX = (scan_code << 8) | ascii_code;
      CLEAR_ZF();
      break;

    case 0x02: /* get shift flag status */
      shift_flags = read_byte(0x0040, 0x17);
      SET_AL(shift_flags);
      break;

    case 0x05: /* store key-stroke into buffer */
      if ( !enqueue_key(GET_CH(), GET_CL()) ) {
        SET_AL(1);
        }
      else {
        SET_AL(0);
        }
      break;

    case 0x09: /* GET KEYBOARD FUNCTIONALITY */
      // bit Bochs Description     
      //  7    0   reserved
      //  6    0   INT 16/AH=20h-22h supported (122-key keyboard support)
      //  5    1   INT 16/AH=10h-12h supported (enhanced keyboard support)
      //  4    1   INT 16/AH=0Ah supported
      //  3    0   INT 16/AX=0306h supported
      //  2    0   INT 16/AX=0305h supported
      //  1    0   INT 16/AX=0304h supported
      //  0    0   INT 16/AX=0300h supported
      //
      SET_AL(0x30);
      break;

    case 0x0A: /* GET KEYBOARD ID */
      count = 2;
      kbd_code = 0x0;
      outb(0x60, 0xf2);
      /* Wait for data */
      max=0xffff;
      while ( ((inb(0x64) & 0x01) == 0) && (--max>0) ) outb(0x80, 0x00);
      if (max>0x0) {
        if ((inb(0x60) == 0xfa)) {
          do {
            max=0xffff;
            while ( ((inb(0x64) & 0x01) == 0) && (--max>0) ) outb(0x80, 0x00);
            if (max>0x0) {
              kbd_code >>= 8;
              kbd_code |= (inb(0x60) << 8);
            }
          } while (--count>0);
	}
      }
      BX=kbd_code;
      break;

    case 0x10: /* read MF-II keyboard input */

      if ( !dequeue_key(&scan_code, &ascii_code, 1) ) {
        BX_PANIC("KBD: int16h: out of keyboard input\n");
        }
      if (scan_code !=0 && ascii_code == 0xF0) ascii_code = 0;
      AX = (scan_code << 8) | ascii_code;
      break;

    case 0x11: /* check MF-II keyboard status */
      if ( !dequeue_key(&scan_code, &ascii_code, 0) ) {
        SET_ZF();
        return;
        }
      if (scan_code !=0 && ascii_code == 0xF0) ascii_code = 0;
      AX = (scan_code << 8) | ascii_code;
      CLEAR_ZF();
      break;

    case 0x12: /* get extended keyboard status */
      shift_flags = read_byte(0x0040, 0x17);
      SET_AL(shift_flags);
      shift_flags = read_byte(0x0040, 0x18) & 0x73;
      shift_flags |= read_byte(0x0040, 0x96) & 0x0c;
      SET_AH(shift_flags);
      BX_DEBUG_INT16("int16: func 12 sending %04x\n",AX);
      break;

    case 0x92: /* keyboard capability check called by DOS 5.0+ keyb */
      SET_AH(0x80); // function int16 ah=0x10-0x12 supported
      break;

    case 0xA2: /* 122 keys capability check called by DOS 5.0+ keyb */
      // don't change AH : function int16 ah=0x20-0x22 NOT supported
      break;

    case 0x6F:
      if (GET_AL() == 0x08)
	SET_AH(0x02); // unsupported, aka normal keyboard

    default:
      BX_INFO("KBD: unsupported int 16h function %02x\n", GET_AH());
    }
}

  unsigned int
dequeue_key(scan_code, ascii_code, incr)
  Bit8u *scan_code;
  Bit8u *ascii_code;
  unsigned int incr;
{
  Bit16u buffer_start, buffer_end, buffer_head, buffer_tail;
  Bit16u ss;
  Bit8u  acode, scode;

#if BX_CPU < 2
  buffer_start = 0x001E;
  buffer_end   = 0x003E;
#else
  buffer_start = read_word(0x0040, 0x0080);
  buffer_end   = read_word(0x0040, 0x0082);
#endif

  buffer_head = read_word(0x0040, 0x001a);
  buffer_tail = read_word(0x0040, 0x001c);

  if (buffer_head != buffer_tail) {
    ss = get_SS();
    acode = read_byte(0x0040, buffer_head);
    scode = read_byte(0x0040, buffer_head+1);
    write_byte(ss, ascii_code, acode);
    write_byte(ss, scan_code, scode);

    if (incr) {
      buffer_head += 2;
      if (buffer_head >= buffer_end)
        buffer_head = buffer_start;
      write_word(0x0040, 0x001a, buffer_head);
      }
    return(1);
    }
  else {
    return(0);
    }
}

static char panic_msg_keyb_buffer_full[] = "%s: keyboard input buffer full\n";

  Bit8u
inhibit_mouse_int_and_events()
{
  Bit8u command_byte, prev_command_byte;

  // Turn off IRQ generation and aux data line
  if ( inb(0x64) & 0x02 )
    BX_PANIC(panic_msg_keyb_buffer_full,"inhibmouse");
  outb(0x64, 0x20); // get command byte
  while ( (inb(0x64) & 0x01) != 0x01 );
  prev_command_byte = inb(0x60);
  command_byte = prev_command_byte;
  //while ( (inb(0x64) & 0x02) );
  if ( inb(0x64) & 0x02 )
    BX_PANIC(panic_msg_keyb_buffer_full,"inhibmouse");
  command_byte &= 0xfd; // turn off IRQ 12 generation
  command_byte |= 0x20; // disable mouse serial clock line
  outb(0x64, 0x60); // write command byte
  outb(0x60, command_byte);
  return(prev_command_byte);
}

  void
enable_mouse_int_and_events()
{
  Bit8u command_byte;

  // Turn on IRQ generation and aux data line
  if ( inb(0x64) & 0x02 )
    BX_PANIC(panic_msg_keyb_buffer_full,"enabmouse");
  outb(0x64, 0x20); // get command byte
  while ( (inb(0x64) & 0x01) != 0x01 );
  command_byte = inb(0x60);
  //while ( (inb(0x64) & 0x02) );
  if ( inb(0x64) & 0x02 )
    BX_PANIC(panic_msg_keyb_buffer_full,"enabmouse");
  command_byte |= 0x02; // turn on IRQ 12 generation
  command_byte &= 0xdf; // enable mouse serial clock line
  outb(0x64, 0x60); // write command byte
  outb(0x60, command_byte);
}

  Bit8u
send_to_mouse_ctrl(sendbyte)
  Bit8u sendbyte;
{
  Bit8u response;

  // wait for chance to write to ctrl
  if ( inb(0x64) & 0x02 )
    BX_PANIC(panic_msg_keyb_buffer_full,"sendmouse");
  outb(0x64, 0xD4);
  outb(0x60, sendbyte);
  return(0);
}


  Bit8u
get_mouse_data(data)
  Bit8u *data;
{
  Bit8u response;
  Bit16u ss;

  while ( (inb(0x64) & 0x21) != 0x21 ) {
    }

  response = inb(0x60);

  ss = get_SS();
  write_byte(ss, data, response);
  return(0);
}

  void
set_kbd_command_byte(command_byte)
  Bit8u command_byte;
{
  if ( inb(0x64) & 0x02 )
    BX_PANIC(panic_msg_keyb_buffer_full,"setkbdcomm");
  outb(0x64, 0xD4);

  outb(0x64, 0x60); // write command byte
  outb(0x60, command_byte);
}

  void
int09_function(DI, SI, BP, SP, BX, DX, CX, AX)
  Bit16u DI, SI, BP, SP, BX, DX, CX, AX;
{
  Bit8u scancode, asciicode, shift_flags;
  Bit8u mf2_flags, mf2_state;

  //
  // DS has been set to F000 before call
  //


  scancode = GET_AL();

  if (scancode == 0) {
    BX_INFO("KBD: int09 handler: AL=0\n");
    return;
    }


  shift_flags = read_byte(0x0040, 0x17);
  mf2_flags = read_byte(0x0040, 0x18);
  mf2_state = read_byte(0x0040, 0x96);
  asciicode = 0;

  switch (scancode) {
    case 0x3a: /* Caps Lock press */
      shift_flags ^= 0x40;
      write_byte(0x0040, 0x17, shift_flags);
      mf2_flags |= 0x40;
      write_byte(0x0040, 0x18, mf2_flags);
      break;
    case 0xba: /* Caps Lock release */
      mf2_flags &= ~0x40;
      write_byte(0x0040, 0x18, mf2_flags);
      break;

    case 0x2a: /* L Shift press */
      shift_flags |= 0x02;
      write_byte(0x0040, 0x17, shift_flags);
      break;
    case 0xaa: /* L Shift release */
      shift_flags &= ~0x02;
      write_byte(0x0040, 0x17, shift_flags);
      break;

    case 0x36: /* R Shift press */
      shift_flags |= 0x01;
      write_byte(0x0040, 0x17, shift_flags);
      break;
    case 0xb6: /* R Shift release */
      shift_flags &= ~0x01;
      write_byte(0x0040, 0x17, shift_flags);
      break;

    case 0x1d: /* Ctrl press */
      if ((mf2_state & 0x01) == 0) {
        shift_flags |= 0x04;
        write_byte(0x0040, 0x17, shift_flags);
        if (mf2_state & 0x02) {
          mf2_state |= 0x04;
          write_byte(0x0040, 0x96, mf2_state);
        } else {
          mf2_flags |= 0x01;
          write_byte(0x0040, 0x18, mf2_flags);
        }
      }
      break;
    case 0x9d: /* Ctrl release */
      if ((mf2_state & 0x01) == 0) {
        shift_flags &= ~0x04;
        write_byte(0x0040, 0x17, shift_flags);
        if (mf2_state & 0x02) {
          mf2_state &= ~0x04;
          write_byte(0x0040, 0x96, mf2_state);
        } else {
          mf2_flags &= ~0x01;
          write_byte(0x0040, 0x18, mf2_flags);
        }
      }
      break;

    case 0x38: /* Alt press */
      shift_flags |= 0x08;
      write_byte(0x0040, 0x17, shift_flags);
      if (mf2_state & 0x02) {
        mf2_state |= 0x08;
        write_byte(0x0040, 0x96, mf2_state);
      } else {
        mf2_flags |= 0x02;
        write_byte(0x0040, 0x18, mf2_flags);
      }
      break;
    case 0xb8: /* Alt release */
      shift_flags &= ~0x08;
      write_byte(0x0040, 0x17, shift_flags);
      if (mf2_state & 0x02) {
        mf2_state &= ~0x08;
        write_byte(0x0040, 0x96, mf2_state);
      } else {
        mf2_flags &= ~0x02;
        write_byte(0x0040, 0x18, mf2_flags);
      }
      break;

    case 0x45: /* Num Lock press */
      if ((mf2_state & 0x03) == 0) {
        mf2_flags |= 0x20;
        write_byte(0x0040, 0x18, mf2_flags);
        shift_flags ^= 0x20;
        write_byte(0x0040, 0x17, shift_flags);
      }
      break;
    case 0xc5: /* Num Lock release */
      if ((mf2_state & 0x03) == 0) {
        mf2_flags &= ~0x20;
        write_byte(0x0040, 0x18, mf2_flags);
      }
      break;

    case 0x46: /* Scroll Lock press */
      mf2_flags |= 0x10;
      write_byte(0x0040, 0x18, mf2_flags);
      shift_flags ^= 0x10;
      write_byte(0x0040, 0x17, shift_flags);
      break;

    case 0xc6: /* Scroll Lock release */
      mf2_flags &= ~0x10;
      write_byte(0x0040, 0x18, mf2_flags);
      break;

    default:
      if (scancode & 0x80) {
        break; /* toss key releases ... */
      }
      if (scancode > MAX_SCAN_CODE) {
        BX_INFO("KBD: int09h_handler(): unknown scancode read: 0x%02x!\n", scancode);
        return;
      }
      if (shift_flags & 0x08) { /* ALT */
        asciicode = scan_to_scanascii[scancode].alt;
        scancode = scan_to_scanascii[scancode].alt >> 8;
      } else if (shift_flags & 0x04) { /* CONTROL */
        asciicode = scan_to_scanascii[scancode].control;
        scancode = scan_to_scanascii[scancode].control >> 8;
      } else if (((mf2_state & 0x02) > 0) && ((scancode >= 0x47) && (scancode <= 0x53))) {
        /* extended keys handling */
        asciicode = 0xe0;
        scancode = scan_to_scanascii[scancode].normal >> 8;
      } else if (shift_flags & 0x03) { /* LSHIFT + RSHIFT */
        /* check if lock state should be ignored 
         * because a SHIFT key are pressed */

        if (shift_flags & scan_to_scanascii[scancode].lock_flags) {
          asciicode = scan_to_scanascii[scancode].normal;
          scancode = scan_to_scanascii[scancode].normal >> 8;
        } else {
          asciicode = scan_to_scanascii[scancode].shift;
          scancode = scan_to_scanascii[scancode].shift >> 8;
        }
      } else {
        /* check if lock is on */
        if (shift_flags & scan_to_scanascii[scancode].lock_flags) {
          asciicode = scan_to_scanascii[scancode].shift;
          scancode = scan_to_scanascii[scancode].shift >> 8;
        } else {
          asciicode = scan_to_scanascii[scancode].normal;
          scancode = scan_to_scanascii[scancode].normal >> 8;
        }
      }
      if (scancode==0 && asciicode==0) {
        BX_INFO("KBD: int09h_handler(): scancode & asciicode are zero?\n");
      }
      enqueue_key(scancode, asciicode);
      break;
  }
  if ((scancode & 0x7f) != 0x1d) {
    mf2_state &= ~0x01;
  }
  mf2_state &= ~0x02;
  write_byte(0x0040, 0x96, mf2_state);
}

  unsigned int
enqueue_key(scan_code, ascii_code)
  Bit8u scan_code, ascii_code;
{
  Bit16u buffer_start, buffer_end, buffer_head, buffer_tail, temp_tail;

#if BX_CPU < 2
  buffer_start = 0x001E;
  buffer_end   = 0x003E;
#else
  buffer_start = read_word(0x0040, 0x0080);
  buffer_end   = read_word(0x0040, 0x0082);
#endif

  buffer_head = read_word(0x0040, 0x001A);
  buffer_tail = read_word(0x0040, 0x001C);

  temp_tail = buffer_tail;
  buffer_tail += 2;
  if (buffer_tail >= buffer_end)
    buffer_tail = buffer_start;

  if (buffer_tail == buffer_head) {
    return(0);
    }

   write_byte(0x0040, temp_tail, ascii_code);
   write_byte(0x0040, temp_tail+1, scan_code);
   write_word(0x0040, 0x001C, buffer_tail);
   return(1);
}


  void
int74_function(make_farcall, Z, Y, X, status)
  Bit16u make_farcall, Z, Y, X, status;
{
  Bit16u ebda_seg=read_word(0x0040,0x000E);
  Bit8u  in_byte, index, package_count;
  Bit8u  mouse_flags_1, mouse_flags_2;

BX_DEBUG_INT74("entering int74_function\n");
  make_farcall = 0;

  in_byte = inb(0x64);
  if ( (in_byte & 0x21) != 0x21 ) {
    return;
    }
  in_byte = inb(0x60);
BX_DEBUG_INT74("int74: read byte %02x\n", in_byte);

  mouse_flags_1 = read_byte(ebda_seg, 0x0026);
  mouse_flags_2 = read_byte(ebda_seg, 0x0027);

  if ( (mouse_flags_2 & 0x80) != 0x80 ) {
      return;
  }

  package_count = mouse_flags_2 & 0x07;
  index = mouse_flags_1 & 0x07;
  write_byte(ebda_seg, 0x28 + index, in_byte);

  if ( (index+1) >= package_count ) {
BX_DEBUG_INT74("int74_function: make_farcall=1\n");
    status = read_byte(ebda_seg, 0x0028 + 0);
    X      = read_byte(ebda_seg, 0x0028 + 1);
    Y      = read_byte(ebda_seg, 0x0028 + 2);
    Z      = 0;
    mouse_flags_1 = 0;
    // check if far call handler installed
    if (mouse_flags_2 & 0x80)
      make_farcall = 1;
    }
  else {
    mouse_flags_1++;
    }
  write_byte(ebda_seg, 0x0026, mouse_flags_1);
}

#define SET_DISK_RET_STATUS(status) write_byte(0x0040, 0x0074, status)

#if BX_USE_ATADRV

  void
int13_harddisk(DS, ES, DI, SI, BP, ELDX, BX, DX, CX, AX, IP, CS, FLAGS)
  Bit16u DS, ES, DI, SI, BP, ELDX, BX, DX, CX, AX, IP, CS, FLAGS;
{
  Bit32u lba;
  Bit16u ebda_seg=read_word(0x0040,0x000E);
  Bit16u cylinder, head, sector;
  Bit16u segment, offset;
  Bit16u npc, nph, npspt, nlc, nlh, nlspt;
  Bit16u size, count;
  Bit8u  device, status;

  BX_DEBUG_INT13_HD("int13_harddisk: AX=%04x BX=%04x CX=%04x DX=%04x ES=%04x\n", AX, BX, CX, DX, ES);

  write_byte(0x0040, 0x008e, 0);  // clear completion flag

  // basic check : device has to be defined
  if ( (GET_ELDL() < 0x80) || (GET_ELDL() >= 0x80 + BX_MAX_ATA_DEVICES) ) {
    BX_INFO("int13_harddisk: function %02x, ELDL out of range %02x\n", GET_AH(), GET_ELDL());
    goto int13_fail;
    }

  // Get the ata channel
  device=read_byte(ebda_seg,&EbdaData->ata.hdidmap[GET_ELDL()-0x80]);

  // basic check : device has to be valid 
  if (device >= BX_MAX_ATA_DEVICES) {
    BX_INFO("int13_harddisk: function %02x, unmapped device for ELDL=%02x\n", GET_AH(), GET_ELDL());
    goto int13_fail;
    }
  
  switch (GET_AH()) {

    case 0x00: /* disk controller reset */
      ata_reset (device);
      goto int13_success;
      break;

    case 0x01: /* read disk status */
      status = read_byte(0x0040, 0x0074);
      SET_AH(status);
      SET_DISK_RET_STATUS(0);
      /* set CF if error status read */
      if (status) goto int13_fail_nostatus;
      else        goto int13_success_noah;
      break;

    case 0x02: // read disk sectors
    case 0x03: // write disk sectors 
    case 0x04: // verify disk sectors

      count       = GET_AL();
      cylinder    = GET_CH();
      cylinder   |= ( ((Bit16u) GET_CL()) << 2) & 0x300;
      sector      = (GET_CL() & 0x3f);
      head        = GET_DH();

      segment = ES;
      offset  = BX;

      if ( (count > 128) || (count == 0) ) {
        BX_INFO("int13_harddisk: function %02x, count out of range!\n",GET_AH());
        goto int13_fail;
        }

      nlc   = read_word(ebda_seg, &EbdaData->ata.devices[device].lchs.cylinders);
      nlh   = read_word(ebda_seg, &EbdaData->ata.devices[device].lchs.heads);
      nlspt = read_word(ebda_seg, &EbdaData->ata.devices[device].lchs.spt);

      // sanity check on cyl heads, sec
      if( (cylinder >= nlc) || (head >= nlh) || (sector > nlspt )) {
        BX_INFO("int13_harddisk: function %02x, parameters out of range %04x/%04x/%04x!\n", GET_AH(), cylinder, head, sector);
        goto int13_fail;
        }
      
      // FIXME verify
      if ( GET_AH() == 0x04 ) goto int13_success;

      nph   = read_word(ebda_seg, &EbdaData->ata.devices[device].pchs.heads);
      npspt = read_word(ebda_seg, &EbdaData->ata.devices[device].pchs.spt);

      // if needed, translate lchs to lba, and execute command
      if ( (nph != nlh) || (npspt != nlspt)) {
        lba = ((((Bit32u)cylinder * (Bit32u)nlh) + (Bit32u)head) * (Bit32u)nlspt) + (Bit32u)sector - 1;
        sector = 0; // this forces the command to be lba
        }

      if ( GET_AH() == 0x02 )
        status=ata_cmd_data_in(device, ATA_CMD_READ_SECTORS, count, cylinder, head, sector, lba, segment, offset);
      else
        status=ata_cmd_data_out(device, ATA_CMD_WRITE_SECTORS, count, cylinder, head, sector, lba, segment, offset);

      // Set nb of sector transferred
      SET_AL(read_word(ebda_seg, &EbdaData->ata.trsfsectors));

      if (status != 0) {
        BX_INFO("int13_harddisk: function %02x, error %02x !\n",GET_AH(),status);
        SET_AH(0x0c);
        goto int13_fail_noah;
        }

      goto int13_success;
      break;

    case 0x05: /* format disk track */
      BX_INFO("format disk track called\n");
      goto int13_success;
      return;
      break;

    case 0x08: /* read disk drive parameters */
      
      // Get logical geometry from table
      nlc   = read_word(ebda_seg, &EbdaData->ata.devices[device].lchs.cylinders);
      nlh   = read_word(ebda_seg, &EbdaData->ata.devices[device].lchs.heads);
      nlspt = read_word(ebda_seg, &EbdaData->ata.devices[device].lchs.spt);
      count = read_byte(ebda_seg, &EbdaData->ata.hdcount);

      nlc = nlc - 2; /* 0 based , last sector not used */
      SET_AL(0);
      SET_CH(nlc & 0xff);
      SET_CL(((nlc >> 2) & 0xc0) | (nlspt & 0x3f));
      SET_DH(nlh - 1);
      SET_DL(count); /* FIXME returns 0, 1, or n hard drives */

      // FIXME should set ES & DI
      
      goto int13_success;
      break;

    case 0x10: /* check drive ready */
      // should look at 40:8E also???
      
      // Read the status from controller
      status = inb(read_word(ebda_seg, &EbdaData->ata.channels[device/2].iobase1) + ATA_CB_STAT);
      if ( (status & ( ATA_CB_STAT_BSY | ATA_CB_STAT_RDY )) == ATA_CB_STAT_RDY ) {
        goto int13_success;
        }
      else {
        SET_AH(0xAA);
        goto int13_fail_noah;
        }
      break;

    case 0x15: /* read disk drive size */

      // Get physical geometry from table
      npc   = read_word(ebda_seg, &EbdaData->ata.devices[device].pchs.cylinders);
      nph   = read_word(ebda_seg, &EbdaData->ata.devices[device].pchs.heads);
      npspt = read_word(ebda_seg, &EbdaData->ata.devices[device].pchs.spt);

      // Compute sector count seen by int13
      lba = (Bit32u)(npc - 1) * (Bit32u)nph * (Bit32u)npspt;
      CX = lba >> 16;
      DX = lba & 0xffff;

      SET_AH(3);  // hard disk accessible
      goto int13_success_noah;
      break;

    case 0x41: // IBM/MS installation check
      BX=0xaa55;     // install check
      SET_AH(0x30);  // EDD 3.0
      CX=0x0007;     // ext disk access and edd, removable supported
      goto int13_success_noah;
      break;

    case 0x42: // IBM/MS extended read
    case 0x43: // IBM/MS extended write
    case 0x44: // IBM/MS verify
    case 0x47: // IBM/MS extended seek

      count=read_word(DS, SI+(Bit16u)&Int13Ext->count);
      segment=read_word(DS, SI+(Bit16u)&Int13Ext->segment);
      offset=read_word(DS, SI+(Bit16u)&Int13Ext->offset);
 
      // Can't use 64 bits lba
      lba=read_dword(DS, SI+(Bit16u)&Int13Ext->lba2);
      if (lba != 0L) {
        BX_PANIC("int13_harddisk: function %02x. Can't use 64bits lba\n",GET_AH());
        goto int13_fail;
        }

      // Get 32 bits lba and check
      lba=read_dword(DS, SI+(Bit16u)&Int13Ext->lba1);
      if (lba >= read_dword(ebda_seg, &EbdaData->ata.devices[device].sectors) ) {
        BX_INFO("int13_harddisk: function %02x. LBA out of range\n",GET_AH());
        goto int13_fail;
        }

      // If verify or seek
      if (( GET_AH() == 0x44 ) || ( GET_AH() == 0x47 ))
        goto int13_success;
      
      // Execute the command
      if ( GET_AH() == 0x42 )
        status=ata_cmd_data_in(device, ATA_CMD_READ_SECTORS, count, 0, 0, 0, lba, segment, offset);
      else
        status=ata_cmd_data_out(device, ATA_CMD_WRITE_SECTORS, count, 0, 0, 0, lba, segment, offset);

      count=read_word(ebda_seg, &EbdaData->ata.trsfsectors);
      write_word(DS, SI+(Bit16u)&Int13Ext->count, count);

      if (status != 0) {
        BX_INFO("int13_harddisk: function %02x, error %02x !\n",GET_AH(),status);
        SET_AH(0x0c);
        goto int13_fail_noah;
        }

      goto int13_success;
      break;

    case 0x45: // IBM/MS lock/unlock drive
    case 0x49: // IBM/MS extended media change
      goto int13_success;    // Always success for HD
      break;
      
    case 0x46: // IBM/MS eject media
      SET_AH(0xb2);          // Volume Not Removable
      goto int13_fail_noah;  // Always fail for HD
      break;

    case 0x48: // IBM/MS get drive parameters
      size=read_word(DS,SI+(Bit16u)&Int13DPT->size);

      // Buffer is too small
      if(size < 0x1a) 
        goto int13_fail;

      // EDD 1.x
      if(size >= 0x1a) {
        Bit16u   blksize;

        npc     = read_word(ebda_seg, &EbdaData->ata.devices[device].pchs.cylinders);
        nph     = read_word(ebda_seg, &EbdaData->ata.devices[device].pchs.heads);
        npspt   = read_word(ebda_seg, &EbdaData->ata.devices[device].pchs.spt);
        lba     = read_dword(ebda_seg, &EbdaData->ata.devices[device].sectors);
        blksize = read_word(ebda_seg, &EbdaData->ata.devices[device].blksize);

        write_word(DS, SI+(Bit16u)&Int13DPT->size, 0x1a);
        write_word(DS, SI+(Bit16u)&Int13DPT->infos, 0x02); // geometry is valid
        write_dword(DS, SI+(Bit16u)&Int13DPT->cylinders, (Bit32u)npc);
        write_dword(DS, SI+(Bit16u)&Int13DPT->heads, (Bit32u)nph);
        write_dword(DS, SI+(Bit16u)&Int13DPT->spt, (Bit32u)npspt);
        write_dword(DS, SI+(Bit16u)&Int13DPT->sector_count1, lba);  // FIXME should be Bit64
        write_dword(DS, SI+(Bit16u)&Int13DPT->sector_count2, 0L);  
        write_word(DS, SI+(Bit16u)&Int13DPT->blksize, blksize);  
        }

      // EDD 2.x
      if(size >= 0x1e) {
        Bit8u  channel, dev, irq, mode, checksum, i, translation;
        Bit16u iobase1, iobase2, options;

        write_word(DS, SI+(Bit16u)&Int13DPT->size, 0x1e);

        write_word(DS, SI+(Bit16u)&Int13DPT->dpte_segment, ebda_seg);  
        write_word(DS, SI+(Bit16u)&Int13DPT->dpte_offset, &EbdaData->ata.dpte);  

        // Fill in dpte
        channel = device / 2;
        iobase1 = read_word(ebda_seg, &EbdaData->ata.channels[channel].iobase1);
        iobase2 = read_word(ebda_seg, &EbdaData->ata.channels[channel].iobase2);
        irq = read_byte(ebda_seg, &EbdaData->ata.channels[channel].irq);
        mode = read_byte(ebda_seg, &EbdaData->ata.devices[device].mode);
        translation = read_byte(ebda_seg, &EbdaData->ata.devices[device].translation);

        options  = (translation==ATA_TRANSLATION_NONE?0:1<<3); // chs translation
        options |= (1<<4); // lba translation
        options |= (mode==ATA_MODE_PIO32?1:0<<7);
        options |= (translation==ATA_TRANSLATION_LBA?1:0<<9); 
        options |= (translation==ATA_TRANSLATION_RECHS?3:0<<9); 

        write_word(ebda_seg, &EbdaData->ata.dpte.iobase1, iobase1);
        write_word(ebda_seg, &EbdaData->ata.dpte.iobase2, iobase2);
        write_byte(ebda_seg, &EbdaData->ata.dpte.prefix, (0xe | (device % 2))<<4 );
        write_byte(ebda_seg, &EbdaData->ata.dpte.unused, 0xcb );
        write_byte(ebda_seg, &EbdaData->ata.dpte.irq, irq );
        write_byte(ebda_seg, &EbdaData->ata.dpte.blkcount, 1 );
        write_byte(ebda_seg, &EbdaData->ata.dpte.dma, 0 );
        write_byte(ebda_seg, &EbdaData->ata.dpte.pio, 0 );
        write_word(ebda_seg, &EbdaData->ata.dpte.options, options);
        write_word(ebda_seg, &EbdaData->ata.dpte.reserved, 0);
        write_byte(ebda_seg, &EbdaData->ata.dpte.revision, 0x11);
 
        checksum=0;
        for (i=0; i<15; i++) checksum+=read_byte(ebda_seg, (&EbdaData->ata.dpte) + i);
        checksum = ~checksum;
        write_byte(ebda_seg, &EbdaData->ata.dpte.checksum, checksum);
        }

      // EDD 3.x
      if(size >= 0x42) {
        Bit8u channel, iface, checksum, i;
        Bit16u iobase1;

        channel = device / 2;
        iface = read_byte(ebda_seg, &EbdaData->ata.channels[channel].iface);
        iobase1 = read_word(ebda_seg, &EbdaData->ata.channels[channel].iobase1);

        write_word(DS, SI+(Bit16u)&Int13DPT->size, 0x42);
        write_word(DS, SI+(Bit16u)&Int13DPT->key, 0xbedd);
        write_byte(DS, SI+(Bit16u)&Int13DPT->dpi_length, 0x24);
        write_byte(DS, SI+(Bit16u)&Int13DPT->reserved1, 0);
        write_word(DS, SI+(Bit16u)&Int13DPT->reserved2, 0);

        if (iface==ATA_IFACE_ISA) {
          write_byte(DS, SI+(Bit16u)&Int13DPT->host_bus[0], 'I');
          write_byte(DS, SI+(Bit16u)&Int13DPT->host_bus[1], 'S');
          write_byte(DS, SI+(Bit16u)&Int13DPT->host_bus[2], 'A');
          write_byte(DS, SI+(Bit16u)&Int13DPT->host_bus[3], 0);
          }
        else { 
          // FIXME PCI
          }
        write_byte(DS, SI+(Bit16u)&Int13DPT->iface_type[0], 'A');
        write_byte(DS, SI+(Bit16u)&Int13DPT->iface_type[1], 'T');
        write_byte(DS, SI+(Bit16u)&Int13DPT->iface_type[2], 'A');
        write_byte(DS, SI+(Bit16u)&Int13DPT->iface_type[3], 0);

        if (iface==ATA_IFACE_ISA) {
          write_word(DS, SI+(Bit16u)&Int13DPT->iface_path[0], iobase1);
          write_word(DS, SI+(Bit16u)&Int13DPT->iface_path[2], 0);
          write_dword(DS, SI+(Bit16u)&Int13DPT->iface_path[4], 0L);
          }
        else { 
          // FIXME PCI
          }
        write_byte(DS, SI+(Bit16u)&Int13DPT->device_path[0], device%2);
        write_byte(DS, SI+(Bit16u)&Int13DPT->device_path[1], 0);
        write_word(DS, SI+(Bit16u)&Int13DPT->device_path[2], 0);
        write_dword(DS, SI+(Bit16u)&Int13DPT->device_path[4], 0L);

        checksum=0;
        for (i=30; i<64; i++) checksum+=read_byte(DS, SI + i);
        checksum = ~checksum;
        write_byte(DS, SI+(Bit16u)&Int13DPT->checksum, checksum);
        }

      goto int13_success;
      break;

    case 0x4e: // // IBM/MS set hardware configuration
      // DMA, prefetch, PIO maximum not supported
      switch (GET_AL()) {
        case 0x01:
        case 0x03:
        case 0x04:
        case 0x06:
          goto int13_success;
          break;
        default :
          goto int13_fail;
        }
      break;

    case 0x09: /* initialize drive parameters */
    case 0x0c: /* seek to specified cylinder */
    case 0x0d: /* alternate disk reset */
    case 0x11: /* recalibrate */
    case 0x14: /* controller internal diagnostic */
      BX_INFO("int13h_harddisk function %02xh unimplemented, returns success\n", GET_AH());
      goto int13_success;
      break;

    case 0x0a: /* read disk sectors with ECC */
    case 0x0b: /* write disk sectors with ECC */
    case 0x18: // set media type for format
    case 0x50: // IBM/MS send packet command
    default:
      BX_INFO("int13_harddisk function %02xh unsupported, returns fail\n", GET_AH());
      goto int13_fail;
      break;
    }

int13_fail:
    SET_AH(0x01); // defaults to invalid function in AH or invalid parameter
int13_fail_noah:
    SET_DISK_RET_STATUS(GET_AH());
int13_fail_nostatus:
    SET_CF();     // error occurred
    return;

int13_success:
    SET_AH(0x00); // no error
int13_success_noah:
    SET_DISK_RET_STATUS(0x00);
    CLEAR_CF();   // no error
    return;
}

// ---------------------------------------------------------------------------
// Start of int13 for cdrom
// ---------------------------------------------------------------------------

  void
int13_cdrom(EHBX, DS, ES, DI, SI, BP, ELDX, BX, DX, CX, AX, IP, CS, FLAGS)
  Bit16u EHBX, DS, ES, DI, SI, BP, ELDX, BX, DX, CX, AX, IP, CS, FLAGS;
{
  Bit16u ebda_seg=read_word(0x0040,0x000E);
  Bit8u  device, status, locks;
  Bit8u  atacmd[12];
  Bit32u lba;
  Bit16u count, segment, offset, i, size;

  BX_DEBUG_INT13_CD("int13_cdrom: AX=%04x BX=%04x CX=%04x DX=%04x ES=%04x\n", AX, BX, CX, DX, ES);
  
  SET_DISK_RET_STATUS(0x00);

  /* basic check : device should be 0xE0+ */
  if( (GET_ELDL() < 0xE0) || (GET_ELDL() >= 0xE0+BX_MAX_ATA_DEVICES) ) {
    BX_INFO("int13_cdrom: function %02x, ELDL out of range %02x\n", GET_AH(), GET_ELDL());
    goto int13_fail;
    }

  // Get the ata channel
  device=read_byte(ebda_seg,&EbdaData->ata.cdidmap[GET_ELDL()-0xE0]);

  /* basic check : device has to be valid  */
  if (device >= BX_MAX_ATA_DEVICES) {
    BX_INFO("int13_cdrom: function %02x, unmapped device for ELDL=%02x\n", GET_AH(), GET_ELDL());
    goto int13_fail;
    }
  
  switch (GET_AH()) {

    // all those functions return SUCCESS
    case 0x00: /* disk controller reset */
    case 0x09: /* initialize drive parameters */
    case 0x0c: /* seek to specified cylinder */
    case 0x0d: /* alternate disk reset */  
    case 0x10: /* check drive ready */    
    case 0x11: /* recalibrate */      
    case 0x14: /* controller internal diagnostic */
    case 0x16: /* detect disk change */
      goto int13_success;
      break;

    // all those functions return disk write-protected
    case 0x03: /* write disk sectors */
    case 0x05: /* format disk track */
    case 0x43: // IBM/MS extended write
      SET_AH(0x03);
      goto int13_fail_noah;
      break;

    case 0x01: /* read disk status */
      status = read_byte(0x0040, 0x0074);
      SET_AH(status);
      SET_DISK_RET_STATUS(0);

      /* set CF if error status read */
      if (status) goto int13_fail_nostatus;
      else        goto int13_success_noah;
      break;      

    case 0x15: /* read disk drive size */
      SET_AH(0x02);
      goto int13_fail_noah;
      break;

    case 0x41: // IBM/MS installation check
      BX=0xaa55;     // install check
      SET_AH(0x30);  // EDD 2.1
      CX=0x0007;     // ext disk access, removable and edd
      goto int13_success_noah;
      break;

    case 0x42: // IBM/MS extended read
    case 0x44: // IBM/MS verify sectors
    case 0x47: // IBM/MS extended seek
       
      count=read_word(DS, SI+(Bit16u)&Int13Ext->count);
      segment=read_word(DS, SI+(Bit16u)&Int13Ext->segment);
      offset=read_word(DS, SI+(Bit16u)&Int13Ext->offset);
 
      // Can't use 64 bits lba
      lba=read_dword(DS, SI+(Bit16u)&Int13Ext->lba2);
      if (lba != 0L) {
        BX_PANIC("int13_cdrom: function %02x. Can't use 64bits lba\n",GET_AH());
        goto int13_fail;
        }

      // Get 32 bits lba 
      lba=read_dword(DS, SI+(Bit16u)&Int13Ext->lba1);

      // If verify or seek
      if (( GET_AH() == 0x44 ) || ( GET_AH() == 0x47 ))
        goto int13_success;
      
      memsetb(get_SS(),atacmd,0,12);
      atacmd[0]=0x28;                      // READ command
      atacmd[7]=(count & 0xff00) >> 8;     // Sectors
      atacmd[8]=(count & 0x00ff);          // Sectors
      atacmd[2]=(lba & 0xff000000) >> 24;  // LBA
      atacmd[3]=(lba & 0x00ff0000) >> 16;
      atacmd[4]=(lba & 0x0000ff00) >> 8;
      atacmd[5]=(lba & 0x000000ff);
      status = ata_cmd_packet(device, 12, get_SS(), atacmd, 0, count*2048L, ATA_DATA_IN, segment,offset); 

      count = (Bit16u)(read_dword(ebda_seg, &EbdaData->ata.trsfbytes) >> 11);
      write_word(DS, SI+(Bit16u)&Int13Ext->count, count);

      if (status != 0) {
        BX_INFO("int13_cdrom: function %02x, status %02x !\n",GET_AH(),status);
        SET_AH(0x0c);
        goto int13_fail_noah;
        }

      goto int13_success;
      break;

    case 0x45: // IBM/MS lock/unlock drive
      if (GET_AL() > 2) goto int13_fail;

      locks = read_byte(ebda_seg, &EbdaData->ata.devices[device].lock);

      switch (GET_AL()) {
        case 0 :  // lock
          if (locks == 0xff) {
            SET_AH(0xb4);
            SET_AL(1);
            goto int13_fail_noah;
            }
          write_byte(ebda_seg, &EbdaData->ata.devices[device].lock, ++locks);
          SET_AL(1);
          break;
        case 1 :  // unlock
          if (locks == 0x00) {
            SET_AH(0xb0);
            SET_AL(0);
            goto int13_fail_noah;
            }
          write_byte(ebda_seg, &EbdaData->ata.devices[device].lock, --locks);
          SET_AL(locks==0?0:1);
          break;
        case 2 :  // status
          SET_AL(locks==0?0:1);
          break;
        }
      goto int13_success;
      break;

    case 0x46: // IBM/MS eject media
      locks = read_byte(ebda_seg, &EbdaData->ata.devices[device].lock);
      
      if (locks != 0) {
        SET_AH(0xb1); // media locked
        goto int13_fail_noah;
        }
      // FIXME should handle 0x31 no media in device
      // FIXME should handle 0xb5 valid request failed
    
      // Call removable media eject
      ASM_START
        push bp
        mov  bp, sp

        mov ah, #0x52
        int 15
        mov _int13_cdrom.status + 2[bp], ah
        jnc int13_cdrom_rme_end
        mov _int13_cdrom.status, #1
int13_cdrom_rme_end:
        pop bp
      ASM_END

      if (status != 0) {
        SET_AH(0xb1); // media locked
        goto int13_fail_noah;
      }

      goto int13_success;
      break;

    case 0x48: // IBM/MS get drive parameters
      size = read_word(DS,SI+(Bit16u)&Int13Ext->size);

      // Buffer is too small
      if(size < 0x1a) 
        goto int13_fail;

      // EDD 1.x
      if(size >= 0x1a) {
        Bit16u   cylinders, heads, spt, blksize;

        blksize   = read_word(ebda_seg, &EbdaData->ata.devices[device].blksize);

        write_word(DS, SI+(Bit16u)&Int13DPT->size, 0x1a);
        write_word(DS, SI+(Bit16u)&Int13DPT->infos, 0x74); // removable, media change, lockable, max values
        write_dword(DS, SI+(Bit16u)&Int13DPT->cylinders, 0xffffffff);
        write_dword(DS, SI+(Bit16u)&Int13DPT->heads, 0xffffffff);
        write_dword(DS, SI+(Bit16u)&Int13DPT->spt, 0xffffffff);
        write_dword(DS, SI+(Bit16u)&Int13DPT->sector_count1, 0xffffffff);  // FIXME should be Bit64
        write_dword(DS, SI+(Bit16u)&Int13DPT->sector_count2, 0xffffffff);  
        write_word(DS, SI+(Bit16u)&Int13DPT->blksize, blksize);  
        }

      // EDD 2.x
      if(size >= 0x1e) {
        Bit8u  channel, dev, irq, mode, checksum, i;
        Bit16u iobase1, iobase2, options;

        write_word(DS, SI+(Bit16u)&Int13DPT->size, 0x1e);

        write_word(DS, SI+(Bit16u)&Int13DPT->dpte_segment, ebda_seg);  
        write_word(DS, SI+(Bit16u)&Int13DPT->dpte_offset, &EbdaData->ata.dpte);  

        // Fill in dpte
        channel = device / 2;
        iobase1 = read_word(ebda_seg, &EbdaData->ata.channels[channel].iobase1);
        iobase2 = read_word(ebda_seg, &EbdaData->ata.channels[channel].iobase2);
        irq = read_byte(ebda_seg, &EbdaData->ata.channels[channel].irq);
        mode = read_byte(ebda_seg, &EbdaData->ata.devices[device].mode);

        // FIXME atapi device
        options  = (1<<4); // lba translation
        options |= (1<<5); // removable device
        options |= (1<<6); // atapi device
        options |= (mode==ATA_MODE_PIO32?1:0<<7);

        write_word(ebda_seg, &EbdaData->ata.dpte.iobase1, iobase1);
        write_word(ebda_seg, &EbdaData->ata.dpte.iobase2, iobase2);
        write_byte(ebda_seg, &EbdaData->ata.dpte.prefix, (0xe | (device % 2))<<4 );
        write_byte(ebda_seg, &EbdaData->ata.dpte.unused, 0xcb );
        write_byte(ebda_seg, &EbdaData->ata.dpte.irq, irq );
        write_byte(ebda_seg, &EbdaData->ata.dpte.blkcount, 1 );
        write_byte(ebda_seg, &EbdaData->ata.dpte.dma, 0 );
        write_byte(ebda_seg, &EbdaData->ata.dpte.pio, 0 );
        write_word(ebda_seg, &EbdaData->ata.dpte.options, options);
        write_word(ebda_seg, &EbdaData->ata.dpte.reserved, 0);
        write_byte(ebda_seg, &EbdaData->ata.dpte.revision, 0x11);

        checksum=0;
        for (i=0; i<15; i++) checksum+=read_byte(ebda_seg, (&EbdaData->ata.dpte) + i);
        checksum = ~checksum;
        write_byte(ebda_seg, &EbdaData->ata.dpte.checksum, checksum);
        }

      // EDD 3.x
      if(size >= 0x42) {
        Bit8u channel, iface, checksum, i;
        Bit16u iobase1;

        channel = device / 2;
        iface = read_byte(ebda_seg, &EbdaData->ata.channels[channel].iface);
        iobase1 = read_word(ebda_seg, &EbdaData->ata.channels[channel].iobase1);

        write_word(DS, SI+(Bit16u)&Int13DPT->size, 0x42);
        write_word(DS, SI+(Bit16u)&Int13DPT->key, 0xbedd);
        write_byte(DS, SI+(Bit16u)&Int13DPT->dpi_length, 0x24);
        write_byte(DS, SI+(Bit16u)&Int13DPT->reserved1, 0);
        write_word(DS, SI+(Bit16u)&Int13DPT->reserved2, 0);

        if (iface==ATA_IFACE_ISA) {
          write_byte(DS, SI+(Bit16u)&Int13DPT->host_bus[0], 'I');
          write_byte(DS, SI+(Bit16u)&Int13DPT->host_bus[1], 'S');
          write_byte(DS, SI+(Bit16u)&Int13DPT->host_bus[2], 'A');
          write_byte(DS, SI+(Bit16u)&Int13DPT->host_bus[3], 0);
          }
        else { 
          // FIXME PCI
          }
        write_byte(DS, SI+(Bit16u)&Int13DPT->iface_type[0], 'A');
        write_byte(DS, SI+(Bit16u)&Int13DPT->iface_type[1], 'T');
        write_byte(DS, SI+(Bit16u)&Int13DPT->iface_type[2], 'A');
        write_byte(DS, SI+(Bit16u)&Int13DPT->iface_type[3], 0);

        if (iface==ATA_IFACE_ISA) {
          write_word(DS, SI+(Bit16u)&Int13DPT->iface_path[0], iobase1);
          write_word(DS, SI+(Bit16u)&Int13DPT->iface_path[2], 0);
          write_dword(DS, SI+(Bit16u)&Int13DPT->iface_path[4], 0L);
          }
        else { 
          // FIXME PCI
          }
        write_byte(DS, SI+(Bit16u)&Int13DPT->device_path[0], device%2);
        write_byte(DS, SI+(Bit16u)&Int13DPT->device_path[1], 0);
        write_word(DS, SI+(Bit16u)&Int13DPT->device_path[2], 0);
        write_dword(DS, SI+(Bit16u)&Int13DPT->device_path[4], 0L);

        checksum=0;
        for (i=30; i<64; i++) checksum+=read_byte(DS, SI + i);
        checksum = ~checksum;
        write_byte(DS, SI+(Bit16u)&Int13DPT->checksum, checksum);
        }

      goto int13_success;
      break;

    case 0x49: // IBM/MS extended media change
      // always send changed ??
      SET_AH(06);
      goto int13_fail_nostatus;
      break;
      
    case 0x4e: // // IBM/MS set hardware configuration
      // DMA, prefetch, PIO maximum not supported
      switch (GET_AL()) {
        case 0x01:
        case 0x03:
        case 0x04:
        case 0x06:
          goto int13_success;
          break;
        default :
          goto int13_fail;
        }
      break;

    // all those functions return unimplemented
    case 0x02: /* read sectors */
    case 0x04: /* verify sectors */
    case 0x08: /* read disk drive parameters */
    case 0x0a: /* read disk sectors with ECC */
    case 0x0b: /* write disk sectors with ECC */
    case 0x18: /* set media type for format */
    case 0x50: // ? - send packet command
    default:
      BX_INFO("int13_cdrom: unsupported AH=%02x\n", GET_AH());
      goto int13_fail;
      break;
    }

int13_fail:
    SET_AH(0x01); // defaults to invalid function in AH or invalid parameter
int13_fail_noah:
    SET_DISK_RET_STATUS(GET_AH());
int13_fail_nostatus:
    SET_CF();     // error occurred
    return;

int13_success:
    SET_AH(0x00); // no error
int13_success_noah:
    SET_DISK_RET_STATUS(0x00);
    CLEAR_CF();   // no error
    return;
}

// ---------------------------------------------------------------------------
// End of int13 for cdrom
// ---------------------------------------------------------------------------

#if BX_ELTORITO_BOOT
// ---------------------------------------------------------------------------
// Start of int13 for eltorito functions
// ---------------------------------------------------------------------------

  void
int13_eltorito(DS, ES, DI, SI, BP, SP, BX, DX, CX, AX, IP, CS, FLAGS)
  Bit16u DS, ES, DI, SI, BP, SP, BX, DX, CX, AX, IP, CS, FLAGS;
{
  Bit16u ebda_seg=read_word(0x0040,0x000E);

  BX_DEBUG_INT13_ET("int13_eltorito: AX=%04x BX=%04x CX=%04x DX=%04x ES=%04x\n", AX, BX, CX, DX, ES);
  // BX_DEBUG_INT13_ET("int13_eltorito: SS=%04x DS=%04x ES=%04x DI=%04x SI=%04x\n",get_SS(), DS, ES, DI, SI);
  
  switch (GET_AH()) {

    // FIXME ElTorito Various. Should be implemented
    case 0x4a: // ElTorito - Initiate disk emu
    case 0x4c: // ElTorito - Initiate disk emu and boot
    case 0x4d: // ElTorito - Return Boot catalog
      BX_PANIC("Int13 eltorito call with AX=%04x. Please report\n",AX);
      goto int13_fail;
      break;

    case 0x4b: // ElTorito - Terminate disk emu
      // FIXME ElTorito Hardcoded
      write_byte(DS,SI+0x00,0x13);
      write_byte(DS,SI+0x01,read_byte(ebda_seg,&EbdaData->cdemu.media));
      write_byte(DS,SI+0x02,read_byte(ebda_seg,&EbdaData->cdemu.emulated_drive));
      write_byte(DS,SI+0x03,read_byte(ebda_seg,&EbdaData->cdemu.controller_index));
      write_dword(DS,SI+0x04,read_dword(ebda_seg,&EbdaData->cdemu.ilba));
      write_word(DS,SI+0x08,read_word(ebda_seg,&EbdaData->cdemu.device_spec));
      write_word(DS,SI+0x0a,read_word(ebda_seg,&EbdaData->cdemu.buffer_segment));
      write_word(DS,SI+0x0c,read_word(ebda_seg,&EbdaData->cdemu.load_segment));
      write_word(DS,SI+0x0e,read_word(ebda_seg,&EbdaData->cdemu.sector_count));
      write_byte(DS,SI+0x10,read_byte(ebda_seg,&EbdaData->cdemu.vdevice.cylinders));
      write_byte(DS,SI+0x11,read_byte(ebda_seg,&EbdaData->cdemu.vdevice.spt));
      write_byte(DS,SI+0x12,read_byte(ebda_seg,&EbdaData->cdemu.vdevice.heads));

      // If we have to terminate emulation
      if(GET_AL() == 0x00) {
        // FIXME ElTorito Various. Should be handled accordingly to spec
        write_byte(ebda_seg,&EbdaData->cdemu.active, 0x00); // bye bye
        }

      goto int13_success;
      break;

    default:
      BX_INFO("int13_eltorito: unsupported AH=%02x\n", GET_AH());
      goto int13_fail;
      break;
    }

int13_fail:
    SET_AH(0x01); // defaults to invalid function in AH or invalid parameter
    SET_DISK_RET_STATUS(GET_AH());
    SET_CF();     // error occurred
    return;

int13_success:
    SET_AH(0x00); // no error
    SET_DISK_RET_STATUS(0x00);
    CLEAR_CF();   // no error
    return;
}

// ---------------------------------------------------------------------------
// End of int13 for eltorito functions
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Start of int13 when emulating a device from the cd
// ---------------------------------------------------------------------------

  void
int13_cdemu(DS, ES, DI, SI, BP, SP, BX, DX, CX, AX, IP, CS, FLAGS)
  Bit16u DS, ES, DI, SI, BP, SP, BX, DX, CX, AX, IP, CS, FLAGS;
{
  Bit16u ebda_seg=read_word(0x0040,0x000E);
  Bit8u  device, status;
  Bit16u vheads, vspt, vcylinders;
  Bit16u head, sector, cylinder, nbsectors;
  Bit32u vlba, ilba, slba, elba;
  Bit16u before, segment, offset;
  Bit8u  atacmd[12];

  BX_DEBUG_INT13_ET("int13_cdemu: AX=%04x BX=%04x CX=%04x DX=%04x ES=%04x\n", AX, BX, CX, DX, ES);
  
  /* at this point, we are emulating a floppy/harddisk */
  
  // Recompute the device number 
  device  = read_byte(ebda_seg,&EbdaData->cdemu.controller_index) * 2;
  device += read_byte(ebda_seg,&EbdaData->cdemu.device_spec);

  SET_DISK_RET_STATUS(0x00);

  /* basic checks : emulation should be active, dl should equal the emulated drive */
  if( (read_byte(ebda_seg,&EbdaData->cdemu.active) ==0 )
   || (read_byte(ebda_seg,&EbdaData->cdemu.emulated_drive ) != GET_DL())) {
    BX_INFO("int13_cdemu: function %02x, emulation not active for DL= %02x\n", GET_AH(), GET_DL());
    goto int13_fail;
    }
  
  switch (GET_AH()) {

    // all those functions return SUCCESS
    case 0x00: /* disk controller reset */
    case 0x09: /* initialize drive parameters */
    case 0x0c: /* seek to specified cylinder */
    case 0x0d: /* alternate disk reset */  // FIXME ElTorito Various. should really reset ?
    case 0x10: /* check drive ready */     // FIXME ElTorito Various. should check if ready ?
    case 0x11: /* recalibrate */      
    case 0x14: /* controller internal diagnostic */
    case 0x16: /* detect disk change */
      goto int13_success;
      break;

    // all those functions return disk write-protected
    case 0x03: /* write disk sectors */
    case 0x05: /* format disk track */
      SET_AH(0x03);
      goto int13_fail_noah;
      break;

    case 0x01: /* read disk status */
      status=read_byte(0x0040, 0x0074);
      SET_AH(status);
      SET_DISK_RET_STATUS(0);

      /* set CF if error status read */
      if (status) goto int13_fail_nostatus;
      else        goto int13_success_noah;
      break;

    case 0x02: // read disk sectors
    case 0x04: // verify disk sectors
      vspt       = read_word(ebda_seg,&EbdaData->cdemu.vdevice.spt); 
      vcylinders = read_word(ebda_seg,&EbdaData->cdemu.vdevice.cylinders); 
      vheads     = read_word(ebda_seg,&EbdaData->cdemu.vdevice.heads); 

      ilba       = read_dword(ebda_seg,&EbdaData->cdemu.ilba);

      sector    = GET_CL() & 0x003f;
      cylinder  = (GET_CL() & 0x00c0) << 2 | GET_CH();
      head      = GET_DH();
      nbsectors = GET_AL();
      segment   = ES;
      offset    = BX;

      // no sector to read ?
      if(nbsectors==0) goto int13_success;

      // sanity checks sco openserver needs this!
      if ((sector   >  vspt)
       || (cylinder >= vcylinders)
       || (head     >= vheads)) {
        goto int13_fail;
        }

      // After controls, verify do nothing
      if (GET_AH() == 0x04) goto int13_success;

      segment = ES+(BX / 16);
      offset  = BX % 16;

      // calculate the virtual lba inside the image
      vlba=((((Bit32u)cylinder*(Bit32u)vheads)+(Bit32u)head)*(Bit32u)vspt)+((Bit32u)(sector-1));
 
      // In advance so we don't loose the count
      SET_AL(nbsectors);

      // start lba on cd
      slba  = (Bit32u)vlba/4; 
      before= (Bit16u)vlba%4;

      // end lba on cd
      elba = (Bit32u)(vlba+nbsectors-1)/4;
      
      memsetb(get_SS(),atacmd,0,12);
      atacmd[0]=0x28;                      // READ command
      atacmd[7]=((Bit16u)(elba-slba+1) & 0xff00) >> 8; // Sectors
      atacmd[8]=((Bit16u)(elba-slba+1) & 0x00ff);      // Sectors
      atacmd[2]=(ilba+slba & 0xff000000) >> 24;  // LBA
      atacmd[3]=(ilba+slba & 0x00ff0000) >> 16;
      atacmd[4]=(ilba+slba & 0x0000ff00) >> 8;
      atacmd[5]=(ilba+slba & 0x000000ff);
      if((status = ata_cmd_packet(device, 12, get_SS(), atacmd, before*512, nbsectors*512L, ATA_DATA_IN, segment,offset)) != 0) {
        BX_INFO("int13_cdemu: function %02x, error %02x !\n",GET_AH(),status);
        SET_AH(0x02);
        SET_AL(0);
        goto int13_fail_noah;
        }

      goto int13_success;
      break;

    case 0x08: /* read disk drive parameters */
      vspt=read_word(ebda_seg,&EbdaData->cdemu.vdevice.spt); 
      vcylinders=read_word(ebda_seg,&EbdaData->cdemu.vdevice.cylinders) - 1; 
      vheads=read_word(ebda_seg,&EbdaData->cdemu.vdevice.heads) - 1; 
 
      SET_AL( 0x00 );
      SET_BL( 0x00 );
      SET_CH( vcylinders & 0xff );
      SET_CL((( vcylinders >> 2) & 0xc0) | ( vspt  & 0x3f ));
      SET_DH( vheads );
      SET_DL( 0x02 );   // FIXME ElTorito Various. should send the real count of drives 1 or 2
                        // FIXME ElTorito Harddisk. should send the HD count
 
      switch(read_byte(ebda_seg,&EbdaData->cdemu.media)) {
        case 0x01: SET_BL( 0x02 ); break;
        case 0x02: SET_BL( 0x04 ); break;
        case 0x03: SET_BL( 0x06 ); break;
        }

ASM_START
      push bp
      mov  bp, sp
      mov ax, #diskette_param_table2
      mov _int13_cdemu.DI+2[bp], ax
      mov _int13_cdemu.ES+2[bp], cs
      pop  bp
ASM_END
      goto int13_success;
      break;

    case 0x15: /* read disk drive size */
      // FIXME ElTorito Harddisk. What geometry to send ?
      SET_AH(0x03);
      goto int13_success_noah;
      break;

    // all those functions return unimplemented
    case 0x0a: /* read disk sectors with ECC */
    case 0x0b: /* write disk sectors with ECC */
    case 0x18: /* set media type for format */
    case 0x41: // IBM/MS installation check
      // FIXME ElTorito Harddisk. Darwin would like to use EDD
    case 0x42: // IBM/MS extended read
    case 0x43: // IBM/MS extended write
    case 0x44: // IBM/MS verify sectors
    case 0x45: // IBM/MS lock/unlock drive
    case 0x46: // IBM/MS eject media
    case 0x47: // IBM/MS extended seek
    case 0x48: // IBM/MS get drive parameters 
    case 0x49: // IBM/MS extended media change
    case 0x4e: // ? - set hardware configuration
    case 0x50: // ? - send packet command
    default:
      BX_INFO("int13_cdemu function AH=%02x unsupported, returns fail\n", GET_AH());
      goto int13_fail;
      break;
    }

int13_fail:
    SET_AH(0x01); // defaults to invalid function in AH or invalid parameter
int13_fail_noah:
    SET_DISK_RET_STATUS(GET_AH());
int13_fail_nostatus:
    SET_CF();     // error occurred
    return;

int13_success:
    SET_AH(0x00); // no error
int13_success_noah:
    SET_DISK_RET_STATUS(0x00);
    CLEAR_CF();   // no error
    return;
}

// ---------------------------------------------------------------------------
// End of int13 when emulating a device from the cd
// ---------------------------------------------------------------------------

#endif // BX_ELTORITO_BOOT

#else //BX_USE_ATADRV

  void
outLBA(cylinder,hd_heads,head,hd_sectors,sector,dl)
  Bit16u cylinder;
  Bit16u hd_heads;
  Bit16u head;
  Bit16u hd_sectors;
  Bit16u sector;
  Bit16u dl;
{
ASM_START
        push   bp
        mov    bp, sp
        push   eax
        push   ebx
        push   edx
        xor    eax,eax
        mov    ax,4[bp]  // cylinder
        xor    ebx,ebx
        mov    bl,6[bp]  // hd_heads
        imul   ebx

        mov    bl,8[bp]  // head
        add    eax,ebx
        mov    bl,10[bp] // hd_sectors
        imul   ebx
        mov    bl,12[bp] // sector
        add    eax,ebx

        dec    eax
        mov    dx,#0x1f3
        out    dx,al
        mov    dx,#0x1f4
        mov    al,ah
        out    dx,al
        shr    eax,#16
        mov    dx,#0x1f5
        out    dx,al
        and    ah,#0xf
        mov    bl,14[bp] // dl
        and    bl,#1
        shl    bl,#4
        or     ah,bl
        or     ah,#0xe0
        mov    al,ah
        mov    dx,#0x01f6
        out    dx,al
        pop    edx
        pop    ebx
        pop    eax
        pop    bp
ASM_END
}

  void
int13_harddisk(DS, ES, DI, SI, BP, ELDX, BX, DX, CX, AX, IP, CS, FLAGS)
  Bit16u DS, ES, DI, SI, BP, ELDX, BX, DX, CX, AX, IP, CS, FLAGS;
{
  Bit8u    drive, num_sectors, sector, head, status, mod;
  Bit8u    drive_map;
  Bit8u    n_drives;
  Bit16u   cyl_mod, ax;
  Bit16u   max_cylinder, cylinder, total_sectors;
  Bit16u   hd_cylinders;
  Bit8u    hd_heads, hd_sectors;
  Bit16u   val16;
  Bit8u    sector_count;
  unsigned int i;
  Bit16u   tempbx;
  Bit16u   dpsize;

  Bit16u   count, segment, offset;
  Bit32u   lba;
  Bit16u   error;

  BX_DEBUG_INT13_HD("int13 harddisk: AX=%04x BX=%04x CX=%04x DX=%04x ES=%04x\n", AX, BX, CX, DX, ES);

  write_byte(0x0040, 0x008e, 0);  // clear completion flag

  /* at this point, DL is >= 0x80 to be passed from the floppy int13h
     handler code */
  /* check how many disks first (cmos reg 0x12), return an error if
     drive not present */
  drive_map = inb_cmos(0x12);
  drive_map = (((drive_map & 0xf0)==0) ? 0 : 1) |
              (((drive_map & 0x0f)==0) ? 0 : 2);
  n_drives = (drive_map==0) ? 0 :
    ((drive_map==3) ? 2 : 1);

  if (!(drive_map & (1<<(GET_ELDL()&0x7f)))) { /* allow 0, 1, or 2 disks */
    SET_AH(0x01);
    SET_DISK_RET_STATUS(0x01);
    SET_CF(); /* error occurred */
    return;
    }

  switch (GET_AH()) {

    case 0x00: /* disk controller reset */
BX_DEBUG_INT13_HD("int13_f00\n");

      SET_AH(0);
      SET_DISK_RET_STATUS(0);
      set_diskette_ret_status(0);
      set_diskette_current_cyl(0, 0); /* current cylinder, diskette 1 */
      set_diskette_current_cyl(1, 0); /* current cylinder, diskette 2 */
      CLEAR_CF(); /* successful */
      return;
      break;

    case 0x01: /* read disk status */
BX_DEBUG_INT13_HD("int13_f01\n");
      status = read_byte(0x0040, 0x0074);
      SET_AH(status);
      SET_DISK_RET_STATUS(0);
      /* set CF if error status read */
      if (status) SET_CF();
      else        CLEAR_CF();
      return;
      break;

    case 0x04: // verify disk sectors
    case 0x02: // read disk sectors
      drive = GET_ELDL();
      get_hd_geometry(drive, &hd_cylinders, &hd_heads, &hd_sectors);

      num_sectors = GET_AL();
      cylinder    = (GET_CL() & 0x00c0) << 2 | GET_CH();
      sector      = (GET_CL() & 0x3f);
      head        = GET_DH();


      if (hd_cylinders > 1024) {
        if (hd_cylinders <= 2048) {
          cylinder <<= 1;
          }
        else if (hd_cylinders <= 4096) {
          cylinder <<= 2;
          }
        else if (hd_cylinders <= 8192) {
          cylinder <<= 3;
          }
        else { // hd_cylinders <= 16384
          cylinder <<= 4;
          }

        ax = head / hd_heads;
        cyl_mod = ax & 0xff;
        head    = ax >> 8;
        cylinder |= cyl_mod;
        }

      if ( (cylinder >= hd_cylinders) ||
           (sector > hd_sectors) ||
           (head >= hd_heads) ) {
        SET_AH(1);
        SET_DISK_RET_STATUS(1);
        SET_CF(); /* error occurred */
        return;
        }

      if ( (num_sectors > 128) || (num_sectors == 0) )
        BX_PANIC("int13_harddisk(): num_sectors out of range!\n");

      if (head > 15)
        BX_PANIC("hard drive BIOS:(read/verify) head > 15\n");

      if ( GET_AH() == 0x04 ) {
        SET_AH(0);
        SET_DISK_RET_STATUS(0);
        CLEAR_CF();
        return;
        }

      status = inb(0x1f7);
      if (status & 0x80) {
        BX_PANIC("hard drive BIOS:(read/verify) BUSY bit set\n");
        }
      outb(0x01f2, num_sectors);
      /* activate LBA? (tomv) */
      if (hd_heads > 16) {
BX_DEBUG_INT13_HD("CHS: %x %x %x\n", cylinder, head, sector);
        outLBA(cylinder,hd_heads,head,hd_sectors,sector,drive);
        }
      else {
        outb(0x01f3, sector);
        outb(0x01f4, cylinder & 0x00ff);
        outb(0x01f5, cylinder >> 8);
        outb(0x01f6, 0xa0 | ((drive & 0x01)<<4) | (head & 0x0f));
        }
      outb(0x01f7, 0x20);

      while (1) {
        status = inb(0x1f7);
        if ( !(status & 0x80) ) break;
        }

      if (status & 0x01) {
        BX_PANIC("hard drive BIOS:(read/verify) read error\n");
      } else if ( !(status & 0x08) ) {
        BX_DEBUG_INT13_HD("status was %02x\n", (unsigned) status);
        BX_PANIC("hard drive BIOS:(read/verify) expected DRQ=1\n");
      }

      sector_count = 0;
      tempbx = BX;

ASM_START
  sti  ;; enable higher priority interrupts
ASM_END

      while (1) {
ASM_START
        ;; store temp bx in real DI register
        push bp
        mov  bp, sp
        mov  di, _int13_harddisk.tempbx + 2 [bp]
        pop  bp

        ;; adjust if there will be an overrun
        cmp   di, #0xfe00
        jbe   i13_f02_no_adjust
i13_f02_adjust:
        sub   di, #0x0200 ; sub 512 bytes from offset
        mov   ax, es
        add   ax, #0x0020 ; add 512 to segment
        mov   es, ax

i13_f02_no_adjust:
        mov  cx, #0x0100   ;; counter (256 words = 512b)
        mov  dx, #0x01f0  ;; AT data read port

        rep
          insw ;; CX words transfered from port(DX) to ES:[DI]

i13_f02_done:
        ;; store real DI register back to temp bx
        push bp
        mov  bp, sp
        mov  _int13_harddisk.tempbx + 2 [bp], di
        pop  bp
ASM_END

        sector_count++;
        num_sectors--;
        if (num_sectors == 0) {
          status = inb(0x1f7);
          if ( (status & 0xc9) != 0x40 )
            BX_PANIC("no sectors left to read/verify, status is %02x\n", (unsigned) status);
          break;
          }
        else {
          status = inb(0x1f7);
          if ( (status & 0xc9) != 0x48 )
            BX_PANIC("more sectors left to read/verify, status is %02x\n", (unsigned) status);
          continue;
          }
        }

      SET_AH(0);
      SET_DISK_RET_STATUS(0);
      SET_AL(sector_count);
      CLEAR_CF(); /* successful */
      return;
      break;


    case 0x03: /* write disk sectors */
BX_DEBUG_INT13_HD("int13_f03\n");
      drive = GET_ELDL ();
      get_hd_geometry(drive, &hd_cylinders, &hd_heads, &hd_sectors);

      num_sectors = GET_AL();
      cylinder    = GET_CH();
      cylinder    |= ( ((Bit16u) GET_CL()) << 2) & 0x300;
      sector      = (GET_CL() & 0x3f);
      head        = GET_DH();

      if (hd_cylinders > 1024) {
        if (hd_cylinders <= 2048) {
          cylinder <<= 1;
          }
        else if (hd_cylinders <= 4096) {
          cylinder <<= 2;
          }
        else if (hd_cylinders <= 8192) {
          cylinder <<= 3;
          }
        else { // hd_cylinders <= 16384
          cylinder <<= 4;
          }

        ax = head / hd_heads;
        cyl_mod = ax & 0xff;
        head    = ax >> 8;
        cylinder |= cyl_mod;
        }

      if ( (cylinder >= hd_cylinders) ||
           (sector > hd_sectors) ||
           (head >= hd_heads) ) {
        SET_AH( 1);
        SET_DISK_RET_STATUS(1);
        SET_CF(); /* error occurred */
        return;
        }

      if ( (num_sectors > 128) || (num_sectors == 0) )
        BX_PANIC("int13_harddisk(): num_sectors out of range!\n");

      if (head > 15)
        BX_PANIC("hard drive BIOS:(read) head > 15\n");

      status = inb(0x1f7);
      if (status & 0x80) {
        BX_PANIC("hard drive BIOS:(read) BUSY bit set\n");
        }
// should check for Drive Ready Bit also in status reg
      outb(0x01f2, num_sectors);

      /* activate LBA? (tomv) */
      if (hd_heads > 16) {
BX_DEBUG_INT13_HD("CHS (write): %x %x %x\n", cylinder, head, sector);
        outLBA(cylinder,hd_heads,head,hd_sectors,sector,GET_ELDL());
        }
      else {
        outb(0x01f3, sector);
        outb(0x01f4, cylinder & 0x00ff);
        outb(0x01f5, cylinder >> 8);
        outb(0x01f6, 0xa0 | ((GET_ELDL() & 0x01)<<4) | (head & 0x0f));
        }
      outb(0x01f7, 0x30);

      // wait for busy bit to turn off after seeking
      while (1) {
        status = inb(0x1f7);
        if ( !(status & 0x80) ) break;
        }

      if ( !(status & 0x08) ) {
        BX_DEBUG_INT13_HD("status was %02x\n", (unsigned) status);
        BX_PANIC("hard drive BIOS:(write) data-request bit not set\n");
        }

      sector_count = 0;
      tempbx = BX;

ASM_START
  sti  ;; enable higher priority interrupts
ASM_END

      while (1) {
ASM_START
        ;; store temp bx in real SI register
        push bp
        mov  bp, sp
        mov  si, _int13_harddisk.tempbx + 2 [bp]
        pop  bp

        ;; adjust if there will be an overrun
        cmp   si, #0xfe00
        jbe   i13_f03_no_adjust
i13_f03_adjust:
        sub   si, #0x0200 ; sub 512 bytes from offset
        mov   ax, es
        add   ax, #0x0020 ; add 512 to segment
        mov   es, ax

i13_f03_no_adjust:
        mov  cx, #0x0100   ;; counter (256 words = 512b)
        mov  dx, #0x01f0  ;; AT data read port

        seg ES
        rep
          outsw ;; CX words tranfered from ES:[SI] to port(DX)

        ;; store real SI register back to temp bx
        push bp
        mov  bp, sp
        mov  _int13_harddisk.tempbx + 2 [bp], si
        pop  bp
ASM_END

        sector_count++;
        num_sectors--;
        if (num_sectors == 0) {
          status = inb(0x1f7);
          if ( (status & 0xe9) != 0x40 )
            BX_PANIC("no sectors left to write, status is %02x\n", (unsigned) status);
          break;
          }
        else {
          status = inb(0x1f7);
          if ( (status & 0xc9) != 0x48 )
            BX_PANIC("more sectors left to write, status is %02x\n", (unsigned) status);
          continue;
          }
        }

      SET_AH(0);
      SET_DISK_RET_STATUS(0);
      SET_AL(sector_count);
      CLEAR_CF(); /* successful */
      return;
      break;

    case 0x05: /* format disk track */
BX_DEBUG_INT13_HD("int13_f05\n");
      BX_PANIC("format disk track called\n");
      /* nop */
      SET_AH(0);
      SET_DISK_RET_STATUS(0);
      CLEAR_CF(); /* successful */
      return;
      break;

    case 0x08: /* read disk drive parameters */
BX_DEBUG_INT13_HD("int13_f08\n");
      
      drive = GET_ELDL ();
      get_hd_geometry(drive, &hd_cylinders, &hd_heads, &hd_sectors);

      // translate CHS
      //
      if (hd_cylinders <= 1024) {
        // hd_cylinders >>= 0;
        // hd_heads <<= 0;
        }
      else if (hd_cylinders <= 2048) {
        hd_cylinders >>= 1;
        hd_heads <<= 1;
        }
      else if (hd_cylinders <= 4096) {
        hd_cylinders >>= 2;
        hd_heads <<= 2;
        }
      else if (hd_cylinders <= 8192) {
        hd_cylinders >>= 3;
        hd_heads <<= 3;
        }
      else { // hd_cylinders <= 16384
        hd_cylinders >>= 4;
        hd_heads <<= 4;
        }

      max_cylinder = hd_cylinders - 2; /* 0 based */
      SET_AL(0);
      SET_CH(max_cylinder & 0xff);
      SET_CL(((max_cylinder >> 2) & 0xc0) | (hd_sectors & 0x3f));
      SET_DH(hd_heads - 1);
      SET_DL(n_drives); /* returns 0, 1, or 2 hard drives */
      SET_AH(0);
      SET_DISK_RET_STATUS(0);
      CLEAR_CF(); /* successful */

      return;
      break;

    case 0x09: /* initialize drive parameters */
BX_DEBUG_INT13_HD("int13_f09\n");
      SET_AH(0);
      SET_DISK_RET_STATUS(0);
      CLEAR_CF(); /* successful */
      return;
      break;

    case 0x0a: /* read disk sectors with ECC */
BX_DEBUG_INT13_HD("int13_f0a\n");
    case 0x0b: /* write disk sectors with ECC */
BX_DEBUG_INT13_HD("int13_f0b\n");
      BX_PANIC("int13h Functions 0Ah & 0Bh not implemented!\n");
      return;
      break;

    case 0x0c: /* seek to specified cylinder */
BX_DEBUG_INT13_HD("int13_f0c\n");
      BX_INFO("int13h function 0ch (seek) not implemented!\n");
      SET_AH(0);
      SET_DISK_RET_STATUS(0);
      CLEAR_CF(); /* successful */
      return;
      break;

    case 0x0d: /* alternate disk reset */
BX_DEBUG_INT13_HD("int13_f0d\n");
      SET_AH(0);
      SET_DISK_RET_STATUS(0);
      CLEAR_CF(); /* successful */
      return;
      break;

    case 0x10: /* check drive ready */
BX_DEBUG_INT13_HD("int13_f10\n");
      //SET_AH(0);
      //SET_DISK_RET_STATUS(0);
      //CLEAR_CF(); /* successful */
      //return;
      //break;

      // should look at 40:8E also???
      status = inb(0x01f7);
      if ( (status & 0xc0) == 0x40 ) {
        SET_AH(0);
        SET_DISK_RET_STATUS(0);
        CLEAR_CF(); // drive ready
        return;
        }
      else {
        SET_AH(0xAA);
        SET_DISK_RET_STATUS(0xAA);
        SET_CF(); // not ready
        return;
        }
      break;

    case 0x11: /* recalibrate */
BX_DEBUG_INT13_HD("int13_f11\n");
      SET_AH(0);
      SET_DISK_RET_STATUS(0);
      CLEAR_CF(); /* successful */
      return;
      break;

    case 0x14: /* controller internal diagnostic */
BX_DEBUG_INT13_HD("int13_f14\n");
      SET_AH(0);
      SET_DISK_RET_STATUS(0);
      CLEAR_CF(); /* successful */
      SET_AL(0);
      return;
      break;

    case 0x15: /* read disk drive size */
      drive = GET_ELDL();
      get_hd_geometry(drive, &hd_cylinders, &hd_heads, &hd_sectors);
ASM_START
      push bp
      mov  bp, sp
      mov  al, _int13_harddisk.hd_heads + 2 [bp]
      mov  ah, _int13_harddisk.hd_sectors + 2 [bp]
      mul  al, ah ;; ax = heads * sectors
      mov  bx, _int13_harddisk.hd_cylinders + 2 [bp]
      dec  bx     ;; use (cylinders - 1) ???
      mul  ax, bx ;; dx:ax = (cylinders -1) * (heads * sectors)
      ;; now we need to move the 32bit result dx:ax to what the
      ;; BIOS wants which is cx:dx.
      ;; and then into CX:DX on the stack
      mov  _int13_harddisk.CX + 2 [bp], dx
      mov  _int13_harddisk.DX + 2 [bp], ax
      pop  bp
ASM_END
      SET_AH(3);  // hard disk accessible
      SET_DISK_RET_STATUS(0); // ??? should this be 0
      CLEAR_CF(); // successful
      return;
      break;

    case 0x18: // set media type for format
    case 0x41: // IBM/MS 
    case 0x42: // IBM/MS 
    case 0x43: // IBM/MS 
    case 0x44: // IBM/MS 
    case 0x45: // IBM/MS lock/unlock drive
    case 0x46: // IBM/MS eject media
    case 0x47: // IBM/MS extended seek
    case 0x49: // IBM/MS extended media change
    case 0x50: // IBM/MS send packet command
    default:
      BX_INFO("int13_harddisk: unsupported AH=%02x\n", GET_AH());

      SET_AH(1);  // code=invalid function in AH or invalid parameter
      SET_DISK_RET_STATUS(1);
      SET_CF(); /* unsuccessful */
      return;
      break;
    }
}

static char panic_msg_reg12h[] = "HD%d cmos reg 12h not type F\n";
static char panic_msg_reg19h[] = "HD%d cmos reg %02xh not user definable type 47\n";

  void
get_hd_geometry(drive, hd_cylinders, hd_heads, hd_sectors)
  Bit8u drive;
  Bit16u *hd_cylinders;
  Bit8u  *hd_heads;
  Bit8u  *hd_sectors;
{
  Bit8u hd_type;
  Bit16u ss;
  Bit16u cylinders;
  Bit8u iobase;

  ss = get_SS();
  if (drive == 0x80) {
    hd_type = inb_cmos(0x12) & 0xf0;
    if (hd_type != 0xf0)
      BX_INFO(panic_msg_reg12h,0);
    hd_type = inb_cmos(0x19); // HD0: extended type
    if (hd_type != 47)
      BX_INFO(panic_msg_reg19h,0,0x19);
    iobase = 0x1b;
  } else {
    hd_type = inb_cmos(0x12) & 0x0f;
    if (hd_type != 0x0f)
      BX_INFO(panic_msg_reg12h,1);
    hd_type = inb_cmos(0x1a); // HD0: extended type
    if (hd_type != 47)
      BX_INFO(panic_msg_reg19h,0,0x1a);
    iobase = 0x24;
  }

  // cylinders
  cylinders = inb_cmos(iobase) | (inb_cmos(iobase+1) << 8);
  write_word(ss, hd_cylinders, cylinders);

  // heads
  write_byte(ss, hd_heads, inb_cmos(iobase+2));

  // sectors per track
  write_byte(ss, hd_sectors, inb_cmos(iobase+8));
}

#endif //else BX_USE_ATADRV


//////////////////////
// FLOPPY functions //
//////////////////////

void floppy_reset_controller()
{
  Bit8u val8;

  // Reset controller
  val8 = inb(0x03f2);
  outb(0x03f2, val8 & ~0x04);
  outb(0x03f2, val8 | 0x04);

  // Wait for controller to come out of reset  
  do {
    val8 = inb(0x3f4);
  } while ( (val8 & 0xc0) != 0x80 );
}

void floppy_prepare_controller(drive)
  Bit16u drive;
{
  Bit8u  val8, dor, prev_reset;

  // set 40:3e bit 7 to 0
  val8 = read_byte(0x0040, 0x003e);
  val8 &= 0x7f;
  write_byte(0x0040, 0x003e, val8);

  // turn on motor of selected drive, DMA & int enabled, normal operation
  prev_reset = inb(0x03f2) & 0x04;
  if (drive)
    dor = 0x20;
  else
    dor = 0x10;
  dor |= 0x0c;
  dor |= drive;
  outb(0x03f2, dor);

  // reset the disk motor timeout value of INT 08
  write_byte(0x40,0x40, BX_FLOPPY_ON_CNT);

  // wait for drive readiness
  do {
    val8 = inb(0x3f4);
  } while ( (val8 & 0xc0) != 0x80 );

  if (prev_reset == 0) {
    // turn on interrupts
ASM_START
    sti
ASM_END
    // wait on 40:3e bit 7 to become 1
    do {
      val8 = read_byte(0x0040, 0x003e);
    } while ( (val8 & 0x80) == 0 );
    val8 &= 0x7f;
ASM_START
    cli
ASM_END
    write_byte(0x0040, 0x003e, val8);
  }
}

  bx_bool
floppy_media_known(drive)
  Bit16u drive;
{
  Bit8u  val8;
  Bit16u media_state_offset;

  val8 = read_byte(0x0040, 0x003e); // diskette recal status
  if (drive)
    val8 >>= 1;
  val8 &= 0x01;
  if (val8 == 0)
    return(0);

  media_state_offset = 0x0090;
  if (drive)
    media_state_offset += 1;

  val8 = read_byte(0x0040, media_state_offset);
  val8 = (val8 >> 4) & 0x01;
  if (val8 == 0)
    return(0);

  // check pass, return KNOWN
  return(1);
}

  bx_bool
floppy_media_sense(drive)
  Bit16u drive;
{
  bx_bool retval;
  Bit16u  media_state_offset;
  Bit8u   drive_type, config_data, media_state;

  if (floppy_drive_recal(drive) == 0) {
    return(0);
    }

  // for now cheat and get drive type from CMOS,
  // assume media is same as drive type

  // ** config_data **
  // Bitfields for diskette media control:
  // Bit(s)  Description (Table M0028)
  //  7-6  last data rate set by controller
  //        00=500kbps, 01=300kbps, 10=250kbps, 11=1Mbps
  //  5-4  last diskette drive step rate selected
  //        00=0Ch, 01=0Dh, 10=0Eh, 11=0Ah
  //  3-2  {data rate at start of operation}
  //  1-0  reserved

  // ** media_state **
  // Bitfields for diskette drive media state:
  // Bit(s)  Description (Table M0030)
  //  7-6  data rate
  //    00=500kbps, 01=300kbps, 10=250kbps, 11=1Mbps
  //  5  double stepping required (e.g. 360kB in 1.2MB)
  //  4  media type established
  //  3  drive capable of supporting 4MB media
  //  2-0  on exit from BIOS, contains
  //    000 trying 360kB in 360kB
  //    001 trying 360kB in 1.2MB
  //    010 trying 1.2MB in 1.2MB
  //    011 360kB in 360kB established
  //    100 360kB in 1.2MB established
  //    101 1.2MB in 1.2MB established
  //    110 reserved
  //    111 all other formats/drives

  drive_type = inb_cmos(0x10);
  if (drive == 0)
    drive_type >>= 4;
  else
    drive_type &= 0x0f;
  if ( drive_type == 1 ) {
    // 360K 5.25" drive
    config_data = 0x00; // 0000 0000
    media_state = 0x25; // 0010 0101
    retval = 1;
    }
  else if ( drive_type == 2 ) {
    // 1.2 MB 5.25" drive
    config_data = 0x00; // 0000 0000
    media_state = 0x25; // 0010 0101   // need double stepping??? (bit 5)
    retval = 1;
    }
  else if ( drive_type == 3 ) {
    // 720K 3.5" drive
    config_data = 0x00; // 0000 0000 ???
    media_state = 0x17; // 0001 0111
    retval = 1;
    }
  else if ( drive_type == 4 ) {
    // 1.44 MB 3.5" drive
    config_data = 0x00; // 0000 0000
    media_state = 0x17; // 0001 0111
    retval = 1;
    }
  else if ( drive_type == 5 ) {
    // 2.88 MB 3.5" drive
    config_data = 0xCC; // 1100 1100
    media_state = 0xD7; // 1101 0111
    retval = 1;
    }
  //
  // Extended floppy size uses special cmos setting 
  else if ( drive_type == 6 ) {
    // 160k 5.25" drive
    config_data = 0x00; // 0000 0000
    media_state = 0x27; // 0010 0111
    retval = 1;
    }
  else if ( drive_type == 7 ) {
    // 180k 5.25" drive
    config_data = 0x00; // 0000 0000
    media_state = 0x27; // 0010 0111
    retval = 1;
    }
  else if ( drive_type == 8 ) {
    // 320k 5.25" drive
    config_data = 0x00; // 0000 0000
    media_state = 0x27; // 0010 0111
    retval = 1;
    }

  else {
    // not recognized
    config_data = 0x00; // 0000 0000
    media_state = 0x00; // 0000 0000
    retval = 0;
    }

  if (drive == 0)
    media_state_offset = 0x90;
  else
    media_state_offset = 0x91;
  write_byte(0x0040, 0x008B, config_data);
  write_byte(0x0040, media_state_offset, media_state);

  return(retval);
}

  bx_bool
floppy_drive_recal(drive)
  Bit16u drive;
{
  Bit8u  val8;
  Bit16u curr_cyl_offset;

  floppy_prepare_controller(drive);

  // send Recalibrate command (2 bytes) to controller
  outb(0x03f5, 0x07);  // 07: Recalibrate
  outb(0x03f5, drive); // 0=drive0, 1=drive1

  // turn on interrupts
ASM_START
  sti
ASM_END

  // wait on 40:3e bit 7 to become 1
  do {
    val8 = (read_byte(0x0040, 0x003e) & 0x80);
  } while ( val8 == 0 );

  val8 = 0; // separate asm from while() loop
  // turn off interrupts
ASM_START
  cli
ASM_END

  // set 40:3e bit 7 to 0, and calibrated bit
  val8 = read_byte(0x0040, 0x003e);
  val8 &= 0x7f;
  if (drive) {
    val8 |= 0x02; // Drive 1 calibrated
    curr_cyl_offset = 0x0095;
  } else {
    val8 |= 0x01; // Drive 0 calibrated
    curr_cyl_offset = 0x0094;
  }
  write_byte(0x0040, 0x003e, val8);
  write_byte(0x0040, curr_cyl_offset, 0); // current cylinder is 0

  return(1);
}



  bx_bool
floppy_drive_exists(drive)
  Bit16u drive;
{
  Bit8u  drive_type;

  // check CMOS to see if drive exists
  drive_type = inb_cmos(0x10);
  if (drive == 0)
    drive_type >>= 4;
  else
    drive_type &= 0x0f;
  if ( drive_type == 0 )
    return(0);
  else
    return(1);
}

#if BX_SUPPORT_FLOPPY
  void
int13_diskette_function(DS, ES, DI, SI, BP, ELDX, BX, DX, CX, AX, IP, CS, FLAGS)
  Bit16u DS, ES, DI, SI, BP, ELDX, BX, DX, CX, AX, IP, CS, FLAGS;
{
  Bit8u  drive, num_sectors, track, sector, head, status;
  Bit16u base_address, base_count, base_es;
  Bit8u  page, mode_register, val8, dor;
  Bit8u  return_status[7];
  Bit8u  drive_type, num_floppies, ah;
  Bit16u es, last_addr;

  BX_DEBUG_INT13_FL("int13_diskette: AX=%04x BX=%04x CX=%04x DX=%04x ES=%04x\n", AX, BX, CX, DX, ES);

  ah = GET_AH();

  switch ( ah ) {
    case 0x00: // diskette controller reset
BX_DEBUG_INT13_FL("floppy f00\n");
      drive = GET_ELDL();
      if (drive > 1) {
        SET_AH(1); // invalid param
        set_diskette_ret_status(1);
        SET_CF();
        return;
      }
      drive_type = inb_cmos(0x10);

      if (drive == 0)
        drive_type >>= 4;
      else
        drive_type &= 0x0f;
      if (drive_type == 0) {
        SET_AH(0x80); // drive not responding
        set_diskette_ret_status(0x80);
        SET_CF();
        return;
      }
      SET_AH(0);
      set_diskette_ret_status(0);
      CLEAR_CF(); // successful
      set_diskette_current_cyl(drive, 0); // current cylinder
      return;

    case 0x01: // Read Diskette Status
      CLEAR_CF();
      val8 = read_byte(0x0000, 0x0441);
      SET_AH(val8);
      if (val8) {
        SET_CF();
      }
      return;

    case 0x02: // Read Diskette Sectors
    case 0x03: // Write Diskette Sectors
    case 0x04: // Verify Diskette Sectors
      num_sectors = GET_AL();
      track       = GET_CH();
      sector      = GET_CL();
      head        = GET_DH();
      drive       = GET_ELDL();

      if ( (drive > 1) || (head > 1) ||
           (num_sectors == 0) || (num_sectors > 72) ) {
BX_INFO("floppy: drive>1 || head>1 ...\n");
        SET_AH(1);
        set_diskette_ret_status(1);
        SET_AL(0); // no sectors read
        SET_CF(); // error occurred
        return;
      }

      // see if drive exists
      if (floppy_drive_exists(drive) == 0) {
        SET_AH(0x80); // not responding
        set_diskette_ret_status(0x80);
        SET_AL(0); // no sectors read
        SET_CF(); // error occurred
        return;
      }

      // see if media in drive, and type is known
      if (floppy_media_known(drive) == 0) {
        if (floppy_media_sense(drive) == 0) {
          SET_AH(0x0C); // Media type not found
          set_diskette_ret_status(0x0C);
          SET_AL(0); // no sectors read
          SET_CF(); // error occurred
          return;
        }
      }

      if (ah == 0x02) {
        // Read Diskette Sectors

        //-----------------------------------
        // set up DMA controller for transfer
        //-----------------------------------

        // es:bx = pointer to where to place information from diskette
        // port 04: DMA-1 base and current address, channel 2
        // port 05: DMA-1 base and current count, channel 2
        page = (ES >> 12);   // upper 4 bits
        base_es = (ES << 4); // lower 16bits contributed by ES
        base_address = base_es + BX; // lower 16 bits of address
                                     // contributed by ES:BX
        if ( base_address < base_es ) {
          // in case of carry, adjust page by 1
          page++;
        }
        base_count = (num_sectors * 512) - 1;

        // check for 64K boundary overrun
        last_addr = base_address + base_count;
        if (last_addr < base_address) {
          SET_AH(0x09);
          set_diskette_ret_status(0x09);
          SET_AL(0); // no sectors read
          SET_CF(); // error occurred
          return;
        }

        BX_DEBUG_INT13_FL("masking DMA-1 c2\n");
        outb(0x000a, 0x06);

  BX_DEBUG_INT13_FL("clear flip-flop\n");
        outb(0x000c, 0x00); // clear flip-flop
        outb(0x0004, base_address);
        outb(0x0004, base_address>>8);
  BX_DEBUG_INT13_FL("clear flip-flop\n");
        outb(0x000c, 0x00); // clear flip-flop
        outb(0x0005, base_count);
        outb(0x0005, base_count>>8);

        // port 0b: DMA-1 Mode Register
        mode_register = 0x46; // single mode, increment, autoinit disable,
                              // transfer type=write, channel 2
  BX_DEBUG_INT13_FL("setting mode register\n");
        outb(0x000b, mode_register);

  BX_DEBUG_INT13_FL("setting page register\n");
        // port 81: DMA-1 Page Register, channel 2
        outb(0x0081, page);

  BX_DEBUG_INT13_FL("unmask chan 2\n");
        outb(0x000a, 0x02); // unmask channel 2

        BX_DEBUG_INT13_FL("unmasking DMA-1 c2\n");
        outb(0x000a, 0x02);

        //--------------------------------------
        // set up floppy controller for transfer
        //--------------------------------------
        floppy_prepare_controller(drive);

        // send read-normal-data command (9 bytes) to controller
        outb(0x03f5, 0xe6); // e6: read normal data
        outb(0x03f5, (head << 2) | drive); // HD DR1 DR2
        outb(0x03f5, track);
        outb(0x03f5, head);
        outb(0x03f5, sector);
        outb(0x03f5, 2); // 512 byte sector size
        outb(0x03f5, sector + num_sectors - 1); // last sector to read on track
        outb(0x03f5, 0); // Gap length
        outb(0x03f5, 0xff); // Gap length

        // turn on interrupts
  ASM_START
        sti
  ASM_END

        // wait on 40:3e bit 7 to become 1
        do {
          val8 = read_byte(0x0040, 0x0040);
          if (val8 == 0) {
            floppy_reset_controller();
            SET_AH(0x80); // drive not ready (timeout)
            set_diskette_ret_status(0x80);
            SET_AL(0); // no sectors read
            SET_CF(); // error occurred
            return;
          }
          val8 = (read_byte(0x0040, 0x003e) & 0x80);
        } while ( val8 == 0 );

        val8 = 0; // separate asm from while() loop
        // turn off interrupts
  ASM_START
        cli
  ASM_END

        // set 40:3e bit 7 to 0
        val8 = read_byte(0x0040, 0x003e);
        val8 &= 0x7f;
        write_byte(0x0040, 0x003e, val8);

        // check port 3f4 for accessibility to status bytes
        val8 = inb(0x3f4);
        if ( (val8 & 0xc0) != 0xc0 )
          BX_PANIC("int13_diskette: ctrl not ready\n");

        // read 7 return status bytes from controller
        // using loop index broken, have to unroll...
        return_status[0] = inb(0x3f5);
        return_status[1] = inb(0x3f5);
        return_status[2] = inb(0x3f5);
        return_status[3] = inb(0x3f5);
        return_status[4] = inb(0x3f5);
        return_status[5] = inb(0x3f5);
        return_status[6] = inb(0x3f5);
        // record in BIOS Data Area
        write_byte(0x0040, 0x0042, return_status[0]);
        write_byte(0x0040, 0x0043, return_status[1]);
        write_byte(0x0040, 0x0044, return_status[2]);
        write_byte(0x0040, 0x0045, return_status[3]);
        write_byte(0x0040, 0x0046, return_status[4]);
        write_byte(0x0040, 0x0047, return_status[5]);
        write_byte(0x0040, 0x0048, return_status[6]);

        if ( (return_status[0] & 0xc0) != 0 ) {
          SET_AH(0x20);
          set_diskette_ret_status(0x20);
          SET_AL(0); // no sectors read
          SET_CF(); // error occurred
          return;
        }

        // ??? should track be new val from return_status[3] ?
        set_diskette_current_cyl(drive, track);
        // AL = number of sectors read (same value as passed)
        SET_AH(0x00); // success
        CLEAR_CF();   // success
        return;
      } else if (ah == 0x03) {
        // Write Diskette Sectors

        //-----------------------------------
        // set up DMA controller for transfer
        //-----------------------------------

        // es:bx = pointer to where to place information from diskette
        // port 04: DMA-1 base and current address, channel 2
        // port 05: DMA-1 base and current count, channel 2
        page = (ES >> 12);   // upper 4 bits
        base_es = (ES << 4); // lower 16bits contributed by ES
        base_address = base_es + BX; // lower 16 bits of address
                                     // contributed by ES:BX
        if ( base_address < base_es ) {
          // in case of carry, adjust page by 1
          page++;
        }
        base_count = (num_sectors * 512) - 1;

        // check for 64K boundary overrun
        last_addr = base_address + base_count;
        if (last_addr < base_address) {
          SET_AH(0x09);
          set_diskette_ret_status(0x09);
          SET_AL(0); // no sectors read
          SET_CF(); // error occurred
          return;
        }

        BX_DEBUG_INT13_FL("masking DMA-1 c2\n");
        outb(0x000a, 0x06);

        outb(0x000c, 0x00); // clear flip-flop
        outb(0x0004, base_address);
        outb(0x0004, base_address>>8);
        outb(0x000c, 0x00); // clear flip-flop
        outb(0x0005, base_count);
        outb(0x0005, base_count>>8);

        // port 0b: DMA-1 Mode Register
        mode_register = 0x4a; // single mode, increment, autoinit disable,
                              // transfer type=read, channel 2
        outb(0x000b, mode_register);

        // port 81: DMA-1 Page Register, channel 2
        outb(0x0081, page);

        BX_DEBUG_INT13_FL("unmasking DMA-1 c2\n");
        outb(0x000a, 0x02);

        //--------------------------------------
        // set up floppy controller for transfer
        //--------------------------------------
        floppy_prepare_controller(drive);

        // send write-normal-data command (9 bytes) to controller
        outb(0x03f5, 0xc5); // c5: write normal data
        outb(0x03f5, (head << 2) | drive); // HD DR1 DR2
        outb(0x03f5, track);
        outb(0x03f5, head);
        outb(0x03f5, sector);
        outb(0x03f5, 2); // 512 byte sector size
        outb(0x03f5, sector + num_sectors - 1); // last sector to write on track
        outb(0x03f5, 0); // Gap length
        outb(0x03f5, 0xff); // Gap length

        // turn on interrupts
  ASM_START
        sti
  ASM_END

        // wait on 40:3e bit 7 to become 1
        do {
          val8 = read_byte(0x0040, 0x0040);
          if (val8 == 0) {
            floppy_reset_controller();
            SET_AH(0x80); // drive not ready (timeout)
            set_diskette_ret_status(0x80);
            SET_AL(0); // no sectors written
            SET_CF(); // error occurred
            return;
          }
          val8 = (read_byte(0x0040, 0x003e) & 0x80);
        } while ( val8 == 0 );

        val8 = 0; // separate asm from while() loop
        // turn off interrupts
  ASM_START
        cli
  ASM_END

        // set 40:3e bit 7 to 0
        val8 = read_byte(0x0040, 0x003e);
        val8 &= 0x7f;
        write_byte(0x0040, 0x003e, val8);

        // check port 3f4 for accessibility to status bytes
        val8 = inb(0x3f4);
        if ( (val8 & 0xc0) != 0xc0 )
          BX_PANIC("int13_diskette: ctrl not ready\n");

        // read 7 return status bytes from controller
        // using loop index broken, have to unroll...
        return_status[0] = inb(0x3f5);
        return_status[1] = inb(0x3f5);
        return_status[2] = inb(0x3f5);
        return_status[3] = inb(0x3f5);
        return_status[4] = inb(0x3f5);
        return_status[5] = inb(0x3f5);
        return_status[6] = inb(0x3f5);
        // record in BIOS Data Area
        write_byte(0x0040, 0x0042, return_status[0]);
        write_byte(0x0040, 0x0043, return_status[1]);
        write_byte(0x0040, 0x0044, return_status[2]);
        write_byte(0x0040, 0x0045, return_status[3]);
        write_byte(0x0040, 0x0046, return_status[4]);
        write_byte(0x0040, 0x0047, return_status[5]);
        write_byte(0x0040, 0x0048, return_status[6]);

        if ( (return_status[0] & 0xc0) != 0 ) {
          if ( (return_status[1] & 0x02) != 0 ) {
            // diskette not writable.
            // AH=status code=0x03 (tried to write on write-protected disk)
            // AL=number of sectors written=0
            AX = 0x0300;
            SET_CF();
            return;
          } else {
            BX_PANIC("int13_diskette_function: read error\n");
          }
        }

        // ??? should track be new val from return_status[3] ?
        set_diskette_current_cyl(drive, track);
        // AL = number of sectors read (same value as passed)
        SET_AH(0x00); // success
        CLEAR_CF();   // success
        return;
      } else {  // if (ah == 0x04)
        // Verify Diskette Sectors

        // ??? should track be new val from return_status[3] ?
        set_diskette_current_cyl(drive, track);
        // AL = number of sectors verified (same value as passed)
        CLEAR_CF();   // success
        SET_AH(0x00); // success
        return;
      }
      break;

    case 0x05: // format diskette track
BX_DEBUG_INT13_FL("floppy f05\n");

      num_sectors = GET_AL();
      track       = GET_CH();
      head        = GET_DH();
      drive       = GET_ELDL();

      if ((drive > 1) || (head > 1) || (track > 79) ||
          (num_sectors == 0) || (num_sectors > 18)) {
        SET_AH(1);
        set_diskette_ret_status(1);
        SET_CF(); // error occurred
      }

      // see if drive exists
      if (floppy_drive_exists(drive) == 0) {
        SET_AH(0x80); // drive not responding
        set_diskette_ret_status(0x80);
        SET_CF(); // error occurred
        return;
      }

      // see if media in drive, and type is known
      if (floppy_media_known(drive) == 0) {
        if (floppy_media_sense(drive) == 0) {
          SET_AH(0x0C); // Media type not found
          set_diskette_ret_status(0x0C);
          SET_AL(0); // no sectors read
          SET_CF(); // error occurred
          return;
        }
      }

      // set up DMA controller for transfer
      page = (ES >> 12);   // upper 4 bits
      base_es = (ES << 4); // lower 16bits contributed by ES
      base_address = base_es + BX; // lower 16 bits of address
                                   // contributed by ES:BX
      if ( base_address < base_es ) {
        // in case of carry, adjust page by 1
        page++;
      }
      base_count = (num_sectors * 4) - 1;

      // check for 64K boundary overrun
      last_addr = base_address + base_count;
      if (last_addr < base_address) {
        SET_AH(0x09);
        set_diskette_ret_status(0x09);
        SET_AL(0); // no sectors read
        SET_CF(); // error occurred
        return;
      }

      outb(0x000a, 0x06);
      outb(0x000c, 0x00); // clear flip-flop
      outb(0x0004, base_address);
      outb(0x0004, base_address>>8);
      outb(0x000c, 0x00); // clear flip-flop
      outb(0x0005, base_count);
      outb(0x0005, base_count>>8);
      mode_register = 0x4a; // single mode, increment, autoinit disable,
                            // transfer type=read, channel 2
      outb(0x000b, mode_register);
      // port 81: DMA-1 Page Register, channel 2
      outb(0x0081, page);
      outb(0x000a, 0x02);

      // set up floppy controller for transfer
      floppy_prepare_controller(drive);

      // send format-track command (6 bytes) to controller
      outb(0x03f5, 0x4d); // 4d: format track
      outb(0x03f5, (head << 2) | drive); // HD DR1 DR2
      outb(0x03f5, 2); // 512 byte sector size
      outb(0x03f5, num_sectors); // number of sectors per track
      outb(0x03f5, 0); // Gap length
      outb(0x03f5, 0xf6); // Fill byte
      // turn on interrupts
  ASM_START
      sti
  ASM_END

      // wait on 40:3e bit 7 to become 1
      do {
        val8 = read_byte(0x0040, 0x0040);
        if (val8 == 0) {
          floppy_reset_controller();
          SET_AH(0x80); // drive not ready (timeout)
          set_diskette_ret_status(0x80);
          SET_CF(); // error occurred
          return;
        }
        val8 = (read_byte(0x0040, 0x003e) & 0x80);
      } while ( val8 == 0 );

      val8 = 0; // separate asm from while() loop
      // turn off interrupts
  ASM_START
      cli
  ASM_END
      // set 40:3e bit 7 to 0
      val8 = read_byte(0x0040, 0x003e);
      val8 &= 0x7f;
      write_byte(0x0040, 0x003e, val8);
      // check port 3f4 for accessibility to status bytes
      val8 = inb(0x3f4);
      if ( (val8 & 0xc0) != 0xc0 )
        BX_PANIC("int13_diskette: ctrl not ready\n");

      // read 7 return status bytes from controller
      // using loop index broken, have to unroll...
      return_status[0] = inb(0x3f5);
      return_status[1] = inb(0x3f5);
      return_status[2] = inb(0x3f5);
      return_status[3] = inb(0x3f5);
      return_status[4] = inb(0x3f5);
      return_status[5] = inb(0x3f5);
      return_status[6] = inb(0x3f5);
      // record in BIOS Data Area
      write_byte(0x0040, 0x0042, return_status[0]);
      write_byte(0x0040, 0x0043, return_status[1]);
      write_byte(0x0040, 0x0044, return_status[2]);
      write_byte(0x0040, 0x0045, return_status[3]);
      write_byte(0x0040, 0x0046, return_status[4]);
      write_byte(0x0040, 0x0047, return_status[5]);
      write_byte(0x0040, 0x0048, return_status[6]);

      if ( (return_status[0] & 0xc0) != 0 ) {
        if ( (return_status[1] & 0x02) != 0 ) {
          // diskette not writable.
          // AH=status code=0x03 (tried to write on write-protected disk)
          // AL=number of sectors written=0
          AX = 0x0300;
          SET_CF();
          return;
        } else {
          BX_PANIC("int13_diskette_function: write error\n");
        }
      }

      SET_AH(0);
      set_diskette_ret_status(0);
      set_diskette_current_cyl(drive, 0);
      CLEAR_CF(); // successful
      return;


    case 0x08: // read diskette drive parameters
BX_DEBUG_INT13_FL("floppy f08\n");
      drive = GET_ELDL();

      if (drive > 1) {
        AX = 0;
        BX = 0;
        CX = 0;
        DX = 0;
        ES = 0;
        DI = 0;
        SET_DL(num_floppies);
        SET_CF();
        return;
        }

      drive_type = inb_cmos(0x10);
      num_floppies = 0;
      if (drive_type & 0xf0)
        num_floppies++;
      if (drive_type & 0x0f)
        num_floppies++;

      if (drive == 0)
        drive_type >>= 4;
      else
        drive_type &= 0x0f;

      SET_BH(0);
      SET_BL(drive_type);
      SET_AH(0);
      SET_AL(0);
      SET_DL(num_floppies);

      switch (drive_type) {
        case 0: // none
          CX = 0;
          SET_DH(0); // max head #
          break;

        case 1: // 360KB, 5.25"
          CX = 0x2709; // 40 tracks, 9 sectors
          SET_DH(1); // max head #
          break;

        case 2: // 1.2MB, 5.25"
          CX = 0x4f0f; // 80 tracks, 15 sectors
          SET_DH(1); // max head #
          break;

        case 3: // 720KB, 3.5"
          CX = 0x4f09; // 80 tracks, 9 sectors
          SET_DH(1); // max head #
          break;

        case 4: // 1.44MB, 3.5"
          CX = 0x4f12; // 80 tracks, 18 sectors
          SET_DH(1); // max head #
          break;

        case 5: // 2.88MB, 3.5"
          CX = 0x4f24; // 80 tracks, 36 sectors
          SET_DH(1); // max head #
          break;

        case 6: // 160k, 5.25"
          CX = 0x2708; // 40 tracks, 8 sectors
          SET_DH(0); // max head #
          break;

        case 7: // 180k, 5.25"
          CX = 0x2709; // 40 tracks, 9 sectors
          SET_DH(0); // max head #
          break;

        case 8: // 320k, 5.25"
          CX = 0x2708; // 40 tracks, 8 sectors
          SET_DH(1); // max head #
          break;

        default: // ?
          BX_PANIC("floppy: int13: bad floppy type\n");
        }

      /* set es & di to point to 11 byte diskette param table in ROM */
ASM_START
      push bp
      mov  bp, sp
      mov ax, #diskette_param_table2
      mov _int13_diskette_function.DI+2[bp], ax
      mov _int13_diskette_function.ES+2[bp], cs
      pop  bp
ASM_END
      CLEAR_CF(); // success
      /* disk status not changed upon success */
      return;


    case 0x15: // read diskette drive type
BX_DEBUG_INT13_FL("floppy f15\n");
      drive = GET_ELDL();
      if (drive > 1) {
        SET_AH(0); // only 2 drives supported
        // set_diskette_ret_status here ???
        SET_CF();
        return;
        }
      drive_type = inb_cmos(0x10);

      if (drive == 0)
        drive_type >>= 4;
      else
        drive_type &= 0x0f;
      CLEAR_CF(); // successful, not present
      if (drive_type==0) {
        SET_AH(0); // drive not present
        }
      else {
        SET_AH(1); // drive present, does not support change line
        }

      return;

    case 0x16: // get diskette change line status
BX_DEBUG_INT13_FL("floppy f16\n");
      drive = GET_ELDL();
      if (drive > 1) {
        SET_AH(0x01); // invalid drive
        set_diskette_ret_status(0x01);
        SET_CF();
        return;
        }

      SET_AH(0x06); // change line not supported
      set_diskette_ret_status(0x06);
      SET_CF();
      return;

    case 0x17: // set diskette type for format(old)
BX_DEBUG_INT13_FL("floppy f17\n");
      /* not used for 1.44M floppies */
      SET_AH(0x01); // not supported
      set_diskette_ret_status(1); /* not supported */
      SET_CF();
      return;

    case 0x18: // set diskette type for format(new)
BX_DEBUG_INT13_FL("floppy f18\n");
      SET_AH(0x01); // do later
      set_diskette_ret_status(1);
      SET_CF();
      return;

    default:
        BX_INFO("int13_diskette: unsupported AH=%02x\n", GET_AH());

      // if ( (ah==0x20) || ((ah>=0x41) && (ah<=0x49)) || (ah==0x4e) ) {
        SET_AH(0x01); // ???
        set_diskette_ret_status(1);
        SET_CF();
        return;
      //   }
    }
}
#else  // #if BX_SUPPORT_FLOPPY
  void
int13_diskette_function(DS, ES, DI, SI, BP, ELDX, BX, DX, CX, AX, IP, CS, FLAGS)
  Bit16u DS, ES, DI, SI, BP, ELDX, BX, DX, CX, AX, IP, CS, FLAGS;
{
  Bit8u  val8;

  switch ( GET_AH() ) {

    case 0x01: // Read Diskette Status
      CLEAR_CF();
      val8 = read_byte(0x0000, 0x0441);
      SET_AH(val8);
      if (val8) {
        SET_CF();
        }
      return;

    default:
      SET_CF();
      write_byte(0x0000, 0x0441, 0x01);
      SET_AH(0x01);
    }
}
#endif  // #if BX_SUPPORT_FLOPPY

 void
set_diskette_ret_status(value)
  Bit8u value;
{
  write_byte(0x0040, 0x0041, value);
}

  void
set_diskette_current_cyl(drive, cyl)
  Bit8u drive;
  Bit8u cyl;
{
  if (drive > 1)
    BX_PANIC("set_diskette_current_cyl(): drive > 1\n");
  write_byte(0x0040, 0x0094+drive, cyl);
}

  void
determine_floppy_media(drive)
  Bit16u drive;
{
#if 0
  Bit8u  val8, DOR, ctrl_info;

  ctrl_info = read_byte(0x0040, 0x008F);
  if (drive==1)
    ctrl_info >>= 4;
  else
    ctrl_info &= 0x0f;

#if 0
  if (drive == 0) {
    DOR = 0x1c; // DOR: drive0 motor on, DMA&int enabled, normal op, drive select 0
    }
  else {
    DOR = 0x2d; // DOR: drive1 motor on, DMA&int enabled, normal op, drive select 1
    }
#endif

  if ( (ctrl_info & 0x04) != 0x04 ) {
    // Drive not determined means no drive exists, done.
    return;
    }

#if 0
  // check Main Status Register for readiness
  val8 = inb(0x03f4) & 0x80; // Main Status Register
  if (val8 != 0x80)
    BX_PANIC("d_f_m: MRQ bit not set\n");

  // change line

  // existing BDA values

  // turn on drive motor
  outb(0x03f2, DOR); // Digital Output Register
  //
#endif
  BX_PANIC("d_f_m: OK so far\n");
#endif
}

  void
int17_function(regs, ds, iret_addr)
  pusha_regs_t regs; // regs pushed from PUSHA instruction
  Bit16u ds; // previous DS:, DS set to 0x0000 by asm wrapper
  iret_addr_t  iret_addr; // CS,IP,Flags pushed from original INT call
{
  Bit16u addr,timeout;
  Bit8u val8;

  ASM_START
  sti
  ASM_END

  addr = read_word(0x0040, (regs.u.r16.dx << 1) + 8);
  if ((regs.u.r8.ah < 3) && (regs.u.r16.dx < 3) && (addr > 0)) {
    timeout = read_byte(0x0040, 0x0078 + regs.u.r16.dx) << 8;
    if (regs.u.r8.ah == 0) {
      outb(addr, regs.u.r8.al);
      val8 = inb(addr+2);
      outb(addr+2, val8 | 0x01); // send strobe
      ASM_START
      nop
      ASM_END
      outb(addr+2, val8 & ~0x01);
      while (((inb(addr+1) & 0x40) == 0x40) && (timeout)) {
        timeout--;
      }
    }
    if (regs.u.r8.ah == 1) {
      val8 = inb(addr+2);
      outb(addr+2, val8 & ~0x04); // send init
      ASM_START
      nop
      ASM_END
      outb(addr+2, val8 | 0x04);
    }
    val8 = inb(addr+1);
    regs.u.r8.ah = (val8 ^ 0x48);
    if (!timeout) regs.u.r8.ah |= 0x01;
    ClearCF(iret_addr.flags);
  } else {
    SetCF(iret_addr.flags); // Unsupported
  }
}

// returns bootsegment in ax, drive in bl
  Bit32u 
int19_function(bseqnr)
Bit8u bseqnr;
{
  Bit16u ebda_seg=read_word(0x0040,0x000E);
  Bit16u bootseq;
  Bit8u  bootdrv;
  Bit8u  bootcd;
  Bit8u  bootchk;
  Bit16u bootseg;
  Bit16u status;
  Bit8u  lastdrive=0;

  // if BX_ELTORITO_BOOT is not defined, old behavior
  //   check bit 5 in CMOS reg 0x2d.  load either 0x00 or 0x80 into DL
  //   in preparation for the intial INT 13h (0=floppy A:, 0x80=C:)
  //     0: system boot sequence, first drive C: then A:
  //     1: system boot sequence, first drive A: then C:
  // else BX_ELTORITO_BOOT is defined
  //   CMOS regs 0x3D and 0x38 contain the boot sequence:
  //     CMOS reg 0x3D & 0x0f : 1st boot device
  //     CMOS reg 0x3D & 0xf0 : 2nd boot device
  //     CMOS reg 0x38 & 0xf0 : 3rd boot device
  //   boot device codes:
  //     0x00 : not defined
  //     0x01 : first floppy 
  //     0x02 : first harddrive
  //     0x03 : first cdrom
  //     else : boot failure

  // Get the boot sequence
#if BX_ELTORITO_BOOT
  bootseq=inb_cmos(0x3d);
  bootseq|=((inb_cmos(0x38) & 0xf0) << 4);

  if (bseqnr==2) bootseq >>= 4;
  if (bseqnr==3) bootseq >>= 8;
  if (bootseq<0x10) lastdrive = 1;
  bootdrv=0x00; bootcd=0;
  switch(bootseq & 0x0f) {
    case 0x01: bootdrv=0x00; bootcd=0; break;
    case 0x02: bootdrv=0x80; bootcd=0; break;
    case 0x03: bootdrv=0x00; bootcd=1; break;
    default:   return 0x00000000;
    }
#else
  bootseq=inb_cmos(0x2d);

  if (bseqnr==2) {
    bootseq ^= 0x20;
    lastdrive = 1;
  }
  bootdrv=0x00; bootcd=0;
  if((bootseq&0x20)==0) bootdrv=0x80;
#endif // BX_ELTORITO_BOOT

#if BX_ELTORITO_BOOT
  // We have to boot from cd
  if (bootcd != 0) {
    status = cdrom_boot();

    // If failure
    if ( (status & 0x00ff) !=0 ) {
      print_cdromboot_failure(status);
      print_boot_failure(bootcd, bootdrv, 1, lastdrive);
      return 0x00000000;
      }

    bootseg = read_word(ebda_seg,&EbdaData->cdemu.load_segment);
    bootdrv = (Bit8u)(status>>8);
    }

#endif // BX_ELTORITO_BOOT

  // We have to boot from harddisk or floppy
  if (bootcd == 0) {
    bootseg=0x07c0;

ASM_START
    push bp
    mov  bp, sp

    xor  ax, ax
    mov  _int19_function.status + 2[bp], ax
    mov  dl, _int19_function.bootdrv + 2[bp]
    mov  ax, _int19_function.bootseg + 2[bp]
    mov  es, ax         ;; segment
    xor  bx, bx         ;; offset
    mov  ah, #0x02      ;; function 2, read diskette sector
    mov  al, #0x01      ;; read 1 sector
    mov  ch, #0x00      ;; track 0
    mov  cl, #0x01      ;; sector 1
    mov  dh, #0x00      ;; head 0
    int  #0x13          ;; read sector
    jnc  int19_load_done
    mov  ax, #0x0001
    mov  _int19_function.status + 2[bp], ax

int19_load_done:
    pop  bp
ASM_END
    
    if (status != 0) {
      print_boot_failure(bootcd, bootdrv, 1, lastdrive);
      return 0x00000000;
      }
    }

  // check signature if instructed by cmos reg 0x38, only for floppy
  // bootchk = 1 : signature check disabled
  // bootchk = 0 : signature check enabled
  if (bootdrv != 0) bootchk = 0;
  else bootchk = inb_cmos(0x38) & 0x01;

#if BX_ELTORITO_BOOT
  // if boot from cd, no signature check
  if (bootcd != 0)
    bootchk = 1;
#endif // BX_ELTORITO_BOOT

  if (bootchk == 0) {
    if (read_word(bootseg,0x1fe) != 0xaa55) {
      print_boot_failure(bootcd, bootdrv, 0, lastdrive);
      return 0x00000000;
      }
    }
  
#if BX_ELTORITO_BOOT
  // Print out the boot string
  print_boot_device(bootcd, bootdrv);
#else // BX_ELTORITO_BOOT
  print_boot_device(0, bootdrv);
#endif // BX_ELTORITO_BOOT

  // return the boot segment
  return (((Bit32u)bootdrv) << 16) + bootseg;
}

  void
int1a_function(regs, ds, iret_addr)
  pusha_regs_t regs; // regs pushed from PUSHA instruction
  Bit16u ds; // previous DS:, DS set to 0x0000 by asm wrapper
  iret_addr_t  iret_addr; // CS,IP,Flags pushed from original INT call
{
  Bit8u val8;

  BX_DEBUG_INT1A("int1a: AX=%04x BX=%04x CX=%04x DX=%04x DS=%04x\n", regs.u.r16.ax, regs.u.r16.bx, regs.u.r16.cx, regs.u.r16.dx, ds);

  ASM_START
  sti
  ASM_END

  switch (regs.u.r8.ah) {
    case 0: // get current clock count
      ASM_START
      cli
      ASM_END
      regs.u.r16.cx = BiosData->ticks_high;
      regs.u.r16.dx = BiosData->ticks_low;
      regs.u.r8.al  = BiosData->midnight_flag;
      BiosData->midnight_flag = 0; // reset flag
      ASM_START
      sti
      ASM_END
      // AH already 0
      ClearCF(iret_addr.flags); // OK
      break;

    case 1: // Set Current Clock Count
      ASM_START
      cli
      ASM_END
      BiosData->ticks_high = regs.u.r16.cx;
      BiosData->ticks_low  = regs.u.r16.dx;
      BiosData->midnight_flag = 0; // reset flag
      ASM_START
      sti
      ASM_END
      regs.u.r8.ah = 0;
      ClearCF(iret_addr.flags); // OK
      break;


    case 2: // Read CMOS Time
      if (rtc_updating()) {
        SetCF(iret_addr.flags);
        break;
        }

      regs.u.r8.dh = inb_cmos(0x00); // Seconds
      regs.u.r8.cl = inb_cmos(0x02); // Minutes
      regs.u.r8.ch = inb_cmos(0x04); // Hours
      regs.u.r8.dl = inb_cmos(0x0b) & 0x01; // Stat Reg B
      regs.u.r8.ah = 0;
      regs.u.r8.al = regs.u.r8.ch;
      ClearCF(iret_addr.flags); // OK
      break;

    case 3: // Set CMOS Time
      // Using a debugger, I notice the following masking/setting
      // of bits in Status Register B, by setting Reg B to
      // a few values and getting its value after INT 1A was called.
      //
      //        try#1       try#2       try#3
      // before 1111 1101   0111 1101   0000 0000
      // after  0110 0010   0110 0010   0000 0010
      //
      // Bit4 in try#1 flipped in hardware (forced low) due to bit7=1
      // My assumption: RegB = ((RegB & 01100000b) | 00000010b)
      if (rtc_updating()) {
        init_rtc();
        // fall through as if an update were not in progress
        }
      outb_cmos(0x00, regs.u.r8.dh); // Seconds
      outb_cmos(0x02, regs.u.r8.cl); // Minutes
      outb_cmos(0x04, regs.u.r8.ch); // Hours
      // Set Daylight Savings time enabled bit to requested value
      val8 = (inb_cmos(0x0b) & 0x60) | 0x02 | (regs.u.r8.dl & 0x01);
      // (reg B already selected)
      outb_cmos(0x0b, val8);
      regs.u.r8.ah = 0;
      regs.u.r8.al = val8; // val last written to Reg B
      ClearCF(iret_addr.flags); // OK
      break;

    case 4: // Read CMOS Date
      regs.u.r8.ah = 0;
      if (rtc_updating()) {
        SetCF(iret_addr.flags);
        break;
        }
      regs.u.r8.cl = inb_cmos(0x09); // Year
      regs.u.r8.dh = inb_cmos(0x08); // Month
      regs.u.r8.dl = inb_cmos(0x07); // Day of Month
      regs.u.r8.ch = inb_cmos(0x32); // Century
      regs.u.r8.al = regs.u.r8.ch;
      ClearCF(iret_addr.flags); // OK
      break;

    case 5: // Set CMOS Date
      // Using a debugger, I notice the following masking/setting
      // of bits in Status Register B, by setting Reg B to
      // a few values and getting its value after INT 1A was called.
      //
      //        try#1       try#2       try#3       try#4
      // before 1111 1101   0111 1101   0000 0010   0000 0000
      // after  0110 1101   0111 1101   0000 0010   0000 0000
      //
      // Bit4 in try#1 flipped in hardware (forced low) due to bit7=1
      // My assumption: RegB = (RegB & 01111111b)
      if (rtc_updating()) {
        init_rtc();
        SetCF(iret_addr.flags);
        break;
        }
      outb_cmos(0x09, regs.u.r8.cl); // Year
      outb_cmos(0x08, regs.u.r8.dh); // Month
      outb_cmos(0x07, regs.u.r8.dl); // Day of Month
      outb_cmos(0x32, regs.u.r8.ch); // Century
      val8 = inb_cmos(0x0b) & 0x7f; // clear halt-clock bit
      outb_cmos(0x0b, val8);
      regs.u.r8.ah = 0;
      regs.u.r8.al = val8; // AL = val last written to Reg B
      ClearCF(iret_addr.flags); // OK
      break;

    case 6: // Set Alarm Time in CMOS
      // Using a debugger, I notice the following masking/setting
      // of bits in Status Register B, by setting Reg B to
      // a few values and getting its value after INT 1A was called.
      //
      //        try#1       try#2       try#3
      // before 1101 1111   0101 1111   0000 0000
      // after  0110 1111   0111 1111   0010 0000
      //
      // Bit4 in try#1 flipped in hardware (forced low) due to bit7=1
      // My assumption: RegB = ((RegB & 01111111b) | 00100000b)
      val8 = inb_cmos(0x0b); // Get Status Reg B
      regs.u.r16.ax = 0;
      if (val8 & 0x20) {
        // Alarm interrupt enabled already
        SetCF(iret_addr.flags); // Error: alarm in use
        break;
        }
      if (rtc_updating()) {
        init_rtc();
        // fall through as if an update were not in progress
        }
      outb_cmos(0x01, regs.u.r8.dh); // Seconds alarm
      outb_cmos(0x03, regs.u.r8.cl); // Minutes alarm
      outb_cmos(0x05, regs.u.r8.ch); // Hours alarm
      outb(0xa1, inb(0xa1) & 0xfe); // enable IRQ 8
      // enable Status Reg B alarm bit, clear halt clock bit
      outb_cmos(0x0b, (val8 & 0x7f) | 0x20);
      ClearCF(iret_addr.flags); // OK
      break;

    case 7: // Turn off Alarm
      // Using a debugger, I notice the following masking/setting
      // of bits in Status Register B, by setting Reg B to
      // a few values and getting its value after INT 1A was called.
      //
      //        try#1       try#2       try#3       try#4
      // before 1111 1101   0111 1101   0010 0000   0010 0010
      // after  0100 0101   0101 0101   0000 0000   0000 0010
      //
      // Bit4 in try#1 flipped in hardware (forced low) due to bit7=1
      // My assumption: RegB = (RegB & 01010111b)
      val8 = inb_cmos(0x0b); // Get Status Reg B
      // clear clock-halt bit, disable alarm bit
      outb_cmos(0x0b, val8 & 0x57); // disable alarm bit
      regs.u.r8.ah = 0;
      regs.u.r8.al = val8; // val last written to Reg B
      ClearCF(iret_addr.flags); // OK
      break;
#if BX_PCIBIOS
    case 0xb1:
      // real mode PCI BIOS functions now handled in assembler code
      // this C code handles the error code for information only
      if (regs.u.r8.bl == 0xff) {
        BX_INFO("PCI BIOS: PCI not present\n");
      } else if (regs.u.r8.bl == 0x81) {
        BX_INFO("unsupported PCI BIOS function 0x%02x\n", regs.u.r8.al);
      } else if (regs.u.r8.bl == 0x83) {
        BX_INFO("bad PCI vendor ID %04x\n", regs.u.r16.dx);
      } else if (regs.u.r8.bl == 0x86) {
        if (regs.u.r8.al == 0x02) {
          BX_INFO("PCI device %04x:%04x not found at index %d\n", regs.u.r16.dx, regs.u.r16.cx, regs.u.r16.si);
        } else {
          BX_INFO("no PCI device with class code 0x%02x%04x found at index %d\n", regs.u.r8.cl, regs.u.r16.dx, regs.u.r16.si);
        }
      }
      regs.u.r8.ah = regs.u.r8.bl;
      SetCF(iret_addr.flags);
      break;
#endif

    default:
      SetCF(iret_addr.flags); // Unsupported
    }
}

  void
int70_function(regs, ds, iret_addr)
  pusha_regs_t regs; // regs pushed from PUSHA instruction
  Bit16u ds; // previous DS:, DS set to 0x0000 by asm wrapper
  iret_addr_t  iret_addr; // CS,IP,Flags pushed from original INT call
{
  // INT 70h: IRQ 8 - CMOS RTC interrupt from periodic or alarm modes
  Bit8u registerB = 0, registerC = 0;

  // Check which modes are enabled and have occurred.
  registerB = inb_cmos( 0xB );
  registerC = inb_cmos( 0xC );

  if( ( registerB & 0x60 ) != 0 ) {
    if( ( registerC & 0x20 ) != 0 ) {
      // Handle Alarm Interrupt.
ASM_START
      sti
      int #0x4a
      cli
ASM_END
    }
    if( ( registerC & 0x40 ) != 0 ) {
      // Handle Periodic Interrupt.

      if( read_byte( 0x40, 0xA0 ) != 0 ) {
        // Wait Interval (Int 15, AH=83) active.
        Bit32u time, toggle;

        time = read_dword( 0x40, 0x9C );  // Time left in microseconds.
        if( time < 0x3D1 ) {
          // Done waiting.
          Bit16u segment, offset;

          segment = read_word( 0x40, 0x98 );
          offset = read_word( 0x40, 0x9A );
          write_byte( 0x40, 0xA0, 0 );  // Turn of status byte.
          outb_cmos( 0xB, registerB & 0x37 ); // Clear the Periodic Interrupt.
          write_byte(segment, offset, read_byte(segment, offset) | 0x80 );  // Write to specified flag byte.
        } else {
          // Continue waiting.
          time -= 0x3D1;
          write_dword( 0x40, 0x9C, time );
        }
      }
    }
  }

ASM_START
  call eoi_both_pics
ASM_END
}


ASM_START
;------------------------------------------
;- INT74h : PS/2 mouse hardware interrupt -
;------------------------------------------
int74_handler:
  sti
  pusha
  push ds         ;; save DS
  push #0x00 ;; placeholder for status
  push #0x00 ;; placeholder for X
  push #0x00 ;; placeholder for Y
  push #0x00 ;; placeholder for Z
  push #0x00 ;; placeholder for make_far_call boolean
  call _int74_function
  pop  cx      ;; remove make_far_call from stack
  jcxz int74_done

  ;; make far call to EBDA:0022
  push #0x00
  pop ds
  push 0x040E     ;; push 0000:040E (opcodes 0xff, 0x36, 0x0E, 0x04)
  pop ds
  //CALL_EP(0x0022) ;; call far routine (call_Ep DS:0022 :opcodes 0xff, 0x1e, 0x22, 0x00)
  call far ptr[0x22]
int74_done:
  cli
  call eoi_both_pics
  add sp, #8     ;; pop status, x, y, z

  pop ds          ;; restore DS
  popa
  iret


;; This will perform an IRET, but will retain value of current CF
;; by altering flags on stack.  Better than RETF #02.
iret_modify_cf:
  jc   carry_set
  push bp
  mov  bp, sp
  and  BYTE [bp + 0x06], #0xfe
  pop  bp
  iret
carry_set:
  push bp
  mov  bp, sp
  or   BYTE [bp + 0x06], #0x01
  pop  bp
  iret


;----------------------
;- INT13h (relocated) -
;----------------------
;
; int13_relocated is a little bit messed up since I played with it
; I have to rewrite it:
;   - call a function that detect which function to call
;   - make all called C function get the same parameters list
;
int13_relocated:

#if BX_ELTORITO_BOOT
  ;; check for an eltorito function
  cmp   ah,#0x4a
  jb    int13_not_eltorito
  cmp   ah,#0x4d
  ja    int13_not_eltorito

  pusha
  push  es
  push  ds
  push  ss
  pop   ds

  push  #int13_out
  jmp   _int13_eltorito      ;; ELDX not used

int13_not_eltorito:
  push  ax
  push  bx
  push  cx
  push  dx

  ;; check if emulation active
  call  _cdemu_isactive
  cmp   al,#0x00
  je    int13_cdemu_inactive

  ;; check if access to the emulated drive
  call  _cdemu_emulated_drive
  pop   dx
  push  dx
  cmp   al,dl                ;; int13 on emulated drive
  jne   int13_nocdemu

  pop   dx
  pop   cx
  pop   bx
  pop   ax

  pusha
  push  es
  push  ds
  push  ss
  pop   ds

  push  #int13_out
  jmp   _int13_cdemu         ;; ELDX not used

int13_nocdemu:
  and   dl,#0xE0             ;; mask to get device class, including cdroms
  cmp   al,dl                ;; al is 0x00 or 0x80
  jne   int13_cdemu_inactive ;; inactive for device class

  pop   dx
  pop   cx
  pop   bx
  pop   ax

  push  ax
  push  cx
  push  dx
  push  bx

  dec   dl                   ;; real drive is dl - 1
  jmp   int13_legacy

int13_cdemu_inactive:
  pop   dx
  pop   cx
  pop   bx
  pop   ax

#endif // BX_ELTORITO_BOOT

int13_noeltorito:

  push  ax
  push  cx
  push  dx
  push  bx

int13_legacy:

  push  dx                   ;; push eltorito value of dx instead of sp

  push  bp
  push  si
  push  di

  push  es
  push  ds
  push  ss
  pop   ds

  ;; now the 16-bit registers can be restored with:
  ;; pop ds; pop es; popa; iret
  ;; arguments passed to functions should be
  ;; DS, ES, DI, SI, BP, ELDX, BX, DX, CX, AX, IP, CS, FLAGS

  test  dl, #0x80
  jnz   int13_notfloppy

  push #int13_out
  jmp _int13_diskette_function

int13_notfloppy:

#if BX_USE_ATADRV

  cmp   dl, #0xE0
  jb    int13_notcdrom

  // ebx is modified: BSD 5.2.1 boot loader problem
  // someone should figure out which 32 bit register that actually are used

  shr   ebx, #16
  push  bx

  call  _int13_cdrom

  pop   bx
  shl   ebx, #16

  jmp int13_out

int13_notcdrom:

#endif

int13_disk:
  call  _int13_harddisk

int13_out:
  pop ds
  pop es
  popa
  iret 


;----------
;- INT18h -
;----------
int18_handler: ;; Boot Failure routing
  call _int18_panic_msg
  hlt
  iret

;----------
;- INT19h -
;----------
int19_relocated: ;; Boot function, relocated

  ;; int19 was beginning to be really complex, so now it
  ;; just calls an C function, that does the work
  ;; it returns in BL the boot drive, and in AX the boot segment
  ;; the boot segment will be 0x0000 if something has failed

  push bp
  mov  bp, sp

  ;; drop ds
  xor  ax, ax
  mov  ds, ax

  ;; 1st boot device
  mov  ax, #0x0001
  push ax
  call _int19_function
  inc  sp
  inc  sp
  ;; bl contains the boot drive
  ;; ax contains the boot segment or 0 if failure

  test       ax, ax  ;; if ax is 0 try next boot device
  jnz        boot_setup

  ;; 2nd boot device
  mov  ax, #0x0002
  push ax
  call _int19_function
  inc  sp
  inc  sp
  test       ax, ax  ;; if ax is 0 try next boot device
  jnz        boot_setup

  ;; 3rd boot device
  mov  ax, #0x0003
  push ax
  call _int19_function
  inc  sp
  inc  sp
  test       ax, ax  ;; if ax is 0 call int18
  jz         int18_handler

boot_setup:
  mov dl,    bl      ;; set drive so guest os find it
  shl eax,   #0x04   ;; convert seg to ip
  mov 2[bp], ax      ;; set ip

  shr eax,   #0x04   ;; get cs back
  and ax,    #0xF000 ;; remove what went in ip
  mov 4[bp], ax      ;; set cs
  xor ax,    ax
  mov es,    ax      ;; set es to zero fixes [ 549815 ]
  mov [bp],  ax      ;; set bp to zero
  mov ax,    #0xaa55 ;; set ok flag

  pop bp
  iret               ;; Beam me up Scotty

;----------
;- INT1Ch -
;----------
int1c_handler: ;; User Timer Tick
  iret


;----------------------
;- POST: Floppy Drive -
;----------------------
floppy_drive_post:
  xor  ax, ax
  mov  ds, ax

  mov  al, #0x00
  mov  0x043e, al ;; drive 0 & 1 uncalibrated, no interrupt has occurred

  mov  0x043f, al  ;; diskette motor status: read op, drive0, motors off

  mov  0x0440, al  ;; diskette motor timeout counter: not active
  mov  0x0441, al  ;; diskette controller status return code

  mov  0x0442, al  ;; disk & diskette controller status register 0
  mov  0x0443, al  ;; diskette controller status register 1
  mov  0x0444, al  ;; diskette controller status register 2
  mov  0x0445, al  ;; diskette controller cylinder number
  mov  0x0446, al  ;; diskette controller head number
  mov  0x0447, al  ;; diskette controller sector number
  mov  0x0448, al  ;; diskette controller bytes written

  mov  0x048b, al  ;; diskette configuration data

  ;; -----------------------------------------------------------------
  ;; (048F) diskette controller information
  ;;
  mov  al, #0x10   ;; get CMOS diskette drive type
  out  0x70, AL
  in   AL, 0x71
  mov  ah, al      ;; save byte to AH

look_drive0:
  shr  al, #4      ;; look at top 4 bits for drive 0
  jz   f0_missing  ;; jump if no drive0
  mov  bl, #0x07   ;; drive0 determined, multi-rate, has changed line
  jmp  look_drive1
f0_missing:
  mov  bl, #0x00   ;; no drive0

look_drive1:
  mov  al, ah      ;; restore from AH
  and  al, #0x0f   ;; look at bottom 4 bits for drive 1
  jz   f1_missing  ;; jump if no drive1
  or   bl, #0x70   ;; drive1 determined, multi-rate, has changed line
f1_missing:
                   ;; leave high bits in BL zerod
  mov  0x048f, bl  ;; put new val in BDA (diskette controller information)
  ;; -----------------------------------------------------------------

  mov  al, #0x00
  mov  0x0490, al  ;; diskette 0 media state
  mov  0x0491, al  ;; diskette 1 media state

                   ;; diskette 0,1 operational starting state
                   ;; drive type has not been determined,
                   ;; has no changed detection line
  mov  0x0492, al
  mov  0x0493, al

  mov  0x0494, al  ;; diskette 0 current cylinder
  mov  0x0495, al  ;; diskette 1 current cylinder

  mov  al, #0x02
  out  #0x0a, al   ;; clear DMA-1 channel 2 mask bit

  SET_INT_VECTOR(0x1E, #0xF000, #diskette_param_table2)
  SET_INT_VECTOR(0x40, #0xF000, #int13_diskette)
  SET_INT_VECTOR(0x0E, #0xF000, #int0e_handler) ;; IRQ 6

  ret


;--------------------
;- POST: HARD DRIVE -
;--------------------
; relocated here because the primary POST area isnt big enough.
hard_drive_post:
  // IRQ 14 = INT 76h
  // INT 76h calls INT 15h function ax=9100

  mov  al, #0x0a   ; 0000 1010 = reserved, disable IRQ 14
  mov  dx, #0x03f6
  out  dx, al

  xor  ax, ax
  mov  ds, ax
  mov  0x0474, al /* hard disk status of last operation */
  mov  0x0477, al /* hard disk port offset (XT only ???) */
  mov  0x048c, al /* hard disk status register */
  mov  0x048d, al /* hard disk error register */
  mov  0x048e, al /* hard disk task complete flag */
  mov  al, #0x01
  mov  0x0475, al /* hard disk number attached */
  mov  al, #0xc0
  mov  0x0476, al /* hard disk control byte */
  SET_INT_VECTOR(0x13, #0xF000, #int13_handler)
  SET_INT_VECTOR(0x76, #0xF000, #int76_handler)
  ;; INT 41h: hard disk 0 configuration pointer
  ;; INT 46h: hard disk 1 configuration pointer
  SET_INT_VECTOR(0x41, #EBDA_SEG, #0x003D)
  SET_INT_VECTOR(0x46, #EBDA_SEG, #0x004D)

  ;; move disk geometry data from CMOS to EBDA disk parameter table(s)
  mov  al, #0x12
  out  #0x70, al
  in   al, #0x71
  and  al, #0xf0
  cmp  al, #0xf0
  je   post_d0_extended
  jmp check_for_hd1
post_d0_extended:
  mov  al, #0x19
  out  #0x70, al
  in   al, #0x71
  cmp  al, #47  ;; decimal 47 - user definable
  je   post_d0_type47
  HALT(__LINE__)
post_d0_type47:
  ;; CMOS  purpose                  param table offset
  ;; 1b    cylinders low            0
  ;; 1c    cylinders high           1
  ;; 1d    heads                    2
  ;; 1e    write pre-comp low       5
  ;; 1f    write pre-comp high      6
  ;; 20    retries/bad map/heads>8  8
  ;; 21    landing zone low         C
  ;; 22    landing zone high        D
  ;; 23    sectors/track            E

  mov  ax, #EBDA_SEG
  mov  ds, ax

  ;;; Filling EBDA table for hard disk 0.
  mov  al, #0x1f
  out  #0x70, al
  in   al, #0x71
  mov  ah, al
  mov  al, #0x1e
  out  #0x70, al
  in   al, #0x71
  mov   (0x003d + 0x05), ax ;; write precomp word

  mov  al, #0x20
  out  #0x70, al
  in   al, #0x71
  mov   (0x003d + 0x08), al ;; drive control byte

  mov  al, #0x22
  out  #0x70, al
  in   al, #0x71
  mov  ah, al
  mov  al, #0x21
  out  #0x70, al
  in   al, #0x71
  mov   (0x003d + 0x0C), ax ;; landing zone word

  mov  al, #0x1c   ;; get cylinders word in AX
  out  #0x70, al
  in   al, #0x71   ;; high byte
  mov  ah, al
  mov  al, #0x1b
  out  #0x70, al
  in   al, #0x71   ;; low byte
  mov  bx, ax      ;; BX = cylinders

  mov  al, #0x1d
  out  #0x70, al
  in   al, #0x71
  mov  cl, al      ;; CL = heads

  mov  al, #0x23
  out  #0x70, al
  in   al, #0x71
  mov  dl, al      ;; DL = sectors

  cmp  bx, #1024
  jnbe hd0_post_logical_chs ;; if cylinders > 1024, use translated style CHS

hd0_post_physical_chs:
  ;; no logical CHS mapping used, just physical CHS
  ;; use Standard Fixed Disk Parameter Table (FDPT)
  mov   (0x003d + 0x00), bx ;; number of physical cylinders
  mov   (0x003d + 0x02), cl ;; number of physical heads
  mov   (0x003d + 0x0E), dl ;; number of physical sectors
  jmp check_for_hd1

hd0_post_logical_chs:
  ;; complies with Phoenix style Translated Fixed Disk Parameter Table (FDPT)
  mov   (0x003d + 0x09), bx ;; number of physical cylinders
  mov   (0x003d + 0x0b), cl ;; number of physical heads
  mov   (0x003d + 0x04), dl ;; number of physical sectors
  mov   (0x003d + 0x0e), dl ;; number of logical sectors (same)
  mov al, #0xa0
  mov   (0x003d + 0x03), al ;; A0h signature, indicates translated table

  cmp bx, #2048
  jnbe hd0_post_above_2048
  ;; 1024 < c <= 2048 cylinders
  shr bx, #0x01
  shl cl, #0x01
  jmp hd0_post_store_logical

hd0_post_above_2048:
  cmp bx, #4096
  jnbe hd0_post_above_4096
  ;; 2048 < c <= 4096 cylinders
  shr bx, #0x02
  shl cl, #0x02
  jmp hd0_post_store_logical

hd0_post_above_4096:
  cmp bx, #8192
  jnbe hd0_post_above_8192
  ;; 4096 < c <= 8192 cylinders
  shr bx, #0x03
  shl cl, #0x03
  jmp hd0_post_store_logical

hd0_post_above_8192:
  ;; 8192 < c <= 16384 cylinders
  shr bx, #0x04
  shl cl, #0x04

hd0_post_store_logical:
  mov   (0x003d + 0x00), bx ;; number of physical cylinders
  mov   (0x003d + 0x02), cl ;; number of physical heads
  ;; checksum
  mov   cl, #0x0f     ;; repeat count
  mov   si, #0x003d   ;; offset to disk0 FDPT
  mov   al, #0x00     ;; sum
hd0_post_checksum_loop:
  add   al, [si]
  inc   si
  dec   cl
  jnz hd0_post_checksum_loop
  not   al  ;; now take 2s complement
  inc   al
  mov   [si], al
;;; Done filling EBDA table for hard disk 0.


check_for_hd1:
  ;; is there really a second hard disk?  if not, return now
  mov  al, #0x12
  out  #0x70, al
  in   al, #0x71
  and  al, #0x0f
  jnz   post_d1_exists
  ret
post_d1_exists:
  ;; check that the hd type is really 0x0f.
  cmp al, #0x0f
  jz post_d1_extended
  HALT(__LINE__)
post_d1_extended:
  ;; check that the extended type is 47 - user definable
  mov  al, #0x1a
  out  #0x70, al
  in   al, #0x71
  cmp  al, #47  ;; decimal 47 - user definable
  je   post_d1_type47
  HALT(__LINE__)
post_d1_type47:
  ;; Table for disk1.
  ;; CMOS  purpose                  param table offset
  ;; 0x24    cylinders low            0
  ;; 0x25    cylinders high           1
  ;; 0x26    heads                    2
  ;; 0x27    write pre-comp low       5
  ;; 0x28    write pre-comp high      6
  ;; 0x29    heads>8                  8
  ;; 0x2a    landing zone low         C
  ;; 0x2b    landing zone high        D
  ;; 0x2c    sectors/track            E
;;; Fill EBDA table for hard disk 1.
  mov  ax, #EBDA_SEG
  mov  ds, ax
  mov  al, #0x28
  out  #0x70, al
  in   al, #0x71
  mov  ah, al
  mov  al, #0x27
  out  #0x70, al
  in   al, #0x71
  mov   (0x004d + 0x05), ax ;; write precomp word

  mov  al, #0x29
  out  #0x70, al
  in   al, #0x71
  mov   (0x004d + 0x08), al ;; drive control byte

  mov  al, #0x2b
  out  #0x70, al
  in   al, #0x71
  mov  ah, al
  mov  al, #0x2a
  out  #0x70, al
  in   al, #0x71
  mov   (0x004d + 0x0C), ax ;; landing zone word

  mov  al, #0x25   ;; get cylinders word in AX
  out  #0x70, al
  in   al, #0x71   ;; high byte
  mov  ah, al
  mov  al, #0x24
  out  #0x70, al
  in   al, #0x71   ;; low byte
  mov  bx, ax      ;; BX = cylinders

  mov  al, #0x26
  out  #0x70, al
  in   al, #0x71
  mov  cl, al      ;; CL = heads

  mov  al, #0x2c
  out  #0x70, al
  in   al, #0x71
  mov  dl, al      ;; DL = sectors

  cmp  bx, #1024
  jnbe hd1_post_logical_chs ;; if cylinders > 1024, use translated style CHS

hd1_post_physical_chs:
  ;; no logical CHS mapping used, just physical CHS
  ;; use Standard Fixed Disk Parameter Table (FDPT)
  mov   (0x004d + 0x00), bx ;; number of physical cylinders
  mov   (0x004d + 0x02), cl ;; number of physical heads
  mov   (0x004d + 0x0E), dl ;; number of physical sectors
  ret

hd1_post_logical_chs:
  ;; complies with Phoenix style Translated Fixed Disk Parameter Table (FDPT)
  mov   (0x004d + 0x09), bx ;; number of physical cylinders
  mov   (0x004d + 0x0b), cl ;; number of physical heads
  mov   (0x004d + 0x04), dl ;; number of physical sectors
  mov   (0x004d + 0x0e), dl ;; number of logical sectors (same)
  mov al, #0xa0
  mov   (0x004d + 0x03), al ;; A0h signature, indicates translated table

  cmp bx, #2048
  jnbe hd1_post_above_2048
  ;; 1024 < c <= 2048 cylinders
  shr bx, #0x01
  shl cl, #0x01
  jmp hd1_post_store_logical

hd1_post_above_2048:
  cmp bx, #4096
  jnbe hd1_post_above_4096
  ;; 2048 < c <= 4096 cylinders
  shr bx, #0x02
  shl cl, #0x02
  jmp hd1_post_store_logical

hd1_post_above_4096:
  cmp bx, #8192
  jnbe hd1_post_above_8192
  ;; 4096 < c <= 8192 cylinders
  shr bx, #0x03
  shl cl, #0x03
  jmp hd1_post_store_logical

hd1_post_above_8192:
  ;; 8192 < c <= 16384 cylinders
  shr bx, #0x04
  shl cl, #0x04

hd1_post_store_logical:
  mov   (0x004d + 0x00), bx ;; number of physical cylinders
  mov   (0x004d + 0x02), cl ;; number of physical heads
  ;; checksum
  mov   cl, #0x0f     ;; repeat count
  mov   si, #0x004d   ;; offset to disk0 FDPT
  mov   al, #0x00     ;; sum
hd1_post_checksum_loop:
  add   al, [si]
  inc   si
  dec   cl
  jnz hd1_post_checksum_loop
  not   al  ;; now take 2s complement
  inc   al
  mov   [si], al
;;; Done filling EBDA table for hard disk 1.

  ret

;--------------------
;- POST: EBDA segment
;--------------------
; relocated here because the primary POST area isnt big enough.
ebda_post:
#if BX_USE_EBDA
  mov ax, #EBDA_SEG
  mov ds, ax
  mov byte ptr [0x0], #EBDA_SIZE
#endif
  xor ax, ax            ; mov EBDA seg into 40E
  mov ds, ax
  mov word ptr [0x40E], #EBDA_SEG
  ret;;

;--------------------
;- POST: EOI + jmp via [0x40:67)
;--------------------
; relocated here because the primary POST area isnt big enough.
eoi_jmp_post:
  call eoi_both_pics

  xor ax, ax
  mov ds, ax

  jmp far ptr [0x467]


;--------------------
eoi_both_pics:
  mov   al, #0x20
  out   #0xA0, al ;; slave  PIC EOI
eoi_master_pic:
  mov   al, #0x20
  out   #0x20, al ;; master PIC EOI
  ret

;--------------------
BcdToBin:
  ;; in:  AL in BCD format
  ;; out: AL in binary format, AH will always be 0
  ;; trashes BX
  mov  bl, al
  and  bl, #0x0f ;; bl has low digit
  shr  al, #4    ;; al has high digit
  mov  bh, #10
  mul  al, bh    ;; multiply high digit by 10 (result in AX)
  add  al, bl    ;;   then add low digit
  ret

;--------------------
timer_tick_post:
  ;; Setup the Timer Ticks Count (0x46C:dword) and
  ;;   Timer Ticks Roller Flag (0x470:byte)
  ;; The Timer Ticks Count needs to be set according to
  ;; the current CMOS time, as if ticks have been occurring
  ;; at 18.2hz since midnight up to this point.  Calculating
  ;; this is a little complicated.  Here are the factors I gather
  ;; regarding this.  14,318,180 hz was the original clock speed,
  ;; chosen so it could be divided by either 3 to drive the 5Mhz CPU
  ;; at the time, or 4 to drive the CGA video adapter.  The div3
  ;; source was divided again by 4 to feed a 1.193Mhz signal to
  ;; the timer.  With a maximum 16bit timer count, this is again
  ;; divided down by 65536 to 18.2hz.
  ;;
  ;; 14,318,180 Hz clock
  ;;   /3 = 4,772,726 Hz fed to orginal 5Mhz CPU
  ;;   /4 = 1,193,181 Hz fed to timer
  ;;   /65536 (maximum timer count) = 18.20650736 ticks/second
  ;; 1 second = 18.20650736 ticks
  ;; 1 minute = 1092.390442 ticks
  ;; 1 hour   = 65543.42651 ticks
  ;;
  ;; Given the values in the CMOS clock, one could calculate
  ;; the number of ticks by the following:
  ;;   ticks = (BcdToBin(seconds) * 18.206507) +
  ;;           (BcdToBin(minutes) * 1092.3904)
  ;;           (BcdToBin(hours)   * 65543.427)
  ;; To get a little more accuracy, since Im using integer
  ;; arithmatic, I use:
  ;;   ticks = (BcdToBin(seconds) * 18206507) / 1000000 +
  ;;           (BcdToBin(minutes) * 10923904) / 10000 +
  ;;           (BcdToBin(hours)   * 65543427) / 1000

  ;; assuming DS=0000

  ;; get CMOS seconds
  xor  eax, eax ;; clear EAX
  mov  al, #0x00
  out  #0x70, al
  in   al, #0x71 ;; AL has CMOS seconds in BCD
  call BcdToBin  ;; EAX now has seconds in binary
  mov  edx, #18206507
  mul  eax, edx
  mov  ebx, #1000000
  xor  edx, edx
  div  eax, ebx
  mov  ecx, eax  ;; ECX will accumulate total ticks

  ;; get CMOS minutes
  xor  eax, eax ;; clear EAX
  mov  al, #0x02
  out  #0x70, al
  in   al, #0x71 ;; AL has CMOS minutes in BCD
  call BcdToBin  ;; EAX now has minutes in binary
  mov  edx, #10923904
  mul  eax, edx
  mov  ebx, #10000
  xor  edx, edx
  div  eax, ebx
  add  ecx, eax  ;; add to total ticks

  ;; get CMOS hours
  xor  eax, eax ;; clear EAX
  mov  al, #0x04
  out  #0x70, al
  in   al, #0x71 ;; AL has CMOS hours in BCD
  call BcdToBin  ;; EAX now has hours in binary
  mov  edx, #65543427
  mul  eax, edx
  mov  ebx, #1000
  xor  edx, edx
  div  eax, ebx
  add  ecx, eax  ;; add to total ticks

  mov  0x46C, ecx ;; Timer Ticks Count
  xor  al, al
  mov  0x470, al  ;; Timer Ticks Rollover Flag
  ret

;--------------------
int76_handler:
  ;; record completion in BIOS task complete flag
  push  ax
  push  ds
  mov   ax, #0x0040
  mov   ds, ax
  mov   0x008E, #0xff
  call  eoi_both_pics
  pop   ds
  pop   ax
  iret


;--------------------
#if BX_APM

use32 386
#define APM_PROT32
#include "apmbios.S"

use16 386
#define APM_PROT16
#include "apmbios.S"

#define APM_REAL
#include "apmbios.S"

#endif

;--------------------
#if BX_PCIBIOS
use32 386
.align 16
bios32_structure:
  db 0x5f, 0x33, 0x32, 0x5f  ;; "_32_" signature
  dw bios32_entry_point, 0xf ;; 32 bit physical address
  db 0             ;; revision level
  ;; length in paragraphs and checksum stored in a word to prevent errors
  dw (~(((bios32_entry_point >> 8) + (bios32_entry_point & 0xff) + 0x32) \
        & 0xff) << 8) + 0x01
  db 0,0,0,0,0     ;; reserved

.align 16
bios32_entry_point:
  pushfd
  cmp eax, #0x49435024 ;; "$PCI"
  jne unknown_service
  mov eax, #0x80000000
  mov dx, #0x0cf8
  out dx, eax
  mov dx, #0x0cfc
  in  eax, dx
#ifdef PCI_FIXED_HOST_BRIDGE
  cmp eax, #PCI_FIXED_HOST_BRIDGE
  jne unknown_service
#else
  ;; say ok if a device is present
  cmp eax, #0xffffffff
  je unknown_service
#endif
  mov ebx, #0x000f0000
  mov ecx, #0
  mov edx, #pcibios_protected
  xor al, al
  jmp bios32_end
unknown_service:
  mov al, #0x80
bios32_end:
#ifdef BX_QEMU
  and dword ptr[esp+8],0xfffffffc ;; reset CS.RPL for kqemu
#endif
  popfd
  retf

.align 16
pcibios_protected:
  pushfd
  cli
  push esi
  push edi
  cmp al, #0x01 ;; installation check
  jne pci_pro_f02
  mov bx, #0x0210
  mov cx, #0
  mov edx, #0x20494350 ;; "PCI "
  mov al, #0x01
  jmp pci_pro_ok
pci_pro_f02: ;; find pci device
  cmp al, #0x02
  jne pci_pro_f08
  shl ecx, #16
  mov cx, dx
  xor bx, bx
  mov di, #0x00
pci_pro_devloop:
  call pci_pro_select_reg
  mov dx, #0x0cfc
  in  eax, dx
  cmp eax, ecx
  jne pci_pro_nextdev
  cmp si, #0
  je  pci_pro_ok
  dec si
pci_pro_nextdev:
  inc bx
  cmp bx, #0x0100
  jne pci_pro_devloop
  mov ah, #0x86
  jmp pci_pro_fail
pci_pro_f08: ;; read configuration byte
  cmp al, #0x08
  jne pci_pro_f09
  call pci_pro_select_reg
  push edx
  mov dx, di
  and dx, #0x03
  add dx, #0x0cfc
  in  al, dx
  pop edx
  mov cl, al
  jmp pci_pro_ok
pci_pro_f09: ;; read configuration word
  cmp al, #0x09
  jne pci_pro_f0a
  call pci_pro_select_reg
  push edx
  mov dx, di
  and dx, #0x02
  add dx, #0x0cfc
  in  ax, dx
  pop edx
  mov cx, ax
  jmp pci_pro_ok
pci_pro_f0a: ;; read configuration dword
  cmp al, #0x0a
  jne pci_pro_f0b
  call pci_pro_select_reg
  push edx
  mov dx, #0x0cfc
  in  eax, dx
  pop edx
  mov ecx, eax
  jmp pci_pro_ok
pci_pro_f0b: ;; write configuration byte
  cmp al, #0x0b
  jne pci_pro_f0c
  call pci_pro_select_reg
  push edx
  mov dx, di
  and dx, #0x03
  add dx, #0x0cfc
  mov al, cl
  out dx, al
  pop edx
  jmp pci_pro_ok
pci_pro_f0c: ;; write configuration word
  cmp al, #0x0c
  jne pci_pro_f0d
  call pci_pro_select_reg
  push edx
  mov dx, di
  and dx, #0x02
  add dx, #0x0cfc
  mov ax, cx
  out dx, ax
  pop edx
  jmp pci_pro_ok
pci_pro_f0d: ;; write configuration dword
  cmp al, #0x0d
  jne pci_pro_unknown
  call pci_pro_select_reg
  push edx
  mov dx, #0x0cfc
  mov eax, ecx
  out dx, eax
  pop edx
  jmp pci_pro_ok
pci_pro_unknown:
  mov ah, #0x81
pci_pro_fail:
  pop edi
  pop esi
#ifdef BX_QEMU
  and dword ptr[esp+8],0xfffffffc ;; reset CS.RPL for kqemu
#endif
  popfd
  stc
  retf
pci_pro_ok:
  xor ah, ah
  pop edi
  pop esi
#ifdef BX_QEMU
  and dword ptr[esp+8],0xfffffffc ;; reset CS.RPL for kqemu
#endif
  popfd
  clc
  retf

pci_pro_select_reg:
  push edx
  mov eax, #0x800000
  mov ax,  bx
  shl eax, #8
  and di,  #0xff
  or  ax,  di
  and al,  #0xfc
  mov dx, #0x0cf8
  out dx,  eax
  pop edx
  ret

use16 386

pcibios_real:
  push eax
  push dx
  mov eax, #0x80000000
  mov dx, #0x0cf8
  out dx, eax
  mov dx, #0x0cfc
  in  eax, dx
#ifdef PCI_FIXED_HOST_BRIDGE
  cmp eax, #PCI_FIXED_HOST_BRIDGE
  je  pci_present
#else
  ;; say ok if a device is present
  cmp eax, #0xffffffff
  jne  pci_present
#endif
  pop dx
  pop eax
  mov ah, #0xff
  stc
  ret
pci_present:
  pop dx
  pop eax
  cmp al, #0x01 ;; installation check
  jne pci_real_f02
  mov ax, #0x0001
  mov bx, #0x0210
  mov cx, #0
  mov edx, #0x20494350 ;; "PCI "
  mov edi, #0xf0000
  mov di, #pcibios_protected
  clc
  ret
pci_real_f02: ;; find pci device
  push esi
  push edi
  cmp al, #0x02
  jne pci_real_f03
  shl ecx, #16
  mov cx, dx
  xor bx, bx
  mov di, #0x00
pci_real_devloop:
  call pci_real_select_reg
  mov dx, #0x0cfc
  in  eax, dx
  cmp eax, ecx
  jne pci_real_nextdev
  cmp si, #0
  je  pci_real_ok
  dec si
pci_real_nextdev:
  inc bx
  cmp bx, #0x0100
  jne pci_real_devloop
  mov dx, cx
  shr ecx, #16
  mov ax, #0x8602
  jmp pci_real_fail
pci_real_f03: ;; find class code
  cmp al, #0x03
  jne pci_real_f08
  ; TODO
  mov dx, cx
  shr ecx, #16
  mov ax, #0x8603
  jmp pci_real_fail
pci_real_f08: ;; read configuration byte
  cmp al, #0x08
  jne pci_real_f09
  call pci_real_select_reg
  push dx
  mov dx, di
  and dx, #0x03
  add dx, #0x0cfc
  in  al, dx
  pop dx
  mov cl, al
  jmp pci_real_ok
pci_real_f09: ;; read configuration word
  cmp al, #0x09
  jne pci_real_f0a
  call pci_real_select_reg
  push dx
  mov dx, di
  and dx, #0x02
  add dx, #0x0cfc
  in  ax, dx
  pop dx
  mov cx, ax
  jmp pci_real_ok
pci_real_f0a: ;; read configuration dword
  cmp al, #0x0a
  jne pci_real_f0b
  call pci_real_select_reg
  push dx
  mov dx, #0x0cfc
  in  eax, dx
  pop dx
  mov ecx, eax
  jmp pci_real_ok
pci_real_f0b: ;; write configuration byte
  cmp al, #0x0b
  jne pci_real_f0c
  call pci_real_select_reg
  push dx
  mov dx, di
  and dx, #0x03
  add dx, #0x0cfc
  mov al, cl
  out dx, al
  pop dx
  jmp pci_real_ok
pci_real_f0c: ;; write configuration word
  cmp al, #0x0c
  jne pci_real_f0d
  call pci_real_select_reg
  push dx
  mov dx, di
  and dx, #0x02
  add dx, #0x0cfc
  mov ax, cx
  out dx, ax
  pop dx
  jmp pci_real_ok
pci_real_f0d: ;; write configuration dword
  cmp al, #0x0d
  jne pci_real_f0e
  call pci_real_select_reg
  push dx
  mov dx, #0x0cfc
  mov eax, ecx
  out dx, eax
  pop dx
  jmp pci_real_ok
pci_real_f0e: ;; get irq routing options
  cmp al, #0x0e
  jne pci_real_unknown
  SEG ES
  cmp word ptr [di], #pci_routing_table_structure_end - pci_routing_table_structure_start
  jb pci_real_too_small    
  SEG ES
  mov word ptr [di], #pci_routing_table_structure_end - pci_routing_table_structure_start        
  pushf
  push ds
  push es
  push cx
  push si
  push di
  cld
  mov si, #pci_routing_table_structure_start
  push cs
  pop ds
  SEG ES
  mov cx, [di+2]
  SEG ES
  mov es, [di+4]
  mov di, cx
  mov cx, #pci_routing_table_structure_end - pci_routing_table_structure_start
  rep 
      movsb
  pop di
  pop si
  pop cx
  pop es
  pop ds
  popf
  mov bx, #(1 << 9) | (1 << 11)   ;; irq 9 and 11 are used
  jmp pci_real_ok
pci_real_too_small:
  SEG ES
  mov word ptr [di], #pci_routing_table_structure_end - pci_routing_table_structure_start        
  mov ah, #0x89
  jmp pci_real_fail

pci_real_unknown:
  mov ah, #0x81
pci_real_fail:
  pop edi
  pop esi
  stc
  ret
pci_real_ok:
  xor ah, ah
  pop edi
  pop esi
  clc
  ret

pci_real_select_reg:
  push dx
  mov eax, #0x800000
  mov ax,  bx
  shl eax, #8
  and di,  #0xff
  or  ax,  di
  and al,  #0xfc
  mov dx,  #0x0cf8
  out dx,  eax
  pop dx
  ret
  
.align 16
pci_routing_table_structure:
  db 0x24, 0x50, 0x49, 0x52  ;; "$PIR" signature
  db 0, 1 ;; version
  dw 32 + (6 * 16) ;; table size
  db 0 ;; PCI interrupt router bus
  db 0x08 ;; PCI interrupt router DevFunc
  dw 0x0000 ;; PCI exclusive IRQs 
  dw 0x8086 ;; compatible PCI interrupt router vendor ID
  dw 0x7000 ;; compatible PCI interrupt router device ID
  dw 0,0 ;; Miniport data
  db 0,0,0,0,0,0,0,0,0,0,0 ;; reserved
  db 0x07 ;; checksum
pci_routing_table_structure_start:
  ;; first slot entry PCI-to-ISA (embedded)
  db 0 ;; pci bus number
  db 0x08 ;; pci device number (bit 7-3)
  db 0x60 ;; link value INTA#: pointer into PCI2ISA config space
  dw 0xdef8 ;; IRQ bitmap INTA# 
  db 0x61 ;; link value INTB#
  dw 0xdef8 ;; IRQ bitmap INTB# 
  db 0x62 ;; link value INTC#
  dw 0xdef8 ;; IRQ bitmap INTC# 
  db 0x63 ;; link value INTD#
  dw 0xdef8 ;; IRQ bitmap INTD#
  db 0 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; second slot entry: 1st PCI slot
  db 0 ;; pci bus number
  db 0x10 ;; pci device number (bit 7-3)
  db 0x61 ;; link value INTA#
  dw 0xdef8 ;; IRQ bitmap INTA# 
  db 0x62 ;; link value INTB#
  dw 0xdef8 ;; IRQ bitmap INTB# 
  db 0x63 ;; link value INTC#
  dw 0xdef8 ;; IRQ bitmap INTC# 
  db 0x60 ;; link value INTD#
  dw 0xdef8 ;; IRQ bitmap INTD#
  db 1 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; third slot entry: 2nd PCI slot
  db 0 ;; pci bus number
  db 0x18 ;; pci device number (bit 7-3)
  db 0x62 ;; link value INTA#
  dw 0xdef8 ;; IRQ bitmap INTA# 
  db 0x63 ;; link value INTB#
  dw 0xdef8 ;; IRQ bitmap INTB# 
  db 0x60 ;; link value INTC#
  dw 0xdef8 ;; IRQ bitmap INTC# 
  db 0x61 ;; link value INTD#
  dw 0xdef8 ;; IRQ bitmap INTD#
  db 2 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; 4th slot entry: 3rd PCI slot
  db 0 ;; pci bus number
  db 0x20 ;; pci device number (bit 7-3)
  db 0x63 ;; link value INTA#
  dw 0xdef8 ;; IRQ bitmap INTA# 
  db 0x60 ;; link value INTB#
  dw 0xdef8 ;; IRQ bitmap INTB# 
  db 0x61 ;; link value INTC#
  dw 0xdef8 ;; IRQ bitmap INTC# 
  db 0x62 ;; link value INTD#
  dw 0xdef8 ;; IRQ bitmap INTD#
  db 3 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; 5th slot entry: 4rd PCI slot
  db 0 ;; pci bus number
  db 0x28 ;; pci device number (bit 7-3)
  db 0x60 ;; link value INTA#
  dw 0xdef8 ;; IRQ bitmap INTA# 
  db 0x61 ;; link value INTB#
  dw 0xdef8 ;; IRQ bitmap INTB# 
  db 0x62 ;; link value INTC#
  dw 0xdef8 ;; IRQ bitmap INTC# 
  db 0x63 ;; link value INTD#
  dw 0xdef8 ;; IRQ bitmap INTD#
  db 4 ;; physical slot (0 = embedded)
  db 0 ;; reserved
  ;; 6th slot entry: 5rd PCI slot
  db 0 ;; pci bus number
  db 0x30 ;; pci device number (bit 7-3)
  db 0x61 ;; link value INTA#
  dw 0xdef8 ;; IRQ bitmap INTA# 
  db 0x62 ;; link value INTB#
  dw 0xdef8 ;; IRQ bitmap INTB# 
  db 0x63 ;; link value INTC#
  dw 0xdef8 ;; IRQ bitmap INTC# 
  db 0x60 ;; link value INTD#
  dw 0xdef8 ;; IRQ bitmap INTD#
  db 5 ;; physical slot (0 = embedded)
  db 0 ;; reserved
pci_routing_table_structure_end:

#if !BX_ROMBIOS32
pci_irq_list:
  db 11, 10, 9, 5;

pcibios_init_sel_reg:
  push eax
  mov eax, #0x800000
  mov ax,  bx
  shl eax, #8
  and dl,  #0xfc
  or  al,  dl
  mov dx,  #0x0cf8
  out dx,  eax
  pop eax
  ret
  
pcibios_init_iomem_bases:
  push bp
  mov  bp, sp
  mov  eax, #0xe0000000 ;; base for memory init
  push eax
  mov  ax, #0xc000 ;; base for i/o init
  push ax
  mov  ax, #0x0010 ;; start at base address #0
  push ax
  mov  bx, #0x0008
pci_init_io_loop1:
  mov  dl, #0x00
  call pcibios_init_sel_reg
  mov  dx, #0x0cfc
  in   ax, dx
  cmp  ax, #0xffff
  jz   next_pci_dev
  mov  dl, #0x04 ;; disable i/o and memory space access
  call pcibios_init_sel_reg
  mov  dx, #0x0cfc
  in   al, dx
  and  al, #0xfc
  out  dx, al
pci_init_io_loop2:
  mov  dl, [bp-8]
  call pcibios_init_sel_reg
  mov  dx, #0x0cfc
  in   eax, dx
  test al, #0x01
  jnz  init_io_base
  mov  ecx, eax
  mov  eax, #0xffffffff
  out  dx, eax
  in   eax, dx
  cmp  eax, ecx
  je   next_pci_base
  xor  eax, #0xffffffff
  mov  ecx, eax
  mov  eax, [bp-4]
  out  dx, eax
  add  eax, ecx ;; calculate next free mem base
  add  eax, #0x01000000
  and  eax, #0xff000000
  mov  [bp-4], eax
  jmp  next_pci_base
init_io_base:
  mov  cx, ax
  mov  ax, #0xffff
  out  dx, ax
  in   ax, dx
  cmp  ax, cx
  je   next_pci_base
  xor  ax, #0xfffe
  mov  cx, ax
  mov  ax, [bp-6]
  out  dx, ax
  add  ax, cx ;; calculate next free i/o base
  add  ax, #0x0100
  and  ax, #0xff00
  mov  [bp-6], ax
next_pci_base:
  mov  al, [bp-8]
  add  al, #0x04
  cmp  al, #0x28
  je   enable_iomem_space
  mov  byte ptr[bp-8], al
  jmp  pci_init_io_loop2
enable_iomem_space:
  mov  dl, #0x04 ;; enable i/o and memory space access if available
  call pcibios_init_sel_reg
  mov  dx, #0x0cfc
  in   al, dx
  or   al, #0x07
  out  dx, al
next_pci_dev:
  mov  byte ptr[bp-8], #0x10
  inc  bx
  cmp  bx, #0x0100
  jne  pci_init_io_loop1
  mov  sp, bp
  pop  bp
  ret

pcibios_init_set_elcr:
  push ax
  push cx
  mov  dx, #0x04d0
  test al, #0x08
  jz   is_master_pic
  inc  dx
  and  al, #0x07
is_master_pic:
  mov  cl, al
  mov  bl, #0x01
  shl  bl, cl
  in   al, dx
  or   al, bl
  out  dx, al
  pop  cx
  pop  ax
  ret

pcibios_init_irqs:
  push ds
  push bp
  mov  ax, #0xf000
  mov  ds, ax
  mov  dx, #0x04d0 ;; reset ELCR1 + ELCR2
  mov  al, #0x00
  out  dx, al
  inc  dx
  out  dx, al
  mov  si, #pci_routing_table_structure
  mov  bh, [si+8]
  mov  bl, [si+9]
  mov  dl, #0x00
  call pcibios_init_sel_reg
  mov  dx, #0x0cfc
  in   eax, dx
  cmp  eax, [si+12] ;; check irq router
  jne  pci_init_end
  mov  dl, [si+34]
  call pcibios_init_sel_reg
  push bx ;; save irq router bus + devfunc
  mov  dx, #0x0cfc
  mov  ax, #0x8080
  out  dx, ax ;; reset PIRQ route control
  inc  dx
  inc  dx
  out  dx, ax
  mov  ax, [si+6]
  sub  ax, #0x20
  shr  ax, #4
  mov  cx, ax
  add  si, #0x20 ;; set pointer to 1st entry
  mov  bp, sp
  mov  ax, #pci_irq_list
  push ax
  xor  ax, ax
  push ax
pci_init_irq_loop1:
  mov  bh, [si]
  mov  bl, [si+1]
pci_init_irq_loop2:
  mov  dl, #0x00
  call pcibios_init_sel_reg
  mov  dx, #0x0cfc
  in   ax, dx
  cmp  ax, #0xffff
  jnz  pci_test_int_pin
  test bl, #0x07
  jz   next_pir_entry
  jmp  next_pci_func
pci_test_int_pin:
  mov  dl, #0x3c
  call pcibios_init_sel_reg
  mov  dx, #0x0cfd
  in   al, dx
  and  al, #0x07
  jz   next_pci_func
  dec  al ;; determine pirq reg
  mov  dl, #0x03
  mul  al, dl
  add  al, #0x02
  xor  ah, ah
  mov  bx, ax
  mov  al, [si+bx]
  mov  dl, al
  mov  bx, [bp]
  call pcibios_init_sel_reg
  mov  dx, #0x0cfc
  and  al, #0x03
  add  dl, al
  in   al, dx
  cmp  al, #0x80
  jb   pirq_found
  mov  bx, [bp-2] ;; pci irq list pointer
  mov  al, [bx]
  out  dx, al
  inc  bx
  mov  [bp-2], bx
  call pcibios_init_set_elcr
pirq_found:
  mov  bh, [si]
  mov  bl, [si+1]
  add  bl, [bp-3] ;; pci function number
  mov  dl, #0x3c
  call pcibios_init_sel_reg
  mov  dx, #0x0cfc
  out  dx, al
next_pci_func:
  inc  byte ptr[bp-3]
  inc  bl
  test bl, #0x07
  jnz  pci_init_irq_loop2
next_pir_entry:
  add  si, #0x10
  mov  byte ptr[bp-3], #0x00
  loop pci_init_irq_loop1
  mov  sp, bp
  pop  bx
pci_init_end:
  pop  bp
  pop  ds
  ret
#endif // BX_ROMBIOS32
#endif // BX_PCIBIOS

#if BX_ROMBIOS32
rombios32_init:
  ;; save a20 and enable it
  in al, 0x92
  push ax
  or al, #0x02
  out 0x92, al

  ;; save SS:SP to the BDA
  xor ax, ax
  mov ds, ax
  mov 0x0469, ss
  mov 0x0467, sp

  SEG CS
    lidt [pmode_IDT_info]
  SEG CS
    lgdt [rombios32_gdt_48]
  ;; set PE bit in CR0
  mov  eax, cr0
  or   al, #0x01
  mov  cr0, eax
  ;; start protected mode code: ljmpl 0x10:rombios32_init1
  db 0x66, 0xea
  dw rombios32_05
  dw 0x000f       ;; high 16 bit address
  dw 0x0010

use32 386
rombios32_05:
  ;; init data segments
  mov eax, #0x18
  mov ds, ax
  mov es, ax
  mov ss, ax
  xor eax, eax
  mov fs, ax
  mov gs, ax
  cld

  ;; copy rombios32 code to ram (ram offset = 1MB)
  mov esi, #0xfffe0000
  mov edi, #0x00040000
  mov ecx, #0x10000 / 4
  rep 
    movsd

  ;; init the stack pointer
  mov esp, #0x00080000

  ;; call rombios32 code
  mov eax, #0x00040000
  call eax

  ;; return to 16 bit protected mode first
  db 0xea
  dd rombios32_10
  dw 0x20

use16 386
rombios32_10:
  ;; restore data segment limits to 0xffff
  mov ax, #0x28
  mov ds, ax
  mov es, ax
  mov ss, ax
  mov fs, ax
  mov gs, ax

  ;; reset PE bit in CR0
  mov  eax, cr0
  and  al, #0xFE
  mov  cr0, eax

  ;; far jump to flush CPU queue after transition to real mode
  JMP_AP(0xf000, rombios32_real_mode)

rombios32_real_mode:
  ;; restore IDT to normal real-mode defaults
  SEG CS
    lidt [rmode_IDT_info]

  xor ax, ax
  mov ds, ax
  mov es, ax
  mov fs, ax
  mov gs, ax

  ;; restore SS:SP from the BDA
  mov ss, 0x0469
  xor esp, esp
  mov sp, 0x0467
  ;; restore a20
  pop ax
  out 0x92, al
  ret

rombios32_gdt_48:
  dw 0x30
  dw rombios32_gdt                  
  dw 0x000f

rombios32_gdt:
  dw 0, 0, 0, 0
  dw 0, 0, 0, 0
  dw 0xffff, 0, 0x9b00, 0x00cf ; 32 bit flat code segment (0x10)
  dw 0xffff, 0, 0x9300, 0x00cf ; 32 bit flat data segment (0x18)
  dw 0xffff, 0, 0x9b0f, 0x0000 ; 16 bit code segment base=0xf0000 limit=0xffff
  dw 0xffff, 0, 0x9300, 0x0000 ; 16 bit data segment base=0x0 limit=0xffff
#endif


; parallel port detection: base address in DX, index in BX, timeout in CL
detect_parport:
  push dx
  add  dx, #2
  in   al, dx
  and  al, #0xdf ; clear input mode
  out  dx, al
  pop  dx
  mov  al, #0xaa
  out  dx, al
  in   al, dx
  cmp  al, #0xaa
  jne  no_parport
  push bx
  shl  bx, #1
  mov  [bx+0x408], dx ; Parallel I/O address
  pop  bx
  mov  [bx+0x478], cl ; Parallel printer timeout
  inc  bx
no_parport:
  ret

; serial port detection: base address in DX, index in BX, timeout in CL
detect_serial:
  push dx
  inc  dx
  mov  al, #0x02
  out  dx, al
  in   al, dx
  cmp  al, #0x02
  jne  no_serial
  inc  dx
  in   al, dx
  cmp  al, #0x02
  jne  no_serial
  dec  dx
  xor  al, al
  out  dx, al
  pop  dx
  push bx
  shl  bx, #1
  mov  [bx+0x400], dx ; Serial I/O address
  pop  bx
  mov  [bx+0x47c], cl ; Serial timeout
  inc  bx
  ret
no_serial:
  pop  dx
  ret

rom_checksum:
  push ax
  push bx
  push cx
  xor  ax, ax
  xor  bx, bx
  xor  cx, cx
  mov  ch, [2]
  shl  cx, #1
checksum_loop:
  add  al, [bx]
  inc  bx
  loop checksum_loop
  and  al, #0xff
  pop  cx
  pop  bx
  pop  ax
  ret

rom_scan:
  ;; Scan for existence of valid expansion ROMS.
  ;;   Video ROM:   from 0xC0000..0xC7FFF in 2k increments
  ;;   General ROM: from 0xC8000..0xDFFFF in 2k increments
  ;;   System  ROM: only 0xE0000
  ;;
  ;; Header:
  ;;   Offset    Value
  ;;   0         0x55
  ;;   1         0xAA
  ;;   2         ROM length in 512-byte blocks
  ;;   3         ROM initialization entry point (FAR CALL)

  mov  cx, #0xc000
rom_scan_loop:
  mov  ds, cx
  mov  ax, #0x0004 ;; start with increment of 4 (512-byte) blocks = 2k
  cmp [0], #0xAA55 ;; look for signature
  jne  rom_scan_increment
  call rom_checksum
  jnz  rom_scan_increment
  mov  al, [2]  ;; change increment to ROM length in 512-byte blocks

  ;; We want our increment in 512-byte quantities, rounded to
  ;; the nearest 2k quantity, since we only scan at 2k intervals.
  test al, #0x03
  jz   block_count_rounded
  and  al, #0xfc ;; needs rounding up
  add  al, #0x04
block_count_rounded:

  xor  bx, bx   ;; Restore DS back to 0000:
  mov  ds, bx
  push ax       ;; Save AX
  ;; Push addr of ROM entry point
  push cx       ;; Push seg
  push #0x0003  ;; Push offset
  mov  bp, sp   ;; Call ROM init routine using seg:off on stack
  db   0xff     ;; call_far ss:[bp+0]
  db   0x5e
  db   0
  cli           ;; In case expansion ROM BIOS turns IF on
  add  sp, #2   ;; Pop offset value
  pop  cx       ;; Pop seg value (restore CX)
  pop  ax       ;; Restore AX
rom_scan_increment:
  shl  ax, #5   ;; convert 512-bytes blocks to 16-byte increments
                ;; because the segment selector is shifted left 4 bits.
  add  cx, ax
  cmp  cx, #0xe000
  jbe  rom_scan_loop

  xor  ax, ax   ;; Restore DS back to 0000:
  mov  ds, ax
  ret

;; for 'C' strings and other data, insert them here with
;; a the following hack:
;; DATA_SEG_DEFS_HERE


;; the following area can be used to write dynamically generated tables
  .align 16
bios_table_area_start:
  dd 0xaafb4442
  dd bios_table_area_end - bios_table_area_start - 8;

;--------
;- POST -
;--------
.org 0xe05b ; POST Entry Point
bios_table_area_end:
post:

  xor ax, ax

  ;; first reset the DMA controllers
  out 0x0d,al
  out 0xda,al

  ;; then initialize the DMA controllers
  mov al, #0xC0
  out 0xD6, al ; cascade mode of channel 4 enabled
  mov al, #0x00
  out 0xD4, al ; unmask channel 4

  ;; Examine CMOS shutdown status.
  mov AL, #0x0f
  out 0x70, AL
  in  AL, 0x71

  ;; backup status
  mov bl, al

  ;; Reset CMOS shutdown status.
  mov AL, #0x0f
  out 0x70, AL          ; select CMOS register Fh
  mov AL, #0x00
  out 0x71, AL          ; set shutdown action to normal

  ;; Examine CMOS shutdown status.
  mov al, bl

  ;; 0x00, 0x09, 0x0D+ = normal startup
  cmp AL, #0x00
  jz normal_post
  cmp AL, #0x0d
  jae normal_post
  cmp AL, #0x09
  je normal_post

  ;; 0x05 = eoi + jmp via [0x40:0x67] jump
  cmp al, #0x05
  je  eoi_jmp_post

  ;; Examine CMOS shutdown status.
  ;;  0x01,0x02,0x03,0x04,0x06,0x07,0x08, 0x0a, 0x0b, 0x0c = Unimplemented shutdown status.
  push bx
  call _shutdown_status_panic

#if 0 
  HALT(__LINE__)
  ;
  ;#if 0
  ;  0xb0, 0x20,       /* mov al, #0x20 */
  ;  0xe6, 0x20,       /* out 0x20, al    ;send EOI to PIC */
  ;#endif
  ;
  pop es
  pop ds
  popa
  iret
#endif

normal_post:
  ; case 0: normal startup

  cli
  mov  ax, #0xfffe
  mov  sp, ax
  xor  ax, ax
  mov  ds, ax
  mov  ss, ax

  ;; zero out BIOS data area (40:00..40:ff)
  mov  es, ax
  mov  cx, #0x0080 ;; 128 words
  mov  di, #0x0400
  cld
  rep
    stosw

  call _log_bios_start

  ;; set all interrupts to default handler
  xor  bx, bx         ;; offset index
  mov  cx, #0x0100    ;; counter (256 interrupts)
  mov  ax, #dummy_iret_handler
  mov  dx, #0xF000

post_default_ints:
  mov  [bx], ax
  inc  bx
  inc  bx
  mov  [bx], dx
  inc  bx
  inc  bx
  loop post_default_ints

  ;; set vector 0x79 to zero
  ;; this is used by 'gardian angel' protection system
  SET_INT_VECTOR(0x79, #0, #0)

  ;; base memory in K 40:13 (word)
  mov  ax, #BASE_MEM_IN_K
  mov  0x0413, ax


  ;; Manufacturing Test 40:12
  ;;   zerod out above

  ;; Warm Boot Flag 0040:0072
  ;;   value of 1234h = skip memory checks
  ;;   zerod out above


  ;; Printer Services vector
  SET_INT_VECTOR(0x17, #0xF000, #int17_handler)

  ;; Bootstrap failure vector
  SET_INT_VECTOR(0x18, #0xF000, #int18_handler)

  ;; Bootstrap Loader vector
  SET_INT_VECTOR(0x19, #0xF000, #int19_handler)

  ;; User Timer Tick vector
  SET_INT_VECTOR(0x1c, #0xF000, #int1c_handler)

  ;; Memory Size Check vector
  SET_INT_VECTOR(0x12, #0xF000, #int12_handler)

  ;; Equipment Configuration Check vector
  SET_INT_VECTOR(0x11, #0xF000, #int11_handler)

  ;; System Services
  SET_INT_VECTOR(0x15, #0xF000, #int15_handler)

  ;; EBDA setup
  call ebda_post

  ;; PIT setup
  SET_INT_VECTOR(0x08, #0xF000, #int08_handler)
  ;; int 1C already points at dummy_iret_handler (above)
  mov al, #0x34 ; timer0: binary count, 16bit count, mode 2
  out 0x43, al
  mov al, #0x00 ; maximum count of 0000H = 18.2Hz
  out 0x40, al
  out 0x40, al

  ;; Keyboard
  SET_INT_VECTOR(0x09, #0xF000, #int09_handler)
  SET_INT_VECTOR(0x16, #0xF000, #int16_handler)

  xor  ax, ax
  mov  ds, ax
  mov  0x0417, al /* keyboard shift flags, set 1 */
  mov  0x0418, al /* keyboard shift flags, set 2 */
  mov  0x0419, al /* keyboard alt-numpad work area */
  mov  0x0471, al /* keyboard ctrl-break flag */
  mov  0x0497, al /* keyboard status flags 4 */
  mov  al, #0x10
  mov  0x0496, al /* keyboard status flags 3 */


  /* keyboard head of buffer pointer */
  mov  bx, #0x001E
  mov  0x041A, bx

  /* keyboard end of buffer pointer */
  mov  0x041C, bx

  /* keyboard pointer to start of buffer */
  mov  bx, #0x001E
  mov  0x0480, bx

  /* keyboard pointer to end of buffer */
  mov  bx, #0x003E
  mov  0x0482, bx

  /* init the keyboard */
  call _keyboard_init

  ;; mov CMOS Equipment Byte to BDA Equipment Word
  mov  ax, 0x0410
  mov  al, #0x14
  out  0x70, al
  in   al, 0x71
  mov  0x0410, ax


  ;; Parallel setup
  SET_INT_VECTOR(0x0F, #0xF000, #dummy_iret_handler)
  xor ax, ax
  mov ds, ax
  xor bx, bx
  mov cl, #0x14 ; timeout value
  mov dx, #0x378 ; Parallel I/O address, port 1
  call detect_parport
  mov dx, #0x278 ; Parallel I/O address, port 2
  call detect_parport
  shl bx, #0x0e
  mov ax, 0x410   ; Equipment word bits 14..15 determing # parallel ports
  and ax, #0x3fff
  or  ax, bx ; set number of parallel ports
  mov 0x410, ax

  ;; Serial setup
  SET_INT_VECTOR(0x0C, #0xF000, #dummy_iret_handler)
  SET_INT_VECTOR(0x14, #0xF000, #int14_handler)
  xor bx, bx
  mov cl, #0x0a ; timeout value
  mov dx, #0x03f8 ; Serial I/O address, port 1
  call detect_serial
  mov dx, #0x02f8 ; Serial I/O address, port 2
  call detect_serial
  mov dx, #0x03e8 ; Serial I/O address, port 3
  call detect_serial
  mov dx, #0x02e8 ; Serial I/O address, port 4
  call detect_serial
  shl bx, #0x09
  mov ax, 0x410   ; Equipment word bits 9..11 determing # serial ports
  and ax, #0xf1ff
  or  ax, bx ; set number of serial port
  mov 0x410, ax

  ;; CMOS RTC
  SET_INT_VECTOR(0x1A, #0xF000, #int1a_handler)
  SET_INT_VECTOR(0x4A, #0xF000, #dummy_iret_handler)
  SET_INT_VECTOR(0x70, #0xF000, #int70_handler)
  ;; BIOS DATA AREA 0x4CE ???
  call timer_tick_post

  ;; PS/2 mouse setup
  SET_INT_VECTOR(0x74, #0xF000, #int74_handler)

  ;; IRQ13 (FPU exception) setup
  SET_INT_VECTOR(0x75, #0xF000, #int75_handler)

  ;; Video setup
  SET_INT_VECTOR(0x10, #0xF000, #int10_handler)

  ;; PIC
  mov al, #0x11 ; send initialisation commands
  out 0x20, al
  out 0xa0, al
  mov al, #0x08
  out 0x21, al
  mov al, #0x70
  out 0xa1, al
  mov al, #0x04
  out 0x21, al
  mov al, #0x02
  out 0xa1, al
  mov al, #0x01
  out 0x21, al
  out 0xa1, al
  mov  al, #0xb8
  out  0x21, AL ;master pic: unmask IRQ 0, 1, 2, 6
#if BX_USE_PS2_MOUSE
  mov  al, #0x8f
#else
  mov  al, #0x9f
#endif
  out  0xa1, AL ;slave  pic: unmask IRQ 12, 13, 14

#if BX_ROMBIOS32
  call rombios32_init
#else
  call pcibios_init_iomem_bases
  call pcibios_init_irqs
#endif
  call rom_scan

  call _print_bios_banner 

  ;;
  ;; Floppy setup
  ;;
  call floppy_drive_post

#if BX_USE_ATADRV

  ;;
  ;; Hard Drive setup
  ;;
  call hard_drive_post

  ;;
  ;; ATA/ATAPI driver setup
  ;;
  call _ata_init
  call _ata_detect
  ;;
#else // BX_USE_ATADRV

  ;;
  ;; Hard Drive setup
  ;;
  call hard_drive_post

#endif // BX_USE_ATADRV

#if BX_ELTORITO_BOOT
  ;;
  ;; eltorito floppy/harddisk emulation from cd
  ;;
  call _cdemu_init
  ;;
#endif // BX_ELTORITO_BOOT
 
  sti        ;; enable interrupts
  int  #0x19


.org 0xe2c3 ; NMI Handler Entry Point
nmi:
  ;; FIXME the NMI handler should not panic
  ;; but iret when called from int75 (fpu exception)
  call _nmi_handler_msg
  iret

int75_handler:
  out  0xf0, al         // clear irq13 
  call eoi_both_pics    // clear interrupt
  int  2                // legacy nmi call
  iret

;-------------------------------------------
;- INT 13h Fixed Disk Services Entry Point -
;-------------------------------------------
.org 0xe3fe ; INT 13h Fixed Disk Services Entry Point
int13_handler:
  //JMPL(int13_relocated)
  jmp int13_relocated

.org 0xe401 ; Fixed Disk Parameter Table

;----------
;- INT19h -
;----------
.org 0xe6f2 ; INT 19h Boot Load Service Entry Point
int19_handler:

  jmp int19_relocated
;-------------------------------------------
;- System BIOS Configuration Data Table
;-------------------------------------------
.org BIOS_CONFIG_TABLE
db 0x08                  ; Table size (bytes) -Lo
db 0x00                  ; Table size (bytes) -Hi
db SYS_MODEL_ID
db SYS_SUBMODEL_ID
db BIOS_REVISION
; Feature byte 1
; b7: 1=DMA channel 3 used by hard disk
; b6: 1=2 interrupt controllers present
; b5: 1=RTC present
; b4: 1=BIOS calls int 15h/4Fh every key
; b3: 1=wait for extern event supported (Int 15h/41h)
; b2: 1=extended BIOS data area used
; b1: 0=AT or ESDI bus, 1=MicroChannel
; b0: 1=Dual bus (MicroChannel + ISA)
db (0 << 7) | \
   (1 << 6) | \
   (1 << 5) | \
   (BX_CALL_INT15_4F << 4) | \
   (0 << 3) | \
   (BX_USE_EBDA << 2) | \
   (0 << 1) | \
   (0 << 0)
; Feature byte 2
; b7: 1=32-bit DMA supported
; b6: 1=int16h, function 9 supported
; b5: 1=int15h/C6h (get POS data) supported
; b4: 1=int15h/C7h (get mem map info) supported
; b3: 1=int15h/C8h (en/dis CPU) supported
; b2: 1=non-8042 kb controller
; b1: 1=data streaming supported
; b0: reserved
db (0 << 7) | \
   (1 << 6) | \
   (0 << 5) | \
   (0 << 4) | \
   (0 << 3) | \
   (0 << 2) | \
   (0 << 1) | \
   (0 << 0)
; Feature byte 3
; b7: not used
; b6: reserved
; b5: reserved
; b4: POST supports ROM-to-RAM enable/disable
; b3: SCSI on system board
; b2: info panel installed
; b1: Initial Machine Load (IML) system - BIOS on disk
; b0: SCSI supported in IML
db 0x00
; Feature byte 4
; b7: IBM private
; b6: EEPROM present
; b5-3: ABIOS presence (011 = not supported)
; b2: private
; b1: memory split above 16Mb supported
; b0: POSTEXT directly supported by POST
db 0x00
; Feature byte 5 (IBM)
; b1: enhanced mouse
; b0: flash EPROM
db 0x00



.org 0xe729 ; Baud Rate Generator Table

;----------
;- INT14h -
;----------
.org 0xe739 ; INT 14h Serial Communications Service Entry Point
int14_handler:
  push ds
  pusha
  xor  ax, ax
  mov  ds, ax
  call _int14_function
  popa
  pop  ds
  iret


;----------------------------------------
;- INT 16h Keyboard Service Entry Point -
;----------------------------------------
.org 0xe82e
int16_handler:

  sti
  push  ds
  pushf
  pusha

  cmp   ah, #0x00
  je    int16_F00
  cmp   ah, #0x10
  je    int16_F00

  mov  bx, #0xf000
  mov  ds, bx
  call _int16_function
  popa
  popf
  pop  ds
  jz   int16_zero_set

int16_zero_clear:
  push bp
  mov  bp, sp
  //SEG SS
  and  BYTE [bp + 0x06], #0xbf
  pop  bp
  iret

int16_zero_set:
  push bp
  mov  bp, sp
  //SEG SS
  or   BYTE [bp + 0x06], #0x40
  pop  bp
  iret

int16_F00:
  mov  bx, #0x0040
  mov  ds, bx

int16_wait_for_key:
  cli
  mov  bx, 0x001a
  cmp  bx, 0x001c
  jne  int16_key_found
  sti
  nop
#if 0
                           /* no key yet, call int 15h, function AX=9002 */
  0x50,                    /* push AX */
  0xb8, 0x02, 0x90,        /* mov AX, #0x9002 */
  0xcd, 0x15,              /* int 15h */
  0x58,                    /* pop  AX */
  0xeb, 0xea,              /* jmp   WAIT_FOR_KEY */
#endif
  jmp  int16_wait_for_key

int16_key_found:
  mov  bx, #0xf000
  mov  ds, bx
  call _int16_function
  popa
  popf
  pop  ds
#if 0
                           /* notify int16 complete w/ int 15h, function AX=9102 */
  0x50,                    /* push AX */
  0xb8, 0x02, 0x91,        /* mov AX, #0x9102 */
  0xcd, 0x15,              /* int 15h */
  0x58,                    /* pop  AX */
#endif
  iret



;-------------------------------------------------
;- INT09h : Keyboard Hardware Service Entry Point -
;-------------------------------------------------
.org 0xe987
int09_handler:
  cli
  push ax

  mov al, #0xAD      ;;disable keyboard
  out #0x64, al

  mov al, #0x0B
  out #0x20, al
  in  al, #0x20
  and al, #0x02
  jz  int09_finish

  in  al, #0x60             ;;read key from keyboard controller
  sti
  push  ds
  pusha
#ifdef BX_CALL_INT15_4F
  mov  ah, #0x4f     ;; allow for keyboard intercept
  stc
  int  #0x15
  jnc  int09_done
#endif

  ;; check for extended key
  cmp  al, #0xe0
  jne int09_check_pause
  xor  ax, ax
  mov  ds, ax
  mov  al, BYTE [0x496]     ;; mf2_state |= 0x02
  or   al, #0x02
  mov  BYTE [0x496], al
  jmp int09_done

int09_check_pause:  ;; check for pause key
  cmp  al, #0xe1
  jne int09_process_key
  xor  ax, ax
  mov  ds, ax
  mov  al, BYTE [0x496]     ;; mf2_state |= 0x01
  or   al, #0x01
  mov  BYTE [0x496], al
  jmp int09_done

int09_process_key:
  mov   bx, #0xf000
  mov   ds, bx
  call  _int09_function

int09_done:
  popa
  pop   ds
  cli
  call eoi_master_pic

int09_finish:
  mov al, #0xAE      ;;enable keyboard
  out #0x64, al
  pop ax
  iret


;----------------------------------------
;- INT 13h Diskette Service Entry Point -
;----------------------------------------
.org 0xec59
int13_diskette:
  jmp int13_noeltorito

;---------------------------------------------
;- INT 0Eh Diskette Hardware ISR Entry Point -
;---------------------------------------------
.org 0xef57 ; INT 0Eh Diskette Hardware ISR Entry Point
int0e_handler:
  push ax
  push dx
  mov  dx, #0x03f4
  in   al, dx
  and  al, #0xc0
  cmp  al, #0xc0
  je   int0e_normal
  mov  dx, #0x03f5
  mov  al, #0x08 ; sense interrupt status
  out  dx, al
int0e_loop1:
  mov  dx, #0x03f4
  in   al, dx
  and  al, #0xc0
  cmp  al, #0xc0
  jne  int0e_loop1
int0e_loop2:
  mov  dx, #0x03f5
  in   al, dx
  mov  dx, #0x03f4
  in   al, dx
  and  al, #0xc0
  cmp  al, #0xc0
  je int0e_loop2
int0e_normal:
  push ds
  xor  ax, ax ;; segment 0000
  mov  ds, ax
  call eoi_master_pic
  mov  al, 0x043e
  or   al, #0x80 ;; diskette interrupt has occurred
  mov  0x043e, al
  pop  ds
  pop  dx
  pop  ax
  iret


.org 0xefc7 ; Diskette Controller Parameter Table
diskette_param_table:
;;  Since no provisions are made for multiple drive types, most
;;  values in this table are ignored.  I set parameters for 1.44M
;;  floppy here
db  0xAF
db  0x02 ;; head load time 0000001, DMA used
db  0x25
db  0x02
db    18
db  0x1B
db  0xFF
db  0x6C
db  0xF6
db  0x0F
db  0x08


;----------------------------------------
;- INT17h : Printer Service Entry Point -
;----------------------------------------
.org 0xefd2
int17_handler:
  push ds
  pusha
  xor  ax, ax
  mov  ds, ax
  call _int17_function
  popa
  pop  ds
  iret

diskette_param_table2:
;;  New diskette parameter table adding 3 parameters from IBM
;;  Since no provisions are made for multiple drive types, most
;;  values in this table are ignored.  I set parameters for 1.44M
;;  floppy here
db  0xAF
db  0x02 ;; head load time 0000001, DMA used
db  0x25
db  0x02
db    18
db  0x1B
db  0xFF
db  0x6C
db  0xF6
db  0x0F
db  0x08
db    79 ;; maximum track
db     0 ;; data transfer rate
db     4 ;; drive type in cmos

.org 0xf045 ; INT 10 Functions 0-Fh Entry Point
  HALT(__LINE__)
  iret

;----------
;- INT10h -
;----------
.org 0xf065 ; INT 10h Video Support Service Entry Point
int10_handler:
  ;; dont do anything, since the VGA BIOS handles int10h requests
  iret

.org 0xf0a4 ; MDA/CGA Video Parameter Table (INT 1Dh)

;----------
;- INT12h -
;----------
.org 0xf841 ; INT 12h Memory Size Service Entry Point
; ??? different for Pentium (machine check)?
int12_handler:
  push ds
  mov  ax, #0x0040
  mov  ds, ax
  mov  ax, 0x0013
  pop  ds
  iret

;----------
;- INT11h -
;----------
.org 0xf84d ; INT 11h Equipment List Service Entry Point
int11_handler:
  push ds
  mov  ax, #0x0040
  mov  ds, ax
  mov  ax, 0x0010
  pop  ds
  iret

;----------
;- INT15h -
;----------
.org 0xf859 ; INT 15h System Services Entry Point
int15_handler:
  pushf
#if BX_APM
  cmp ah, #0x53
  je apm_call
#endif
  push  ds
  push  es
  cmp  ah, #0x86
  je int15_handler32
  cmp  ah, #0xE8
  je int15_handler32
  pusha
#if BX_USE_PS2_MOUSE
  cmp  ah, #0xC2
  je int15_handler_mouse
#endif
  call _int15_function
int15_handler_mouse_ret:
  popa
int15_handler32_ret:
  pop   es
  pop   ds
  popf
  jmp iret_modify_cf
#if BX_APM
apm_call:
  jmp _apmreal_entry
#endif

#if BX_USE_PS2_MOUSE
int15_handler_mouse:
  call _int15_function_mouse
  jmp int15_handler_mouse_ret
#endif

int15_handler32:
  pushad
  call _int15_function32
  popad
  jmp int15_handler32_ret

;; Protected mode IDT descriptor
;;
;; I just make the limit 0, so the machine will shutdown
;; if an exception occurs during protected mode memory
;; transfers.
;;
;; Set base to f0000 to correspond to beginning of BIOS,
;; in case I actually define an IDT later
;; Set limit to 0

pmode_IDT_info:
dw 0x0000  ;; limit 15:00
dw 0x0000  ;; base  15:00
db 0x0f    ;; base  23:16

;; Real mode IDT descriptor
;;
;; Set to typical real-mode values.
;; base  = 000000
;; limit =   03ff

rmode_IDT_info:
dw 0x03ff  ;; limit 15:00
dw 0x0000  ;; base  15:00
db 0x00    ;; base  23:16


;----------
;- INT1Ah -
;----------
.org 0xfe6e ; INT 1Ah Time-of-day Service Entry Point
int1a_handler:
#if BX_PCIBIOS
  cmp  ah, #0xb1
  jne  int1a_normal
  call pcibios_real
  jc   pcibios_error
  retf 2
pcibios_error:
  mov  bl, ah
  mov  ah, #0xb1
  push ds
  pusha
  mov ax, ss  ; set readable descriptor to ds, for calling pcibios
  mov ds, ax  ;  on 16bit protected mode.
  jmp int1a_callfunction
int1a_normal:
#endif
  push ds
  pusha
  xor  ax, ax
  mov  ds, ax
int1a_callfunction:
  call _int1a_function
  popa
  pop  ds
  iret

;;
;; int70h: IRQ8 - CMOS RTC
;;
int70_handler:
  push ds
  pushad
  xor  ax, ax
  mov  ds, ax
  call _int70_function
  popad
  pop  ds
  iret

;---------
;- INT08 -
;---------
.org 0xfea5 ; INT 08h System Timer ISR Entry Point
int08_handler:
  sti
  push eax
  push ds
  xor ax, ax
  mov ds, ax

  ;; time to turn off drive(s)?
  mov  al,0x0440
  or   al,al
  jz   int08_floppy_off
  dec  al
  mov  0x0440,al
  jnz  int08_floppy_off
  ;; turn motor(s) off
  push dx
  mov  dx,#0x03f2
  in   al,dx
  and  al,#0xcf
  out  dx,al
  pop  dx
int08_floppy_off:

  mov eax, 0x046c ;; get ticks dword
  inc eax

  ;; compare eax to one days worth of timer ticks at 18.2 hz
  cmp eax, #0x001800B0
  jb  int08_store_ticks
  ;; there has been a midnight rollover at this point
  xor eax, eax    ;; zero out counter
  inc BYTE 0x0470 ;; increment rollover flag

int08_store_ticks:
  mov 0x046c, eax ;; store new ticks dword
  ;; chain to user timer tick INT #0x1c
  //pushf
  //;; call_ep [ds:loc]
  //CALL_EP( 0x1c << 2 )
  int #0x1c
  cli
  call eoi_master_pic
  pop ds
  pop eax
  iret

.org 0xfef3 ; Initial Interrupt Vector Offsets Loaded by POST


.org 0xff00
.ascii BIOS_COPYRIGHT_STRING

;------------------------------------------------
;- IRET Instruction for Dummy Interrupt Handler -
;------------------------------------------------
.org 0xff53 ; IRET Instruction for Dummy Interrupt Handler
dummy_iret_handler:
  iret

.org 0xff54 ; INT 05h Print Screen Service Entry Point
  HALT(__LINE__)
  iret

.org 0xfff0 ; Power-up Entry Point
  jmp 0xf000:post

.org 0xfff5 ; ASCII Date ROM was built - 8 characters in MM/DD/YY
.ascii BIOS_BUILD_DATE

.org 0xfffe ; System Model ID
db SYS_MODEL_ID
db 0x00   ; filler

.org 0xfa6e ;; Character Font for 320x200 & 640x200 Graphics (lower 128 characters)
ASM_END
/*
 * This font comes from the fntcol16.zip package (c) by  Joseph Gil 
 * found at ftp://ftp.simtel.net/pub/simtelnet/msdos/screen/fntcol16.zip
 * This font is public domain
 */ 
static Bit8u vgafont8[128*8]=
{
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x7e, 0x81, 0xa5, 0x81, 0xbd, 0x99, 0x81, 0x7e,
 0x7e, 0xff, 0xdb, 0xff, 0xc3, 0xe7, 0xff, 0x7e,
 0x6c, 0xfe, 0xfe, 0xfe, 0x7c, 0x38, 0x10, 0x00,
 0x10, 0x38, 0x7c, 0xfe, 0x7c, 0x38, 0x10, 0x00,
 0x38, 0x7c, 0x38, 0xfe, 0xfe, 0x7c, 0x38, 0x7c,
 0x10, 0x10, 0x38, 0x7c, 0xfe, 0x7c, 0x38, 0x7c,
 0x00, 0x00, 0x18, 0x3c, 0x3c, 0x18, 0x00, 0x00,
 0xff, 0xff, 0xe7, 0xc3, 0xc3, 0xe7, 0xff, 0xff,
 0x00, 0x3c, 0x66, 0x42, 0x42, 0x66, 0x3c, 0x00,
 0xff, 0xc3, 0x99, 0xbd, 0xbd, 0x99, 0xc3, 0xff,
 0x0f, 0x07, 0x0f, 0x7d, 0xcc, 0xcc, 0xcc, 0x78,
 0x3c, 0x66, 0x66, 0x66, 0x3c, 0x18, 0x7e, 0x18,
 0x3f, 0x33, 0x3f, 0x30, 0x30, 0x70, 0xf0, 0xe0,
 0x7f, 0x63, 0x7f, 0x63, 0x63, 0x67, 0xe6, 0xc0,
 0x99, 0x5a, 0x3c, 0xe7, 0xe7, 0x3c, 0x5a, 0x99,
 0x80, 0xe0, 0xf8, 0xfe, 0xf8, 0xe0, 0x80, 0x00,
 0x02, 0x0e, 0x3e, 0xfe, 0x3e, 0x0e, 0x02, 0x00,
 0x18, 0x3c, 0x7e, 0x18, 0x18, 0x7e, 0x3c, 0x18,
 0x66, 0x66, 0x66, 0x66, 0x66, 0x00, 0x66, 0x00,
 0x7f, 0xdb, 0xdb, 0x7b, 0x1b, 0x1b, 0x1b, 0x00,
 0x3e, 0x63, 0x38, 0x6c, 0x6c, 0x38, 0xcc, 0x78,
 0x00, 0x00, 0x00, 0x00, 0x7e, 0x7e, 0x7e, 0x00,
 0x18, 0x3c, 0x7e, 0x18, 0x7e, 0x3c, 0x18, 0xff,
 0x18, 0x3c, 0x7e, 0x18, 0x18, 0x18, 0x18, 0x00,
 0x18, 0x18, 0x18, 0x18, 0x7e, 0x3c, 0x18, 0x00,
 0x00, 0x18, 0x0c, 0xfe, 0x0c, 0x18, 0x00, 0x00,
 0x00, 0x30, 0x60, 0xfe, 0x60, 0x30, 0x00, 0x00,
 0x00, 0x00, 0xc0, 0xc0, 0xc0, 0xfe, 0x00, 0x00,
 0x00, 0x24, 0x66, 0xff, 0x66, 0x24, 0x00, 0x00,
 0x00, 0x18, 0x3c, 0x7e, 0xff, 0xff, 0x00, 0x00,
 0x00, 0xff, 0xff, 0x7e, 0x3c, 0x18, 0x00, 0x00,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x30, 0x78, 0x78, 0x30, 0x30, 0x00, 0x30, 0x00,
 0x6c, 0x6c, 0x6c, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x6c, 0x6c, 0xfe, 0x6c, 0xfe, 0x6c, 0x6c, 0x00,
 0x30, 0x7c, 0xc0, 0x78, 0x0c, 0xf8, 0x30, 0x00,
 0x00, 0xc6, 0xcc, 0x18, 0x30, 0x66, 0xc6, 0x00,
 0x38, 0x6c, 0x38, 0x76, 0xdc, 0xcc, 0x76, 0x00,
 0x60, 0x60, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x18, 0x30, 0x60, 0x60, 0x60, 0x30, 0x18, 0x00,
 0x60, 0x30, 0x18, 0x18, 0x18, 0x30, 0x60, 0x00,
 0x00, 0x66, 0x3c, 0xff, 0x3c, 0x66, 0x00, 0x00,
 0x00, 0x30, 0x30, 0xfc, 0x30, 0x30, 0x00, 0x00,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x60,
 0x00, 0x00, 0x00, 0xfc, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x00,
 0x06, 0x0c, 0x18, 0x30, 0x60, 0xc0, 0x80, 0x00,
 0x7c, 0xc6, 0xce, 0xde, 0xf6, 0xe6, 0x7c, 0x00,
 0x30, 0x70, 0x30, 0x30, 0x30, 0x30, 0xfc, 0x00,
 0x78, 0xcc, 0x0c, 0x38, 0x60, 0xcc, 0xfc, 0x00,
 0x78, 0xcc, 0x0c, 0x38, 0x0c, 0xcc, 0x78, 0x00,
 0x1c, 0x3c, 0x6c, 0xcc, 0xfe, 0x0c, 0x1e, 0x00,
 0xfc, 0xc0, 0xf8, 0x0c, 0x0c, 0xcc, 0x78, 0x00,
 0x38, 0x60, 0xc0, 0xf8, 0xcc, 0xcc, 0x78, 0x00,
 0xfc, 0xcc, 0x0c, 0x18, 0x30, 0x30, 0x30, 0x00,
 0x78, 0xcc, 0xcc, 0x78, 0xcc, 0xcc, 0x78, 0x00,
 0x78, 0xcc, 0xcc, 0x7c, 0x0c, 0x18, 0x70, 0x00,
 0x00, 0x30, 0x30, 0x00, 0x00, 0x30, 0x30, 0x00,
 0x00, 0x30, 0x30, 0x00, 0x00, 0x30, 0x30, 0x60,
 0x18, 0x30, 0x60, 0xc0, 0x60, 0x30, 0x18, 0x00,
 0x00, 0x00, 0xfc, 0x00, 0x00, 0xfc, 0x00, 0x00,
 0x60, 0x30, 0x18, 0x0c, 0x18, 0x30, 0x60, 0x00,
 0x78, 0xcc, 0x0c, 0x18, 0x30, 0x00, 0x30, 0x00,
 0x7c, 0xc6, 0xde, 0xde, 0xde, 0xc0, 0x78, 0x00,
 0x30, 0x78, 0xcc, 0xcc, 0xfc, 0xcc, 0xcc, 0x00,
 0xfc, 0x66, 0x66, 0x7c, 0x66, 0x66, 0xfc, 0x00,
 0x3c, 0x66, 0xc0, 0xc0, 0xc0, 0x66, 0x3c, 0x00,
 0xf8, 0x6c, 0x66, 0x66, 0x66, 0x6c, 0xf8, 0x00,
 0xfe, 0x62, 0x68, 0x78, 0x68, 0x62, 0xfe, 0x00,
 0xfe, 0x62, 0x68, 0x78, 0x68, 0x60, 0xf0, 0x00,
 0x3c, 0x66, 0xc0, 0xc0, 0xce, 0x66, 0x3e, 0x00,
 0xcc, 0xcc, 0xcc, 0xfc, 0xcc, 0xcc, 0xcc, 0x00,
 0x78, 0x30, 0x30, 0x30, 0x30, 0x30, 0x78, 0x00,
 0x1e, 0x0c, 0x0c, 0x0c, 0xcc, 0xcc, 0x78, 0x00,
 0xe6, 0x66, 0x6c, 0x78, 0x6c, 0x66, 0xe6, 0x00,
 0xf0, 0x60, 0x60, 0x60, 0x62, 0x66, 0xfe, 0x00,
 0xc6, 0xee, 0xfe, 0xfe, 0xd6, 0xc6, 0xc6, 0x00,
 0xc6, 0xe6, 0xf6, 0xde, 0xce, 0xc6, 0xc6, 0x00,
 0x38, 0x6c, 0xc6, 0xc6, 0xc6, 0x6c, 0x38, 0x00,
 0xfc, 0x66, 0x66, 0x7c, 0x60, 0x60, 0xf0, 0x00,
 0x78, 0xcc, 0xcc, 0xcc, 0xdc, 0x78, 0x1c, 0x00,
 0xfc, 0x66, 0x66, 0x7c, 0x6c, 0x66, 0xe6, 0x00,
 0x78, 0xcc, 0xe0, 0x70, 0x1c, 0xcc, 0x78, 0x00,
 0xfc, 0xb4, 0x30, 0x30, 0x30, 0x30, 0x78, 0x00,
 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xfc, 0x00,
 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0x78, 0x30, 0x00,
 0xc6, 0xc6, 0xc6, 0xd6, 0xfe, 0xee, 0xc6, 0x00,
 0xc6, 0xc6, 0x6c, 0x38, 0x38, 0x6c, 0xc6, 0x00,
 0xcc, 0xcc, 0xcc, 0x78, 0x30, 0x30, 0x78, 0x00,
 0xfe, 0xc6, 0x8c, 0x18, 0x32, 0x66, 0xfe, 0x00,
 0x78, 0x60, 0x60, 0x60, 0x60, 0x60, 0x78, 0x00,
 0xc0, 0x60, 0x30, 0x18, 0x0c, 0x06, 0x02, 0x00,
 0x78, 0x18, 0x18, 0x18, 0x18, 0x18, 0x78, 0x00,
 0x10, 0x38, 0x6c, 0xc6, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff,
 0x30, 0x30, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x00, 0x78, 0x0c, 0x7c, 0xcc, 0x76, 0x00,
 0xe0, 0x60, 0x60, 0x7c, 0x66, 0x66, 0xdc, 0x00,
 0x00, 0x00, 0x78, 0xcc, 0xc0, 0xcc, 0x78, 0x00,
 0x1c, 0x0c, 0x0c, 0x7c, 0xcc, 0xcc, 0x76, 0x00,
 0x00, 0x00, 0x78, 0xcc, 0xfc, 0xc0, 0x78, 0x00,
 0x38, 0x6c, 0x60, 0xf0, 0x60, 0x60, 0xf0, 0x00,
 0x00, 0x00, 0x76, 0xcc, 0xcc, 0x7c, 0x0c, 0xf8,
 0xe0, 0x60, 0x6c, 0x76, 0x66, 0x66, 0xe6, 0x00,
 0x30, 0x00, 0x70, 0x30, 0x30, 0x30, 0x78, 0x00,
 0x0c, 0x00, 0x0c, 0x0c, 0x0c, 0xcc, 0xcc, 0x78,
 0xe0, 0x60, 0x66, 0x6c, 0x78, 0x6c, 0xe6, 0x00,
 0x70, 0x30, 0x30, 0x30, 0x30, 0x30, 0x78, 0x00,
 0x00, 0x00, 0xcc, 0xfe, 0xfe, 0xd6, 0xc6, 0x00,
 0x00, 0x00, 0xf8, 0xcc, 0xcc, 0xcc, 0xcc, 0x00,
 0x00, 0x00, 0x78, 0xcc, 0xcc, 0xcc, 0x78, 0x00,
 0x00, 0x00, 0xdc, 0x66, 0x66, 0x7c, 0x60, 0xf0,
 0x00, 0x00, 0x76, 0xcc, 0xcc, 0x7c, 0x0c, 0x1e,
 0x00, 0x00, 0xdc, 0x76, 0x66, 0x60, 0xf0, 0x00,
 0x00, 0x00, 0x7c, 0xc0, 0x78, 0x0c, 0xf8, 0x00,
 0x10, 0x30, 0x7c, 0x30, 0x30, 0x34, 0x18, 0x00,
 0x00, 0x00, 0xcc, 0xcc, 0xcc, 0xcc, 0x76, 0x00,
 0x00, 0x00, 0xcc, 0xcc, 0xcc, 0x78, 0x30, 0x00,
 0x00, 0x00, 0xc6, 0xd6, 0xfe, 0xfe, 0x6c, 0x00,
 0x00, 0x00, 0xc6, 0x6c, 0x38, 0x6c, 0xc6, 0x00,
 0x00, 0x00, 0xcc, 0xcc, 0xcc, 0x7c, 0x0c, 0xf8,
 0x00, 0x00, 0xfc, 0x98, 0x30, 0x64, 0xfc, 0x00,
 0x1c, 0x30, 0x30, 0xe0, 0x30, 0x30, 0x1c, 0x00,
 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00,
 0xe0, 0x30, 0x30, 0x1c, 0x30, 0x30, 0xe0, 0x00,
 0x76, 0xdc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
 0x00, 0x10, 0x38, 0x6c, 0xc6, 0xc6, 0xfe, 0x00,
};

ASM_START
.org 0xcc00
// bcc-generated data will be placed here
ASM_END
