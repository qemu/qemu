// ============================================================================================
//  
//  Copyright (C) 2002 Jeroen Janssen
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
//  This VBE is part of the VGA Bios specific to the plex86/bochs Emulated VGA card. 
//  You can NOT drive any physical vga card with it. 
//
// ============================================================================================
//  
//  This VBE Bios is based on information taken from :
//   - VESA BIOS EXTENSION (VBE) Core Functions Standard Version 3.0 located at www.vesa.org
//
// ============================================================================================


// defines available

// disable VESA/VBE2 check in vbe info
//#define VBE2_NO_VESA_CHECK


#include "vbe.h"
#include "vbetables.h"

#define VBE_TOTAL_VIDEO_MEMORY_DIV_64K (VBE_DISPI_TOTAL_VIDEO_MEMORY_MB*1024/64)

// The current OEM Software Revision of this VBE Bios
#define VBE_OEM_SOFTWARE_REV 0x0002;

extern char vbebios_copyright;
extern char vbebios_vendor_name;
extern char vbebios_product_name;
extern char vbebios_product_revision;

ASM_START
// FIXME: 'merge' these (c) etc strings with the vgabios.c strings?
_vbebios_copyright:
.ascii       "Bochs/Plex86 VBE(C) 2003 http://savannah.nongnu.org/projects/vgabios/"
.byte        0x00

_vbebios_vendor_name:
.ascii       "Bochs/Plex86 Developers"
.byte        0x00

_vbebios_product_name:
.ascii       "Bochs/Plex86 VBE Adapter"
.byte        0x00

_vbebios_product_revision:
.ascii       "$Id: vbe.c,v 1.58 2006/08/19 09:39:43 vruppert Exp $"
.byte        0x00

_vbebios_info_string:
.ascii      "Bochs VBE Display Adapter enabled"
.byte	0x0a,0x0d
.byte	0x0a,0x0d
.byte	0x00

_no_vbebios_info_string:
.ascii      "NO Bochs VBE Support available!"
.byte	0x0a,0x0d
.byte	0x0a,0x0d
.byte 0x00

#if defined(USE_BX_INFO) || defined(DEBUG)
msg_vbe_init:
.ascii      "VBE Bios $Id: vbe.c,v 1.58 2006/08/19 09:39:43 vruppert Exp $"
.byte	0x0a,0x0d, 0x00
#endif

  .align 2
vesa_pm_start:
  dw vesa_pm_set_window - vesa_pm_start
  dw vesa_pm_set_display_start - vesa_pm_start
  dw vesa_pm_unimplemented - vesa_pm_start
  dw vesa_pm_io_ports_table - vesa_pm_start
vesa_pm_io_ports_table:
  dw VBE_DISPI_IOPORT_INDEX
  dw VBE_DISPI_IOPORT_INDEX + 1
  dw VBE_DISPI_IOPORT_DATA
  dw VBE_DISPI_IOPORT_DATA + 1
  dw 0xffff
  dw 0xffff

  USE32
vesa_pm_set_window:
  cmp  bx, #0x00
  je  vesa_pm_set_display_window1
  mov  ax, #0x0100
  ret
vesa_pm_set_display_window1:
  mov  ax, dx
  push dx
  push ax
  mov  dx, # VBE_DISPI_IOPORT_INDEX
  mov  ax, # VBE_DISPI_INDEX_BANK
  out  dx, ax
  pop  ax
  mov  dx, # VBE_DISPI_IOPORT_DATA
  out  dx, ax
  in   ax, dx
  pop  dx
  cmp  dx, ax
  jne  illegal_window
  mov  ax, #0x004f
  ret
illegal_window:
  mov  ax, #0x014f
  ret

vesa_pm_set_display_start:
  cmp  bl, #0x80
  je   vesa_pm_set_display_start1
  cmp  bl, #0x00
  je   vesa_pm_set_display_start1
  mov  ax, #0x0100
  ret
vesa_pm_set_display_start1:
; convert offset to (X, Y) coordinate 
; (would be simpler to change Bochs VBE API...)
  push eax
  push ecx
  push edx
  push esi
  push edi
  shl edx, #16
  and ecx, #0xffff
  or ecx, edx
  shl ecx, #2
  mov eax, ecx

  push eax
  mov  dx, # VBE_DISPI_IOPORT_INDEX
  mov  ax, # VBE_DISPI_INDEX_VIRT_WIDTH
  out  dx, ax
  mov  dx, # VBE_DISPI_IOPORT_DATA
  in   ax, dx
  movzx ecx, ax

  mov  dx, # VBE_DISPI_IOPORT_INDEX
  mov  ax, # VBE_DISPI_INDEX_BPP
  out  dx, ax
  mov  dx, # VBE_DISPI_IOPORT_DATA
  in   ax, dx
  movzx esi, ax
  pop  eax

  cmp esi, #4
  jz bpp4_mode
  add esi, #7
  shr esi, #3
  imul ecx, esi
  xor edx, edx
  div ecx
  mov edi, eax
  mov eax, edx
  xor edx, edx
  div esi
  jmp set_xy_regs

bpp4_mode:
  shr ecx, #1
  xor edx, edx
  div ecx
  mov edi, eax
  mov eax, edx
  shl eax, #1

