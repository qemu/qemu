//  APM BIOS support for the Bochs BIOS
//  Copyright (C) 2004 Fabrice Bellard
//
//  Debugging extensions, 16-bit interface and extended power options
//  Copyright (C) 2005 Struan Bartlett
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

#if defined(APM_REAL)
#define APMSYM(s) apmreal_ ## s
#elif defined(APM_PROT16)
#define APMSYM(s) apm16_ ## s
#elif defined(APM_PROT32)
#define APMSYM(s) apm32_ ## s
#else
#error unsupported APM mode
#endif

APMSYM(out_str):      
  push eax
  push ebx
  mov ebx, eax
APMSYM(out_str1):
  SEG CS
  mov al, byte ptr [bx]
  cmp al, #0
  je APMSYM(out_str2)
  outb dx, al
  inc ebx
  jmp APMSYM(out_str1)
APMSYM(out_str2):
  pop ebx
  pop eax
  ret
  
APMSYM(07_poweroff_str):
  .ascii "Shutdown"
  db 0
APMSYM(07_suspend_str):
  .ascii "Suspend"
  db 0
APMSYM(07_standby_str):
  .ascii "Standby"
  db 0
  
#if DEBUG_APM
APMSYM(put_str):      
  push edx
  mov dx, #INFO_PORT
  call APMSYM(out_str)
  pop edx
  ret
  
; print the hex number in eax
APMSYM(put_num):      
  push eax
  push ebx
  push ecx
  push edx
  mov ecx, eax
  mov bx, #8
  mov dx, #INFO_PORT
APMSYM(put_num1):
  mov eax, ecx
  shr eax, #28
  add al, #0x30
  cmp al, #0x39
  jbe APMSYM(put_num2)
  add al, #0x27
APMSYM(put_num2):
  outb dx, al
  shl ecx, #4
  dec bx
  jne APMSYM(put_num1)
  pop edx
  pop ecx
  pop ebx
  pop eax
  ret

APMSYM(put_reg):
  outb dx, al
  shr eax, #8
  outb dx, al
  shr eax, #8
  outb dx, al
  shr eax, #8
  outb dx, al
  
  mov eax,ebx
  call APMSYM(put_num)
  
  mov al, #0x3b
  outb dx,al
  mov al, #0x20
  outb dx,al
  ret  

APMSYM(put_regs):
  push eax
  push edx
  push ebx
  mov dx, #INFO_PORT
  
  mov ebx, eax
  mov eax, #0x3d584145 // 'EAX='
  call APMSYM(put_reg)
  pop ebx
  push ebx
  mov eax, #0x3d584245 // 'EBX='
  call APMSYM(put_reg)
  mov ebx, ecx
  mov eax, #0x3d584345 // 'ECX='
  call APMSYM(put_reg)
  mov ebx, edx
  mov eax, #0x3d584445 // 'EDX='
  call APMSYM(put_reg)
  mov ebx, esi
  mov eax, #0x3d495345 // 'ESI='
  call APMSYM(put_reg)
  mov ebx, edi
  mov eax, #0x3d494445 // 'EDI='
  call APMSYM(put_reg)
  
  mov al, #0x0a
  outb dx, al
  pop ebx
  pop edx
  pop eax
  ret
#endif

#if defined(APM_PROT32)
_apm32_entry:
#endif
#if defined(APM_PROT16)
_apm16_entry:
#endif
  pushf
  
#if defined(APM_REAL)
_apmreal_entry:
#endif

#if DEBUG_APM
  call APMSYM(put_regs)
#endif

#if defined(APM_REAL)
;-----------------
; APM installation check
APMSYM(00):
  cmp al, #0x00
  jne APMSYM(01)

  mov ah, #1 // APM major version
  mov al, #2 // APM minor version
  
  mov bh, #0x50 // 'P'
  mov bl, #0x4d // 'M'
  
  // bit 0 : 16 bit interface supported
  // bit 1 : 32 bit interface supported
  mov cx, #0x3
  jmp APMSYM(ok)
  
;-----------------
; APM real mode interface connect
APMSYM(01):
  cmp al, #0x01
  jne APMSYM(02)
  jmp APMSYM(ok)

