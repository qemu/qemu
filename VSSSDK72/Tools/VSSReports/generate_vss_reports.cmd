@setlocal
@set REPORTS_DIR=%systemdrive%\vssdiag

@echo.
@echo +------------------------------------------------+
@echo ^|                                                ^|
@echo ^|  Before gathering VSS related information      ^|
@echo ^|                                                ^|
@echo ^|  we need to stop the VSS services.             ^|
@echo ^|                                                ^|
@echo ^|  Please stop any VSS requestors now            ^|
@echo ^|  Press ^<ENTER^> when the requestors are closed  ^|
@echo ^|                                                ^|
@echo +------------------------------------------------+
@echo.
@pause


@echo.
@echo * Creating the VSS Reports directory ...
@echo.
if exist %REPORTS_DIR% ren %REPORTS_DIR% vssdiag.%RANDOM%
md %REPORTS_DIR%
if not exist %REPORTS_DIR% @echo ERROR: cannot create %REPORTS_DIR%  & @goto :EOF

@echo.
@echo * Gathering the mounted devices database ...
@echo.
REG.EXE export HKLM\SYSTEM\MountedDevices %REPORTS_DIR%\Mounted_devices.txt

@echo.
@echo * Gathering the mounted devices database ...
@echo.
schtasks /query /v /fo list > %REPORTS_DIR%\scheduled_tasks.txt

@echo.
@echo * Gathering the mountvol output ...
@echo.
mountvol > %REPORTS_DIR%\Mountvol_output.txt

@echo.
@echo * Gathering the VSS registry keys ... 
@echo.
REG.EXE export HKLM\SYSTEM\CurrentControlSet\Services\VSS %REPORTS_DIR%\vss_registry.txt 

@echo.
@echo * Gathering VSS-related information ...
@echo.
"%~dp0\vssagent.exe" -gather %REPORTS_DIR%\before_diag.xml

@echo.
@echo * Get the cluster log ...
@echo.
copy %windir%\cluster\cluster.log %REPORTS_DIR%\cluster.log

@echo.
@echo * Gathering the list of cluster resources ...
@echo.
cluster.exe resource /prop /priv /check /status > %REPORTS_DIR%\cluster_resources.txt

@echo.
@echo * Gathering the list of cluster dependencies ...
@echo.
cscript "%~dp0\get_dependencies.vbs" > %REPORTS_DIR%\cluster_res_dependencies.txt

@echo.
@echo * Stopping VSS services ...
@echo.
net stop vss
net stop swprv

@echo.
@echo * Enabling VSS lightweight tracing ...
@echo.
"%~dp0\vssagent.exe" -enablediag

@echo.
@echo * Enabling VSS full tracing ...
@echo.
call "%~dp0\vsstracing" -enable %REPORTS_DIR%\trace.txt

@echo.
@echo +---------------------------------------------------------------------------+
@echo ^|                                                                           ^|
@echo ^|  VSS tracing is now enabled on the %systemdrive% drive                               ^|
@echo ^|                                                                           ^|
@echo ^|  Please reproduce the problem now.                                        ^|
@echo ^|                                                                           ^|
@echo ^|  WARNING: In your repro, do not create shadow copies on the %systemdrive% drive      ^|
@echo ^|    since the trace file is located on this volume!                        ^|
@echo ^|    (otherwise your system will deadlock and a creation error will occur)  ^|
@echo ^|                                                                           ^|
@echo ^|    You can change the trace-file-path by modifying registry key           ^|
@echo ^|    HKLM\SYSTEM\CurrentControlSet\Services\VSS\Debug\Tracing\TraceFile     ^|
@echo ^|    You can disable full tracing by setting TraceLevel to 0                ^|
@echo ^|    HKLM\SYSTEM\CurrentControlSet\Services\VSS\Debug\Tracing\TraceLevel    ^|
@echo ^|                                                                           ^|
@echo ^|  Press ^<ENTER^> when the investigation is done.                            ^|
@echo ^|                                                                           ^|
@echo +---------------------------------------------------------------------------+
@echo.
@pause

@echo.
@echo * Stopping VSS services ...
@echo.
net stop vss
net stop swprv

@echo.
@echo * Disabling VSS full tracing ...
@echo.
call "%~dp0\vsstracing" -disable

@echo.
@echo * Disabling VSS lightweight tracing ...
@echo.
"%~dp0\vssagent.exe" -disablediag

@echo.
@echo * Gathering VSS-related information again after repro ...
@echo.
"%~dp0\vssagent.exe" -gather %REPORTS_DIR%\after_diag.xml


@echo.
@echo * Archiving the VSS Reports files ...
@echo.

@set BACKUP_DATE=%date:~4%_%date:~0,3%
@set BACKUP_DATE=%BACKUP_DATE:/=.%
@set BACKUP_TIME=%time:~0,8%
@set BACKUP_TIME=%BACKUP_TIME::=-%
@set BACKUP_TIME=%BACKUP_TIME: =0%
@set VSS_REPORTS_CAB=VSSREPORTS_%BACKUP_DATE%__%BACKUP_TIME%.cab

"%~dp0\CabArc.Exe" n %VSS_REPORTS_CAB% %REPORTS_DIR%\*

@echo.
@echo ====================================================
@echo. 
@echo     The VSS investigation is now done!
@echo.  
@echo     Please send the following file to Microsoft. 
@echo.  
@echo     %VSS_REPORTS_CAB% 
@echo. 
@echo ====================================================
@echo.
