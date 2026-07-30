# Kinexis brand kit — for the PrusaSlicer fork

Everything needed to re-skin a PrusaSlicer fork as **Kinexis Slicer**. Assets in
this folder are the source of truth; carry this whole `branding/` directory into
the fork.

## Name / strings
- App name: **Kinexis Slicer**
- Short name / key: `Kinexis`
- Tagline: *Open-source 3-axis & continuous 5-axis non-planar FDM slicer.*

## Colour palette (from the Rust app's `theme.rs`)

| Role | Hex | RGB |
|---|---|---|
| **Accent (primary)** | `#00828C` | 0, 130, 140 |
| **Accent bright / hover** | `#00A0AC` | 0, 160, 172 |
| Text primary | `#262A30` | 38, 42, 48 |
| Panel background | `#ECEDF0` | 236, 237, 240 |
| Window background | `#F4F5F7` | 244, 245, 247 |
| Widget background | `#FFFFFF` | 255, 255, 255 |
| Panel stroke / borders | `#CCCFD4` | 204, 207, 212 |
| Viewport / bed fill | `#D4D5D7` | 212, 213, 215 |
| Bed grid lines | `#94969A` | 148, 150, 154 |

The identity colour is the **teal `#00828C`** — use it for selection, active
controls, highlights, links.

## Icons (in `branding/icons/`)
- `Kinexis.ico` — Windows app icon (multi-size).
- `Kinexis_16..1024px.png` — PNG set for Linux/GTK, in-app logos, splash.
- `kinexis-logo-master.png` (1254×1254) — master; regenerate any size from this.
- **Still needed:** `Kinexis.icns` (macOS — generate on a Mac with `iconutil`,
  or `png2icns`), and ideally a **vector `Kinexis.svg`** recreated from the
  original logo artwork if you have it (PrusaSlicer uses SVGs in the toolbar).

## Where branding lives in a PrusaSlicer fork

PrusaSlicer refactors often, so `grep` for the current names — but the anchors are:

1. **App name & keys** — `version.inc` (`SLIC3R_APP_NAME`, `SLIC3R_APP_KEY`) and
   the top-level `CMakeLists.txt`. Set these to Kinexis so config dirs, window
   titles, and about text follow.
2. **Windows icon/resource** — `src/platform/msw/*.rc` (points at the `.ico`),
   plus `resources/icons/`.
3. **App icons** — `resources/icons/` (replace `PrusaSlicer*.png/.svg/.ico`,
   `.icns` for mac). Keep the same filenames PrusaSlicer expects, swap the pixels.
4. **Splash / about** — the about dialog bitmap and splash image in
   `resources/icons/`; about text pulls from the app-name strings above.
5. **macOS bundle** — `Info.plist` (bundle name/identifier) + `.icns`.
6. **UI accent colour** — this is the one that isn't a drop-in. PrusaSlicer's
   green highlight is set in C++ (search `GUI_App` colour setup / `ColorRGB`
   highlight constants and the dark/light `update_ui_colours` path). Replace the
   green with `#00828C`. Expect to touch a few call sites, not one constant.
7. **Profiles / vendor** — `resources/profiles/` if you ship Kinexis machine
   profiles (this is also where 5-axis machine definitions will go later).

## Rule
Swap **pixels and strings**, keep PrusaSlicer's **filenames and structure**, so
you stay mergeable with upstream. Fence any code-level colour changes with a
`// KINEXIS:` comment for the same reason.
