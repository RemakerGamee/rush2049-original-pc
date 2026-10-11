# Dependency notices

The v0.3 executable installer bundles the compiled integration and required runtime DLLs, without a ROM or extracted original game assets. The corresponding integration source and modified dependency recipes are provided; developer rebuilds fetch exact recorded upstream source commits. Dependency license/notice files are preserved under the installation's `notices/` directory.

| Dependency | Upstream | License notice |
| --- | --- | --- |
| N64ModernRuntime | https://github.com/N64Recomp/N64ModernRuntime | GNU GPL v3; project `COPYING` retains the license text |
| N64Recomp | https://github.com/N64Recomp/N64Recomp | MIT, copyright 2024 Wiseguy; `notices/N64Recomp-LICENSE` |
| RT64 | https://github.com/rt64/rt64 | MIT, copyright 2024 RT64 Contributors; `notices/RT64-LICENSE` |

RT64's nested sources include Plume, SDL2, DXC, Dear ImGui and other libraries with their own notices. They remain in the fetched source trees; `install.py` preserves LICENSE/COPYING/NOTICE files, including SDL's required notice, in the local installation. Pinned Plume/DXC patch recipes are included. Dear ImGui is a transitive RT64 dependency and powers the settings overlay. libultraship-switch Config is linked at commit cb52fcfefb73b3140f46b03195d1c10b757d49ad (MIT); its license and spdlog MIT notice are bundled. The renderer, runtime, SDL input and audio retain existing ownership; full libultraship platform ownership is not claimed.

Modified runtime, renderer, recompiler and nested dependency files are recorded as source patches under `tools/windows_native/`; the native build preparation applies them to fresh sources. The executable package uses the verified native build from those sources. Generated original-game sources remain local and are outside the project integration license grant.

The executable package includes app-local Microsoft Visual C++ redistributable DLLs from the licensed Visual Studio build installation. They retain Microsoft's license; see the current [redistributable files list](https://aka.ms/vs/18/redistribution) and [deployment terms](https://learn.microsoft.com/cpp/windows/redistributing-visual-cpp-files). No system runtime installer, driver or service is installed. The standalone setup and launcher use a statically linked C++ runtime. Release maintainers must update the bundled app-local runtime when needed.
