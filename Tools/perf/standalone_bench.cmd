@echo off
rem Standalone benchmark (server and client in one process, offline dev session).
rem   standalone_bench.cmd town      - the old Grasslands town, idle
rem   standalone_bench.cmd eldmoor   - Eldmoor, teleported to Harrow's Rest at frame 20
rem Optional 2nd argument: extra console commands run at frame 900, e.g. "scalability 2".
rem Optional: set GRAPHICS to valhalla.Graphics arguments applied at frame 10 (not saved), e.g.
rem   set GRAPHICS=preset=epic      or      set GRAPHICS=preset=high scale=85
rem Without it the run uses this PC's saved graphics settings (B-27 Phase 4: High by default).
rem Captures 2400 frames at 1920x1080 with vsync off and no frame cap, then exits.
call "%~dp0_env.cmd"
set "ZONE=%~1"
if "%ZONE%"=="" set "ZONE=town"
set "EXEC=20:stat none"
if /i "%ZONE%"=="eldmoor" set "EXEC=20:valhalla.DebugTeleport 83200 7168"
if not "%~2"=="" set "EXEC=%EXEC%,900:%~2"
if defined GRAPHICS set "EXEC=10:valhalla.Graphics %GRAPHICS%,%EXEC%"
"%UE_EXE%" "%PROJ%" /Game/Valhalla/Maps/L_World -game -windowed -resx=1920 -resy=1080 -nosound -csvCaptureFrames=2400 -csvGpuStats -ExitAfterCsvProfiling "-csvExecCmds=%EXEC%" "-ExecCmds=r.VSync 0,t.MaxFPS 0" -log=PerfStandalone.log
call :copyout standalone_%ZONE%
python "%~dp0csv_summary.py" "%PERF_OUT%\standalone_%ZONE%.csv" --skip 600
exit /b 0
:copyout
for /f "delims=" %%F in ('dir /b /o-d "%ENGINE_SAVED%\Profiling\CSV\*.csv"') do (copy /y "%ENGINE_SAVED%\Profiling\CSV\%%F" "%PERF_OUT%\%1.csv" >nul & goto :copied)
:copied
copy /y "%ENGINE_SAVED%\Logs\PerfStandalone.log" "%PERF_OUT%\%1.log" >nul
exit /b 0