set_xy_regs:
  push dx
  push ax
  mov  dx, # VBE_DISPI_IOPORT_INDEX
  mov  ax, # VBE_DISPI_INDEX_X_OFFSET
  out  dx, ax
  pop  ax
  mov  dx, # VBE_DISPI_IOPORT_DATA
  out  dx, ax
  pop  dx

  mov  ax, di
  push dx
  push ax
  mov  dx, # VBE_DISPI_IOPORT_INDEX
  mov  ax, # VBE_DISPI_INDEX_Y_OFFSET
  out  dx, ax
  pop  ax
  mov  dx, # VBE_DISPI_IOPORT_DATA
  out  dx, ax
  pop  dx

  pop edi
  pop esi
  pop edx
  pop ecx
  pop eax
  mov  ax, #0x004f
  ret

vesa_pm_unimplemented:
  mov ax, #0x014f
  ret
  USE16
vesa_pm_end:

; DISPI ioport functions

dispi_get_id:
  push dx
  mov  dx, # VBE_DISPI_IOPORT_INDEX
  mov  ax, # VBE_DISPI_INDEX_ID
  out  dx, ax
  mov  dx, # VBE_DISPI_IOPORT_DATA
  in   ax, dx
  pop  dx
  ret

dispi_set_id:
  push dx
  push ax
  mov  dx, # VBE_DISPI_IOPORT_INDEX
  mov  ax, # VBE_DISPI_INDEX_ID
  out  dx, ax
  pop  ax
  mov  dx, # VBE_DISPI_IOPORT_DATA
  out  dx, ax
  pop  dx
  ret
ASM_END

static void dispi_set_xres(xres)
  Bit16u xres;
{
ASM_START
  push bp
  mov  bp, sp
  push ax
  push dx

  mov  dx, # VBE_DISPI_IOPORT_INDEX
  mov  ax, # VBE_DISPI_INDEX_XRES
  out  dx, ax
  mov  dx, # VBE_DISPI_IOPORT_DATA
  mov  ax, 4[bp] ; xres
  out  dx, ax

  pop  dx
  pop  ax
  pop  bp
ASM_END
}

static void dispi_set_yres(yres)
  Bit16u yres;
{
  outw(VBE_DISPI_IOPORT_INDEX,VBE_DISPI_INDEX_YRES);
  outw(VBE_DISPI_IOPORT_DATA,yres);
}

static void dispi_set_bpp(bpp)
  Bit16u bpp;
{
  outw(VBE_DISPI_IOPORT_INDEX,VBE_DISPI_INDEX_BPP);
  outw(VBE_DISPI_IOPORT_DATA,bpp);
}

ASM_START
; AL = bits per pixel / AH = bytes per pixel
dispi_get_bpp:
  push dx
  mov  dx, # VBE_DISPI_IOPORT_INDEX
  mov  ax, # VBE_DISPI_INDEX_BPP
  out  dx, ax
  mov  dx, # VBE_DISPI_IOPORT_DATA
  in   ax, dx
  mov  ah, al
  shr  ah, 3
  test al, #0x07
  jz   get_bpp_noinc
  inc  ah
get_bpp_noinc:
  pop  dx
  ret

; get display capabilities

_dispi_get_max_xres:
  push dx
  push bx
  call dispi_get_enable
  mov  bx, ax
  or   ax, # VBE_DISPI_GETCAPS
  call _dispi_set_enable
  mov  dx, # VBE_DISPI_IOPORT_INDEX
  mov  ax, # VBE_DISPI_INDEX_XRES
  out  dx, ax
  mov  dx, # VBE_DISPI_IOPORT_DATA
  in   ax, dx
  push ax
  mov  ax, bx
  call _dispi_set_enable
  pop  ax
  pop  bx
  pop  dx
  ret

_dispi_get_max_bpp:
  push dx
  push bx
  call dispi_get_enable
  mov  bx, ax
  or   ax, # VBE_DISPI_GETCAPS
  call _dispi_set_enable
  mov  dx, # VBE_DISPI_IOPORT_INDEX
  mov  ax, # VBE_DISPI_INDEX_BPP
  out  dx, ax
  mov  dx, # VBE_DISPI_IOPORT_DATA
  in   ax, dx
  push ax
  mov  ax, bx
  call _dispi_set_enable
  pop  ax
  pop  bx
  pop  dx
  ret

_dispi_set_enable:
  push dx
  push ax
  mov  dx, # VBE_DISPI_IOPORT_INDEX
  mov  ax, # VBE_DISPI_INDEX_ENABLE
  out  dx, ax
  pop  ax
  mov  dx, # VBE_DISPI_IOPORT_DATA
  out  dx, ax
  pop  dx
  ret

dispi_get_enable:
  push dx
  mov  dx, # VBE_DISPI_IOPORT_INDEX
  mov  ax, # VBE_DISPI_INDEX_ENABLE
  out  dx, ax
  mov  dx, # VBE_DISPI_IOPORT_DATA
  in   ax, dx
  pop  dx
  ret

_dispi_set_bank:
  push dx
  push ax
  mov  dx, # VBE_DISPI_IOPORT_INDEX
  mov  ax, # VBE_DISPI_INDEX_BANK
  out  dx, ax
  pop  ax
  mov  dx, # VBE_DISPI_IOPORT_DATA
  out  dx, ax
  pop  dx
  ret

dispi_get_bank:
  push dx
  mov  dx, # VBE_DISPI_IOPORT_INDEX
  mov  ax, # VBE_DISPI_INDEX_BANK
  out  dx, ax
  mov  dx, # VBE_DISPI_IOPORT_DATA
  in   ax, dx
  pop  dx
  ret
ASM_END

