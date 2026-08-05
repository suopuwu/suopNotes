#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

// A source of notes that is not the notes folder.
//
// The app has exactly one built-in kind of note — a .md file in the notes
// folder — and NoteStore owns it. A provider adds a second kind, owned by
// something else entirely (a cloud service, in the first case), while keeping
// every consumer of notes unaware that there is more than one kind.
//
// That works because a note is only ever identified by an opaque id string:
// window geometry, per-note appearance overrides and KWin rules all key off the
// id and never look at where the note lives. So a provider's notes flow through
// the whole app unchanged as long as their ids can be told apart from local
// ones — which is the provider's own business, via ownsNoteId().
//
// Registered with NoteStore::registerProvider(). NoteStore then routes by id,
// so QML keeps calling noteStore.noteIds() / loadNoteContent() exactly as
// before and nothing in the UI has to learn what a provider is.
//
// IMPORTANT for implementors: loadNoteContent() is called synchronously during
// note-window construction and must return immediately. A provider backed by
// anything slower than a local file has to serve from a local cache rather than
// fetching on demand — there is no asynchronous load path in the app, and
// adding one would mean a loading state, an error state and a read-only editor
// mode, none of which exist.
class NoteContentProvider : public QObject
{
    Q_OBJECT

public:
    explicit NoteContentProvider(QObject *parent = nullptr);
    ~NoteContentProvider() override;

    // Must be cheap and total: NoteStore asks this for every id it routes, and
    // an id no provider claims is treated as a local note.
    virtual bool ownsNoteId(const QString &id) const = 0;

    virtual QStringList noteIds() const = 0;

    // Called on the GUI thread during window construction — see the class note.
    virtual QString loadNoteContent(const QString &id) const = 0;

    // Return false to decline; NoteStore then does nothing at all rather than
    // falling back to writing a local file, which would leave a stray .md named
    // after a note the provider owns. The default is to decline, so a
    // read-only provider is the safe thing to write by omission.
    virtual bool saveNoteContent(const QString &id, const QString &content);
    virtual bool deleteNote(const QString &id);

signals:
    // The provider's set of notes changed. NoteStore re-broadcasts this as its
    // own noteIdsChanged(), which is what drives reconcile.
    void noteIdsChanged();

    // This note's content changed underneath the app (a remote edit landing in
    // the cache). Re-broadcast as NoteStore::noteContentChangedExternally().
    void noteContentChangedExternally(const QString &id);
};
