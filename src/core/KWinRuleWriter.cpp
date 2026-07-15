#include "KWinRuleWriter.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QThread>

#include <atomic>

QMap<QString, QString> buildKWinRuleKeys(const KWinRuleSpec &spec)
{
    QMap<QString, QString> keys;

    keys["description"] = QStringLiteral("suopNotes note window: %1").arg(spec.windowTitle);

    // Scope to this app, then to this specific note's window via its
    // unique title — wmclass alone is shared by every note window (Qt
    // derives it app-wide from the binary/desktop file), so title is what
    // actually distinguishes one note from another.
    // Must match Qt's actual resourceClass for this app exactly (confirmed
    // live via a KWin scripting probe: "suopNotes", mixed case — NOT the
    // lowercased form one might assume).
    keys["wmclass"] = QStringLiteral("suopNotes");
    keys["wmclassmatch"] = QStringLiteral("1"); // ExactMatch
    keys["title"] = spec.windowTitle;
    keys["titlematch"] = QStringLiteral("1"); // ExactMatch — title is a unique per-note string

    // Position: "Apply Initially" (3) is what actually seeds the spawn
    // position on Wayland — "Remember" (4) never updates on Wayland, per
    // the project's Wayland placement research, so it's deliberately not
    // used here; live position is captured separately and written back via
    // a fresh applyRule() call instead.
    //
    // spec.x/y are a true global (virtual-desktop) point, not adjusted for
    // any monitor, and there is deliberately no "screen="/"screenrule="
    // key at all: KWin only consults the position rule at window creation
    // (any point that lies inside *some* connected output is used verbatim
    // via a raw move — the screen rule is never consulted in that path), so
    // the global position alone already determines the correct spawn
    // monitor. A "screen=" rule only ever matters later, for an interactive
    // move to another output or a reconfigure — i.e. it would forcibly pin
    // the note to one monitor, fighting the user's own drag, for no spawn
    // benefit at all. (An earlier version of this rule wrote a screen-
    // relative position plus a Force screen rule; removing the Force
    // uncovered that the position math itself was wrong — it only produced
    // the right spot because Force's corrective re-snap was masking it.)
    keys["position"] = QStringLiteral("%1,%2").arg(spec.x).arg(spec.y);
    keys["positionrule"] = QStringLiteral("3");

    // Qt::Tool (set in NoteWindow.qml) hides the window from the
    // taskbar/switcher on Windows/macOS but not on KWin/Wayland — force it
    // here to close that gap.
    keys["skiptaskbar"] = QStringLiteral("true");
    keys["skiptaskbarrule"] = QStringLiteral("2");
    keys["skippager"] = QStringLiteral("true");
    keys["skippagerrule"] = QStringLiteral("2");
    keys["skipswitcher"] = QStringLiteral("true");
    keys["skipswitcherrule"] = QStringLiteral("2");

    // Pin (req. 5): Force (2) either way, so a stale "above=true" from a
    // previously-pinned state is actively cleared when unpinned, not just
    // left in place. Force-false still permits normal alt-tab/click-to-raise
    // behavior — it only stops KWin from doing anything's *own* implicit
    // always-above handling, which this app never asked for anyway.
    keys["above"] = spec.alwaysOnTop ? QStringLiteral("true") : QStringLiteral("false");
    keys["aboverule"] = QStringLiteral("2");

    return keys;
}

namespace {

constexpr int kProcessTimeoutMs = 3000;

const QSet<QString> &boolValuedKeys()
{
    static const QSet<QString> keys = { QStringLiteral("skiptaskbar"), QStringLiteral("skippager"),
                                         QStringLiteral("skipswitcher"), QStringLiteral("above") };
    return keys;
}

// Every call is bounded (waitForFinished with a timeout, not the
// unbounded QProcess::execute) — a hung/unresponsive kwriteconfig6,
// kreadconfig6, or qdbus6 must not be able to freeze the app.
bool runProcess(const QString &program, const QStringList &arguments, QString *stdOut = nullptr)
{
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForFinished(kProcessTimeoutMs)) {
        process.kill();
        return false;
    }
    if (stdOut)
        *stdOut = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

QString readKConfig(const QString &group, const QString &key)
{
    QString output;
    runProcess(QStringLiteral("kreadconfig6"), { "--file", "kwinrulesrc", "--group", group, "--key", key }, &output);
    return output;
}

void writeKConfig(const QString &group, const QString &key, const QString &value)
{
    QStringList args{ "--file", "kwinrulesrc", "--group", group, "--key", key };
    if (boolValuedKeys().contains(key))
        args << "--type" << "bool";
    args << value;
    runProcess(QStringLiteral("kwriteconfig6"), args);
}

void deleteKConfigKey(const QString &group, const QString &key)
{
    runProcess(QStringLiteral("kwriteconfig6"),
               { "--file", "kwinrulesrc", "--group", group, "--key", key, "--delete", QString() });
}

}

bool KWinRuleApplier::isAvailable()
{
    return !QStandardPaths::findExecutable(QStringLiteral("kwriteconfig6")).isEmpty()
        && !QStandardPaths::findExecutable(QStringLiteral("kreadconfig6")).isEmpty();
}

