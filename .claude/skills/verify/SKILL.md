---
name: verify
description: Build, launch and drive the real suopNotes app to observe a change working — safely, without disturbing the user's own running instance or their notes.
---

# Verifying suopNotes

suopNotes is a Qt/QML tray app with **no main window**: note windows are
top-level Wayland surfaces restored from settings at startup. The surface is
pixels, so verification means launching it, getting a note on screen, and
capturing it.

## Three hazards — read these first

**1. Launching your build will KILL the user's running suopNotes unless you
isolate `TMPDIR`.** `SingleInstanceGuard` uses `QLocalServer`, and Qt puts
named sockets in **`QDir::tempPath()` (i.e. `/tmp`), NOT `XDG_RUNTIME_DIR`**.
`main.cpp` calls `takeOwnership()`, so a second instance tells the first to
quit — silently closing every note the user has open. Setting `XDG_RUNTIME_DIR`
alone does **not** protect them. Always set `TMPDIR` to a scratch dir; verify
afterwards that `/tmp/suopNotes-singleinstance` is untouched and their process
still lives.

**2. Your sandboxed note windows get ADOPTED by the user's running app, which
then writes them into the user's REAL config.** Sandboxing `XDG_CONFIG_HOME`
does not prevent this, because it isn't your app doing the writing — theirs is.
`PositionManager` finds note windows through KWin by `resourceName`/caption
across the whole session, with no check that a window belongs to its own
process, so it treats yours as its own notes: they get appended to
`~/.config/suopNotes/suopNotes/settings.ini`'s `[OpenNotes]` and given rule
groups in `~/.config/kwinrulesrc` (confirmed live — four sandbox notes ended up
in the user's open-notes list at the ghost windows' 260x260 size, and the user's
app re-adds them on its next position-save, so cleaning the files while it runs
is undone). Their app never removes them, so on its next start it tries to open
notes whose IDs aren't in the user's index.

There is no known way to launch a second instance and avoid this. So: **check
both files afterwards** (`grep <your-note-uuid>`), and if you polluted them,
cleanup must be ordered — quit their app FIRST (memory is the source of truth),
then edit the files, then restart. Prefer a note UUID with an obvious fixed
prefix so the damage is greppable.

**3. Never drive this app with synthetic global input (`/dev/uinput`) on a live
desktop.** Keystrokes go wherever focus is, not where you aim. Activating the
note window via KWin scripting does **not** focus the inner `TextArea`, and the
note often won't raise above other windows — keystrokes intended for it land in
whatever is on top (this cost a real accidental edit to the user's open VSCode
buffer). If you must type, make the note **fullscreen** first (below) so a click
cannot miss.

## Sandboxed launch (safe recipe)

Config/data/kwinrules all follow XDG vars, so this touches none of the user's
real state:

```bash
SB=/tmp/<short>/sandbox                 # keep paths SHORT: socket paths blow
RT=/tmp/snv; GT=/tmp/sng                # the 108-byte sun_path limit
mkdir -p "$SB/config/suopNotes/suopNotes" "$SB/data/suopNotes/suopNotes/notes" "$RT" "$GT"

env -i HOME="$HOME" PATH="$PATH" \
  XDG_CONFIG_HOME="$SB/config" XDG_DATA_HOME="$SB/data" \
  XDG_RUNTIME_DIR=/run/user/1000 TMPDIR="$GT" \
  WAYLAND_DISPLAY=wayland-0 DISPLAY=:0 QT_QPA_PLATFORM=wayland \
  DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/1000/bus" \
  setsid nohup ./build/src/suopNotes > "$SB/app.log" 2>&1 < /dev/null &
```

Org and app name are BOTH `suopNotes`, so paths nest:
`$XDG_CONFIG_HOME/suopNotes/suopNotes/settings.ini` and
`$XDG_DATA_HOME/suopNotes/suopNotes/notes/`.

## Getting a note on screen

There is no tray in a test context, so **seed an open note** — the app restores
`[OpenNotes]` at startup. `noteId` is a **UUID mapped to a filename by
`.suopnotes-index.json`**, not the filename itself:

