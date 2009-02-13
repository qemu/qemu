;!/usr/bin/makensis

; This NSIS script creates an installer for QEMU on Windows.

; Copyright (C) 2006-2009 Stefan Weil
;
; This program is free software: you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation, either version 2 of the License, or
; (at your option) any later version.
;
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program.  If not, see <http://www.gnu.org/licenses/>.

!define PRODUCT "QEMU"
!define UNINST_EXE "$INSTDIR\${PRODUCT}-uninstall.exe"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT}"

; The name of the installer
Name "QEMU"

; The file to write
OutFile "qemu-setup.exe"

; The default installation directory
InstallDir $PROGRAMFILES\qemu

; Registry key to check for directory (so if you install again, it will 
; overwrite the old one automatically)
InstallDirRegKey HKLM "Software\qemu" "Install_Dir"

;--------------------------------

; Pages

Page components
Page directory
Page instfiles

UninstPage uninstConfirm
UninstPage instfiles

;--------------------------------

; The stuff to install
Section "${PRODUCT} (required)"

  SectionIn RO

  ; Set output path to the installation directory.
  SetOutPath "$INSTDIR"

  File "i386-softmmu\qemu"
  ;;;File "SDL.dll"
  File "${SRC_PATH}/Changelog"
  File "${SRC_PATH}/COPYING"
  File "${SRC_PATH}/COPYING.LIB"
  File "${SRC_PATH}/README"
  File "${SRC_PATH}/VERSION"

  SetOutPath "$INSTDIR\keymaps"
  File /r /x .svn "${SRC_PATH}/keymaps"

  SetOutPath "$INSTDIR\pc-bios"
  File "${SRC_PATH}/pc-bios/bios.bin"
  File "${SRC_PATH}/pc-bios/vgabios.bin"
  File "${SRC_PATH}/pc-bios/vgabios-cirrus.bin"

  ; Write the installation path into the registry
  WriteRegStr HKLM SOFTWARE\${PRODUCT} "Install_Dir" "$INSTDIR"

  ; Write the uninstall keys for Windows
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "QEMU"
  WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"${UNINST_EXE}"'
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
  WriteUninstaller "${PRODUCT}-uninstall.exe"
SectionEnd

Section "${PRODUCT} Documentation"
  SetOutPath "$INSTDIR"
  File qemu-doc.html
  File qemu-tech.html
  CreateShortCut "$SMPROGRAMS\${PRODUCT}\User Documentation.lnk" "$INSTDIR\qemu-doc.html" "" "$INSTDIR\qemu-doc.html" 0
SectionEnd

; Optional section (can be disabled by the user)
Section "Start Menu Shortcuts"
  CreateDirectory "$SMPROGRAMS\${PRODUCT}"
  CreateShortCut "$SMPROGRAMS\${PRODUCT}\Uninstall.lnk" "${UNINST_EXE}" "" "${UNINST_EXE}" 0
SectionEnd

;--------------------------------

; Uninstaller

Section "Uninstall"
  ; Remove registry keys
  DeleteRegKey HKLM "${UNINST_KEY}"
  DeleteRegKey HKLM SOFTWARE\${PRODUCT}

  ; Remove files and uninstaller
  Delete "${UNINST_EXE}"

  ; Remove shortcuts, if any
  RMDir /r "$SMPROGRAMS\${PRODUCT}"

  ; Remove directories used
  RMDir /r "$INSTDIR"
SectionEnd
