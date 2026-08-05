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

### The one sanctioned exception: the TickTick pull

`TickTickSync`'s `m_scheduleTimer` is a genuine repeating timer, granted explicitly and
deliberately. It is the *only* one in the app. Do not "fix" it, and do not treat it as
precedent for a second one.

Why it earns the exception: the TickTick API has no webhooks, no server-sent events and no
changed-since query, so there is **no event that can exist** to tell the app a task changed
elsewhere. Asking is the only mechanism the API offers. Without a schedule, the only way to see
a task added on a phone is to remember to press Sync — which makes the integration feel broken
rather than idle.

What keeps it within the spirit of the rule, and what any change to it must preserve:

- One HTTPS request per tick, nothing else. Between ticks it is a single armed `QTimer`.
- Defaults to once an hour (`TickTickPrefs::kDefaultSyncIntervalMinutes`).
- Floored at 5 minutes (`kMinSyncIntervalMinutes`), enforced in `TickTickPrefs` on both read and
  write so a hand-edited INI cannot turn it into a busy loop.
- Stopped outright — not merely short-circuited in its handler — whenever the module is
  unauthorized or the user sets the interval to 0.

Every other TickTick trigger is still an event: a login completing, a stored token finishing its
async load at startup, a tray click, the Settings button, and an edit's one-shot upload debounce.

## Tests as living documentation

Tests should serve as living documentation of how the code is meant to behave, not just as
a correctness check.

- Test names and structure should describe the behavior being verified in plain terms, so a
  reader can understand what a component/function is supposed to do by skimming the test file
  alone — without needing to read the implementation first.
- When behavior changes, update the corresponding tests in the same change, so the tests never
  drift out of sync with what the code actually does. Stale tests are worse than no tests,
  since they document behavior that no longer exists.
- Prefer clear, explicit assertions over clever/DRY test helpers that obscure what's actually
  being checked — a test that's easy to read top-to-bottom is more valuable as documentation
  than one that's short.
- When adding a new feature or fixing a bug, add or update a test that captures the expected
  behavior, so future readers (human or Claude) can trust the test suite to reflect current,
  intended behavior rather than having to cross-reference commit history.
