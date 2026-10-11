# v0.3.1 validation

The two v0.3.1 EXE installers provide Original and Remastered with Dear ImGui
settings and the libultraship Config component. The download table also retains
the two native-menu v0.2.0 choices.

## Automated checks

- Release builds of the game and RushLusConfigCheck succeeded.
- Configuration checks covered fullscreen fill at 720p, 1080p, ultrawide 1440p,
  4K and portrait sizes, VI crop relationships and saved fill/fit preferences.
- Cheat checks covered native INVINCIBLE OFF, exact fog and mass values,
  persistence/reload, unchanged-menu suppression and rejection of a stale
  guest revision after a newer launcher choice.
- Main cheat preferences retain the v2 format. Optional `.values` companion
  files preserve native multi-value settings; mismatching signatures are ignored.
- Both actual EXE installers passed missing/wrong ROM rejection, external Unicode
  ROM paths, moved/modified ROM rejection at launch, z64/v64/n64 byte orders,
  original ROM preservation and retained player data on upgrade. Tests used
  separate folders and temporary ROM copies, without modifying real player data.
- Both editions completed 25-second RTX 5080 startup/render/shutdown checks.
  Dear ImGui drew, the Controller Pak attached and the replacement texture pack
  loaded only for Remastered.
- A separate fullscreen review launched at 3440x1440 with `fillScreen=1`; the
  captured game window showed the game extending across the display. The user
  confirmed fullscreen fill and an in-game INVINCIBLE change appearing in F5.

These checks do not certify every gameplay mode, every cheat's gameplay effect,
all physical controllers or long sessions. Native in-game edits are captured on
the guest thread after the retail cheat-menu setter runs. The settings UI reads
the saved snapshot each frame; cheat controls save immediately.

## Packaging

No ROM, expanded original game data, raw generated game sources or player saves
are included. Installers retain the requirement for an external supported US ROM
before installation and on every launch. Authored corresponding integration
sources, pinned dependency recipes and license notices accompany the release.
