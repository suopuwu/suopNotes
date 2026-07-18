#include "NoteStore.h"
#include "NoteFilename.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUrl>
#include <QUuid>

namespace
{
constexpr int kSaveDebounceMs = 300;
}

NoteStore::NoteStore(QObject *parent)
    : QObject(parent)
{
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &NoteStore::onDirectoryChanged);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &NoteStore::onFileChanged);
}

QString NoteStore::notesFolder() const
{
    return m_notesFolder;
}

void NoteStore::setNotesFolder(const QString &path)
{
    if (m_notesFolder == path)
        return;

    // Flush any debounced writes for the OLD folder before we let go of it,
    // so an in-flight edit isn't silently lost on a folder switch.
    const QStringList pendingIds = m_pendingTimers.keys();
    for (const QString &id : pendingIds) {
        if (QTimer *timer = m_pendingTimers.take(id)) {
            timer->stop();
            timer->deleteLater();
        }
    }
    for (auto it = m_pendingContent.constBegin(); it != m_pendingContent.constEnd(); ++it)
        writeNoteToDisk(it.key(), it.value());
    m_pendingContent.clear();

    // Stop watching everything from the old folder.
    const QStringList watchedDirs = m_watcher.directories();
    if (!watchedDirs.isEmpty())
        m_watcher.removePaths(watchedDirs);
    const QStringList watchedFiles = m_watcher.files();
    if (!watchedFiles.isEmpty())
        m_watcher.removePaths(watchedFiles);

    m_lastWrittenContent.clear();
    m_idToFileName.clear();

    m_notesFolder = path;
    emit notesFolderChanged();

    if (!path.isEmpty()) {
        // First-run / not-yet-created folder: create it rather than silently
        // failing to enumerate/watch. This is more robust than requiring the
        // caller (Settings/UI) to pre-create it.
        QDir dir(path);
        if (!dir.exists())
            dir.mkpath(QStringLiteral("."));
        m_watcher.addPath(path);
        loadIndex();
    }

    // Force re-enumeration to be treated as a change even if (by coincidence)
    // the new folder happens to contain the same set of ids.
    m_noteIds.clear();
    refreshNoteIds(true);
}

QStringList NoteStore::noteIds() const
{
    return m_noteIds;
}

QString NoteStore::loadNoteContent(const QString &id) const
{
    QFile file(filePathForId(id));
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(file.readAll());
}

QString NoteStore::noteFilePath(const QString &id) const
{
    // The existence check is not belt-and-braces: filePathForId() answers from
    // m_idToFileName alone, which is an in-memory map that can outlive the file
    // it names (an external delete is only noticed when the watcher fires).
    // Handing a caller a path to a file that isn't there would turn "reveal
    // this note" into a file manager opening on nothing.
    const QString path = filePathForId(id);
    if (path.isEmpty() || !QFileInfo::exists(path))
        return QString();
    return QFileInfo(path).absoluteFilePath();
}

// The content write here is deliberately synchronous rather than a
// saveNoteContent() call, which is what a "duplicate note" caller would
// otherwise reach for. saveNoteContent() debounces ~kSaveDebounceMs before it
// touches disk, but opening a note causes its window to call loadNoteContent()
// immediately — so a create-then-save-then-open sequence would read the file
// back well before the debounce ever fired and reliably open an empty
// duplicate. Writing here, on the same call stack as the id being minted,
// means the file is complete by the time createNote() returns.
QString NoteStore::createNote(const QString &content)
{
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QDir().mkpath(m_notesFolder);

    // Empty content derives to "Untitled" (see NoteFilename); writeNoteToDisk
    // will rename it to something content-derived on the first real save.
    // Non-empty content (a duplicate) derives its name up front instead, and
    // uniqueFileName's own " (2)"/" (3)" collision suffix is what keeps a
    // duplicate of "Groceries" from fighting the original for the same file.
    m_idToFileName.insert(id, uniqueFileName(NoteFilename::deriveBaseName(content), id));

    QSaveFile file(filePathForId(id));
    bool written = false;
    if (file.open(QIODevice::WriteOnly)) {
        file.write(content.toUtf8());
        written = file.commit();
    }

    // If the file could not be written (unwritable folder, disk full), undo the
    // id we just minted and report failure rather than returning an id with no
    // file behind it. refreshNoteIds() below re-enumerates the folder and would
    // prune such an id moments later — but the caller opens the returned id
    // immediately (Main.qml, NoteWindow's duplicate path), so it would open a
    // phantom, empty note that then vanishes on the next enumeration.
    if (!written) {
        m_idToFileName.remove(id);
        return {};
    }

    m_lastWrittenContent[id] = content;
    saveIndex();

    refreshNoteIds(true);
    return id;
}

