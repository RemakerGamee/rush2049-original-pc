# Rush 2049 Original PC

Unofficial **experimental Windows executable release** of San Francisco Rush 2049 using the original SDL2/runtime integration and RT64 Vulkan renderer. This is the original project option. A separate libultraship/Dear ImGui option is being developed; it is not released here.

**You must supply your own supported original US Nintendo 64 ROM before installation.** The executable installer validates it, remembers its current location and checks it before every game launch. Your ROM stays in your chosen folder and is never modified or copied into the installation. No ROM, expanded game-data images, saves or recordings are bundled. The Remastered download includes the reviewed restored texture pack. The compiled game, launcher and required runtime DLLs are included.

## Download and play

Choose the [Original installer](https://github.com/RemakerGamee/rush2049-original-pc/releases/download/v0.2.0/Rush2049-Original-PC-v0.2.0-Setup.exe). For 4x restored textures, choose the [Remastered installer](https://github.com/RemakerGamee/rush2049-original-pc/releases/download/v0.2.0/Rush2049-Remastered-PC-v0.2.0-Setup.exe). Both are ready-to-install executables. Run your chosen installer, click **Browse**, and select your original US N64 ROM. **Install** becomes available only after the supported ROM is verified. Choose a new installation folder and click Install. Open the desktop shortcut or installed `Rush2049Original.exe` to play.

Prerequisites:

- 64-bit Windows with Vulkan-capable graphics hardware and its existing driver.
- Original US 12 MiB ROM. Normalized SHA1: `3f99351d7bb61656614bdb2aa1a90cfe55d1922c`. z64, v64 and n64 byte orders are accepted; other versions are refused.

**Players need no Python, Git, CMake or compiler tools.** Required DLLs are installed next to the game. Setup does not install drivers or services and runs under the current Windows user.

The launcher checks the saved ROM location and complete supported identity every time. If the file was moved, removed or changed, gameplay is blocked and you are asked to select it again. A valid new selection updates the saved location. There is no installed ROM copy or cached fallback. z64/v64/n64 byte order is normalized in memory; temporary ROM files are not created.

Player saves and remaps default to `%LOCALAPPDATA%\Rush2049Original\PlayerData`, outside the installed game. Upgrades use a **new installation folder** and the **same player-data folder**. Existing installs are refused and existing player data is retained. The default app folder is `%LOCALAPPDATA%\Rush2049Original\App-v0.2.0`.

Keyboard: Enter = Start, Z = A, X = B, Space = Z trigger, arrows = stick. The top menu opens **Cheats (F1)**, **Controller (F8)** and **Display (F9)**. **F11** toggles fullscreen.

F1 opens a cheat panel that stays open while you make selections. Click outside it, **Done**, or its close button when finished. **Enable Cheats** works before or after Start. **All Cheats**, directly beneath the selection heading, toggles every cheat together. Check an individual cheat to activate it; click it again to turn it off. Selections are saved and applied directly when the game's main menu runs, without entering button codes or selecting a matching in-game entry. Choices made during a race apply on returning to the main menu. Value cheats show their selected value beside the name; collisions and brakes show a “disable” modifier. Turning off Enable Cheats restores normal modifiers, but does not erase rewards already saved by the game. Cheat effects are not all independently verified.

F8 shows keyboard and controller bindings side by side. **Xbox One Controller**, **Wired N64 Controller (KIWITATA)** and **Wireless N64 / USB Dongle Controller (SAFFUN)** presets fill the bindings for the connected device; **Change** remaps each button, D-pad direction, trigger or stick axis. **Save** retains your mapping. N64 USB layouts vary by adapter; custom mapping remains available.

F9 offers **720p**, **1080p**, **1440p 16:9**, **1440p ultrawide 21:9 (3440×1440)** and **4K**, plus VSync and fullscreen. Display choices are saved beside your controller configuration. Existing game menu artwork retains its original layout.

## Visual remaster support

The native game includes the reviewed original-menu circuit-lighting recipe and 50% jet-gain adjustment. Residual jet crackling is unresolved. The executable download uses the original textures read from your ROM.

The **Remastered installer includes the reviewed 5,957-texture 4x pack**, retaining the original artwork, UV canvas and alpha. The Original installer uses original ROM textures. Separate default app folders and desktop shortcuts let you keep both editions; they share your retained player-data folder. Neither edition contains a ROM. The optional local-pack source-build workflow remains available to developers:

```powershell
python tools/release/setup_original.py --rom "C:\My ROMs\rush2049.z64" --install-dir C:\Games\Rush2049Original-LocalPack --remaster-pack "C:\My Local Pack"
```

That developer source-build workflow is separate from the no-build-tools executable installer. Do not share a developer-built ROM-bearing installation.

## Preview limitations

This is not a claim of a finished stable port. Historical local tests reached menus and gameplay on an RTX 5080, and the remaster's reviewed scenes received user visual approval. That does not certify every car variant, alternate route, Battle/Obstacle mode, earned progression, four-player setup or long session. Startup hitches, wider hardware compatibility and intermittent shutdown faults remain acceptance work. The options integration is separate. See [executable verification](BINARY-VERIFICATION.md).

This release is unsigned. A fresh second-PC walkthrough remains unfinished. Report reproducible problems using Issues, including your GPU and game version. Do not attach ROMs, extracted textures, saves or generated game sources.

## Sources and licenses

Project integration sources are distributed under GPL-3.0; see [COPYING](COPYING). Third-party sources retain their own licenses and are fetched from [pinned upstream commits](tools/windows_native/dependencies.json). [Notices](THIRD-PARTY-NOTICES.md) describe those dependencies. The original game and artwork are not licensed by this project. This fan project is unofficial and unaffiliated with the original creators.

The installer is accompanied by `SHA256SUMS.txt` and a per-file `package-manifest.json`. Corresponding authored integration source is supplied in the release and inside the installation as `Integration-Source-v0.2.0.zip`; pinned upstream sources and ROM-based generation recipes allow developer rebuilds. No private development history or raw ROM-derived game sources are included.
