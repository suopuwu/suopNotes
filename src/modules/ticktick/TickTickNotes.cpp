#include "TickTickNotes.h"

#include "TickTickModule.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
const QString kCacheSubdir = QStringLiteral("ticktick-notes");
const QString kExtension = QStringLiteral(".md");
const QString kTaskListUuid = QStringLiteral("tasks");

QString defaultCacheDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return QDir(base).filePath(kCacheSubdir);
}

// A local uuid is this app's own minted key, so it is already filename-safe.
// Checked rather than assumed because the value reaches here from a config file
// that a user can hand-edit, and a stray separator would let a note id escape
// the cache directory.
bool isSafeLocalUuid(const QString &localUuid)
{
    if (localUuid.isEmpty())
        return false;
    if (localUuid.contains(QLatin1Char('/')) || localUuid.contains(QLatin1Char('\\')))
        return false;
    if (localUuid.startsWith(QLatin1Char('.')))
        return false;
    return true;
}
}

TickTickNotes::TickTickNotes(QObject *parent)
    : TickTickNotes(defaultCacheDir(), parent)
{
}

TickTickNotes::TickTickNotes(const QString &cacheDirPath, QObject *parent)
    : NoteContentProvider(parent)
    , m_cacheDirPath(cacheDirPath)
{
}

QString TickTickNotes::cacheDirPath() const
{
    return m_cacheDirPath;
}

QString TickTickNotes::taskListUuid()
{
    return kTaskListUuid;
}

bool TickTickNotes::ownsNoteId(const QString &id) const
{
    return TickTickModule::ownsNoteId(id);
}

QStringList TickTickNotes::noteIds() const
{
    // Exactly one id, and only once the cache actually holds it — a fresh
    // install (or one that has authorized but not pulled yet) has no task
    // note to open, same as before.
    if (!hasCachedNote(kTaskListUuid))
        return {};
    return {TickTickModule::makeNoteId(kTaskListUuid)};
}

QString TickTickNotes::loadNoteContent(const QString &id) const
{
    const QString localUuid = TickTickModule::localUuidFor(id);
    if (!isSafeLocalUuid(localUuid))
        return QString();

    // Synchronous by contract — this runs during note-window construction. It
    // is a plain local read for exactly that reason; see the class comment.
    QFile file(filePathForLocalUuid(localUuid));
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(file.readAll());
}

bool TickTickNotes::writeCacheFile(const QString &localUuid, const QString &content)
{
    if (!isSafeLocalUuid(localUuid) || !ensureCacheDirExists())
        return false;

    // Atomic, same as the local note path: a half-written cache file would be
    // indistinguishable from a note whose content really is truncated.
    QSaveFile file(filePathForLocalUuid(localUuid));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(content.toUtf8());
    return file.commit();
}

bool TickTickNotes::saveNoteContent(const QString &id, const QString &content)
{
    const QString localUuid = TickTickModule::localUuidFor(id);
    if (localUuid.isEmpty())
        return false;

    // Deliberately silent: the editor already holds this text. Announcing it as
    // a content change would make the open window reload mid-keystroke.
    if (!writeCacheFile(localUuid, content))
        return false;

    emit noteEditedLocally(localUuid, content);
    return true;
}

QString TickTickNotes::writeCachedNote(const QString &localUuid, const QString &content)
{
    if (!isSafeLocalUuid(localUuid))
        return QString();

    const bool isNew = !hasCachedNote(localUuid);
    const QString id = TickTickModule::makeNoteId(localUuid);

    // A pull that found nothing changed writes the byte-identical text it
    // already had, and announcing THAT as an external change is a real,
    // reported defect rather than a wasted signal: the open note reacts by
    // reloading itself through setMarkdown(), which rebuilds the whole
    // QTextDocument — so every markdown style and every spell-check underline
    // in the note visibly blinks off and back on ("a pull will cause a rerender
    // of the note, meaning styling flashes on and off, spellcheck does as
    // well"). Since a scheduled pull happens hourly and a pull also fires every
    // time the note regains focus, that blink was most of what a pull ever did.
    //
    // Checked before the write, against the file, rather than against anything
    // remembered in memory: the file is what loadNoteContent() will serve, so
    // it is the only comparison that can promise the reload would be a no-op.
    if (!isNew && loadNoteContent(id) == content)
        return id;

    if (!writeCacheFile(localUuid, content))
        return QString();

    // A new note changes the set of notes; an updated one changes only its
    // content. Reported separately because they drive different things —
    // reconcile opens windows, the content signal reloads an open one.
    if (isNew)
        emit noteIdsChanged();
    else
        emit noteContentChangedExternally(id);

    return id;
}

bool TickTickNotes::hasCachedNote(const QString &localUuid) const
{
    if (!isSafeLocalUuid(localUuid))
        return false;
    return QFileInfo::exists(filePathForLocalUuid(localUuid));
}

void TickTickNotes::removeAllCachedNotesExcept(const QString &localUuidToKeep)
{
    QDir dir(m_cacheDirPath);
    if (!dir.exists())
        return;

    const QStringList files =
        dir.entryList({QStringLiteral("*") + kExtension}, QDir::Files, QDir::Name);

    bool removedAny = false;
    for (const QString &file : files) {
        const QString localUuid = QFileInfo(file).completeBaseName();
        if (localUuid == localUuidToKeep)
            continue;
        if (QFile::remove(dir.filePath(file)))
            removedAny = true;
    }

    // Only one signal for the whole sweep, not one per file removed — this
    // runs once at startup, before anything has had a chance to react to a
    // note appearing, so there is no reconcile in progress that needs to see
    // each removal separately.
    if (removedAny)
        emit noteIdsChanged();
}

QString TickTickNotes::filePathForLocalUuid(const QString &localUuid) const
{
    return QDir(m_cacheDirPath).filePath(localUuid + kExtension);
}

bool TickTickNotes::ensureCacheDirExists() const
{
    QDir dir(m_cacheDirPath);
    return dir.exists() || dir.mkpath(QStringLiteral("."));
}