static void dispi_set_bank_farcall()
{
ASM_START
  cmp bx,#0x0100
  je dispi_set_bank_farcall_get
  or bx,bx
  jnz dispi_set_bank_farcall_error
  mov ax,dx
  push dx
  push ax
  mov ax,# VBE_DISPI_INDEX_BANK
  mov dx,# VBE_DISPI_IOPORT_INDEX
  out dx,ax
  pop ax
  mov dx,# VBE_DISPI_IOPORT_DATA
  out dx,ax
  in  ax,dx
  pop dx
  cmp dx,ax
  jne dispi_set_bank_farcall_error
  mov ax, #0x004f
  retf
dispi_set_bank_farcall_get:
  mov ax,# VBE_DISPI_INDEX_BANK
  mov dx,# VBE_DISPI_IOPORT_INDEX
  out dx,ax
  mov dx,# VBE_DISPI_IOPORT_DATA
  in ax,dx
  mov dx,ax
  retf
dispi_set_bank_farcall_error:
  mov ax,#0x014F
  retf
ASM_END
}

ASM_START
dispi_set_x_offset:
  push dx
  push ax
  mov  dx, # VBE_DISPI_IOPORT_INDEX
  mov  ax, # VBE_DISPI_INDEX_X_OFFSET
  out  dx, ax
  pop  ax
  mov  dx, # VBE_DISPI_IOPORT_DATA
  out  dx, ax
  pop  dx
  ret

dispi_get_x_offset:
  push dx
  mov  dx, # VBE_DISPI_IOPORT_INDEX
  mov  ax, # VBE_DISPI_INDEX_X_OFFSET
  out  dx, ax
  mov  dx, # VBE_DISPI_IOPORT_DATA
  in   ax, dx
  pop  dx
  ret

dispi_set_y_offset:
  push dx
  push ax
  mov  dx, # VBE_DISPI_IOPORT_INDEX
  mov  ax, # VBE_DISPI_INDEX_Y_OFFSET
  out  dx, ax
  pop  ax
  mov  dx, # VBE_DISPI_IOPORT_DATA
  out  dx, ax
  pop  dx
  ret

dispi_get_y_offset:
  push dx
  mov  dx, # VBE_DISPI_IOPORT_INDEX
  mov  ax, # VBE_DISPI_INDEX_Y_OFFSET
  out  dx, ax
  mov  dx, # VBE_DISPI_IOPORT_DATA
  in   ax, dx
  pop  dx
  ret

vga_set_virt_width:
  push ax
  push bx
  push dx
  mov  bx, ax
  call dispi_get_bpp
  cmp  al, #0x04
  ja   set_width_svga
  shr  bx, #1
set_width_svga:
  shr  bx, #3
  mov  dx, # VGAREG_VGA_CRTC_ADDRESS
  mov  ah, bl
  mov  al, #0x13
  out  dx, ax
  pop  dx
  pop  bx
  pop  ax
  ret

dispi_set_virt_width:
  call vga_set_virt_width
  push dx
  push ax
  mov  dx, # VBE_DISPI_IOPORT_INDEX
  mov  ax, # VBE_DISPI_INDEX_VIRT_WIDTH
  out  dx, ax
  pop  ax
  mov  dx, # VBE_DISPI_IOPORT_DATA
  out  dx, ax
  pop  dx
  ret

dispi_get_virt_width:
  push dx
  mov  dx, # VBE_DISPI_IOPORT_INDEX
  mov  ax, # VBE_DISPI_INDEX_VIRT_WIDTH
  out  dx, ax
  mov  dx, # VBE_DISPI_IOPORT_DATA
  in   ax, dx
  pop  dx
  ret

dispi_get_virt_height:
  push dx
  mov  dx, # VBE_DISPI_IOPORT_INDEX
  mov  ax, # VBE_DISPI_INDEX_VIRT_HEIGHT
  out  dx, ax
  mov  dx, # VBE_DISPI_IOPORT_DATA
  in   ax, dx
  pop  dx
  ret

_vga_compat_setup:
  push ax
  push dx

  ; set CRT X resolution
  mov  dx, # VBE_DISPI_IOPORT_INDEX
  mov  ax, # VBE_DISPI_INDEX_XRES
  out  dx, ax
  mov  dx, # VBE_DISPI_IOPORT_DATA
  in   ax, dx
  push ax
  mov  dx, # VGAREG_VGA_CRTC_ADDRESS
  mov  ax, #0x0011
  out  dx, ax
  pop  ax
  push ax
  shr  ax, #3
  dec  ax
  mov  ah, al
  mov  al, #0x01
  out  dx, ax
  pop  ax
  call vga_set_virt_width

  ; set CRT Y resolution
  mov  dx, # VBE_DISPI_IOPORT_INDEX
  mov  ax, # VBE_DISPI_INDEX_YRES
  out  dx, ax
  mov  dx, # VBE_DISPI_IOPORT_DATA
  in   ax, dx
  dec  ax
  push ax
  mov  dx, # VGAREG_VGA_CRTC_ADDRESS
  mov  ah, al
  mov  al, #0x12
  out  dx, ax
  pop  ax
  mov  al, #0x07
  out  dx, al
  inc  dx
  in   al, dx
  and  al, #0xbd
  test ah, #0x01
  jz   bit8_clear
  or   al, #0x02
bit8_clear:
  test ah, #0x02
  jz   bit9_clear
  or   al, #0x40
