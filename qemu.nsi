;!/usr/bin/makensis

; This NSIS script creates an installer for QEMU on Windows.

; Copyright (C) 2006-2012 Stefan Weil
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
!define URL     "https://www.qemu.org/"

!define UNINST_EXE "$INSTDIR\qemu-uninstall.exe"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT}"

!ifndef BINDIR
!define BINDIR nsis.tmp
!endif
!ifndef SRCDIR
!define SRCDIR .
!endif
!ifndef OUTFILE
!define OUTFILE "qemu-setup.exe"
!endif

; Optionally install documentation.
!ifndef CONFIG_DOCUMENTATION
!define CONFIG_DOCUMENTATION
!endif

; Use maximum compression.
SetCompressor /SOLID lzma

!include "MUI2.nsh"

; The name of the installer.
Name "QEMU"

; The file to write
OutFile "${OUTFILE}"

; The default installation directory.
!ifdef W64
InstallDir $PROGRAMFILES64\qemu
!else
InstallDir $PROGRAMFILES\qemu
!endif

; Registry key to check for directory (so if you install again, it will
; overwrite the old one automatically)
!ifdef W64
InstallDirRegKey HKLM "Software\qemu64" "Install_Dir"
!else
InstallDirRegKey HKLM "Software\qemu32" "Install_Dir"
!endif

; Request administrator privileges for Windows Vista.
RequestExecutionLevel admin

;--------------------------------
; Interface Settings.
;!define MUI_HEADERIMAGE "qemu-nsis.bmp"
; !define MUI_SPECIALBITMAP "qemu.bmp"
!define MUI_ICON "${SRCDIR}\pc-bios\qemu-nsis.ico"
!define MUI_UNICON "${SRCDIR}\pc-bios\qemu-nsis.ico"
!define MUI_WELCOMEFINISHPAGE_BITMAP "${SRCDIR}\pc-bios\qemu-nsis.bmp"
; !define MUI_HEADERIMAGE_BITMAP "qemu-install.bmp"
; !define MUI_HEADERIMAGE_UNBITMAP "qemu-uninstall.bmp"
; !define MUI_COMPONENTSPAGE_SMALLDESC
; !define MUI_WELCOMEPAGE_TEXT "Insert text here.$\r$\n$\r$\n$\r$\n$_CLICK"

;--------------------------------
; Pages.

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${SRCDIR}\COPYING"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_LINK "Visit the QEMU Wiki online!"
!define MUI_FINISHPAGE_LINK_LOCATION "${URL}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

;--------------------------------
; Languages.

!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "French"
!insertmacro MUI_LANGUAGE "German"

;--------------------------------

; The stuff to install.
;
; Remember to keep the "Uninstall" section in sync.

Section "${PRODUCT} (required)"

    SectionIn RO

    ; Set output path to the installation directory.
    SetOutPath "$INSTDIR"

    File "${SRCDIR}\Changelog"
    File "${SRCDIR}\COPYING"
    File "${SRCDIR}\COPYING.LIB"
    File "${SRCDIR}\README.rst"
    File "${SRCDIR}\VERSION"

    File "${BINDIR}\*.bmp"
    File "${BINDIR}\*.bin"
    File "${BINDIR}\*.dtb"
    File "${BINDIR}\*.fd"
    File "${BINDIR}\*.img"
    File "${BINDIR}\*.lid"
    File "${BINDIR}\*.ndrv"
    File "${BINDIR}\*.rom"
    File "${BINDIR}\openbios-*"

    File /r "${BINDIR}\keymaps"
!ifdef CONFIG_GTK
    File /r "${BINDIR}\share"
!endif

!ifdef W64
    SetRegView 64
!endif

    ; Write the installation path into the registry
    WriteRegStr HKLM SOFTWARE\${PRODUCT} "Install_Dir" "$INSTDIR"

    ; Write the uninstall keys for Windows
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "QEMU"
!ifdef DISPLAYVERSION
    WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${DISPLAYVERSION}"
!endif
    WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"${UNINST_EXE}"'
    WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
    WriteUninstaller "qemu-uninstall.exe"
SectionEnd

Section "Tools" SectionTools
    SetOutPath "$INSTDIR"
    File "${BINDIR}\qemu-img.exe"
    File "${BINDIR}\qemu-io.exe"
SectionEnd

SectionGroup "System Emulations" SectionSystem

!include "${BINDIR}\system-emulations.nsh"

