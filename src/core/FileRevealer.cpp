#include "FileRevealer.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDesktopServices>
#include <QFileInfo>
#include <QStringList>
#include <QUrl>

namespace
{
// The freedesktop.org file-manager interface. The service name doubles as the
// interface name here, and the object path is fixed by the spec — all three
// are part of the contract every implementing file manager honors, so none of
// them is ours to choose.
constexpr auto kFileManagerService = "org.freedesktop.FileManager1";
constexpr auto kFileManagerPath = "/org/freedesktop/FileManager1";
constexpr auto kFileManagerInterface = "org.freedesktop.FileManager1";
constexpr auto kShowItemsMethod = "ShowItems";
}

FileRevealer::FileRevealer(QObject *parent)
    : QObject(parent)
{
}

bool FileRevealer::canReveal(const QString &filePath)
{
    if (filePath.isEmpty())
        return false;
    return QFileInfo::exists(filePath);
}

QString FileRevealer::containingFolder(const QString &filePath)
{
    if (!canReveal(filePath))
        return QString();
    return QFileInfo(filePath).absolutePath();
}

bool FileRevealer::reveal(const QString &filePath)
{
    if (!canReveal(filePath))
        return false;

    const QString absolutePath = QFileInfo(filePath).absoluteFilePath();
    const QString folder = containingFolder(filePath);

    // No session bus at all (a headless session, a stripped container): there
    // is no file manager to ask, so skip straight to the fallback rather than
    // constructing a call that can only fail.
    if (!QDBusConnection::sessionBus().isConnected()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
        return true;
    }

    // A raw createMethodCall rather than a QDBusInterface: constructing a
    // QDBusInterface performs a BLOCKING introspection round-trip on the spot,
    // which buys nothing here (the method's signature is fixed by the spec, so
    // there is nothing to discover) and costs a UI stall on a slow or absent
    // file manager. ShowItems takes the URIs to select plus a startup id, which
    // is optional and empty here — this app has no startup notification token
    // to pass on.
    QDBusMessage request = QDBusMessage::createMethodCall(kFileManagerService, kFileManagerPath,
                                                         kFileManagerInterface, kShowItemsMethod);
    request << QStringList { QUrl::fromLocalFile(absolutePath).toString() } << QString();

    // ASYNC, deliberately — a blocking call is the trap here. This is the one
    // D-Bus call in the app whose callee may need to be STARTED to answer it
    // (FileManager1 is activatable; a cold Dolphin/Nautilus launch can take
    // seconds, and the reply doesn't come back until it has), so any timeout
    // short enough not to freeze the UI is also short enough to spuriously
    // "fail" a launch that was merely slow — firing the fallback and leaving
    // the user with TWO windows for one click. Waiting for the real reply, off
    // the UI thread, is the only way to tell those two cases apart.
    //
    // Not a poll of any kind: the watcher fires once, on the actual reply.
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(request), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [folder](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        const QDBusPendingReply<> reply = *call;
        if (!reply.isError())
            return;

        // Every reason the call can fail lands here and gets the same
        // treatment: no FileManager1 on this desktop (ServiceUnknown), a file
        // manager that refused, an activation that genuinely failed. None of
        // them are worth distinguishing for the user, and all of them are
        // better answered with their folder than with nothing.
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
    });

    return true;
}
