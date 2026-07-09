#pragma once

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

// NoteStore manages a folder of plain .md files on disk, one file per note.
//
// Responsibilities:
//  - Enumerate the .md files in a user-supplied "notesFolder", exposing each
//    as a stable id (a UUID, unrelated to the file's on-disk name — see
//    below).
//  - Provide instant-feeling saves that are actually debounced (~300ms after
//    the last edit for a given note) and written atomically via QSaveFile.
//  - Detect changes made to the folder/files by something other than this
//    NoteStore instance (external editor, sync tool, etc.) and notify via
//    noteContentChangedExternally()/noteIdsChanged(), without spuriously
//    reacting to its own debounced writes.
//  - Keep each note's on-disk filename human-readable: derived from its
//    first line via NoteFilename::deriveBaseName(), re-derived and the
//    file renamed on every debounced save. This is
//    deliberately decoupled from the note's id: the id is what the rest of
//    the app (window titles, KWin rules, Settings session/override state)
//    keys everything on, and none of that may change just because the user
//    edited a note's first line. The id<->filename mapping is persisted in
//    a small JSON index file (see indexFilePath()) alongside the notes; if
//    that index is missing, unreadable, or out of date, NoteStore falls
//    back to minting fresh ids for any unmapped .md file rather than
//    failing — the note content itself is never at risk, only continuity
//    of window/rule state tied to a stale id.
//
// NoteStore deliberately knows nothing about QStandardPaths or app settings;
// it just operates on whatever folder path it is given.
class NoteStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString notesFolder READ notesFolder WRITE setNotesFolder NOTIFY notesFolderChanged)

public:
    explicit NoteStore(QObject *parent = nullptr);

    QString notesFolder() const;
    void setNotesFolder(const QString &path);

    Q_INVOKABLE QStringList noteIds() const;
    Q_INVOKABLE QString loadNoteContent(const QString &id) const;
    Q_INVOKABLE QString createNote();
    Q_INVOKABLE void deleteNote(const QString &id);
    Q_INVOKABLE void saveNoteContent(const QString &id, const QString &markdownText);

signals:
    void notesFolderChanged();
    void noteIdsChanged();
    void noteContentChangedExternally(const QString &id);

private slots:
    void onDirectoryChanged(const QString &path);
    void onFileChanged(const QString &path);
    void flushPendingSave(const QString &id);

private:
    QString filePathForId(const QString &id) const;
    // Reverse lookup for onFileChanged(), which only has a path/basename to
    // go on. Empty if no tracked id currently maps to that basename.
    QString idForFileName(const QString &baseName) const;
    void refreshNoteIds(bool emitIfChanged);
    void watchAllNoteFiles();
    void writeNoteToDisk(const QString &id, const QString &content);

    // id<->filename index (see class comment above).
    QString indexFilePath() const;
    void loadIndex();
    void saveIndex() const;
    // Picks a filesystem-safe name for `id` that isn't already used by any
    // *other* id, appending " (2)", " (3)", ... on collision.
    QString uniqueFileName(const QString &desiredBase, const QString &excludeId) const;

    QString m_notesFolder;
    QStringList m_noteIds;
    QFileSystemWatcher m_watcher;

    // id -> current on-disk base name (no ".md"). The authoritative,
    // in-memory source of truth for "what file backs this id right now";
    // indexFilePath() is just this map's on-disk persistence.
    QHash<QString, QString> m_idToFileName;

    // Pending debounced writes: id -> latest content not yet flushed to disk.
    QHash<QString, QString> m_pendingContent;
    // Per-id debounce timers (single-shot, restarted on every save call).
    QHash<QString, QTimer *> m_pendingTimers;

    // Tracks the content we ourselves last wrote for each id, so that the
    // QFileSystemWatcher signal produced by our own write doesn't get
    // misreported as an external change.
    QHash<QString, QString> m_lastWrittenContent;
};
