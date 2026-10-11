# Dependency notices

This source-only archive contains integration sources and patches. It does not bundle third-party executables or the original game. Setup fetches exact recorded upstream source commits; the installed local build copies dependency license/notice files into `notices/`.

| Dependency | Upstream | License notice |
| --- | --- | --- |
| N64ModernRuntime | https://github.com/N64Recomp/N64ModernRuntime | GNU GPL v3; project `COPYING` retains the license text |
| N64Recomp | https://github.com/N64Recomp/N64Recomp | MIT, copyright 2024 Wiseguy; `notices/N64Recomp-LICENSE` |
| RT64 | https://github.com/rt64/rt64 | MIT, copyright 2024 RT64 Contributors; `notices/RT64-LICENSE` |

RT64's nested sources include Plume, SDL2, DXC, Dear ImGui and other libraries with their own notices. They remain in the fetched source trees; `install.py` preserves LICENSE/COPYING/NOTICE files, including SDL's required notice, in the local installation. Pinned Plume/DXC patch recipes are included. Dear ImGui is a transitive RT64 dependency; the separate new settings integration is not enabled by this release. libultraship is not a dependency of this original option.

Modified runtime, renderer, recompiler and nested dependency files are recorded as source patches under `tools/windows_native/`; setup applies them to fresh sources. No developer's dirty dependency working tree or prebuilt output is reused. Generated original-game sources remain local and are outside the project integration license grant.