void NoteStore::deleteNote(const QString &id)
{
    const QString path = filePathForId(id);

    // Captured before the file is removed, so its images can be garbage-
    // collected once the note is gone (any that no other note references).
    const QString content = loadNoteContent(id);

    m_pendingContent.remove(id);
    if (QTimer *timer = m_pendingTimers.take(id)) {
        timer->stop();
        timer->deleteLater();
    }
    m_lastWrittenContent.remove(id);

    QFile::remove(path);

    m_idToFileName.remove(id);
    saveIndex();

    // The note's file is gone from disk and its id from the map, so the
    // cross-note reference check below no longer sees this note — every image
    // it uniquely held is now unreferenced and gets swept.
    cleanupRemovedImages(content, QString());

    refreshNoteIds(true);
}

void NoteStore::saveNoteContent(const QString &id, const QString &markdownText)
{
    m_pendingContent[id] = markdownText;

    QTimer *timer = m_pendingTimers.value(id, nullptr);
    if (!timer) {
        timer = new QTimer(this);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, this, [this, id]() { flushPendingSave(id); });
        m_pendingTimers.insert(id, timer);
    }
    // Restarting an already-running single-shot timer coalesces rapid calls
    // into a single write ~kSaveDebounceMs after the LAST call for this id.
    timer->start(kSaveDebounceMs);
}

bool NoteStore::hasUnsavedChanges(const QString &id) const
{
    // Pending content is cleared the instant flushPendingSave() writes it (see
    // flushPendingSave), so its presence is exactly "there are edits on screen
    // that aren't on disk yet."
    return m_pendingContent.contains(id);
}

void NoteStore::flushPendingSave(const QString &id)
{
    if (QTimer *timer = m_pendingTimers.take(id)) {
        timer->stop();
        timer->deleteLater();
    }

    if (!m_pendingContent.contains(id))
        return;

    const QString content = m_pendingContent.take(id);
    writeNoteToDisk(id, content);
}

void NoteStore::onDirectoryChanged(const QString &path)
{
    Q_UNUSED(path);
    refreshNoteIds(true);
}

void NoteStore::onFileChanged(const QString &path)
{
    const QFileInfo info(path);
    const QString id = idForFileName(info.completeBaseName());
    if (id.isEmpty())
        return; // Not (or no longer) a tracked note — e.g. we just renamed it away.

    if (info.exists()) {
        QFile file(path);
        QString diskContent;
        if (file.open(QIODevice::ReadOnly)) {
            diskContent = QString::fromUtf8(file.readAll());
            file.close();
        }

        // Only report as "external" if the content differs from what we
        // last wrote ourselves (or what we're already tracking as current) —
        // this is what keeps our own debounced writes from re-triggering
        // noteContentChangedExternally.
        const bool matchesKnownContent =
            m_lastWrittenContent.contains(id) && m_lastWrittenContent.value(id) == diskContent;
        if (!matchesKnownContent) {
            m_lastWrittenContent[id] = diskContent;
            emit noteContentChangedExternally(id);
        }

        // QSaveFile (ours or an external editor's own atomic-save pattern)
        // replaces the file via rename, which can silently drop the
        // underlying OS-level watch on the old inode. Re-arm it.
        m_watcher.removePath(path);
        m_watcher.addPath(path);
    }
    // If the file no longer exists, leave enumeration/cleanup to the
    // directoryChanged handler triggered by the same removal.
}

QString NoteStore::filePathForId(const QString &id) const
{
    const QString fileName = m_idToFileName.value(id);
    if (fileName.isEmpty())
        return QString();
    return QDir(m_notesFolder).filePath(fileName + QStringLiteral(".md"));
}

