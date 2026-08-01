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

    // The path of the .md file currently backing `id`, or an empty string if
    // there isn't one on disk right now — an unknown id, or an id whose file
    // has been removed by something other than this app in the window before
    // the watcher notices. Callers are expected to treat "" as "there is
    // nothing to show the user," not as an error.
    //
    // Deliberately the only way out of this class for the id->filename
    // mapping, which is otherwise private on purpose (see the class comment:
    // the two are decoupled precisely so a note's id can stay stable while its
    // filename tracks its first line). It exists for the one job that
    // genuinely needs the real file rather than the id — showing the user
    // where their note lives on disk (the context menu's "Reveal in File
    // Manager") — and re-derives from m_idToFileName on every call rather than
    // handing back something cacheable, because a rename lands on any
    // debounced save.
    Q_INVOKABLE QString noteFilePath(const QString &id) const;
    // `content` is written to the new note's file synchronously, unlike every
    // other write in this class — see the definition for why duplicating a
    // note can't go through the debounced saveNoteContent() path.
    Q_INVOKABLE QString createNote(const QString &content = QString());
    Q_INVOKABLE void deleteNote(const QString &id);
    Q_INVOKABLE void saveNoteContent(const QString &id, const QString &markdownText);

    // Whether this note has edits queued but not yet written to disk — i.e. a
    // debounced saveNoteContent() is still pending. Lets a caller reloading a
    // note after an external file change (see noteContentChangedExternally)
    // decline to clobber the user's own in-flight edits: with unsaved changes
    // present, the pending local save is the one that should win.
    Q_INVOKABLE bool hasUnsavedChanges(const QString &id) const;

    // Copies an image (a local path or a file:// URL, e.g. from a drag-and-drop
    // or a file dialog) into an "img/" subfolder of the notes folder and returns
    // the note-relative path to reference it by ("img/<name>"), or an empty
    // string on failure. The name is kept human-readable (the source's own
    // basename), with a " (2)"/" (3)"-style suffix only on collision so two
    // different images never clobber each other. img/ is a single folder shared
    // by every note — there are no per-note folders here (notes are flat .md
    // files) — which is why removal (see cleanupRemovedImages) has to check that
    // no OTHER note still references an image before deleting it.
    Q_INVOKABLE QString importImage(const QString &sourceUrl);

signals:
    void notesFolderChanged();
    void noteIdsChanged();
    void noteContentChangedExternally(const QString &id);
    // Fires once a debounced save has landed on disk AND the post-write image
    // GC has run, i.e. the note's stored state is both durable and consistent.
    // Anything that mirrors note content somewhere else (an export, an upload)
    // can hang off this instead of running its own timer — which is the point:
    // there was previously no post-save hook at all, so the only way to notice
    // a save was to poll, and this app does not poll (see CLAUDE.md).
    //
    // Deliberately NOT emitted by createNote(): that writes through its own
    // synchronous path rather than writeNoteToDisk(), and a brand-new note is
    // already announced by noteIdsChanged(). "Saved" here means "an existing
    // note's content changed and has been persisted".
    void noteSaved(const QString &id);

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

    // The set of note-relative image paths ("img/<name>") a note's markdown
    // references — only images under our own img/ folder, never external URLs
    // (which we don't manage and must never delete). Static: it's a pure
    // function of the text.
    static QSet<QString> imageReferencesIn(const QString &content);
    // Whether ANY note currently on disk references `relativePath`. Used before
    // deleting an image, because img/ is shared across all notes (see
    // importImage): another note may still be using an image this one dropped.
    bool isImageReferencedByAnyNote(const QString &relativePath) const;
    // Deletes image files that `before` referenced but `after` no longer does,
    // provided no other note references them either. Only ever touches files in
    // our own img/ folder. Called from every write and from deleteNote.
    void cleanupRemovedImages(const QString &before, const QString &after);

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
