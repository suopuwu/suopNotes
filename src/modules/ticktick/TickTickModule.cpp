#include "TickTickModule.h"

namespace {
// Short, and punctuated with a character a UUID never contains, so an owned id
// can never be confused with a bare local uuid in either direction.
const QString kPrefix = QStringLiteral("tt:");
}

TickTickModule::TickTickModule(QObject *parent)
    : QObject(parent)
    , m_tokenStore(this)
    , m_auth(m_tokenStore, this)
{
}

TickTickAuth *TickTickModule::auth()
{
    return &m_auth;
}

bool TickTickModule::secureStorageAvailable() const
{
    return TickTickTokenStore::secureBackendAvailable();
}

QString TickTickModule::prefix()
{
    return kPrefix;
}

bool TickTickModule::ownsNoteId(const QString &noteId)
{
    // A bare prefix with nothing after it is not a note — rejecting it here
    // means makeNoteId/localUuidFor never have to represent an owned id with an
    // empty uuid.
    return noteId.startsWith(kPrefix) && noteId.size() > kPrefix.size();
}

QString TickTickModule::makeNoteId(const QString &localUuid)
{
    if (localUuid.isEmpty())
        return QString();

    // Idempotent: handing back an already-owned id returns it unchanged rather
    // than double-prefixing it, so a caller that has lost track of which form it
    // is holding cannot corrupt the id.
    if (ownsNoteId(localUuid))
        return localUuid;

    return kPrefix + localUuid;
}

QString TickTickModule::localUuidFor(const QString &noteId)
{
    if (!ownsNoteId(noteId))
        return QString();

    return noteId.mid(kPrefix.size());
}
