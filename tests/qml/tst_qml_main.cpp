#include <QObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QtQuickTest/quicktest.h>

#include <memory>

#include "core/AutostartManager.h"
#include "core/BlurEffect.h"
#include "core/DocumentHandler.h"
#include "core/EditorDragFilter.h"
#include "core/FileRevealer.h"
#include "core/NoteAppearance.h"
#include "core/NoteStore.h"
#include "core/OpenNotesModel.h"
#include "core/PositionManager.h"
#include "core/Settings.h"
#include "core/WindowInputMask.h"

#ifdef SUOPNOTES_HAVE_TICKTICK
#include "TickTickModule.h"
#endif

// Injects the context properties every NoteWindow.qml instance expects:
// a Settings backed by a QTemporaryDir (never a real user config path),
// a NoteStore/OpenNotesModel pointed at that same temp dir, a real
// BlurEffect (a no-op off Linux/without KWindowSystem, harmless to call in
// tests either way), a real WindowInputMask (its setMask() has no observable
// effect under the offscreen QPA plugin, but it still emits inputMaskApplied,
// which is what the tests below actually assert on), and a real
// PositionManager whose CONFIG-writing half is switched off for the run — see
// disableKWinRules() below, which fixes a real bug rather than tidying up.
// Also registers the DocumentHandler and EditorDragFilter QML types
// NoteWindow.qml's editor instantiates/is wrapped in.
//
// The FileRevealer here is real, like the rest, but is present only so the
// QML that binds to it resolves — no test calls reveal(), and none should:
// unlike every other object above, its one method has an effect OUTSIDE this
// process that the offscreen platform does nothing to contain (it would
// launch the tester's actual file manager). Its dispatch logic is covered in
// tests/unit/test_file_revealer.cpp instead, and what the menu does with it
// is asserted through the row's enabled state — see
// tst_NoteContextMenu.qml's reveal tests.
namespace {

// Stops this run writing KWin window rules into the tester's own live
// ~/.config/kwinrulesrc.
//
// Unlike Settings, the notes folder and the TickTick module — all of which the
// harness points at a QTemporaryDir — KWin rules have no redirectable path at
// all: KWinRuleApplier shells out to `kwriteconfig6 --file kwinrulesrc`, and
// that name always resolves to the real user config. So every NoteWindow these
// tests construct used to create a genuine rule group there, and delete it again
// on teardown.
//
// For a test note with an invented id that is litter (a real user's config was
// found holding leftover suopNotes-chromeWindowTest and
// suopNotes-toolbar-test-note-* groups from earlier runs). For ONE test window
// it is a real bug: tst_NoteWindow.qml's task note asks the TickTick module for
// the actual task note's id, deliberately, so that it really is the task note —
// which means it shares the live note's rule group AND its window title. A suite
// run against an open app therefore rewrites the real note's spawn position to
// this window's, then deletes the group outright on teardown, leaving that note
// with no spawn rule until the app next writes one. KWin then places it by its
// own default rather than where the user left it ("sometimes it will spawn in
// the wrong monitor").
//
// Set through the environment rather than a constructor argument because the
// applier is a set of statics reached from several places (PositionManager here,
// and NoteWindow.qml's own applyPosition), with no single instance to configure.
// Returns bool purely so it can be run from the initialiser list, before any
// member that might reach the applier exists.
bool disableKWinRules()
{
    qputenv("SUOPNOTES_NO_KWIN_RULES", "1");
    return true;
}

}

class Setup : public QObject
{
    Q_OBJECT
public:
    Setup()
        : m_kwinRulesDisabled(disableKWinRules())
        , m_settings(m_tempDir.path() + QStringLiteral("/settings.ini"))
        , m_openNotesModel(&m_settings)
        // Point autostart at a temp dir with a dummy exec path so any QML test
        // that toggles "Launch on login" writes there, never the tester's real
        // ~/.config/autostart.
        , m_autostartManager(m_tempDir.path() + QStringLiteral("/autostart"),
                             m_tempDir.path() + QStringLiteral("/suopNotes"))
    {
        qmlRegisterType<DocumentHandler>("SuopNotesUi", 1, 0, "DocumentHandler");
        qmlRegisterType<NoteAppearance>("SuopNotesUi", 1, 0, "NoteAppearance");
        qmlRegisterType<EditorDragFilter>("SuopNotesUi", 1, 0, "EditorDragFilter");
        m_noteStore.setNotesFolder(m_tempDir.path() + QStringLiteral("/notes"));
#ifdef SUOPNOTES_HAVE_TICKTICK
        m_tickTickModule = std::make_unique<TickTickModule>(
            m_tempDir.path() + QStringLiteral("/ticktick.ini"),
            m_tempDir.path() + QStringLiteral("/ticktick-cache"));
#endif
    }

public slots:
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        engine->rootContext()->setContextProperty("settings", &m_settings);
        engine->rootContext()->setContextProperty("blurEffect", &m_blurEffect);
        engine->rootContext()->setContextProperty("windowInputMask", &m_windowInputMask);
        engine->rootContext()->setContextProperty("noteStore", &m_noteStore);
        engine->rootContext()->setContextProperty("openNotesModel", &m_openNotesModel);
        engine->rootContext()->setContextProperty("positionManager", &m_positionManager);
        engine->rootContext()->setContextProperty("autostart", &m_autostartManager);
        engine->rootContext()->setContextProperty("fileRevealer", &m_fileRevealer);
        // Mirrors main.cpp: the real module when it is built, null when it is
        // not — never an undefined name, which would be a hard ReferenceError
        // in any binding that touched it.
        //
        // The module is given temp paths so it cannot reach the tester's real
        // config, note cache or keyring.
#ifdef SUOPNOTES_HAVE_TICKTICK
        engine->rootContext()->setContextProperty("tickTick", m_tickTickModule.get());
#else
        engine->rootContext()->setContextProperty("tickTick", nullptr);
#endif
    }

private:
    // Declared FIRST so it is initialised first: members are constructed in
    // declaration order, and the guard has to be in place before anything that
    // could reach KWinRuleApplier exists. The value itself is unused.
    bool m_kwinRulesDisabled = false;
    QTemporaryDir m_tempDir;
    Settings m_settings;
    BlurEffect m_blurEffect;
    WindowInputMask m_windowInputMask;
    NoteStore m_noteStore;
    OpenNotesModel m_openNotesModel;
    PositionManager m_positionManager;
    AutostartManager m_autostartManager;
    FileRevealer m_fileRevealer;
#ifdef SUOPNOTES_HAVE_TICKTICK
    // Constructed in the body rather than the init list: it needs m_tempDir's
    // path, and a unique_ptr keeps this header-free when the module is absent.
    std::unique_ptr<TickTickModule> m_tickTickModule;
#endif
};

QUICK_TEST_MAIN_WITH_SETUP(suopNotesQml, Setup)

#include "tst_qml_main.moc"
