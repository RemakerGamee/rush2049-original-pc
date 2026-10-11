# Original Windows setup and player data

Run `python tools/release/setup_original.py --rom "C:/My ROMs/rush2049.z64" --install-dir C:/Games/Rush2049Original-v0.1` from the extracted source kit. Requirements and preview limitations are in the root README. Choose new, short paths outside synced folders; `--workspace C:/RushBuild-v0.1` supplies an explicit new build folder. Failed build folders are retained for diagnosis; use a new path on retry.

Setup checks the ROM before fetching pinned sources, builds the game/audio recompilers, generates and audits private game sources, compiles RT64/runtime/game, then installs. `--dependency-donor` may point to already fetched pinned Git repositories; only committed Git objects are exported, never dirty files or old generated/game binaries. Dependencies are otherwise fetched automatically. Setup does not launch a game.

## Saves and upgrades

Default player data: `%LOCALAPPDATA%/Rush2049Original/PlayerData`. Existing data is retained. A new version must use a new installation directory and the same `--data-dir`. Never store player data inside the installation. Keep the old version until the new one is verified.

For explicit migration, close the old game and choose a NEW data folder:

```powershell
python tools/release/install.py data C:/RushPlayerData-Migrated --import-pak "C:/Old Save/controller.mpk" --import-input-config "C:/Old Settings/input.cfg"
python tools/release/setup_original.py --rom "C:/My ROMs/rush2049.z64" --install-dir C:/Games/Rush2049Original-v0.1 --data-dir C:/RushPlayerData-Migrated
```

Imports copy exactly the explicitly selected 32 KiB Pak and version-1 input config. Existing destinations and linked/junction paths are refused. Originals remain untouched. A size-valid Pak does not establish guest filesystem integrity. Other JSON/profile formats are not automatically imported; keep their originals. No personal-save discovery occurs.

Optional local textures: add `--remaster-pack "C:/My Local RT64 Pack"`. The pack must contain RT64 configuration version 3/hash version 5 and contained PNG paths with unique texture hashes. No pack is bundled in this public release. Installation validates the pack and includes its files in the local checksum manifest.

## Launch and removal

Double-click installed `start.cmd` (Python required). The launcher verifies installed hashes, enumerates existing Rush processes, and acquires a Windows session mutex and player-data file lock before starting. Another Rush session is refused. F8 opens the existing controller settings; remaps stay in player data. Optional PowerShell scripts require an already permitted shell; no execution-policy bypass is used.

To remove a version, close it and delete only its chosen installation/build folders. Preserve the separate player-data folder and original ROM. Never upload a locally built installation: it contains the player's ROM and privately regenerated game images.

## Source checks

`python tools/release/test_release.py` checks migration, byte-order handling, non-replacing installation, exact data preservation, real Windows mutex/file-lock behavior and dependency source export. Synthetic Windows lock tests use unique test mutexes so they never acquire the actual game-session mutex. Metadata FIFO tests require Linux; run `python3 -m unittest discover -s tools -p 'test_audit*.py'` there. Tests do not certify graphics, gameplay or listening.
