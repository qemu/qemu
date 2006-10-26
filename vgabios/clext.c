//
//  QEMU Cirrus CLGD 54xx VGABIOS Extension.
//
//  Copyright (c) 2004 Makoto Suzuki (suzu)
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
//  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
// 

//#define CIRRUS_VESA3_PMINFO
#ifdef VBE
#undef CIRRUS_VESA3_PMINFO
#endif

#define PM_BIOSMEM_CURRENT_MODE 0x449
#define PM_BIOSMEM_CRTC_ADDRESS 0x463
#define PM_BIOSMEM_VBE_MODE 0x4BA

typedef struct
{
  /* + 0 */
  unsigned short mode;
  unsigned short width;
  unsigned short height;
  unsigned short depth;
  /* + 8 */
  unsigned short hidden_dac; /* 0x3c6 */
  unsigned short *seq; /* 0x3c4 */
  unsigned short *graph; /* 0x3ce */
  unsigned short *crtc; /* 0x3d4 */
  /* +16 */
  unsigned char bitsperpixel;
  unsigned char vesacolortype;
  unsigned char vesaredmask;
  unsigned char vesaredpos;
  unsigned char vesagreenmask;
  unsigned char vesagreenpos;
  unsigned char vesabluemask;
  unsigned char vesabluepos;
  /* +24 */
  unsigned char vesareservedmask;
  unsigned char vesareservedpos;
} cirrus_mode_t;
#define CIRRUS_MODE_SIZE 26


/* For VESA BIOS 3.0 */
#define CIRRUS_PM16INFO_SIZE 20

/* VGA */
unsigned short cseq_vga[] = {0x0007,0xffff};
unsigned short cgraph_vga[] = {0x0009,0x000a,0x000b,0xffff};
unsigned short ccrtc_vga[] = {0x001a,0x001b,0x001d,0xffff};

/* extensions */
unsigned short cgraph_svgacolor[] = {
0x0000,0x0001,0x0002,0x0003,0x0004,0x4005,0x0506,0x0f07,0xff08,
0x0009,0x000a,0x000b,
0xffff
};
/* 640x480x8 */
unsigned short cseq_640x480x8[] = {
0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1107,
0x580b,0x580c,0x580d,0x580e,
0x0412,0x0013,0x2017,
0x331b,0x331c,0x331d,0x331e,
0xffff
};
unsigned short ccrtc_640x480x8[] = {
0x2c11,
0x5f00,0x4f01,0x4f02,0x8003,0x5204,0x1e05,0x0b06,0x3e07,
0x4009,0x000c,0x000d,
0xea10,0xdf12,0x5013,0x4014,0xdf15,0x0b16,0xc317,0xff18,
0x001a,0x221b,0x001d,
0xffff
};
/* 640x480x16 */
unsigned short cseq_640x480x16[] = {
0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1707,
0x580b,0x580c,0x580d,0x580e,
0x0412,0x0013,0x2017,
0x331b,0x331c,0x331d,0x331e,
0xffff
};
unsigned short ccrtc_640x480x16[] = {
0x2c11,
0x5f00,0x4f01,0x4f02,0x8003,0x5204,0x1e05,0x0b06,0x3e07,
0x4009,0x000c,0x000d,
0xea10,0xdf12,0xa013,0x4014,0xdf15,0x0b16,0xc317,0xff18,
0x001a,0x221b,0x001d,
0xffff
};
/* 640x480x24 */
unsigned short cseq_640x480x24[] = {
0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1507,
0x580b,0x580c,0x580d,0x580e,
0x0412,0x0013,0x2017,
0x331b,0x331c,0x331d,0x331e,
0xffff
};
unsigned short ccrtc_640x480x24[] = {
0x2c11,
0x5f00,0x4f01,0x4f02,0x8003,0x5204,0x1e05,0x0b06,0x3e07,
0x4009,0x000c,0x000d,
0xea10,0xdf12,0x0013,0x4014,0xdf15,0x0b16,0xc317,0xff18,
0x001a,0x321b,0x001d,
0xffff
};
/* 800x600x8 */
unsigned short cseq_800x600x8[] = {
0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1107,
0x230b,0x230c,0x230d,0x230e,
0x0412,0x0013,0x2017,
0x141b,0x141c,0x141d,0x141e,
0xffff
};
unsigned short ccrtc_800x600x8[] = {
0x2311,0x7d00,0x6301,0x6302,0x8003,0x6b04,0x1a05,0x9806,0xf007,
0x6009,0x000c,0x000d,
0x7d10,0x5712,0x6413,0x4014,0x5715,0x9816,0xc317,0xff18,
0x001a,0x221b,0x001d,
0xffff
};
/* 800x600x16 */
unsigned short cseq_800x600x16[] = {
0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1707,
0x230b,0x230c,0x230d,0x230e,
0x0412,0x0013,0x2017,
0x141b,0x141c,0x141d,0x141e,
0xffff
};
unsigned short ccrtc_800x600x16[] = {
0x2311,0x7d00,0x6301,0x6302,0x8003,0x6b04,0x1a05,0x9806,0xf007,
0x6009,0x000c,0x000d,
0x7d10,0x5712,0xc813,0x4014,0x5715,0x9816,0xc317,0xff18,
0x001a,0x221b,0x001d,
0xffff
};
/* 800x600x24 */
unsigned short cseq_800x600x24[] = {
0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1507,
0x230b,0x230c,0x230d,0x230e,
0x0412,0x0013,0x2017,
0x141b,0x141c,0x141d,0x141e,
0xffff
};
unsigned short ccrtc_800x600x24[] = {
0x2311,0x7d00,0x6301,0x6302,0x8003,0x6b04,0x1a05,0x9806,0xf007,
0x6009,0x000c,0x000d,
0x7d10,0x5712,0x2c13,0x4014,0x5715,0x9816,0xc317,0xff18,
0x001a,0x321b,0x001d,
0xffff
};
/* 1024x768x8 */
unsigned short cseq_1024x768x8[] = {
0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1107,
0x760b,0x760c,0x760d,0x760e,
0x0412,0x0013,0x2017,
0x341b,0x341c,0x341d,0x341e,
0xffff
};
unsigned short ccrtc_1024x768x8[] = {
0x2911,0xa300,0x7f01,0x7f02,0x8603,0x8304,0x9405,0x2406,0xf507,
0x6009,0x000c,0x000d,
0x0310,0xff12,0x8013,0x4014,0xff15,0x2416,0xc317,0xff18,
0x001a,0x221b,0x001d,
0xffff
};
/* 1024x768x16 */
unsigned short cseq_1024x768x16[] = {
0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1707,
0x760b,0x760c,0x760d,0x760e,
0x0412,0x0013,0x2017,
0x341b,0x341c,0x341d,0x341e,
0xffff
};
unsigned short ccrtc_1024x768x16[] = {
0x2911,0xa300,0x7f01,0x7f02,0x8603,0x8304,0x9405,0x2406,0xf507,
0x6009,0x000c,0x000d,
0x0310,0xff12,0x0013,0x4014,0xff15,0x2416,0xc317,0xff18,
0x001a,0x321b,0x001d,
0xffff
};
/* 1024x768x24 */
unsigned short cseq_1024x768x24[] = {
0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1507,
0x760b,0x760c,0x760d,0x760e,
0x0412,0x0013,0x2017,
0x341b,0x341c,0x341d,0x341e,
0xffff
};
unsigned short ccrtc_1024x768x24[] = {
0x2911,0xa300,0x7f01,0x7f02,0x8603,0x8304,0x9405,0x2406,0xf507,
0x6009,0x000c,0x000d,
0x0310,0xff12,0x8013,0x4014,0xff15,0x2416,0xc317,0xff18,
0x001a,0x321b,0x001d,
0xffff
};
/* 1280x1024x8 */
unsigned short cseq_1280x1024x8[] = {
0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1107,
0x760b,0x760c,0x760d,0x760e,
0x0412,0x0013,0x2017,
0x341b,0x341c,0x341d,0x341e,
0xffff
};
unsigned short ccrtc_1280x1024x8[] = {
0x2911,0xc300,0x9f01,0x9f02,0x8603,0x8304,0x9405,0x2406,0xf707,
0x6009,0x000c,0x000d,
0x0310,0xff12,0xa013,0x4014,0xff15,0x2416,0xc317,0xff18,
0x001a,0x221b,0x001d,
0xffff
};
/* 1280x1024x16 */
unsigned short cseq_1280x1024x16[] = {
0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1707,
0x760b,0x760c,0x760d,0x760e,
0x0412,0x0013,0x2017,
0x341b,0x341c,0x341d,0x341e,
0xffff
};
unsigned short ccrtc_1280x1024x16[] = {
0x2911,0xc300,0x9f01,0x9f02,0x8603,0x8304,0x9405,0x2406,0xf707,
0x6009,0x000c,0x000d,
0x0310,0xff12,0x4013,0x4014,0xff15,0x2416,0xc317,0xff18,
0x001a,0x321b,0x001d,
0xffff
};

