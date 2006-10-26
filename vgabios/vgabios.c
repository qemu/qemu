// ============================================================================================
/*
 * vgabios.c
 */
// ============================================================================================
//  
//  Copyright (C) 2001,2002 the LGPL VGABios developers Team
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
// ============================================================================================
//  
//  This VGA Bios is specific to the plex86/bochs Emulated VGA card. 
//  You can NOT drive any physical vga card with it. 
//     
// ============================================================================================
//  
//  This file contains code ripped from :
//   - rombios.c of plex86 
//
//  This VGA Bios contains fonts from :
//   - fntcol16.zip (c) by Joseph Gil avalable at :
//      ftp://ftp.simtel.net/pub/simtelnet/msdos/screen/fntcol16.zip
//     These fonts are public domain 
//
//  This VGA Bios is based on information taken from :
//   - Kevin Lawton's vga card emulation for bochs/plex86
//   - Ralf Brown's interrupts list available at http://www.cs.cmu.edu/afs/cs/user/ralf/pub/WWW/files.html
//   - Finn Thogersons' VGADOC4b available at http://home.worldonline.dk/~finth/
//   - Michael Abrash's Graphics Programming Black Book
//   - Francois Gervais' book "programmation des cartes graphiques cga-ega-vga" edited by sybex
//   - DOSEMU 1.0.1 source code for several tables values and formulas
//
// Thanks for patches, comments and ideas to :
//   - techt@pikeonline.net
//
// ============================================================================================

#include "vgabios.h"

#ifdef VBE
#include "vbe.h"
#endif

#define USE_BX_INFO

/* Declares */
static Bit8u          read_byte();
static Bit16u         read_word();
static void           write_byte();
static void           write_word();
static Bit8u          inb();
static Bit16u         inw();
static void           outb();
static void           outw();

static Bit16u         get_SS();

// Output
static void           printf();
static void           unimplemented();
static void           unknown();

static Bit8u find_vga_entry();

static void memsetb();
static void memsetw();
static void memcpyb();
static void memcpyw();

static void biosfn_set_video_mode();
static void biosfn_set_cursor_shape();
static void biosfn_set_cursor_pos();
static void biosfn_get_cursor_pos();
static void biosfn_set_active_page();
static void biosfn_scroll();
static void biosfn_read_char_attr();
static void biosfn_write_char_attr();
static void biosfn_write_char_only();
static void biosfn_write_pixel();
static void biosfn_read_pixel();
static void biosfn_write_teletype();
static void biosfn_perform_gray_scale_summing();
static void biosfn_load_text_user_pat();
static void biosfn_load_text_8_14_pat();
static void biosfn_load_text_8_8_pat();
static void biosfn_load_text_8_16_pat();
static void biosfn_load_gfx_8_8_chars();
static void biosfn_load_gfx_user_chars();
static void biosfn_load_gfx_8_14_chars();
static void biosfn_load_gfx_8_8_dd_chars();
static void biosfn_load_gfx_8_16_chars();
static void biosfn_get_font_info();
static void biosfn_alternate_prtsc();
static void biosfn_switch_video_interface();
static void biosfn_enable_video_refresh_control();
static void biosfn_write_string();
static void biosfn_read_state_info();
static void biosfn_read_video_state_size();
static Bit16u biosfn_save_video_state();
static Bit16u biosfn_restore_video_state();
extern Bit8u video_save_pointer_table[];

// This is for compiling with gcc2 and gcc3
#define ASM_START #asm
#define ASM_END   #endasm

ASM_START

MACRO SET_INT_VECTOR
  push ds
  xor ax, ax
  mov ds, ax
  mov ax, ?3
  mov ?1*4, ax
  mov ax, ?2
  mov ?1*4+2, ax
  pop ds
MEND

ASM_END

ASM_START
.text
.rom
.org 0

use16 386

vgabios_start:
.byte	0x55, 0xaa	/* BIOS signature, required for BIOS extensions */

.byte	0x40		/* BIOS extension length in units of 512 bytes */


vgabios_entry_point:
           
  jmp vgabios_init_func

vgabios_name:
.ascii	"Plex86/Bochs VGABios"
.ascii	" "
.byte	0x00

// Info from Bart Oldeman
.org 0x1e
.ascii  "IBM"
.byte   0x00

vgabios_version:
#ifndef VGABIOS_VERS
.ascii	"current-cvs"
#else
.ascii VGABIOS_VERS
#endif
.ascii	" "

vgabios_date:
.ascii  VGABIOS_DATE
.byte   0x0a,0x0d
.byte	0x00

vgabios_copyright:
.ascii	"(C) 2003 the LGPL VGABios developers Team"
.byte	0x0a,0x0d
.byte	0x00

vgabios_license:
.ascii	"This VGA/VBE Bios is released under the GNU LGPL"
.byte	0x0a,0x0d
.byte	0x0a,0x0d
.byte	0x00

vgabios_website:
.ascii	"Please visit :"
.byte	0x0a,0x0d
;;.ascii  " . http://www.plex86.org"
;;.byte	0x0a,0x0d
.ascii	" . http://bochs.sourceforge.net"
.byte	0x0a,0x0d
.ascii	" . http://www.nongnu.org/vgabios"
.byte	0x0a,0x0d
.byte	0x0a,0x0d
.byte	0x00
 

;; ============================================================================================
;;
;; Init Entry point
;;
;; ============================================================================================
vgabios_init_func:

;; init vga card
  call init_vga_card

;; init basic bios vars
  call init_bios_area

#ifdef VBE  
;; init vbe functions
  call vbe_init  
#endif

