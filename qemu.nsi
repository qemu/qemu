;!/usr/bin/makensis

; This NSIS script creates an installer for QEMU on Windows.

; Copyright (C) 2006-2011 Stefan Weil
;
; This program is free software: you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation, either version 2 of the License, or
; (at your option) version 3 or any later version.
;
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program.  If not, see <http://www.gnu.org/licenses/>.

; NSIS_WIN32_MAKENSIS

!define PRODUCT "QEMU"
!define UNINST_EXE "$INSTDIR\qemu-uninstall.exe"
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

    File "${SRCDIR}\Changelog"
    File "${SRCDIR}\COPYING"
    File "${SRCDIR}\COPYING.LIB"
    File "${SRCDIR}\README"
    File "${SRCDIR}\VERSION"

    File "${BINDIR}\*.bin"
    File "${BINDIR}\*.dll"
    File "${BINDIR}\*.dtb"
    File "${BINDIR}\*.rom"
    File "${BINDIR}\openbios-*"

    File /r "${BINDIR}\keymaps"
    File /r "${BINDIR}\qemu"

    ; Write the installation path into the registry
    WriteRegStr HKLM SOFTWARE\${PRODUCT} "Install_Dir" "$INSTDIR"

    ; Write the uninstall keys for Windows
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "QEMU"
    WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"${UNINST_EXE}"'
    WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
    WriteUninstaller "qemu-uninstall.exe"
SectionEnd

Section "${PRODUCT} Tools"
    SetOutPath "$INSTDIR"
    File "${BINDIR}\qemu-img.exe"
    File "${BINDIR}\qemu-io.exe"
SectionEnd

Section "${PRODUCT} PC (i386) System Emulation"
    SetOutPath "$INSTDIR"
    File "${BINDIR}\qemu.exe"
SectionEnd

Section "${PRODUCT} Other System Emulations"
    SetOutPath "$INSTDIR"
    File "${BINDIR}\qemu-system-*.exe"
SectionEnd

!ifdef CONFIG_DOCUMENTATION
Section "${PRODUCT} Documentation"
    SetOutPath "$INSTDIR"
    File qemu-doc.html
    File qemu-tech.html
    CreateDirectory "$SMPROGRAMS\${PRODUCT}"
    CreateShortCut "$SMPROGRAMS\${PRODUCT}\User Documentation.lnk" "$INSTDIR\qemu-doc.html" "" "$INSTDIR\qemu-doc.html" 0
    CreateShortCut "$SMPROGRAMS\${PRODUCT}\Technical Documentation.lnk" "$INSTDIR\qemu-tech.html" "" "$INSTDIR\qemu-tech.html" 0
SectionEnd
!endif

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

;--------------------------------

; Descriptions (mouse-over)
; !insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
;   !insertmacro MUI_DESCRIPTION_TEXT ${Section1} "xxx"
; !insertmacro MUI_FUNCTION_DESCRIPTION_END
