#pragma once

#include "core/NoteContentProvider.h"

#include <QString>
#include <QStringList>

// The TickTick-backed half of the note world.
//
// TickTick owns these notes' content. What lives on this machine is a CACHE,
// never a peer copy — that distinction is the whole design. Two authoritative
// copies would have to be merged, and merging needs to know which side changed
// more recently, which TickTick cannot answer: its task objects carry no
// modification timestamp at all. With one authority there is nothing to merge:
// a pull overwrites the cache, a local edit writes the cache and queues a push.
//
// The cache is also what makes these notes work at all, for a reason that has
// nothing to do with sync: note content is loaded synchronously on the GUI
// thread during window construction (see NoteContentProvider). A note that
// could only be fetched over the network could not be opened by this app
// without an async load path, a loading state, an error state and a read-only
// editor — none of which exist. Reading a local cache file keeps the existing
// path working untouched, and as a side effect the notes stay readable offline
// and survive a kill -9 with the same guarantee local notes have.
//
// Cache files live under QStandardPaths::CacheLocation rather than in the notes
// folder, deliberately: NoteStore enumerates that folder, so a cache file there
// would be picked up as a second, local copy of the same note.
class TickTickNotes : public NoteContentProvider
{
    Q_OBJECT

public:
    explicit TickTickNotes(QObject *parent = nullptr);

    // Test seam, mirroring Settings' and TickTickTokenStore's explicit-path
    // constructors.
    explicit TickTickNotes(const QString &cacheDirPath, QObject *parent = nullptr);

    QString cacheDirPath() const;

    // The one local uuid every open TickTick task now lives under (feature
    // request: one aggregated checkbox-list note instead of one note per
    // task). A plain constant rather than a per-task mint: there is nothing
    // left to disambiguate between tasks at the note-identity level once
    // there is only one note, and "tasks" already passes isSafeLocalUuid (no
    // '/', no '\', no leading '.').
    static QString taskListUuid();

    bool ownsNoteId(const QString &id) const override;
    QStringList noteIds() const override;
    QString loadNoteContent(const QString &id) const override;

    // Accepts the edit by writing it to the cache immediately and reporting it
    // for upload. Writing first is the point: the cache write is atomic and
    // local, so an edit survives a crash or a dead network exactly as well as a
    // local note's would, and the upload becomes a delivery problem rather than
    // a data-integrity one.
    bool saveNoteContent(const QString &id, const QString &content) override;

    // deleteNote is deliberately still declined. Deleting a note here would
    // mean deleting the user's TickTick task, which is a bigger and less
    // reversible action than the close button implies — better to leave it
    // unsupported than to guess.

    // --- cache maintenance, driven by the sync layer ---

    // Writes (or replaces) a cached note and announces it. Returns the note id.
    // localUuid is this app's own stable key for the note; the remote task id
    // is mapped to it elsewhere rather than being used as the id, so the note
    // survives the task being recreated remotely.
    QString writeCachedNote(const QString &localUuid, const QString &content);

    // True when the cache holds this note. Distinguishes "no such note" from
    // "a note that is genuinely empty", which a bare content read cannot.
    bool hasCachedNote(const QString &localUuid) const;

    // One-time migration off the old one-note-per-task cache (feature
    // request): deletes every cached "*.md" file whose local uuid isn't
    // `localUuidToKeep` (in practice, taskListUuid()). Every cached note
    // under the old scheme belongs to a task this app no longer tracks by
    // its own note id — left in place they would sit alongside the new
    // aggregated note as orphans that no pull can ever reconcile away, the
    // same failure mode selectProject() already guards against when the
    // user switches TickTick lists. The caller (TickTickSync) is
    // responsible for only calling this once per install — see
    // TickTickPrefs::migratedToTaskList.
    void removeAllCachedNotesExcept(const QString &localUuidToKeep);

signals:
    // A local edit has been cached and now needs uploading. Distinct from
    // noteContentChangedExternally, which means the opposite direction and would
    // make the editor reload the text the user is in the middle of typing.
    void noteEditedLocally(const QString &localUuid, const QString &content);

private:
    QString filePathForLocalUuid(const QString &localUuid) const;
    bool ensureCacheDirExists() const;
    bool writeCacheFile(const QString &localUuid, const QString &content);

    QString m_cacheDirPath;
};
