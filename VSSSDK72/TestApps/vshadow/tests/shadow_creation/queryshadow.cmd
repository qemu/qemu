call %Tgtdir%\setvar1.cmd
for /R %SHADOW_DEVICE_1%\ %%i in (*) do @echo %%i
%Tgtdir%\VSHADOW -s=%SHADOW_ID_1%
%Tgtdir%\VSHADOW -s=%SHADOW_ID_2%