;; set int10 vect
  SET_INT_VECTOR(0x10, #0xC000, #vgabios_int10_handler)

#ifdef CIRRUS
  call cirrus_init
#endif

;; display splash screen
  call _display_splash_screen

;; init video mode and clear the screen
  mov ax,#0x0003
  int #0x10

;; show info
  call _display_info

#ifdef VBE  
;; show vbe info
  call vbe_display_info  
#endif

#ifdef CIRRUS
;; show cirrus info
  call cirrus_display_info
#endif

  retf
ASM_END

/*
 *  int10 handled here
 */
ASM_START
vgabios_int10_handler:
  pushf
#ifdef DEBUG
  push es
  push ds
  pusha
  mov   bx, #0xc000
  mov   ds, bx
  call _int10_debugmsg
  popa
  pop ds
  pop es
#endif
  cmp   ah, #0x0f
  jne   int10_test_1A
  call  biosfn_get_video_mode
  jmp   int10_end
int10_test_1A:
  cmp   ah, #0x1a
  jne   int10_test_0B
  call  biosfn_group_1A
  jmp   int10_end
int10_test_0B:
  cmp   ah, #0x0b
  jne   int10_test_1103
  call  biosfn_group_0B
  jmp   int10_end
int10_test_1103:
  cmp   ax, #0x1103
  jne   int10_test_12
  call  biosfn_set_text_block_specifier
  jmp   int10_end
int10_test_12:
  cmp   ah, #0x12
  jne   int10_test_101B
  cmp   bl, #0x10
  jne   int10_test_BL30
  call  biosfn_get_ega_info
  jmp   int10_end
int10_test_BL30:
  cmp   bl, #0x30
  jne   int10_test_BL31
  call  biosfn_select_vert_res
  jmp   int10_end
int10_test_BL31:
  cmp   bl, #0x31
  jne   int10_test_BL32
  call  biosfn_enable_default_palette_loading
  jmp   int10_end
int10_test_BL32:
  cmp   bl, #0x32
  jne   int10_test_BL33
  call  biosfn_enable_video_addressing
  jmp   int10_end
int10_test_BL33:
  cmp   bl, #0x33
  jne   int10_test_BL34
  call  biosfn_enable_grayscale_summing
  jmp   int10_end
int10_test_BL34:
  cmp   bl, #0x34
  jne   int10_normal
  call  biosfn_enable_cursor_emulation
  jmp   int10_end
int10_test_101B:
  cmp   ax, #0x101b
  je    int10_normal
  cmp   ah, #0x10
#ifndef VBE
  jne   int10_normal
#else
  jne   int10_test_4F
#endif
  call  biosfn_group_10
  jmp   int10_end
#ifdef VBE
int10_test_4F:
  cmp   ah, #0x4f
  jne   int10_normal
  cmp   al, #0x03
  jne   int10_test_vbe_05
  call  vbe_biosfn_return_current_mode
  jmp   int10_end
int10_test_vbe_05:
  cmp   al, #0x05
  jne   int10_test_vbe_06
  call  vbe_biosfn_display_window_control
  jmp   int10_end
int10_test_vbe_06:
  cmp   al, #0x06
  jne   int10_test_vbe_07
  call  vbe_biosfn_set_get_logical_scan_line_length
  jmp   int10_end
int10_test_vbe_07:
  cmp   al, #0x07
  jne   int10_test_vbe_08
  call  vbe_biosfn_set_get_display_start
  jmp   int10_end
int10_test_vbe_08:
  cmp   al, #0x08
  jne   int10_test_vbe_0A
  call  vbe_biosfn_set_get_dac_palette_format
  jmp   int10_end
int10_test_vbe_0A:
  cmp   al, #0x0A
  jne   int10_normal
  call  vbe_biosfn_return_protected_mode_interface
  jmp   int10_end
#endif

int10_normal:
  push es
  push ds
  pusha

;; We have to set ds to access the right data segment
  mov   bx, #0xc000
  mov   ds, bx
  call _int10_func

  popa
  pop ds
  pop es
int10_end:
  popf
  iret
ASM_END

#include "vgatables.h"
#include "vgafonts.h"

/*
 * Boot time harware inits 
 */
ASM_START
init_vga_card:
;; switch to color mode and enable CPU access 480 lines
  mov dx, #0x3C2
  mov al, #0xC3
  outb dx,al

;; more than 64k 3C4/04
  mov dx, #0x3C4
  mov al, #0x04
  outb dx,al
  mov dx, #0x3C5
  mov al, #0x02
  outb dx,al

#if defined(USE_BX_INFO) || defined(DEBUG)
  mov  bx, #msg_vga_init
  push bx
  call _printf
#endif
  inc  sp
  inc  sp
  ret

#if defined(USE_BX_INFO) || defined(DEBUG)
msg_vga_init:
.ascii "VGABios $Id: vgabios.c,v 1.66 2006/07/10 07:47:51 vruppert Exp $"
.byte 0x0d,0x0a,0x00
#endif
ASM_END

// --------------------------------------------------------------------------------------------
/*
 *  Boot time bios area inits 
 */
ASM_START
init_bios_area:
  push  ds
  mov   ax, # BIOSMEM_SEG
  mov   ds, ax

;; init detected hardware BIOS Area
  mov   bx, # BIOSMEM_INITIAL_MODE
  mov   ax, [bx]
  and   ax, #0xffcf
;; set 80x25 color (not clear from RBIL but usual)
  or    ax, #0x0020
  mov   [bx], ax

;; Just for the first int10 find its children

;; the default char height
  mov   bx, # BIOSMEM_CHAR_HEIGHT
  mov   al, #0x10
  mov   [bx], al

;; Clear the screen 
  mov   bx, # BIOSMEM_VIDEO_CTL
  mov   al, #0x60
  mov   [bx], al

;; Set the basic screen we have
  mov   bx, # BIOSMEM_SWITCHES
  mov   al, #0xf9
  mov   [bx], al

;; Set the basic modeset options
  mov   bx, # BIOSMEM_MODESET_CTL
  mov   al, #0x51
  mov   [bx], al

;; Set the  default MSR
  mov   bx, # BIOSMEM_CURRENT_MSR
  mov   al, #0x09
  mov   [bx], al

  pop ds
  ret

_video_save_pointer_table:
  .word _video_param_table
  .word 0xc000

  .word 0 /* XXX: fill it */
  .word 0

  .word 0 /* XXX: fill it */
  .word 0

  .word 0 /* XXX: fill it */
  .word 0

  .word 0 /* XXX: fill it */
  .word 0

  .word 0 /* XXX: fill it */
  .word 0

  .word 0 /* XXX: fill it */
  .word 0

ASM_END

// --------------------------------------------------------------------------------------------
/*
 *  Boot time Splash screen
 */
static void display_splash_screen()
{
}

// --------------------------------------------------------------------------------------------
/*
 *  Tell who we are
 */

static void display_info()
{
ASM_START
 mov ax,#0xc000
 mov ds,ax
 mov si,#vgabios_name
 call _display_string
 mov si,#vgabios_version
 call _display_string
 
 ;;mov si,#vgabios_copyright
 ;;call _display_string
 ;;mov si,#crlf
 ;;call _display_string

 mov si,#vgabios_license
 call _display_string
 mov si,#vgabios_website
 call _display_string
ASM_END
}

static void display_string()
{
 // Get length of string
ASM_START
 mov ax,ds
 mov es,ax
 mov di,si
 xor cx,cx
 not cx
 xor al,al
 cld
 repne 
  scasb
 not cx
 dec cx
 push cx

 mov ax,#0x0300
 mov bx,#0x0000
 int #0x10
 
 pop cx
 mov ax,#0x1301
 mov bx,#0x000b
 mov bp,si
 int #0x10
ASM_END
}

// --------------------------------------------------------------------------------------------
#ifdef DEBUG
static void int10_debugmsg(DI, SI, BP, SP, BX, DX, CX, AX, DS, ES, FLAGS)
  Bit16u DI, SI, BP, SP, BX, DX, CX, AX, ES, DS, FLAGS;
{
 // 0E is write char...
 if(GET_AH()!=0x0E)
  printf("vgabios call ah%02x al%02x bx%04x cx%04x dx%04x\n",GET_AH(),GET_AL(),BX,CX,DX);
}
#endif

// --------------------------------------------------------------------------------------------
/*
 * int10 main dispatcher
 */
static void int10_func(DI, SI, BP, SP, BX, DX, CX, AX, DS, ES, FLAGS)
  Bit16u DI, SI, BP, SP, BX, DX, CX, AX, ES, DS, FLAGS;
{

 // BIOS functions
 switch(GET_AH())
  {
   case 0x00:
     biosfn_set_video_mode(GET_AL());
     switch(GET_AL()&0x7F)
      {case 6: 
        SET_AL(0x3F);
        break;
       case 0:
       case 1:
       case 2:
       case 3:
       case 4:
       case 5:
       case 7:
        SET_AL(0x30);
        break;
      default:
        SET_AL(0x20);
      }
     break;
   case 0x01:
     biosfn_set_cursor_shape(GET_CH(),GET_CL());
     break;
   case 0x02:
     biosfn_set_cursor_pos(GET_BH(),DX);
     break;
   case 0x03:
     biosfn_get_cursor_pos(GET_BH(),&CX,&DX);
     break;
   case 0x04:
     // Read light pen pos (unimplemented)
#ifdef DEBUG
     unimplemented();
#endif
     AX=0x00;
     BX=0x00;
     CX=0x00;
     DX=0x00;
     break;
   case 0x05:
     biosfn_set_active_page(GET_AL());
     break;
   case 0x06:
     biosfn_scroll(GET_AL(),GET_BH(),GET_CH(),GET_CL(),GET_DH(),GET_DL(),0xFF,SCROLL_UP);
     break;
   case 0x07:
     biosfn_scroll(GET_AL(),GET_BH(),GET_CH(),GET_CL(),GET_DH(),GET_DL(),0xFF,SCROLL_DOWN);
     break;
   case 0x08:
     biosfn_read_char_attr(GET_BH(),&AX);
     break;
   case 0x09:
     biosfn_write_char_attr(GET_AL(),GET_BH(),GET_BL(),CX);
     break;
   case 0x0A:
     biosfn_write_char_only(GET_AL(),GET_BH(),GET_BL(),CX);
     break;
   case 0x0C:
     biosfn_write_pixel(GET_BH(),GET_AL(),CX,DX);
     break;
   case 0x0D:
     biosfn_read_pixel(GET_BH(),CX,DX,&AX);
     break;
   case 0x0E:
     // Ralf Brown Interrupt list is WRONG on bh(page)
     // We do output only on the current page !
     biosfn_write_teletype(GET_AL(),0xff,GET_BL(),NO_ATTR);
     break;
   case 0x10:
     // All other functions of group AH=0x10 rewritten in assembler
     biosfn_perform_gray_scale_summing(BX,CX);
     break;
   case 0x11:
     switch(GET_AL())
      {
       case 0x00:
       case 0x10:
        biosfn_load_text_user_pat(GET_AL(),ES,BP,CX,DX,GET_BL(),GET_BH());
        break;
       case 0x01:
       case 0x11:
        biosfn_load_text_8_14_pat(GET_AL(),GET_BL());
        break;
       case 0x02:
       case 0x12:
        biosfn_load_text_8_8_pat(GET_AL(),GET_BL());
        break;
       case 0x04:
       case 0x14:
        biosfn_load_text_8_16_pat(GET_AL(),GET_BL());
        break;
       case 0x20:
        biosfn_load_gfx_8_8_chars(ES,BP);
        break;
       case 0x21:
        biosfn_load_gfx_user_chars(ES,BP,CX,GET_BL(),GET_DL());
        break;
       case 0x22:
        biosfn_load_gfx_8_14_chars(GET_BL());
        break;
       case 0x23:
        biosfn_load_gfx_8_8_dd_chars(GET_BL());
        break;
       case 0x24:
        biosfn_load_gfx_8_16_chars(GET_BL());
        break;
       case 0x30:
        biosfn_get_font_info(GET_BH(),&ES,&BP,&CX,&DX);
        break;
#ifdef DEBUG
       default:
        unknown();
#endif
      }
     
     break;
   case 0x12:
     switch(GET_BL())
      {
       case 0x20:
        biosfn_alternate_prtsc();
        break;
       case 0x35:
        biosfn_switch_video_interface(GET_AL(),ES,DX);
        SET_AL(0x12);
        break;
       case 0x36:
        biosfn_enable_video_refresh_control(GET_AL());
        SET_AL(0x12);
        break;
#ifdef DEBUG
       default:
        unknown();
#endif
      }
     break;
   case 0x13:
     biosfn_write_string(GET_AL(),GET_BH(),GET_BL(),CX,GET_DH(),GET_DL(),ES,BP);
     break;
   case 0x1B:
     biosfn_read_state_info(BX,ES,DI);
     SET_AL(0x1B);
     break;
   case 0x1C:
     switch(GET_AL())
      {
       case 0x00:
        biosfn_read_video_state_size(CX,&BX);
        break;
       case 0x01:
        biosfn_save_video_state(CX,ES,BX);
        break;
       case 0x02:
        biosfn_restore_video_state(CX,ES,BX);
        break;
#ifdef DEBUG
       default:
        unknown();
#endif
      }
     SET_AL(0x1C);
     break;

#ifdef VBE 
   case 0x4f:
     if (vbe_has_vbe_display()) {
       switch(GET_AL())
       {
         case 0x00:
          vbe_biosfn_return_controller_information(&AX,ES,DI);
          break;
         case 0x01:
          vbe_biosfn_return_mode_information(&AX,CX,ES,DI);
          break;
         case 0x02:
          vbe_biosfn_set_mode(&AX,BX,ES,DI);
          break;
         case 0x04:
          vbe_biosfn_save_restore_state(&AX, CX, DX, ES, &BX);
          break;
         case 0x09:
          //FIXME
#ifdef DEBUG
          unimplemented();
#endif
          // function failed
          AX=0x100;
          break;
         case 0x0A:
          //FIXME
#ifdef DEBUG
          unimplemented();
#endif
          // function failed
          AX=0x100;
          break;
         default:
#ifdef DEBUG
          unknown();
#endif   		 
          // function failed
          AX=0x100;
          }
        }
        else {
          // No VBE display
          AX=0x0100;
          }
        break;
#endif

#ifdef DEBUG
   default:
     unknown();
#endif
  }
}

// ============================================================================================
// 
// BIOS functions
// 
// ============================================================================================

static void biosfn_set_video_mode(mode) Bit8u mode; 
{// mode: Bit 7 is 1 if no clear screen

 // Should we clear the screen ?
 Bit8u noclearmem=mode&0x80;
 Bit8u line,mmask,*palette,vpti;
 Bit16u i,twidth,theightm1,cheight;
 Bit8u modeset_ctl,video_ctl,vga_switches;
 Bit16u crtc_addr;
 
#ifdef VBE
 if (vbe_has_vbe_display()) { 
   dispi_set_enable(VBE_DISPI_DISABLED);
  }
#endif // def VBE
 
 // The real mode
 mode=mode&0x7f;

 // find the entry in the video modes
 line=find_vga_entry(mode);

#ifdef DEBUG
 printf("mode search %02x found line %02x\n",mode,line);
#endif

 if(line==0xFF)
  return;

 vpti=line_to_vpti[line];
 twidth=video_param_table[vpti].twidth;
 theightm1=video_param_table[vpti].theightm1;
 cheight=video_param_table[vpti].cheight;
 
 // Read the bios vga control
 video_ctl=read_byte(BIOSMEM_SEG,BIOSMEM_VIDEO_CTL);

 // Read the bios vga switches
 vga_switches=read_byte(BIOSMEM_SEG,BIOSMEM_SWITCHES);

 // Read the bios mode set control
 modeset_ctl=read_byte(BIOSMEM_SEG,BIOSMEM_MODESET_CTL);

 // Then we know the number of lines
// FIXME

 // if palette loading (bit 3 of modeset ctl = 0)
 if((modeset_ctl&0x08)==0)
  {// Set the PEL mask
   outb(VGAREG_PEL_MASK,vga_modes[line].pelmask);

   // Set the whole dac always, from 0
   outb(VGAREG_DAC_WRITE_ADDRESS,0x00);

   // From which palette
   switch(vga_modes[line].dacmodel)
    {case 0:
      palette=&palette0;
      break;
     case 1:
      palette=&palette1;
      break;
     case 2:
      palette=&palette2;
      break;
     case 3:
      palette=&palette3;
      break;
    }
   // Always 256*3 values
   for(i=0;i<0x0100;i++)
    {if(i<=dac_regs[vga_modes[line].dacmodel])
      {outb(VGAREG_DAC_DATA,palette[(i*3)+0]);
       outb(VGAREG_DAC_DATA,palette[(i*3)+1]);
       outb(VGAREG_DAC_DATA,palette[(i*3)+2]);
      }
     else
      {outb(VGAREG_DAC_DATA,0);
       outb(VGAREG_DAC_DATA,0);
       outb(VGAREG_DAC_DATA,0);
      }
    }
   if((modeset_ctl&0x02)==0x02)
    {
     biosfn_perform_gray_scale_summing(0x00, 0x100);
    }
  }

 // Reset Attribute Ctl flip-flop
 inb(VGAREG_ACTL_RESET);

 // Set Attribute Ctl
 for(i=0;i<=0x13;i++)
  {outb(VGAREG_ACTL_ADDRESS,i);
   outb(VGAREG_ACTL_WRITE_DATA,video_param_table[vpti].actl_regs[i]);
  }
 outb(VGAREG_ACTL_ADDRESS,0x14);
 outb(VGAREG_ACTL_WRITE_DATA,0x00);

 // Set Sequencer Ctl
 outb(VGAREG_SEQU_ADDRESS,0);
 outb(VGAREG_SEQU_DATA,0x03);
 for(i=1;i<=4;i++)
  {outb(VGAREG_SEQU_ADDRESS,i);
   outb(VGAREG_SEQU_DATA,video_param_table[vpti].sequ_regs[i - 1]);
  }

 // Set Grafx Ctl
 for(i=0;i<=8;i++)
  {outb(VGAREG_GRDC_ADDRESS,i);
   outb(VGAREG_GRDC_DATA,video_param_table[vpti].grdc_regs[i]);
  }

 // Set CRTC address VGA or MDA 
 crtc_addr=vga_modes[line].memmodel==MTEXT?VGAREG_MDA_CRTC_ADDRESS:VGAREG_VGA_CRTC_ADDRESS;

 // Disable CRTC write protection
 outw(crtc_addr,0x0011);
 // Set CRTC regs
 for(i=0;i<=0x18;i++)
  {outb(crtc_addr,i);
   outb(crtc_addr+1,video_param_table[vpti].crtc_regs[i]);
  }

 // Set the misc register
 outb(VGAREG_WRITE_MISC_OUTPUT,video_param_table[vpti].miscreg);

 // Enable video
 outb(VGAREG_ACTL_ADDRESS,0x20);
 inb(VGAREG_ACTL_RESET);

 if(noclearmem==0x00)
  {
   if(vga_modes[line].class==TEXT)
    {
     memsetw(vga_modes[line].sstart,0,0x0720,0x4000); // 32k
    }
   else
    {
     if(mode<0x0d)
      {
       memsetw(vga_modes[line].sstart,0,0x0000,0x4000); // 32k
      }
     else
      {
       outb( VGAREG_SEQU_ADDRESS, 0x02 );
       mmask = inb( VGAREG_SEQU_DATA );
       outb( VGAREG_SEQU_DATA, 0x0f ); // all planes
       memsetw(vga_modes[line].sstart,0,0x0000,0x8000); // 64k
       outb( VGAREG_SEQU_DATA, mmask );
      }
    }
  }

 // Set the BIOS mem
 write_byte(BIOSMEM_SEG,BIOSMEM_CURRENT_MODE,mode);
 write_word(BIOSMEM_SEG,BIOSMEM_NB_COLS,twidth);
 write_word(BIOSMEM_SEG,BIOSMEM_PAGE_SIZE,*(Bit16u *)&video_param_table[vpti].slength_l);
 write_word(BIOSMEM_SEG,BIOSMEM_CRTC_ADDRESS,crtc_addr);
 write_byte(BIOSMEM_SEG,BIOSMEM_NB_ROWS,theightm1);
 write_word(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT,cheight);
 write_byte(BIOSMEM_SEG,BIOSMEM_VIDEO_CTL,(0x60|noclearmem));
 write_byte(BIOSMEM_SEG,BIOSMEM_SWITCHES,0xF9);
 write_byte(BIOSMEM_SEG,BIOSMEM_MODESET_CTL,read_byte(BIOSMEM_SEG,BIOSMEM_MODESET_CTL)&0x7f);

 // FIXME We nearly have the good tables. to be reworked
 write_byte(BIOSMEM_SEG,BIOSMEM_DCC_INDEX,0x08);    // 8 is VGA should be ok for now
 write_word(BIOSMEM_SEG,BIOSMEM_VS_POINTER, video_save_pointer_table);
 write_word(BIOSMEM_SEG,BIOSMEM_VS_POINTER+2, 0xc000);

 // FIXME
 write_byte(BIOSMEM_SEG,BIOSMEM_CURRENT_MSR,0x00); // Unavailable on vanilla vga, but...
 write_byte(BIOSMEM_SEG,BIOSMEM_CURRENT_PAL,0x00); // Unavailable on vanilla vga, but...
 
 // Set cursor shape
 if(vga_modes[line].class==TEXT)
  {
   biosfn_set_cursor_shape(0x06,0x07);
  }

 // Set cursor pos for page 0..7
 for(i=0;i<8;i++)
  biosfn_set_cursor_pos(i,0x0000);

 // Set active page 0
 biosfn_set_active_page(0x00);

 // Write the fonts in memory
 if(vga_modes[line].class==TEXT)
  { 
ASM_START
  ;; copy and activate 8x16 font
  mov ax, #0x1104
  mov bl, #0x00
  int #0x10
  mov ax, #0x1103
  mov bl, #0x00
  int #0x10
ASM_END
  }

 // Set the ints 0x1F and 0x43
ASM_START
 SET_INT_VECTOR(0x1f, #0xC000, #_vgafont8+128*8)
ASM_END

  switch(cheight)
   {case 8:
ASM_START
     SET_INT_VECTOR(0x43, #0xC000, #_vgafont8)
ASM_END
     break;
    case 14:
ASM_START
     SET_INT_VECTOR(0x43, #0xC000, #_vgafont14)
ASM_END
     break;
    case 16:
ASM_START
     SET_INT_VECTOR(0x43, #0xC000, #_vgafont16)
ASM_END
     break;
   }
}

// --------------------------------------------------------------------------------------------
static void biosfn_set_cursor_shape (CH,CL) 
Bit8u CH;Bit8u CL; 
{Bit16u cheight,curs,crtc_addr;
 Bit8u modeset_ctl;

 CH&=0x3f;
 CL&=0x1f;

 curs=(CH<<8)+CL;
 write_word(BIOSMEM_SEG,BIOSMEM_CURSOR_TYPE,curs);

 modeset_ctl=read_byte(BIOSMEM_SEG,BIOSMEM_MODESET_CTL);
 cheight = read_word(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT);
 if((modeset_ctl&0x01) && (cheight>8) && (CL<8) && (CH<0x20))
  {
   if(CL!=(CH+1))
    {
     CH = ((CH+1) * cheight / 8) -1;
    }
   else
    {
     CH = ((CL+1) * cheight / 8) - 2;
    }
   CL = ((CL+1) * cheight / 8) - 1;
  }

 // CTRC regs 0x0a and 0x0b
 crtc_addr=read_word(BIOSMEM_SEG,BIOSMEM_CRTC_ADDRESS);
 outb(crtc_addr,0x0a);
 outb(crtc_addr+1,CH);
 outb(crtc_addr,0x0b);
 outb(crtc_addr+1,CL);
}

// --------------------------------------------------------------------------------------------
static void biosfn_set_cursor_pos (page, cursor) 
Bit8u page;Bit16u cursor;
{
 Bit8u xcurs,ycurs,current;
 Bit16u nbcols,nbrows,address,crtc_addr;

 // Should not happen...
 if(page>7)return;

 // Bios cursor pos
 write_word(BIOSMEM_SEG, BIOSMEM_CURSOR_POS+2*page, cursor);

 // Set the hardware cursor
 current=read_byte(BIOSMEM_SEG,BIOSMEM_CURRENT_PAGE);
 if(page==current)
  {
   // Get the dimensions
   nbcols=read_word(BIOSMEM_SEG,BIOSMEM_NB_COLS);
   nbrows=read_byte(BIOSMEM_SEG,BIOSMEM_NB_ROWS)+1;

   xcurs=cursor&0x00ff;ycurs=(cursor&0xff00)>>8;
 
   // Calculate the address knowing nbcols nbrows and page num
   address=SCREEN_IO_START(nbcols,nbrows,page)+xcurs+ycurs*nbcols;
   
   // CRTC regs 0x0e and 0x0f
   crtc_addr=read_word(BIOSMEM_SEG,BIOSMEM_CRTC_ADDRESS);
   outb(crtc_addr,0x0e);
   outb(crtc_addr+1,(address&0xff00)>>8);
   outb(crtc_addr,0x0f);
   outb(crtc_addr+1,address&0x00ff);
  }
}

// --------------------------------------------------------------------------------------------
static void biosfn_get_cursor_pos (page,shape, pos) 
Bit8u page;Bit16u *shape;Bit16u *pos;
{
 Bit16u ss=get_SS();

 // Default
 write_word(ss, shape, 0);
 write_word(ss, pos, 0);

 if(page>7)return;
 // FIXME should handle VGA 14/16 lines
 write_word(ss,shape,read_word(BIOSMEM_SEG,BIOSMEM_CURSOR_TYPE));
 write_word(ss,pos,read_word(BIOSMEM_SEG,BIOSMEM_CURSOR_POS+page*2));
}

// --------------------------------------------------------------------------------------------
static void biosfn_set_active_page (page) 
Bit8u page;
{
 Bit16u cursor,dummy,crtc_addr;
 Bit16u nbcols,nbrows,address;
 Bit8u mode,line;

 if(page>7)return;

 // Get the mode
 mode=read_byte(BIOSMEM_SEG,BIOSMEM_CURRENT_MODE);
 line=find_vga_entry(mode);
 if(line==0xFF)return;

 // Get pos curs pos for the right page 
 biosfn_get_cursor_pos(page,&dummy,&cursor);

 if(vga_modes[line].class==TEXT)
  {
   // Get the dimensions
   nbcols=read_word(BIOSMEM_SEG,BIOSMEM_NB_COLS);
   nbrows=read_byte(BIOSMEM_SEG,BIOSMEM_NB_ROWS)+1;
 
   // Calculate the address knowing nbcols nbrows and page num
   address=SCREEN_MEM_START(nbcols,nbrows,page);
   write_word(BIOSMEM_SEG,BIOSMEM_CURRENT_START,address);

   // Start address
   address=SCREEN_IO_START(nbcols,nbrows,page);
  }
 else
  {
   address = page * (*(Bit16u *)&video_param_table[line_to_vpti[line]].slength_l);
  }

 // CRTC regs 0x0c and 0x0d
 crtc_addr=read_word(BIOSMEM_SEG,BIOSMEM_CRTC_ADDRESS);
 outb(crtc_addr,0x0c);
 outb(crtc_addr+1,(address&0xff00)>>8);
 outb(crtc_addr,0x0d);
 outb(crtc_addr+1,address&0x00ff);

 // And change the BIOS page
 write_byte(BIOSMEM_SEG,BIOSMEM_CURRENT_PAGE,page);

#ifdef DEBUG
 printf("Set active page %02x address %04x\n",page,address);
#endif

 // Display the cursor, now the page is active
 biosfn_set_cursor_pos(page,cursor);
}

// --------------------------------------------------------------------------------------------
static void vgamem_copy_pl4(xstart,ysrc,ydest,cols,nbcols,cheight)
Bit8u xstart;Bit8u ysrc;Bit8u ydest;Bit8u cols;Bit8u nbcols;Bit8u cheight;
{
 Bit16u src,dest;
 Bit8u i;

 src=ysrc*cheight*nbcols+xstart;
 dest=ydest*cheight*nbcols+xstart;
 outw(VGAREG_GRDC_ADDRESS, 0x0105);
 for(i=0;i<cheight;i++)
  {
   memcpyb(0xa000,dest+i*nbcols,0xa000,src+i*nbcols,cols);
  }
 outw(VGAREG_GRDC_ADDRESS, 0x0005);
}

// --------------------------------------------------------------------------------------------
static void vgamem_fill_pl4(xstart,ystart,cols,nbcols,cheight,attr)
Bit8u xstart;Bit8u ystart;Bit8u cols;Bit8u nbcols;Bit8u cheight;Bit8u attr;
{
 Bit16u dest;
 Bit8u i;

 dest=ystart*cheight*nbcols+xstart;
 outw(VGAREG_GRDC_ADDRESS, 0x0205);
 for(i=0;i<cheight;i++)
  {
   memsetb(0xa000,dest+i*nbcols,attr,cols);
  }
 outw(VGAREG_GRDC_ADDRESS, 0x0005);
}

// --------------------------------------------------------------------------------------------
static void vgamem_copy_cga(xstart,ysrc,ydest,cols,nbcols,cheight)
Bit8u xstart;Bit8u ysrc;Bit8u ydest;Bit8u cols;Bit8u nbcols;Bit8u cheight;
{
 Bit16u src,dest;
 Bit8u i;

 src=((ysrc*cheight*nbcols)>>1)+xstart;
 dest=((ydest*cheight*nbcols)>>1)+xstart;
 for(i=0;i<cheight;i++)
  {
   if (i & 1)
     memcpyb(0xb800,0x2000+dest+(i>>1)*nbcols,0xb800,0x2000+src+(i>>1)*nbcols,cols);
   else
     memcpyb(0xb800,dest+(i>>1)*nbcols,0xb800,src+(i>>1)*nbcols,cols);
  }
}

// --------------------------------------------------------------------------------------------
static void vgamem_fill_cga(xstart,ystart,cols,nbcols,cheight,attr)
Bit8u xstart;Bit8u ystart;Bit8u cols;Bit8u nbcols;Bit8u cheight;Bit8u attr;
{
 Bit16u dest;
 Bit8u i;

 dest=((ystart*cheight*nbcols)>>1)+xstart;
 for(i=0;i<cheight;i++)
  {
   if (i & 1)
     memsetb(0xb800,0x2000+dest+(i>>1)*nbcols,attr,cols);
   else
     memsetb(0xb800,dest+(i>>1)*nbcols,attr,cols);
  }
}

// --------------------------------------------------------------------------------------------
static void biosfn_scroll (nblines,attr,rul,cul,rlr,clr,page,dir)
Bit8u nblines;Bit8u attr;Bit8u rul;Bit8u cul;Bit8u rlr;Bit8u clr;Bit8u page;Bit8u dir;
{
 // page == 0xFF if current

 Bit8u mode,line,cheight,bpp,cols;
 Bit16u nbcols,nbrows,i;
 Bit16u address;

 if(rul>rlr)return;
 if(cul>clr)return;

 // Get the mode
 mode=read_byte(BIOSMEM_SEG,BIOSMEM_CURRENT_MODE);
 line=find_vga_entry(mode);
 if(line==0xFF)return;

 // Get the dimensions
 nbrows=read_byte(BIOSMEM_SEG,BIOSMEM_NB_ROWS)+1;
 nbcols=read_word(BIOSMEM_SEG,BIOSMEM_NB_COLS);

 // Get the current page
 if(page==0xFF)
  page=read_byte(BIOSMEM_SEG,BIOSMEM_CURRENT_PAGE);

 if(rlr>=nbrows)rlr=nbrows-1;
 if(clr>=nbcols)clr=nbcols-1;
 if(nblines>nbrows)nblines=0;
 cols=clr-cul+1;

 if(vga_modes[line].class==TEXT)
  {
   // Compute the address
   address=SCREEN_MEM_START(nbcols,nbrows,page);
#ifdef DEBUG
   printf("Scroll, address %04x (%04x %04x %02x)\n",address,nbrows,nbcols,page);
#endif

   if(nblines==0&&rul==0&&cul==0&&rlr==nbrows-1&&clr==nbcols-1)
    {
     memsetw(vga_modes[line].sstart,address,(Bit16u)attr*0x100+' ',nbrows*nbcols);
    }
   else
    {// if Scroll up
     if(dir==SCROLL_UP)
      {for(i=rul;i<=rlr;i++)
        {
         if((i+nblines>rlr)||(nblines==0))
          memsetw(vga_modes[line].sstart,address+(i*nbcols+cul)*2,(Bit16u)attr*0x100+' ',cols);
         else
          memcpyw(vga_modes[line].sstart,address+(i*nbcols+cul)*2,vga_modes[line].sstart,((i+nblines)*nbcols+cul)*2,cols);
        }
      }
     else
      {for(i=rlr;i>=rul;i--)
        {
         if((i<rul+nblines)||(nblines==0))
          memsetw(vga_modes[line].sstart,address+(i*nbcols+cul)*2,(Bit16u)attr*0x100+' ',cols);
         else
          memcpyw(vga_modes[line].sstart,address+(i*nbcols+cul)*2,vga_modes[line].sstart,((i-nblines)*nbcols+cul)*2,cols);
         if (i>rlr) break;
        }
      }
    }
  }
 else
  {
   // FIXME gfx mode not complete
   cheight=video_param_table[line_to_vpti[line]].cheight;
   switch(vga_modes[line].memmodel)
    {
     case PLANAR4:
     case PLANAR1:
       if(nblines==0&&rul==0&&cul==0&&rlr==nbrows-1&&clr==nbcols-1)
        {
         outw(VGAREG_GRDC_ADDRESS, 0x0205);
         memsetb(vga_modes[line].sstart,0,attr,nbrows*nbcols*cheight);
         outw(VGAREG_GRDC_ADDRESS, 0x0005);
        }
       else
        {// if Scroll up
         if(dir==SCROLL_UP)
          {for(i=rul;i<=rlr;i++)
            {
             if((i+nblines>rlr)||(nblines==0))
              vgamem_fill_pl4(cul,i,cols,nbcols,cheight,attr);
             else
              vgamem_copy_pl4(cul,i+nblines,i,cols,nbcols,cheight);
            }
          }
         else
          {for(i=rlr;i>=rul;i--)
            {
             if((i<rul+nblines)||(nblines==0))
              vgamem_fill_pl4(cul,i,cols,nbcols,cheight,attr);
             else
              vgamem_copy_pl4(cul,i,i-nblines,cols,nbcols,cheight);
             if (i>rlr) break;
            }
          }
        }
       break;
     case CGA:
       bpp=vga_modes[line].pixbits;
       if(nblines==0&&rul==0&&cul==0&&rlr==nbrows-1&&clr==nbcols-1)
        {
         memsetb(vga_modes[line].sstart,0,attr,nbrows*nbcols*cheight*bpp);
        }
       else
        {
         if(bpp==2)
          {
           cul<<=1;
           cols<<=1;
           nbcols<<=1;
          }
         // if Scroll up
         if(dir==SCROLL_UP)
          {for(i=rul;i<=rlr;i++)
            {
             if((i+nblines>rlr)||(nblines==0))
              vgamem_fill_cga(cul,i,cols,nbcols,cheight,attr);
             else
              vgamem_copy_cga(cul,i+nblines,i,cols,nbcols,cheight);
            }
          }
         else
          {for(i=rlr;i>=rul;i--)
            {
             if((i<rul+nblines)||(nblines==0))
              vgamem_fill_cga(cul,i,cols,nbcols,cheight,attr);
             else
              vgamem_copy_cga(cul,i,i-nblines,cols,nbcols,cheight);
             if (i>rlr) break;
            }
          }
        }
       break;
#ifdef DEBUG
     default:
       printf("Scroll in graphics mode ");
       unimplemented();
#endif
    }
  }
}

// --------------------------------------------------------------------------------------------
static void biosfn_read_char_attr (page,car) 
Bit8u page;Bit16u *car;
{Bit16u ss=get_SS();
 Bit8u xcurs,ycurs,mode,line;
 Bit16u nbcols,nbrows,address;
 Bit16u cursor,dummy;

 // Get the mode
 mode=read_byte(BIOSMEM_SEG,BIOSMEM_CURRENT_MODE);
 line=find_vga_entry(mode);
 if(line==0xFF)return;

 // Get the cursor pos for the page
 biosfn_get_cursor_pos(page,&dummy,&cursor);
 xcurs=cursor&0x00ff;ycurs=(cursor&0xff00)>>8;

 // Get the dimensions
 nbrows=read_byte(BIOSMEM_SEG,BIOSMEM_NB_ROWS)+1;
 nbcols=read_word(BIOSMEM_SEG,BIOSMEM_NB_COLS);

 if(vga_modes[line].class==TEXT)
  {
   // Compute the address
   address=SCREEN_MEM_START(nbcols,nbrows,page)+(xcurs+ycurs*nbcols)*2;

   write_word(ss,car,read_word(vga_modes[line].sstart,address));
  }
 else
  {
   // FIXME gfx mode
#ifdef DEBUG
   unimplemented();
#endif
  }
}

// --------------------------------------------------------------------------------------------
static void write_gfx_char_pl4(car,attr,xcurs,ycurs,nbcols,cheight)
Bit8u car;Bit8u attr;Bit8u xcurs;Bit8u ycurs;Bit8u nbcols;Bit8u cheight;
{
 Bit8u i,j,mask;
 Bit8u *fdata;
 Bit16u addr,dest,src;

 switch(cheight)
  {case 14:
    fdata = &vgafont14;
    break;
   case 16:
    fdata = &vgafont16;
    break;
   default:
    fdata = &vgafont8;
  }
 addr=xcurs+ycurs*cheight*nbcols;
 src = car * cheight;
 outw(VGAREG_SEQU_ADDRESS, 0x0f02);
 outw(VGAREG_GRDC_ADDRESS, 0x0205);
 if(attr&0x80)
  {
   outw(VGAREG_GRDC_ADDRESS, 0x1803);
  }
 else
  {
   outw(VGAREG_GRDC_ADDRESS, 0x0003);
  }
 for(i=0;i<cheight;i++)
  {
   dest=addr+i*nbcols;
   for(j=0;j<8;j++)
    {
     mask=0x80>>j;
     outw(VGAREG_GRDC_ADDRESS, (mask << 8) | 0x08);
     read_byte(0xa000,dest);
     if(fdata[src+i]&mask)
      {
       write_byte(0xa000,dest,attr&0x0f);
      }
     else
      {
       write_byte(0xa000,dest,0x00);
      }
    }
  }
ASM_START
  mov dx, # VGAREG_GRDC_ADDRESS
  mov ax, #0xff08
  out dx, ax
  mov ax, #0x0005
  out dx, ax
  mov ax, #0x0003
  out dx, ax
ASM_END
}

// --------------------------------------------------------------------------------------------
static void write_gfx_char_cga(car,attr,xcurs,ycurs,nbcols,bpp)
Bit8u car;Bit8u attr;Bit8u xcurs;Bit8u ycurs;Bit8u nbcols;Bit8u bpp;
{
 Bit8u i,j,mask,data;
 Bit8u *fdata;
 Bit16u addr,dest,src;

 fdata = &vgafont8;
 addr=(xcurs*bpp)+ycurs*320;
 src = car * 8;
 for(i=0;i<8;i++)
  {
   dest=addr+(i>>1)*80;
   if (i & 1) dest += 0x2000;
   mask = 0x80;
   if (bpp == 1)
    {
     if (attr & 0x80)
      {
       data = read_byte(0xb800,dest);
      }
     else
      {
       data = 0x00;
      }
     for(j=0;j<8;j++)
      {
       if (fdata[src+i] & mask)
        {
         if (attr & 0x80)
          {
           data ^= (attr & 0x01) << (7-j);
          }
         else
          {
           data |= (attr & 0x01) << (7-j);
          }
        }
       mask >>= 1;
      }
     write_byte(0xb800,dest,data);
    }
   else
    {
     while (mask > 0)
      {
       if (attr & 0x80)
        {
         data = read_byte(0xb800,dest);
        }
       else
        {
         data = 0x00;
        }
       for(j=0;j<4;j++)
        {
         if (fdata[src+i] & mask)
          {
           if (attr & 0x80)
            {
             data ^= (attr & 0x03) << ((3-j)*2);
            }
           else
            {
             data |= (attr & 0x03) << ((3-j)*2);
            }
          }
         mask >>= 1;
        }
       write_byte(0xb800,dest,data);
       dest += 1;
      }
    }
  }
}

// --------------------------------------------------------------------------------------------
static void write_gfx_char_lin(car,attr,xcurs,ycurs,nbcols)
Bit8u car;Bit8u attr;Bit8u xcurs;Bit8u ycurs;Bit8u nbcols;
{
 Bit8u i,j,mask,data;
 Bit8u *fdata;
 Bit16u addr,dest,src;

 fdata = &vgafont8;
 addr=xcurs*8+ycurs*nbcols*64;
 src = car * 8;
 for(i=0;i<8;i++)
  {
   dest=addr+i*nbcols*8;
   mask = 0x80;
   for(j=0;j<8;j++)
    {
     data = 0x00;
     if (fdata[src+i] & mask)
      {
       data = attr;
      }
     write_byte(0xa000,dest+j,data);
     mask >>= 1;
    }
  }
}

// --------------------------------------------------------------------------------------------
static void biosfn_write_char_attr (car,page,attr,count) 
Bit8u car;Bit8u page;Bit8u attr;Bit16u count;
{
 Bit8u cheight,xcurs,ycurs,mode,line,bpp;
 Bit16u nbcols,nbrows,address;
 Bit16u cursor,dummy;

 // Get the mode
 mode=read_byte(BIOSMEM_SEG,BIOSMEM_CURRENT_MODE);
 line=find_vga_entry(mode);
 if(line==0xFF)return;

 // Get the cursor pos for the page
 biosfn_get_cursor_pos(page,&dummy,&cursor);
 xcurs=cursor&0x00ff;ycurs=(cursor&0xff00)>>8;

 // Get the dimensions
 nbrows=read_byte(BIOSMEM_SEG,BIOSMEM_NB_ROWS)+1;
 nbcols=read_word(BIOSMEM_SEG,BIOSMEM_NB_COLS);

 if(vga_modes[line].class==TEXT)
  {
   // Compute the address
   address=SCREEN_MEM_START(nbcols,nbrows,page)+(xcurs+ycurs*nbcols)*2;

   dummy=((Bit16u)attr<<8)+car;
   memsetw(vga_modes[line].sstart,address,dummy,count);
  }
 else
  {
   // FIXME gfx mode not complete
   cheight=video_param_table[line_to_vpti[line]].cheight;
   bpp=vga_modes[line].pixbits;
   while((count-->0) && (xcurs<nbcols))
    {
     switch(vga_modes[line].memmodel)
      {
       case PLANAR4:
       case PLANAR1:
         write_gfx_char_pl4(car,attr,xcurs,ycurs,nbcols,cheight);
         break;
       case CGA:
         write_gfx_char_cga(car,attr,xcurs,ycurs,nbcols,bpp);
         break;
       case LINEAR8:
         write_gfx_char_lin(car,attr,xcurs,ycurs,nbcols);
         break;
#ifdef DEBUG
       default:
         unimplemented();
#endif
      }
     xcurs++;
    }
  }
}

// --------------------------------------------------------------------------------------------
static void biosfn_write_char_only (car,page,attr,count)
Bit8u car;Bit8u page;Bit8u attr;Bit16u count;
{
 Bit8u cheight,xcurs,ycurs,mode,line,bpp;
 Bit16u nbcols,nbrows,address;
 Bit16u cursor,dummy;

 // Get the mode
 mode=read_byte(BIOSMEM_SEG,BIOSMEM_CURRENT_MODE);
 line=find_vga_entry(mode);
 if(line==0xFF)return;

 // Get the cursor pos for the page
 biosfn_get_cursor_pos(page,&dummy,&cursor);
 xcurs=cursor&0x00ff;ycurs=(cursor&0xff00)>>8;

 // Get the dimensions
 nbrows=read_byte(BIOSMEM_SEG,BIOSMEM_NB_ROWS)+1;
 nbcols=read_word(BIOSMEM_SEG,BIOSMEM_NB_COLS);

 if(vga_modes[line].class==TEXT)
  {
   // Compute the address
   address=SCREEN_MEM_START(nbcols,nbrows,page)+(xcurs+ycurs*nbcols)*2;

   while(count-->0)
    {write_byte(vga_modes[line].sstart,address,car);
     address+=2;
    }
  }
 else
  {
   // FIXME gfx mode not complete
   cheight=video_param_table[line_to_vpti[line]].cheight;
   bpp=vga_modes[line].pixbits;
   while((count-->0) && (xcurs<nbcols))
    {
     switch(vga_modes[line].memmodel)
      {
       case PLANAR4:
       case PLANAR1:
         write_gfx_char_pl4(car,attr,xcurs,ycurs,nbcols,cheight);
         break;
       case CGA:
         write_gfx_char_cga(car,attr,xcurs,ycurs,nbcols,bpp);
         break;
       case LINEAR8:
         write_gfx_char_lin(car,attr,xcurs,ycurs,nbcols);
         break;
#ifdef DEBUG
       default:
         unimplemented();
#endif
      }
     xcurs++;
    }
  }
}

// --------------------------------------------------------------------------------------------
ASM_START
biosfn_group_0B:
  cmp   bh, #0x00
  je    biosfn_set_border_color
  cmp   bh, #0x01
  je    biosfn_set_palette
#ifdef DEBUG
  call  _unknown
#endif
  ret
biosfn_set_border_color:
  push  ax
  push  bx
  push  cx
  push  dx
  mov   dx, # VGAREG_ACTL_RESET
  in    al, dx
  mov   dx, # VGAREG_ACTL_ADDRESS
  mov   al, #0x00
  out   dx, al
  mov   al, bl
  and   al, #0x0f
  test  al, #0x08
  jz    set_low_border
  add   al, #0x08
set_low_border:
  out   dx, al
  mov   cl, #0x01
  and   bl, #0x10
set_intensity_loop:
  mov   dx, # VGAREG_ACTL_ADDRESS
  mov   al, cl
  out   dx, al
  mov   dx, # VGAREG_ACTL_READ_DATA
  in    al, dx
  and   al, #0xef
  or    al, bl
  mov   dx, # VGAREG_ACTL_ADDRESS
  out   dx, al
  inc   cl
  cmp   cl, #0x04
  jne   set_intensity_loop
  mov   al, #0x20
  out   dx, al
  pop   dx
  pop   cx
  pop   bx
  pop   ax
  ret
biosfn_set_palette:
  push  ax
  push  bx
  push  cx
  push  dx
  mov   dx, # VGAREG_ACTL_RESET
  in    al, dx
  mov   cl, #0x01
  and   bl, #0x01
set_cga_palette_loop:
  mov   dx, # VGAREG_ACTL_ADDRESS
  mov   al, cl
  out   dx, al
  mov   dx, # VGAREG_ACTL_READ_DATA
  in    al, dx
  and   al, #0xfe
  or    al, bl
  mov   dx, # VGAREG_ACTL_ADDRESS
  out   dx, al
  inc   cl
  cmp   cl, #0x04
  jne   set_cga_palette_loop
  mov   al, #0x20
  out   dx, al
  pop   dx
  pop   cx
  pop   bx
  pop   ax
  ret
ASM_END

// --------------------------------------------------------------------------------------------
static void biosfn_write_pixel (BH,AL,CX,DX) Bit8u BH;Bit8u AL;Bit16u CX;Bit16u DX;
{
 Bit8u mode,line,mask,attr,data;
 Bit16u addr;

 // Get the mode
 mode=read_byte(BIOSMEM_SEG,BIOSMEM_CURRENT_MODE);
 line=find_vga_entry(mode);
 if(line==0xFF)return;
 if(vga_modes[line].class==TEXT)return;

 switch(vga_modes[line].memmodel)
  {
   case PLANAR4:
   case PLANAR1:
     addr = CX/8+DX*read_word(BIOSMEM_SEG,BIOSMEM_NB_COLS);
     mask = 0x80 >> (CX & 0x07);
     outw(VGAREG_GRDC_ADDRESS, (mask << 8) | 0x08);
     outw(VGAREG_GRDC_ADDRESS, 0x0205);
     data = read_byte(0xa000,addr);
     if (AL & 0x80)
      {
       outw(VGAREG_GRDC_ADDRESS, 0x1803);
      }
     write_byte(0xa000,addr,AL);
ASM_START
     mov dx, # VGAREG_GRDC_ADDRESS
     mov ax, #0xff08
     out dx, ax
     mov ax, #0x0005
     out dx, ax
     mov ax, #0x0003
     out dx, ax
ASM_END
     break;
   case CGA:
     if(vga_modes[line].pixbits==2)
      {
       addr=(CX>>2)+(DX>>1)*80;
      }
     else
      {
       addr=(CX>>3)+(DX>>1)*80;
      }
     if (DX & 1) addr += 0x2000;
     data = read_byte(0xb800,addr);
     if(vga_modes[line].pixbits==2)
      {
       attr = (AL & 0x03) << ((3 - (CX & 0x03)) * 2);
       mask = 0x03 << ((3 - (CX & 0x03)) * 2);
      }
     else
      {
       attr = (AL & 0x01) << (7 - (CX & 0x07));
       mask = 0x01 << (7 - (CX & 0x07));
      }
     if (AL & 0x80)
      {
       data ^= attr;
      }
     else
      {
       data &= ~mask;
       data |= attr;
      }
     write_byte(0xb800,addr,data);
     break;
   case LINEAR8:
     addr=CX+DX*(read_word(BIOSMEM_SEG,BIOSMEM_NB_COLS)*8);
     write_byte(0xa000,addr,AL);
     break;
#ifdef DEBUG
   default:
     unimplemented();
#endif
  }
}

// --------------------------------------------------------------------------------------------
static void biosfn_read_pixel (BH,CX,DX,AX) Bit8u BH;Bit16u CX;Bit16u DX;Bit16u *AX;
{
 Bit8u mode,line,mask,attr,data,i;
 Bit16u addr;
 Bit16u ss=get_SS();

 // Get the mode
 mode=read_byte(BIOSMEM_SEG,BIOSMEM_CURRENT_MODE);
 line=find_vga_entry(mode);
 if(line==0xFF)return;
 if(vga_modes[line].class==TEXT)return;

 switch(vga_modes[line].memmodel)
  {
   case PLANAR4:
   case PLANAR1:
     addr = CX/8+DX*read_word(BIOSMEM_SEG,BIOSMEM_NB_COLS);
     mask = 0x80 >> (CX & 0x07);
     attr = 0x00;
     for(i=0;i<4;i++)
      {
       outw(VGAREG_GRDC_ADDRESS, (i << 8) | 0x04);
       data = read_byte(0xa000,addr) & mask;
       if (data > 0) attr |= (0x01 << i);
      }
     break;
   case CGA:
     addr=(CX>>2)+(DX>>1)*80;
     if (DX & 1) addr += 0x2000;
     data = read_byte(0xb800,addr);
     if(vga_modes[line].pixbits==2)
      {
       attr = (data >> ((3 - (CX & 0x03)) * 2)) & 0x03;
      }
     else
      {
       attr = (data >> (7 - (CX & 0x07))) & 0x01;
      }
     break;
   case LINEAR8:
     addr=CX+DX*(read_word(BIOSMEM_SEG,BIOSMEM_NB_COLS)*8);
     attr=read_byte(0xa000,addr);
     break;
   default:
#ifdef DEBUG
     unimplemented();
#endif
     attr = 0;
  }
 write_word(ss,AX,(read_word(ss,AX) & 0xff00) | attr);
}

// --------------------------------------------------------------------------------------------
static void biosfn_write_teletype (car, page, attr, flag) 
Bit8u car;Bit8u page;Bit8u attr;Bit8u flag;
{// flag = WITH_ATTR / NO_ATTR

 Bit8u cheight,xcurs,ycurs,mode,line,bpp;
 Bit16u nbcols,nbrows,address;
 Bit16u cursor,dummy;

 // special case if page is 0xff, use current page
 if(page==0xff)
  page=read_byte(BIOSMEM_SEG,BIOSMEM_CURRENT_PAGE);

 // Get the mode
 mode=read_byte(BIOSMEM_SEG,BIOSMEM_CURRENT_MODE);
 line=find_vga_entry(mode);
 if(line==0xFF)return;

 // Get the cursor pos for the page
 biosfn_get_cursor_pos(page,&dummy,&cursor);
 xcurs=cursor&0x00ff;ycurs=(cursor&0xff00)>>8;

 // Get the dimensions
 nbrows=read_byte(BIOSMEM_SEG,BIOSMEM_NB_ROWS)+1;
 nbcols=read_word(BIOSMEM_SEG,BIOSMEM_NB_COLS);

 switch(car)
  {
   case 7:
    //FIXME should beep
    break;

   case 8:
    if(xcurs>0)xcurs--;
    break;

   case '\r':
    xcurs=0;
    break;

   case '\n':
    ycurs++;
    break;

   case '\t':
    do
     {
      biosfn_write_teletype(' ',page,attr,flag);
      biosfn_get_cursor_pos(page,&dummy,&cursor);
      xcurs=cursor&0x00ff;ycurs=(cursor&0xff00)>>8;
     }while(xcurs%8==0);
    break;

   default:

    if(vga_modes[line].class==TEXT)
     {
      // Compute the address  
      address=SCREEN_MEM_START(nbcols,nbrows,page)+(xcurs+ycurs*nbcols)*2;

      // Write the char 
      write_byte(vga_modes[line].sstart,address,car);

      if(flag==WITH_ATTR)
       write_byte(vga_modes[line].sstart,address+1,attr);
     }
    else
     {
      // FIXME gfx mode not complete
      cheight=video_param_table[line_to_vpti[line]].cheight;
      bpp=vga_modes[line].pixbits;
      switch(vga_modes[line].memmodel)
       {
        case PLANAR4:
        case PLANAR1:
          write_gfx_char_pl4(car,attr,xcurs,ycurs,nbcols,cheight);
          break;
        case CGA:
          write_gfx_char_cga(car,attr,xcurs,ycurs,nbcols,bpp);
          break;
        case LINEAR8:
          write_gfx_char_lin(car,attr,xcurs,ycurs,nbcols);
          break;
#ifdef DEBUG
        default:
          unimplemented();
#endif
       }
     }
    xcurs++;
  }

 // Do we need to wrap ?
 if(xcurs==nbcols)
  {xcurs=0;
   ycurs++;
  }

 // Do we need to scroll ?
 if(ycurs==nbrows)
  {
   if(vga_modes[line].class==TEXT)
    {
     biosfn_scroll(0x01,0x07,0,0,nbrows-1,nbcols-1,page,SCROLL_UP);
    }
   else
    {
     biosfn_scroll(0x01,0x00,0,0,nbrows-1,nbcols-1,page,SCROLL_UP);
    }
   ycurs-=1;
  }
 
 // Set the cursor for the page
 cursor=ycurs; cursor<<=8; cursor+=xcurs;
 biosfn_set_cursor_pos(page,cursor);
}

// --------------------------------------------------------------------------------------------
ASM_START
biosfn_get_video_mode:
  push  ds
  mov   ax, # BIOSMEM_SEG
  mov   ds, ax
  push  bx
  mov   bx, # BIOSMEM_CURRENT_PAGE
  mov   al, [bx]
  pop   bx
  mov   bh, al
  push  bx
  mov   bx, # BIOSMEM_VIDEO_CTL
  mov   ah, [bx]
  and   ah, #0x80
  mov   bx, # BIOSMEM_CURRENT_MODE
  mov   al, [bx]
  or    al, ah
  mov   bx, # BIOSMEM_NB_COLS
  mov   ah, [bx]
  pop   bx
  pop   ds
  ret
ASM_END

// --------------------------------------------------------------------------------------------
ASM_START
biosfn_group_10:
  cmp   al, #0x00
  jne   int10_test_1001
  jmp   biosfn_set_single_palette_reg
int10_test_1001:
  cmp   al, #0x01
  jne   int10_test_1002
  jmp   biosfn_set_overscan_border_color
int10_test_1002:
  cmp   al, #0x02
  jne   int10_test_1003
  jmp   biosfn_set_all_palette_reg
int10_test_1003:
  cmp   al, #0x03
  jne   int10_test_1007
  jmp   biosfn_toggle_intensity
int10_test_1007:
  cmp   al, #0x07
  jne   int10_test_1008
  jmp   biosfn_get_single_palette_reg
int10_test_1008:
  cmp   al, #0x08
  jne   int10_test_1009
  jmp   biosfn_read_overscan_border_color
int10_test_1009:
  cmp   al, #0x09
  jne   int10_test_1010
  jmp   biosfn_get_all_palette_reg
int10_test_1010:
  cmp   al, #0x10
  jne   int10_test_1012
  jmp  biosfn_set_single_dac_reg
int10_test_1012:
  cmp   al, #0x12
  jne   int10_test_1013
  jmp   biosfn_set_all_dac_reg
int10_test_1013:
  cmp   al, #0x13
  jne   int10_test_1015
  jmp   biosfn_select_video_dac_color_page
int10_test_1015:
  cmp   al, #0x15
  jne   int10_test_1017
  jmp   biosfn_read_single_dac_reg
int10_test_1017:
  cmp   al, #0x17
  jne   int10_test_1018
  jmp   biosfn_read_all_dac_reg
int10_test_1018:
  cmp   al, #0x18
  jne   int10_test_1019
  jmp   biosfn_set_pel_mask
int10_test_1019:
  cmp   al, #0x19
  jne   int10_test_101A
  jmp   biosfn_read_pel_mask
int10_test_101A:
  cmp   al, #0x1a
  jne   int10_group_10_unknown
  jmp   biosfn_read_video_dac_state
int10_group_10_unknown:
#ifdef DEBUG
  call  _unknown
#endif
  ret

biosfn_set_single_palette_reg:
  cmp   bl, #0x14
  ja    no_actl_reg1
  push  ax
  push  dx
  mov   dx, # VGAREG_ACTL_RESET
  in    al, dx
  mov   dx, # VGAREG_ACTL_ADDRESS
  mov   al, bl
  out   dx, al
  mov   al, bh
  out   dx, al
  mov   al, #0x20
  out   dx, al
  pop   dx
  pop   ax
no_actl_reg1:
  ret
ASM_END

// --------------------------------------------------------------------------------------------
ASM_START
biosfn_set_overscan_border_color:
  push  bx
  mov   bl, #0x11
  call  biosfn_set_single_palette_reg
  pop   bx
  ret
ASM_END

// --------------------------------------------------------------------------------------------
ASM_START
biosfn_set_all_palette_reg:
  push  ax
  push  bx
  push  cx
  push  dx
  mov   bx, dx
  mov   dx, # VGAREG_ACTL_RESET
  in    al, dx
  mov   cl, #0x00
  mov   dx, # VGAREG_ACTL_ADDRESS
set_palette_loop:
  mov   al, cl
  out   dx, al
  seg   es
  mov   al, [bx]
  out   dx, al
  inc   bx
  inc   cl
  cmp   cl, #0x10
  jne   set_palette_loop
  mov   al, #0x11
  out   dx, al
  seg   es
  mov   al, [bx]
  out   dx, al
  mov   al, #0x20
  out   dx, al
  pop   dx
  pop   cx
  pop   bx
  pop   ax
  ret
ASM_END

// --------------------------------------------------------------------------------------------
ASM_START
biosfn_toggle_intensity:
  push  ax
  push  bx
  push  dx
  mov   dx, # VGAREG_ACTL_RESET
  in    al, dx
  mov   dx, # VGAREG_ACTL_ADDRESS
  mov   al, #0x10
  out   dx, al
  mov   dx, # VGAREG_ACTL_READ_DATA
  in    al, dx
  and   al, #0xf7
  and   bl, #0x01
  shl   bl, 3
  or    al, bl
  mov   dx, # VGAREG_ACTL_ADDRESS
  out   dx, al
  mov   al, #0x20
  out   dx, al
  pop   dx
  pop   bx
  pop   ax
  ret
ASM_END

// --------------------------------------------------------------------------------------------
ASM_START
biosfn_get_single_palette_reg:
  cmp   bl, #0x14
  ja    no_actl_reg2
  push  ax
  push  dx
  mov   dx, # VGAREG_ACTL_RESET
  in    al, dx
  mov   dx, # VGAREG_ACTL_ADDRESS
  mov   al, bl
  out   dx, al
  mov   dx, # VGAREG_ACTL_READ_DATA
  in    al, dx
  mov   bh, al
  mov   dx, # VGAREG_ACTL_RESET
  in    al, dx
  mov   dx, # VGAREG_ACTL_ADDRESS
  mov   al, #0x20
  out   dx, al
  pop   dx
  pop   ax
no_actl_reg2:
  ret
ASM_END

// --------------------------------------------------------------------------------------------
ASM_START
biosfn_read_overscan_border_color:
  push  ax
  push  bx
  mov   bl, #0x11
  call  biosfn_get_single_palette_reg
  mov   al, bh
  pop   bx
  mov   bh, al
  pop   ax
  ret
ASM_END

// --------------------------------------------------------------------------------------------
ASM_START
biosfn_get_all_palette_reg:
  push  ax
  push  bx
  push  cx
  push  dx
  mov   bx, dx
  mov   cl, #0x00
get_palette_loop:
  mov   dx, # VGAREG_ACTL_RESET
  in    al, dx
  mov   dx, # VGAREG_ACTL_ADDRESS
  mov   al, cl
  out   dx, al
  mov   dx, # VGAREG_ACTL_READ_DATA
  in    al, dx
  seg   es
  mov   [bx], al
  inc   bx
  inc   cl
  cmp   cl, #0x10
  jne   get_palette_loop
  mov   dx, # VGAREG_ACTL_RESET
  in    al, dx
  mov   dx, # VGAREG_ACTL_ADDRESS
  mov   al, #0x11
  out   dx, al
  mov   dx, # VGAREG_ACTL_READ_DATA
  in    al, dx
  seg   es
  mov   [bx], al
  mov   dx, # VGAREG_ACTL_RESET
  in    al, dx
  mov   dx, # VGAREG_ACTL_ADDRESS
  mov   al, #0x20
  out   dx, al
  pop   dx
  pop   cx
  pop   bx
  pop   ax
  ret
ASM_END

// --------------------------------------------------------------------------------------------
ASM_START
biosfn_set_single_dac_reg:
  push  ax
  push  dx
  mov   dx, # VGAREG_DAC_WRITE_ADDRESS
  mov   al, bl
  out   dx, al
  mov   dx, # VGAREG_DAC_DATA
  pop   ax
  push  ax
  mov   al, ah
  out   dx, al
  mov   al, ch
  out   dx, al
  mov   al, cl
  out   dx, al
  pop   dx
  pop   ax
  ret
ASM_END

// --------------------------------------------------------------------------------------------
ASM_START
biosfn_set_all_dac_reg:
  push  ax
  push  bx
  push  cx
  push  dx
  mov   dx, # VGAREG_DAC_WRITE_ADDRESS
  mov   al, bl
  out   dx, al
  pop   dx
  push  dx
  mov   bx, dx
  mov   dx, # VGAREG_DAC_DATA
set_dac_loop:
  seg   es
  mov   al, [bx]
  out   dx, al
  inc   bx
  seg   es
  mov   al, [bx]
  out   dx, al
  inc   bx
  seg   es
  mov   al, [bx]
  out   dx, al
  inc   bx
  dec   cx
  jnz   set_dac_loop
  pop   dx
  pop   cx
  pop   bx
  pop   ax
  ret
ASM_END

// --------------------------------------------------------------------------------------------
ASM_START
biosfn_select_video_dac_color_page:
  push  ax
  push  bx
  push  dx
  mov   dx, # VGAREG_ACTL_RESET
  in    al, dx
  mov   dx, # VGAREG_ACTL_ADDRESS
  mov   al, #0x10
  out   dx, al
  mov   dx, # VGAREG_ACTL_READ_DATA
  in    al, dx
  and   bl, #0x01
  jnz   set_dac_page
  and   al, #0x7f
  shl   bh, 7
  or    al, bh
  mov   dx, # VGAREG_ACTL_ADDRESS
  out   dx, al
  jmp   set_actl_normal
set_dac_page:
  push  ax
  mov   dx, # VGAREG_ACTL_RESET
  in    al, dx
  mov   dx, # VGAREG_ACTL_ADDRESS
  mov   al, #0x14
  out   dx, al
  pop   ax
  and   al, #0x80
  jnz   set_dac_16_page
  shl   bh, 2
set_dac_16_page:
  and   bh, #0x0f
  mov   al, bh
  out   dx, al
set_actl_normal:
  mov   al, #0x20
  out   dx, al
  pop   dx
  pop   bx
  pop   ax
  ret
ASM_END

// --------------------------------------------------------------------------------------------
ASM_START
biosfn_read_single_dac_reg:
  push  ax
  push  dx
  mov   dx, # VGAREG_DAC_READ_ADDRESS
  mov   al, bl
  out   dx, al
  pop   ax
  mov   ah, al
  mov   dx, # VGAREG_DAC_DATA
  in    al, dx
  xchg  al, ah
  push  ax
  in    al, dx
  mov   ch, al
  in    al, dx
  mov   cl, al
  pop   dx
  pop   ax
  ret
ASM_END

// --------------------------------------------------------------------------------------------
ASM_START
biosfn_read_all_dac_reg:
  push  ax
  push  bx
  push  cx
  push  dx
  mov   dx, # VGAREG_DAC_READ_ADDRESS
  mov   al, bl
  out   dx, al
  pop   dx
  push  dx
  mov   bx, dx
  mov   dx, # VGAREG_DAC_DATA
read_dac_loop:
  in    al, dx
  seg   es
  mov   [bx], al
  inc   bx
  in    al, dx
  seg   es
  mov   [bx], al
  inc   bx
  in    al, dx
  seg   es
  mov   [bx], al
  inc   bx
  dec   cx
  jnz   read_dac_loop
  pop   dx
  pop   cx
  pop   bx
  pop   ax
  ret
ASM_END

// --------------------------------------------------------------------------------------------
ASM_START
biosfn_set_pel_mask:
  push  ax
  push  dx
  mov   dx, # VGAREG_PEL_MASK
  mov   al, bl
  out   dx, al
  pop   dx
  pop   ax
  ret
ASM_END

// --------------------------------------------------------------------------------------------
ASM_START
biosfn_read_pel_mask:
  push  ax
  push  dx
  mov   dx, # VGAREG_PEL_MASK
  in    al, dx
  mov   bl, al
  pop   dx
  pop   ax
  ret
ASM_END

// --------------------------------------------------------------------------------------------
ASM_START
biosfn_read_video_dac_state:
  push  ax
  push  dx
  mov   dx, # VGAREG_ACTL_RESET
  in    al, dx
  mov   dx, # VGAREG_ACTL_ADDRESS
  mov   al, #0x10
  out   dx, al
  mov   dx, # VGAREG_ACTL_READ_DATA
  in    al, dx
  mov   bl, al
  shr   bl, 7
  mov   dx, # VGAREG_ACTL_RESET
  in    al, dx
  mov   dx, # VGAREG_ACTL_ADDRESS
  mov   al, #0x14
  out   dx, al
  mov   dx, # VGAREG_ACTL_READ_DATA
  in    al, dx
  mov   bh, al
  and   bh, #0x0f
  test  bl, #0x01
  jnz   get_dac_16_page
  shr   bh, 2
get_dac_16_page:
  mov   dx, # VGAREG_ACTL_RESET
  in    al, dx
  mov   dx, # VGAREG_ACTL_ADDRESS
  mov   al, #0x20
  out   dx, al
  pop   dx
  pop   ax
  ret
ASM_END

// --------------------------------------------------------------------------------------------
static void biosfn_perform_gray_scale_summing (start,count) 
Bit16u start;Bit16u count;
{Bit8u r,g,b;
 Bit16u i;
 Bit16u index;

 inb(VGAREG_ACTL_RESET);
 outb(VGAREG_ACTL_ADDRESS,0x00);

 for( index = 0; index < count; index++ ) 
  {
   // set read address and switch to read mode
   outb(VGAREG_DAC_READ_ADDRESS,start);
   // get 6-bit wide RGB data values
   r=inb( VGAREG_DAC_DATA );
   g=inb( VGAREG_DAC_DATA );
   b=inb( VGAREG_DAC_DATA );

   // intensity = ( 0.3 * Red ) + ( 0.59 * Green ) + ( 0.11 * Blue )
   i = ( ( 77*r + 151*g + 28*b ) + 0x80 ) >> 8;

   if(i>0x3f)i=0x3f;
 
   // set write address and switch to write mode
   outb(VGAREG_DAC_WRITE_ADDRESS,start);
   // write new intensity value
   outb( VGAREG_DAC_DATA, i&0xff );
   outb( VGAREG_DAC_DATA, i&0xff );
   outb( VGAREG_DAC_DATA, i&0xff );
   start++;
  }  
 inb(VGAREG_ACTL_RESET);
 outb(VGAREG_ACTL_ADDRESS,0x20);
}

// --------------------------------------------------------------------------------------------
static void get_font_access()
{
ASM_START
 mov dx, # VGAREG_SEQU_ADDRESS
 mov ax, #0x0100
 out dx, ax
 mov ax, #0x0402
 out dx, ax
 mov ax, #0x0704
 out dx, ax
 mov ax, #0x0300
 out dx, ax
 mov dx, # VGAREG_GRDC_ADDRESS
 mov ax, #0x0204
 out dx, ax
 mov ax, #0x0005
 out dx, ax
 mov ax, #0x0406
 out dx, ax
ASM_END
}

static void release_font_access()
{
ASM_START
 mov dx, # VGAREG_SEQU_ADDRESS
 mov ax, #0x0100
 out dx, ax
 mov ax, #0x0302
 out dx, ax
 mov ax, #0x0304
 out dx, ax
 mov ax, #0x0300
 out dx, ax
 mov dx, # VGAREG_READ_MISC_OUTPUT
 in  al, dx
 and al, #0x01
 shl al, 2
 or  al, #0x0a
 mov ah, al
 mov al, #0x06
 mov dx, # VGAREG_GRDC_ADDRESS
 out dx, ax
 mov ax, #0x0004
 out dx, ax
 mov ax, #0x1005
 out dx, ax
ASM_END
}

ASM_START
idiv_u:
  xor dx,dx
  div bx
  ret
ASM_END

static void set_scan_lines(lines) Bit8u lines;
{
 Bit16u crtc_addr,cols,page,vde;
 Bit8u crtc_r9,ovl,rows;

 crtc_addr = read_word(BIOSMEM_SEG,BIOSMEM_CRTC_ADDRESS);
 outb(crtc_addr, 0x09);
 crtc_r9 = inb(crtc_addr+1);
 crtc_r9 = (crtc_r9 & 0xe0) | (lines - 1);
 outb(crtc_addr+1, crtc_r9);
 if(lines==8)
  {
   biosfn_set_cursor_shape(0x06,0x07);
  }
 else
  {
   biosfn_set_cursor_shape(lines-4,lines-3);
  }
 write_word(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT, lines);
 outb(crtc_addr, 0x12);
 vde = inb(crtc_addr+1);
 outb(crtc_addr, 0x07);
 ovl = inb(crtc_addr+1);
 vde += (((ovl & 0x02) << 7) + ((ovl & 0x40) << 3) + 1);
 rows = vde / lines;
 write_byte(BIOSMEM_SEG,BIOSMEM_NB_ROWS, rows-1);
 cols = read_word(BIOSMEM_SEG,BIOSMEM_NB_COLS);
 write_word(BIOSMEM_SEG,BIOSMEM_PAGE_SIZE, rows * cols * 2);
}

static void biosfn_load_text_user_pat (AL,ES,BP,CX,DX,BL,BH) Bit8u AL;Bit16u ES;Bit16u BP;Bit16u CX;Bit16u DX;Bit8u BL;Bit8u BH;
{
 Bit16u blockaddr,dest,i,src;

 get_font_access();
 blockaddr = ((BL & 0x03) << 14) + ((BL & 0x04) << 11);
 for(i=0;i<CX;i++)
  {
   src = BP + i * BH;
   dest = blockaddr + (DX + i) * 32;
   memcpyb(0xA000, dest, ES, src, BH);
  }
 release_font_access();
 if(AL>=0x10)
  {
   set_scan_lines(BH);
  }
}

static void biosfn_load_text_8_14_pat (AL,BL) Bit8u AL;Bit8u BL;
{
 Bit16u blockaddr,dest,i,src;

 get_font_access();
 blockaddr = ((BL & 0x03) << 14) + ((BL & 0x04) << 11);
 for(i=0;i<0x100;i++)
  {
   src = i * 14;
   dest = blockaddr + i * 32;
   memcpyb(0xA000, dest, 0xC000, vgafont14+src, 14);
  }
 release_font_access();
 if(AL>=0x10)
  {
   set_scan_lines(14);
  }
}

static void biosfn_load_text_8_8_pat (AL,BL) Bit8u AL;Bit8u BL;
{
 Bit16u blockaddr,dest,i,src;

 get_font_access();
 blockaddr = ((BL & 0x03) << 14) + ((BL & 0x04) << 11);
 for(i=0;i<0x100;i++)
  {
   src = i * 8;
   dest = blockaddr + i * 32;
   memcpyb(0xA000, dest, 0xC000, vgafont8+src, 8);
  }
 release_font_access();
 if(AL>=0x10)
  {
   set_scan_lines(8);
  }
}

// --------------------------------------------------------------------------------------------
ASM_START
biosfn_set_text_block_specifier:
  push  ax
  push  dx
  mov   dx, # VGAREG_SEQU_ADDRESS
  mov   ah, bl
  mov   al, #0x03
  out   dx, ax
  pop   dx
  pop   ax
  ret
ASM_END

// --------------------------------------------------------------------------------------------
static void biosfn_load_text_8_16_pat (AL,BL) Bit8u AL;Bit8u BL;
{
 Bit16u blockaddr,dest,i,src;

 get_font_access();
 blockaddr = ((BL & 0x03) << 14) + ((BL & 0x04) << 11);
 for(i=0;i<0x100;i++)
  {
   src = i * 16;
   dest = blockaddr + i * 32;
   memcpyb(0xA000, dest, 0xC000, vgafont16+src, 16);
  }
 release_font_access();
 if(AL>=0x10)
  {
   set_scan_lines(16);
  }
}

static void biosfn_load_gfx_8_8_chars (ES,BP) Bit16u ES;Bit16u BP;
{
#ifdef DEBUG
 unimplemented();
#endif
}
static void biosfn_load_gfx_user_chars (ES,BP,CX,BL,DL) Bit16u ES;Bit16u BP;Bit16u CX;Bit8u BL;Bit8u DL;
{
#ifdef DEBUG
 unimplemented();
#endif
}
static void biosfn_load_gfx_8_14_chars (BL) Bit8u BL;
{
#ifdef DEBUG
 unimplemented();
#endif
}
static void biosfn_load_gfx_8_8_dd_chars (BL) Bit8u BL;
{
#ifdef DEBUG
 unimplemented();
#endif
}
static void biosfn_load_gfx_8_16_chars (BL) Bit8u BL;
{
#ifdef DEBUG
 unimplemented();
#endif
}
// --------------------------------------------------------------------------------------------
static void biosfn_get_font_info (BH,ES,BP,CX,DX) 
Bit8u BH;Bit16u *ES;Bit16u *BP;Bit16u *CX;Bit16u *DX;
{Bit16u ss=get_SS();
 
 switch(BH)
  {case 0x00:
    write_word(ss,ES,read_word(0x00,0x1f*4));
    write_word(ss,BP,read_word(0x00,(0x1f*4)+2));
    break;
   case 0x01:
    write_word(ss,ES,read_word(0x00,0x43*4));
    write_word(ss,BP,read_word(0x00,(0x43*4)+2));
    break;
   case 0x02:
    write_word(ss,ES,0xC000);
    write_word(ss,BP,vgafont14);
    break;
   case 0x03:
    write_word(ss,ES,0xC000);
    write_word(ss,BP,vgafont8);
    break;
   case 0x04:
    write_word(ss,ES,0xC000);
    write_word(ss,BP,vgafont8+128*8);
    break;
   case 0x05:
    write_word(ss,ES,0xC000);
    write_word(ss,BP,vgafont14alt);
    break;
   case 0x06:
    write_word(ss,ES,0xC000);
    write_word(ss,BP,vgafont16);
    break;
   case 0x07:
    write_word(ss,ES,0xC000);
    write_word(ss,BP,vgafont16alt);
    break;
   default:
    #ifdef DEBUG
     printf("Get font info BH(%02x) was discarded\n",BH);
    #endif
    return;
  }
 // Set byte/char of on screen font
 write_word(ss,CX,(Bit16u)read_byte(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT));

 // Set Highest char row
 write_word(ss,DX,(Bit16u)read_byte(BIOSMEM_SEG,BIOSMEM_NB_ROWS));
}

// --------------------------------------------------------------------------------------------
ASM_START
biosfn_get_ega_info:
  push  ds
  push  ax
  mov   ax, # BIOSMEM_SEG
  mov   ds, ax
  xor   ch, ch
  mov   bx, # BIOSMEM_SWITCHES
  mov   cl, [bx]
  and   cl, #0x0f
  mov   bx, # BIOSMEM_CRTC_ADDRESS
  mov   ax, [bx]
  mov   bx, #0x0003
  cmp   ax, # VGAREG_MDA_CRTC_ADDRESS
  jne   mode_ega_color
  mov   bh, #0x01
mode_ega_color:
  pop   ax
  pop   ds
  ret
ASM_END

// --------------------------------------------------------------------------------------------
static void biosfn_alternate_prtsc()
{
#ifdef DEBUG
 unimplemented();
#endif
}

// --------------------------------------------------------------------------------------------
ASM_START
biosfn_select_vert_res:

; res : 00 200 lines, 01 350 lines, 02 400 lines

  push  ds
  push  bx
  push  dx
  mov   dl, al
  mov   ax, # BIOSMEM_SEG
  mov   ds, ax
  mov   bx, # BIOSMEM_MODESET_CTL
  mov   al, [bx]
  mov   bx, # BIOSMEM_SWITCHES
  mov   ah, [bx]
  cmp   dl, #0x01
  je    vert_res_350
  jb    vert_res_200
  cmp   dl, #0x02
  je    vert_res_400
#ifdef DEBUG
  mov   al, dl
  xor   ah, ah
  push  ax
  mov   bx, #msg_vert_res
  push  bx
  call  _printf
  add   sp, #4
#endif
  jmp   set_retcode
vert_res_400:

  ; reset modeset ctl bit 7 and set bit 4
  ; set switches bit 3-0 to 0x09

  and   al, #0x7f
  or    al, #0x10
  and   ah, #0xf0
  or    ah, #0x09
  jnz   set_vert_res
vert_res_350:

  ; reset modeset ctl bit 7 and bit 4
  ; set switches bit 3-0 to 0x09

  and   al, #0x6f
  and   ah, #0xf0
  or    ah, #0x09
  jnz   set_vert_res
vert_res_200:

  ; set modeset ctl bit 7 and reset bit 4
  ; set switches bit 3-0 to 0x08

  and   al, #0xef
  or    al, #0x80
  and   ah, #0xf0
  or    ah, #0x08
set_vert_res:
  mov   bx, # BIOSMEM_MODESET_CTL
  mov   [bx], al
  mov   bx, # BIOSMEM_SWITCHES
  mov   [bx], ah
set_retcode:
  mov   ax, #0x1212
  pop   dx
  pop   bx
  pop   ds
  ret

#ifdef DEBUG
msg_vert_res:
.ascii "Select vert res (%02x) was discarded"
.byte 0x0d,0x0a,0x00
#endif


biosfn_enable_default_palette_loading:
  push  ds
  push  bx
  push  dx
  mov   dl, al
  and   dl, #0x01
  shl   dl, 3
  mov   ax, # BIOSMEM_SEG
  mov   ds, ax
  mov   bx, # BIOSMEM_MODESET_CTL
  mov   al, [bx]
  and   al, #0xf7
  or    al, dl
  mov   [bx], al
  mov   ax, #0x1212
  pop   dx
  pop   bx
  pop   ds
  ret


biosfn_enable_video_addressing:
  push  bx
  push  dx
  mov   bl, al
  and   bl, #0x01
  xor   bl, #0x01
  shl   bl, 1
  mov   dx, # VGAREG_READ_MISC_OUTPUT
  in    al, dx
  and   al, #0xfd
  or    al, bl
  mov   dx, # VGAREG_WRITE_MISC_OUTPUT
  out   dx, al
  mov   ax, #0x1212
  pop   dx
  pop   bx
  ret


biosfn_enable_grayscale_summing:
  push  ds
  push  bx
  push  dx
  mov   dl, al
  and   dl, #0x01
  xor   dl, #0x01
  shl   dl, 1
  mov   ax, # BIOSMEM_SEG
  mov   ds, ax
  mov   bx, # BIOSMEM_MODESET_CTL
  mov   al, [bx]
  and   al, #0xfd
  or    al, dl
  mov   [bx], al
  mov   ax, #0x1212
  pop   dx
  pop   bx
  pop   ds
  ret


biosfn_enable_cursor_emulation:
  push  ds
  push  bx
  push  dx
  mov   dl, al
  and   dl, #0x01
  xor   dl, #0x01
  mov   ax, # BIOSMEM_SEG
  mov   ds, ax
  mov   bx, # BIOSMEM_MODESET_CTL
  mov   al, [bx]
  and   al, #0xfe
  or    al, dl
  mov   [bx], al
  mov   ax, #0x1212
  pop   dx
  pop   bx
  pop   ds
  ret
ASM_END

// --------------------------------------------------------------------------------------------
static void biosfn_switch_video_interface (AL,ES,DX) Bit8u AL;Bit16u ES;Bit16u DX;
{
#ifdef DEBUG
 unimplemented();
#endif
}
static void biosfn_enable_video_refresh_control (AL) Bit8u AL;
{
#ifdef DEBUG
 unimplemented();
#endif
}

// --------------------------------------------------------------------------------------------
static void biosfn_write_string (flag,page,attr,count,row,col,seg,offset) 
Bit8u flag;Bit8u page;Bit8u attr;Bit16u count;Bit8u row;Bit8u col;Bit16u seg;Bit16u offset;
{
 Bit16u newcurs,oldcurs,dummy;
 Bit8u car,carattr;

 // Read curs info for the page
 biosfn_get_cursor_pos(page,&dummy,&oldcurs);

 // if row=0xff special case : use current cursor position
 if(row==0xff)
  {col=oldcurs&0x00ff;
   row=(oldcurs&0xff00)>>8;
  }

 newcurs=row; newcurs<<=8; newcurs+=col;
 biosfn_set_cursor_pos(page,newcurs);
 
 while(count--!=0)
  {
   car=read_byte(seg,offset++);
   if((flag&0x02)!=0)
    attr=read_byte(seg,offset++);

   biosfn_write_teletype(car,page,attr,WITH_ATTR);
  }
 
 // Set back curs pos 
 if((flag&0x01)==0)
  biosfn_set_cursor_pos(page,oldcurs);
}

// --------------------------------------------------------------------------------------------
ASM_START
biosfn_group_1A:
  cmp   al, #0x00
  je    biosfn_read_display_code
  cmp   al, #0x01
  je    biosfn_set_display_code
#ifdef DEBUG
  call  _unknown
#endif
  ret
biosfn_read_display_code:
  push  ds
  push  ax
  mov   ax, # BIOSMEM_SEG
  mov   ds, ax
  mov   bx, # BIOSMEM_DCC_INDEX
  mov   al, [bx]
  mov   bl, al
  xor   bh, bh
  pop   ax
  mov   al, ah
  pop   ds
  ret
biosfn_set_display_code:
  push  ds
  push  ax
  push  bx
  mov   ax, # BIOSMEM_SEG
  mov   ds, ax
  mov   ax, bx
  mov   bx, # BIOSMEM_DCC_INDEX
  mov   [bx], al
#ifdef DEBUG
  mov   al, ah
  xor   ah, ah
  push  ax
  mov   bx, #msg_alt_dcc
  push  bx
  call  _printf
  add   sp, #4
#endif
  pop   bx
  pop   ax
  mov   al, ah
  pop   ds
  ret

#ifdef DEBUG
msg_alt_dcc:
.ascii "Alternate Display code (%02x) was discarded"
.byte 0x0d,0x0a,0x00
#endif
ASM_END

// --------------------------------------------------------------------------------------------
static void biosfn_read_state_info (BX,ES,DI) 
Bit16u BX;Bit16u ES;Bit16u DI;
{
 // Address of static functionality table
 write_word(ES,DI+0x00,&static_functionality);
 write_word(ES,DI+0x02,0xC000);

 // Hard coded copy from BIOS area. Should it be cleaner ?
 memcpyb(ES,DI+0x04,BIOSMEM_SEG,0x49,30);
 memcpyb(ES,DI+0x22,BIOSMEM_SEG,0x84,3);
 
 write_byte(ES,DI+0x25,read_byte(BIOSMEM_SEG,BIOSMEM_DCC_INDEX));
 write_byte(ES,DI+0x26,0);
 write_byte(ES,DI+0x27,16);
 write_byte(ES,DI+0x28,0);
 write_byte(ES,DI+0x29,8);
 write_byte(ES,DI+0x2a,2);
 write_byte(ES,DI+0x2b,0);
 write_byte(ES,DI+0x2c,0);
 write_byte(ES,DI+0x31,3);
 write_byte(ES,DI+0x32,0);
 
 memsetb(ES,DI+0x33,0,13);
}

// --------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------
static Bit16u biosfn_read_video_state_size2 (CX) 
     Bit16u CX;
{
    Bit16u size;
    size = 0;
    if (CX & 1) {
        size += 0x46;
    }
    if (CX & 2) {
        size += (5 + 8 + 5) * 2 + 6;
    }
    if (CX & 4) {
        size += 3 + 256 * 3 + 1;
}
    return size;
}

static void biosfn_read_video_state_size (CX, BX) 
     Bit16u CX; Bit16u *BX;
{
    Bit16u ss=get_SS();
    write_word(ss, BX, biosfn_read_video_state_size2(CX));
}

static Bit16u biosfn_save_video_state (CX,ES,BX) 
     Bit16u CX;Bit16u ES;Bit16u BX;
{
    Bit16u i, v, crtc_addr, ar_index;

    crtc_addr = read_word(BIOSMEM_SEG, BIOSMEM_CRTC_ADDRESS);
    if (CX & 1) {
        write_byte(ES, BX, inb(VGAREG_SEQU_ADDRESS)); BX++;
        write_byte(ES, BX, inb(crtc_addr)); BX++;
        write_byte(ES, BX, inb(VGAREG_GRDC_ADDRESS)); BX++;
        inb(VGAREG_ACTL_RESET);
        ar_index = inb(VGAREG_ACTL_ADDRESS);
        write_byte(ES, BX, ar_index); BX++;
        write_byte(ES, BX, inb(VGAREG_READ_FEATURE_CTL)); BX++;

        for(i=1;i<=4;i++){
            outb(VGAREG_SEQU_ADDRESS, i);
            write_byte(ES, BX, inb(VGAREG_SEQU_DATA)); BX++;
        }
        outb(VGAREG_SEQU_ADDRESS, 0);
        write_byte(ES, BX, inb(VGAREG_SEQU_DATA)); BX++;

        for(i=0;i<=0x18;i++) {
            outb(crtc_addr,i);
            write_byte(ES, BX, inb(crtc_addr+1)); BX++;
        }

        for(i=0;i<=0x13;i++) {
            inb(VGAREG_ACTL_RESET);
            outb(VGAREG_ACTL_ADDRESS, i | (ar_index & 0x20));
            write_byte(ES, BX, inb(VGAREG_ACTL_READ_DATA)); BX++;
        }
        inb(VGAREG_ACTL_RESET);

        for(i=0;i<=8;i++) {
            outb(VGAREG_GRDC_ADDRESS,i);
            write_byte(ES, BX, inb(VGAREG_GRDC_DATA)); BX++;
        }

        write_word(ES, BX, crtc_addr); BX+= 2;

        /* XXX: read plane latches */
        write_byte(ES, BX, 0); BX++;
        write_byte(ES, BX, 0); BX++;
        write_byte(ES, BX, 0); BX++;
        write_byte(ES, BX, 0); BX++;
    }
    if (CX & 2) {
        write_byte(ES, BX, read_byte(BIOSMEM_SEG,BIOSMEM_CURRENT_MODE)); BX++;
        write_word(ES, BX, read_word(BIOSMEM_SEG,BIOSMEM_NB_COLS)); BX += 2;
        write_word(ES, BX, read_word(BIOSMEM_SEG,BIOSMEM_PAGE_SIZE)); BX += 2;
        write_word(ES, BX, read_word(BIOSMEM_SEG,BIOSMEM_CRTC_ADDRESS)); BX += 2;
        write_byte(ES, BX, read_byte(BIOSMEM_SEG,BIOSMEM_NB_ROWS)); BX++;
        write_word(ES, BX, read_word(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT)); BX += 2;
        write_byte(ES, BX, read_byte(BIOSMEM_SEG,BIOSMEM_VIDEO_CTL)); BX++;
        write_byte(ES, BX, read_byte(BIOSMEM_SEG,BIOSMEM_SWITCHES)); BX++;
        write_byte(ES, BX, read_byte(BIOSMEM_SEG,BIOSMEM_MODESET_CTL)); BX++;
        write_word(ES, BX, read_word(BIOSMEM_SEG,BIOSMEM_CURSOR_TYPE)); BX += 2;
        for(i=0;i<8;i++) {
            write_word(ES, BX, read_word(BIOSMEM_SEG, BIOSMEM_CURSOR_POS+2*i));
            BX += 2;
        }
        write_word(ES, BX, read_word(BIOSMEM_SEG,BIOSMEM_CURRENT_START)); BX += 2;
        write_byte(ES, BX, read_byte(BIOSMEM_SEG,BIOSMEM_CURRENT_PAGE)); BX++;
        /* current font */
        write_word(ES, BX, read_word(0, 0x1f * 4)); BX += 2;
        write_word(ES, BX, read_word(0, 0x1f * 4 + 2)); BX += 2;
        write_word(ES, BX, read_word(0, 0x43 * 4)); BX += 2;
        write_word(ES, BX, read_word(0, 0x43 * 4 + 2)); BX += 2;
    }
    if (CX & 4) {
        /* XXX: check this */
        write_byte(ES, BX, inb(VGAREG_DAC_STATE)); BX++; /* read/write mode dac */
        write_byte(ES, BX, inb(VGAREG_DAC_WRITE_ADDRESS)); BX++; /* pix address */
        write_byte(ES, BX, inb(VGAREG_PEL_MASK)); BX++;
        // Set the whole dac always, from 0
        outb(VGAREG_DAC_WRITE_ADDRESS,0x00);
        for(i=0;i<256*3;i++) {
            write_byte(ES, BX, inb(VGAREG_DAC_DATA)); BX++;
        }
        write_byte(ES, BX, 0); BX++; /* color select register */
    }
    return BX;
}

static Bit16u biosfn_restore_video_state (CX,ES,BX) 
     Bit16u CX;Bit16u ES;Bit16u BX;
{
    Bit16u i, crtc_addr, v, addr1, ar_index;

    if (CX & 1) {
        // Reset Attribute Ctl flip-flop
        inb(VGAREG_ACTL_RESET);

        crtc_addr = read_word(ES, BX + 0x40);
        addr1 = BX;
        BX += 5;
        
        for(i=1;i<=4;i++){
            outb(VGAREG_SEQU_ADDRESS, i);
            outb(VGAREG_SEQU_DATA, read_byte(ES, BX)); BX++;
        }
        outb(VGAREG_SEQU_ADDRESS, 0);
        outb(VGAREG_SEQU_DATA, read_byte(ES, BX)); BX++;

        // Disable CRTC write protection
        outw(crtc_addr,0x0011);
        // Set CRTC regs
        for(i=0;i<=0x18;i++) {
            if (i != 0x11) {
                outb(crtc_addr,i);
                outb(crtc_addr+1, read_byte(ES, BX));
            }
            BX++;
        }
        // select crtc base address
        v = inb(VGAREG_READ_MISC_OUTPUT) & ~0x01;
        if (crtc_addr = 0x3d4)
            v |= 0x01;
        outb(VGAREG_WRITE_MISC_OUTPUT, v);

        // enable write protection if needed
        outb(crtc_addr, 0x11);
        outb(crtc_addr+1, read_byte(ES, BX - 0x18 + 0x11));
        
        // Set Attribute Ctl
        ar_index = read_byte(ES, addr1 + 0x03);
        inb(VGAREG_ACTL_RESET);
        for(i=0;i<=0x13;i++) {
            outb(VGAREG_ACTL_ADDRESS, i | (ar_index & 0x20));
            outb(VGAREG_ACTL_WRITE_DATA, read_byte(ES, BX)); BX++;
        }
        outb(VGAREG_ACTL_ADDRESS, ar_index);
        inb(VGAREG_ACTL_RESET);
        
        for(i=0;i<=8;i++) {
            outb(VGAREG_GRDC_ADDRESS,i);
            outb(VGAREG_GRDC_DATA, read_byte(ES, BX)); BX++;
        }
        BX += 2; /* crtc_addr */
        BX += 4; /* plane latches */
        
        outb(VGAREG_SEQU_ADDRESS, read_byte(ES, addr1)); addr1++;
        outb(crtc_addr, read_byte(ES, addr1)); addr1++;
        outb(VGAREG_GRDC_ADDRESS, read_byte(ES, addr1)); addr1++;
        addr1++;
        outb(crtc_addr - 0x4 + 0xa, read_byte(ES, addr1)); addr1++;
    }
    if (CX & 2) {
        write_byte(BIOSMEM_SEG,BIOSMEM_CURRENT_MODE, read_byte(ES, BX)); BX++;
        write_word(BIOSMEM_SEG,BIOSMEM_NB_COLS, read_word(ES, BX)); BX += 2;
        write_word(BIOSMEM_SEG,BIOSMEM_PAGE_SIZE, read_word(ES, BX)); BX += 2;
        write_word(BIOSMEM_SEG,BIOSMEM_CRTC_ADDRESS, read_word(ES, BX)); BX += 2;
        write_byte(BIOSMEM_SEG,BIOSMEM_NB_ROWS, read_byte(ES, BX)); BX++;
        write_word(BIOSMEM_SEG,BIOSMEM_CHAR_HEIGHT, read_word(ES, BX)); BX += 2;
        write_byte(BIOSMEM_SEG,BIOSMEM_VIDEO_CTL, read_byte(ES, BX)); BX++;
        write_byte(BIOSMEM_SEG,BIOSMEM_SWITCHES, read_byte(ES, BX)); BX++;
        write_byte(BIOSMEM_SEG,BIOSMEM_MODESET_CTL, read_byte(ES, BX)); BX++;
        write_word(BIOSMEM_SEG,BIOSMEM_CURSOR_TYPE, read_word(ES, BX)); BX += 2;
        for(i=0;i<8;i++) {
            write_word(BIOSMEM_SEG, BIOSMEM_CURSOR_POS+2*i, read_word(ES, BX));
            BX += 2;
        }
        write_word(BIOSMEM_SEG,BIOSMEM_CURRENT_START, read_word(ES, BX)); BX += 2;
        write_byte(BIOSMEM_SEG,BIOSMEM_CURRENT_PAGE, read_byte(ES, BX)); BX++;
        /* current font */
        write_word(0, 0x1f * 4, read_word(ES, BX)); BX += 2;
        write_word(0, 0x1f * 4 + 2, read_word(ES, BX)); BX += 2;
        write_word(0, 0x43 * 4, read_word(ES, BX)); BX += 2;
        write_word(0, 0x43 * 4 + 2, read_word(ES, BX)); BX += 2;
    }
    if (CX & 4) {
        BX++;
        v = read_byte(ES, BX); BX++;
        outb(VGAREG_PEL_MASK, read_byte(ES, BX)); BX++;
        // Set the whole dac always, from 0
        outb(VGAREG_DAC_WRITE_ADDRESS,0x00);
        for(i=0;i<256*3;i++) {
            outb(VGAREG_DAC_DATA, read_byte(ES, BX)); BX++;
        }
        BX++;
        outb(VGAREG_DAC_WRITE_ADDRESS, v);
    }
    return BX;
}

// ============================================================================================
//
// Video Utils
//
// ============================================================================================
 
// --------------------------------------------------------------------------------------------
static Bit8u find_vga_entry(mode) 
Bit8u mode;
{
 Bit8u i,line=0xFF;
 for(i=0;i<=MODE_MAX;i++)
  if(vga_modes[i].svgamode==mode)
   {line=i;
    break;
   }
 return line;
}

/* =========================================================== */
/*
 * Misc Utils
*/
/* =========================================================== */

// --------------------------------------------------------------------------------------------
static void memsetb(seg,offset,value,count)
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

// --------------------------------------------------------------------------------------------
static void memsetw(seg,offset,value,count)
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
    je   memsetw_end
    mov  ax, 4[bp] ; segment
    mov  es, ax
    mov  ax, 6[bp] ; offset
    mov  di, ax
    mov  ax, 8[bp] ; value
    cld
    rep
     stosw

memsetw_end:
    pop di
    pop es
    pop cx
    pop ax

  pop bp
ASM_END
}

// --------------------------------------------------------------------------------------------
static void memcpyb(dseg,doffset,sseg,soffset,count)
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

// --------------------------------------------------------------------------------------------
static void memcpyw(dseg,doffset,sseg,soffset,count)
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
    je   memcpyw_end
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
     movsw

memcpyw_end:
    pop si
    pop ds
    pop di
    pop es
    pop cx
    pop ax

  pop bp
ASM_END
}

/* =========================================================== */
/*
 * These functions where ripped from Kevin's rombios.c
*/
/* =========================================================== */

// --------------------------------------------------------------------------------------------
static Bit8u
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

// --------------------------------------------------------------------------------------------
static Bit16u
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

// --------------------------------------------------------------------------------------------
static void
write_byte(seg, offset, data)
  Bit16u seg;
  Bit16u offset;
  Bit8u  data;
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

// --------------------------------------------------------------------------------------------
static void
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

// --------------------------------------------------------------------------------------------
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

// --------------------------------------------------------------------------------------------
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

// --------------------------------------------------------------------------------------------
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

Bit16u get_SS()
{
ASM_START
  mov  ax, ss
ASM_END
}

#ifdef DEBUG
void unimplemented()
{
 printf("--> Unimplemented\n");
}

void unknown()
{
 printf("--> Unknown int10\n");
}
#endif

// --------------------------------------------------------------------------------------------
#if defined(USE_BX_INFO) || defined(DEBUG) || defined(CIRRUS_DEBUG)
void printf(s)
  Bit8u *s;
{
  Bit8u c, format_char;
  Boolean  in_format;
  unsigned format_width, i;
  Bit16u  *arg_ptr;
  Bit16u   arg_seg, arg, digit, nibble, shift_count;

  arg_ptr = &s;
  arg_seg = get_SS();

  in_format = 0;
  format_width = 0;

  while (c = read_byte(0xc000, s)) {
    if ( c == '%' ) {
      in_format = 1;
      format_width = 0;
      }
    else if (in_format) {
      if ( (c>='0') && (c<='9') ) {
        format_width = (format_width * 10) + (c - '0');
        }
      else if (c == 'x') {
        arg_ptr++; // increment to next arg
        arg = read_word(arg_seg, arg_ptr);
        if (format_width == 0)
          format_width = 4;
        i = 0;
        digit = format_width - 1;
        for (i=0; i<format_width; i++) {
          nibble = (arg >> (4 * digit)) & 0x000f;
          if (nibble <= 9)
            outb(0x0500, nibble + '0');
          else
            outb(0x0500, (nibble - 10) + 'A');
          digit--;
          }
        in_format = 0;
        }
      //else if (c == 'd') {
      //  in_format = 0;
      //  }
      }
    else {
      outb(0x0500, c);
      }
    s ++;
    }
}
#endif

#ifdef VBE
#include "vbe.c"
#endif

#ifdef CIRRUS
#include "clext.c"
#endif

// --------------------------------------------------------------------------------------------

ASM_START 
;; DATA_SEG_DEFS_HERE
ASM_END

ASM_START
.ascii "vgabios ends here"
.byte  0x00
vgabios_end:
.byte 0xCB
;; BLOCK_STRINGS_BEGIN
ASM_END
