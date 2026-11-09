# AGENTS.md

Guidance for coding agents working on Vanta. Read this before touching code.

## What Vanta is

A Linux game launcher written in C++17 with Qt 6 Widgets. It keeps a local
library of games the user already owns, launches native Linux binaries and
Windows binaries through Wine or Proton, discovers runners other launchers
installed, and reports what it finds inside a game folder.

Two product rules outrank everything else:

1. **Vanta never downloads games.** It has no store integration and no
   acquisition path. Do not add one.
2. **Vanta never claims a detected OnlineFix works.** Detection is a signature
   match, not proof. Any UI text, log line or doc that implies functionality is
   a bug.

## Build and run

```bash
# Configure (Ninja is the supported generator)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build -j"$(nproc)"

# Run
./build/vanta
```

Requirements: CMake ≥ 3.21, a C++17 compiler, Qt ≥ 6.2 with the `Widgets`,
`Svg`, `Concurrent` and `Network` modules, and zlib.

### Why Qt 6.2 is the floor

`find_package(Qt6 6.2 ...)` is deliberate, not stale. Release packages are built
on Ubuntu 22.04 so that one set of binaries installs across Ubuntu 22.04+,
Debian 12+, Fedora, openSUSE and Arch. Raising the minimum Qt version raises the
ABI requirement of the shipped binary and silently drops distributions.

Rules that follow from this:

- Do not call an API newer than Qt 6.2 without a `QT_VERSION_CHECK` guard and a
  fallback. `QStyleHints::colorScheme()` (6.5) is the existing example.
- `qt_standard_project_setup()` is guarded with `if(COMMAND ...)` because it
  only exists from Qt 6.3.
- The release workflow fails if the binary needs a `Qt_6.x` symbol above 6.2,
  glibc above 2.35, GLIBCXX above 3.4.30 or CXXABI above 1.3.13. Those are the
  Ubuntu 22.04 runtime ceilings. A local build with a newer toolchain will
  exceed them; that is fine for development, but never ship such a binary.

A build is only acceptable when it produces **zero warnings** with the
`-Wall -Wextra` flags the top-level `CMakeLists.txt` already sets. Warnings are
treated as errors in review even though the build does not use `-Werror`.

Headless smoke testing (containers, CI, no display):

```bash
Xvfb :99 -screen 0 1600x900x24 &
DISPLAY=:99 QT_QPA_PLATFORM=xcb ./build/vanta
```

When testing, always redirect the XDG directories so a test run cannot touch a
real library:

```bash
export XDG_CONFIG_HOME=/tmp/vanta-test/config
export XDG_DATA_HOME=/tmp/vanta-test/data
export XDG_CACHE_HOME=/tmp/vanta-test/cache
```

Never synthesise mouse or keyboard input against a user's real desktop session.
Use Xvfb.

## Code conventions

These are enforced in review and, where possible, by the compiler.

- **C++17**, no extensions (`CMAKE_CXX_EXTENSIONS OFF`).
- **No raw owning pointers.** Use `std::unique_ptr`, `QScopedPointer`,
  value members, or Qt's parent-child ownership. Raw pointers are only for
  non-owning references and are documented as such when ambiguous.
- **`QT_NO_CAST_FROM_ASCII` and `QT_NO_CAST_TO_ASCII` are defined.** Every
  string literal handed to a Qt API must be wrapped: `QStringLiteral("...")`,
  or `QLatin1String` where a lightweight comparison is enough. `"text"` will
  not compile, and that is intentional.
- **`QT_USE_QSTRINGBUILDER` is defined.** Prefer `QStringLiteral("%1").arg(x)`
  and `+` over manual `QString` juggling; avoid `QString::append` chains that
  defeat the builder.
- **No TODOs, stubs, placeholders, or "coming soon" code.** If a feature is not
  implemented, it does not ship. `grep -rn "TODO\|FIXME\|XXX"` must stay empty.
- **No `Q_OBJECT` in a `.cpp`** — moc runs on headers only through AUTOMOC.
- Prefer forward declarations in headers and `#include` in `.cpp`.
- Comments explain *why*, not *what*. A comment restating the next line is
  noise and will be removed.
