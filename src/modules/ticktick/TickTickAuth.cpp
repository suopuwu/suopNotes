#include "TickTickAuth.h"

#include "TickTickTokenStore.h"

#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>

namespace {
const QString kAuthorizeUrl = QStringLiteral("https://ticktick.com/oauth/authorize");
const QString kTokenUrl = QStringLiteral("https://ticktick.com/oauth/token");
const QString kCallbackPath = QStringLiteral("/callback");

// What the browser sees once the code has been captured. Deliberately plain
// text: the page is served by a socket that is about to close, so anything
// referencing external resources would just fail to load.
const QByteArray kSuccessResponse =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/plain; charset=utf-8\r\n"
    "Connection: close\r\n"
    "\r\n"
    "suopNotes is connected to TickTick. You can close this tab.";

const QByteArray kFailureResponse =
    "HTTP/1.1 400 Bad Request\r\n"
    "Content-Type: text/plain; charset=utf-8\r\n"
    "Connection: close\r\n"
    "\r\n"
    "suopNotes could not complete the TickTick login. Check the app for details.";
}

TickTickAuth::TickTickAuth(TickTickTokenStore &tokenStore, QObject *parent)
    : QObject(parent)
    , m_tokenStore(tokenStore)
{
    loadPersistedState();
}

TickTickAuth::~TickTickAuth() = default;

QString TickTickAuth::scope()
{
    // The only two scopes TickTick defines.
    return QStringLiteral("tasks:read tasks:write");
}

QString TickTickAuth::redirectUriForPort(quint16 port)
{
    return QStringLiteral("http://localhost:%1%2").arg(port).arg(kCallbackPath);
}

QUrl TickTickAuth::buildAuthorizeUrl(const QString &clientId,
                                     const QString &redirectUri,
                                     const QString &state)
{
    QUrl url(kAuthorizeUrl);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), clientId);
    query.addQueryItem(QStringLiteral("scope"), scope());
    query.addQueryItem(QStringLiteral("state"), state);
    query.addQueryItem(QStringLiteral("redirect_uri"), redirectUri);
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    url.setQuery(query);
    return url;
}

QByteArray TickTickAuth::basicAuthHeader(const QString &clientId, const QString &clientSecret)
{
    // TickTick takes the client credentials as HTTP Basic on the token request,
    // not as body parameters.
    const QByteArray pair = (clientId + QLatin1Char(':') + clientSecret).toUtf8();
    return QByteArrayLiteral("Basic ") + pair.toBase64();
}

QByteArray TickTickAuth::buildTokenRequestBody(const QString &code, const QString &redirectUri)
{
    QUrlQuery body;
    body.addQueryItem(QStringLiteral("code"), code);
    // TickTick documents this as the only accepted grant type — there is no
    // refresh_token grant to fall back on later.
    body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    body.addQueryItem(QStringLiteral("scope"), scope());
    body.addQueryItem(QStringLiteral("redirect_uri"), redirectUri);
    return body.toString(QUrl::FullyEncoded).toUtf8();
}

TickTickAuth::CallbackParams TickTickAuth::parseCallbackRequest(const QByteArray &httpRequest)
{
    CallbackParams result;

    // Only the request line matters. Anything that is not "GET <target> HTTP/x"
    // is not a browser following our redirect.
    const int lineEnd = httpRequest.indexOf('\r');
    const QByteArray requestLine =
        lineEnd >= 0 ? httpRequest.left(lineEnd) : httpRequest;

    const QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 2 || parts.at(0) != QByteArrayLiteral("GET"))
        return result;

    const QUrl target(QString::fromUtf8(parts.at(1)));
    if (target.path() != kCallbackPath)
        return result;

    const QUrlQuery query(target);
    // A callback with neither a code nor an error is not a usable result — a
    // bare GET of the path (someone opening the URL by hand) lands here.
    if (!query.hasQueryItem(QStringLiteral("code"))
        && !query.hasQueryItem(QStringLiteral("error")))
        return result;

    result.code = query.queryItemValue(QStringLiteral("code"), QUrl::FullyDecoded);
    result.state = query.queryItemValue(QStringLiteral("state"), QUrl::FullyDecoded);
    result.error = query.queryItemValue(QStringLiteral("error"), QUrl::FullyDecoded);
    result.valid = true;
    return result;
}

