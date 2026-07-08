#define CATCH_CONFIG_RUNNER
#include <catch2/catch_session.hpp>
#include <QGuiApplication>

// A real QGuiApplication (not Catch2's default main, and not merely
// QCoreApplication) is required because:
//  - NoteStore/Settings specs exercise QTimer-based debouncing and
//    QFileSystemWatcher signals, which need a running Qt event loop
//    (pumped per-test via QTest::qWait).
//  - TextFormatting specs call QTextDocument::toMarkdown(), which internally
//    needs QFontDatabase — QCoreApplication alone makes that qFatal/abort.
// Run under QT_QPA_PLATFORM=offscreen (set via CTest properties) since this
// binary never shows a real window.
int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    Catch::Session session;
    int returnCode = session.applyCommandLine(argc, argv);
    if (returnCode != 0)
        return returnCode;

    return session.run();
}
