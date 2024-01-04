setlocal
call %~dp0\setvar.cmd

vshadow -wi=%COMPONENT_DB% -exec=restore_copy.cmd -r=_%DB%_bc.xml 
if errorlevel 1 @goto :ERROR

@goto :EOF

:ERROR

@echo.
@echo FAILED TEST: %errorlevel%
@echo.
@pause
