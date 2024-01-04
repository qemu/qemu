rem @echo off
setlocal

set INSTALLTO=%SystemDrive%\VssSampleProvider\
set INSTALLFROM=%~dp0

net stop vds
net stop vss
net stop swprv

pushd %INSTALLTO%

reg.exe delete HKLM\SYSTEM\CurrentControlSet\Services\Eventlog\Application\VssSampleProvider /f

cscript register_app.vbs -unregister "VssSampleProvider"
regsvr32 /s /u VssSampleProvider.dll

vstorcontrol uninstall

del %INSTALLTO%disk.image
