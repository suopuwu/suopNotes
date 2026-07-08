# suopNotes

A lightweight, cross-platform sticky-notes app built with Qt 6 / QML, primarily targeting
KDE Plasma on Wayland with a fallback path for X11, Windows, and macOS.

## Requirements

- Qt 6.11+ (Core, Gui, Qml, Quick, QuickControls2, Test, QuickTest, Core5Compat, LabsPlatform, Widgets)
- CMake 3.21+
- Catch2 3.x
- On Linux/KDE: `kwriteconfig6`/`kreadconfig6`/`qdbus6` (for window-position persistence and
  blur) and KF6WindowSystem (for blur-behind) — both optional; the app degrades gracefully
  without them.

On Arch/CachyOS:

```sh
sudo pacman -S --needed extra-cmake-modules catch2
```

## Building

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## Running

```sh
./build/src/suopNotes
```

The app has no main window — it lives in the system tray. Left-click the tray icon (or
"New Note" from its context menu) to create a note. Right-click any note (or tap-and-hold
on touch) to drag it. Notes save automatically as you type.

## Testing

```sh
cd build && ctest --output-on-failure
```

Tests are written as Catch2 BDD scenarios (`SCENARIO`/`GIVEN`/`WHEN`/`THEN`) for the C++ core
and Qt Quick Test for QML behavior — they're intended to double as living documentation of
the app's expected behavior. See `tests/unit/` and `tests/qml/`.

## Architecture

- `src/core/` — testable C++ core: `NoteStore` (markdown file storage), `Settings` (config +
  session state), `OpenNotesModel` (bridges session state to QML), `BlurEffect`/`BlurRegion`
  (KWindowEffects wrapper), `TextFormatting`/`DocumentHandler` (WYSIWYG editor formatting),
  `KWinRuleWriter`/`PositionManager` (Wayland window-position persistence via KWin rules).
- `src/qml/` — `Main.qml` (tray icon + note-window instantiation, no main window),
  `NoteWindow.qml` (one frameless window per note), `SettingsDialog.qml` (appearance settings).
- `.task/` — planning notes from this project's build-out; safe to delete once you've reviewed
  them, or keep for historical context on design decisions.

## Manual verification checklist

Automated tests cover the logic that can be tested without a live compositor or synthesized
input. A few things are best (or only) confirmed by a human at the keyboard — Wayland
deliberately blocks synthetic input injection, so these couldn't be scripted:

- [ ] **Drag**: right-click-drag a note with the mouse; tap-and-drag on a touchscreen.
      Left-click should edit text, not drag.
- [ ] **Blur**: toggle "Enable blur behind notes" in Settings and confirm the note's
      background visibly blurs the desktop behind it (vs. a sharp/transparent look when off).
- [ ] **Position restore**: open a note on a specific monitor (if you have more than one),
      move it somewhere distinctive, quit the app, relaunch, and confirm it reopens in the
      same place on the same monitor. **Known caveat**: multi-monitor screen-targeting was
      validated against the underlying math and a known-working reference rule on this
      system, but repeated live testing produced some inconsistent results that weren't fully
      isolated — if a note reopens on the wrong monitor, that's the area to investigate first
      (see `KWinRuleWriter`/`PositionManager`).
- [ ] **Tray-only presence**: confirm notes never appear in the taskbar/Alt-Tab/pager, only
      the tray icon is visible when notes are open.
- [ ] **WYSIWYG**: type in a note and confirm you never see raw markdown syntax (`**`, `#`,
      etc.) — formatting is toolbar/shortcut-driven only.
- [ ] **Autostart** (not yet implemented): if you want suopNotes to launch at login, you'll
      currently need to add your own `.desktop` file to `~/.config/autostart/`.
