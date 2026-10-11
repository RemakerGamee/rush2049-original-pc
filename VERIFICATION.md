# Original source preview verification

Verified October 11, 2026 on the development Windows PC. These results establish reproducible preparation, compilation and installation, not full game acceptance.

| Check | Result |
| --- | --- |
| Fresh Windows setup from supported US ROM using this public source export | PASS: regenerated game/audio sources, configured, compiled, linked and installed into new folders |
| Pinned game-body audit | PASS: 2,053 required bodies, no missing or duplicate required bodies/polls |
| Native wrapper definitions | PASS: all 21 required definitions matched |
| Installed manifest and separate player-data preflight | PASS; no game launched |
| Windows installer regression suite | PASS: 19 tests, 17 passed and two privileged symlink fixtures skipped; real Windows junction and lock checks passed |
| Linux installer regression suite | PASS: 19 tests, 15 passed and four Windows-specific tests skipped |
| Linux metadata and generated-body audit regressions | PASS: 28 tests |
| Section metadata regressions | PASS: 12 tests |
| Clean source export checks | PASS: three tests; dirty donor files and cached executables not copied |
| Full optional local remaster pack installation | PASS: 5,957 mappings in each of two new versions, all installed hashes verified |
| Upgrade reusing test player data | PASS: test Pak/input bytes unchanged across both local-pack installations |
| Unsafe local pack paths and duplicate mappings | PASS: refused without a partial installation or modified player data |
| Fresh game/display/audio/physical-controller run from this exact public-source executable | NOT RUN for this packaging pass; concurrent game work preserved |
| Fresh different PC/toolchain walkthrough | NOT RUN |

Fresh local executable SHA256: `7ce00e4c798293ab5968907b95c277f6425c6e4d53abdc048a757b07164fa4e5`. The executable is private and is not distributed. Compiler/link logs, private generated-source identities, original ROM and test data remain local. Pinned dependency sources came from committed Git objects in an existing previously fetched donor; the build used no donor binaries or pre-generated game sources. The prior dependency-fetch workflow was independently checked during October 10 setup validation.

The source-preparation receipt records generation only; its `native_build_completed` field remains false at that preparation stage. The subsequent successful native configure/link logs and installed manifest are the build/install evidence.

Historical RTX 5080 scenes and user-approved remaster reviews belong to earlier executable identities. They do not prove new-PC compatibility or full acceptance of this release. No completed-race, earned-progression, all-mode, all-variant, four-player or endurance claim is made. Residual jet crackling remains explicitly deferred. Shutdown changes are included as reviewed source recipes; they are not a guarantee against every shutdown fault.

The public package is curated from the reviewed ROM-only setup baseline and original remaster integration. It contains only text sources, metadata, recipes, documentation and notices. A private-data/credential audit is required before its initial commit and the deterministic archive is checked against the release manifest. ROMs, generated game/RSP code, artwork, builds, recordings and personal settings are excluded. The separate libultraship/Dear ImGui work is not imported.
