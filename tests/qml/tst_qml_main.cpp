#include <QObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QtQuickTest/quicktest.h>

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

// Injects the context properties every NoteWindow.qml instance expects:
// a Settings backed by a QTemporaryDir (never a real user config path),
// a NoteStore/OpenNotesModel pointed at that same temp dir, a real
// BlurEffect (a no-op off Linux/without KWindowSystem, harmless to call in
// tests either way), a real WindowInputMask (its setMask() has no observable
// effect under the offscreen QPA plugin, but it still emits inputMaskApplied,
// which is what the tests below actually assert on), and a real
// PositionManager (a no-op off Linux/without
// kwriteconfig6 — harmless here too; never touches a real kwinrulesrc in
// this offscreen test environment since it targets the test system's own,
// if any). Also registers the DocumentHandler and EditorDragFilter QML
// types NoteWindow.qml's editor instantiates/is wrapped in.
//
// The FileRevealer here is real, like the rest, but is present only so the
// QML that binds to it resolves — no test calls reveal(), and none should:
// unlike every other object above, its one method has an effect OUTSIDE this
// process that the offscreen platform does nothing to contain (it would
// launch the tester's actual file manager). Its dispatch logic is covered in
// tests/unit/test_file_revealer.cpp instead, and what the menu does with it
// is asserted through the row's enabled state — see
// tst_NoteContextMenu.qml's reveal tests.
class Setup : public QObject
{
    Q_OBJECT
public:
    Setup()
        : m_settings(m_tempDir.path() + QStringLiteral("/settings.ini"))
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
    }

private:
    QTemporaryDir m_tempDir;
    Settings m_settings;
    BlurEffect m_blurEffect;
    WindowInputMask m_windowInputMask;
    NoteStore m_noteStore;
    OpenNotesModel m_openNotesModel;
    PositionManager m_positionManager;
    AutostartManager m_autostartManager;
    FileRevealer m_fileRevealer;
};

QUICK_TEST_MAIN_WITH_SETUP(suopNotesQml, Setup)

#include "tst_qml_main.moc"