/* 1600x1200x8 */
unsigned short cseq_1600x1200x8[] = {
0x0300,0x2101,0x0f02,0x0003,0x0e04,0x1107,
0x760b,0x760c,0x760d,0x760e,
0x0412,0x0013,0x2017,
0x341b,0x341c,0x341d,0x341e,
0xffff
};
unsigned short ccrtc_1600x1200x8[] = {
0x2911,0xc300,0x9f01,0x9f02,0x8603,0x8304,0x9405,0x2406,0xf707,
0x6009,0x000c,0x000d,
0x0310,0xff12,0xa013,0x4014,0xff15,0x2416,0xc317,0xff18,
0x001a,0x221b,0x001d,
0xffff
};

cirrus_mode_t cirrus_modes[] =
{
 {0x5f,640,480,8,0x00,
   cseq_640x480x8,cgraph_svgacolor,ccrtc_640x480x8,8,
   4,0,0,0,0,0,0,0,0},
 {0x64,640,480,16,0xe1,
   cseq_640x480x16,cgraph_svgacolor,ccrtc_640x480x16,16,
   6,5,11,6,5,5,0,0,0},
 {0x66,640,480,15,0xf0,
   cseq_640x480x16,cgraph_svgacolor,ccrtc_640x480x16,16,
   6,5,10,5,5,5,0,1,15},
 {0x71,640,480,24,0xe5,
   cseq_640x480x24,cgraph_svgacolor,ccrtc_640x480x24,24,
   6,8,16,8,8,8,0,0,0},

 {0x5c,800,600,8,0x00,
   cseq_800x600x8,cgraph_svgacolor,ccrtc_800x600x8,8,
   4,0,0,0,0,0,0,0,0},
 {0x65,800,600,16,0xe1,
   cseq_800x600x16,cgraph_svgacolor,ccrtc_800x600x16,16,
   6,5,11,6,5,5,0,0,0},
 {0x67,800,600,15,0xf0,
   cseq_800x600x16,cgraph_svgacolor,ccrtc_800x600x16,16,
   6,5,10,5,5,5,0,1,15},

 {0x60,1024,768,8,0x00,
   cseq_1024x768x8,cgraph_svgacolor,ccrtc_1024x768x8,8,
   4,0,0,0,0,0,0,0,0},
 {0x74,1024,768,16,0xe1,
   cseq_1024x768x16,cgraph_svgacolor,ccrtc_1024x768x16,16,
   6,5,11,6,5,5,0,0,0},
 {0x68,1024,768,15,0xf0,
   cseq_1024x768x16,cgraph_svgacolor,ccrtc_1024x768x16,16,
   6,5,10,5,5,5,0,1,15},

 {0x78,800,600,24,0xe5,
   cseq_800x600x24,cgraph_svgacolor,ccrtc_800x600x24,24,
   6,8,16,8,8,8,0,0,0},
 {0x79,1024,768,24,0xe5,
   cseq_1024x768x24,cgraph_svgacolor,ccrtc_1024x768x24,24,
   6,8,16,8,8,8,0,0,0},

 {0x6d,1280,1024,8,0x00,
   cseq_1280x1024x8,cgraph_svgacolor,ccrtc_1280x1024x8,8,
   4,0,0,0,0,0,0,0,0},
 {0x69,1280,1024,15,0xf0,
   cseq_1280x1024x16,cgraph_svgacolor,ccrtc_1280x1024x16,16,
   6,5,10,5,5,5,0,1,15},
 {0x75,1280,1024,16,0xe1,
   cseq_1280x1024x16,cgraph_svgacolor,ccrtc_1280x1024x16,16,
   6,5,11,6,5,5,0,0,0},

 {0x7b,1600,1200,8,0x00,
   cseq_1600x1200x8,cgraph_svgacolor,ccrtc_1600x1200x8,8,
   4,0,0,0,0,0,0,0,0},

 {0xfe,0,0,0,0,cseq_vga,cgraph_vga,ccrtc_vga,0,
   0xff,0,0,0,0,0,0,0,0},
 {0xff,0,0,0,0,0,0,0,0,
   0xff,0,0,0,0,0,0,0,0},
};

