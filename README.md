# San Francisco Rush 2049 PC Port — Windows Downloads, Original and Remaster

Play **San Francisco Rush 2049 for Nintendo 64 on Windows PC** with ready-to-install EXE downloads, controller remapping, display settings and optional remastered textures. This unofficial, experimental N64 PC port offers both the original SDL2/RT64 release and a newer release with Dear ImGui settings and the libultraship Config component. Both use the RT64 Vulkan renderer.

**You must provide your own supported original US Nintendo 64 ROM. No ROM is included.** Setup validates it before installation, and the launcher and game validate it again before launch. Your ROM remains in your chosen folder; it is not copied into the installation, uploaded, or cached as a fallback. z64, v64 and n64 byte orders are accepted. Supported normalized SHA1: `3f99351d7bb61656614bdb2aa1a90cfe55d1922c` (12 MiB).

## Download and play

Desktop, installer and executable icon artwork: **[POOTERMAN](https://www.deviantart.com/pooterman)**.

![Rush 2049 PC artwork by POOTERMAN](https://github.com/RemakerGamee/rush2049-original-pc/releases/download/v0.3.2/Rush2049-PC-Artwork-POOTERMAN.png)

Choose the version you want:

| Choose one of four Windows downloads | Visuals | Settings interface |
| --- | --- | --- |
| [**Original** — v0.3.2 EXE](https://github.com/RemakerGamee/rush2049-original-pc/releases/download/v0.3.2/Rush2049-Original-PC-v0.3.2-Setup.exe) | Original game textures | Native Windows menus; without libultraship / ImGui |
| [**Remastered** — v0.3.2 EXE](https://github.com/RemakerGamee/rush2049-original-pc/releases/download/v0.3.2/Rush2049-Remastered-PC-v0.3.2-Setup.exe) | Restored 4x texture pack and circuit lighting | Native Windows menus; without libultraship / ImGui |
| [**Original + libultraship / ImGui** — v0.3.2 EXE](https://github.com/RemakerGamee/rush2049-original-pc/releases/download/v0.3.2/Rush2049-Original-Libultraship-PC-v0.3.2-Setup.exe) | Original game textures | Dear ImGui settings and libultraship Config; fullscreen fill |
| [**Remastered + libultraship / ImGui** — v0.3.2 EXE](https://github.com/RemakerGamee/rush2049-original-pc/releases/download/v0.3.2/Rush2049-Remastered-Libultraship-PC-v0.3.2-Setup.exe) | Restored 4x texture pack and circuit lighting | Dear ImGui settings and libultraship Config; fullscreen fill |

All four downloads require your own supported US Nintendo 64 ROM. v0.3.2 updates the artwork icons and credit in all four installers; the native-menu game retains the v0.2.0 core and the libultraship/ImGui game retains the v0.3.1 core. The instructions and settings below describe the libultraship/ImGui choices; the [v0.2.0 release notes](https://github.com/RemakerGamee/rush2049-original-pc/releases/tag/v0.2.0) describe its native menus and shortcuts. Earlier [v0.3.0 downloads](https://github.com/RemakerGamee/rush2049-original-pc/releases/tag/v0.3.0), including the combined visual-choice installer and dedicated libultraship packages, remain available.

The **Original installers** are smaller and use original ROM textures. The **Remastered installers** include the reviewed 5,957-texture pack and launch Remaster directly. Original bypasses the replacement pack and remastered circuit lighting; Remaster enables them. The two libultraship / ImGui choices use RT64 and the same settings, controller mappings and retained saves.

Run Setup, browse to your ROM, choose a new installation folder and install. Open the desktop shortcut or `Rush2049Original.exe`. If your ROM moves or changes, select the valid file again. No Python, Git, compiler, driver installation or service changes are needed for players. Use 64-bit Windows and existing Vulkan-capable graphics hardware/drivers.

Saves and controller settings stay outside the installation in `%LOCALAPPDATA%\Rush2049Original\PlayerData`. Upgrades require a new app folder and retain existing player data. Only one Rush session may run at a time. Setup preserves existing Controller Pak images and mappings.

## Settings and controls

Horizontal settings categories: Interface, Display, Controllers, Diagnostics and Cheats. Shortcuts appear only in the top bar: F1 Interface, F2 Display, F5 Cheats, F8 Controllers, F9 Diagnostics and F11 Fullscreen.

Display offers Standard, 720p, 1080p, 1440p 16:9, 1440p 21:9 (3440×1440) and 4K (3840×2160), plus VSync and presentation targets of 60/120/144/165/180/200/240 Hz. Fullscreen uses the desktop size. **Fill screen in fullscreen** defaults to On and stretches the complete game image across the display, including menus; turn it off and Apply and save to preserve the original proportions. Presentation targets do not change the monitor's Windows refresh rate.

The adjustable-window update starts fresh windowed sessions at 800×600. Under Display, select Custom and enter a width and height, or drag the window edges and select Use current window size. Apply and save remembers the choice. Sizes fit the desktop's usable area; changing the window does not change Windows display settings. Existing saved resolution selections remain supported.

Keyboard is enabled by default: Enter Start, Z A, X B, Space Z trigger, arrows stick, Q/E L/R, I/J/K/L C up/left/down/right. Controllers supports four ports, per-button input additions beside Keyboard, named verified wired N64 and wireless USB dongle presets, SDL/Xbox mappings, custom remapping, stick sensitivity and deadzone.

Controllers includes **Memory Pak / Rumble Pak** selection, applied on the next launch. Memory storage currently attaches to Port 1. Not all controllers support rumble; check the controller manufacturer's specifications. Physical rumble compatibility depends on the device and SDL support.

**Enable cheat menu** works before pressing Start. **Enable all cheats**, directly below it, selects all 21 modifiers. Cheat controls save and apply automatically. Changes made in the native in-game cheat menu automatically update the launcher, including multi-value choices such as fog color and mass. Every unlocked option remains adjustable even when currently off. Individual choices remain available. Some modifiers hide the car/track or disable brakes/collisions. Master OFF restores normal modifiers but does not erase rewards already saved by the game.

## Unlisted controllers

Choose **Controllers → Add device → Unlisted**, enter the controller's name and type, select the connected device, map inputs beside Keyboard, then save and test. **Submit mapping for review** previews the chosen profile and opens the GitHub submission form; finish posting there. A local JSON copy is retained. ROMs, saves, device serials and other profiles are excluded.

[Controller review area](https://github.com/RemakerGamee/rush2049-original-pc/tree/main/controller-review). Profiles require maintainer approval and explicit preset integration before becoming distributed defaults; submissions never auto-install.

## Status and source

The local remaster scenes and latest UI received user review. This remains an experimental port: broader hardware testing, long-session stability, all gameplay modes and physical rumble acceptance are not certified. Some jet audio crackling remains. Report reproducible issues without attaching ROMs, saves or extracted game data.

Authored integration sources and modified dependency recipes are supplied under GPL-3.0; dependencies retain their own licenses. See [COPYING](COPYING) and [notices](THIRD-PARTY-NOTICES.md). Exact upstream commits and ROM-based generation recipes support developer rebuilds; raw generated original-game sources are not published. This is an unofficial fan project, unaffiliated with the original creators.
