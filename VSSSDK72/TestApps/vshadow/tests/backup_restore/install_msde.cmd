setlocal
call %~dp0\setvar.cmd

if "%MSDE_INSTANCE%"=="" @echo Cannot install un-named MSDE instance! Please uncomment the MSDE_INSTANCE line in setvar.cmd & goto :EOF

%MSDE_SETUP_LOCATION%\setup INSTANCENAME=%MSDE_INSTANCE% BLANKSAPWD=1 %OTHER_MSDE_OPTIONS%
net start MSSQL$%MSDE_INSTANCE%