;-----------------
; APM 16 bit protected mode interface connect
APMSYM(02):
  cmp al, #0x02
  jne APMSYM(03)

  mov bx, #_apm16_entry
  
  mov ax, #0xf000 // 16 bit code segment base
  mov si, #0xfff0 // 16 bit code segment size
  mov cx, #0xf000 // data segment address
  mov di, #0xfff0 // data segment length
  jmp APMSYM(ok)

;-----------------
; APM 32 bit protected mode interface connect
APMSYM(03):
  cmp al, #0x03
  jne APMSYM(04)
  mov ax, #0xf000 // 32 bit code segment base
  mov ebx, #_apm32_entry
  mov cx, #0xf000 // 16 bit code segment base
  // 32 bit code segment size (low 16 bits)
  // 16 bit code segment size (high 16 bits)
  mov esi, #0xfff0fff0
  mov dx, #0xf000 // data segment address
  mov di, #0xfff0 // data segment length
  jmp APMSYM(ok)
#endif

;-----------------
; APM interface disconnect
APMSYM(04):
  cmp al, #0x04
  jne APMSYM(05)
  jmp APMSYM(ok)

;-----------------
; APM cpu idle
APMSYM(05):
  cmp al, #0x05
  jne APMSYM(07)
  sti
  hlt
  jmp APMSYM(ok)

;-----------------
; APM Set Power State
APMSYM(07):
  cmp al, #0x07
  jne APMSYM(08)
  
  cmp bx, #1
  jne APMSYM(ok)
  
  cmp cx, #3
  je APMSYM(07_poweroff)
  
  cmp cx, #2
  je APMSYM(07_suspend)
  
  cmp cx, #1
  je APMSYM(07_standby)
  
  jne APMSYM(ok)
  
APMSYM(07_poweroff):  
  // send power off event to emulator
  cli
  mov dx, #0x8900
  mov ax, #APMSYM(07_poweroff_str)
  call APMSYM(out_str)

APMSYM(07_1):
  hlt
  jmp APMSYM(07_1)

APMSYM(07_suspend):
  push edx
  mov dx, #0x8900
  mov ax, #APMSYM(07_suspend_str)
  call APMSYM(out_str)
  pop edx
  jmp APMSYM(ok)

APMSYM(07_standby):
  push edx
  mov dx, #0x8900
  mov ax, #APMSYM(07_standby_str)
  call APMSYM(out_str)
  pop edx
  jmp APMSYM(ok)

;-----------------
; APM Enable / Disable
APMSYM(08):
  cmp al, #0x08
  jne APMSYM(0a)

  jmp APMSYM(ok)

;-----------------
; Get Power Status
APMSYM(0a):
  cmp al, #0x0a
  jne APMSYM(0b)
  mov bh, #0x01 // on line
  // mov bh, #0x02 // battery
  mov bl, #0xff // unknown battery status
  // mov bl, #0x03 // charging
  mov ch, #0x80 // no system battery
  // mov ch, #0x8 // charging
  mov cl, #0xff // unknown remaining time
  // mov cl, #50
  mov dx, #0xffff // unknown remaining time 
  mov si, #0      // zero battery
  // mov si, #1      // one battery
  jmp APMSYM(ok)

;-----------------
; Get PM Event
APMSYM(0b):
  cmp al, #0x0b
  jne APMSYM(0e)
  mov ah, #0x80 // no event pending
  jmp APMSYM(error)
   
;-----------------
; APM Driver Version
APMSYM(0e):
  cmp al, #0x0e
  jne APMSYM(0f)
  
  mov ah, #1
  mov al, #2
  
  jmp APMSYM(ok)

;-----------------
; APM Engage / Disengage
APMSYM(0f):
  cmp al, #0x0f
  jne APMSYM(10)

  jmp APMSYM(ok)

;-----------------
; APM Get Capabilities
APMSYM(10):
  cmp al, #0x10
  jne APMSYM(unimplemented)

  mov bl, #0
  mov cx, #0

  jmp APMSYM(ok)

;-----------------
APMSYM(ok):
  popf
  clc
#if defined(APM_REAL)
  jmp iret_modify_cf
#else
  retf  
#endif
APMSYM(unimplemented):
APMSYM(error):
  popf
  stc
#if defined(APM_REAL)
  jmp iret_modify_cf
#else
  retf
#endif

#undef APM_PROT32
#undef APM_PROT16
#undef APM_REAL
#undef APMSYM
