del _*
for /F %%i in ('dir /b _*') do rd /s/q %%i
call osql_db cleanup.sql