#include "TickTickTokenStore.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#ifdef SUOPNOTES_HAVE_QTKEYCHAIN
#include <qt6keychain/keychain.h>
#endif

namespace {
const QString kServiceName = QStringLiteral("suopNotes-ticktick");
const QString kKeyAccessToken = QStringLiteral("access_token");
const QString kKeyClientSecret = QStringLiteral("client_secret");

const QString kIniClientId = QStringLiteral("clientId");
const QString kIniCallbackPort = QStringLiteral("callbackPort");
const QString kIniExpiresAt = QStringLiteral("expiresAt");
// Only ever written on the no-keychain path.
const QString kIniPlaintextSecret = QStringLiteral("insecure/clientSecret");
const QString kIniPlaintextToken = QStringLiteral("insecure/accessToken");

QString defaultConfigPath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return QDir(dir).filePath(QStringLiteral("ticktick.ini"));
}
}

TickTickTokenStore::TickTickTokenStore(QObject *parent)
    : TickTickTokenStore(defaultConfigPath(), parent)
{
}

TickTickTokenStore::TickTickTokenStore(const QString &configFilePath, QObject *parent)
    : QObject(parent)
    , m_configFilePath(configFilePath)
{
}

bool TickTickTokenStore::secureBackendAvailable()
{
#ifdef SUOPNOTES_HAVE_QTKEYCHAIN
    return true;
#else
    return false;
#endif
}

QString TickTickTokenStore::configFilePath() const
{
    return m_configFilePath;
}

QString TickTickTokenStore::clientId() const
{
    QSettings settings(m_configFilePath, QSettings::IniFormat);
    return settings.value(kIniClientId).toString();
}

void TickTickTokenStore::setClientId(const QString &clientId)
{
    QSettings settings(m_configFilePath, QSettings::IniFormat);
    settings.setValue(kIniClientId, clientId);
    settings.sync();
}

quint16 TickTickTokenStore::callbackPort() const
{
    QSettings settings(m_configFilePath, QSettings::IniFormat);
    // 8080 is the port most TickTick client implementations register, so it is
    // the least surprising default for a user copying a redirect URI from
    // someone else's instructions.
    const uint stored = settings.value(kIniCallbackPort, 8080).toUInt();
    if (stored == 0 || stored > 65535)
        return 8080;
    return static_cast<quint16>(stored);
}

void TickTickTokenStore::setCallbackPort(quint16 port)
{
    QSettings settings(m_configFilePath, QSettings::IniFormat);
    settings.setValue(kIniCallbackPort, port);
    settings.sync();
}

QDateTime TickTickTokenStore::expiresAt() const
{
    QSettings settings(m_configFilePath, QSettings::IniFormat);
    const QString raw = settings.value(kIniExpiresAt).toString();
    if (raw.isEmpty())
        return QDateTime();
    return QDateTime::fromString(raw, Qt::ISODate);
}

void TickTickTokenStore::loadSecrets()
{
#ifdef SUOPNOTES_HAVE_QTKEYCHAIN
    // Two reads, chained: the secret first, then the token, so both are in hand
    // before a single result is reported. Chaining rather than firing both at
    // once keeps the completion condition trivial — there is no "which one came
    // back second" bookkeeping to get wrong.
    auto *secretJob = new QKeychain::ReadPasswordJob(kServiceName, this);
    secretJob->setAutoDelete(true);
    secretJob->setKey(kKeyClientSecret);

    connect(secretJob, &QKeychain::Job::finished, this, [this, secretJob]() {
        // EntryNotFound is the ordinary first-run answer, not a failure.
        if (secretJob->error() != QKeychain::NoError
            && secretJob->error() != QKeychain::EntryNotFound) {
            emit secretsLoadFailed(secretJob->errorString());
            return;
        }
        const QString clientSecret = secretJob->textData();

        auto *tokenJob = new QKeychain::ReadPasswordJob(kServiceName, this);
        tokenJob->setAutoDelete(true);
        tokenJob->setKey(kKeyAccessToken);
        connect(tokenJob, &QKeychain::Job::finished, this, [this, tokenJob, clientSecret]() {
            if (tokenJob->error() != QKeychain::NoError
                && tokenJob->error() != QKeychain::EntryNotFound) {
                emit secretsLoadFailed(tokenJob->errorString());
                return;
            }
            emit secretsLoaded(clientSecret, tokenJob->textData());
        });
        tokenJob->start();
    });

    secretJob->start();
#else
    // Emitted through the event loop rather than inline, so this path has the
    // same "never completes before the caller has returned" shape as the
    // keychain one. A caller that connected after calling would otherwise miss
    // the result on one backend and not the other.
    QTimer::singleShot(0, this, [this]() { readSecretsFromPlaintext(); });
#endif
}

