call %Tgtdir%\setvar1.cmd
for /R %SHADOW_DEVICE_1%\ %%i in (*) do @echo %%i
%Tgtdir%\VSHADOW -bw=%SHADOW_SET_ID%