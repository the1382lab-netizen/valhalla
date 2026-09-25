# Valhalla branding

The game logo is the **coin monogram**: a gold "V" on a royal-blue coin with a gold rim that reads VALHALLA. Its colours come from the Art Bible palette (royal blue `#2F4A7A`, gold trim `#B08D3C`).

| File | Use |
|---|---|
| `valhalla_logo_1024.png` | Master image: Discord server icon, store pages, splash screens |
| `valhalla_logo_512.png` | Smaller copy for places with an upload size limit |
| `valhalla_logo.ico` | Windows icon (16–256 px) for the packaged client |
| `valhalla_logo.svg` | Vector source. It uses the Cinzel font (free, SIL Open Font License); Cinzel must be installed for the SVG to render correctly |

## Class icons

The class icon kit (a coin per class in the logo's style) and the "Gilded Hall" colours and fonts for the login and character select screens are in `class-icons/README.md`.

## To do when the client is packaged

Use this logo as the packaged game's icon:

- In **Project Settings → Platforms → Windows → Game Icon**, pick `valhalla_logo.ico`. Unreal copies it to `Valhalla2/Build/Windows/Application.ico`.
- Use it for the client's window/taskbar icon, and as the splash or loading-screen art if we add one.
- Add it to the First Public Test Checklist's packaging steps.
