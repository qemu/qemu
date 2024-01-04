setlocal
if not "%1"=="" set INPUT_SCRIPT=-i %1
if not "%2"=="" set DATABASE=-d %2
call %~dp0\setvar.cmd
osql -E  -S %DATABASE_SERVER% %DATABASE% %INPUT_SCRIPT%