TickTickAuth::TokenResponse TickTickAuth::parseTokenResponse(const QByteArray &json)
{
    TokenResponse result;

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        result.error = QStringLiteral("The token response was not valid JSON.");
        return result;
    }

    const QJsonObject object = doc.object();

    // OAuth error responses are a JSON object too, so check for them before
    // concluding the token is merely missing.
    if (object.contains(QStringLiteral("error"))) {
        const QString description = object.value(QStringLiteral("error_description")).toString();
        result.error = description.isEmpty()
            ? object.value(QStringLiteral("error")).toString()
            : description;
        return result;
    }

    result.accessToken = object.value(QStringLiteral("access_token")).toString();
    if (result.accessToken.isEmpty()) {
        result.error = QStringLiteral("The token response contained no access token.");
        return result;
    }

    // Present in the live response even though TickTick's docs omit it. Absence
    // is not fatal — a token with an unknown expiry still works, the app just
    // cannot warn ahead of time.
    result.expiresInSeconds = static_cast<qint64>(object.value(QStringLiteral("expires_in")).toDouble());
    result.ok = true;
    return result;
}

QString TickTickAuth::redirectUri() const
{
    return redirectUriForPort(m_callbackPort);
}

bool TickTickAuth::isConfigured() const
{
    return !m_clientId.isEmpty() && !m_clientSecret.isEmpty();
}

bool TickTickAuth::isAuthorized() const
{
    return !m_accessToken.isEmpty();
}

void TickTickAuth::loadPersistedState()
{
    m_clientId = m_tokenStore.clientId();
    m_callbackPort = m_tokenStore.callbackPort();

    connect(&m_tokenStore, &TickTickTokenStore::secretsLoaded, this,
            [this](const QString &clientSecret, const QString &accessToken) {
                const bool wasConfigured = isConfigured();
                const bool wasAuthorized = isAuthorized();
                m_clientSecret = clientSecret;
                m_accessToken = accessToken;
                if (isConfigured() != wasConfigured)
                    emit configuredChanged();
                if (isAuthorized() != wasAuthorized)
                    emit authorizedChanged();
            });

    m_tokenStore.loadSecrets();
}

void TickTickAuth::setClientCredentials(const QString &clientId, const QString &clientSecret)
{
    const bool wasConfigured = isConfigured();
    m_clientId = clientId;
    m_clientSecret = clientSecret;
    m_tokenStore.setClientId(clientId);
    // Persisted alongside whatever token is currently held, so entering
    // credentials does not silently discard an existing session.
    m_tokenStore.storeSecrets(clientSecret, m_accessToken, 0);
    if (isConfigured() != wasConfigured)
        emit configuredChanged();
}

void TickTickAuth::setCallbackPort(quint16 port)
{
    if (port == 0)
        return;
    m_callbackPort = port;
    m_tokenStore.setCallbackPort(port);
    emit configuredChanged(); // redirectUri is derived from the port
}

