if "%1"=="" @echo Missing first parameter! & @goto :EOF

setlocal
call %~dp0\setvar.cmd

osql -E  -S %DATABASE_SERVER% -i check_%DB%.sql -o _%1%OUTPUT_CHECK%