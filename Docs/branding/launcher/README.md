# Launcher art (B-17)

The launcher's home screen uses layout A from the design canvas "Valhalla login & character select" (row "Launcher (B-17) · windowed"): a 960 × 600 window with the hero image on the left, a News / Settings / About panel on the right, and a status bar along the bottom with the progress bar and the Play button. Colours and fonts are the Gilded Hall set in `Docs/branding/class-icons/README.md`.

## Hero image

`launcher_hero_keep_entrance.jpg` (1280 × 954): the entrance to Ashvane Keep in Eldmoor Grasslands, with no characters in shot. The launcher shows it with `object-fit: cover`, anchored at 50 % 30 %, under a dark gradient on the right and bottom edges.

To capture it again after the keep changes, open `L_World`, start Simulate (no player pawn, so fires burn and no editor icons show) and capture the level viewport from:

- Location X 84420, Y 2820, Z 760
- Rotation pitch -24, yaw -76, roll 0 (90° FOV)

Crop the top of the capture to a 1.34 : 1 frame (this also drops the axis gizmo in the bottom-left corner) and save it at 1280 px wide.
