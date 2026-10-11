# v0.3.2 artwork update

All four installer choices use the supplied Rush 2049 PC artwork credited to
**[POOTERMAN](https://www.deviantart.com/pooterman)**. The original supplied PNG
is preserved as a public release asset. ICO conversion preserves the composition
and transparency, with 16, 24, 32, 48, 64, 128 and 256 pixel sizes.

The icon is embedded in each installer, launcher and game executable. All twelve
executables' seven icon sizes were extracted and compared with the supplied ICO.
Desktop shortcuts explicitly reference their launcher icon. Each package includes
ARTWORK-CREDITS.txt; the download page and release notes include the artist link.

Executable game code sections were hashed before and after resource embedding
and remained identical. Native-menu choices retain the v0.2.0 game core;
libultraship/ImGui choices retain the v0.3.1 game core, including its fullscreen
fill and two-way cheat synchronization. This release changes packaging, icons and
attribution, rather than game behavior.

All four actual installers passed isolated checks for ROM rejection before
installation, external Unicode ROM paths, missing/moved/modified ROM rejection
at launch, z64/v64/n64 byte orders, original ROM preservation and retained player
data during an upgrade. No game or real player saves were used by these checks.

Packages include corresponding authored integration source, notices and the
previous core's integration source archive. No ROM or expanded original game
data is included. Historical releases remain available.