bit9_clear:
  out  dx, al

  ; other settings
  mov  dx, # VGAREG_VGA_CRTC_ADDRESS
  mov  ax, #0x0009
  out  dx, ax
  mov  al, #0x17
  out  dx, al
  mov  dx, # VGAREG_VGA_CRTC_DATA
  in   al, dx
  or   al, #0x03
  out  dx, al
  mov  dx, # VGAREG_ACTL_RESET
  in   al, dx
  mov  dx, # VGAREG_ACTL_ADDRESS
  mov  al, #0x10
  out  dx, al
  mov  dx, # VGAREG_ACTL_READ_DATA
  in   al, dx
  or   al, #0x01
  mov  dx, # VGAREG_ACTL_ADDRESS
  out  dx, al
  mov  al, #0x20
  out  dx, al
  mov  dx, # VGAREG_GRDC_ADDRESS
  mov  ax, #0x0506
  out  dx, ax
  mov  dx, # VGAREG_SEQU_ADDRESS
  mov  ax, #0x0f02
  out  dx, ax

  ; settings for >= 8bpp
  mov  dx, # VBE_DISPI_IOPORT_INDEX
  mov  ax, # VBE_DISPI_INDEX_BPP
  out  dx, ax
  mov  dx, # VBE_DISPI_IOPORT_DATA
  in   ax, dx
  cmp  al, #0x08
  jb   vga_compat_end
  mov  dx, # VGAREG_VGA_CRTC_ADDRESS
  mov  al, #0x14
  out  dx, al
  mov  dx, # VGAREG_VGA_CRTC_DATA
  in   al, dx
  or   al, #0x40
  out  dx, al
  mov  dx, # VGAREG_ACTL_RESET
  in   al, dx
  mov  dx, # VGAREG_ACTL_ADDRESS
  mov  al, #0x10
  out  dx, al
  mov  dx, # VGAREG_ACTL_READ_DATA
  in   al, dx
  or   al, #0x40
  mov  dx, # VGAREG_ACTL_ADDRESS
  out  dx, al
  mov  al, #0x20
  out  dx, al
  mov  dx, # VGAREG_SEQU_ADDRESS
  mov  al, #0x04
  out  dx, al
  mov  dx, # VGAREG_SEQU_DATA
  in   al, dx
  or   al, #0x08
  out  dx, al
  mov  dx, # VGAREG_GRDC_ADDRESS
  mov  al, #0x05
  out  dx, al
  mov  dx, # VGAREG_GRDC_DATA
  in   al, dx
  and  al, #0x9f
  or   al, #0x40
  out  dx, al

vga_compat_end:
  pop  dx
  pop  ax
ASM_END


// ModeInfo helper function
static ModeInfoListItem* mode_info_find_mode(mode, using_lfb)
  Bit16u mode; Boolean using_lfb;
{
  ModeInfoListItem  *cur_info=&mode_info_list;

  while (cur_info->mode != VBE_VESA_MODE_END_OF_LIST)
  {
    if (cur_info->mode == mode)
    {
      if (!using_lfb)
      {
        return cur_info;
      }
      else if (cur_info->info.ModeAttributes & VBE_MODE_ATTRIBUTE_LINEAR_FRAME_BUFFER_MODE)
      {
        return cur_info;
      }
      else
      {
        cur_info++;
      }
    }
    else
    {
      cur_info++;
    }
  }

  return 0;
}

ASM_START

; Has VBE display - Returns true if VBE display detected

_vbe_has_vbe_display:
  push ds
  push bx
  mov  ax, # BIOSMEM_SEG
  mov  ds, ax
  mov  bx, # BIOSMEM_VBE_FLAG
  mov  al, [bx]
  and  al, #0x01
  xor  ah, ah
  pop  bx
  pop  ds
  ret

; VBE Init - Initialise the Vesa Bios Extension Code
; This function does a sanity check on the host side display code interface.

vbe_init:
  mov  ax, # VBE_DISPI_ID0
  call dispi_set_id
  call dispi_get_id
  cmp  ax, # VBE_DISPI_ID0
  jne  no_vbe_interface
  push ds
  push bx
  mov  ax, # BIOSMEM_SEG
  mov  ds, ax
  mov  bx, # BIOSMEM_VBE_FLAG
  mov  al, #0x01
  mov  [bx], al
  pop  bx
  pop  ds
  mov  ax, # VBE_DISPI_ID4
  call dispi_set_id
no_vbe_interface:
#if defined(USE_BX_INFO) || defined(DEBUG)
  mov  bx, #msg_vbe_init
  push bx
  call _printf
  inc  sp
  inc  sp
#endif
  ret

; VBE Display Info - Display information on screen about the VBE

vbe_display_info:
  call _vbe_has_vbe_display
  test ax, ax
  jz   no_vbe_flag
  mov  ax, #0xc000
  mov  ds, ax
  mov  si, #_vbebios_info_string
  jmp  _display_string
no_vbe_flag:
  mov  ax, #0xc000
  mov  ds, ax
  mov  si, #_no_vbebios_info_string
  jmp  _display_string
ASM_END  

/** Function 00h - Return VBE Controller Information
 * 
 * Input:
 *              AX      = 4F00h
 *              ES:DI   = Pointer to buffer in which to place VbeInfoBlock structure
 *                        (VbeSignature should be VBE2 when VBE 2.0 information is desired and
 *                        the info block is 512 bytes in size)
 * Output:
 *              AX      = VBE Return Status
 * 
 */
