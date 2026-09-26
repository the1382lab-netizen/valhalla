@echo off
rem Vision fog benchmark (B-27 Phase 5): standalone, teleported into Harrow's Rest (the walled
rem village, the fog's worst case), standing still, then walking about (valhalla.UI walk).
rem   fog_bench.cmd          - the Phase 5 fog (recompute only on change, cached walls)
rem   fog_bench.cmd old      - valhalla.FogAlwaysRecompute 1 (every frame, overlap query)
rem Reads the CSV stat Valhalla/GameThread/FogTick (ms a frame). Graphics pinned to High at 100 %.
call "%~dp0_env.cmd"
set "MODE=%~1"
set "FOGCMD=valhalla.FogAlwaysRecompute 0"
if /i "%MODE%"=="old" set "FOGCMD=valhalla.FogAlwaysRecompute 1"
if "%MODE%"=="" set "MODE=new"
set "EXEC=10:valhalla.Graphics preset=high scale=100,20:valhalla.DebugTeleport 83200 7168,30:%FOGCMD%,900:valhalla.UI walk w 4,1200:valhalla.UI walk d 4,1500:valhalla.UI walk s 4,1800:valhalla.UI walk a 4"
"%UE_EXE%" "%PROJ%" /Game/Valhalla/Maps/L_World -game -windowed -resx=1920 -resy=1080 -nosound -csvCaptureFrames=2400 -csvGpuStats -ExitAfterCsvProfiling "-csvExecCmds=%EXEC%" "-ExecCmds=r.VSync 0,t.MaxFPS 0" -log=PerfFog.log
for /f "delims=" %%F in ('dir /b /o-d "%ENGINE_SAVED%\Profiling\CSV\*.csv"') do (copy /y "%ENGINE_SAVED%\Profiling\CSV\%%F" "%PERF_OUT%\fog_%MODE%.csv" >nul & goto :copied)
:copied
copy /y "%ENGINE_SAVED%\Logs\PerfFog.log" "%PERF_OUT%\fog_%MODE%.log" >nul
python "%~dp0csv_summary.py" "%PERF_OUT%\fog_%MODE%.csv" --skip 600 --stats "FogTick"
exit /b 0
