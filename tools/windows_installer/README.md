# Native Windows executable packaging (maintainers)

Players download the release Setup.exe; these instructions are for maintainers.

First build the native game from the pinned sources with `tools/windows_native/prepare_clean.py` and its documented generation/build workflow. The external-ROM patch and `rom_identity.hpp` must be applied. The build root must contain `source/` and `build/Release/` with the game, SDL2, DXC and DXIL binaries.

Create a text-only archive of the repository integration sources, preserving COPYING, dependency pins, patches and generation recipes. Exclude .git, binaries, generated original-game sources, ROMs, expanded images and local texture packs. Then run:

```powershell
python tools/windows_installer/build_installer.py --native-root C:\Build\Rush2049 --output C:\Build\Rush2049-Package --source-archive C:\Build\Integration-Source-v0.2.0.zip
```

Use a new output folder. This requires the documented Visual Studio C++ toolchain and CMake on the maintainer's machine. The installer embeds the compiled game, required app-local DLLs, notices and integration source archive. Setup and launcher link the C++ runtime statically. Check current redistribution terms for the licensed Visual Studio installation before publishing its redistributable DLLs.

For isolated verification, setup accepts `--install --rom PATH --directory NEW_APP --data-dir TEST_DATA --no-shortcut`. The installed launcher accepts `--preflight`, which validates its payload and saved external ROM without starting the game. Test missing, altered and moved ROMs before publishing. Preserve real player data; use private test copies and separate folders. See [binary verification](../../BINARY-VERIFICATION.md).

For the separate Remastered executable, pass --remaster-pack PATH pointing to the reviewed RT64 hash-version-5 pack with 5,957 mappings. Only the mapped PNGs and rt64.json are embedded; provenance captures and unrelated files are excluded. Keep artwork out of source Git history. The remastered launcher enables its bundled pack after clearing inherited diagnostic overrides.
