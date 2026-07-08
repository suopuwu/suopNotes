#include "PositionEventService.h"
#include "Settings.h"

#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace {

constexpr int kProcessTimeoutMs = 3000;

const QString &serviceName()
{
    static const QString name = QStringLiteral("org.suopnotes.PositionService");
    return name;
}

// Every call is bounded (waitForFinished with a timeout, not the unbounded
// QProcess::execute) — a hung/unresponsive qdbus6 must not be able to
// freeze the app. Mirrors KWinRuleWriter.cpp's runProcess.
bool runProcess(const QString &program, const QStringList &arguments)
{
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForFinished(kProcessTimeoutMs)) {
        process.kill();
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool kwinScriptingAvailable()
{
    return !QStandardPaths::findExecutable(QStringLiteral("qdbus6")).isEmpty();
}

}

PositionEventService::PositionEventService(Settings &settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    tryClaimService();
}

PositionEventService::~PositionEventService()
{
    unloadWatcherScript();
    if (m_serviceRegistered) {
        QDBusConnection::sessionBus().unregisterObject(QStringLiteral("/PositionService"));
        QDBusConnection::sessionBus().unregisterService(serviceName());
    }
}

void PositionEventService::tryClaimService()
{
    if (m_serviceRegistered)
        return;

    if (!QDBusConnection::sessionBus().registerService(serviceName())) {
        // Almost always a just-replaced predecessor still finishing its own
        // bounded-but-non-instant shutdown (captureLivePositionsBeforeQuit
        // does a real KWin round-trip per open note before that process's
        // own PositionEventService destructor releases this name — see
        // SingleInstanceGuard's destructor comment for the same timing
        // issue on its own IPC channel). Confirmed live: a fresh instance
        // launched while an old one was still mid-quit lost this race and,
        // before this fix, silently went without live position-saving for
        // its entire remaining lifetime.
        //
        // Rather than give up permanently, watch for the name to become
        // free and claim it the instant that happens — QDBusServiceWatcher
        // surfaces the bus's own NameOwnerChanged signal, so this is a real
        // event, not a poll. If some other process legitimately holds this
        // name indefinitely (shouldn't happen — SingleInstanceGuard is
        // supposed to guarantee only one suopNotes process runs at a time),
        // this simply never fires again; no cost while idle.
        if (!m_serviceWatcher) {
            m_serviceWatcher = new QDBusServiceWatcher(serviceName(), QDBusConnection::sessionBus(),
                                                         QDBusServiceWatcher::WatchForUnregistration, this);
            connect(m_serviceWatcher, &QDBusServiceWatcher::serviceUnregistered, this,
                    &PositionEventService::tryClaimService);
        }
        return;
    }

    m_serviceRegistered = true;
    // deleteLater(), not delete: this can run from inside a slot connected
    // to m_serviceWatcher's own serviceUnregistered signal, and Qt's own
    // signal-emission machinery is still on the stack above this call in
    // that case — safer to defer destruction than to free the object out
    // from under its own in-flight signal dispatch.
    if (m_serviceWatcher) {
        m_serviceWatcher->deleteLater();
        m_serviceWatcher = nullptr;
    }

    QDBusConnection::sessionBus().registerObject(QStringLiteral("/PositionService"), this,
                                                  QDBusConnection::ExportScriptableSlots);
    loadWatcherScript();
}

void PositionEventService::notePositionChanged(const QString &windowTitle, int x, int y,
                                                const QString &screenName)
{
    const Settings::NoteSessionState state = mergeLivePosition(m_settings.openNotes(), windowTitle, x, y, screenName);
    if (state.noteId.isEmpty())
        return;
    m_settings.upsertNoteState(state);
}

void PositionEventService::notePositionChanged(const QString &windowTitle, int x, double y,
                                                const QString &screenName)
{
    notePositionChanged(windowTitle, x, qRound(y), screenName);
}

void PositionEventService::notePositionChanged(const QString &windowTitle, double x, int y,
                                                const QString &screenName)
{
    notePositionChanged(windowTitle, qRound(x), y, screenName);
}

void PositionEventService::notePositionChanged(const QString &windowTitle, double x, double y,
                                                const QString &screenName)
{
    notePositionChanged(windowTitle, qRound(x), qRound(y), screenName);
}

Settings::NoteSessionState PositionEventService::mergeLivePosition(const QList<Settings::NoteSessionState> &notes,
                                                                    const QString &windowTitle, int x, int y,
                                                                    const QString &screenName)
{
    static const QString prefix = QStringLiteral("suopNotes-");
    if (!windowTitle.startsWith(prefix))
        return Settings::NoteSessionState{};

    const QString noteId = windowTitle.mid(prefix.size());

    // Preserve width/height (tracked separately, live, via QML's own
    // onWidthChanged/onHeightChanged) rather than clobbering them with
    // this struct's defaults.
    Settings::NoteSessionState state;
    state.noteId = noteId;
    for (const auto &existing : notes) {
        if (existing.noteId == noteId) {
            state = existing;
            break;
        }
    }
    state.x = x;
    state.y = y;
    state.screenName = screenName;
    return state;
}

void PositionEventService::loadWatcherScript()
{
    if (!kwinScriptingAvailable())
        return;

    QTemporaryFile scriptFile(QDir::tempPath() + QStringLiteral("/suopNotes-position-watcher-XXXXXX.js"));
    if (!scriptFile.open())
        return;

    // Hooks every note window that exists now, plus any opened later
    // (workspace.windowAdded) — a note opened after this script loads must
    // still be watched. w.caption is this window's title
    // ("suopNotes-<noteId>", see NoteWindow.qml); wmclass is shared by
    // every note so title is what actually identifies one from another,
    // same convention KWinRuleWriter's rules use.
    const QString script = QStringLiteral(
        "function hook(w) {\n"
        "    if (String(w.caption).indexOf(\"suopNotes-\") !== 0) return;\n"
        "    w.interactiveMoveResizeFinished.connect(function() {\n"
        "        callDBus(\"%1\", \"/PositionService\", \"%1\", \"notePositionChanged\",\n"
        "                 w.caption, w.frameGeometry.x, w.frameGeometry.y, w.output.name);\n"
        "    });\n"
        "}\n"
        "for (const w of workspace.windowList()) hook(w);\n"
        "workspace.windowAdded.connect(hook);\n").arg(serviceName());

    scriptFile.write(script.toUtf8());
    scriptFile.close();

    m_pluginName = QStringLiteral("suopNotesPositionWatcher");
    if (runProcess(QStringLiteral("qdbus6"),
                    { "org.kde.KWin", "/Scripting", "loadScript", scriptFile.fileName(), m_pluginName })) {
        runProcess(QStringLiteral("qdbus6"), { "org.kde.KWin", "/Scripting", "start" });
        m_scriptLoaded = true;
    }
}

void PositionEventService::unloadWatcherScript()
{
    if (!m_scriptLoaded)
        return;
    runProcess(QStringLiteral("qdbus6"), { "org.kde.KWin", "/Scripting", "unloadScript", m_pluginName });
    m_scriptLoaded = false;
}