bool KWinRuleApplier::isScriptingAvailable()
{
    return !QStandardPaths::findExecutable(QStringLiteral("qdbus6")).isEmpty();
}

QString KWinRuleApplier::groupNameFor(const QString &noteId)
{
    return QStringLiteral("suopNotes-%1").arg(noteId);
}

QString KWinRuleApplier::kwinRulesFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/kwinrulesrc");
}

void KWinRuleApplier::applyRule(const QString &noteId, const KWinRuleSpec &spec)
{
    if (!isAvailable())
        return;

    const QString group = groupNameFor(noteId);
    const QMap<QString, QString> keys = buildKWinRuleKeys(spec);
    for (auto it = keys.constBegin(); it != keys.constEnd(); ++it)
        writeKConfig(group, it.key(), it.value());

    // Migration: an earlier version of this rule forced a "screen="/
    // "screenrule=" key (see buildKWinRuleKeys' comment on why that's gone
    // now). A note whose group was written by that version still has it
    // sitting in the user's real kwinrulesrc — writeKConfig above only ever
    // adds/updates keys buildKWinRuleKeys currently produces, so a stale
    // one left over from before would otherwise keep forcibly pinning the
    // note to one monitor forever. Deleting a key that was never present
    // is a harmless no-op, so this doesn't need to know which case it's in.
    deleteKConfigKey(group, QStringLiteral("screen"));
    deleteKConfigKey(group, QStringLiteral("screenrule"));

    updateGeneralRulesList(group, /*add=*/true);
    reconfigure();
}

void KWinRuleApplier::removeRule(const QString &noteId)
{
    if (!isAvailable())
        return;

    const QString group = groupNameFor(noteId);
    // buildKWinRuleKeys' key SET is fixed regardless of the spec's values,
    // so this always matches exactly what applyRule wrote for this group.
    const QMap<QString, QString> keys = buildKWinRuleKeys(KWinRuleSpec());
    for (auto it = keys.constBegin(); it != keys.constEnd(); ++it)
        deleteKConfigKey(group, it.key());

    updateGeneralRulesList(group, /*add=*/false);
    reconfigure();
}

void KWinRuleApplier::updateGeneralRulesList(const QString &groupName, bool add)
{
    const QString existing = readKConfig(QStringLiteral("General"), QStringLiteral("rules"));
    QStringList groups = existing.isEmpty() ? QStringList() : existing.split(',', Qt::SkipEmptyParts);

    const bool present = groups.contains(groupName);
    if (add && !present) {
        groups << groupName;
    } else if (!add && present) {
        groups.removeAll(groupName);
    } else {
        return; // already in the desired state — skip the write + reconfigure
    }

    writeKConfig(QStringLiteral("General"), QStringLiteral("rules"), groups.join(','));
    writeKConfig(QStringLiteral("General"), QStringLiteral("count"), QString::number(groups.size()));
}

void KWinRuleApplier::reconfigure()
{
    runProcess(QStringLiteral("qdbus6"), { "org.kde.KWin", "/KWin", "org.kde.KWin.reconfigure" });
    // The D-Bus call itself returns as soon as KWin acknowledges it, but
    // KWin's own rule-cache rebuild appears to happen slightly after that
    // (confirmed empirically: without this, a note applied and shown
    // immediately after spawned at KWin's own default placement, not the
    // requested rule position) — a short settle delay here is cheaper and
    // more honest than a caller-side retry loop.
    QThread::msleep(200);
}

LivePosition KWinRuleApplier::captureLivePosition(const QString &windowTitle)
{
    LivePosition result;
    // isScriptingAvailable(), not isAvailable(): like moveWindow below, this
    // shells out to qdbus6 and never touches kwriteconfig6/kreadconfig6,
    // which is all the latter probes for. A silent found=false here is
    // correct and already contractual (see LivePosition's own comment —
    // callers fall back to the last persisted position), so unlike
    // moveWindow this needs no warning.
    if (!isScriptingAvailable())
        return result;

    QTemporaryFile scriptFile(QDir::tempPath() + QStringLiteral("/suopNotes-probe-XXXXXX.js"));
    if (!scriptFile.open())
        return result;

    // A fresh marker per call so we can find exactly this invocation's
    // output line in the journal, not a stale one from a previous probe.
    const QString marker =
        QStringLiteral("SUOPNOTES_PROBE_%1").arg(QDateTime::currentMSecsSinceEpoch());
    const QString script = QStringLiteral(
        "for (const w of workspace.windowList()) {\n"
        "    if (String(w.caption) === \"%1\") {\n"
        "        console.log(\"%2 \" + w.frameGeometry.x + \",\" + w.frameGeometry.y + \" \" + w.output.name);\n"
        "    }\n"
        "}\n").arg(windowTitle, marker);

    scriptFile.write(script.toUtf8());
    scriptFile.close();

    const QString pluginName = QStringLiteral("suopNotesPositionProbe");
    if (!runProcess(QStringLiteral("qdbus6"),
                     { "org.kde.KWin", "/Scripting", "loadScript", scriptFile.fileName(), pluginName }))
        return result;

    runProcess(QStringLiteral("qdbus6"), { "org.kde.KWin", "/Scripting", "start" });

    // Give the script + journald a brief moment to actually run and flush
    // before we go looking for its output.
    QThread::msleep(150);

    QString journalOutput;
    runProcess(QStringLiteral("journalctl"), { "--since", "-10 seconds", "--no-pager", "-q" }, &journalOutput);

    runProcess(QStringLiteral("qdbus6"), { "org.kde.KWin", "/Scripting", "unloadScript", pluginName });

    const QString needle = marker + QStringLiteral(" ");
    const int idx = journalOutput.lastIndexOf(needle);
    if (idx < 0)
        return result;

    const QString rest = journalOutput.mid(idx + needle.size());
    const int newlineIdx = rest.indexOf(QLatin1Char('\n'));
    const QString line = (newlineIdx < 0 ? rest : rest.left(newlineIdx)).trimmed();
    const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() != 2)
        return result;

    const QStringList xy = parts.at(0).split(QLatin1Char(','));
    if (xy.size() != 2)
        return result;

    bool xOk = false;
    bool yOk = false;
    const int x = xy.at(0).toInt(&xOk);
    const int y = xy.at(1).toInt(&yOk);
    if (!xOk || !yOk)
        return result;

    result.found = true;
    result.x = x;
    result.y = y;
    result.screenName = parts.at(1);
    return result;
}

