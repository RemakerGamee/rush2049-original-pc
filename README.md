# Rush 2049 Original PC

Unofficial **experimental Windows source release** of San Francisco Rush 2049 using the original SDL2/runtime integration and RT64 Vulkan renderer. This is the original project option. A separate libultraship/Dear ImGui option is being developed; it is not released here.

**You must supply your own supported original US Nintendo 64 ROM.** This download includes authored source, build recipes and dependency notices. It includes no ROM, generated game/RSP code, game executable, extracted artwork, saves or recordings. Setup generates the game sources and executable locally from your ROM without modifying it.

## Download and build

Download `Rush2049-Original-PC-v0.1.0-Windows-Source-Setup.zip` from [Releases](../../releases). Extract to a short local path, for example `C:\RushOriginalSetup`.

Prerequisites:

- 64-bit Windows with Vulkan-capable graphics hardware and its existing driver.
- [Python](https://www.python.org/downloads/windows/) 3.12 or newer, [Git](https://git-scm.com/downloads/win), [CMake](https://cmake.org/download/), and Visual Studio C++ x64 build tools with a Windows SDK. The verified toolchain uses the `Visual Studio 18 2026` generator; other toolchains have not been verified.
- Internet access for the pinned open-source dependencies and several GB of free disk space.
- Original US 12 MiB ROM. Normalized SHA1: `3f99351d7bb61656614bdb2aa1a90cfe55d1922c`. z64, v64 and n64 byte orders are accepted; other versions are refused.

Double-click `Start-Setup.cmd`, enter your ROM path and a **new**, short installation folder. Setup downloads pinned sources, generates private game code, compiles and installs. It does not install prerequisites, drivers or services. After success, double-click the installed `start.cmd`. Python remains required for launch.

Command-line equivalent:

```powershell
python tools/release/setup_original.py --rom "C:\My ROMs\rush2049.z64" --install-dir C:\Games\Rush2049Original-v0.1
```

Player saves and remaps default to `%LOCALAPPDATA%\Rush2049Original\PlayerData`, outside the installed game. Upgrades use a **new installation folder** and the **same player-data folder**. Existing installs are refused and existing player data is retained. See [setup and migration](tools/release/README.md).

Keyboard: Enter = Start, Z = A, X = B, Space = Z trigger, arrows = stick. F8 opens the existing controller configuration. Wired KIWITATA and SAFFUN defaults are included; broader device compatibility remains unverified.

## Visual remaster support

The release includes the native original-menu circuit-lighting recipe, optional RT64 v5 local replacement-pack loading, and the reviewed 50% jet-gain adjustment. Residual jet crackling is unresolved. Without a replacement pack, the game uses its ROM textures.

The separately reviewed **5,957-texture 4x pack is retained locally and is not included in this public source download**: it contains restored original game artwork. This release does not promise those textures to downloaders. If you have a compatible local pack, install it with:

```powershell
python tools/release/setup_original.py --rom "C:\My ROMs\rush2049.z64" --install-dir C:\Games\Rush2049Original-LocalPack --remaster-pack "C:\My Local Pack"
```

The installer validates pack paths/hashes, copies the pack into the new installation and records file checksums. Do not share the resulting local installation: it contains your ROM and privately generated game data.

## Preview limitations

This is not a claim of a finished stable port. Historical local tests reached menus and gameplay on an RTX 5080, and the remaster's reviewed scenes received user visual approval. That does not certify every car variant, alternate route, Battle/Obstacle mode, earned progression, four-player setup or long session. Startup hitches, wider hardware compatibility and intermittent shutdown faults remain acceptance work. The options integration is separate. See [verification](VERIFICATION.md).

This release is unsigned and source-built. A friendly binary installer and a fresh second-PC walkthrough are unfinished. Report reproducible problems using Issues, including your GPU/toolchain and whether you used a local pack. Do not attach ROMs, extracted textures, saves or generated game sources.

## Sources and licenses

Project integration sources are distributed under GPL-3.0; see [COPYING](COPYING). Third-party sources retain their own licenses and are fetched from [pinned upstream commits](tools/windows_native/dependencies.json). [Notices](THIRD-PARTY-NOTICES.md) describe those dependencies. The original game and artwork are not licensed by this project. This fan project is unofficial and unaffiliated with the original creators.

The release archive is accompanied by `SHA256SUMS.txt` and a per-file `package-manifest.json`. No private development history is included.
