@echo.
@echo Copyright (c) 2004  Microsoft Corporation
@echo Module Name: SHADOW_TEST_XP.CMD
@echo.
@echo Abstract:
@echo    Test scenarios for shadow.exe for Windows XP
@echo.
@echo Notes:
@echo   Alter the script as needed.
@echo	HW_DRIVE_1 is configured using hardware simulator
@echo   TgtVol is any NTFS volume
@echo   EXPOSE_AS_DRIVE is any drive letter not used in the system
@echo.

@rem------General Settings-----


@if "%1"=="" @goto :USAGE 
@if /i "%1"=="%SYSTEMDRIVE%" @goto :USAGE 

@goto :TEST

:USAGE
 @echo.
 @echo Usage:
 @echo   SHADOW_TEST_XP.CMD [drive_letter]
 @echo.
 @echo (where the drive letter is not the system volume)
 @echo.
@goto :EOF


:TEST

set Tgtdir=%~dp0..\..\bin\debug-xp
set TgtVol=%1
set SYSTEM_DRIVE=%SYSTEMDRIVE%
set Execdir=%~dp0


@echo.
@echo "Scenario 1 : Summary of Writer Metadata..." > shadow_xp.log
@echo.
%Tgtdir%\vshadow -wm
if ERRORLEVEL 1 @echo ERROR![%errorlevel%] & @goto:EOF




@echo.
echo "Scenario 2 : Verify Component is NOT included" >> shadow_xp.log
@echo.
%Tgtdir%\vshadow -wi="WMI Writer" -script=%Tgtdir%\setvar1.cmd %TgtVol% >> shadow_xp.log
REM if ERRORLEVEL 1 @echo ERROR![%errorlevel%] & @goto:EOF
if ERRORLEVEL 1 @echo "ERROR [%ERRORLEVEL%] is expected, please check the command for details." >> shadow_xp.log


echo "Scenario 3 : Verify component IS included" >> shadow_xp.log
%Tgtdir%\vshadow -wi="WMI Writer" -script=%Tgtdir%\setvar1.cmd   %SYSTEM_DRIVE%  >> shadow_xp.log
if ERRORLEVEL 1 @echo ERROR![%errorlevel%] & @goto:EOF


@echo.
echo "Scenario 4 : Exclude a component..."  >> shadow_xp.log
%Tgtdir%\vshadow -wx="WMI Writer" -script=%Tgtdir%\setvar1.cmd  %SYSTEM_DRIVE%  >> shadow_xp.log
if ERRORLEVEL 1 @echo ERROR![%errorlevel%] & @goto:EOF


@echo.
echo "Scenario 5 : Wait for user interaction before exiting"  >> shadow_xp.log
%Tgtdir%\vshadow -script=%Tgtdir%\setvar1.cmd %TgtVol%   >> shadow_xp.log
Echo y | %Tgtdir%\vshadow -wait -q  >> shadow_xp.log
if ERRORLEVEL 1 @echo ERROR![%errorlevel%] & @goto:EOF  


@echo.
echo "Scenario 6 : Tracing enabled..."  >> shadow_xp.log
%Tgtdir%\vshadow -tracing -script=%Tgtdir%\setvar1.cmd %TgtVol%  >> shadow_xp.log
if ERRORLEVEL 1 @echo ERROR![%errorlevel%] & @goto:EOF


@echo.
echo "Scenario 7 : Query ALL volumes..."  >> shadow_xp.log
%Tgtdir%\vshadow  -q			>> shadow_xp.log
if ERRORLEVEL 1 @echo ERROR![%errorlevel%] & @goto:EOF


@echo.
echo "Scenario 13 : Query shadowset..." >> shadow_xp.log
%Tgtdir%\vshadow -script=%Tgtdir%\setvar1.cmd -exec=%Execdir%query.cmd %TgtVol% %SYSTEM_DRIVE% >> shadow_xp.log
if ERRORLEVEL 1 @echo ERROR![%errorlevel%] & @goto:EOF


@echo.
echo "Scenario 14 : Query shadow..."  >> shadow_xp.log
%Tgtdir%\vshadow -script=%Tgtdir%\setvar1.cmd -exec=%Execdir%queryShadow.cmd %TgtVol% %SYSTEM_DRIVE%  >> shadow_xp.log
if ERRORLEVEL 1 @echo ERROR![%errorlevel%] & @goto:EOF



@echo.
echo "Scenario 15 : List writers..."  >> shadow_xp.log
%Tgtdir%\vshadow -ws   >> shadow_xp.log
if ERRORLEVEL 1 @echo ERROR![%errorlevel%] & @goto:EOF


@echo.
echo "Scenario 16 : List writer metadata..."   >> shadow_xp.log
%Tgtdir%\vshadow -wm    >> shadow_xp.log
if ERRORLEVEL 1 @echo ERROR![%errorlevel%] & @goto:EOF


@echo.
echo "Scenario 17 : List FULL writer metadata..."  >> shadow_xp.log
%Tgtdir%\vshadow -wm2  >> shadow_xp.log
if ERRORLEVEL 1 @echo ERROR![%errorlevel%] & @goto:EOF


:EOF
@echo.
echo "Scenario LAST : Cleanup..."  >> shadow_xp.log
Echo y | %Tgtdir%\vshadow -da  >> shadow_xp.log
