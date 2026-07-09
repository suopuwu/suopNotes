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
    static bool isAvailable();

    // Writes/updates this note's rule group with the given spec and
    // triggers a KWin reconfigure. The note's own id is reused as the
    // group name (already a UUID from NoteStore) — no separate rule-id
    // bookkeeping needed.
    static void applyRule(const QString &noteId, const KWinRuleSpec &spec);

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
    static QString kwinRulesFilePath();
    static void updateGeneralRulesList(const QString &groupName, bool add);
    static void reconfigure();
};
