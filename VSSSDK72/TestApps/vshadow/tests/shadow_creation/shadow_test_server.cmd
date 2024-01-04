@rem -------------- Parameter Checking ----------
@rem "system drive or hardware Drive Letter"
@if /I "%1" == "" goto:Usage

@rem "drive letter..."
@if /I "%2" == ""  goto:Usage



@echo.
@echo Copyright (c) 2004  Microsoft Corporation
@echo Module Name: shadow_test_server.cmd
@echo.
@echo Abstract:
@echo    Test scenarios for vshadow.exe for Windows Server 2003
@echo.
@echo Notes:
@echo   Alter the script as needed.
@echo	HW_DRIVE_1 is configured using hardware simulator
@echo   TgtVol is any NTFS volume
@echo   EXPOSE_AS_DRIVE is any drive letter not used in the system
@echo.


@rem ------General Settings-----

@IF "%PROCESSOR_ARCHITECTURE%" EQU "x86" (@set BINSRC=release-server)
@IF "%PROCESSOR_ARCHITECTURE%" EQU "AMD64" (@set BINSRC=Debug-server-amd64)
@IF "%PROCESSOR_ARCHITECTURE%" EQU "IA64" (@set BINSRC=Debug-server-ia64)

set Tgtdir=%~dp0..\..\bin\%BINSRC%
set SYSTEM_DRIVE=C:
set Execdir=%~dp0

@echo.
echo "Delete all existing shadows in the system..."
@echo.
Echo y | %Tgtdir%\vshadow.exe -da >> shadow.log



@rem "Execute hardware or software related tests"
@if /I "%1" == "2" goto:Hardware_provider_Tests



@echo "Setting system drive..."
set TgtVol=%2
@echo %TgtVol%

@echo "Setting expose as drive letter..."

@rem "expose as drive letter..."
@if /I "%3" == ""  goto:Usage

set EXPOSE_AS_DRIVE=%3

@echo.
echo "Scenario 1 : Create persistent Shadow Copy..."  > shadow.log
@echo.
%Tgtdir%\vshadow -p -script=%Tgtdir%\setvar1.cmd  %TgtVol%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST 
call %Tgtdir%\setvar1.cmd >> shadow.log
%Tgtdir%\vshadow.exe -ds=%SHADOW_ID_1%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST


@echo.
echo "Scenario 2 : Create persistent Shadow Copy without writers..." >> shadow.log
@echo.
%Tgtdir%\vshadow -p -nw -script=%Tgtdir%\setvar1.cmd  %TgtVol%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST
call %Tgtdir%\setvar1.cmd  >> shadow.log
%Tgtdir%\vshadow.exe -ds=%SHADOW_ID_1%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST




@echo.
echo "Scenario 3 : Create Shadow Copy for Shared Folders..." >> shadow.log
@echo.
%Tgtdir%\vshadow -scsf -script=%Tgtdir%\setvar1.cmd  %TgtVol% >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST
call %Tgtdir%\setvar1.cmd >> shadow.log
%Tgtdir%\vshadow.exe -ds=%SHADOW_ID_1%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST

@echo.
echo "Scenario 4 : Summary of Writer Metadata..." >> shadow.log
@echo.
%Tgtdir%\vshadow -wm >> shadow.log >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST


@echo.
echo "Scenario 5 : Verify component is included:: Component NOT included" >> shadow.log
@echo.
%Tgtdir%\vshadow -wi="WMI Writer" -script=%Tgtdir%\setvar1.cmd  -p %TgtVol% >> shadow.log
REM if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST
if ERRORLEVEL 1 @echo "ERROR [%ERRORLEVEL%] is expected, please check the command for details." >> shadow.log
call %Tgtdir%\setvar1.cmd  >> shadow.log
@echo %SHADOW_ID_1%
%Tgtdir%\vshadow -ds=%SHADOW_ID_1%  >> shadow.log
REM if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST


@echo.
echo "Scenario 6 : Verify component is included:: Component IS included"  >> shadow.log
@echo.
%Tgtdir%\vshadow -wi="WMI Writer" -script=%Tgtdir%\setvar1.cmd  -p %SYSTEM_DRIVE%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST
call %Tgtdir%\setvar1.cmd  >> shadow.log
@echo %SHADOW_ID_1%
%Tgtdir%\vshadow -ds=%SHADOW_ID_1%  >> shadow.log
REM if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST

@echo.
echo "Scenario 7 : Exclude a component..." >> shadow.log
@echo.
%Tgtdir%\vshadow -wx="WMI Writer" -script=%Tgtdir%\setvar1.cmd  -p %SYSTEM_DRIVE% >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST
call %Tgtdir%\setvar1.cmd >> shadow.log
@echo %SHADOW_ID_1%
%Tgtdir%\vshadow -ds=%SHADOW_ID_1% >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST


@echo.
echo "Scenario 8 : Wait for user interaction before exiting" >> shadow.log
@echo.
%Tgtdir%\vshadow -script=%Tgtdir%\setvar1.cmd %TgtVol%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST
Echo y | %Tgtdir%\vshadow -wait -q >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST


