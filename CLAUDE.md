# CLAUDE.md

Working notes for Claude when modifying Vanta. **[AGENTS.md](AGENTS.md) is the
authoritative guide** — read it first. This file only adds what is specific to
running here.

## Quick orientation

Vanta is a C++17 / Qt 6 Widgets Linux game launcher. ~21k lines across
`src/core` (no Widgets, testable) and `src/ui` (Widgets only, no I/O).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
./build/vanta
```

Zero warnings is the bar. Check with:

```bash
cmake --build build --clean-first -j"$(nproc)" 2>&1 | grep -cE "warning:|error:"
```

The answer must be `0`.

## Verifying UI changes without a display

Never drive the user's real desktop with `xdotool` — on a Wayland session it
lands clicks in whatever window happens to be under the cursor. Use Xvfb and
redirect the XDG directories so a test cannot touch a real library:

```bash
Xvfb :99 -screen 0 1600x900x24 &
export DISPLAY=:99 QT_QPA_PLATFORM=xcb XDG_SESSION_TYPE=x11
export XDG_CONFIG_HOME=/tmp/vanta-test/config
export XDG_DATA_HOME=/tmp/vanta-test/data
export XDG_CACHE_HOME=/tmp/vanta-test/cache
mkdir -p "$XDG_CONFIG_HOME/vanta"
# Seed a library.json, then:
./build/vanta &
sleep 8
import -window root /tmp/shot.png   # ImageMagick
```

To catch a transition mid-flight, take two screenshots a second apart — one
during the animation and one after — rather than trusting the final state.
A settled screenshot cannot tell you whether the slide happened.

## Things that have bitten us

- **`QT_NO_CAST_FROM_ASCII` is on.** `QLabel *l = new QLabel("text")` does not
  compile. Wrap every literal in `QStringLiteral(...)`.
- **Qt 6.2 is the compatibility floor, not an oversight.** Release packages are
  built on Ubuntu 22.04 so one binary runs everywhere from Ubuntu 22.04 up. A
  local build against system Qt 6.12 will link `Qt_6.12` symbols and refuse to
  start on older distributions — that is expected locally, never in a release.
  Check with `objdump -T build/vanta | grep -oE 'Qt_6\.[0-9]+' | sort -uV | tail -1`.
- **`qt_standard_project_setup()` needs Qt 6.3**, so it is wrapped in
  `if(COMMAND qt_standard_project_setup)`.
- **`setWindowOpacity` does nothing on a child widget.** For a fade, use
  `QGraphicsOpacityEffect`.
- **A checkable `QToolButton` with no menu needs `Qt::ToolButtonTextOnly`** and
  an explicit text update in the `toggled` handler, or the checked state is
  invisible.
- **`QNetworkRequest::setRedirectPolicy()` does not exist in Qt 6.12.** Use
  `setAttribute(QNetworkRequest::RedirectPolicyAttribute, ...)`.
- **Concurrent thumbnail downloads need one reply object per request.** A
  single `QNetworkReply *` member silently cancels every download but the last.
  Keep a list and clean it up in `finished`.
- **`QListWidget` icon-mode captions elide** unless `gridSize()` leaves room
  for the text. Add roughly 24 px of width and 26 px of height on top of the
  icon size.
- **`QTableWidget` rebuilds drop the selection.** `GameListView` guards this
  with `m_populating`; any new rebuild path needs the same guard.
- **SteamGridDB's shared API key hits per-key 429 limits.** A failure there is
  not a bug in the request.
- **`CoverFetcher` handles one lookup at a time.** Batch calls (scan results,
  "Fetch missing covers") are appended to `m_fetchQueue` and drained with a
  150 ms gap; calling `fetchCover()` directly while `m_reply` is active would
  reject the request with "already running". Queue instead of retrying.
- **Rebuilding the sidebar must `delete` the old rows, not `deleteLater()`.**
  Queued deletion leaves the previous widgets visible for a frame, so repeated
  library refreshes stack duplicate headings and paint over each other.
- **Library refreshes are debounced.** A multi-game scan emits `gamesChanged()`
  for every game; `MainWindow::refreshLibrary()` coalesces them behind a 40 ms
  single-shot timer. Rebuilding the sidebar and grid per signal freezes the
  event loop and rewrites the cache far more often than needed.
- **Lutris import prefers the `sqlite3` CLI.** Without it, `LibraryImporter`
  falls back to the YAML files in `lutris/games/`, which is less complete but
  still works. Do not turn the optional tool into a hard dependency.
- **Imported covers are referenced in place.** Lutris and Heroic artwork is not
  copied into the XDG data directory, so deleting the source launcher's files
  breaks those covers. Re-fetching through the cover picker replaces the path.
- **The scan cache knows nothing about mod installs.** `MainWindow` re-runs
  `BepInEx::detect()` before a launch instead of trusting `scanResult`, otherwise
  a mod installed in the manager would not enable the `winhttp=n,b` override.
- **`QTableWidget` swallows Space and Enter.** The mod list uses an event filter
  on the table to make Space toggle the selected row; a `QShortcut` never fires.
- **Thunderstore community slugs are guessed, not authoritative.** A wrong guess
  returns an empty package list or a 503 "No cache available"; the dialog treats
  both as "check the community slug", never as an empty community.
- **BepInExPack is a loader, not a plugin.** Its files belong at the game root
  and overwrite `BepInEx/core`. It is skipped as a dependency, but an explicit
  request for it must confirm the loader replacement instead of being refused.
- **A pinned dependency version may not exist.** Communities keep the newest
  releases only, so the resolver falls back to the newest one and must report
  the substitution rather than silently installing something else.
- **A failed download must not leave files behind.** Packages are held in memory
  and parsed before the install plan runs, so cancelling mid-sequence writes
  nothing.
- **Wine prints nothing useful at `WINEDEBUG=-all`.** The log viewer would be
  empty. The setting defaults to `-all,err+all`; the `err` channel alone is what
  makes a crash diagnosable, and adding `warn` produces hundreds of lines per
  launch.
- **Packaging needs an install tree, not the build tree.** Run
  `DESTDIR=... cmake --install` first; `packaging/build-packages.sh` reads that
  tree and copies it verbatim into each format.

## Editing discipline

- Add new `.cpp`/`.h` pairs to `VANTA_SOURCES` in `CMakeLists.txt`. A file that
  is not listed is not compiled, and the missing symbol only shows up at link
  time.
- Keep the `core`/`ui` boundary: no `#include <QWidget>` or any Widgets header
  in `src/core`.
- Style through object names and `ThemeManager`'s QSS. Inline
  `setStyleSheet` breaks theme switching.
- No TODOs, stubs or placeholder branches. If it is not finished, do not land
  it.
- Back up before any write into a game folder, and never let the scanner
  modify anything. The mod manager may rename a DLL to enable or disable it, but
  it never edits, deletes, or follows a symlink.

## Reporting results

State what was verified and how. "Built clean and tested the picker under Xvfb"
is useful; "should work" is not. If something is unverified, say so plainly
instead of implying it was checked.
