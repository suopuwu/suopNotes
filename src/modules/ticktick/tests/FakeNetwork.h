#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QUrl>

// A stand-in for the live TickTick API, installed through
// TickTickClient::setNetworkAccessManager.
//
// Every outbound request is recorded, so a spec can assert WHICH request went
// out (path, method, body) — the thing pendingUploadCount() alone can never
// say. By default a request then simply never completes, like a black-holed
// socket: no spurious error, no spurious success, and "dispatched, waiting on
// a reply" stays observable for as long as the spec cares to look. A spec that
// wants a reply either queues a canned one here (matched by path substring,
// first match wins and is consumed) or emits the client's own signals
// directly, whichever reads better for what it is pinning.
//
// Before this existed, the sync specs ran against the real
// api.ticktick.com — every run sent a dozen genuine HTTPS requests with a
// bogus bearer token and depended on them failing quietly.

struct RecordedRequest
{
    QByteArray method;
    QUrl url;
    QByteArray body;
};

struct CannedResponse
{
    // Consumed by the first request whose URL path contains this.
    QByteArray pathContains;
    int httpStatus = 200;
    QByteArray payload;
    // Set to something other than NoError to exercise checkReply's error
    // path; Qt does not derive error() from the status attribute in a fake.
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    QString errorString;
};

class FakeReply : public QNetworkReply
{
public:
    // finishes == false builds the default never-completing reply.
    FakeReply(const QNetworkRequest &request,
              QNetworkAccessManager::Operation operation,
              const CannedResponse &response,
              bool finishes,
              QObject *parent)
        : QNetworkReply(parent)
        , m_payload(response.payload)
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(operation);
        setOpenMode(ReadOnly);
        if (!finishes)
            return;

        if (response.httpStatus > 0)
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.httpStatus);
        if (response.error != QNetworkReply::NoError)
            setError(response.error, response.errorString);

        // Through the event loop, never inline: a real reply can never finish
        // before the request call has returned, and the client's handlers are
        // connected after posting.
        QTimer::singleShot(0, this, [this]() {
            if (error() != QNetworkReply::NoError)
                emit errorOccurred(error());
            if (!m_payload.isEmpty())
                emit readyRead();
            setFinished(true);
            emit finished();
        });
    }

    void abort() override { }

    qint64 bytesAvailable() const override
    {
        return m_payload.size() - m_offset + QIODevice::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const qint64 available = m_payload.size() - m_offset;
        if (available <= 0)
            return -1;
        const qint64 chunk = qMin(maxSize, available);
        memcpy(data, m_payload.constData() + m_offset, static_cast<size_t>(chunk));
        m_offset += chunk;
        return chunk;
    }

private:
    QByteArray m_payload;
    qint64 m_offset = 0;
};

class FakeNetworkAccessManager : public QNetworkAccessManager
{
public:
    explicit FakeNetworkAccessManager(QObject *parent = nullptr)
        : QNetworkAccessManager(parent)
    {
    }

    QList<RecordedRequest> requests;
    QList<CannedResponse> canned;

    // The recorded URL paths, in dispatch order — what most assertions want.
    QStringList requestPaths() const
    {
        QStringList paths;
        for (const RecordedRequest &request : requests)
            paths.append(request.url.path());
        return paths;
    }

    bool sawRequestTo(const QString &pathFragment) const
    {
        return countRequestsTo(pathFragment) > 0;
    }

    int countRequestsTo(const QString &pathFragment) const
    {
        int count = 0;
        for (const RecordedRequest &request : requests) {
            if (request.url.path().contains(pathFragment))
                ++count;
        }
        return count;
    }

    // GET /project (the list fetch) and POST /project (creating a list) share
    // a path, so "was a list created?" needs the method too.
    bool sawPostTo(const QString &pathFragment) const
    {
        for (const RecordedRequest &request : requests) {
            if (request.method == QByteArrayLiteral("POST")
                && request.url.path().contains(pathFragment)) {
                return true;
            }
        }
        return false;
    }

    bool sawBodyContaining(const QByteArray &needle) const
    {
        for (const RecordedRequest &request : requests) {
            if (request.body.contains(needle))
                return true;
        }
        return false;
    }

protected:
    QNetworkReply *createRequest(Operation operation,
                                 const QNetworkRequest &request,
                                 QIODevice *outgoingData) override
    {
        RecordedRequest recorded;
        recorded.method = operation == GetOperation ? QByteArrayLiteral("GET")
                                                    : QByteArrayLiteral("POST");
        recorded.url = request.url();
        if (outgoingData)
            recorded.body = outgoingData->readAll();
        requests.append(recorded);

        for (int i = 0; i < canned.size(); ++i) {
            if (recorded.url.path().contains(QString::fromUtf8(canned.at(i).pathContains))) {
                const CannedResponse response = canned.takeAt(i);
                return new FakeReply(request, operation, response, true, this);
            }
        }

        return new FakeReply(request, operation, CannedResponse{}, false, this);
    }
};