- New source files must be added to `VANTA_SOURCES` in `CMakeLists.txt`,
  otherwise they are silently not compiled.

## Architecture

```
src/
  main.cpp              Entry point, app metadata, theme bootstrap
  Version.h             VANTA_VERSION and VANTA_APP_ID

  core/                 No Qt Widgets. Logic must be testable headless.
    Game.*              Game value type, paths, executable probing
    Library.*           In-memory library + settings, save/load via Storage
    Storage.*           XDG-aware JSON persistence (atomic writes)
    Scanner.*           Read-only folder inspection, signature detection
    ScanController.*    QtConcurrent orchestration and cache
    Launcher.*          Process launch, playtime accounting, signals
    LaunchPlan.*        Builds the argv/env for a launch
    RunnerDetector.*    Finds Wine/Proton installs across launchers
    RunnerInstaller.*   Installs and uninstalls Wine/GE-Proton
    LibraryImporter.*   Read-only Lutris/Heroic discovery and porting
    OnlineFix.*         Signature detection and confirmed INI writes
    SteamPrecondition.* Steam-closed prompts before an OnlineFix launch
    BepInEx.*           Installation detection, plugin listing, safe toggle
    ModInstaller.*      ZIP reading, install planning, backups, atomic writes
    Thunderstore.*      Community lookup, package listing, dependency resolution
    LaunchLogs.*        Per-launch log store under the XDG data directory
    IniFile.*           INI parsing that preserves formatting
    CoverFetcher.*      SteamGridDB search, artwork listing, downloads
    UpdateChecker.*     GitHub latest-release lookup, version comparison
    ThemeManager.*      ThemeColors, palettes and the QSS
    Types.*             Enums and their display names

  ui/                   Qt Widgets only. No file I/O, no process spawning.
    MainWindow.*        Wiring, actions, dialogs, status bar
    GameCard.*          Grid card painting
    GameGridView.*      Grid layout, keyboard nav
    GameListView.*      Table view
    LibraryView.*       Switches grid/list, empty states
    GameSelectionBar.*  Bottom detail strip with slide animation
    CoverPickerDialog.* Artwork grid with preview
    LibraryImportDialog.* External library picker with duplicate marking
    RunnerInstallDialog.* Runner download/uninstall with version picker
    SettingsDialog.*    Preferences and runner/import entry points
    Sidebar.*           Source grouping and filter list
    UpdateBanner.*      Newer-release strip above the library
    AddEditDialog.*     Add/edit form, executable-only path entry
    GameDetailsDialog.* Read-only detail sheet
    OnlineFixDialog.*   Detection report and confirmed actions
    ModManagerDialog.*  BepInEx mod list, toggle, local import, Thunderstore entry
    ThunderstoreDialog.* Community browser, package search, dependency install
    LogsDialog.*        Terminal-styled launch log viewer
    LaunchProgressDialog.* Staged launch feedback
    IconFactory.*       Glyph and brand SVG rendering
    Common.*            Small shared UI helpers
```

The `core`/`ui` split is a hard boundary: `core` must never include a Widgets
header. It keeps scanning and launching testable without a display.

### External library import

`LibraryImporter` is **read-only against other launchers**. It queries Lutris'
`pga.db` through the optional `sqlite3` CLI (falling back to the YAML files in
`games/`), reads Heroic's JSON configuration, and discovers local covers under
Lutris' `coverart/`/`banners/` and Heroic's `icons/` directories. It never
writes to, moves, or deletes anything owned by Lutris or Heroic.

Steam and Bottles are deliberately not scanned; importing them duplicated most
of a normal library and buried the games the user actually added. Porting only
adds entries to Vanta's own library, marks candidates that are already present,
and references discovered cover files in place rather than copying them.

### Mod manager and Thunderstore

