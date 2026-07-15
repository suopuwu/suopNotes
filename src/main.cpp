#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSurfaceFormat>

#include "core/BlurEffect.h"
#include "core/DocumentHandler.h"
#include "core/EditorDragFilter.h"
#include "core/KWinRuleWriter.h"
#include "core/NoteAppearance.h"
#include "core/NoteStore.h"
#include "core/OpenNotesModel.h"
#include "core/PositionEventService.h"
#include "core/PositionManager.h"
#include "core/Settings.h"
#include "core/SingleInstanceGuard.h"
#include "core/TrayIconProvider.h"
#include "core/WindowInputMask.h"

namespace
{
QString windowTitleForNote(const QString &noteId)
{
    return QStringLiteral("suopNotes-%1").arg(noteId);
}

// Captures each open note's live on-screen position (Linux/KWin only — a
// no-op elsewhere) before the app actually quits, so the next launch's
// spawn-position KWin rules are seeded from where the user actually left
// them, not stale data. This is now just a last-chance safety net for a
// graceful quit (aboutToQuit) — the primary save path is
// PositionEventService, which persists a note's position the instant KWin
// reports its drag has actually finished (interactiveMoveResizeFinished),
// regardless of how the app eventually exits (Ctrl+C, crash, etc., none of
// which fire aboutToQuit at all). Size is tracked continuously client-side
// instead (QML persists it on every resize) since, unlike position, it IS
// directly readable on Wayland.
void captureLivePositionsBeforeQuit(Settings &settings)
{
    const QList<Settings::NoteSessionState> notes = settings.openNotes();
    for (Settings::NoteSessionState state : notes) {
        const LivePosition live = KWinRuleApplier::captureLivePosition(windowTitleForNote(state.noteId));
        if (!live.found)
            continue;

        state.x = live.x;
        state.y = live.y;
        state.screenName = live.screenName;
        settings.upsertNoteState(state);
    }
}
}

int main(int argc, char *argv[])
{
    // Must be set before QGuiApplication is constructed — a per-window format
    // set later is too late for the first window's surface to carry alpha,
    // which is required for real transparency and compositor blur-behind.
    QSurfaceFormat format;
    format.setAlphaBufferSize(8);
    QSurfaceFormat::setDefaultFormat(format);

    // QApplication (not QGuiApplication) is required — Qt.labs.platform's
    // SystemTrayIcon wraps QSystemTrayIcon, a QtWidgets class that asserts
    // without a live QApplication instance (confirmed live: QGuiApplication
    // alone aborted with "QWidget: Cannot create a QWidget without
    // QApplication").
    QApplication app(argc, argv);
    QApplication::setApplicationName("suopNotes");
    QApplication::setOrganizationName("suopNotes");

    // Single-instance (req. 2): a second launch replaces the first rather
    // than coexisting with or merely focusing it. Runs before any window is
    // created — if a prior instance answers, this call blocks (briefly,
    // bounded) until it has ceded, so there's never a window flash for an
    // instance that's about to be told to quit.
    SingleInstanceGuard singleInstanceGuard(QStringLiteral("suopNotes-singleinstance"));
    singleInstanceGuard.takeOwnership();

    qmlRegisterType<DocumentHandler>("SuopNotesUi", 1, 0, "DocumentHandler");
    qmlRegisterType<NoteAppearance>("SuopNotesUi", 1, 0, "NoteAppearance");
    qmlRegisterType<EditorDragFilter>("SuopNotesUi", 1, 0, "EditorDragFilter");

    Settings settings;
    NoteStore noteStore;
    noteStore.setNotesFolder(settings.notesFolder());
    OpenNotesModel openNotesModel(&settings);
    BlurEffect blurEffect;
    WindowInputMask windowInputMask;
    PositionManager positionManager;
    PositionEventService positionEventService(settings);

    // A replace-quit isn't the user closing these notes (see
    // PositionManager::suppressCleanupOnQuit) — must be set before app.quit()
    // actually unwinds the QML engine and starts destroying NoteWindow
    // objects.
    QObject::connect(&singleInstanceGuard, &SingleInstanceGuard::replaceRequested, &app, [&positionManager, &app]() {
        positionManager.setSuppressCleanupOnQuit(true);
        app.quit();
    });

    QObject::connect(&app, &QGuiApplication::aboutToQuit, [&settings]() {
        captureLivePositionsBeforeQuit(settings);
    });

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("settings", &settings);
    engine.rootContext()->setContextProperty("noteStore", &noteStore);
    engine.rootContext()->setContextProperty("openNotesModel", &openNotesModel);
    engine.rootContext()->setContextProperty("blurEffect", &blurEffect);
    engine.rootContext()->setContextProperty("windowInputMask", &windowInputMask);
    engine.rootContext()->setContextProperty("positionManager", &positionManager);
    engine.addImageProvider(QStringLiteral("notecount"), new TrayIconProvider);

    engine.loadFromModule("SuopNotesUi", "Main");

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
