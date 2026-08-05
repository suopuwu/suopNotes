#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>

// Persistence for the TickTick access token and the user's own registered-app
// credentials.
//
// The token is worth protecting: it lasts one second under 180 days and carries
// full task read/write, so it is a standing credential rather than something
// short-lived. When qtkeychain is available it goes to the platform secret
// store; when it is not, the module keeps working and falls back to the config
// file, reporting that it did so rather than doing it silently.
//
// That fallback is exactly why qtkeychain's presence cannot double as the
// module's build switch — "no secret store" and "no TickTick" are different
// answers to different questions.
//
// Non-secret settings (client id, callback port, token expiry) live in the
// module's own INI, deliberately NOT in the app's Settings class: the module
// then needs no core changes at all, and removing it leaves one orphan file
// rather than keys threaded through shared config.
class TickTickTokenStore : public QObject
{
    Q_OBJECT

public:
    enum class SecretBackend {
        // Use the platform secret store when one is available.
        SystemKeychain,
        // Never contact the platform secret store, whatever the build supports.
        //
        // This exists for tests, and it is not optional politeness: the secret
        // store is a real, shared, system-wide resource keyed by service name,
        // so a spec that used it would write into the developer's own keyring —
        // and, because the service name is the same one the app uses, would
        // overwrite their real TickTick credentials every time the suite ran.
        ConfigFileOnly,
    };

    explicit TickTickTokenStore(QObject *parent = nullptr);

    // Test seam, mirroring Settings' and AutostartManager's own explicit-path
    // constructors: lets specs point at a temporary file instead of the real
    // user config. Specs should pass ConfigFileOnly — see the enum.
    explicit TickTickTokenStore(const QString &configFilePath,
                                SecretBackend backend = SecretBackend::SystemKeychain,
                                QObject *parent = nullptr);

    // True when this build can use the platform secret store. Reported so the
    // UI can warn before a 180-day credential lands in a plaintext file.
    static bool secureBackendAvailable();

    // Where non-secret module config lives.
    QString configFilePath() const;

    // --- non-secret config (synchronous; it is just an INI) ---

    QString clientId() const;
    void setClientId(const QString &clientId);

    quint16 callbackPort() const;
    void setCallbackPort(quint16 port);

    // Absolute expiry, derived from the token response's expires_in at the
    // moment it was received. Stored so the app can say "reconnect soon"
    // instead of only discovering the problem as a 401 mid-sync. Null when no
    // token is held.
    QDateTime expiresAt() const;

    // --- secrets (asynchronous: the keychain backend is a job, not a call) ---

    // Both emit their result exactly once, including on the plaintext path, so
    // callers have a single shape to code against regardless of backend.
    void loadSecrets();
    void storeSecrets(const QString &clientSecret,
                      const QString &accessToken,
                      qint64 expiresInSeconds);
    void clearSecrets();

signals:
    // accessToken/clientSecret are empty when nothing was stored — that is a
    // normal first-run answer, not an error.
    void secretsLoaded(const QString &clientSecret, const QString &accessToken);
    void secretsLoadFailed(const QString &reason);
    void secretsStored();
    void secretsStoreFailed(const QString &reason);
    void secretsCleared();

private:
    void writeSecretsToPlaintext(const QString &clientSecret, const QString &accessToken);
    void readSecretsFromPlaintext();
    bool useSystemKeychain() const;

    QString m_configFilePath;
    SecretBackend m_backend = SecretBackend::SystemKeychain;
};
