#pragma once

#include <QObject>
#include <QString>

class QLocalServer;

// Enforces "launching a second time replaces the first instance" rather
// than the more common "focus the existing instance and do nothing new."
// This app already persists all session/appearance state to disk on every
// change (Settings) and captures live window positions via
// PositionEventService's drag-settle callback, so a freshly-launched
// process picking up where a just-quit one left off needs no explicit
// state hand-off at all — the only job left here is making sure the OLD
// process actually exits before/as the new one takes over, so there is
// never more than one process holding the notes folder / KWin rules /
// D-Bus service names at once.
//
// Real key this app uses in main.cpp: "suopNotes-singleinstance". Tests
// must never use that key (see test_single_instance_guard.cpp) — a test
// run sharing it with a real running suopNotes instance would kill the
// user's actual session.
class SingleInstanceGuard : public QObject
{
    Q_OBJECT
public:
    explicit SingleInstanceGuard(const QString &key, QObject *parent = nullptr);
    ~SingleInstanceGuard() override;

    // Runs the full replace protocol synchronously, bounded throughout (no
    // step can hang forever — same discipline as KWinRuleWriter.cpp's
    // runProcess): tries connecting to an existing instance at `key` first;
    // if one answers, asks it to quit and waits (bounded) for its server to
    // actually go away before proceeding. Either way, finishes by starting
    // this process's own QLocalServer listening on `key` (after
    // QLocalServer::removeServer(key), Qt's documented way to clear a stale
    // socket file left by a prior process that crashed without a clean
    // shutdown) and returns true once this process is confirmed to be the
    // sole listener.
    bool takeOwnership();

signals:
    // Emitted when a later-launched instance asks THIS (currently running)
    // process to shut down. main.cpp is expected to connect this straight
    // to qApp->quit() — this class only detects the request, it never quits
    // the app itself (this class has no idea whether that's safe to do
    // synchronously from inside the slot that emits it).
    void replaceRequested();

private slots:
    void handleNewConnection();

private:
    QString m_key;
    QLocalServer *m_server = nullptr;
    // Set once this instance has ceded ownership to a later-launched one —
    // guards the destructor against deleting a server file this process no
    // longer owns (see the destructor's comment for the real race this
    // prevents).
    bool m_ceded = false;
};
