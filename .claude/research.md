# Research — durable project knowledge

Distilled, reusable facts from every round of work so far (see `.task/archive/` for the
full narrative history and `.task/plan.md`/`findings.md` for the in-progress round). Lives
in `.claude/` rather than `.task/` because `.task/` is temporary/task-scoped and safe to
delete, while this is meant to persist. Not auto-loaded — open it manually when it's
relevant to what you're working on.

Add to this file, don't just accumulate more archive rounds: when a round finishes, pull
anything reusable out of its findings into the relevant section below before archiving.

## Toolchain

- Qt 6.11+ (Core, Gui, Qml, Quick, QuickControls2, Test, QuickTest, Core5Compat,
  LabsPlatform, Widgets), KF6WindowSystem (blur, optional), CMake 3.21+, Catch2 3.x.
- Arch/CachyOS: `pacman -S extra-cmake-modules catch2` (not present by default).
- System routes Qt logging to journald by default — `QT_LOGGING_TO_CONSOLE=1` to see
  stdout logs while debugging.

## Qt/QML gotchas (general)

- **Alpha surface**: `QSurfaceFormat` with `alphaBufferSize(8)` must be set *before*
  `QGuiApplication`/`QApplication` is constructed, or transparency/blur is silently dead.
- **Rounded corners**: `Rectangle.radius` does not clip children — need
  `layer.effect: OpacityMask` (`Qt5Compat.GraphicalEffects`).
- **Never toggle `FramelessWindowHint` at runtime on Wayland** — needs a hide/recreate
  dance. Sidestep by making relevant windows always-frameless from creation.
- **`Repeater` can't host `Window`-based delegates** — `Repeater.itemAt()` returns `null`
  for non-`Item` delegates even with a correct count. Use `Instantiator` (`import QtQml`)
  instead; it has `count`/`objectAt(index)` and is documented for exactly this.
