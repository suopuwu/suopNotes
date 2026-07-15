#include "OpenNotesModel.h"

#include <algorithm>

OpenNotesModel::OpenNotesModel(Settings *settings, QObject *parent)
    : QAbstractListModel(parent)
    , m_settings(settings)
    , m_notes(settings->openNotes())
{
    connect(m_settings, &Settings::openNotesChanged, this, &OpenNotesModel::syncFromSettings);
}

int OpenNotesModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_notes.size();
}

QVariant OpenNotesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_notes.size())
        return QVariant();

    const Settings::NoteSessionState &state = m_notes.at(index.row());
    switch (role) {
    case NoteIdRole:
        return state.noteId;
    case XRole:
        return state.x;
    case YRole:
        return state.y;
    case WidthRole:
        return state.width;
    case HeightRole:
        return state.height;
    case ScreenNameRole:
        return state.screenName;
    case AlwaysOnTopRole:
        return state.alwaysOnTop;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> OpenNotesModel::roleNames() const
{
    return {
        { NoteIdRole, "noteId" },
        { XRole, "noteX" },
        { YRole, "noteY" },
        { WidthRole, "noteWidth" },
        { HeightRole, "noteHeight" },
        { ScreenNameRole, "screenName" },
        { AlwaysOnTopRole, "alwaysOnTop" },
    };
}

void OpenNotesModel::openNote(const QString &noteId)
{
    if (isOpen(noteId))
        return;

    Settings::NoteSessionState state;
    state.noteId = noteId;
    m_settings->upsertNoteState(state);
}

void OpenNotesModel::openNoteAt(const QString &noteId, int x, int y)
{
    if (isOpen(noteId))
        return;

    Settings::NoteSessionState state;
    state.noteId = noteId;
    state.x = x;
    state.y = y;
    m_settings->upsertNoteState(state);
}

void OpenNotesModel::closeNote(const QString &noteId)
{
    m_settings->removeNoteState(noteId);
}

bool OpenNotesModel::isOpen(const QString &noteId) const
{
    return indexOf(noteId) >= 0;
}

void OpenNotesModel::updateSize(const QString &noteId, int width, int height)
{
    // Start from the note's existing cached state (if any) rather than a
    // fresh default-constructed one, so fields this call doesn't touch
    // (x/y/screenName/alwaysOnTop) aren't silently reset to their defaults
    // on every resize.
    const int row = indexOf(noteId);
    Settings::NoteSessionState state = row >= 0 ? m_notes.at(row) : Settings::NoteSessionState();
    state.noteId = noteId;
    state.width = width;
    state.height = height;
    // upsertNoteState persists and emits Settings::openNotesChanged, which
    // syncFromSettings picks up to refresh our own cache — no local mutation
    // needed here, keeping Settings as the single source of truth.
    m_settings->upsertNoteState(state);
}

void OpenNotesModel::setAlwaysOnTop(const QString &noteId, bool alwaysOnTop)
{
    const int row = indexOf(noteId);
    Settings::NoteSessionState state = row >= 0 ? m_notes.at(row) : Settings::NoteSessionState();
    state.noteId = noteId;
    state.alwaysOnTop = alwaysOnTop;
    m_settings->upsertNoteState(state);
}

QStringList OpenNotesModel::openNoteIds() const
{
    QStringList ids;
    ids.reserve(m_notes.size());
    for (const Settings::NoteSessionState &state : m_notes)
        ids << state.noteId;
    return ids;
}

int OpenNotesModel::indexOf(const QString &noteId) const
{
    for (int i = 0; i < m_notes.size(); ++i) {
        if (m_notes.at(i).noteId == noteId)
            return i;
    }
    return -1;
}

void OpenNotesModel::syncFromSettings()
{
    const QList<Settings::NoteSessionState> newNotes = m_settings->openNotes();

    // Remove ids no longer present, back-to-front so earlier indices stay valid.
    for (int i = m_notes.size() - 1; i >= 0; --i) {
        const QString &id = m_notes.at(i).noteId;
        const bool stillPresent = std::any_of(newNotes.cbegin(), newNotes.cend(), [&id](const auto &s) {
            return s.noteId == id;
        });
        if (!stillPresent) {
            beginRemoveRows(QModelIndex(), i, i);
            m_notes.removeAt(i);
            endRemoveRows();
        }
    }

    // Add ids that are new, and refresh geometry for ids that already existed
    // (e.g. updateSize/upsertNoteState changing an existing note in place).
    for (const Settings::NoteSessionState &state : std::as_const(newNotes)) {
        const int row = indexOf(state.noteId);
        if (row < 0) {
            const int newRow = m_notes.size();
            beginInsertRows(QModelIndex(), newRow, newRow);
            m_notes.append(state);
            endInsertRows();
        } else if (!(m_notes.at(row).x == state.x && m_notes.at(row).y == state.y
                     && m_notes.at(row).width == state.width && m_notes.at(row).height == state.height
                     && m_notes.at(row).screenName == state.screenName
                     && m_notes.at(row).alwaysOnTop == state.alwaysOnTop)) {
            m_notes[row] = state;
            const QModelIndex modelIndex = index(row);
            emit dataChanged(modelIndex, modelIndex);
        }
    }
}
