# Edition update and recovery checks

These checks do not publish releases, install a game, or use a ROM. They exercise
the actual updater transactions with synthetic application fixtures. They do not
establish gameplay, real installer, or graphics compatibility.

On Windows, supply the existing pinned RT64 JSON header under a directory named
`nlohmann/json.hpp`, or the nlohmann/json v3.11.3 single_include directory:

```powershell
cmake -S tools/windows_update_tests -B update-checks -A x64 -DRUSH_JSON_INCLUDE_DIR=PATH_TO_JSON_INCLUDE
cmake --build update-checks --config Release --parallel 2
ctest --test-dir update-checks -C Release --output-on-failure
python -m unittest discover -s tools/windows_installer -p test_update_packaging.py -v
```

The release model check also builds on Linux. `RushUpdateModelCheck releases.json`
can inspect a saved GitHub releases API response without downloading or executing
any installer. Native Windows UI and transaction checks require Windows.

Coverage: four package identities and all 16 selection combinations; SHA-256 and
incomplete download refusal; installer cancellation; wrong edition/interface;
failed new-app preflight; protected receipt corruption; retained-file tampering;
shortcut activation failure; offline recovery with a broken current game and missing current metadata;
byte-identical saves, controller/display/options files, ROM pointer and retained
application folders; legacy v0.3.3 libultraship receipts; native menu preservation; native Support contacts, email-draft identity and copy fallbacks without opening external apps during tests.
Enable `-DRUSH_TEST_LIVE_UPDATES=ON` for metadata checks of all four public
streams and one representative native installer download. It checks SHA-256,
tampering, in-transfer cancellation and response bounds entirely in memory;
no installer is saved or executed. Windows CI enables this check.

## Full private package acceptance (required separately)

Prepare separate fresh build roots with `RUSH_ENABLE_LUS_IMGUI=OFF` for native and
`ON` for libultraship. Build the game and PortUpdateAgent. The generated
`build/rush-build-runtime.txt` must match packaging's `--native-menus` selection.
Package Original native, Remastered native and Original libultraship. Remastered
packages are now single-edition, including when the old optional
`--remaster-only` flag is omitted. Use the existing approved icon/credits/notices.

Build isolated private 0.3.3 and 0.3.4 fixtures for each identity; these versions
are test inputs, not an instruction to replace public assets. With a separate ROM
copy, new test data and an unused test root:

```powershell
RushUpdateAgentCheck.exe --cycle PRIVATE_033_SETUP PRIVATE_034_SETUP NEW_TEST_ROOT original TEST_ROM native
RushUpdateAgentCheck.exe --cycle PRIVATE_033_SETUP PRIVATE_034_SETUP NEW_TEST_ROOT remastered TEST_ROM native
RushUpdateAgentCheck.exe --cycle PRIVATE_033_SETUP PRIVATE_034_SETUP NEW_TEST_ROOT original TEST_ROM libultraship
```

Each cycle uses the production install/rollback transactions, isolated shortcuts,
ROM relocation and damaged-current-game recovery checks. Do not point it at an
existing installation, real saves or a live ROM. The harness intentionally
modifies only its private copies. Repeat acceptance against a copy of the actual
v0.3.2 installed layout to confirm the first manual upgrade preserves the chosen
application parent and player-data pointer. v0.3.2 executables cannot gain an
Updates menu without installing a new version into a new application folder.

Manually verify the native Updates window while gameplay continues, check/download
cancellation, normal close-and-install, missing/moved ROM reselection, Unicode and
external paths, all controls/display settings, and unchanged Original/Remastered
visuals. Confirm the real player-data folder and all older apps remain untouched.
Recheck the Remastered libultraship path for regressions. Offline rollback must
succeed with networking disconnected, and damaged game launch must offer the
separately verified recovery helper.

Only after these checks pass, record exact candidate installer names, sizes and
SHA-256 values for publication authorization. Then publish *new* assets with
GitHub SHA-256 digests and update download references. Retain every old download.
The existing authorization for one Remastered libultraship v0.3.3 binary does not
authorize any new binary. No publication or download-reference changes are part
of this source preparation.
