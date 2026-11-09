# OnlineFix support in Vanta

Vanta implements the launcher-side behaviour of **OnlineFix Linux (OFLL)** by
[ZzEdovec](https://github.com/ZzEdovec/onlinefix-linux). This document explains
what that port covers, how it maps onto the upstream project, and what Vanta
deliberately does not do.

> **OnlineFix support is provided as-is.** Vanta can detect files whose names
> match OnlineFix signatures and can prepare the configuration those files
> expect. Detection is not proof that a fix works. Vanta never patches game
> binaries, never downloads fix files and never modifies a game executable.

Upstream project: <https://github.com/ZzEdovec/onlinefix-linux> — licensed under
the **GNU Affero General Public License v3.0**, the same license as Vanta.

## What the port covers

| OFLL behaviour | Vanta implementation |
| --- | --- |
| Recognise OnlineFix / SteamFix signature files next to a game | `OnlineFixDetector::detect()` in `src/core/OnlineFix.cpp` |
| Swap `RealAppId` and `FakeAppId` in `steamfix.ini` (FreeTP titles) | `OnlineFixPatcher::apply()` |
| Force `ExtraProtection=false` in `[Misc]` | `OnlineFixPatcher::apply()` |
| Compose the Wine DLL override chain | `OnlineFixOverrides::compose()` |
| Provide `Newtonsoft.Json.dll` beside a Photon launcher | `patchPhoton()` |
| Check for a running Steam client before an OnlineFix launch | `SteamPrecondition` in `src/core/SteamPrecondition.cpp` |
| Ask how to handle a closed Steam client | `SteamPromptDialog` in `src/ui/SteamPromptDialog.cpp` |
| Launch-only application of the fix | `LaunchPlanBuilder` in `src/core/LaunchPlan.cpp` |

Vanta is a C++/Qt rewrite, not a wrapper: it does not shell out to the OFLL
Python scripts, and no OFLL source file is copied into this repository.

## Detection rules

The detector looks at the **top level** of the executable's directory and of the
game folder, deduplicates by absolute path, and matches file names against these
patterns (case-insensitive, `OnlineFixDetector::signaturePatterns()`):

```
^(emp|custom)\.dll$
^epicfix\.dll$
^onlinefix(32|64)?\.(dll|ini|json)$
^steamfix(32|64)?\.(dll|ini)$
^winmm\.(dll|txt)$
^dlllist\.txt$
^launch_data\.of.*$
```

The list is deliberately narrow. Broader patterns such as `win*.dll`,
`steam*.dll` and `eos*.dll` also match ordinary runtime libraries
(`wininet.dll`, `steam_api64.dll`, `EOSSDK-Win64-Shipping.dll`), which made
nearly every Steam or Epic title look like it carried a fix. A false positive is
worse than a miss here, because it turns a detection into a launch prompt.

Every match is reported in the OnlineFix dialog with its size, timestamp and the
rule that matched it. Detection never opens a DLL, never hashes a binary and
never writes anything.

The detected kind is derived from which files are present:

| Condition | `FixKind` |
| --- | --- |
| `onlinefix.ini` present | `OnlineFix` |
| `steamfix.ini` present without an `[OnlineFix Linux]` section | `FreeTP` |
| `launch_data.of*` or `onlinefix.json` beside a `Launcher.exe` | `Photon` |
| `steamfix*.dll` present without `steamfix.ini` | `SteamFixOnly` |
| signatures present but none of the above | `Unknown` |

Module lists (`winmm.txt`, `dlllist.txt`) are parsed line by line; every `.dll`
they name becomes an extra override module. Backslashes are normalised to
forward slashes and only the base name is used.

App IDs are read from `[OnlineFix Linux]` first and from `[Main]` second, in both
`onlinefix.ini` and `steamfix.ini`.

## File modifications

Vanta modifies files in exactly three situations, and only after you confirm the
launch (or set the global policy to *Always apply*).

### 1. `steamfix.ini` — FreeTP app-id swap

For a FreeTP title, the fix expects the *fake* app id in `RealAppId` and the
original id preserved for Steam. The swap is **idempotent**: it only runs when the
`[OnlineFix Linux]` section is absent, and after the first run that section exists,
so a second launch leaves the file alone.

```ini
; before
[Main]
RealAppId=1234560
FakeAppId=480
[Misc]
ExtraProtection=true

; after one launch
[Main]
RealAppId=480
FakeAppId=480
[Misc]
ExtraProtection=false
[OnlineFix Linux]
RealAppId=1234560
```

If neither `RealAppId` nor `FakeAppId` exists, the file is left untouched and a
diagnostic explains why.

### 2. `onlinefix.ini` / `steamfix.ini` — `ExtraProtection`

If `[Misc] ExtraProtection` exists, it is set to the literal string `false`. No
other key in the file is touched. Files are only rewritten when a value actually
changed, so unchanged files keep their timestamps.

### 3. Photon — `Newtonsoft.Json.dll` link

When a Photon marker (`launch_data.of*` or `onlinefix.json`) sits beside a
`Launcher.exe` and `Newtonsoft.Json.dll` is missing there, Vanta searches the game
tree (skipping the launcher directory itself) for an existing
`Newtonsoft.Json.dll` and creates a link to it beside the launcher. If the
dependency cannot be found or the link cannot be created, the launch continues and
the diagnostic log says so.

## Backups

Before any INI is rewritten, Vanta copies it to `<file>.vanta.bak`. An existing
backup is **never** overwritten, so the first backup always reflects the original
state:

```
steamfix.ini
steamfix.ini.vanta.bak
```

The patch log in the launch dialog lists every file written and every backup
created. If a backup cannot be created, the file is not modified and the launch is
stopped before the game starts.

File permissions are captured before the write and restored afterwards, because
`QSaveFile` replaces the inode.

## Override composition

`WINEDLLOVERRIDES` is assembled in a fixed order and only for the current launch:

1. **Forced DXVK chain** — always first, always present for Wine and Proton:

   ```
   d3d11=n;d3d10=n;d3d10core=n;dxgi=n;openvr_api_dxvk=n;d3d12=n;d3d12core=n;d3d9=n;d3d8=n;
   ```

2. **Your stored overrides** — appended in the order you wrote them in the game
   editor. Nothing is filtered or reordered here.

3. **OnlineFix modules** — every detected DLL. Names starting with `win` are
   written as `=n,b;` (native, builtin); everything else as `=n;`.

4. **`winmm.txt` / `dlllist.txt` modules** — appended as `=n;`.

5. **BepInEx** — when BepInEx is detected or the compatibility toggle is on,
   `winhttp=n,b;` is appended.

Duplicates are removed case-insensitively by module name, so an override you
already wrote for `winmm` is not repeated. Trailing semicolons are stripped before
the value is handed to Wine, because a trailing separator makes some Wine versions
treat the last entry as empty.

The result is stored only in the launch environment. It is **never** written back
into the game's saved environment, and it is never written into an INI file.

## Steam precondition

Some fixes need a signed-in Steam client. When *Require Steam to be running before
an OnlineFix launch* is enabled in Settings, Vanta first checks whether Steam is
already running. If it is, the launch continues immediately.

If Steam is **not** running, Vanta does not start the client on its own. It shows
a dialog with exactly two choices:

1. **Open the game with no steam ( online will not work )** — the OnlineFix files
   are still applied, but the game starts without a Steam session. Online
   features, matchmaking and friend lobbies will fail; single-player content
   usually works. The launch diagnostics record that Steam was skipped.
2. **Open steam for online** — Vanta starts Steam through the detected launcher,
   waits for the client to come up with progress and a cancel button, then
   launches the game once it is ready.

Choosing *Cancel* aborts the launch cleanly: no patch is applied and no process is
started.

The OnlineFix configuration Vanta writes is identical in both cases — only the
Steam session differs.

If the *Open steam for online* path cannot be satisfied, the launch is **aborted
cleanly** and the dialog explains what failed.

## Launch plan order

For a Wine or Proton launch with OnlineFix applied, `LaunchPlanBuilder::build()`
performs these steps in order:

1. Copy the system environment.
2. Apply the game's stored environment variables, skipping Vanta-controlled keys.
3. Set the prefix variables (`WINEPREFIX` for Wine, `STEAM_COMPAT_DATA_PATH` for
   Proton).
4. Insert the composed `WINEDLLOVERRIDES`.
5. Add Proton toggles (`PROTON_USE_WINED3D`, `PROTON_ENABLE_WAYLAND`), the Steam
   overlay variables and `SteamLinuxRuntime` wrapping when requested.
6. Build the argument vector: wrapper → runner → pre-arguments → executable →
   launch arguments.

OnlineFix is applied only for Wine and Proton runners. Native, Flatpak and custom
launches ignore it entirely.

## Per-game and global policy

Per-game mode (`Auto`, `Force`, `Disabled`) and the global policy (`Ask before
every launch`, `Always apply when detected`, `Never apply`) are combined as
follows:

| Per-game | Global | Result |
| --- | --- | --- |
| `Disabled` | any | Never applied |
| `Auto`, nothing detected | any | Never applied |
| `Auto`, detected | `Ask` | Prompt |
| `Auto`, detected | `Always apply` | Applied |
| `Auto`, detected | `Never apply` | Not applied |
| `Force` | `Ask` | Prompt |
| `Force` | `Always apply` | Applied |
| `Force` | `Never apply` | Not applied |

`Force` means "use the OnlineFix override chain and app ids even if no signature
file was found". It does not invent a fix and it does not patch anything, because
there is no INI to patch.

The prompt offers three outcomes: **Cancel**, **Launch without OnlineFix**, and
**Apply and launch**. Cancelling leaves the library and the game files untouched.

## What Vanta does not do

- It does not modify `.exe` files, DLLs, game assets or save data.
- It does not download OnlineFix files, cracks, or game content.
- It does not verify, test or guarantee that a fix works online.
- It does not bypass anti-cheat, and it does not claim that a detected file is a
  working fix.
- It does not keep OnlineFix state anywhere except the launch environment and the
  three documented file edits above.

## Attribution

The behaviour documented here was derived from **OnlineFix Linux** by
**ZzEdovec** (<https://github.com/ZzEdovec/onlinefix-linux>), distributed under the
GNU Affero General Public License v3.0. Vanta is an independent reimplementation
in C++/Qt and is not affiliated with that project, with OnlineFix, or with any
game publisher.
