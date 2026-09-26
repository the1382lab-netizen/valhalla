@echo off
rem Package a Development Win64 client for benchmarking (B-27), into Valhalla2\Saved\Perf\pkg.
rem Works with the editor open: the editor target is not rebuilt (-nocompileeditor), so build
rem the editor first after C++ changes. -IgnoreCookErrors: the cook commandlet reports two
rem editor-environment errors (no GameFeatureData asset rule; the MCP HTTP port is taken by the
rem running editor) that do not affect the build.
rem The game data is staged the way stage_game_data.py does it (shared\data -> Content\Data).
rem Run it with: Valhalla2\Saved\Perf\pkg\Windows\Valhalla2.exe /Game/Valhalla/Maps/L_World
rem (standalone) or set CLIENT_EXE to it for combat_bench.cmd.
rem After packaging, check_packaged.py runs the packaged client's content check (first public
rem test: the cook had left out everything loaded by name) and prints CONTENT-CHECK=OK or FAIL.
rem   package_client.cmd nocheck   - skip that check
call "%~dp0_env.cmd"
set "CONTENT_DATA=%~dp0..\..\Valhalla2\Content\Data"
if not exist "%CONTENT_DATA%" mkdir "%CONTENT_DATA%"
copy /y "%~dp0..\..\shared\data\*.json" "%CONTENT_DATA%\" >nul
call "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="%PROJ%" -noP4 -platform=Win64 -clientconfig=Development -nocompileeditor -build -cook -stage -pak -archive -archivedirectory="%PERF_OUT%\pkg" -utf8output -unattended -IgnoreCookErrors
set "PKG_RC=%ERRORLEVEL%"
echo PACKAGE-RC=%PKG_RC%
if not "%PKG_RC%"=="0" exit /b 0
if /i "%~1"=="nocheck" (
  echo CONTENT-CHECK=SKIPPED
  exit /b 0
)
python "%~dp0check_packaged.py" --build-dir "%PERF_OUT%\pkg\Windows"
exit /b 0
