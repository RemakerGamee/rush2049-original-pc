# Windows executable v0.2.0 verification

Verified on 2026-10-11 on 64-bit Windows with an NVIDIA RTX 5080. This report covers the executable installer and direct external-ROM runtime. Earlier source-preview results are retained in [VERIFICATION.md](VERIFICATION.md).

The compiled installer was exercised with missing and incorrect ROMs: installation was refused before creating application or player-data folders. A supported US ROM installed successfully from a Unicode path outside the application. The saved location matched that external path. The installed application contained no ROM, expanded game-data image, original texture pack or player save.

The actual launcher passed its ROM check with z64, v64 and n64 inputs. Moving, removing or modifying the selected ROM blocked both the launcher and direct engine execution. Restoring the original ROM restored successful checks. The source ROM and test ROM bytes remained unchanged. Executable tests used a PATH containing only the Windows system directory; players do not need Python, compilers or build tools.

Installation into an existing app folder was refused. Installing into a new app folder retained existing player-data file hashes. The user's interactive installation also completed and its installed launcher passed the ROM-location check.

The installed engine ran a bounded 20-second startup using isolated test saves and the external ROM. It verified guest-decompressed sections, initialized RT64 on the RTX 5080, presented Vulkan frames, produced audio and exited with status zero after requesting normal shutdown. No ROM or expanded game-data image was written into its profile. This smoke test does not certify long-session gameplay or every game mode.

The release preserves the original integration, reviewed circuit-lighting recipe and 50% jet gain. Residual jet crackling, broader hardware/controller coverage, intermittent shutdown faults from historical testing and a fresh second-PC walkthrough remain open. The executable is unsigned. The Original installer excludes replacement textures; the Remastered installer includes the reviewed 5,957-texture 4x pack. Both require the external ROM.

The user confirmed the corrected Remastered center-outward circuit lighting. Native menu testing applied 1080p and 4K and toggled fullscreen on and off, with renderer changes recorded. F1 includes one combined Enable Cheats action, and the native Cheats entry was observed at the main menu after enabling. Individual cheat effects remain unverified. Controller defaults and custom button capture are checked with isolated SDL virtual devices; these checks are not a claim of physical Xbox hardware acceptance.

Build recipes, authored dependency patches, license notices and corresponding integration source accompany the executable. The standalone setup and launcher are native Win32 programs; app-local runtime DLLs accompany the game. Installation requires no administrator privileges, driver installation or system runtime installer.

Cheat activation was changed from context-dependent button playback to direct guest-menu settings. An isolated native regression exercises the actual F1 commands for Enable Cheats, All Cheats and all 21 independent ON/OFF selections, confirming checked controls, saved preferences, native row unlocks, mapped gameplay bytes, restoration of normal collisions/brakes and reusable panel cleanup. The guest menu consumes revisions before rendering/input, including settings selected before Start. The user confirmed that a selected cheat appeared active in the game's menu. The subsequently added persistent panel is component-tested; its live visual acceptance and all individual gameplay effects remain open.