@echo.
echo "Scenario 9 : Tracing enabled..." >> shadow.log
@echo.
%Tgtdir%\vshadow -tracing -scsf -script=%Tgtdir%\setvar1.cmd %TgtVol% >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST

@echo. 
echo "Scenario 10 : Query ALL volumes..." >> shadow.log
@echo.
%Tgtdir%\vshadow -p %TgtVol%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST
%Tgtdir%\vshadow  -q  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST


@echo.
echo "Scenario 11 : Query shadowset..." >> shadow.log
@echo.
%Tgtdir%\vshadow -script=%Tgtdir%\setvar1.cmd -p %TgtVol% %SYSTEM_DRIVE% >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST
call %Tgtdir%\setvar1.cmd  >> shadow.log
@echo %SHADOW_SET_ID%
%Tgtdir%\vshadow -qx=%SHADOW_SET_ID%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST


@echo.
echo "Scenario 12 : Query shadow..."  >> shadow.log
@echo.
%Tgtdir%\vshadow -script=%Tgtdir%\setvar1.cmd -p %TgtVol% %SYSTEM_DRIVE%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST
call %Tgtdir%\setvar1.cmd  >> shadow.log
@echo %SHADOW_ID_1%
@echo %SHADOW_ID_2%
%Tgtdir%\vshadow -s=%SHADOW_ID_1%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST
%Tgtdir%\vshadow -s=%SHADOW_ID_2%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST


@echo.
echo "Scenario 13 : Delete ALL shadows in the system..."  >> shadow.log
@echo.
%Tgtdir%\vshadow -script=%Tgtdir%\setvar1.cmd -p %TgtVol% %SYSTEM_DRIVE%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST
Echo y | %Tgtdir%\vshadow -da  >> shadow.log


@echo.
echo "Scenario 14 : Delete by shadowset..."   >> shadow.log
@echo.
%Tgtdir%\vshadow -script=%Tgtdir%\setvar1.cmd -p %TgtVol% %SYSTEM_DRIVE%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST
call %Tgtdir%\setvar1.cmd  >> shadow.log
@echo %SHADOW_SET_ID% 
%Tgtdir%\vshadow -dx=%SHADOW_SET_ID%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST
%Tgtdir%\vshadow -qx=%SHADOW_SET_ID%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST



@echo.
echo "Scenario 15 : Delete this shadow..."   >> shadow.log
@echo.
%Tgtdir%\vshadow -script=%Tgtdir%\setvar1.cmd -p %TgtVol% %SYSTEM_DRIVE%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST
call %Tgtdir%\setvar1.cmd  >> shadow.log
@echo %SHADOW_ID_1%
@echo %SHADOW_ID_2%
%Tgtdir%\vshadow -ds=%SHADOW_ID_1%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST
%Tgtdir%\vshadow -ds=%SHADOW_ID_2%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST





@echo.
echo "Scenario 16 : Expose Shadow copy locally (Drive Letter)..."  >> shadow.log
@echo.

%Tgtdir%\vshadow -script=%Tgtdir%\setvar1.cmd -p %TgtVol%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST
call %Tgtdir%\setvar1.cmd  >> shadow.log
@echo %SHADOW_ID_1%
%Tgtdir%\vshadow -el=%SHADOW_ID_1%,%EXPOSE_AS_DRIVE%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST


@echo.
echo "Scenario 17 : Expose Shadow copy locally (Mount Pt)..."  >> shadow.log
@echo.
%Tgtdir%\vshadow -script=%Tgtdir%\setvar1.cmd -p C: >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST
call %Tgtdir%\setvar1.cmd >> shadow.log
@echo %SHADOW_ID_1%
md %TgtVol%\mountA
%Tgtdir%\vshadow -el=%SHADOW_ID_1%,%TgtVol%\mountA >> shadow.log >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST


@echo.
echo "Scenario 18 : List writers..."  >> shadow.log
@echo.
%Tgtdir%\vshadow -ws >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST


@echo.
echo "Scenario 19 : List writer metadata..." >> shadow.log
@echo.
%Tgtdir%\vshadow -wm >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST


@echo.
echo "Scenario 20 : List FULL writer metadata..."  >> shadow.log
@echo.
%Tgtdir%\vshadow -wm2 >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST

:AllPass
@echo.
@echo "All Scenarios passed tests!"
@echo.

@echo "End of software provider tests... Cleaning up"
@goto :LAST




:Hardware_provider_Tests
@echo "Setting hardware drive..."
set HW_DRIVE_1="%2"
@echo %HW_DRIVE_1%


@echo.
echo "Scenario 1 : Create differential HW Shadow Copy..." >> shadow.log
@echo
%Tgtdir%\vshadow -ad -script=%Tgtdir%\setvar1.cmd  %HW_DRIVE_1%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST


@echo.
echo "Scenario 2 : Create Plex HW Shadow Copy..." >> shadow.log
@echo.
%Tgtdir%\vshadow -ap -script=%Tgtdir%\setvar1.cmd  %HW_DRIVE_1% >> shadow.log
REM if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST



