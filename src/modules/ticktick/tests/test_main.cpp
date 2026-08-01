#define CATCH_CONFIG_RUNNER
#include <catch2/catch_session.hpp>
#include <QCoreApplication>

// A real QCoreApplication is needed because TickTickTokenStore reports its
// results through the event loop (QTimer::singleShot on the plaintext path,
// keychain jobs on the other) so that callers see one consistent shape from
// both backends. QCoreApplication rather than QGuiApplication: nothing here
// touches fonts, windows or the browser — the one Gui call the module makes,
// QDesktopServices::openUrl, only happens in the live login flow, which these
// specs do not drive.
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    Catch::Session session;
    const int returnCode = session.applyCommandLine(argc, argv);
    if (returnCode != 0)
        return returnCode;

    return session.run();
}
