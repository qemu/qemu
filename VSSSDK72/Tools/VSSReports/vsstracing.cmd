@setlocal
@if "%1" == "-enable" goto :ENABLE
@if "%1" == "-disable" goto :DISABLE
@if "%1" == "-query" goto :QUERY
@goto :USAGE

:ENABLE
@rem enable tracing on a file and debugger

set VSS_TRACE_LEVEL=0xffffffff
@if not "%2" == "" @set VSS_TRACING_FILE=%2
@if not "%3" == "" @set VSS_TRACE_LEVEL=%3

@call :ADD_REG_KEY TraceFile 		REG_SZ 	  %VSS_TRACING_FILE%
@call :ADD_REG_KEY TraceLevel 		REG_DWORD %VSS_TRACE_LEVEL%
@call :ADD_REG_KEY TraceEnterExit 	REG_DWORD 1
@call :ADD_REG_KEY TraceToFile 		REG_DWORD 1
@call :ADD_REG_KEY TraceToDebugger 	REG_DWORD 1
@call :ADD_REG_KEY TraceFileLineInfo	REG_DWORD 1
@call :ADD_REG_KEY TraceForceFlush 	REG_DWORD 0
@goto :EOF

:DISABLE
reg delete HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\VSS\Debug\Tracing /f
@goto :EOF

:QUERY
reg query HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\VSS\Debug\Tracing /s
@goto :EOF


:ADD_REG_KEY
reg add HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\VSS\Debug\Tracing /v %1 /t %2 /d %3 /f
@goto :EOF

:USAGE 
@echo.
@echo  VSSTRACING.CMD 1.0
@echo.
@echo  Description:
@echo    Enables/disables VSS tracing
@echo.
@echo  Usage:
@echo.
@echo    1) Enabling vss tracing on a file and debugger:
@echo          vsstracing.cmd -enable [^<file_name^> [ ^<trace_level^> ]]
@echo       Defaults: 
@echo          vsstracing.cmd -enable c:\trace.txt 0xffffffff
@echo.
@echo    2) Disabling vss tracing:
@echo          vsstracing.cmd -disable
@echo.
@echo    3) Display VSS tracing settings:
@echo          vsstracing.cmd -query
@echo.