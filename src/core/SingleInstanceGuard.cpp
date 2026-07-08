#include "SingleInstanceGuard.h"

#include <QEventLoop>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>

namespace {

// A prior instance answering at all is itself the only signal this class
// needs (only suopNotes ever connects to this key) — the message's actual
// bytes are never branched on, only its arrival marks "someone is asking me
// to step aside."
const QByteArray &replaceMessage()
{
    static const QByteArray message = QByteArrayLiteral("REPLACE");
    return message;
}

// If nothing is listening at `key`, a Unix-domain connect() fails
// (ECONNREFUSED) essentially instantly — this bound exists only to survive
// a stale/unresponsive socket path, not because a live "no instance"
// negative response is ever actually slow.
constexpr int kConnectTimeoutMs = 200;

// Bounds how long a freshly-launched instance will wait for the prior one
// to confirm it has stepped aside, before giving up on the handshake and
// just becoming the server anyway (see takeOwnership()) — matches this
// codebase's "never an unbounded wait" rule (KWinRuleWriter.cpp's
// runProcess uses the same discipline for external processes).
constexpr int kReplaceTimeoutMs = 3000;

}

SingleInstanceGuard::SingleInstanceGuard(const QString &key, QObject *parent)
    : QObject(parent)
    , m_key(key)
{
}

SingleInstanceGuard::~SingleInstanceGuard()
{
    // Confirmed live (a real, reproducible bug during development, not
    // theoretical): if this instance already ceded ownership via
    // handleNewConnection() — because a later instance replaced it — its
    // own shutdown can still take a real, noticeable amount of wall-clock
    // time afterward (this app's aboutToQuit handler does bounded but
    // non-instant KWin scripting round-trips per open note). By the time
    // this destructor finally runs, the REPLACING instance has typically
    // already created its own fresh server at the same key.
    // QLocalServer::removeServer() is not scoped to "remove only if I'm the
    // current owner" — it unconditionally deletes whatever socket file
    // exists at that path — so calling it here unconditionally would delete
    // the NEW instance's live, in-use socket out from under it, leaving a
    // stale reference no one can connect to. Only clean up if this instance
    // is exiting while still the legitimate owner.
    if (m_ceded || !m_server)
        return;
    m_server->close();
    QLocalServer::removeServer(m_key);
}

bool SingleInstanceGuard::takeOwnership()
{
    QLocalSocket client;
    client.connectToServer(m_key);
    if (client.waitForConnected(kConnectTimeoutMs)) {
        client.write(replaceMessage());
        client.flush();

        // Wait for the OTHER process to actually close this connection —
        // it does so from its own handleNewConnection() as part of ceding
        // ownership, which happens synchronously with emitting
        // replaceRequested() and does not depend on that process's
        // qApp->quit() having run yet. Poll-free: whichever of
        // "disconnected" or the timeout fires first ends the wait.
        if (client.state() != QLocalSocket::UnconnectedState) {
            QEventLoop loop;
            QTimer timeoutTimer;
            timeoutTimer.setSingleShot(true);
            connect(&client, &QLocalSocket::disconnected, &loop, &QEventLoop::quit);
            connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
            timeoutTimer.start(kReplaceTimeoutMs);
            loop.exec();
        }
    }
    // client falls out of scope here either way, closing its end of the
    // connection (if any) on the way out.

    // Whether or not a prior instance was found: clear any stale socket
    // file first (Qt's documented pattern for a name a crashed-without-
    // cleanup process left behind — see class doc comment), then become
    // the listener ourselves.
    QLocalServer::removeServer(m_key);

    m_server = new QLocalServer(this);
    connect(m_server, &QLocalServer::newConnection, this, &SingleInstanceGuard::handleNewConnection);
    return m_server->listen(m_key);
}

void SingleInstanceGuard::handleNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QLocalSocket *client = m_server->nextPendingConnection();
        if (!client)
            continue;

        // Best-effort: the connecting side always writes its message
        // immediately (see takeOwnership()), but this side's own shutdown
        // below doesn't actually depend on having read it — arrival of the
        // connection is already the whole signal.
        if (client->state() == QLocalSocket::ConnectedState)
            client->waitForReadyRead(kConnectTimeoutMs);

        client->disconnectFromServer();
        client->deleteLater();
    }

    // Cede ownership right now, not whenever main.cpp's qApp->quit() (fired
    // from a replaceRequested() slot) eventually tears this object down —
    // the new instance's takeOwnership() is actively waiting on this
    // connection closing, so this must happen synchronously, before this
    // process's own event loop necessarily unwinds. m_ceded (checked by the
    // destructor) is set for the same reason: this process's own shutdown
    // may still take a while after this point, and must not delete
    // whatever the replacing instance sets up in the meantime.
    m_server->close();
    m_ceded = true;
    emit replaceRequested();
}
