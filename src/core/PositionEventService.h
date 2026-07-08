#pragma once

#include <QObject>
#include <QList>
#include <QString>

#include "Settings.h"

// Replaces a fixed-delay "guess the drag is probably over by now" debounce
// with a real completion event. Confirmed live (dragged a note, watched
// the log): a QML MouseArea's onReleased/onCanceled never fire after a
// startSystemMove()-initiated compositor-driven move — nothing is ever
// delivered back to the client's own Qt/Wayland surface for that gesture.
// But KWin's own scripting API exposes exactly the missing event on the
// window object itself: interactiveMoveResizeFinished, confirmed live to
// fire reliably for this exact gesture (a frameless window moved via
// startSystemMove()), with the window's already-final frameGeometry
// available at that moment — no separate probe/query needed at all.
//
// This class owns both halves of that bridge for the app's entire
// lifetime (constructed once in main.cpp, not per-note): a persistent KWin
// script (loaded once, hooking every current *and future* suopNotes note
// window, unlike KWinRuleWriter's fire-and-forget probes) that calls back
// into this exact process over D-Bus the instant a drag settles, and the
// D-Bus-scriptable slot on this side that receives it and persists
// straight to Settings. No timer, no polling, no guessing.
class PositionEventService : public QObject
{
    Q_OBJECT
    // Pins the D-Bus interface name explicitly, rather than relying on
    // Qt's auto-generated one (confirmed live: without this, it came out
    // as "local.suopnotes.PositionEventService" — derived from the class
    // name, not anything under this file's control — while the watcher
    // script below assumed the service name doubled as the interface name;
    // the mismatch meant callDBus silently found no matching method).
    Q_CLASSINFO("D-Bus Interface", "org.suopnotes.PositionService")
public:
    explicit PositionEventService(Settings &settings, QObject *parent = nullptr);
    ~PositionEventService() override;

    // Pure: parses "suopNotes-<noteId>" out of windowTitle and returns
    // whichever of `notes` already matches that id (preserving its
    // width/height unchanged), with x/y/screenName overwritten — or a
    // fresh default-sized state if no match exists yet. Returns a state
    // with an empty noteId if windowTitle doesn't match the expected
    // prefix, signaling "ignore this." Extracted so the merge logic is
    // unit-testable without constructing the real class, whose constructor
    // has real side effects (registers a D-Bus service, loads a live KWin
    // script) — same reasoning as KWinRuleWriter's buildKWinRuleKeys/
    // KWinRuleApplier split.
    static Settings::NoteSessionState mergeLivePosition(const QList<Settings::NoteSessionState> &notes,
                                                         const QString &windowTitle, int x, int y,
                                                         const QString &screenName);

public Q_SLOTS:
    // Callable over D-Bus by the watcher script only — see the .cpp for the
    // exact script text and the service/path/interface names both sides
    // must agree on. Must be an actual Qt slot (not just a Q_SCRIPTABLE-
    // tagged plain method) — QDBusConnection::ExportScriptableSlots only
    // introspects real slots; a first attempt with Q_SCRIPTABLE on a plain
    // "public:" method silently exported nothing (confirmed live: the
    // object existed on the bus, but introspecting it showed only the
    // standard Properties/Introspectable methods, no notePositionChanged
    // at all — the watcher script's callDBus was therefore a silent no-op).
    Q_SCRIPTABLE void notePositionChanged(const QString &windowTitle, int x, int y, const QString &screenName);

private:
    void loadWatcherScript();
    void unloadWatcherScript();

    Settings &m_settings;
    QString m_pluginName;
    bool m_serviceRegistered = false;
    bool m_scriptLoaded = false;
};