void KWinRuleApplier::moveWindow(const QString &windowTitle, int x, int y)
{
    // isScriptingAvailable(), not isAvailable(): this function's actual
    // dependency is qdbus6, which the latter never probed for (bug: it
    // checked kwriteconfig6/kreadconfig6, neither of which is used here).
    //
    // Warns rather than returning silently. Callers use this to park
    // permanently-mapped, always-on-top windows off-screen, and a park that
    // no-ops leaves one of those sitting on screen with no retry and no
    // trace of why. That can no longer swallow clicks — SelectionToolbar.qml
    // masks those windows' input regions independently of this call ever
    // landing (see its parkOffscreen comment) — but it would still leave
    // them VISIBLE in the wrong place, which is worth a log line.
    if (!isScriptingAvailable()) {
        qWarning() << "KWinRuleApplier::moveWindow: qdbus6 not found; cannot move" << windowTitle
                   << "— windows that rely on being parked off-screen will stay where they are.";
        return;
    }

    QTemporaryFile scriptFile(QDir::tempPath() + QStringLiteral("/suopNotes-move-XXXXXX.js"));
    if (!scriptFile.open())
        return;

    // A plain {x,y,width,height} object (not Qt.rect(), which isn't
    // guaranteed available in KWin's own scripting JS context) — KWin's
    // QJSEngine binding accepts this for a QRect-typed property like
    // frameGeometry. width/height are read back from the window's own
    // current geometry so this call only ever moves it, never resizes it.
    const QString script = QStringLiteral(
        "for (const w of workspace.windowList()) {\n"
        "    if (String(w.caption) === \"%1\") {\n"
        "        w.frameGeometry = { x: %2, y: %3, width: w.frameGeometry.width, height: w.frameGeometry.height };\n"
        "    }\n"
        "}\n").arg(windowTitle).arg(x).arg(y);

    scriptFile.write(script.toUtf8());
    scriptFile.close();

    // Unique per call, not a shared constant (bug found live): this
    // function runs on its own dedicated background thread per call (see
    // PositionManager::moveWindow), and callers now routinely issue two
    // calls for the same popup close together — a hide's park-off-screen
    // move immediately followed by a show's real reposition, or a
    // fast-settling resettle debounce piling a second move on top of the
    // first. A shared plugin name meant concurrent loadScript/start/
    // unloadScript calls collided on KWin's side: either the second
    // loadScript was rejected outright, or KWin silently reused the
    // already-loaded script under that name — either way, one call's own
    // intended position change could be skipped entirely while its
    // unloadScript later tore down whatever the OTHER call had just
    // loaded. Live symptom: a popup that never visibly appeared anywhere
    // at all (not even at the wrong spot), because neither call's actual
    // frameGeometry write survived the collision. A monotonic per-call
    // suffix gives every call's script its own KWin-side identity, so
    // concurrent calls can no longer step on each other.
    static std::atomic<quint64> callCounter{ 0 };
    const QString pluginName = QStringLiteral("suopNotesMoveWindow_%1").arg(callCounter.fetch_add(1));
    if (!runProcess(QStringLiteral("qdbus6"),
                     { "org.kde.KWin", "/Scripting", "loadScript", scriptFile.fileName(), pluginName }))
        return;

    runProcess(QStringLiteral("qdbus6"), { "org.kde.KWin", "/Scripting", "start" });
    // Same reasoning as captureLivePosition's own settle delay: "start"
    // only queues the script, it doesn't wait for it to actually run —
    // unloading immediately after could race the script's own execution.
    QThread::msleep(50);
    runProcess(QStringLiteral("qdbus6"), { "org.kde.KWin", "/Scripting", "unloadScript", pluginName });
}