```bash
UUID=11111111-2222-3333-4444-555555555555
printf '{"%s":"verify"}' "$UUID" > "$NOTES/.suopnotes-index.json"
printf '[Appearance]\nnotesFolder=%s\n\n[OpenNotes]\nsize=1\n1\\noteId=%s\n1\\x=40\n1\\y=40\n1\\width=780\n1\\height=700\n' "$NOTES" "$UUID" \
  > "$SB/config/suopNotes/suopNotes/settings.ini"
```

## Capturing

`QT_QPA_PLATFORM=vnc` does **not** work — the app renders nothing (frameless +
alpha + blur needs a compositor); you get a uniformly black framebuffer. Nested
`kwin_wayland` also failed to produce a usable surface. What works: run on the
**real** session (recipe above) and `spectacle -b -n -f -o out.png`, then crop.

Find the note's geometry via KWin scripting (`qdbus` may be absent; use `gdbus`,
and read `print()` output from `journalctl --user`):

```bash
gdbus call --session --dest org.kde.KWin --object-path /Scripting \
  --method org.kde.kwin.Scripting.loadScript /tmp/x.js tag
gdbus call --session --dest org.kde.KWin --object-path /Scripting \
  --method org.kde.kwin.Scripting.start
journalctl --user --since "-1 min" --no-pager | grep MYTAG
```

Note windows have `caption=suopNotes-<uuid>` and `resourceName=suopNotes`.
To make a click land reliably, set `c.fullScreen = true` on it first.

## Gotchas

- `pgrep -f`/`pkill -f` on a pattern like `build/src/suopNotes` **matches your
  own shell's command line** and kills your session. Kill by exact PID.
- Editing a note **renames its file** from the first non-empty line.
  `NoteFilename::stripLeadingMarker` strips `-*+>` and `1.` but **not `#`**, so
  a note starting with a heading becomes `# Title.md`. Pre-existing, not a bug
  you introduced (the user's own notes folder already contains `## Notes.md`).
- The app writes a per-note KWin rule via `kwriteconfig6`, which does honor
  `XDG_CONFIG_HOME`, so *your* instance's rules land in `$SB/config/kwinrulesrc`
  (verified). That is NOT enough to keep the real one clean — see hazard 2.
- **`ctest` writes into the user's real `~/.config/kwinrulesrc`.** `qml_tests`
  instantiates NoteWindow unsandboxed, and its rule groups
  (`suopNotes-toolbar-test-note-a`/`-b`) persist there. Confirmed by md5 before
  and after a bare `ctest -R qml_tests`.
- **A QML error makes the app exit 255 with NO output under the wayland
  platform** — an empty log and a dead process, which reads exactly like a crash
  with no cause. `main.cpp` does `return -1` when `rootObjects()` is empty. To
  actually see the error, run offscreen:
  ```bash
  env -i HOME="$HOME" PATH="$PATH" XDG_CONFIG_HOME="$SB/config" \
    XDG_DATA_HOME="$SB/data" TMPDIR="$GT" \
    QT_QPA_PLATFORM=offscreen QT_FORCE_STDERR_LOGGING=1 \
    timeout 8 ./build/src/suopNotes 2>&1 | grep -iE "unavailable|Cannot|failed"
  ```
  This is a fast pre-flight for any QML change — it caught "Cannot override
  FINAL property" (a delegate declaring `top`/`bottom`, which `Item` already
  declares final) in seconds after the wayland run said nothing at all.
- **`kill -9` leaves a ghost window in KWin**: a dead client's window keeps
  answering geometry queries. A caption-matching raise script will happily grab
  the ghost, resize it, and screenshot a region that paints nothing — which
  looks precisely like "the app renders an empty note". Use SIGTERM, and give
  each run a UNIQUE note UUID so a stale caption can't be matched.
- The app's own KWin rule sets `positionrule=3`, so KWin **overrides any
  `frameGeometry` you assign** from a script. Always crop the screenshot from
  the geometry KWin *reports*, never from what you asked for. The geometry read
  during a raise can also still be the window's transient startup size (seen
  reporting 260x260 before settling to 780x700) — re-query after it settles, and
  retry rather than sleeping a fixed amount: the window can take well over 4s to
  map.
