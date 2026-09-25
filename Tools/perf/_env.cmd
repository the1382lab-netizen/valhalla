@echo off
rem Shared paths for the B-27 benchmarks. Set UE_ROOT if Unreal is not in the launcher location.
if "%UE_ROOT%"=="" set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
set "UE_EXE=%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor.exe"
set "PROJ=%~dp0..\..\Valhalla2\Valhalla2.uproject"
set "PERF_OUT=%~dp0..\..\Valhalla2\Saved\Perf"
rem A boot-time CSV capture (-csvCaptureFrames) starts before the project is known, so the
rem engine writes its CSV and log under the engine's user folder; the scripts copy them out.
set "ENGINE_SAVED=%LOCALAPPDATA%\UnrealEngine\5.8\Saved"
if not exist "%PERF_OUT%" mkdir "%PERF_OUT%"