unsigned char cirrus_id_table[] = {
  // 5430
  0xA0, 0x32,
  // 5446
  0xB8, 0x39,

  0xff, 0xff
};


unsigned short cirrus_vesa_modelist[] = {
// 640x480x8
  0x101, 0x5f,
// 640x480x15
  0x110, 0x66,
// 640x480x16
  0x111, 0x64,
// 640x480x24
  0x112, 0x71,
// 800x600x8
  0x103, 0x5c,
// 800x600x15
  0x113, 0x67,
// 800x600x16
  0x114, 0x65,
// 800x600x24
  0x115, 0x78,
// 1024x768x8
  0x105, 0x60,
// 1024x768x15
  0x116, 0x68,
// 1024x768x16
  0x117, 0x74,
// 1024x768x24
  0x118, 0x79,
// 1280x1024x8
  0x107, 0x6d,
// 1280x1024x15
  0x119, 0x69,
// 1280x1024x16
  0x11a, 0x75,
// invalid
  0xffff,0xffff
};


ASM_START

cirrus_installed:
.ascii "cirrus-compatible VGA is detected"
.byte 0x0d,0x0a
.byte 0x0d,0x0a,0x00

cirrus_not_installed:
.ascii "cirrus-compatible VGA is not detected"
.byte 0x0d,0x0a
.byte 0x0d,0x0a,0x00

cirrus_vesa_vendorname:
cirrus_vesa_productname:
cirrus_vesa_oemname:
.ascii "VGABIOS Cirrus extension"
.byte 0
cirrus_vesa_productrevision:
.ascii "1.0"
.byte 0

