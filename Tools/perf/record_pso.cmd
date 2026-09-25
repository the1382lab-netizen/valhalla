@echo off
rem Record the shader pipelines (PSOs) a packaged client uses, for the bundled PSO cache (B-27).
rem Run package_client.cmd first (the cook writes the shader stable keys). This plays the
rem packaged client with -logPSO through the town, Harrow's Rest and the networked combat test;
rem each run writes Saved\Perf\pkg\Windows\Valhalla2\Saved\CollectedPSOs\*.rec.upipelinecache.
rem Then run build_pso_cache.cmd and package again.
call "%~dp0_env.cmd"
set "PKG_EXE=%PERF_OUT%\pkg\Windows\Valhalla2.exe"
if not exist "%PKG_EXE%" (echo No packaged client at %PKG_EXE%; run package_client.cmd first. & exit /b 1)
"%PKG_EXE%" /Game/Valhalla/Maps/L_World -windowed -resx=1920 -resy=1080 -nosound -logPSO -csvCaptureFrames=1500 -ExitAfterCsvProfiling -log=PsoTown.log
"%PKG_EXE%" /Game/Valhalla/Maps/L_World -windowed -resx=1920 -resy=1080 -nosound -logPSO -csvCaptureFrames=2400 -ExitAfterCsvProfiling "-csvExecCmds=20:valhalla.DebugTeleport 83200 7168,900:valhalla.DebugTeleport 80480 7300,1500:valhalla.DebugTeleport 86250 3650" -log=PsoEldmoor.log
set "CLIENT_EXE=%PKG_EXE%"
set "CLIENT_EXTRA=-logPSO"
call "%~dp0combat_bench.cmd"
dir /b "%PERF_OUT%\pkg\Windows\Valhalla2\Saved\CollectedPSOs"
exit /b 0
