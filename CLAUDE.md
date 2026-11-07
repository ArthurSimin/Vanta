# CLAUDE.md

Working notes for Claude when modifying Vanta. **[AGENTS.md](AGENTS.md) is the
authoritative guide** — read it first. This file only adds what is specific to
running here.

## Quick orientation

Vanta is a C++17 / Qt 6 Widgets Linux game launcher. ~16k lines across
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
  modify anything.

## Reporting results

State what was verified and how. "Built clean and tested the picker under Xvfb"
is useful; "should work" is not. If something is unverified, say so plainly
instead of implying it was checked.
