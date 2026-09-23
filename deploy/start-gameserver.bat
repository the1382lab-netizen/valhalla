@echo off
rem Game server: the editor binary in dedicated-server mode on UDP 7777.
rem Works with the launcher engine (no source build). Reads
rem VALHALLA_SERVER_SECRET from secrets.local.env at the repo root by itself.
rem Build the editor (Build.bat or the IDE) after pulling C++ changes first.
rem Set UE_ROOT if Unreal is not in the default launcher location.
title Valhalla - game server
if "%UE_ROOT%"=="" set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
set "UE_EXE=%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor.exe"
if not exist "%UE_EXE%" (
  echo Unreal not found at "%UE_EXE%". Set UE_ROOT to your UE 5.8 folder.
  pause
  exit /b 1
)
"%UE_EXE%" "%~dp0..\Valhalla2\Valhalla2.uproject" /Game/Valhalla/Maps/L_World -server -log -port=7777
