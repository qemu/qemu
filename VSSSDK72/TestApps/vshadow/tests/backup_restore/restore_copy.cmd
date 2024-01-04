setlocal
call setvar.cmd

@echo.
@echo Restoring the database files ...
@echo.

rd /s/q %DB_DIR%
xcopy /s %DB_BACKUP_DIR%\* %DB_DIR%\