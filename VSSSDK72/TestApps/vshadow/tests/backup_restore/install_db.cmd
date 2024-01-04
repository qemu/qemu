setlocal
call %~dp0\setvar.cmd
@echo on

@call :RUN_SCRIPT %DB%

@goto :EOF


:RUN_SCRIPT
setlocal
set RPT_FILE=_install_log_%1.rpt
del %RPT_FILE%

@echo if exists (select * from sysdatabases where name='%1')		> _drop_db.sql
@echo begin								>> _drop_db.sql
@echo   raiserror('Dropping existing %1 database ....',0,1)		>> _drop_db.sql
@echo   DROP database %1						>> _drop_db.sql
@echo end								>> _drop_db.sql
@echo GO								>> _drop_db.sql
@echo CHECKPOINT							>> _drop_db.sql
@echo go								>> _drop_db.sql


osql -E  -S %DATABASE_SERVER% -i _drop_db.sql

cscript sleep.vbs
rd /s/q %DB_DIR%
md %DB_DIR%


@echo CREATE DATABASE %1 ON (NAME = %1_dat, FILENAME = '%DB_DIR%\db.mdf') LOG ON (NAME = '%1_log', FILENAME = '%DB_DIR%\log.ldf') > _pre_create_db.sql
@echo GO 			>> _pre_create_db.sql
@echo CHECKPOINT 		>> _pre_create_db.sql
@echo GO 			>> _pre_create_db.sql

osql -E  -S %DATABASE_SERVER% -i _pre_create_db.sql
osql -E  -S %DATABASE_SERVER% -i install_%1.sql -o %RPT_FILE%
@echo === Output: %RPT_FILE% ===
@echo.
@goto :EOF