`BepInEx` detects an installation relative to the executable and the game folder,
and lists `plugins/` and `patchers/`. Enabling and disabling is a rename between
`.dll` and `.dll.disabled`; see the mod safety rules below for what may never
happen. `ModInstaller` turns a `.dll` or `.zip` into an install plan and only
touches the disk after the plan is complete. `Thunderstore` is a plain network
client with no UI: it resolves a community slug from a name, lists packages,
parses package links, resolves dependencies depth-first and downloads versions.

Two behaviours are deliberate:

- `BepInExPack` is skipped when BepInEx is already installed, because
  reinstalling the pack would overwrite the user's core files and config. When
  the user asks for the pack itself, that rule inverts: it is the supported way
  to install or change the loader, so the dialog confirms the replacement of the
  installed core DLLs and the normal backup path protects the old files.
- `r2modman` is refused. It is a mod manager application, not a BepInEx plugin,
  and installing it into `plugins/` would do nothing.
- A dependency pinned to a version the community does not publish falls back to
  its newest release, and the result message names both versions. A pinned
  version is a request from the package author, never a guarantee.

The community is guessed from the game name on first use and remembered in
`Game::thunderstoreCommunity`, so the browser opens on the right community next
time.

### Launch logs

`LaunchLogs` is a separate store, not part of `Storage`: one plain text file per
launch under `$XDG_DATA_HOME/vanta/logs/<game id>/<timestamp>.log`. `Launcher`
writes the file when a launch ends and emits its path alongside the existing
diagnostics, so the UI can offer it after the process is gone.

Two rules keep the viewer honest:

- The log is what Vanta ran (command, workdir, environment overrides, plan
  diagnostics) followed by what the process printed. It is evidence, not a
  verdict; never label it a cause of failure.
- `LogsDialog` only reads. **Clear logs** deletes stored logs for that game and
  nothing else; it never touches the game folder or the prefix.

`WINEDEBUG` is driven by `AppSettings::wineLogLevel` (Settings → Runners). The
default `Errors only` is `-all,err+all`: Wine's `err` channel stays visible while
`fixme` chatter — hundreds of lines per launch — is suppressed. Do not restore
the old unconditional `WINEDEBUG=-all`, because it leaves the viewer empty.

### Update notifications

`UpdateChecker` reads one public endpoint,
`https://api.github.com/repos/ArthurSimin/Vanta/releases/latest`. It keeps no
state, writes no file and never fetches a package. `UpdateBanner` only offers
**Open release page**, which hands the URL to the desktop browser.

Rules that follow from the distribution model:

- **Never download or install an update.** Vanta is installed as a package, so
  replacing its own files would fight the package manager and could leave a
  half-upgraded install. The check exists to tell the user a newer version is
  out, nothing more.
- Version comparison is numeric per segment (`1.0.10` is newer than `1.0.9`).
  A letter suffix marks a pre-release and ranks below the plain release; a
  numeric packaging suffix (`1.0.3-1`) ranks equal. Do not reduce it to a string
  comparison.
- A build without a numeric version (a working-tree development build) skips the
  check. A failed check is a status line, and only when the user asked for it —
  never a dialog on startup.
- The startup check is a single delayed request. Do not add polling, periodic
  re-checks or a background timer.

## Versioning

Vanta versions are **`MAJOR.MINOR.PATCH`** — exactly three dot-separated
numbers, never two and never four. `1.0.4` is a version; `1.0`, `1` and
`1.0.4.1` are not. Each bump raises one segment by one and leaves the others
alone. Pick the segment from what the release contains, not from how big the
diff looks:

- **PATCH** (`1.0.4`) — bug fixes only. A release that changes no behaviour the
  user can ask for, only behaviour that was wrong, is a patch. This is the
  common case.
- **MINOR** (`1.1.0`) — a small, backward-compatible feature. Adding something
  the user can see or do that nothing depended on before is a minor bump, even
  if it also carries fixes. The 1.0.3 update notification is the reference
  example: it added a banner and a menu action while every existing library,
  setting and file stayed compatible.
- **MAJOR** (`2.0.0`) — a big feature that can break compatibility: an on-disk
  format change without a migration, or a feature that changes how existing
  games, runners or settings behave. Vanta has not had one yet; do not reach
  for a major bump for a feature that is merely large.

