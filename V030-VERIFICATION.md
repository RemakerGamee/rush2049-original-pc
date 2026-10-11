# Windows v0.3.0 verification

The final distribution build was reproduced in a new source/build tree from pinned upstream Git objects, the published patch recipes, and raw generated local US-ROM inputs. No donor objects or compiled libraries were reused. All 23 wrapper definitions were checked. The full Release compile and configuration, controller save/reload (89 checks) and controller submission checks passed. Patch files use LF endings so the recipe applies to clean Git exports on Windows or WSL.

The release includes the reviewed Dear ImGui settings and libultraship Config component, RTX 5080 RT64 Vulkan path, controller presets/remapping/submissions, Memory Pak / Rumble Pak preference, title-screen cheat activation and Enable all cheats button. Original and Remaster use the same compiled runtime; the launch choice controls the replacement texture pack and circuit lighting.

Windows Release build passed. Native configuration/state checks cover saved display choices and Pak modes, pre-Start cheat activation, all 21 modifier bindings, revision idempotence, transition refresh, persistence and master-OFF restoration. Controller profile save/reload and virtual device lifecycle passed 89 checks. Submission checks cover required name/type, profile/device matching, exported mappings, privacy, URL encoding and local JSON reload.

Both executable installers were tested using private fixture ROMs and save folders. Missing and wrong ROMs block installation before writes. Missing, moved and modified ROMs block both launcher and runtime. z64/v64/n64 byte orders work, Unicode paths work, the original test ROM remains unchanged, upgrades preserve player data, and installed folders contain no ROM or expanded game-data image. Player tests require no Python or build tools.

Installed native files completed bounded 25-second Original and Remaster startup/title/attract checks with exit code 0, RT64 presentation and graphics tasks, Dear ImGui draw activity, Memory Pak attachment and the NVIDIA GeForce RTX 5080. Shared Windows and WSL session locks protected other workers. These checks are startup/lifecycle evidence, not race completion or acceptance of every gameplay cheat effect. Earlier user review accepted the remastered scenes and latest cheat UI behavior.

Remaining general port limitations are listed in README. Physical rumble on every controller, a second-PC walkthrough, all gameplay modes and long-session stability are not certified. No drivers or system services were installed or changed. Personal saves, ROM/decomp and unrelated workers remain untouched.

Distribution must include checksums, audited corresponding authored sources and pinned modified-dependency recipes. The downloaded game requires the player's own supported US ROM on every launch; no stored-ROM fallback is used.
