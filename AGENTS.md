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

Requirements: CMake ≥ 3.21, a C++17 compiler, Qt ≥ 6.5 with the `Widgets`,
`Svg`, `Concurrent` and `Network` modules.

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
    RunnerInstaller.*   Installs Wine or GE-Proton
    OnlineFix.*         Signature detection and confirmed INI writes
    SteamPrecondition.* Steam-closed prompts before an OnlineFix launch
    IniFile.*           INI parsing that preserves formatting
    CoverFetcher.*      SteamGridDB search, artwork listing, downloads
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
    AddEditDialog.*     Add/edit form, executable-only path entry
    GameDetailsDialog.* Read-only detail sheet
    OnlineFixDialog.*   Detection report and confirmed actions
    LaunchProgressDialog.* Staged launch feedback
    IconFactory.*       Glyph and brand SVG rendering
    Common.*            Small shared UI helpers
```

The `core`/`ui` split is a hard boundary: `core` must never include a Widgets
header. It keeps scanning and launching testable without a display.

## Storage rules

- All state lives under XDG paths, resolved once in `Storage`:
  - config: `$XDG_CONFIG_HOME/vanta/` (`library.json`, `settings.json`)
  - data: `$XDG_DATA_HOME/vanta/` (covers, installed runners)
  - cache: `$XDG_CACHE_HOME/vanta/` (`scan-cache.json`)
- JSON writes are **atomic**: write to a temporary file in the same directory,
  `fsync`, then `rename` over the target. Never truncate the real file first —
  a crash mid-write must not destroy a library.
- Persisted JSON is schema-versioned. Bumping a field's meaning requires a
  migration path, not a silent reinterpretation.
- Never write outside the XDG directories except to a game's own prefix or an
  OnlineFix INI the user explicitly confirmed.

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
- Do not add DRM circumvention, licence bypasses, or key generation. Vanta
  launches games the user already has; OnlineFix support exists because the
  launcher-side configuration is legitimate to reproduce.

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