void vbe_biosfn_return_controller_information(AX, ES, DI)
Bit16u *AX;Bit16u ES;Bit16u DI;
{
        Bit16u            ss=get_SS();
        VbeInfoBlock      vbe_info_block;
        Bit16u            status;
        Bit16u            result;
        Bit16u            vbe2_info;
        Bit16u            cur_mode=0;
        Bit16u            cur_ptr=34;
        ModeInfoListItem  *cur_info=&mode_info_list;
        
        status = read_word(ss, AX);
        
#ifdef DEBUG
        printf("VBE vbe_biosfn_return_vbe_info ES%x DI%x AX%x\n",ES,DI,status);
#endif

        vbe2_info = 0;
#ifdef VBE2_NO_VESA_CHECK
#else
        // get vbe_info_block into local variable
        memcpyb(ss, &vbe_info_block, ES, DI, sizeof(vbe_info_block));

        // check for VBE2 signature
        if (((vbe_info_block.VbeSignature[0] == 'V') &&
             (vbe_info_block.VbeSignature[1] == 'B') &&
             (vbe_info_block.VbeSignature[2] == 'E') &&
             (vbe_info_block.VbeSignature[3] == '2')) ||
             
            ((vbe_info_block.VbeSignature[0] == 'V') &&
             (vbe_info_block.VbeSignature[1] == 'E') &&
             (vbe_info_block.VbeSignature[2] == 'S') &&
             (vbe_info_block.VbeSignature[3] == 'A')) )
        {
                vbe2_info = 1;
#ifdef DEBUG
                printf("VBE correct VESA/VBE2 signature found\n");
#endif
        }
#endif
                
        // VBE Signature
        vbe_info_block.VbeSignature[0] = 'V';
        vbe_info_block.VbeSignature[1] = 'E';
        vbe_info_block.VbeSignature[2] = 'S';
        vbe_info_block.VbeSignature[3] = 'A';
        
        // VBE Version supported
        vbe_info_block.VbeVersion = 0x0200;
        
        // OEM String
        vbe_info_block.OemStringPtr_Seg = 0xc000;
        vbe_info_block.OemStringPtr_Off = &vbebios_copyright;
        
        // Capabilities
        vbe_info_block.Capabilities[0] = VBE_CAPABILITY_8BIT_DAC;
        vbe_info_block.Capabilities[1] = 0;
        vbe_info_block.Capabilities[2] = 0;
        vbe_info_block.Capabilities[3] = 0;

        // VBE Video Mode Pointer (dynamicly generated from the mode_info_list)
        vbe_info_block.VideoModePtr_Seg= ES ;
        vbe_info_block.VideoModePtr_Off= DI + 34;

        // VBE Total Memory (in 64b blocks)
        vbe_info_block.TotalMemory = VBE_TOTAL_VIDEO_MEMORY_DIV_64K;

        if (vbe2_info)
	{
                // OEM Stuff
                vbe_info_block.OemSoftwareRev = VBE_OEM_SOFTWARE_REV;
                vbe_info_block.OemVendorNamePtr_Seg = 0xc000;
                vbe_info_block.OemVendorNamePtr_Off = &vbebios_vendor_name;
                vbe_info_block.OemProductNamePtr_Seg = 0xc000;
                vbe_info_block.OemProductNamePtr_Off = &vbebios_product_name;
                vbe_info_block.OemProductRevPtr_Seg = 0xc000;
                vbe_info_block.OemProductRevPtr_Off = &vbebios_product_revision;

                // copy updates in vbe_info_block back
                memcpyb(ES, DI, ss, &vbe_info_block, sizeof(vbe_info_block));
        }
	else
	{
                // copy updates in vbe_info_block back (VBE 1.x compatibility)
                memcpyb(ES, DI, ss, &vbe_info_block, 256);
	}
                
        do
        {
                if ((cur_info->info.XResolution <= dispi_get_max_xres()) &&
                    (cur_info->info.BitsPerPixel <= dispi_get_max_bpp())) {
#ifdef DEBUG
                  printf("VBE found mode %x => %x\n", cur_info->mode,cur_mode);
#endif
                  write_word(ES, DI + cur_ptr, cur_info->mode);
                  cur_mode++;
                  cur_ptr+=2;
                }
                cur_info++;
        } while (cur_info->mode != VBE_VESA_MODE_END_OF_LIST);
        
        // Add vesa mode list terminator
        write_word(ES, DI + cur_ptr, cur_info->mode);

        result = 0x4f;

        write_word(ss, AX, result);
}


/** Function 01h - Return VBE Mode Information
 * 
 * Input:
 *              AX      = 4F01h
 *              CX      = Mode Number
 *              ES:DI   = Pointer to buffer in which to place ModeInfoBlock structure
 * Output:
 *              AX      = VBE Return Status
 * 
 */
void vbe_biosfn_return_mode_information(AX, CX, ES, DI)
Bit16u *AX;Bit16u CX; Bit16u ES;Bit16u DI;
{
        Bit16u            result=0x0100;
        Bit16u            ss=get_SS();
        ModeInfoBlock     info;
        ModeInfoListItem  *cur_info;
        Boolean           using_lfb;

#ifdef DEBUG
        printf("VBE vbe_biosfn_return_mode_information ES%x DI%x CX%x\n",ES,DI,CX);
#endif

        using_lfb=((CX & VBE_MODE_LINEAR_FRAME_BUFFER) == VBE_MODE_LINEAR_FRAME_BUFFER);
        
        CX = (CX & 0x1ff);
        
        cur_info = mode_info_find_mode(CX, using_lfb, &cur_info);

        if (cur_info != 0)
        {
#ifdef DEBUG
                printf("VBE found mode %x\n",CX);
#endif        
                memsetb(ss, &info, 0, sizeof(ModeInfoBlock));
                memcpyb(ss, &info, 0xc000, &(cur_info->info), sizeof(ModeInfoBlockCompact));
                if (info.WinAAttributes & VBE_WINDOW_ATTRIBUTE_RELOCATABLE) {
                  info.WinFuncPtr = 0xC0000000UL;
                  *(Bit16u *)&(info.WinFuncPtr) = (Bit16u)(dispi_set_bank_farcall);
                }
                
                result = 0x4f;
        }
        else
        {
#ifdef DEBUG
                printf("VBE *NOT* found mode %x\n",CX);
#endif
                result = 0x100;
        }
        
        if (result == 0x4f)
        {
                // copy updates in mode_info_block back
                memcpyb(ES, DI, ss, &info, sizeof(info));
        }

        write_word(ss, AX, result);
}