void NoteStore::refreshNoteIds(bool emitIfChanged)
{
    QSet<QString> diskBaseNames;
    const QDir dir(m_notesFolder);
    if (dir.exists()) {
        const QFileInfoList entries = dir.entryInfoList(QStringList{QStringLiteral("*.md")}, QDir::Files);
        for (const QFileInfo &entry : entries)
            diskBaseNames.insert(entry.completeBaseName());
    }

    bool indexChanged = false;

    // Prune ids whose backing file is gone — deleted externally, or by us as
    // the tail end of a rename (see writeNoteToDisk).
    for (auto it = m_idToFileName.begin(); it != m_idToFileName.end();) {
        if (!diskBaseNames.contains(it.value())) {
            it = m_idToFileName.erase(it);
            indexChanged = true;
        } else {
            ++it;
        }
    }

    // Mint a fresh id for any .md file not already tracked: covers first run
    // over a pre-existing folder, an index file that's missing/corrupt, and
    // files dropped in by something other than this app.
    QSet<QString> trackedBaseNames;
    for (const QString &name : std::as_const(m_idToFileName))
        trackedBaseNames.insert(name);
    for (const QString &baseName : std::as_const(diskBaseNames)) {
        if (!trackedBaseNames.contains(baseName)) {
            m_idToFileName.insert(QUuid::createUuid().toString(QUuid::WithoutBraces), baseName);
            indexChanged = true;
        }
    }

    if (indexChanged)
        saveIndex();

    QStringList newIds = m_idToFileName.keys();
    newIds.sort();

    if (newIds != m_noteIds) {
        m_noteIds = newIds;
        watchAllNoteFiles();
        if (emitIfChanged)
            emit noteIdsChanged();
    }
}

QString NoteStore::idForFileName(const QString &baseName) const
{
    for (auto it = m_idToFileName.constBegin(); it != m_idToFileName.constEnd(); ++it) {
        if (it.value() == baseName)
            return it.key();
    }
    return QString();
}

QString NoteStore::indexFilePath() const
{
    return QDir(m_notesFolder).filePath(QStringLiteral(".suopnotes-index.json"));
}

void NoteStore::loadIndex()
{
    m_idToFileName.clear();

    QFile file(indexFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return; // No index yet — refreshNoteIds() mints ids for whatever's on disk.

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return; // Corrupt/unrecognized — same fallback as a missing index.

    const QJsonObject obj = doc.object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        if (it.value().isString())
            m_idToFileName.insert(it.key(), it.value().toString());
    }
}

void NoteStore::saveIndex() const
{
    QJsonObject obj;
    for (auto it = m_idToFileName.constBegin(); it != m_idToFileName.constEnd(); ++it)
        obj.insert(it.key(), it.value());

    QSaveFile file(indexFilePath());
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    file.commit();
}

QString NoteStore::uniqueFileName(const QString &desiredBase, const QString &excludeId) const
{
    const QString ownCurrentName = m_idToFileName.value(excludeId);

    QString candidate = desiredBase;
    for (int suffix = 2;; ++suffix) {
        if (candidate == ownCurrentName)
            return candidate; // Already this id's own file — never a collision with itself.

        bool collides = false;
        for (auto it = m_idToFileName.constBegin(); it != m_idToFileName.constEnd(); ++it) {
            if (it.key() != excludeId && it.value() == candidate) {
                collides = true;
                break;
            }
        }
        if (!collides && !QFile::exists(QDir(m_notesFolder).filePath(candidate + QStringLiteral(".md"))))
            return candidate;

        candidate = QStringLiteral("%1 (%2)").arg(desiredBase).arg(suffix);
    }
}

void NoteStore::watchAllNoteFiles()
{
    const QStringList currentlyWatched = m_watcher.files();
    if (!currentlyWatched.isEmpty())
        m_watcher.removePaths(currentlyWatched);

    QStringList toWatch;
    toWatch.reserve(m_noteIds.size());
    for (const QString &id : std::as_const(m_noteIds))
        toWatch << filePathForId(id);

    if (!toWatch.isEmpty())
        m_watcher.addPaths(toWatch);
}

