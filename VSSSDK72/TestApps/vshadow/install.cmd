set VSNAP_DIRECTORY=%systemdrive%\VSHADOW
md %VSNAP_DIRECTORY%
xcopy /s /y "%~dp0*.*" %VSNAP_DIRECTORY%\
start %VSNAP_DIRECTORY%
pause