void TickTickTokenStore::storeSecrets(const QString &clientSecret,
                                      const QString &accessToken,
                                      qint64 expiresInSeconds)
{
    // The expiry is not a secret and is useful without unlocking anything — it
    // is what lets the app warn before the token lapses rather than discovering
    // it as a mid-sync 401 — so it stays in the INI either way.
    {
        QSettings settings(m_configFilePath, QSettings::IniFormat);
        if (expiresInSeconds > 0) {
            settings.setValue(kIniExpiresAt,
                              QDateTime::currentDateTimeUtc().addSecs(expiresInSeconds).toString(Qt::ISODate));
        } else {
            settings.remove(kIniExpiresAt);
        }
        settings.sync();
    }

#ifdef SUOPNOTES_HAVE_QTKEYCHAIN
    auto *secretJob = new QKeychain::WritePasswordJob(kServiceName, this);
    secretJob->setAutoDelete(true);
    secretJob->setKey(kKeyClientSecret);
    secretJob->setTextData(clientSecret);

    connect(secretJob, &QKeychain::Job::finished, this, [this, secretJob, accessToken]() {
        if (secretJob->error() != QKeychain::NoError) {
            emit secretsStoreFailed(secretJob->errorString());
            return;
        }
        auto *tokenJob = new QKeychain::WritePasswordJob(kServiceName, this);
        tokenJob->setAutoDelete(true);
        tokenJob->setKey(kKeyAccessToken);
        tokenJob->setTextData(accessToken);
        connect(tokenJob, &QKeychain::Job::finished, this, [this, tokenJob]() {
            if (tokenJob->error() != QKeychain::NoError) {
                emit secretsStoreFailed(tokenJob->errorString());
                return;
            }
            emit secretsStored();
        });
        tokenJob->start();
    });

    secretJob->start();
#else
    writeSecretsToPlaintext(clientSecret, accessToken);
    QTimer::singleShot(0, this, [this]() { emit secretsStored(); });
#endif
}

void TickTickTokenStore::clearSecrets()
{
    {
        QSettings settings(m_configFilePath, QSettings::IniFormat);
        settings.remove(kIniExpiresAt);
        // Removed unconditionally, not just on the fallback path: a build that
        // gains a keychain later must not leave an old plaintext copy behind.
        settings.remove(kIniPlaintextSecret);
        settings.remove(kIniPlaintextToken);
        settings.sync();
    }

#ifdef SUOPNOTES_HAVE_QTKEYCHAIN
    for (const QString &key : {kKeyClientSecret, kKeyAccessToken}) {
        auto *job = new QKeychain::DeletePasswordJob(kServiceName, this);
        job->setAutoDelete(true);
        job->setKey(key);
        job->start();
    }
#endif

    QTimer::singleShot(0, this, [this]() { emit secretsCleared(); });
}

void TickTickTokenStore::writeSecretsToPlaintext(const QString &clientSecret,
                                                 const QString &accessToken)
{
    QSettings settings(m_configFilePath, QSettings::IniFormat);
    settings.setValue(kIniPlaintextSecret, clientSecret);
    settings.setValue(kIniPlaintextToken, accessToken);
    settings.sync();

    // Best-effort: the file holds a six-month credential, so at least keep it
    // off other local accounts. Not a substitute for a secret store, which is
    // why the UI is told the backend is unavailable rather than this being
    // treated as equivalent.
    QFile::setPermissions(m_configFilePath,
                          QFile::ReadOwner | QFile::WriteOwner);
}

void TickTickTokenStore::readSecretsFromPlaintext()
{
    QSettings settings(m_configFilePath, QSettings::IniFormat);
    emit secretsLoaded(settings.value(kIniPlaintextSecret).toString(),
                       settings.value(kIniPlaintextToken).toString());
}
