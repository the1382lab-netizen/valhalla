@echo off
rem Turn the recorded PSOs (record_pso.cmd) into the bundled cache the next package ships:
rem Valhalla2\Build\Windows\PipelineCaches\PSO_Valhalla2_PCD3D_SM6.spc. Needs the shader stable
rem keys from the same cook (Valhalla2\Saved\Cooked\Windows\Valhalla2\Metadata\PipelineCaches\*.shk,
rem written because DefaultEngine.ini sets [DevOptions.Shaders] NeedsShaderStableKeys=true).
rem Re-record after anything that changes the shader set: new materials, new art, the
rem ray tracing or scalability defaults.
call "%~dp0_env.cmd"
set "REC=%PERF_OUT%\pkg\Windows\Valhalla2\Saved\CollectedPSOs"
set "SHK=%~dp0..\..\Valhalla2\Saved\Cooked\Windows\Valhalla2\Metadata\PipelineCaches"
set "DEST=%~dp0..\..\Valhalla2\Build\Windows\PipelineCaches"
if not exist "%DEST%" mkdir "%DEST%"
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%PROJ%" -run=ShaderPipelineCacheTools expand "%REC%\*.rec.upipelinecache" "%SHK%\*SM6*.shk" "%DEST%\PSO_Valhalla2_PCD3D_SM6.spc" -unattended -nosplash -NoP4
echo EXPAND-RC=%ERRORLEVEL%
dir "%DEST%"
exit /b 0
