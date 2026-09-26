# Performance benchmarks (B-27)

Repeatable frame-time measurements for the client performance pass (backlog B-27). Every
script runs the editor binaries (no packaged build needed), vsync off, no frame cap,
1920×1080, and writes its results to `Valhalla2/Saved/Perf/` (git-ignored).

| Script | What it measures |
|---|---|
| `standalone_bench.cmd town` | Standalone game (server + client in one process) idle in the old Grasslands town. Every NPC in every zone is local here, so this is the worst case for character cost. |
| `standalone_bench.cmd eldmoor` | The same at Harrow's Rest in Eldmoor (walls, fires, fog). Add a second argument to change a setting at frame 900, e.g. `standalone_bench.cmd eldmoor "scalability 2"`. |
| `combat_bench.cmd` | A dedicated server, one profiled client and three windowless bot clients fighting at three Eldmoor camps. Writes a CSV and an Insights trace of the profiled client. Needs about 15 GB of free memory. |
| `package_client.cmd` | Stages the game data and packages a Development client into `Saved/Perf/pkg` (the editor can stay open). |
| `record_pso.cmd` / `build_pso_cache.cmd` | Record the shader pipelines a packaged client uses and build the bundled PSO cache from them (below). |
| `csv_summary.py` | Summarises a CSV capture: fps, p99, hitches, game / render thread and GPU time, combat events per second (`--combat` splits the fight into 10 s windows, `--stats` prints any columns). |

## What is in the code for this

- `Valhalla_*` CPU trace scopes (Unreal Insights, `-trace=cpu`): the combat event path
  (`Valhalla_CombatEvents`, `_Anim`, `_Vfx`, `_Listeners`), the HUD (`Valhalla_HUD_Tick`,
  `_WorldLayer`, `_RefreshCombatLog`, `_RefreshChatLines`, `_SpawnFloater`, ...) and the
  vision fog (`Valhalla_Fog_GatherSegments`, `_VisibilityPolygon`, `_VisibleRT`, `_ExploredRT`).
- CSV stat `Valhalla/CombatEventsReceived`: combat events a process received per frame.
- CSV stat `Valhalla/CombatEventsUnresolved` (client): events that arrived naming no actor the client has,
  the events of a fight it cannot see (0 since B-27 Phase 3; `csv_summary.py --combat` prints it as
  "unseen"). Server: `Valhalla/CombatEventsSentGuaranteed`, `…SentSeen`, `…Skipped`.
- Graphics (B-27 Phase 4): every run uses the PC's saved graphics settings (High by default). Set
  `GRAPHICS` to `valhalla.Graphics` arguments to pin them for a run without saving them, e.g.
  `set GRAPHICS=preset=epic scale=100` before `standalone_bench.cmd`.
- `combat_bench.cmd` with `ROUTING=0` set runs the server with `valhalla.CombatEventRouting 0` (every event to
  every player, as before Phase 3), for a before-and-after comparison.

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
  look worse here; judge hitch targets on a packaged client (`package_client.cmd`, then set
  `CLIENT_EXE` to `Valhalla2\Saved\Perf\pkg\Windows\Valhalla2.exe`). A packaged client writes its CSVs and
  logs under `Saved\Perf\pkg\Windows\Valhalla2\Saved`, not the engine folder.

## Shader pipeline cache (bundled PSO cache)

The first time a packaged client draws something with a new shader pipeline it compiles it
on the spot: 68 of them on entering the world in the Phase 0 baseline, as 250-360 ms frames.
The fix is to record the pipelines once and ship them, so the client compiles them while it
starts instead:

1. `package_client.cmd` (the cook writes the shader stable keys, because `DefaultEngine.ini`
   sets `[DevOptions.Shaders] NeedsShaderStableKeys=true`).
2. `record_pso.cmd`: plays that package with `-logPSO` through the town, Harrow's Rest and
   the combat test.
3. `build_pso_cache.cmd`: `ShaderPipelineCacheTools expand` turns the recordings and the
   stable keys into `Valhalla2/Build/Windows/PipelineCaches/PSO_Valhalla2_PCD3D_SM6.spc`
   (committed).
4. `package_client.cmd` again: the package now carries the cache.

Record again after anything that changes the shader set (new materials or art, the ray
tracing or scalability defaults): pipelines missing from the cache fall back to compiling
on the spot, exactly as before.
