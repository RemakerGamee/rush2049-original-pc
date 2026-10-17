# Remastered libultraship v0.3.3: Updates, Support and rollback

This change adds Dear ImGui Updates and Support categories to the Original and
Remastered **libultraship** builds. The legacy native-menu executables have not
been retrofitted. Only the exact tested Remastered installer is authorized for this publication; the other public downloads remain unchanged.

Updates uses the approved `RemakerGamee/rush2049-original-pc` GitHub releases API.
It selects experimental Windows x64 libultraship installers by exact visual
edition, version, asset name, canonical repository URL, size and GitHub's
published SHA-256 digest. No release-note instructions are executed. Missing
digests, unexpected origins, oversized responses and incomplete/corrupt downloads
are refused. HTTPS redirects are checked individually against the GitHub asset
hosts. Embedded publisher signatures, when present, must pass Windows trust
verification; current approved Rush installers are unsigned.

Manual checking runs in a background worker. Startup checks default off and
persist through the existing libultraship configuration. The UI shows installed
and available versions, release notes as plain text, progress, cancellation and
errors. Enabling startup checks permits notifications only. Download and install
remain separate player actions; installation requires confirmation and shutdown.

The launcher starts the bundled update helper after the game exits. The helper
waits for its parent launcher, acquires the existing native/player-data locks,
refuses other Rush processes, rechecks trusted metadata, verifies the downloaded
artifact under a non-writable/non-deletable open handle and opens normal setup.
Setup receives a new versioned application folder and the existing player-data
folder. It retains the previous application. A moved ROM can be reselected in
setup. Preflight verifies the new installation; existing player files are hashed
before and after (excluding update housekeeping, locks and the deliberately
repairable ROM-location pointer). No game restart occurs automatically.

After a successful update, a Windows-user-protected (DPAPI) receipt retains both
application folders and a SHA-256 catalog of their immutable files. Rollback
verifies the retained previous application and switches the stable edition-specific
shortcut back to it **offline**. Both application versions remain available.
Tampered receipts, changed previous files, wrong editions/installation folders and
unsafe paths are refused. The game never restarts automatically. Shortcut failure
restores the prior receipt so a failed activation does not strand rollback state.

The launcher verifies its recovery helper separately from the game payload. If an
update damages the game executable, the launch-error dialog offers recovery using
that verified helper. Application-file verification is separate from ROM checking;
setup can repair a moved ROM pointer without weakening the gameplay ROM gate.
Rollback receipts are read once by the in-game tab, avoiding repeated decryption
of large remaster catalogs during gameplay. The helper revalidates before acting.

Support contacts are maintained in `tools/windows_native/rush-release-metadata.hpp`:
Discord `https://discord.gg/Z7P6WrDfBW`, email `retroremakes26@gmail.com`.
Discord opens the browser; Email opens an editable draft with only the port,
edition and version in the subject. Both destinations have copy fallbacks.
Nothing sends email/messages or attaches files automatically.

## Validation and remaining release gates

- Private overlay compiled against the existing reviewed RT64/runtime/guest
  objects; original build sources and outputs were left untouched. This was an
  incremental integration build, not an independent rebuild of all dependencies.
- Version/edition/channel selection, malformed metadata, unsafe origins,
  checksums, opt-in persistence, cancellation, running-game locks and preservation
  checks passed in compiled regression programs.
- Live GitHub checks found the approved Original/Remastered assets and digests.
  A real release was downloaded and verified in memory without execution;
  cancellation during transfer and response size limits were exercised.
- Actual private Original installation: fresh install, upgrades, Unicode/external
  ROM paths, missing/wrong/moved/modified ROM refusal and existing player-data
  preservation passed without Python/build tools in the player's PATH.
- Installed Original launcher/game: both tabs visible; manual check completed
  while the game continued; email copy fallback worked; Email opened an editable
  draft. The browser did not grant Computer Use access to inspect its draft;
  generated recipient/subject were checked in the compiled tests. No email was
  sent. Further window testing stopped when Scott stopped Computer Use.
- Real private installer integration cycles passed for both Original and
  Remastered: v0.3.3 -> a private v0.3.4 fixture -> v0.3.3 offline. The test runner
  calls the same installation, verification, activation and rollback transactions
  as the shipped helper, using actual compiled setup/launcher executables. The
  installer is driven through its unattended test entry; interactive dialogs are
  not clicked automatically. Private v0.3.4 fixtures have not been published.
- The final fault/recovery checks and rebuilt package validation are recorded in
  the local `updates-preview` reports. Test fixtures use separate ROM copies,
  disposable player data and private shortcut folders. No support email is sent.
## Explicit publication authorization

On 2026-10-17, after being told that this installer embeds ROM-derived game code
and the remaster texture pack, Scott explicitly selected:

> Allow publishing this exact tested installer

This authorizes only `Rush2049-Remastered-Libultraship-PC-v0.3.3-Setup.exe`, SHA-256
`a64fd1e62d7dfca5c8ad89db9088ac152d52c3700581763968c4c4b068ef8c1e`
(138,884,608 bytes). Its 5,957-texture pack and existing POOTERMAN icon/credit are
part of those exact tested bytes. No ROM, player data, private test fixture,
Original installer or new standalone artwork is authorized by this exception.

This does not establish a general exception for future versions or other ports,
or represent this installer as a local ROM-based code-generation workflow. The
standing public/private distribution requirements remain applicable outside this
specific authorization. The publicly supplied authored source, dependency
recipes, license notices and package manifest accompany the release.
