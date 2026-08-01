#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QTcpServer;
class QTcpSocket;
class TickTickTokenStore;

// OAuth2 for the TickTick Open API.
//
// Three facts about TickTick's implementation shape everything here, all three
// measured against the live service rather than taken from its docs:
//
//  - It is plain authorization-code, with NO PKCE. The client secret is sent,
//    so it cannot be kept secret in a distributed binary. This app sidesteps
//    that by having each user register their own app and enter their own
//    credentials, rather than shipping one shared pair.
//
//  - There is NO refresh token. The token response carries access_token,
//    expires_in, scope and token_type — nothing else. Re-authorizing means
//    sending the user through the browser again.
//
//  - The access token lasts 15551999 seconds, one second under 180 days. So
//    "no refresh token" is a prompt roughly twice a year rather than a
//    dealbreaker — but it also makes the stored token a real credential with
//    six months of full task read/write, which is why it goes to the platform
//    secret store when one is available (see TickTickTokenStore).
//
// The callback listener is opened only while a login is in flight and closed
// the moment the code arrives, so an idle app holds no socket.
class TickTickAuth : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool configured READ isConfigured NOTIFY configuredChanged)
    Q_PROPERTY(bool authorized READ isAuthorized NOTIFY authorizedChanged)
    Q_PROPERTY(QString redirectUri READ redirectUri NOTIFY configuredChanged)

public:
    explicit TickTickAuth(TickTickTokenStore &tokenStore, QObject *parent = nullptr);
    ~TickTickAuth() override;

    // --- configuration (the user's own registered app) ---

    // The redirect URI the user must register with TickTick, verbatim. Exposed
    // rather than merely documented because it has to match byte-for-byte on
    // both sides, and a mismatch fails at the authorize step with an error that
    // does not say which side is wrong.
    QString redirectUri() const;

    bool isConfigured() const;
    bool isAuthorized() const;

    Q_INVOKABLE void setClientCredentials(const QString &clientId, const QString &clientSecret);
    Q_INVOKABLE void setCallbackPort(quint16 port);

    // Opens the system browser and starts listening for the callback. No-op if
    // credentials are missing or a login is already in flight.
    Q_INVOKABLE void beginAuthorization();
    Q_INVOKABLE void cancelAuthorization();
    Q_INVOKABLE void signOut();

    // --- pure protocol helpers ---
    //
    // Static and side-effect-free so the wire format can be specified without a
    // network, a browser or a running event loop — the same split the codebase
    // uses for PositionEventService::mergeLivePosition and buildKWinRuleKeys.
    // This is where the fiddly, easy-to-get-silently-wrong parts live.

    struct CallbackParams {
        QString code;
        QString state;
        QString error;
        bool valid = false; // false when the request was not a usable callback at all
    };

    struct TokenResponse {
        QString accessToken;
        qint64 expiresInSeconds = 0;
        QString error;
        bool ok = false;
    };

    static QString redirectUriForPort(quint16 port);
    static QUrl buildAuthorizeUrl(const QString &clientId,
                                  const QString &redirectUri,
                                  const QString &state);
    static QByteArray basicAuthHeader(const QString &clientId, const QString &clientSecret);
    static QByteArray buildTokenRequestBody(const QString &code, const QString &redirectUri);

    // Takes the raw bytes of the browser's HTTP request. Returns valid=false for
    // anything that is not a GET of the callback path, so a stray request (a
    // favicon fetch, a port scan) can never be mistaken for an auth result.
    static CallbackParams parseCallbackRequest(const QByteArray &httpRequest);

    static TokenResponse parseTokenResponse(const QByteArray &json);

    // The scope pair TickTick offers — there are only these two.
    static QString scope();

signals:
    void configuredChanged();
    void authorizedChanged();
    void authorizationSucceeded();
    // Carries a reason fit to show the user; the flow is already torn down.
    void authorizationFailed(const QString &reason);

private:
    void handleCallbackConnection();
    void finishWithError(const QString &reason);
    void exchangeCodeForToken(const QString &code);
    void loadPersistedState();

    TickTickTokenStore &m_tokenStore;
    QNetworkAccessManager *m_network = nullptr;
    QTcpServer *m_callbackServer = nullptr;

    QString m_clientId;
    QString m_clientSecret;
    quint16 m_callbackPort = 8080;

    // Only set while a login is in flight; cleared on every exit path so a
    // stale code can never be replayed against a later attempt.
    QString m_pendingState;

    QString m_accessToken;
};