/** Function 02h - Set VBE Mode
 * 
 * Input:
 *              AX      = 4F02h
 *              BX      = Desired Mode to set
 *              ES:DI   = Pointer to CRTCInfoBlock structure
 * Output:
 *              AX      = VBE Return Status
 * 
 */
void vbe_biosfn_set_mode(AX, BX, ES, DI)
Bit16u *AX;Bit16u BX; Bit16u ES;Bit16u DI;
{
        Bit16u            ss = get_SS();
        Bit16u            result;
        ModeInfoListItem  *cur_info;
        Boolean           using_lfb;
        Bit8u             no_clear;
        Bit8u             lfb_flag;

        using_lfb=((BX & VBE_MODE_LINEAR_FRAME_BUFFER) == VBE_MODE_LINEAR_FRAME_BUFFER);
        lfb_flag=using_lfb?VBE_DISPI_LFB_ENABLED:0;
        no_clear=((BX & VBE_MODE_PRESERVE_DISPLAY_MEMORY) == VBE_MODE_PRESERVE_DISPLAY_MEMORY)?VBE_DISPI_NOCLEARMEM:0;

        BX = (BX & 0x1ff);

        //result=read_word(ss,AX);
        
        // check for non vesa mode
        if (BX<VBE_MODE_VESA_DEFINED)
        {
                Bit8u   mode;
                
                dispi_set_enable(VBE_DISPI_DISABLED);
                // call the vgabios in order to set the video mode
                // this allows for going back to textmode with a VBE call (some applications expect that to work)
                
                mode=(BX & 0xff);
                biosfn_set_video_mode(mode);
                result = 0x4f;
        }
        
        cur_info = mode_info_find_mode(BX, using_lfb, &cur_info);

        if (cur_info != 0)
        {
#ifdef DEBUG
                printf("VBE found mode %x, setting:\n", BX);
                printf("\txres%x yres%x bpp%x\n",
                        cur_info->info.XResolution,
                        cur_info->info.YResolution,
                        cur_info->info.BitsPerPixel);
#endif
                
                // first disable current mode (when switching between vesa modi)
                dispi_set_enable(VBE_DISPI_DISABLED);

                if (cur_info->info.BitsPerPixel == 4)
                {
                  biosfn_set_video_mode(0x6a);
                }

                dispi_set_bpp(cur_info->info.BitsPerPixel);
                dispi_set_xres(cur_info->info.XResolution);
                dispi_set_yres(cur_info->info.YResolution);
                dispi_set_bank(0);
                dispi_set_enable(VBE_DISPI_ENABLED | no_clear | lfb_flag);
                vga_compat_setup();

                write_word(BIOSMEM_SEG,BIOSMEM_VBE_MODE,BX);
                write_byte(BIOSMEM_SEG,BIOSMEM_VIDEO_CTL,(0x60 | no_clear));

                result = 0x4f;                  
        }
        else
        {
#ifdef DEBUG
                printf("VBE *NOT* found mode %x\n" , BX);
#endif        
                result = 0x100;
                
                // FIXME: redirect non VBE modi to normal VGA bios operation
                //        (switch back to VGA mode
                if (BX == 3)
                        result = 0x4f;
        }

        write_word(ss, AX, result);
}

/** Function 03h - Return Current VBE Mode
 * 
 * Input:
 *              AX      = 4F03h
 * Output:
 *              AX      = VBE Return Status
 *              BX      = Current VBE Mode
 * 
 */
ASM_START
vbe_biosfn_return_current_mode:
  push ds
  mov  ax, # BIOSMEM_SEG
  mov  ds, ax
  call dispi_get_enable
  and  ax, # VBE_DISPI_ENABLED
  jz   no_vbe_mode
  mov  bx, # BIOSMEM_VBE_MODE
  mov  ax, [bx]
  mov  bx, ax
  jnz  vbe_03_ok
no_vbe_mode:
  mov  bx, # BIOSMEM_CURRENT_MODE
  mov  al, [bx]
  mov  bl, al
  xor  bh, bh
vbe_03_ok:
  mov  ax, #0x004f
  pop  ds
  ret
ASM_END


Bit16u vbe_biosfn_read_video_state_size()
{
    return 9 * 2;
}

void vbe_biosfn_save_video_state(ES, BX)
     Bit16u ES; Bit16u BX;
{
    Bit16u enable, i;

    outw(VBE_DISPI_IOPORT_INDEX,VBE_DISPI_INDEX_ENABLE);
    enable = inw(VBE_DISPI_IOPORT_DATA);
    write_word(ES, BX, enable);
    BX += 2;
    if (!(enable & VBE_DISPI_ENABLED)) 
        return;
    for(i = VBE_DISPI_INDEX_XRES; i <= VBE_DISPI_INDEX_Y_OFFSET; i++) {
        if (i != VBE_DISPI_INDEX_ENABLE) {
            outw(VBE_DISPI_IOPORT_INDEX, i);
            write_word(ES, BX, inw(VBE_DISPI_IOPORT_DATA));
            BX += 2;
        }
    }
}