A release that ships any user-visible feature is a **minor**, not a patch. The
common mistake is landing features under `X.Y.Z+1`; if the release notes say
"now" or "new", the segment to raise is `MINOR`.

Where the version lives:

- The CMake project version: `set(VANTA_VERSION ...)` in `CMakeLists.txt`, mirrored
  in `src/Version.h` for a build that does not go through CMake. Keep the two in
  step.
- The tag is `v<MAJOR>.<MINOR>.<PATCH>` (for example `v1.0.4`). The release
  workflow strips the leading `v` and passes the rest as `-DVANTA_VERSION`, so
  the tag is the single source of truth for a released build.
- A trailing `-N` on a package file name (`vanta-1.0.4-1.x86_64.rpm`) is the
  packaging revision. It is **not** part of the version and never appears in the
  tag or the About dialog.

`UpdateChecker` compares versions per numeric segment, so the segment actually
matters to users: a feature shipped as a patch tells the update check that only
fixes changed.

## Packaging

`packaging/build-packages.sh` turns a `DESTDIR` install tree into a `.deb`, an
`.rpm` and a pacman `.pkg.tar.zst`. It only builds the formats whose tooling is
installed, and fails when none can be built.

```bash
DESTDIR="$PWD/stage" cmake --install build --prefix /usr
./packaging/build-packages.sh --root stage --version 1.0.3 --out dist
```

Notes that are easy to get wrong:

- **Qt resources are compiled in** via `resources.qrc`, so packages carry only
  the binary, the desktop entry, the icon and the docs. Never add a separate
  data-file install for icons used by the binary.
- The `.deb` depends on `libqt6*6 | libqt6*6t64` because Ubuntu 24.04 and
  Debian 13 renamed Qt 6 to the t64 ABI. Dropping either alternative breaks one
  of the two families.
- The `.rpm` deliberately has **no explicit Qt Requires**. rpm's dependency
  generator records the sonames actually linked, which every RPM distribution
  provides under its own package name; a hardcoded `qt6-qtbase` would break
  openSUSE.
- The pacman package is assembled by hand (`.PKGINFO`, `.INSTALL`, `.MTREE`)
  so the release job does not need pacman on the runner. Ownership is
  normalised to root and mtimes pinned to `SOURCE_DATE_EPOCH` before `.MTREE`
  is written.
- Anything installed by `cmake --install` must also be listed in the RPM
  `%files` section, or rpmbuild fails on an unpackaged file.
- zlib is a direct dependency for mod ZIP reading. The Debian package names it
  explicitly (`zlib1g`) and the pacman package lists `zlib`; the RPM relies on
  its soname generator as it does for Qt.

## Storage rules

- All state lives under XDG paths, resolved once in `Storage`:
  - config: `$XDG_CONFIG_HOME/vanta/` (`library.json`, `settings.json`)
  - data: `$XDG_DATA_HOME/vanta/` (covers, installed runners, launch logs)
  - cache: `$XDG_CACHE_HOME/vanta/` (`scan-cache.json`)
- JSON writes are **atomic**: write to a temporary file in the same directory,
  `fsync`, then `rename` over the target. Never truncate the real file first —
  a crash mid-write must not destroy a library.
- Persisted JSON is schema-versioned. Bumping a field's meaning requires a
  migration path, not a silent reinterpretation.
- Never write outside the XDG directories except to a game's own prefix, an
  OnlineFix INI the user explicitly confirmed, a mod file the user installed or
  toggled in the mod manager, or runner directories (such as
  Steam's compatibilitytools.d or Vanta's managed runners) when the user explicitly
  requests runner uninstallation.

## OnlineFix safety rules

These are non-negotiable. A change that violates them is rejected.

- The scanner is **read-only**. It may open files to read signatures. It must
  never modify, move, or delete anything in a game folder.
- The only permitted writes into a game folder are:
  1. OnlineFix INI edits the user confirmed in `OnlineFixDialog`, and
  2. the Photon `Newtonsoft.Json.dll` link, when that path is confirmed.
