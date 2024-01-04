if "%1"=="" @echo Missing first parameter! & @goto :EOF
if "%2"=="" @echo Missing second parameter! & @goto :EOF
if "%VARIATION%"=="" @echo Missing VARIATION parameter! & @goto :EOF

setlocal
call %~dp0\setvar

fc _%1%OUTPUT_CHECK% _%2%OUTPUT_CHECK%
set RESULT=%ERRORLEVEL%

if "%3"=="check_difference" call :CHECK_DIFF & goto :EOF

call process_result %RESULT%
@goto :EOF


:CHECK_DIFF
setlocal
set VARIATION=%VARIATION% - check differences!
set /A RESULT=1-%RESULT%
call process_result %RESULT%
goto :EOF