cirrus_init:
  call cirrus_check
  jnz no_cirrus
  SET_INT_VECTOR(0x10, #0xC000, #cirrus_int10_handler)
  mov al, #0x0f ; memory setup
  mov dx, #0x3C4
  out dx, al
  inc dx
  in  al, dx
  and al, #0x18
  mov ah, al
  mov al, #0x0a
  dec dx
  out dx, ax
  mov ax, #0x0007 ; set vga mode
  out dx, ax
  mov ax, #0x0431 ; reset bitblt
  mov dx, #0x3CE
  out dx, ax
  mov ax, #0x0031
  out dx, ax
no_cirrus:
  ret

cirrus_display_info:
  push ds
  push si
  push cs
  pop ds
  call cirrus_check
  mov si, #cirrus_not_installed
  jnz cirrus_msgnotinstalled
  mov si, #cirrus_installed

cirrus_msgnotinstalled:
  call _display_string
  pop si
  pop ds
  ret

cirrus_check:
  push ax
  push dx
  mov ax, #0x9206
  mov dx, #0x3C4
  out dx, ax
  inc dx
  in al, dx
  cmp al, #0x12
  pop dx
  pop ax
  ret


cirrus_int10_handler:
  pushf
  push bp
  cmp ah, #0x00  ;; set video mode
  jz cirrus_set_video_mode
  cmp ah, #0x12  ;; cirrus extension
  jz cirrus_extbios
  cmp ah, #0x4F  ;; VESA extension
  jz cirrus_vesa

cirrus_unhandled:
  pop bp
  popf
  jmp vgabios_int10_handler

cirrus_return:
#ifdef CIRRUS_DEBUG
  call cirrus_debug_dump
#endif
  pop bp
  popf
  iret

cirrus_set_video_mode:
#ifdef CIRRUS_DEBUG
  call cirrus_debug_dump
#endif
  push si
  push ax
  push bx
  push ds
#ifdef CIRRUS_VESA3_PMINFO
 db 0x2e ;; cs:
  mov si, [cirrus_vesa_sel0000_data]
#else
  xor si, si
#endif
  mov ds, si
  xor bx, bx
  mov [PM_BIOSMEM_VBE_MODE], bx
  pop ds
  pop bx
  call cirrus_get_modeentry
  jnc cirrus_set_video_mode_extended
  mov al, #0xfe
  call cirrus_get_modeentry_nomask
  call cirrus_switch_mode
  pop ax
  pop si
  jmp cirrus_unhandled

cirrus_extbios:
#ifdef CIRRUS_DEBUG
  call cirrus_debug_dump
#endif
  cmp bl, #0x80
  jb cirrus_unhandled
  cmp bl, #0xAF
  ja cirrus_unhandled
  push bx
  and bx, #0x7F
  shl bx, 1
 db 0x2e ;; cs:
  mov bp, cirrus_extbios_handlers[bx]
  pop bx
  push #cirrus_return
  push bp
  ret

cirrus_vesa:
#ifdef CIRRUS_DEBUG
  call cirrus_debug_dump
#endif
  cmp al, #0x0F
  ja cirrus_vesa_not_handled
  push bx
  xor bx, bx
  mov bl, al
  shl bx, 1
 db 0x2e ;; cs:
  mov bp, cirrus_vesa_handlers[bx]
  pop bx
  push #cirrus_return
  push bp
  ret

cirrus_vesa_not_handled:
  mov ax, #0x014F ;; not implemented
  jmp cirrus_return

#ifdef CIRRUS_DEBUG
cirrus_debug_dump:
  push es
  push ds
  pusha
  push cs
  pop ds
  call _cirrus_debugmsg
  popa
  pop ds
  pop es
  ret
#endif

cirrus_set_video_mode_extended:
  call cirrus_switch_mode
  pop ax ;; mode
  test al, #0x80
  jnz cirrus_set_video_mode_extended_1
  push ax
  mov ax, #0xffff ; set to 0xff to keep win 2K happy
  call cirrus_clear_vram
  pop ax
cirrus_set_video_mode_extended_1:
  and al, #0x7f

  push ds
#ifdef CIRRUS_VESA3_PMINFO
 db 0x2e ;; cs:
  mov si, [cirrus_vesa_sel0000_data]
#else
  xor si, si
#endif
  mov ds, si
  mov [PM_BIOSMEM_CURRENT_MODE], al
  pop ds

  mov al, #0x20

  pop si
  jmp cirrus_return

cirrus_vesa_pmbios_init:
  retf
cirrus_vesa_pmbios_entry:
  pushf
  push bp
  cmp ah, #0x4F
  jnz cirrus_vesa_pmbios_unimplemented
  cmp al, #0x0F
  ja cirrus_vesa_pmbios_unimplemented
  push bx
  xor bx, bx
  mov bl, al
  shl bx, 1
 db 0x2e ;; cs:
  mov bp, cirrus_vesa_handlers[bx]
  pop bx
  push #cirrus_vesa_pmbios_return
  push bp
  ret
cirrus_vesa_pmbios_unimplemented:
  mov ax, #0x014F
cirrus_vesa_pmbios_return:
  pop bp
  popf
  retf

; in si:mode table
cirrus_switch_mode:
  push ds
  push bx
  push dx
  push cs
  pop ds

  mov bx, [si+10] ;; seq
  mov dx, #0x3c4
  mov ax, #0x1206
  out dx, ax ;; Unlock cirrus special
  call cirrus_switch_mode_setregs

  mov bx, [si+12] ;; graph
  mov dx, #0x3ce
  call cirrus_switch_mode_setregs

  mov bx, [si+14] ;; crtc
  call cirrus_get_crtc
  call cirrus_switch_mode_setregs

  mov dx, #0x3c6
  mov al, #0x00
  out dx, al
  in al, dx
  in al, dx
  in al, dx
  in al, dx
  mov al, [si+8]  ;; hidden dac
  out dx, al
  mov al, #0xff
  out dx, al

  mov al, #0x00
  mov bl, [si+17]  ;; memory model
  or  bl, bl
  jz is_text_mode
  mov al, #0x01
  cmp bl, #0x03
  jnz is_text_mode
  or al, #0x40
is_text_mode:
  mov bl, #0x10
  call biosfn_get_single_palette_reg
  and bh, #0xfe
  or bh, al
  call biosfn_set_single_palette_reg

  pop dx
  pop bx
  pop ds
  ret

cirrus_enable_16k_granularity:
  push ax
  push dx
  mov dx, #0x3ce
  mov al, #0x0b
  out dx, al
  inc dx
  in al, dx
  or al, #0x20 ;; enable 16k
  out dx, al
  pop dx
  pop ax
  ret

cirrus_switch_mode_setregs:
csms_1:
  mov ax, [bx]
  cmp ax, #0xffff
  jz csms_2
  out dx, ax
  add bx, #0x2
  jmp csms_1
csms_2:
  ret

cirrus_extbios_80h:
  push dx
  call cirrus_get_crtc
  mov al, #0x27
  out dx, al
  inc dx
  in al, dx
  mov bx, #_cirrus_id_table
c80h_1:
 db 0x2e ;; cs:
  mov ah, [bx]
  cmp ah, al
  jz c80h_2
  cmp ah, #0xff
  jz c80h_2
  inc bx
  inc bx
  jmp c80h_1
c80h_2:
 db 0x2e ;; cs:
  mov al, 0x1[bx]
  pop dx
  mov ah, #0x00
  xor bx, bx
  ret

cirrus_extbios_81h:
  mov ax, #0x100 ;; XXX
  ret
cirrus_extbios_82h:
  push dx
  call cirrus_get_crtc
  xor ax, ax
  mov al, #0x27
  out dx, al
  inc dx
  in al, dx
  and al, #0x03
  mov ah, #0xAF
  pop dx
  ret

cirrus_extbios_85h:
  push cx
  push dx
  mov dx, #0x3C4
  mov al, #0x0f ;; get DRAM band width
  out dx, al
  inc dx
  in al, dx
  ;; al = 4 << bandwidth
  mov cl, al
  shr cl, #0x03
  and cl, #0x03
  cmp cl, #0x03
  je c85h2
  mov al, #0x04
  shl al, cl
  jmp c85h3
c85h2:
;; 4MB or 2MB
  and al, #0x80
  mov al, #0x20 ;; 2 MB
  je c85h3
  mov al, #0x40 ;; 4 MB
c85h3:
  pop dx
  pop cx
  ret

cirrus_extbios_9Ah:
  mov ax, #0x4060
  mov cx, #0x1132
  ret

cirrus_extbios_A0h:
  call cirrus_get_modeentry
  mov ah, #0x01
  sbb ah, #0x00
  mov bx, cirrus_extbios_A0h_callback
  mov si, #0xffff
  mov di, bx
  mov ds, bx
  mov es, bx
  ret

cirrus_extbios_A0h_callback:
  ;; fatal: not implemented yet
  cli
  hlt
  retf

cirrus_extbios_A1h:
  mov bx, #0x0E00 ;; IBM 8512/8513, color
  ret

cirrus_extbios_A2h:
  mov al, #0x07   ;; HSync 31.5 - 64.0 kHz
  ret

cirrus_extbios_AEh:
  mov al, #0x01   ;; High Refresh 75Hz
  ret

cirrus_extbios_unimplemented:
  ret

cirrus_vesa_00h:
  push ds
  push si
  mov bp, di
  push es
  pop ds
  cld
  mov ax, [di]
  cmp ax, #0x4256 ;; VB
  jnz cv00_1
  mov ax, [di+2]
  cmp ax, #0x3245 ;; E2
  jnz cv00_1
  ;; VBE2
  lea di, 0x14[bp]
  mov ax, #0x0100 ;; soft ver.
  stosw
  mov ax, # cirrus_vesa_vendorname
  stosw
  mov ax, cs
  stosw
  mov ax, # cirrus_vesa_productname
  stosw
  mov ax, cs
  stosw
  mov ax, # cirrus_vesa_productrevision
  stosw
  mov ax, cs
  stosw
cv00_1:
  mov di, bp
  mov ax, #0x4556 ;; VE
  stosw
  mov ax, #0x4153 ;; SA
  stosw
  mov ax, #0x0200 ;; v2.00
  stosw
  mov ax, # cirrus_vesa_oemname
  stosw
  mov ax, cs
  stosw
  xor ax, ax ;; caps
  stosw
  stosw
  lea ax, 0x40[bp]
  stosw
  mov ax, es
  stosw
  call cirrus_extbios_85h ;; vram in 64k
  mov ah, #0x00
  stosw

  push cs
  pop ds
  lea di, 0x40[bp]
  mov si, #_cirrus_vesa_modelist
cv00_2:
  lodsw
  stosw
  add si, #2
  cmp ax, #0xffff
  jnz cv00_2

  mov ax, #0x004F
  mov di, bp
  pop si
  pop ds
  ret

cirrus_vesa_01h:
  mov ax, cx
  and ax, #0x3fff
  call cirrus_vesamode_to_mode
  cmp ax, #0xffff
  jnz cirrus_vesa_01h_1
  jmp cirrus_vesa_unimplemented
cirrus_vesa_01h_1:
  push ds
  push si
  push cx
  push dx
  push bx
  mov bp, di
  cld
  push cs
  pop ds
  call cirrus_get_modeentry_nomask

  push di
  xor ax, ax
  mov cx, #0x80
  rep
    stosw ;; clear buffer
  pop di

  mov ax, #0x003b ;; mode
  stosw
  mov ax, #0x0007 ;; attr
  stosw
  mov ax, #0x0010 ;; granularity =16K
  stosw
  mov ax, #0x0040 ;; size =64K
  stosw
  mov ax, #0xA000 ;; segment A
  stosw
  xor ax, ax ;; no segment B
  stosw
  mov ax, #cirrus_vesa_05h_farentry
  stosw
  mov ax, cs
  stosw
  call cirrus_get_line_offset_entry
  stosw ;; bytes per scan line
  mov ax, [si+2] ;; width
  stosw
  mov ax, [si+4] ;; height
  stosw
  mov ax, #0x08
  stosb
  mov ax, #0x10
  stosb
  mov al, #1 ;; count of planes
  stosb
  mov al, [si+6] ;; bpp
  stosb
  mov al, #0x1 ;; XXX number of banks
  stosb
  mov al, [si+17]
  stosb ;; memory model
  mov al, #0x0   ;; XXX size of bank in K
  stosb
  call cirrus_get_line_offset_entry
  mov bx, [si+4]
  mul bx ;; dx:ax=vramdisp
  or ax, ax
  jz cirrus_vesa_01h_3
  inc dx
cirrus_vesa_01h_3:
  call cirrus_extbios_85h ;; al=vram in 64k
  mov ah, #0x00
  mov cx, dx
  xor dx, dx
  div cx
  dec ax
  stosb  ;; number of image pages = vramtotal/vramdisp-1
  mov al, #0x00
  stosb

  ;; v1.2+ stuffs
  push si
  add si, #18
  movsw
  movsw
  movsw
  movsw
  pop si

  mov ah, [si+16]
  mov al, #0x0
  sub ah, #9
  rcl al, #1 ; bit 0=palette flag
  stosb ;; direct screen mode info

  ;; v2.0+ stuffs
  ;; 32-bit LFB address
  xor ax, ax
  stosw
  call cirrus_get_lfb_addr
  stosw
  or ax, ax
  jz cirrus_vesa_01h_4
  push di
  mov di, bp
 db 0x26 ;; es:
  mov ax, [di]
  or ax, #0x0080 ;; mode bit 7:LFB
  stosw
  pop di
cirrus_vesa_01h_4:

  xor ax, ax
  stosw ; reserved
  stosw ; reserved
  stosw ; reserved

  mov ax, #0x004F
  mov di, bp
  pop bx
  pop dx
  pop cx
  pop si
  pop ds

  test cx, #0x4000 ;; LFB flag
  jz cirrus_vesa_01h_5
  push cx
 db 0x26 ;; es:
  mov cx, [di]
  cmp cx, #0x0080 ;; is LFB supported?
  jnz cirrus_vesa_01h_6
  mov ax, #0x014F ;; error - no LFB
cirrus_vesa_01h_6:
  pop cx
cirrus_vesa_01h_5:
  ret

cirrus_vesa_02h:
  ;; XXX support CRTC registers
  test bx, #0x3e00
  jnz cirrus_vesa_02h_2 ;; unknown flags
  mov ax, bx
  and ax, #0x1ff ;; bit 8-0 mode
  cmp ax, #0x100 ;; legacy VGA mode
  jb cirrus_vesa_02h_legacy
  call cirrus_vesamode_to_mode
  cmp ax, #0xffff
  jnz cirrus_vesa_02h_1
cirrus_vesa_02h_2:
  jmp cirrus_vesa_unimplemented
cirrus_vesa_02h_legacy:
#ifdef CIRRUS_VESA3_PMINFO
 db 0x2e ;; cs:
  cmp byte ptr [cirrus_vesa_is_protected_mode], #0
  jnz cirrus_vesa_02h_2
#endif // CIRRUS_VESA3_PMINFO
  int #0x10
  mov ax, #0x004F
  ret
cirrus_vesa_02h_1:
  push si
  push ax
  call cirrus_get_modeentry_nomask
  call cirrus_switch_mode
  test bx, #0x4000 ;; LFB
  jnz cirrus_vesa_02h_3
  call cirrus_enable_16k_granularity
cirrus_vesa_02h_3:
  test bx, #0x8000 ;; no clear
  jnz cirrus_vesa_02h_4
  push ax
  xor ax,ax
  call cirrus_clear_vram
  pop ax
cirrus_vesa_02h_4:
  pop ax
  push ds
#ifdef CIRRUS_VESA3_PMINFO
 db 0x2e ;; cs:
  mov si, [cirrus_vesa_sel0000_data]
#else
  xor si, si
#endif
  mov ds, si
  mov [PM_BIOSMEM_CURRENT_MODE], al
  mov [PM_BIOSMEM_VBE_MODE], bx
  pop ds
  pop si
  mov ax, #0x004F
  ret

cirrus_vesa_03h:
  push ds
#ifdef CIRRUS_VESA3_PMINFO
 db 0x2e ;; cs:
  mov ax, [cirrus_vesa_sel0000_data]
#else
  xor ax, ax
#endif
  mov  ds, ax
  mov  bx, # PM_BIOSMEM_VBE_MODE
  mov  ax, [bx]
  mov  bx, ax
  test bx, bx
  jnz   cirrus_vesa_03h_1
  mov  bx, # PM_BIOSMEM_CURRENT_MODE
  mov  al, [bx]
  mov  bl, al
  xor  bh, bh
cirrus_vesa_03h_1:
  mov  ax, #0x004f
  pop  ds
  ret

cirrus_vesa_05h_farentry:
  call cirrus_vesa_05h
  retf

cirrus_vesa_05h:
  cmp bl, #0x01
  ja cirrus_vesa_05h_1
  cmp bh, #0x00
  jz cirrus_vesa_05h_setmempage
  cmp bh, #0x01
  jz cirrus_vesa_05h_getmempage
cirrus_vesa_05h_1:
  jmp cirrus_vesa_unimplemented
cirrus_vesa_05h_setmempage:
  or dh, dh ; address must be < 0x100
  jnz cirrus_vesa_05h_1
  push dx
  mov al, bl ;; bl=bank number
  add al, #0x09
  mov ah, dl ;; dx=window address in granularity
  mov dx, #0x3ce
  out dx, ax
  pop dx
  mov ax, #0x004F
  ret
cirrus_vesa_05h_getmempage:
  mov al, bl ;; bl=bank number
  add al, #0x09
  mov dx, #0x3ce
  out dx, al
  inc dx
  in al, dx
  xor dx, dx
  mov dl, al ;; dx=window address in granularity
  mov ax, #0x004F
  ret

cirrus_vesa_06h:
  mov  ax, cx
  cmp  bl, #0x01
  je   cirrus_vesa_06h_3
  cmp  bl, #0x02
  je   cirrus_vesa_06h_2
  jb   cirrus_vesa_06h_1
  mov  ax, #0x0100
  ret
cirrus_vesa_06h_1:
  call cirrus_get_bpp_bytes
  mov  bl, al
  xor  bh, bh
  mov  ax, cx
  mul  bx
cirrus_vesa_06h_2:
  call cirrus_set_line_offset
cirrus_vesa_06h_3:
  call cirrus_get_bpp_bytes
  mov  bl, al
  xor  bh, bh
  xor  dx, dx
  call cirrus_get_line_offset
  push ax
  div  bx
  mov  cx, ax
  pop  bx
  call cirrus_extbios_85h ;; al=vram in 64k
  xor  dx, dx
  mov  dl, al
  xor  ax, ax
  div  bx
  mov  dx, ax
  mov  ax, #0x004f
  ret

cirrus_vesa_07h:
  cmp  bl, #0x80
  je   cirrus_vesa_07h_1
  cmp  bl, #0x01
  je   cirrus_vesa_07h_2
  jb   cirrus_vesa_07h_1
  mov  ax, #0x0100
  ret
cirrus_vesa_07h_1:
  push dx
  call cirrus_get_bpp_bytes
  mov  bl, al
  xor  bh, bh
  mov  ax, cx
  mul  bx
  pop  bx
  push ax
  call cirrus_get_line_offset
  mul  bx
  pop  bx
  add  ax, bx
  jnc  cirrus_vesa_07h_3
  inc  dx
cirrus_vesa_07h_3:
  push dx
  and  dx, #0x0003
  mov  bx, #0x04
  div  bx
  pop  dx
  shr  dx, #2
  call cirrus_set_start_addr
  mov  ax, #0x004f
  ret
cirrus_vesa_07h_2:
  call cirrus_get_start_addr
  shl  dx, #2
  push dx
  mov  bx, #0x04
  mul  bx
  pop  bx
  or   dx, bx
  push ax
  call cirrus_get_line_offset
  mov  bx, ax
  pop  ax
  div  bx
  push ax
  push dx
  call cirrus_get_bpp_bytes
  mov  bl, al
  xor  bh, bh
  pop  ax
  xor  dx, dx
  div  bx
  mov  cx, ax
  pop  dx
  mov  ax, #0x004f
  ret

cirrus_vesa_unimplemented:
  mov ax, #0x014F ;; not implemented
  ret


;; in ax:vesamode, out ax:cirrusmode
cirrus_vesamode_to_mode:
  push ds
  push cx
  push si
  push cs
  pop ds
  mov cx, #0xffff
  mov si, #_cirrus_vesa_modelist
cvtm_1:
  cmp [si],ax
  jz cvtm_2
  cmp [si],cx
  jz cvtm_2
  add si, #4
  jmp cvtm_1
cvtm_2:
  mov ax,[si+2]
  pop si
  pop cx
  pop ds
  ret

  ; cirrus_get_crtc
  ;; NOTE - may be called in protected mode
cirrus_get_crtc:
  push ds
  push ax
  mov  dx, #0x3cc
  in   al, dx
  and  al, #0x01
  shl  al, #5
  mov  dx, #0x3b4
  add  dl, al
  pop  ax
  pop  ds
  ret

;; in - al:mode, out - cflag:result, si:table, ax:destroyed
cirrus_get_modeentry:
  and al, #0x7f
cirrus_get_modeentry_nomask:
  mov si, #_cirrus_modes
cgm_1:
 db 0x2e ;; cs:
  mov ah, [si]
  cmp al, ah
  jz cgm_2
  cmp ah, #0xff
  jz cgm_4
  add si, # CIRRUS_MODE_SIZE
  jmp cgm_1
cgm_4:
  xor si, si
  stc ;; video mode is not supported
  jmp cgm_3
cgm_2:
  clc ;; video mode is supported
cgm_3:
  ret

  ; get LFB address
  ; out - ax:LFB address (high 16 bit)
  ;; NOTE - may be called in protected mode
cirrus_get_lfb_addr:
  push cx
  push dx
  push eax
    xor cx, cx
    mov dl, #0x00
    call cirrus_pci_read
    cmp ax, #0xffff
    jz cirrus_get_lfb_addr_5
 cirrus_get_lfb_addr_3:
    mov dl, #0x00
    call cirrus_pci_read
    cmp ax, #0x1013 ;; cirrus
    jz cirrus_get_lfb_addr_4
    add cx, #0x8
    cmp cx, #0x200 ;; search bus #0 and #1
    jb cirrus_get_lfb_addr_3
 cirrus_get_lfb_addr_5:
    xor dx, dx ;; no LFB
    jmp cirrus_get_lfb_addr_6
 cirrus_get_lfb_addr_4:
    mov dl, #0x10 ;; I/O space #0
    call cirrus_pci_read
    test ax, #0xfff1
    jnz cirrus_get_lfb_addr_5
    shr eax, #16
    mov dx, ax ;; LFB address
 cirrus_get_lfb_addr_6:
  pop eax
  mov ax, dx
  pop dx
  pop cx
  ret

cirrus_pci_read:
  mov eax, #0x00800000
  mov ax, cx
  shl eax, #8
  mov al, dl
  mov dx, #0xcf8
  out dx, eax
  add dl, #4
  in  eax, dx
  ret

;; out - al:bytes per pixel
cirrus_get_bpp_bytes:
  push dx
  mov  dx, #0x03c4
  mov  al, #0x07
  out  dx, al
  inc  dx
  in   al, dx
  and  al, #0x0e
  cmp  al, #0x06
  jne  cirrus_get_bpp_bytes_1
  and  al, #0x02
cirrus_get_bpp_bytes_1:
  shr  al, #1
  cmp  al, #0x04
  je  cirrus_get_bpp_bytes_2
  inc  al
cirrus_get_bpp_bytes_2:
  pop  dx
  ret

;; in - ax: new line offset
cirrus_set_line_offset:
  shr  ax, #3
  push ax
  call cirrus_get_crtc
  mov  al, #0x13
  out  dx, al
  inc  dx
  pop  ax
  out  dx, al
  dec  dx
  mov  al, #0x1b
  out  dx, al
  inc  dx
  shl  ah, #4
  in   al, dx
  and  al, #ef
  or   al, ah
  out  dx, al
  ret

;; out - ax: active line offset
cirrus_get_line_offset:
  push dx
  push bx
  call cirrus_get_crtc
  mov  al, #0x13
  out  dx, al
  inc  dx
  in   al, dx
  mov  bl, al
  dec  dx
  mov  al, #0x1b
  out  dx, al
  inc  dx
  in   al, dx
  mov  ah, al
  shr  ah, #4
  and  ah, #0x01
  mov  al, bl
  shl  ax, #3
  pop  bx
  pop  dx
  ret

;; in - si: table
;; out - ax: line offset for mode
cirrus_get_line_offset_entry:
  push bx
  mov  bx, [si+14] ;; crtc table
  push bx
offset_loop1:
  mov  ax, [bx]
  cmp  al, #0x13
  je   offset_found1
  inc  bx
  inc  bx
  jnz  offset_loop1
offset_found1:
  xor  al, al
  shr  ax, #5
  pop  bx
  push ax
offset_loop2:
  mov  ax, [bx]
  cmp  al, #0x1b
  je offset_found2
  inc  bx
  inc  bx
  jnz offset_loop2
offset_found2:
  pop  bx
  and  ax, #0x1000
  shr  ax, #1
  or   ax, bx
  pop  bx
  ret

;; in - new address in DX:AX
cirrus_set_start_addr:
  push bx
  push dx
  push ax
  call cirrus_get_crtc
  mov  al, #0x0d
  out  dx, al
  inc  dx
  pop  ax
  out  dx, al
  dec  dx
  mov  al, #0x0c
  out  dx, al
  inc  dx
  mov  al, ah
  out  dx, al
  dec  dx
  mov  al, #0x1d
  out  dx, al
  inc  dx
  in   al, dx
  and  al, #0x7f
  pop  bx
  mov  ah, bl
  shl  bl, #4
  and  bl, #0x80
  or   al, bl
  out  dx, al
  dec  dx
  mov  bl, ah
  and  ah, #0x01
  shl  bl, #1
  and  bl, #0x0c
  or   ah, bl
  mov  al, #0x1b
  out  dx, al
  inc  dx
  in   al, dx
  and  al, #0xf2
  or   al, ah
  out  dx, al
  pop  bx
  ret

;; out - current address in DX:AX
cirrus_get_start_addr:
  push bx
  call cirrus_get_crtc
  mov  al, #0x0c
  out  dx, al
  inc  dx
  in   al, dx
  mov  ah, al
  dec  dx
  mov  al, #0x0d
  out  dx, al
  inc  dx
  in   al, dx
  push ax
  dec  dx
  mov  al, #0x1b
  out  dx, al
  inc  dx
  in   al, dx
  dec  dx
  mov  bl, al
  and  al, #0x01
  and  bl, #0x0c
  shr  bl, #1
  or   bl, al
  mov  al, #0x1d
  out  dx, al
  inc  dx
  in   al, dx
  and  al, #0x80
  shr  al, #4
  or   bl, al
  mov  dl, bl
  xor  dh, dh
  pop  ax
  pop  bx
  ret

cirrus_clear_vram:
  pusha
  push es
  mov si, ax

  call cirrus_enable_16k_granularity
  call cirrus_extbios_85h
  shl al, #2
  mov bl, al
  xor ah,ah
cirrus_clear_vram_1:
  mov al, #0x09
  mov dx, #0x3ce
  out dx, ax
  push ax
  mov cx, #0xa000
  mov es, cx
  xor di, di
  mov ax, si
  mov cx, #8192
  cld
  rep 
      stosw
  pop ax
  inc ah
  cmp ah, bl
  jne cirrus_clear_vram_1

  xor ah,ah
  mov dx, #0x3ce
  out dx, ax

  pop es
  popa
  ret

cirrus_extbios_handlers:
  ;; 80h
  dw cirrus_extbios_80h
  dw cirrus_extbios_81h
  dw cirrus_extbios_82h
  dw cirrus_extbios_unimplemented
  ;; 84h
  dw cirrus_extbios_unimplemented
  dw cirrus_extbios_85h
  dw cirrus_extbios_unimplemented
  dw cirrus_extbios_unimplemented
  ;; 88h
  dw cirrus_extbios_unimplemented
  dw cirrus_extbios_unimplemented
  dw cirrus_extbios_unimplemented
  dw cirrus_extbios_unimplemented
  ;; 8Ch
  dw cirrus_extbios_unimplemented
  dw cirrus_extbios_unimplemented
  dw cirrus_extbios_unimplemented
  dw cirrus_extbios_unimplemented
  ;; 90h
  dw cirrus_extbios_unimplemented
  dw cirrus_extbios_unimplemented
  dw cirrus_extbios_unimplemented
  dw cirrus_extbios_unimplemented
  ;; 94h
  dw cirrus_extbios_unimplemented
  dw cirrus_extbios_unimplemented
  dw cirrus_extbios_unimplemented
  dw cirrus_extbios_unimplemented
  ;; 98h
  dw cirrus_extbios_unimplemented
  dw cirrus_extbios_unimplemented
  dw cirrus_extbios_9Ah
  dw cirrus_extbios_unimplemented
  ;; 9Ch
  dw cirrus_extbios_unimplemented
  dw cirrus_extbios_unimplemented
  dw cirrus_extbios_unimplemented
  dw cirrus_extbios_unimplemented
  ;; A0h
  dw cirrus_extbios_A0h
  dw cirrus_extbios_A1h
  dw cirrus_extbios_A2h
  dw cirrus_extbios_unimplemented
  ;; A4h
  dw cirrus_extbios_unimplemented
  dw cirrus_extbios_unimplemented
  dw cirrus_extbios_unimplemented
  dw cirrus_extbios_unimplemented
  ;; A8h
  dw cirrus_extbios_unimplemented
  dw cirrus_extbios_unimplemented
  dw cirrus_extbios_unimplemented
  dw cirrus_extbios_unimplemented
  ;; ACh
  dw cirrus_extbios_unimplemented
  dw cirrus_extbios_unimplemented
  dw cirrus_extbios_AEh
  dw cirrus_extbios_unimplemented

cirrus_vesa_handlers:
  ;; 00h
  dw cirrus_vesa_00h
  dw cirrus_vesa_01h
  dw cirrus_vesa_02h
  dw cirrus_vesa_03h
  ;; 04h
  dw cirrus_vesa_unimplemented
  dw cirrus_vesa_05h
  dw cirrus_vesa_06h
  dw cirrus_vesa_07h
  ;; 08h
  dw cirrus_vesa_unimplemented
  dw cirrus_vesa_unimplemented
  dw cirrus_vesa_unimplemented
  dw cirrus_vesa_unimplemented
  ;; 0Ch
  dw cirrus_vesa_unimplemented
  dw cirrus_vesa_unimplemented
  dw cirrus_vesa_unimplemented
  dw cirrus_vesa_unimplemented



ASM_END

#ifdef CIRRUS_VESA3_PMINFO
ASM_START
cirrus_vesa_pminfo:
  /* + 0 */
  .byte 0x50,0x4d,0x49,0x44 ;; signature[4]
  /* + 4 */
  dw cirrus_vesa_pmbios_entry ;; entry_bios
  dw cirrus_vesa_pmbios_init  ;; entry_init
  /* + 8 */
cirrus_vesa_sel0000_data:
  dw 0x0000 ;; sel_00000
cirrus_vesa_selA000_data:
  dw 0xA000 ;; sel_A0000
  /* +12 */
cirrus_vesa_selB000_data:
  dw 0xB000 ;; sel_B0000
cirrus_vesa_selB800_data:
  dw 0xB800 ;; sel_B8000
  /* +16 */
cirrus_vesa_selC000_data:
  dw 0xC000 ;; sel_C0000
cirrus_vesa_is_protected_mode:
  ;; protected mode flag and checksum
  dw (~((0xf2 + (cirrus_vesa_pmbios_entry >> 8) + (cirrus_vesa_pmbios_entry) \
     + (cirrus_vesa_pmbios_init >> 8) + (cirrus_vesa_pmbios_init)) & 0xff) << 8) + 0x01
ASM_END
#endif // CIRRUS_VESA3_PMINFO


#ifdef CIRRUS_DEBUG
static void cirrus_debugmsg(DI, SI, BP, SP, BX, DX, CX, AX, DS, ES, FLAGS)
  Bit16u DI, SI, BP, SP, BX, DX, CX, AX, ES, DS, FLAGS;
{
 if((GET_AH()!=0x0E)&&(GET_AH()!=0x02)&&(GET_AH()!=0x09)&&(AX!=0x4F05))
  printf("vgabios call ah%02x al%02x bx%04x cx%04x dx%04x\n",GET_AH(),GET_AL(),BX,CX,DX);
}
#endif
