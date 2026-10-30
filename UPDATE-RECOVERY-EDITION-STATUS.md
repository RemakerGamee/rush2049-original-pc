# Update and recovery extension: source candidate

Base: `7d200a437689666809429c6bab64ad80cd5177b4` (2026-10-30 inspection).
This is an unpublished source candidate. Public installers, README download
references, artwork attribution and third-party licenses are unchanged.

## Inspection and implementation

The public release assets confirm that Original native, Remastered native and
Original libultraship remain at v0.3.2. Only Remastered libultraship has a public
v0.3.3 installer. Existing release selection hard-coded libultraship asset names;
its helper target and payload were excluded from native builds.

- Reuse the release service, WinHTTP transfer, SHA-256 checks, protected rollback
  catalogs and helper for both interfaces; libultraship keeps its existing UI.
- Add a modeless Win32 Updates window to native builds, appending one menu item
  without replacing existing controls, renderer or gameplay implementation.
- Bind selection, installed metadata, shortcut activation and recovery to both
  visual edition and settings runtime. Refuse cross-edition/interface packages.
- Use independent protected history files for the four identities. Existing
  v0.3.3 receipts remain readable only for the matching libultraship edition.
- Keep application updates in new sibling directories with the existing data
  pointer; retain prior apps, Memory Pak, controller/display/options files and
  external ROM references. All installation and rollback actions remain explicit.
- Include the independently verified recovery helper in all packages. Require a
  matching build-runtime marker before packaging to prevent mislabeled builds.
- Keep each new package single-edition; a Remastered package cannot quietly
  select Original and then follow Original's update stream.

## Validation and release gates

Local portable release-selection and representative packaging tests pass.
Windows x64 cross-compilation passes for the shared updater, transaction checks,
native Updates UI checks, and synthetic native launcher/setup builds. This does
not constitute Windows runtime or game compatibility acceptance.

Windows execution in this environment is blocked: Wine cannot create its required
socket (`Operation not permitted`). Native Windows CI passed all five C++ tests and both packaging tests on
Windows Server 2025 / MSVC 19.51 (run 41075324931). This includes live download
SHA-256 verification, in-transfer cancellation, all four synthetic update/rollback
transactions, the native Updates panel and damaged current metadata recovery.
The real native CMake recipe was additionally checked with a synthetic executable:
its runtime marker appears only after the executable builds, not at configure time. Real private installer cycles and
interactive game/UI acceptance still require the private Windows build inputs,
ROM copy and reviewed artwork/texture inputs; those are not in this checkout.
No new complete game installer was built or published here.

See `tools/windows_update_tests/README.md` for exact checks and the three real
private package acceptance cycles. Publication requires successful compatibility
results and separate authorization tied to the final candidate binary hashes.
Existing v0.3.3 publication authorization remains limited to its original tested
Remastered libultraship bytes.
