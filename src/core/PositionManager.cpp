#include "PositionManager.h"
#include "KWinRuleWriter.h"

#include <QThread>

PositionManager::PositionManager(QObject *parent)
    : QObject(parent)
{
}

void PositionManager::applyPosition(const QString &noteId, const QString &windowTitle, int x, int y,
                                      bool alwaysOnTop)
{
    // x, y are passed through verbatim as the true global (virtual-desktop)
    // coordinate — see KWinRuleSpec's comment for why no per-monitor
    // adjustment or "screen=" rule belongs here.
    KWinRuleSpec spec;
    spec.windowTitle = windowTitle;
    spec.x = x;
    spec.y = y;
    spec.alwaysOnTop = alwaysOnTop;

    KWinRuleApplier::applyRule(noteId, spec);
}

void PositionManager::removePosition(const QString &noteId)
{
    // Run off the main/UI thread: removeRule() shells out to ~15
    // kwriteconfig6/kreadconfig6 invocations plus a qdbus6 reconfigure call
    // and its trailing 200ms settle sleep (see KWinRuleApplier::reconfigure)
    // — confirmed live to add up to the better part of a second. Called
    // synchronously from Component.onDestruction during note deletion, that
    // blocked the whole app for that long before the now-closed note's
    // window actually disappeared. Nothing here needs to finish before the
    // caller (or the window's own teardown) proceeds — unlike applyPosition
    // (whose rule genuinely must be written before the window is first
    // shown), a stale/late-removed rule for an already-closed note has no
    // user-visible effect other than a harmless leftover kwinrulesrc group
    // until this finishes. QThread::create's callable runs KWinRuleApplier's
    // static, purely-local-QProcess method, which needs no event loop of its
    // own (waitForFinished does its own local wait) — safe off the main
    // thread.
    QThread *worker = QThread::create([noteId]() { KWinRuleApplier::removeRule(noteId); });
    QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

void PositionManager::moveWindow(const QString &windowTitle, int x, int y)
{
    QThread *worker = QThread::create([windowTitle, x, y]() { KWinRuleApplier::moveWindow(windowTitle, x, y); });
    QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}

bool PositionManager::suppressCleanupOnQuit() const
{
    return m_suppressCleanupOnQuit;
}

void PositionManager::setSuppressCleanupOnQuit(bool suppress)
{
    m_suppressCleanupOnQuit = suppress;
}
