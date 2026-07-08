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
};
