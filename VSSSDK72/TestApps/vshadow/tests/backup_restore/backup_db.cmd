setlocal
call %~dp0\setvar.cmd

vshadow -p -script=_env_vars.cmd -bc=_%DB%_bc.xml -wi=%COMPONENT_DB%  %DB_VOLUME% 
if errorlevel 1 @goto :ERROR

call _env_vars.cmd

rd /s/q %SNAPSHOT_MOUNT_POINT%
if errorlevel 1 @goto :ERROR

md %SNAPSHOT_MOUNT_POINT%
if errorlevel 1 @goto :ERROR

rd /s/q %DB_BACKUP_DIR%
if errorlevel 1 @goto :ERROR

vshadow -el=%SHADOW_ID_1%,%SNAPSHOT_MOUNT_POINT%
if errorlevel 1 @goto :ERROR

xcopy /s %DB_DIR_ON_SNAPSHOT%\* %DB_BACKUP_DIR%\
if errorlevel 1 @goto :ERROR

vshadow -dx=%SHADOW_SET_ID%
if errorlevel 1 @goto :ERROR

@goto :EOF

:ERROR

@echo.
@echo FAILED TEST: %errorlevel%
@echo.
@pause