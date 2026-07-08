# Claude Code — Development Instructions

This file is read automatically by Claude Code at the start of every session.

---

## No continuous polling loops

The app must use minimal resources when idle/not actively being interacted with.

- **Never poll on a recurring `Timer { repeat: true }`** (or any other repeating-interval
  mechanism) to check external state (KWin window position, Settings, filesystem, etc.).
  Every persisted-state update must be triggered by an actual event (a user action, a signal
  from Qt/KWin, a file-system change notification) — never by "check again in N seconds,
  forever."
- **One-shot `Timer`s for local debouncing are fine and encouraged.** A `Timer { interval: …;
  onTriggered: … }` that gets `restart()`-ed by a real event and fires once after things settle
  is event-driven, not polling — it costs nothing while idle. Example: `NoteWindow.qml`'s
  `positionSaveDebounce` — a right-click-drag (or touch-drag) start restarts a 300ms one-shot
  timer; when it fires, it queries KWin for the note's now-settled live position and persists
  it. This exists specifically so position-saving doesn't depend on a clean app quit (Ctrl+C,
  a crash, `kill -9`, etc. never fire `aboutToQuit`, so a quit-only capture can silently lose
  the last drag) — solved by capturing shortly after every drag instead of only at shutdown,
  without ever running a background loop.
- Before adding any timer, ask: does this fire *because something happened*, or does it fire
  *unconditionally, on a schedule*? Only the former is acceptable.
