# MetaHuman animation authoring

Hand-keyed stance poses and weapon attacks for the Valhalla MetaHuman base body
(B-15, 2026-09-23). Everything is authored in character space on the body's own
skeleton and written into `/Game/Valhalla/Characters/MetaHuman/Animations`.

| File | What it does |
|---|---|
| `author.py` | Pure Python. Builds the grip / shield stance poses, calibrates the weapon grip frames, keys the attacks (sword, dagger, mace, staff, bow) and writes `out/*.json` plus `out/grips.json`. |
| `ue_write_anims.py` | Run in the editor: turns `out/*.json` into AnimSequences (`MHP_*`, `MH_Attack_*`). |
| `ue_retarget_manny.py` | Run in the editor: retargets the UE5 Manny idle / walk / jog / hit / death / punch clips onto the body (`MH_MM_*`, `MH_MF_*`). Needs the template Mannequins content copied into `Content/Characters/Mannequins` first. |
| `qm.py`, `skel.py`, `rig.py` | Quaternion math, FK pose container, hand / grip helpers. |
| `ue_io.py` | Editor-side read of an AnimPose into `skel.Pose` and write of frames into an AnimSequence. |
| `pose_MH_MM_Idle.json`, `parents.json` | The idle's first frame and the skeleton hierarchy the author script starts from. |

Character space: forward = +Y, right = -X, up = +Z, unscaled cm (the body is
180.3 cm; the game draws it at 122 / 180.3).

If you change a grip in `author.py`, copy `out/grips.json` into
`UValhallaVisuals` GripFrame (ValhallaVisuals.cpp) — the attacks are keyed
against those frames.