- **`qt_add_qml_module` URI must differ from the executable target name** (by more than
  case — macOS's default case-insensitive FS would still collide) — the module's staging
  dir is named after the URI and collides with the exe output file otherwise
  (`ld: cannot open output file X: Is a directory`).
- **Don't share one QML module between two binaries via a separate `qt_add_library` +
  `qt_add_qml_module` plugin target** — silently breaks module resolution for *both*
  consumers (`qmlimportscanner`/static-plugin-import generation needs the module's
  `OUTPUT_DIRECTORY` to end in its own URI path segments, and even fixing that wasn't
  sufficient in practice). Instead, have each consuming binary run its own
  `qt_add_qml_module` directly, listing shared `.qml` sources by path with
  `set_source_files_properties(... QT_RESOURCE_ALIAS "Foo.qml")` for files outside that
  target's own source tree.
- **C++ methods callable from QML need explicit `Q_INVOKABLE`** — a method Qt/QML can see
  on the object but not tag as invokable throws a `TypeError` at call time, not a compile
  error. A Quick Test suite that never actually calls a given method from QML won't catch
  this (only caught here via manual verification, twice, on different classes).
- **QML property self-shadowing**: `Child { propName: propName }`, where `Child` has its
  *own* `Q_PROPERTY` literally named `propName`, binds the property to itself — QML
  resolves a bare RHS identifier against the declaring object's own properties before any
  outer id/context property of the same name. No error, just silently wrong (until
  something changes the intended outer source afterward and nothing reacts). Fix: rename,
  or assign imperatively/qualified (`child.propName = outer.propName` in
  `Component.onCompleted`), or use a `readonly property alias` (aliases resolve specially
  at parse time and aren't subject to this shadowing).
- **`color`-typed QML values don't reliably support `!==`/`===`** in this engine build —
  compare via `.toString()` or `Qt.colorEqual` instead.
- **A `property alias` straight through to a *bound* (not plain-stored) child property can
  get stuck at its first-resolved value** across later changes, while reading the same
  value via a direct item-reference (`someItem.color`) stays live. If an alias to a bound
  property looks stale, drop the alias and expose the item reference instead.
- **Property writes sometimes need an event-loop turn to settle before the next read
  observes them** in Quick Tests — a `wait(0)` (or any intervening statement) after writing
  a property, before asserting on something derived from it, avoids reading a stale
  pre-write value. Root cause not fully isolated; treat as a known defensive idiom in this
  codebase, not a one-off.
- **`background: null` on a `TextArea` breaks under the KDE Breeze QtQuickControls2
  style** specifically (breaks inside Breeze's own internal `TextArea.qml`). Use
  `background: Item {}` instead. Reproduces identically under Breeze only —
  `QT_QUICK_CONTROLS_STYLE=Basic` is clean, confirming it's a Breeze-internal quirk, not
  app code.
- **`Qt.labs.platform.SystemTrayIcon` wraps `QSystemTrayIcon` (QtWidgets)** — aborts
  immediately under plain `QGuiApplication` ("QWidget: Cannot create a QWidget without
  QApplication"). Needs `QApplication` + `Qt6::Widgets` linked, even in an otherwise
  QML-only app.
- **Emoji glyph color**: `"📌"` (pushpin emoji) ignores QML `color:` bindings entirely
  (fixed glyph color); plain-presentation glyphs like `"⚲"` respond to `color:` correctly.
  Check any new icon glyph against this before assuming `color:` will work.
- **`QStandardPaths::AppConfigLocation`/`AppDataLocation` nest as `suopNotes/suopNotes/`**
  — both `organizationName()` and `applicationName()` are set to `"suopNotes"` in
  `main.cpp`. Cosmetic, not a bug, but look for the doubled path when inspecting real
  config/data on disk.
- **QML module resource prefix defaults to `/<URI>/`, not `/qt/qml/<URI>/`**, under
  `QTP0001` — relevant if manually constructing a `qrc:`-style resource path.

## Wayland / KWin (position, blur, drag, taskbar)

- **Client cannot read or set its own window x/y on Wayland, ever.** Size is fine
  (readable/writable directly). Position must go through compositor-side mechanisms.
- **`QWaylandSession` is not usable** (checked against Qt 6.11.1 + live KWin 6.7.2): it's a
  private/unstable header, and it targets Wayland global `xx_session_manager_v1` while this
  KWin advertises `xdg_session_manager_v1` (protocol renamed upstream from `xx-` staging to
  `xdg-`; Qt hasn't caught up) — binding is exact-string, so it silently never binds.
  Revisit only once Qt ships a public API against the `xdg-` name.
- **Position persistence = KWin rules (`kwinrulesrc`) + a KWin-scripting D-Bus probe to
  read live position back**, not any client-side API:
  - `position=` with Apply-Initially (rule value `3`) sets spawn position; `screen=` Force
    (rule value `2`) only re-enforces after a drag; Remember (rule value `4`) **never**
    updates on Wayland.
  - Writing via `kwriteconfig6` is not enough by itself — KWin only loads rules listed in
    `[General]`'s `count=`/`rules=<uuid,uuid,...>`; both must be updated when adding or
    removing a rule group, not just the group itself.
  - Apply via `qdbus6 org.kde.KWin /KWin org.kde.KWin.reconfigure` (exits 0 cleanly), but
    its *internal* rule-cache rebuild is still in flight briefly after the call returns —
    empirically needs a ~200ms settle delay before applying/showing a window that depends
    on the new rule, or it can spawn at KWin's default placement instead.
  - Reading live position: KWin scripting probe (`loadScript`/`start`/`unloadScript` over
    D-Bus, `workspace.windowList()`, match by `resourceClass`, read `frameGeometry`).
    `print()` output goes to the **systemd journal**, not any D-Bus return value — read via
    `journalctl` matched against a fresh per-call marker string.
  - Address monitors by **connector name** (e.g. `DP-4`), re-resolved to index only at
    write time — indices aren't stable identifiers. A pre-existing rule on this system
    (unrelated app) uses a numeric index and is a known-fragile example, not something to
    replicate.
  - Rule matching must use the **exact, live, case-sensitive `resourceClass`** — don't
    assume it from the binary/app name; verify with a live probe (a lowercase-vs-mixed-case
    mismatch silently matched nothing, no error).
  - No KWin rule can make a window immovable — an app-level "lock position" feature must
    gate the app's own drag gestures, not rely on the compositor.
- **Blur**: `KWindowEffects::enableBlurBehind(window, enabled, region)`; region is
  window-relative, rounded to match the window's own corner radius, and must be
  recomputed on resize. Last-writer-wins per window (fine when each window has exactly one
  blur consumer, as here).
- **Drag**: `QWindow::startSystemMove()` is the only cross-platform mechanism that works on
  Wayland; `startSystemResize(Qt::Edges)` is the confirmed-working sibling for resize, same
  call pattern, no extra wiring needed.
- **`Qt::Tool` does not suppress taskbar/pager/switcher presence on KWin/Wayland** (it does
  on Windows/macOS) — force `skiptaskbar`/`skippager`/`skipswitcher` via the same per-window
  KWin rule group used for position.
- **Wayland blocks all synthetic input injection** — no `xdotool`/`ydotool`-equivalent
  available. Literal drag/hover/click gestures cannot be scripted; they need a human at the
  keyboard. Where a live end-to-end check is still valuable, prefer invoking the same
  D-Bus interface the real gesture would ultimately hit (e.g. tray icon's
  `org.kde.StatusNotifierItem.Activate` to simulate a left-click) over trying to fake input.
- Screenshot-based visual verification on a live, actively-used multi-monitor desktop can
  be inconclusive (unrelated windows/popups changing between before/after captures) — a
  D-Bus/KWin-scripting state probe is usually a more reliable check than a screenshot diff.

## Markdown / WYSIWYG editor (`QTextDocument`, `TextArea.textFormat: MarkdownText`)

- Round-trips bold/italic/heading/links/strikethrough/GFM task-checkboxes correctly.
  `MarkdownDialectGitHub` is the default dialect for both `toMarkdown()`/`setMarkdown()` —
  no explicit flag needed for tables/task-lists/strikethrough/autolinks.
- **Qt does not support live-typed markdown syntax in WYSIWYG mode** — a user typing `**`
  sees literal asterisks, not live bold. All formatting must be applied via
  toolbar/shortcut actions (`QTextCursor::mergeCharFormat`), never by typing raw syntax.
  This still fully satisfies "no visible markdown" since raw syntax typing is simply not
  the offered input method — it's not a partial workaround.
- **`toMarkdown()`/`setMarkdown()` both lose blank-line *count* information**:
  `toMarkdown()` renders at most one blank line between blocks and drops leading/trailing
  blank blocks entirely, regardless of the real in-document count; `setMarkdown()` can't
  tell 0 vs 1 vs 2 vs 3 blank lines between two paragraphs apart — all parse to the same
  adjacent-blocks-with-nothing-between shape. `MarkdownSerialization::{to,setMarkdown}
  PreservingBlankLines()` (`src/core/MarkdownSerialization.{h,cpp}`) works around both
  directions by walking block structure / raw-line structure directly. Known Qt bug in the
  same area: QTBUG-89630 (missing characters after clearing `MarkdownText` content).
- Per-run serialization gotcha: if a selection's very first block is fenced-code-formatted
  and there's no preceding block included in the selection, Qt's markdown writer silently
  drops the opening fence. Workaround: extend the selection start back one character into
  the previous block's separator (not part of any block's actual text).
- A fenced code block sitting tight (zero blank blocks) against a plain block in the same
  serialization pass has its required separating blank line silently dropped by Qt's
  writer — force a run boundary at every code-format transition, even with no spacer block
  present, to guarantee the CommonMark-minimum separator gets written.

## Architecture decisions worth knowing before touching related code

- **Settings/session-state pattern**: `QSettings` (IniFormat), one group per concern.
  Scalars are plain `Appearance/<key>` values; lists use INI arrays
  (`beginWriteArray`/`beginReadArray`, e.g. `NoteSessionState` under `OpenNotes`);
  per-note overrides use a dedicated `NoteOverride_<id>` group with a `has<Field>`
  flag + value per overridable field (not a JSON blob) — keeps the file human-inspectable
  and consistent with the rest of `Settings`. Every setter syncs synchronously + emits
  `NOTIFY` (settings change rarely, so no debounce, unlike `NoteStore`'s content saves).
- **A struct gaining a new field is safe with existing brace-init call sites** (aggregate
  init fills trailing members from their defaults) — but **any code that partially
  reconstructs such a struct from scratch instead of mutating the cached instance will
  silently reset every field it doesn't explicitly set.** Real bug hit exactly this way
  (`OpenNotesModel::updateGeometry` rebuilt a fresh `NoteSessionState`, resetting
  `alwaysOnTop` on every drag/resize) — when adding a partial-update method touching a
  struct that already has other persisted fields, seed from the existing cached value
  first, don't default-construct.
- **Tray icon count badge**: composited via `QQuickImageProvider`
  (`image://notecount/<count>`), not any native OS tray-badge API — none exists portably
  across desktop environments.
- **Single-instance replace, not focus-existing**: new instance tells the old one to quit
  (reusing the existing `aboutToQuit` position-capture path) rather than building a
  separate "hand off state directly" IPC mechanism.
  - `QLocalServer::removeServer()` does not check ownership — it deletes whatever socket
    file is currently at that path. A guard that's just been *ceded* (replaced) must not
    run this in its own later destructor, or it can delete the *new* owner's live socket
    if its shutdown takes any real wall-clock time. Track a "ceded" flag, set the moment
    the replace handoff happens, and skip cleanup in the destructor when set.
- **Notes default to NOT always-on-top** (a prior version forced it on unconditionally) —
  pin is an explicit per-note opt-in, both via a KWin Force `above` rule (primary, reused
  KWin-rule infra) and a `Qt::WindowStaysOnTopHint` fallback for non-KWin platforms.

## Testing patterns

- Catch2 BDD (`SCENARIO`/`GIVEN`/`WHEN`/`THEN`) for the C++ core, meant to double as living
  documentation — read `tests/unit/*.cpp` as spec prose before writing new core logic.
- `tests/unit/test_main.cpp` supplies a custom Catch2 main that constructs a real
  `QCoreApplication` before running the session — required because specs use
  `QTimer`/`QFileSystemWatcher` signals needing a pumped event loop (`QTest::qWait`). Links
  `Catch2::Catch2`, not `Catch2WithMain`.
- Qt Quick Test (`tests/qml/`) instantiates real QML objects in a live `QQmlEngine` —
  proves the QML is wire-correct and error-free, but cannot exercise literal
  drag/hover/click gestures (see Wayland synthetic-input note above); those need a human
  pass, and should be called out as such rather than claimed as covered.
- `MarkdownText` comparisons should use `.length` (the serialized markdown), not
  `.text.length` (the plain-text view) — a bare `\n` re-serializes as a longer CommonMark
  hard break, so the two aren't the same unit.

## Reference: suopPanel patterns reused (read-only, adjacent working directory)

- `qml/TileGridHud.qml`: collapsed-button-expands-to-a-`Row`-of-glyphs-on-hover pattern —
  reused for `NoteHud.qml`'s pin/settings/close affordances.
- `qml/GlassOverlay.qml`: ported byte-identical, tunables reduced to fixed defaults for
  suopNotes rather than exposing all ~7 knobs suopPanel exposes.
- `qml/GlassAccent.qml`: a *different*, decorative glow-ring effect — not used here (plain
  `border.width`/`border.color` covered suopNotes' border requirement with no new file).
- suopPanel's own KWin rule for its window uses a numeric `screen=` index rather than a
  connector name — a known-fragile pattern in that codebase, not one to copy.