@echo.
echo "Scenario 3 : Break shadow copy set (Persistent, Read Only)..."  >> shadow.log
@echo.
%Tgtdir%\vshadow -script=%Tgtdir%\setvar1.cmd -p %HW_DRIVE_1%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST
call %Tgtdir%\setvar1.cmd >> shadow.log
@echo %SHADOW_SET_ID%
%Tgtdir%\vshadow -b=%SHADOW_SET_ID% >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST


@echo.
echo "Scenario 4 : Break shadow copy set (Persistent, Read Write)..."  >> shadow.log
@echo.
%Tgtdir%\vshadow -script=%Tgtdir%\setvar1.cmd -p %HW_DRIVE_1%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST
call %Tgtdir%\setvar1.cmd  >> shadow.log
@echo %SHADOW_SET_ID%
%Tgtdir%\vshadow -bw=%SHADOW_SET_ID%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST


@echo.
echo "Scenario 5 : Break shadow copy set (Non-Persistent, Read Only)..."  >> shadow.log
@echo.
%Tgtdir%\vshadow  -script=%Tgtdir%\setvar1.cmd -exec=%Execdir%enum.cmd %HW_DRIVE_1%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST


@echo.
echo "Scenario 6 : Break shadow copy set (Non-Persistent, Read Write: )..."  >> shadow.log
@echo.
%Tgtdir%\vshadow  -script=%Tgtdir%\setvar1.cmd -exec=%Execdir%enumRW.cmd %HW_DRIVE_1%  >> shadow.log

@echo. 
echo "Scenario 7 : Break shadow copy set (Persistent, Transportable, Read Only)..."  >> shadow.log
@echo.
%Tgtdir%\vshadow -t=%Tgtdir%\trans.xml -script=%Tgtdir%\setvar1.cmd -p %HW_DRIVE_1%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST
call %Tgtdir%\setvar1.cmd  >> shadow.log
%Tgtdir%\vshadow -i=%Tgtdir%\trans.xml >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST
%Tgtdir%\vshadow -b=%SHADOW_SET_ID%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST


@echo.
echo "Scenario 8 : Break shadow copy set (Persistent, Transportable, ReadWrite)..."  >> shadow.log
@echo.
%Tgtdir%\vshadow -t=%Tgtdir%\trans.xml -script=%Tgtdir%\setvar1.cmd -p %HW_DRIVE_1%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST
call %Tgtdir%\setvar1.cmd  >> shadow.log
%Tgtdir%\vshadow -i=%Tgtdir%\trans.xml >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST
%Tgtdir%\vshadow -bw=%SHADOW_SET_ID%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST


@echo.
echo "Scenario 9 : Break shadow copy set (Non-Persistent, Transportable, Read Only)..."  >> shadow.log
@echo.
%Tgtdir%\vshadow -t=%Tgtdir%\trans.xml -script=%Tgtdir%\setvar1.cmd %HW_DRIVE_1% >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST
%Tgtdir%\vshadow -exec=%Execdir%enum.cmd -i=%Tgtdir%\trans.xml
  

@echo.
echo "Scenario 10 : Break shadow copy set (Non-Persistent, Transportable, ReadWrite)..."  >> shadow.log
@echo.
%Tgtdir%\vshadow -t=%Tgtdir%\trans.xml -script=%Tgtdir%\setvar1.cmd %HW_DRIVE_1%  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST
%Tgtdir%\vshadow -exec=%Execdir%enumRW.cmd -i=%Tgtdir%\trans.xml


@echo.
echo "Scenario 11 : Create transportable shadow copies (Persistent, basic disk)" >> shadow.log
@echo.
%Tgtdir%\vshadow -p -t=%Tgtdir%\t.xml %HW_DRIVE_1% >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST
@echo "Importing transportable shadow copy..."
%Tgtdir%\vshadow -i=%Tgtdir%\t.xml  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST


@echo.
echo "Scenario 12 : Create transportable shadow copies (Non-Persistent, basic disk)" >> shadow.log
@echo.
%Tgtdir%\vshadow -t=%Tgtdir%\t.xml %HW_DRIVE_1% >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST
@echo "Importing transportable shadow copy..."
%Tgtdir%\vshadow -i=%Tgtdir%\t.xml  >> shadow.log
if ERRORLEVEL 1 @echo ERROR! [%ERRORLEVEL%] & @goto :LAST


:AllPass
@echo.
@echo "All Scenarios passed tests!"
@echo.


:LAST
@echo.
@echo "Scenario : Cleanup..." >> shadow.log
@echo.
Echo y |  %Tgtdir%\vshadow -da  >> shadow.log
goto :EOF



:Usage
@echo.
@echo Error - Invalid Parameter
@echo Usage [test-type] [driveletter] {expose-as-drive}
@echo.
@echo.
@echo Running software provider tests:
@echo   shadow_test_server 1 [drive letter] [expose-as-drive]
@echo.
@echo.
@echo Running hardware provider tests:
@echo   shadow_test_server 2 [drive letter]
@echo.
@goto :EOF

:EOF