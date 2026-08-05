#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QStringList>

#include "Settings.h"

// Bridges Settings' persisted "which notes are open" session list (id +
// geometry) to QML. Applies incremental beginInsertRows/beginRemoveRows
// (not a full model reset) so opening/closing one note never touches the
// other open notes' windows/state.
class OpenNotesModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        NoteIdRole = Qt::UserRole + 1,
        XRole,
        YRole,
        WidthRole,
        HeightRole,
        ScreenNameRole,
        AlwaysOnTopRole,
    };

    explicit OpenNotesModel(Settings *settings, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void openNote(const QString &noteId);

    // Opens a note with its spawn position already seeded, for callers that
    // know where the new window belongs (duplicating a note, which offsets the
    // copy from its source). Position has to be in Settings *before* the
    // note's window delegate is constructed: that delegate writes a KWin
    // "Apply Initially" rule from this x/y and must do so before its surface
    // is ever mapped, so there is no post-spawn opportunity to place it
    // without the window visibly jumping. Hence a seeded open rather than an
    // openNote() followed by a move.
    Q_INVOKABLE void openNoteAt(const QString &noteId, int x, int y);

    // Where the NEXT note opened by openNote() should spawn — a one-shot, not a
    // setting. Exists because openNoteAt() alone cannot do the job in the real
    // app, which is a race worth spelling out because it is invisible in tests:
    //
    // NoteStore::createNote() ends with refreshNoteIds(), which emits
    // noteIdsChanged SYNCHRONOUSLY. Main.qml reconciles on that signal, also
    // synchronously, calling openNote() for the id that just appeared — so by
    // the time createNote() returns to its caller, the note is ALREADY open at
    // the default position, its window delegate already constructed, and its
    // KWin spawn-position rule already written and evaluated. A subsequent
    // openNoteAt() is then a no-op (the id is open), and even if it weren't,
    // "Apply Initially" has already fired: a mapped Wayland toplevel cannot be
    // placed by assigning x/y afterwards.
    //
    // Seeding BEFORE createNote() is what closes that gap — the reconcile's own
    // openNote() picks the position up, so the very first thing that opens the
    // note opens it in the right place.
    //
    // The QML test harness has no Main.qml and therefore no reconciliation, so
    // there the caller's own openNoteAt() is what positions the note. Both
    // paths have to work, which is why callers do both.
    //
    // If two notes appear between the seed and the next openNote(), the first
    // one opened takes it. That only happens when an external tool drops a .md
    // into the folder in the same instant a note is created here.
    Q_INVOKABLE void seedNextSpawnPosition(int x, int y);

    // Drops an unconsumed seed. Callers must do this after the creation they
    // seeded for, because a create that FAILS never emits noteIdsChanged and so
    // never triggers the reconcile that would have consumed it — leaving it
    // armed to displace some unrelated note opened later (a TickTick pull, say).
    Q_INVOKABLE void clearNextSpawnPosition();

    Q_INVOKABLE void closeNote(const QString &noteId);
    Q_INVOKABLE bool isOpen(const QString &noteId) const;

    // All currently-open note ids, in model order. Used to reconcile against
    // NoteStore::noteIds() (every .md file on disk must be open, and no
    // open note may outlive its file) — see Main.qml's reconciliation logic.
    Q_INVOKABLE QStringList openNoteIds() const;

    // Persists a note's live size after a resize settles, without disturbing
    // its position, screen, or any other open note's entry. Deliberately
    // takes no x/y: position is PositionEventService's exclusive domain (see
    // its own docs) because it's the only thing that can observe a real,
    // final position after a compositor-driven Wayland drag. A resize
    // handler's own window x/y are never live-synced back from such a
    // drag — confirmed live, this used to be a single updateGeometry(x, y,
    // width, height, screenName) called from NoteWindow's resize handler,
    // and every resize after a drag silently reverted the note's saved
    // position back to wherever it was when the window was created, using
    // those stale x/y, even though the note stayed visually put on screen.
    Q_INVOKABLE void updateSize(const QString &noteId, int width, int height);

    // Toggles the pin (req. 5). Preserves the note's current geometry —
    // only alwaysOnTop changes.
    Q_INVOKABLE void setAlwaysOnTop(const QString &noteId, bool alwaysOnTop);

private slots:
    void syncFromSettings();

private:
    int indexOf(const QString &noteId) const;

    Settings *m_settings;
    QList<Settings::NoteSessionState> m_notes;

    // See seedNextSpawnPosition(). Armed by a caller about to create a note,
    // consumed by whichever openNote() runs next.
    bool m_hasSpawnSeed = false;
    int m_spawnSeedX = 0;
    int m_spawnSeedY = 0;
};