void NoteStore::writeNoteToDisk(const QString &id, const QString &content)
{
    // What this note referenced before this write — diffed against `content`
    // after the write to garbage-collect images the edit removed (see below).
    const QString previousContent = m_lastWrittenContent.value(id);

    // Re-derive the on-disk filename from the content being saved, so the
    // file stays human-readable without the note's id (window title, KWin
    // rule, Settings keys) ever changing — see the class-level comment.
    const QString desiredBase = uniqueFileName(NoteFilename::deriveBaseName(content), id);
    const QString previousBase = m_idToFileName.value(id);
    const bool renaming = !previousBase.isEmpty() && previousBase != desiredBase;
    const QString previousPath =
        renaming ? QDir(m_notesFolder).filePath(previousBase + QStringLiteral(".md")) : QString();

    if (previousBase != desiredBase) {
        m_idToFileName.insert(id, desiredBase);
        saveIndex();
    }

    QSaveFile file(filePathForId(id));
    if (!file.open(QIODevice::WriteOnly))
        return;

    file.write(content.toUtf8());
    if (file.commit()) {
        // Record what we just wrote BEFORE the (queued) fileChanged signal
        // for this write can be delivered, so onFileChanged recognizes it as
        // our own write rather than an external change.
        m_lastWrittenContent[id] = content;

        const QString path = filePathForId(id);
        if (!m_watcher.files().contains(path))
            m_watcher.addPath(path);

        if (renaming) {
            m_watcher.removePath(previousPath);
            QFile::remove(previousPath);
        }

        // Garbage-collect any image this edit removed. Runs AFTER the new
        // content is on disk (so the cross-note reference check sees the edit),
        // and only ever deletes files that no note references any longer.
        cleanupRemovedImages(previousContent, content);
    }
}

QString NoteStore::importImage(const QString &sourceUrl)
{
    // Accept either a file:// URL (what drag-and-drop and the file dialog hand
    // over) or a plain local path.
    QString sourcePath = sourceUrl;
    if (const QUrl url(sourceUrl); url.isLocalFile())
        sourcePath = url.toLocalFile();

    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile())
        return QString();

    const QDir imgDir(QDir(m_notesFolder).filePath(QStringLiteral("img")));
    if (!QDir().mkpath(imgDir.absolutePath()))
        return QString();

    // Keep the source's own basename, but strip "(" and ")" from it: the name
    // ends up inside a markdown image src, "![](img/<name>)", where an unescaped
    // ")" would terminate the src early. The collision suffix is "-2"/"-3"...
    // (NOT " (2)" like note files use) for the same reason — note filenames are
    // never embedded in markdown, image names always are.
    QString base = sourceInfo.completeBaseName();
    base.replace(QRegularExpression(QStringLiteral("[()]")), QStringLiteral("_"));
    if (base.isEmpty())
        base = QStringLiteral("image");
    const QString suffix =
        sourceInfo.suffix().isEmpty() ? QString() : (QStringLiteral(".") + sourceInfo.suffix());
    QString fileName = base + suffix;
    int counter = 2;
    while (imgDir.exists(fileName))
        fileName = QStringLiteral("%1-%2%3").arg(base).arg(counter++).arg(suffix);

    if (!QFile::copy(sourcePath, imgDir.filePath(fileName)))
        return QString();

    return QStringLiteral("img/") + fileName;
}

QSet<QString> NoteStore::imageReferencesIn(const QString &content)
{
    // ![alt](img/<name>) — deliberately ONLY images under our own img/ folder.
    // An external URL (http…, or an absolute path a user typed) is not ours to
    // manage and must never be deleted, so it never matches here.
    static const QRegularExpression pattern(QStringLiteral("!\\[[^\\]\\n]*\\]\\((img/[^)\\n]+)\\)"));
    QSet<QString> refs;
    QRegularExpressionMatchIterator it = pattern.globalMatch(content);
    while (it.hasNext())
        refs.insert(it.next().captured(1));
    return refs;
}

bool NoteStore::isImageReferencedByAnyNote(const QString &relativePath) const
{
    const QDir dir(m_notesFolder);
    const QFileInfoList entries = dir.entryInfoList(QStringList{QStringLiteral("*.md")}, QDir::Files);
    for (const QFileInfo &entry : entries) {
        QFile file(entry.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QString content = QString::fromUtf8(file.readAll());
        file.close();
        if (imageReferencesIn(content).contains(relativePath))
            return true;
    }
    return false;
}

void NoteStore::cleanupRemovedImages(const QString &before, const QString &after)
{
    const QSet<QString> beforeRefs = imageReferencesIn(before);
    if (beforeRefs.isEmpty())
        return; // the common case (no images) costs one regex and stops here

    const QSet<QString> afterRefs = imageReferencesIn(after);
    for (const QString &ref : beforeRefs) {
        if (afterRefs.contains(ref))
            continue; // this note still uses it
        if (isImageReferencedByAnyNote(ref))
            continue; // another note still uses it (img/ is shared)
        QFile::remove(QDir(m_notesFolder).filePath(ref));
    }
}