void vbe_biosfn_restore_video_state(ES, BX)
     Bit16u ES; Bit16u BX;
{
    Bit16u enable, i;

    enable = read_word(ES, BX);
    BX += 2;
    
    if (!(enable & VBE_DISPI_ENABLED)) {
        outw(VBE_DISPI_IOPORT_INDEX,VBE_DISPI_INDEX_ENABLE);
        outw(VBE_DISPI_IOPORT_DATA, enable);
    } else {
        outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_XRES);
        outw(VBE_DISPI_IOPORT_DATA, read_word(ES, BX));
        BX += 2;
        outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_YRES);
        outw(VBE_DISPI_IOPORT_DATA, read_word(ES, BX));
        BX += 2;
        outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_BPP);
        outw(VBE_DISPI_IOPORT_DATA, read_word(ES, BX));
        BX += 2;
        outw(VBE_DISPI_IOPORT_INDEX,VBE_DISPI_INDEX_ENABLE);
        outw(VBE_DISPI_IOPORT_DATA, enable);

        for(i = VBE_DISPI_INDEX_BANK; i <= VBE_DISPI_INDEX_Y_OFFSET; i++) {
            outw(VBE_DISPI_IOPORT_INDEX, i);
            outw(VBE_DISPI_IOPORT_DATA, read_word(ES, BX));
            BX += 2;
        }
    }
}

/** Function 04h - Save/Restore State
 * 
 * Input:
 *              AX      = 4F04h
 *              DL      = 00h Return Save/Restore State buffer size
 *                        01h Save State
 *                        02h Restore State
 *              CX      = Requested states
 *              ES:BX   = Pointer to buffer (if DL <> 00h)
 * Output:
 *              AX      = VBE Return Status
 *              BX      = Number of 64-byte blocks to hold the state buffer (if DL=00h)
 * 
 */
void vbe_biosfn_save_restore_state(AX, CX, DX, ES, BX)
Bit16u *AX; Bit16u CX; Bit16u DX; Bit16u ES; Bit16u *BX;
{
    Bit16u ss=get_SS();
    Bit16u result, val;

    result = 0x4f;
    switch(GET_DL()) {
    case 0x00:
        val = biosfn_read_video_state_size2(CX);
#ifdef DEBUG
        printf("VGA state size=%x\n", val);
#endif
        if (CX & 8)
            val += vbe_biosfn_read_video_state_size();
        write_word(ss, BX, val);
        break;
    case 0x01:
        val = read_word(ss, BX);
        val = biosfn_save_video_state(CX, ES, val);
#ifdef DEBUG
        printf("VGA save_state offset=%x\n", val);
#endif
        if (CX & 8)
            vbe_biosfn_save_video_state(ES, val);
        break;
    case 0x02:
        val = read_word(ss, BX);
        val = biosfn_restore_video_state(CX, ES, val);
#ifdef DEBUG
        printf("VGA restore_state offset=%x\n", val);
#endif
        if (CX & 8)
            vbe_biosfn_restore_video_state(ES, val);
        break;
    default:
        // function failed
        result = 0x100;
        break;
    }
    write_word(ss, AX, result);
}

/** Function 05h - Display Window Control
 * 
 * Input:
 *              AX      = 4F05h
 *     (16-bit) BH      = 00h Set memory window
 *                      = 01h Get memory window
 *              BL      = Window number
 *                      = 00h Window A
 *                      = 01h Window B
 *              DX      = Window number in video memory in window
 *                        granularity units (Set Memory Window only)
 * Note:
 *              If this function is called while in a linear frame buffer mode,
 *              this function must fail with completion code AH=03h
 * 
 * Output:
 *              AX      = VBE Return Status
 *              DX      = Window number in window granularity units
 *                        (Get Memory Window only)
 */
ASM_START
vbe_biosfn_display_window_control:
  cmp  bl, #0x00
  jne  vbe_05_failed
  cmp  bh, #0x01
  je   get_display_window
  jb   set_display_window
  mov  ax, #0x0100
  ret
set_display_window:
  mov  ax, dx
  call _dispi_set_bank
  call dispi_get_bank
  cmp  ax, dx
  jne  vbe_05_failed
  mov  ax, #0x004f
  ret
get_display_window:
  call dispi_get_bank
  mov  dx, ax
  mov  ax, #0x004f
  ret
vbe_05_failed:
  mov  ax, #0x014f
  ret
ASM_END


/** Function 06h - Set/Get Logical Scan Line Length
 *
 * Input:
 *              AX      = 4F06h
 *              BL      = 00h Set Scan Line Length in Pixels
 *                      = 01h Get Scan Line Length
 *                      = 02h Set Scan Line Length in Bytes
 *                      = 03h Get Maximum Scan Line Length
 *              CX      = If BL=00h Desired Width in Pixels
 *                        If BL=02h Desired Width in Bytes
 *                        (Ignored for Get Functions)
 * 
 * Output: 
 *              AX      = VBE Return Status
 *              BX      = Bytes Per Scan Line
 *              CX      = Actual Pixels Per Scan Line
 *                        (truncated to nearest complete pixel)
 *              DX      = Maximum Number of Scan Lines 
 */
ASM_START
vbe_biosfn_set_get_logical_scan_line_length:
  mov  ax, cx
  cmp  bl, #0x01
  je   get_logical_scan_line_length
  cmp  bl, #0x02
  je   set_logical_scan_line_bytes
  jb   set_logical_scan_line_pixels
  mov  ax, #0x0100
  ret
set_logical_scan_line_bytes:
  push ax
  call dispi_get_bpp
  xor  bh, bh
  mov  bl, ah
  or   bl, bl
  jnz  no_4bpp_1
  shl  ax, #3
  mov  bl, #1
