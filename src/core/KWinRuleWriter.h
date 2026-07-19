#pragma once

#include <QMap>
#include <QString>

// A single per-note KWin window rule: spawn position (Apply Initially —
// the only thing KWin actually honors at window creation on Wayland, per
// the project's Wayland placement research) plus forcing the window out of
// the taskbar/pager/switcher (Qt::Tool alone doesn't achieve that on KWin).
//
// Deliberately carries no monitor/screen field: KWin only consults the
// position rule at window creation (any point inside *some* connected
// output is used verbatim via a raw move — the screen rule is never
// consulted in that path), so x/y being a genuine global coordinate is
// exactly what puts the window on the right monitor. A "screen=" rule
// would only affect a later interactive move/reconfigure, i.e. it would
// forcibly pin the note to one monitor against the user's own drag, for no
// spawn-correctness benefit — so this app writes no such rule at all.
struct KWinRuleSpec
{
    QString windowTitle; // matched via title/titlematch — wmclass is shared
                         // app-wide, so per-note windows need to be
                         // identified by their unique title instead.
    int x = 0; // true global (virtual-desktop) coordinates — the same
    int y = 0; // convention frameGeometry and Settings use everywhere else.

    // Pin (req. 5): Force-keeps this note's window above others when true.
    // Defaults to false — un-pinned notes get normal stacking, same as any
    // other window; a note only stays above everything else once the user
    // explicitly pins it.
    bool alwaysOnTop = false;
};

// Pure: the flat kwinrulesrc keys for one rule group. Does not decide the
// group's uuid, does not touch [General], does not talk to KWin at all —
// see KWinRuleApplier for the live, non-pure half.
QMap<QString, QString> buildKWinRuleKeys(const KWinRuleSpec &spec);

// Read-only change detection: returns true iff writing `desiredKeys` to
// `group` in the kwinrulesrc INI at `filePath` would be a complete no-op —
// the group is already listed in [General]'s rules, already has every desired
// key at exactly its value, and carries no stale screen/screenrule key that
// would need deleting. Any mismatch (or a missing file) returns false. This is
// what lets a normal restart skip re-writing (and re-reconfiguring) rules that
// are already correct. Reads the file with a plain INI line scan rather than
// QSettings on purpose: QSettings' IniFormat parses an unquoted comma value
// like position=640,120 as a QStringList, so it would misread every position
// and report it changed. Reading is safe; nothing here writes the file (the
// reason WRITES go through kwriteconfig6 is corruption risk, which a read
// can't cause).
bool kwinRuleAlreadyMatches(const QString &filePath, const QString &group,
                            const QMap<QString, QString> &desiredKeys);

// Result of a best-effort live-position query. found=false whenever the
// probe couldn't complete for any reason (no KWin session, window not
// currently open, timing/parsing failure) — callers should fall back to
// the last-known persisted position rather than treat this as fatal.
struct LivePosition
{
    bool found = false;
    int x = 0;
    int y = 0;
    QString screenName;
};

// The live half: shells out to kwriteconfig6/kreadconfig6 against the
// user's real ~/.config/kwinrulesrc (deliberately NOT rewritten wholesale
// via QSettings — a hand-rolled INI writer risks corrupting other apps'
// rule groups sharing that file; kwriteconfig6 is KWin's own supported
// tool for exactly this). No-op everywhere this isn't available (non-Linux,
// no KWin session, or the tool missing).
class KWinRuleApplier
{
public:
    // Whether the CONFIG-writing half below (applyRule/removeRule) can run:
    // probes kwriteconfig6/kreadconfig6, the only tools those two use.
    static bool isAvailable();

    // Whether the LIVE, KWin-scripting half below (captureLivePosition/
    // moveWindow) can run. Separate from isAvailable() on purpose: that one
    // probes kwriteconfig6/kreadconfig6, but these two shell out to qdbus6,
    // which is an entirely different package (qt6-tools) and can be missing
    // on a system that has kwriteconfig6 perfectly well. Sharing one probe
    // meant isAvailable() answered TRUE while every moveWindow() silently
    // did nothing at all — see moveWindow's own comment for why a silently
    // no-op move is worse than an obvious one.
    static bool isScriptingAvailable();

    // Writes/updates this note's rule group with the given spec and
    // triggers a KWin reconfigure. The note's own id is reused as the
    // group name (already a UUID from NoteStore) — no separate rule-id
    // bookkeeping needed. Equivalent to writeRule() followed by a
    // reconfigure() only if something actually changed.
    static void applyRule(const QString &noteId, const KWinRuleSpec &spec);

    // The config-writing half of applyRule WITHOUT the trailing KWin
    // reconfigure. Returns true iff it actually wrote anything — i.e. the
    // group didn't already have exactly these keys (and its [General] listing
    // and no stale screen rule). A false return means the on-disk rule is
    // already what this spec would produce, so both the writes AND the
    // reconfigure can be skipped; a normal restart (every note's rule
    // identical to last session's) hits this path for every window, turning
    // startup from a per-window kwriteconfig6 storm + reconfigure-each into
    // near-nothing. Split out from applyRule so a batch of writes (all the
    // windows restored at startup) can share ONE reconfigure at the end
    // instead of paying its settle cost per window — see PositionManager's
    // beginBatch()/endBatch().
    static bool writeRule(const QString &noteId, const KWinRuleSpec &spec);

    // Asks KWin to reload its rule cache so freshly-written rules take
    // effect. Public (was private) so a batched caller can issue a single
    // reconfigure covering many writeRule() calls. Carries its own short
    // settle delay — see the definition.
    static void reconfigure();

    // Deletes this note's rule group's keys and drops it from
    // [General]'s rules list, then triggers a KWin reconfigure.
    static void removeRule(const QString &noteId);

    // Best-effort: queries KWin's live window list (via a temporary
    // KWin scripting probe) for the window with this exact title, and
    // returns its current on-screen position + output name.
    static LivePosition captureLivePosition(const QString &windowTitle);

    // Live-moves an already-open window (matched by exact title, same
    // technique captureLivePosition uses for reading) to a new global
    // (virtual-desktop) position, via a one-shot KWin script that sets
    // frameGeometry directly. Needed for windows that are shown once and
    // then repositioned many times afterward (e.g. the app-wide selection
    // toolbar following whichever note is focused): unlike a note's first
    // spawn position, which the Apply-Initially rule in applyRule()
    // handles, a plain QML x/y assignment on an already-mapped Wayland
    // top-level window is not honored by KWin at all (confirmed live —
    // the window stayed wherever KWin's own default placement put it,
    // ignoring the requested coordinates) — an actual KWin-side move is
    // required for every reposition after the first.
    static void moveWindow(const QString &windowTitle, int x, int y);

private:
    static QString groupNameFor(const QString &noteId);
    static void updateGeneralRulesList(const QString &groupName, bool add);
};
