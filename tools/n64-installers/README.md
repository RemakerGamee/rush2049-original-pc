# N64 installer rollout inputs

The catalog binds each of the 61 N64 titles currently listed on RetroRemakes to
its own Windows installer/update/support requirement. No title is marked as
having an installer until its real payload and installer have been validated.
This catalog is not 61 completed installers or 61 verified game ports.

The missing input is each game's existing native build directory. The website
contains title listings, not these build directories. The available GitHub Mario
Party checkpoint explicitly remains incomplete; do not replace that status with
a release claim.

On the Windows build PC, put the already built games under one folder, with each
immediate child named exactly as the title or game ID in catalog.json. Keep the
catalog and export script together. From PowerShell, for example:

```powershell
powershell -NoProfile -File .\Export-N64-Installer-Inputs.ps1 -BuildRoot "G:\PC PORTS\N64 Built Games" -OutputZip "G:\PC PORTS\RR DATA\N64-Installer-Inputs.zip"
```

Use the actual built-games location. The example is not a claim that these paths
exist on the PC. The output must be new and outside the built-game root. No game
runs, settings change, existing installation changes or automatic uploads occur.

The exporter includes matching game executables, runtime files and licenses. It
excludes ROM extensions and N64 ROM magic, saves, common player settings, logs,
credentials, developer build outputs and linked directories. It records SHA-256
and length for every included file and verifies the final archive bytes. Review
`installer-inputs.json`, including unmatched folders and errors, before privately
sharing the export. Matching folder names identifies candidates; exact game
identity, launch arguments, ROM references and save/config paths still require
review against the executable/build records.

After obtaining these inputs, each game needs its own installed identity,
versioned application directory, external player-data location, installer,
verified update feed, offline recovery record and Support contacts. Preserve
required artwork/third-party licenses and existing installations. Validate real
install/upgrade/cancel/failure/rollback cycles before linking a public installer.
The current website's local-build distribution rules remain in force; private
ROM-derived payloads are not authorized for public upload by this export.
