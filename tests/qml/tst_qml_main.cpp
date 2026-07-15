#include <QObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QtQuickTest/quicktest.h>

#include "core/BlurEffect.h"
#include "core/DocumentHandler.h"
#include "core/EditorDragFilter.h"
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
class Setup : public QObject
{
    Q_OBJECT
public:
    Setup()
        : m_settings(m_tempDir.path() + QStringLiteral("/settings.ini"))
        , m_openNotesModel(&m_settings)
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
    }

private:
    QTemporaryDir m_tempDir;
    Settings m_settings;
    BlurEffect m_blurEffect;
    WindowInputMask m_windowInputMask;
    NoteStore m_noteStore;
    OpenNotesModel m_openNotesModel;
    PositionManager m_positionManager;
};

QUICK_TEST_MAIN_WITH_SETUP(suopNotesQml, Setup)

#include "tst_qml_main.moc"
