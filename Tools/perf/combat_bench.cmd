@echo off
rem Networked combat benchmark: a dedicated server, one profiled client (warrior, 1920x1080,
rem CSV + Insights trace) and three windowless bot clients. The server's frame-timed console
rem commands (-csvExecCmds) teleport everyone at server frame 2400: the warrior and the cleric
rem to Eldmoor camp C2, the ranger to camp C3 and the shaman to camp C1 (fights the warrior's
rem client cannot see). Everyone is kept alive (DebugSetHp every second), retargets every 3 s
rem and casts on timers. About 2.5 minutes; needs roughly 15 GB of free memory.
rem Optional: set CLIENT_EXE to a packaged Valhalla2.exe to profile a packaged client.
call "%~dp0_env.cmd"
set "CMDS=2400:valhalla.DebugTeleport 80480 7300 warrior,2400:valhalla.DebugTeleport 80530 7260 cleric,2400:valhalla.DebugTeleport 83050 4760 ranger,2400:valhalla.DebugTeleport 86250 8250 shaman,2460:valhalla.DebugTargetNearest,2470:valhalla.DebugAutoAttack,r60:valhalla.DebugSetHp 99999,r180:valhalla.DebugTargetNearest,r240:valhalla.DebugCast warrior_cleave warrior,r300:valhalla.DebugCast cleric_divine_hammer cleric,r360:valhalla.DebugCast ranger_multi_shot ranger,r200:valhalla.DebugCast shaman_flame_shock shaman,r480:valhalla.DebugCast warrior_taunt warrior"
start "" "%UE_EXE%" "%PROJ%" /Game/Valhalla/Maps/L_World -server -log=PerfServer.log -csvCaptureFrames=20000 -ExitAfterCsvProfiling "-csvExecCmds=%CMDS%"
timeout /t 8 /nobreak >nul
start "" "%UE_EXE%" "%PROJ%" 127.0.0.1?class=cleric?charname=BotCleric -game -nullrhi -nosound -log=PerfBotB.log
start "" "%UE_EXE%" "%PROJ%" 127.0.0.1?class=ranger?charname=BotRanger -game -nullrhi -nosound -log=PerfBotC.log
start "" "%UE_EXE%" "%PROJ%" 127.0.0.1?class=shaman?charname=BotShaman -game -nullrhi -nosound -log=PerfBotD.log
timeout /t 5 /nobreak >nul
set "CLIENT=%CLIENT_EXE%"
if "%CLIENT%"=="" (set "CLIENT_ARGS="%PROJ%" 127.0.0.1?class=warrior?charname=Warrior -game") else (set "CLIENT_ARGS=127.0.0.1?class=warrior?charname=Warrior")
if "%CLIENT%"=="" set "CLIENT=%UE_EXE%"
"%CLIENT%" %CLIENT_ARGS% -windowed -resx=1920 -resy=1080 -nosound -csvCaptureFrames=9000 -ExitAfterCsvProfiling -trace=cpu,frame,bookmark "-tracefile=%PERF_OUT%\combat_client.utrace" "-ExecCmds=r.VSync 0,t.MaxFPS 0" -log=PerfClient.log
timeout /t 5 /nobreak >nul
rem Stop the server and the bots (matched by their log names, so no other Unreal is touched).
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='UnrealEditor.exe'\" | Where-Object { $_.CommandLine -match 'PerfBot|PerfServer' } | ForEach-Object { Invoke-CimMethod -InputObject $_ -MethodName Terminate | Out-Null }"
for /f "delims=" %%F in ('dir /b /o-n "%ENGINE_SAVED%\Profiling\CSV\*.csv"') do (copy /y "%ENGINE_SAVED%\Profiling\CSV\%%F" "%PERF_OUT%\combat_client.csv" >nul & goto :copied)
:copied
copy /y "%ENGINE_SAVED%\Logs\PerfClient.log" "%PERF_OUT%\combat_client.log" >nul
python "%~dp0csv_summary.py" "%PERF_OUT%\combat_client.csv" --combat
exit /b 0