SectionGroupEnd

!ifdef DLLDIR
Section "Libraries (DLL)" SectionDll
    SetOutPath "$INSTDIR"
    File "${DLLDIR}\*.dll"
SectionEnd
!endif

!ifdef CONFIG_DOCUMENTATION
Section "Documentation" SectionDoc
    SetOutPath "$INSTDIR"
    File "${BINDIR}\qemu-doc.html"
    CreateDirectory "$SMPROGRAMS\${PRODUCT}"
    CreateShortCut "$SMPROGRAMS\${PRODUCT}\User Documentation.lnk" "$INSTDIR\qemu-doc.html" "" "$INSTDIR\qemu-doc.html" 0
SectionEnd
!endif

; Optional section (can be disabled by the user)
Section "Start Menu Shortcuts" SectionMenu
    CreateDirectory "$SMPROGRAMS\${PRODUCT}"
    CreateShortCut "$SMPROGRAMS\${PRODUCT}\Uninstall.lnk" "${UNINST_EXE}" "" "${UNINST_EXE}" 0
SectionEnd

;--------------------------------

; Uninstaller

Section "Uninstall"
    ; Remove registry keys
!ifdef W64
    SetRegView 64
!endif
    DeleteRegKey HKLM "${UNINST_KEY}"
    DeleteRegKey HKLM SOFTWARE\${PRODUCT}

    ; Remove shortcuts, if any
    Delete "$SMPROGRAMS\${PRODUCT}\User Documentation.lnk"
    Delete "$SMPROGRAMS\${PRODUCT}\Technical Documentation.lnk"
    Delete "$SMPROGRAMS\${PRODUCT}\Uninstall.lnk"
    RMDir "$SMPROGRAMS\${PRODUCT}"

    ; Remove files and directories used
    Delete "$INSTDIR\Changelog"
    Delete "$INSTDIR\COPYING"
    Delete "$INSTDIR\COPYING.LIB"
    Delete "$INSTDIR\README.rst"
    Delete "$INSTDIR\VERSION"
    Delete "$INSTDIR\*.bmp"
    Delete "$INSTDIR\*.bin"
    Delete "$INSTDIR\*.dll"
    Delete "$INSTDIR\*.dtb"
    Delete "$INSTDIR\*.fd"
    Delete "$INSTDIR\*.img"
    Delete "$INSTDIR\*.lid"
    Delete "$INSTDIR\*.ndrv"
    Delete "$INSTDIR\*.rom"
    Delete "$INSTDIR\openbios-*"
    Delete "$INSTDIR\qemu-img.exe"
    Delete "$INSTDIR\qemu-io.exe"
    Delete "$INSTDIR\qemu.exe"
    Delete "$INSTDIR\qemu-system-*.exe"
    Delete "$INSTDIR\qemu-doc.html"
    RMDir /r "$INSTDIR\keymaps"
    RMDir /r "$INSTDIR\share"
    ; Remove generated files
    Delete "$INSTDIR\stderr.txt"
    Delete "$INSTDIR\stdout.txt"
    ; Remove uninstaller
    Delete "${UNINST_EXE}"
    RMDir "$INSTDIR"
SectionEnd

;--------------------------------

; Descriptions (mouse-over).
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SectionSystem}  "System emulation."
    !insertmacro MUI_DESCRIPTION_TEXT ${Section_alpha}  "Alpha system emulation."
    !insertmacro MUI_DESCRIPTION_TEXT ${Section_alphaw} "Alpha system emulation (GUI)."
    !insertmacro MUI_DESCRIPTION_TEXT ${Section_i386}   "PC i386 system emulation."
    !insertmacro MUI_DESCRIPTION_TEXT ${Section_i386w}  "PC i386 system emulation (GUI)."
    !insertmacro MUI_DESCRIPTION_TEXT ${SectionTools} "Tools."
!ifdef DLLDIR
    !insertmacro MUI_DESCRIPTION_TEXT ${SectionDll}   "Runtime Libraries (DLL)."
!endif
!ifdef CONFIG_DOCUMENTATION
    !insertmacro MUI_DESCRIPTION_TEXT ${SectionDoc}   "Documentation."
!endif
    !insertmacro MUI_DESCRIPTION_TEXT ${SectionMenu}  "Menu entries."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

;--------------------------------
; Functions.

Function .onInit
    !insertmacro MUI_LANGDLL_DISPLAY
FunctionEnd