- **Back up before writing.** Every file about to change is copied to a backup
  first, and the backup path is reported to the user. No backup, no write.
- Preserve the rest of the file. `IniFile` edits must keep unrelated keys,
  comments, ordering and line endings intact.
- Never state or imply that a detected fix works, is applied successfully, or
  will make a game playable. Report what was *detected* and what was *changed*.
- A game whose mode is `Disabled` shows **no** OnlineFix marker anywhere in the
  library. `Game::showsOnlineFix()` is the only gate; every badge, chip and
  label must call it instead of testing `fixKind` directly. Detection stays
  visible in the details sheet, which says the mode is off.
- Do not add DRM circumvention, licence bypasses, or key generation. Vanta
  launches games the user already has; OnlineFix support exists because the
  launcher-side configuration is legitimate to reproduce.

## Mod safety rules

The mod manager writes into game folders, so it follows the same discipline as
OnlineFix. A change that violates these rules is rejected.

- **Enabling and disabling never edits a DLL.** The only operation is a rename
  between `name.dll` and `name.dll.disabled`. Nothing is deleted, and symbolic
  links are never followed, renamed, or replaced.
- **Install planning happens before any write.** A ZIP is read and validated in
  memory first. The reader rejects ZIP64, encrypted entries, absolute paths,
  `..` traversal and symlink entries, and every extracted path must resolve
  inside the game/BepInEx tree.
- **Back up before replacing.** Any existing file that an install would overwrite
  is copied to `BepInEx/.vanta-backups/<timestamp>/` first, and the dialog
  reports the folder. No backup, no write.
- **All writes are atomic.** Files are written to a temporary name in the target
  directory and renamed into place, so a failure cannot leave a half-written
  plugin.
- **No network without user action.** Opening the mod manager makes no request.
  Thunderstore is contacted only when the user loads a community, searches, or
  installs a package.
- **Never claim a mod works.** The UI reports what is installed, enabled, or
  replaced. It cannot know whether a plugin loads at runtime.

## UI conventions

- **Accent colour is `#7c3aed`** (purple). Use `ThemeColors::accent`; never
  hard-code the hex in a widget.
- Everything is **rounded**. Cards, buttons, dialogs and chips use radii
  between 6 px and 14 px; square corners look wrong in this app.
- Style through the QSS in `ThemeManager` using object names
  (`setObjectName(QStringLiteral("..."))`), not per-widget `setStyleSheet`
  calls. Inline stylesheets break theme switching.
- Every colour comes from `ThemeColors` so Light, Dark and Follow-system all
  work. A hard-coded `Qt::black` or `Qt::white` outside an icon tint is a bug.
- Icons are rendered through `Icons::` (`IconFactory`), which tints glyphs and
  brand SVGs to the current palette. Do not load a raw pixmap for a glyph.
- Honour `ThemeManager::animationsEnabled()`. Users who disable animations get
  an instant state change, never a stuck or half-applied one.
- Keyboard: every action reachable by mouse is reachable by keyboard. Escape
  closes dialogs, Enter activates the default action.
- User-facing strings are complete sentences with proper punctuation. No
  exclamation marks, no "Oops", no blame.

## Adding a game source

Sources are a fixed set, not free text. To add one:

1. Add the value to the source enum in `Types.h` and its display name in
   `Types.cpp`.
2. Add the brand mark as `resources/icons/brand/<name>.svg` (Simple-Icons
   style, single colour) and register it in `resources.qrc` under
   `/icons/brand`.
3. Map the source to the resource in `Icons::brandResource()` and to a glyph
   fallback in `Icons::glyphForSource()`.
4. Update the Source combo in `AddEditDialog` and the sidebar grouping in
   `MainWindow`.

## Commits and pull requests

- Conventional commits: `feat:`, `fix:`, `refactor:`, `docs:`, `chore:`,
  `perf:`, `test:`.
- One logical change per commit. Do not mix a refactor with a behaviour change.
- Describe the user-visible effect in the body, not the mechanics of the diff.
- Before committing: clean build, zero warnings, manual smoke test of the
  touched flow.
