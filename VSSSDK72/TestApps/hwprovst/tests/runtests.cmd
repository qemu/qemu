rem @echo off

setlocal
set INSTALLTO=%SystemDrive%\VssHwTests\
set INSTALLFROM=%~dp0

if "%INSTALLFROM%"=="%INSTALLTO%" goto skipinstall

if not exist %INSTALLTO% mkdir %INSTALLTO%

xcopy /y "%INSTALLFROM%*.*" %INSTALLTO%*.*
xcopy /y "%INSTALLFROM%ini\*.*" %INSTALLTO%ini\*.*
xcopy /y "%INSTALLFROM%%PROCESSOR_ARCHITECTURE%\*.*" %INSTALLTO%*.*

:skipinstall

pushd %INSTALLTO%
vsstestcontroller -scenario={ini\hw-any.ini} -section={DEFAULT}

