#include "NoteContentProvider.h"

NoteContentProvider::NoteContentProvider(QObject *parent)
    : QObject(parent)
{
}

NoteContentProvider::~NoteContentProvider() = default;

bool NoteContentProvider::saveNoteContent(const QString &, const QString &)
{
    // Declining by default matters: NoteStore must not fall back to its local
    // write path for a note it does not own, or a read-only provider's notes
    // would quietly sprout .md files in the notes folder.
    return false;
}

bool NoteContentProvider::deleteNote(const QString &)
{
    return false;
}
