rem @echo off
setlocal

if %PROCESSOR_ARCHITECTURE% == x86 goto goodproc

echo "This provider is provided only for x86 processors
goto done

:goodproc

rem Remove existing installation
call "%~dp0%uninstall-sampleprovider.cmd"

set INSTALLTO=%SystemDrive%\VssSampleProvider\
set INSTALLFROM=%~dp0

if %INSTALLTO% == %INSTALLFROM% goto skipcopy

if not exist %INSTALLTO% mkdir %INSTALLTO%

copy /y "%INSTALLFROM%*.*" %INSTALLTO%*.*

:skipcopy

pushd %INSTALLTO%

certmgr -add testroot.cer -r localMachine  -s Root

vstorcontrol install -inf %INSTALLTO%virtualstorage.inf
del %INSTALLTO%disk.image
vstorcontrol create fixeddisk -newimage %INSTALLTO%disk.image -blocks 40000

cscript register_app.vbs -register "VssSampleProvider" %INSTALLTO%VssSampleProvider.dll "VSS HW Sample Provider"

set EVENT_LOG=HKLM\SYSTEM\CurrentControlSet\Services\Eventlog\Application\VssSampleProvider
reg.exe add %EVENT_LOG% /f
reg.exe add %EVENT_LOG% /f /v CustomSource /t REG_DWORD /d 1
reg.exe add %EVENT_LOG% /f /v EventMessageFile /t REG_EXPAND_SZ /d %INSTALLTO%VssSampleProvider.dll
reg.exe add %EVENT_LOG% /f /v TypesSupported /t REG_DWORD /d 7

:done