no_4bpp_1:
  xor  dx, dx
  pop  ax
  div  bx
set_logical_scan_line_pixels:
  call dispi_set_virt_width
get_logical_scan_line_length:
  call dispi_get_bpp
  xor  bh, bh
  mov  bl, ah
  call dispi_get_virt_width
  mov  cx, ax
  or   bl, bl
  jnz  no_4bpp_2
  shr  ax, #3
  mov  bl, #1
no_4bpp_2:
  mul  bx
  mov  bx, ax
  call dispi_get_virt_height
  mov  dx, ax
  mov  ax, #0x004f
  ret
ASM_END


/** Function 07h - Set/Get Display Start
 * 
 * Input(16-bit):
 *              AX      = 4F07h
 *              BH      = 00h Reserved and must be 00h
 *              BL      = 00h Set Display Start
 *                      = 01h Get Display Start
 *                      = 02h Schedule Display Start (Alternate)
 *                      = 03h Schedule Stereoscopic Display Start
 *                      = 04h Get Scheduled Display Start Status
 *                      = 05h Enable Stereoscopic Mode
 *                      = 06h Disable Stereoscopic Mode
 *                      = 80h Set Display Start during Vertical Retrace
 *                      = 82h Set Display Start during Vertical Retrace (Alternate)
 *                      = 83h Set Stereoscopic Display Start during Vertical Retrace
 *              ECX     = If BL=02h/82h Display Start Address in bytes
 *                        If BL=03h/83h Left Image Start Address in bytes
 *              EDX     = If BL=03h/83h Right Image Start Address in bytes
 *              CX      = If BL=00h/80h First Displayed Pixel In Scan Line
 *              DX      = If BL=00h/80h First Displayed Scan Line
 *
 * Output:
 *              AX      = VBE Return Status
 *              BH      = If BL=01h Reserved and will be 0
 *              CX      = If BL=01h First Displayed Pixel In Scan Line
 *                        If BL=04h 0 if flip has not occurred, not 0 if it has
 *              DX      = If BL=01h First Displayed Scan Line
 *
 * Input(32-bit): 
 *              BH      = 00h Reserved and must be 00h
 *              BL      = 00h Set Display Start
 *                      = 80h Set Display Start during Vertical Retrace
 *              CX      = Bits 0-15 of display start address
 *              DX      = Bits 16-31 of display start address
 *              ES      = Selector for memory mapped registers 
 */
ASM_START
vbe_biosfn_set_get_display_start:
  cmp  bl, #0x80
  je   set_display_start
  cmp  bl, #0x01
  je   get_display_start
  jb   set_display_start
  mov  ax, #0x0100
  ret
set_display_start:
  mov  ax, cx
  call dispi_set_x_offset
  mov  ax, dx
  call dispi_set_y_offset
  mov  ax, #0x004f
  ret
get_display_start:
  call dispi_get_x_offset
  mov  cx, ax
  call dispi_get_y_offset
  mov  dx, ax
  xor  bh, bh
  mov  ax, #0x004f
  ret
ASM_END
  

/** Function 08h - Set/Get Dac Palette Format
 * 
 * Input:
 *              AX      = 4F08h
 *              BL      = 00h set DAC palette width
 *                      = 01h get DAC palette width
 *              BH      = If BL=00h: desired number of bits per primary color
 * Output:
 *              AX      = VBE Return Status
 *              BH      = current number of bits per primary color (06h = standard VGA)
 */
ASM_START
vbe_biosfn_set_get_dac_palette_format:
  cmp  bl, #0x01
  je   get_dac_palette_format
  jb   set_dac_palette_format
  mov  ax, #0x0100
  ret
set_dac_palette_format:
  call dispi_get_enable
  cmp  bh, #0x06
  je   set_normal_dac
  cmp  bh, #0x08
  jne  vbe_08_unsupported
  or   ax, # VBE_DISPI_8BIT_DAC
  jnz  set_dac_mode
set_normal_dac:
  and  ax, #~ VBE_DISPI_8BIT_DAC
set_dac_mode:
  call _dispi_set_enable
get_dac_palette_format:
  mov  bh, #0x06
  call dispi_get_enable
  and  ax, # VBE_DISPI_8BIT_DAC
  jz   vbe_08_ok
  mov  bh, #0x08
vbe_08_ok:
  mov  ax, #0x004f
  ret
vbe_08_unsupported:
  mov  ax, #0x014f
  ret
ASM_END


/** Function 09h - Set/Get Palette Data
 * 
 * Input:
 *              AX      = 4F09h
 * Output:
 *              AX      = VBE Return Status
 *
 * FIXME: incomplete API description, Input & Output
 */
void vbe_biosfn_set_get_palette_data(AX)
{
}

/** Function 0Ah - Return VBE Protected Mode Interface
 * Input:    AX   = 4F0Ah   VBE 2.0 Protected Mode Interface
 *           BL   = 00h          Return protected mode table
 *
 *
 * Output:   AX   =         Status
 *           ES   =         Real Mode Segment of Table
 *           DI   =         Offset of Table
 *           CX   =         Length of Table including protected mode code
 *                          (for copying purposes)
 */
ASM_START
vbe_biosfn_return_protected_mode_interface:
  test bl, bl
  jnz _fail
  mov di, #0xc000
  mov es, di
  mov di, # vesa_pm_start
  mov cx, # vesa_pm_end
  sub cx, di
  mov ax, #0x004f
  ret
_fail:
  mov ax, #0x014f
  ret
ASM_END
