# Performance benchmarks (B-27)

Repeatable frame-time measurements for the client performance pass (backlog B-27). Every
script runs the editor binaries (no packaged build needed), vsync off, no frame cap,
1920×1080, and writes its results to `Valhalla2/Saved/Perf/` (git-ignored).

| Script | What it measures |
|---|---|
| `standalone_bench.cmd town` | Standalone game (server + client in one process) idle in the old Grasslands town. Every NPC in every zone is local here, so this is the worst case for character cost. |
| `standalone_bench.cmd eldmoor` | The same at Harrow's Rest in Eldmoor (walls, fires, fog). Add a second argument to change a setting at frame 900, e.g. `standalone_bench.cmd eldmoor "scalability 2"`. |
| `combat_bench.cmd` | A dedicated server, one profiled client and three windowless bot clients fighting at three Eldmoor camps. Writes a CSV and an Insights trace of the profiled client. Needs about 15 GB of free memory. |
| `csv_summary.py` | Summarises a CSV capture: fps, p99, hitches, game / render thread and GPU time, combat events per second (`--combat` splits the fight into 10 s windows, `--stats` prints any columns). |

## What is in the code for this

- `Valhalla_*` CPU trace scopes (Unreal Insights, `-trace=cpu`): the combat event path
  (`Valhalla_CombatEvents`, `_Anim`, `_Vfx`, `_Listeners`), the HUD (`Valhalla_HUD_Tick`,
  `_WorldLayer`, `_RefreshCombatLog`, `_RefreshChatLines`, `_SpawnFloater`, ...) and the
  vision fog (`Valhalla_Fog_GatherSegments`, `_VisibilityPolygon`, `_VisibleRT`, `_ExploredRT`).
- CSV stat `Valhalla/CombatEventsReceived`: combat events a process received per frame.

## Reading a trace

Per-timer totals for a time window, without opening the Insights UI:

```
UnrealInsights.exe -OpenTraceFile="<trace>.utrace" -NoUI -AutoQuit -log -ExecOnAnalysisCompleteCmd="TimingInsights.ExportTimerStatistics <out>.csv -threads=GameThread -startTime=60 -endTime=110 -sortBy=TotalInclusiveTime -sortOrder=Descending -maxTimerCount=300"
```

Times are seconds from the start of the trace. Divide `Incl` by the `Count` of
`FEngineLoop::Tick` for milliseconds per frame.

## Notes

- A boot-time CSV capture starts before the project is known, so Unreal writes the CSV and
  its log under `%LOCALAPPDATA%\UnrealEngine\5.8\Saved`; the scripts copy them out.
- Standalone numbers include the server's work (NPC AI, NPC movement, the game state's
  fixed tick). For what a player's client costs, use `combat_bench.cmd`.
- The editor binaries load assets more slowly than a packaged build, so loading hitches
  look worse here; judge hitch targets on a packaged client (set `CLIENT_EXE`).