void TickTickAuth::beginAuthorization()
{
    if (!isConfigured()) {
        emit authorizationFailed(
            tr("Enter your TickTick client ID and secret first."));
        return;
    }
    if (m_callbackServer) {
        emit authorizationFailed(tr("A TickTick login is already in progress."));
        return;
    }

    m_callbackServer = new QTcpServer(this);
    // Loopback only: this listener exists to catch one redirect from the local
    // browser and must not be reachable from the network.
    if (!m_callbackServer->listen(QHostAddress::LocalHost, m_callbackPort)) {
        const QString reason = tr("Could not listen on port %1 for the TickTick login (%2).")
                                   .arg(m_callbackPort)
                                   .arg(m_callbackServer->errorString());
        m_callbackServer->deleteLater();
        m_callbackServer = nullptr;
        emit authorizationFailed(reason);
        return;
    }

    // Opaque, single-use, and checked when the callback arrives — without it a
    // crafted local request could feed us someone else's authorization code.
    m_pendingState = QString::number(QRandomGenerator::system()->generate64(), 36);

    connect(m_callbackServer, &QTcpServer::newConnection,
            this, &TickTickAuth::handleCallbackConnection);

    const QUrl authorizeUrl = buildAuthorizeUrl(m_clientId, redirectUri(), m_pendingState);
    if (!QDesktopServices::openUrl(authorizeUrl))
        finishWithError(tr("Could not open a browser for the TickTick login."));
}

void TickTickAuth::handleCallbackConnection()
{
    QTcpSocket *socket = m_callbackServer ? m_callbackServer->nextPendingConnection() : nullptr;
    if (!socket)
        return;

    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
        const QByteArray request = socket->readAll();
        const CallbackParams params = parseCallbackRequest(request);

        // Not our redirect (a favicon fetch, a stray probe). Close it and keep
        // waiting rather than failing the login.
        if (!params.valid) {
            socket->disconnectFromHost();
            return;
        }

        const bool stateMatches = !m_pendingState.isEmpty() && params.state == m_pendingState;
        socket->write(params.error.isEmpty() && stateMatches ? kSuccessResponse : kFailureResponse);
        socket->flush();
        socket->disconnectFromHost();

        if (!params.error.isEmpty()) {
            finishWithError(tr("TickTick refused the login (%1).").arg(params.error));
            return;
        }
        if (!stateMatches) {
            finishWithError(tr("The TickTick login response did not match this request."));
            return;
        }

        exchangeCodeForToken(params.code);
    });

    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
}

void TickTickAuth::exchangeCodeForToken(const QString &code)
{
    // The listener has done its job; close it before the network round-trip so
    // an idle app is never left holding a socket.
    if (m_callbackServer) {
        m_callbackServer->close();
        m_callbackServer->deleteLater();
        m_callbackServer = nullptr;
    }
    m_pendingState.clear();

    if (!m_network)
        m_network = new QNetworkAccessManager(this);

    QNetworkRequest request{QUrl(kTokenUrl)};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    request.setRawHeader(QByteArrayLiteral("Authorization"),
                         basicAuthHeader(m_clientId, m_clientSecret));

    QNetworkReply *reply = m_network->post(request, buildTokenRequestBody(code, redirectUri()));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        const QByteArray payload = reply->readAll();
        // The body is parsed even on a transport error: OAuth error responses
        // arrive with a 4xx status, and their body says what actually went
        // wrong, which is far more useful than "Bad Request".
        const TokenResponse response = parseTokenResponse(payload);

        if (!response.ok) {
            const QString detail = response.error.isEmpty() ? reply->errorString() : response.error;
            emit authorizationFailed(tr("TickTick login failed: %1").arg(detail));
            return;
        }

        const bool wasAuthorized = isAuthorized();
        m_accessToken = response.accessToken;
        m_tokenStore.storeSecrets(m_clientSecret, m_accessToken, response.expiresInSeconds);

        if (isAuthorized() != wasAuthorized)
            emit authorizedChanged();
        emit authorizationSucceeded();
    });
}

void TickTickAuth::cancelAuthorization()
{
    if (!m_callbackServer)
        return;
    m_callbackServer->close();
    m_callbackServer->deleteLater();
    m_callbackServer = nullptr;
    m_pendingState.clear();
}

void TickTickAuth::finishWithError(const QString &reason)
{
    cancelAuthorization();
    emit authorizationFailed(reason);
}

void TickTickAuth::signOut()
{
    cancelAuthorization();
    const bool wasAuthorized = isAuthorized();
    m_accessToken.clear();
    m_tokenStore.clearSecrets();
    if (wasAuthorized)
        emit authorizedChanged();
}
