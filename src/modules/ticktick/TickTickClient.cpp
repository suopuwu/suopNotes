#include "TickTickClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace {
const QString kBase = QStringLiteral("https://api.ticktick.com/open/v1");

QNetworkRequest jsonRequest(const QString &path, const QString &accessToken)
{
    QNetworkRequest request{QUrl(kBase + path)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader(QByteArrayLiteral("Authorization"),
                         QByteArrayLiteral("Bearer ") + accessToken.toUtf8());
    // HTTP/1.1 on purpose. With Qt's default HTTP/2, the connection left idle
    // between pulls gets closed by the server, and Qt's h2 frame loop then
    // floods the log with "QIODevice::read (QSslSocket): device not open" as
    // it re-reads the dead socket. A few small JSON requests an hour gain
    // nothing from h2 anyway.
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    return request;
}

TickTickClient::Task taskFromJson(const QJsonObject &object)
{
    TickTickClient::Task task;
    task.id = object.value(QStringLiteral("id")).toString();
    task.projectId = object.value(QStringLiteral("projectId")).toString();
    task.title = object.value(QStringLiteral("title")).toString();
    task.content = object.value(QStringLiteral("content")).toString();
    task.status = object.value(QStringLiteral("status")).toInt();
    return task;
}
}

TickTickClient::TickTickClient(QObject *parent)
    : QObject(parent)
{
}

void TickTickClient::setAccessToken(const QString &accessToken)
{
    m_accessToken = accessToken;
}

bool TickTickClient::hasAccessToken() const
{
    return !m_accessToken.isEmpty();
}

QNetworkAccessManager *TickTickClient::network()
{
    // Created on first use, so an app that never connects TickTick never
    // constructs one.
    if (!m_network)
        m_network = new QNetworkAccessManager(this);
    return m_network;
}

void TickTickClient::setNetworkAccessManager(QNetworkAccessManager *manager)
{
    if (m_network)
        m_network->deleteLater();
    m_network = manager;
    if (m_network)
        m_network->setParent(this);
}

QByteArray TickTickClient::buildProjectBody(const QString &name)
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), name);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray TickTickClient::buildTaskFilterBody(const QString &projectId)
{
    QJsonObject object;
    object.insert(QStringLiteral("projectIds"), QJsonArray{projectId});
    // Open tasks only. Completing a task in TickTick is how the user says they
    // are done with it, so its note stops being shown — the pull's own
    // reconcile then treats the now-absent task exactly like a deleted one and
    // closes the window. An earlier build asked for status 2 as well, on the
    // reasoning that a completed task still exists; that made a checked-off
    // task linger on screen with no way to dismiss it short of deleting it
    // outright, which is the opposite of what completing something means.
    object.insert(QStringLiteral("status"), QJsonArray{0});
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray TickTickClient::buildTaskCreateBody(const QString &projectId, const QString &title,
                                               const QString &content)
{
    QJsonObject object;
    // No id: this is the one request where TickTick mints it and hands it back.
    object.insert(QStringLiteral("projectId"), projectId);
    object.insert(QStringLiteral("title"), title);
    object.insert(QStringLiteral("content"), content);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

TickTickClient::Task TickTickClient::parseTask(const QByteArray &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return Task();
    return taskFromJson(doc.object());
}

QList<TickTickClient::Project> TickTickClient::parseProjectList(const QByteArray &json)
{
    QList<Project> projects;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isArray())
        return projects;

    const QJsonArray array = doc.array();
    projects.reserve(array.size());
    for (const QJsonValue &value : array) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        Project project;
        project.id = object.value(QStringLiteral("id")).toString();
        project.name = object.value(QStringLiteral("name")).toString();
        if (!project.id.isEmpty())
            projects.append(project);
    }
    return projects;
}

TickTickClient::Project TickTickClient::parseProject(const QByteArray &json)
{
    Project project;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return project;
    const QJsonObject object = doc.object();
    project.id = object.value(QStringLiteral("id")).toString();
    project.name = object.value(QStringLiteral("name")).toString();
    return project;
}

QList<TickTickClient::Task> TickTickClient::parseTaskList(const QByteArray &json)
{
    QList<Task> tasks;
    const QJsonDocument doc = QJsonDocument::fromJson(json);

    // /task/filter answers with a bare array, but a project's own data endpoint
    // wraps its tasks in an object. Accepting both means the caller does not
    // have to care which one produced this payload.
    QJsonArray array;
    if (doc.isArray())
        array = doc.array();
    else if (doc.isObject())
        array = doc.object().value(QStringLiteral("tasks")).toArray();
    else
        return tasks;

    tasks.reserve(array.size());
    for (const QJsonValue &value : array) {
        if (!value.isObject())
            continue;
        const Task task = taskFromJson(value.toObject());
        if (!task.id.isEmpty())
            tasks.append(task);
    }
    return tasks;
}

QByteArray TickTickClient::buildTaskUpdateBody(const QString &taskId, const QString &projectId,
                                               const QString &title, const QString &content)
{
    QJsonObject object;
    // id and projectId identify the task; TickTick requires both on an update.
    object.insert(QStringLiteral("id"), taskId);
    object.insert(QStringLiteral("projectId"), projectId);
    object.insert(QStringLiteral("title"), title);
    object.insert(QStringLiteral("content"), content);
    // Nothing else is sent on purpose — see the header. A note knows nothing
    // about due dates, priorities or tags, so it must not overwrite them.
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool TickTickClient::checkReply(QNetworkReply *reply, QByteArray &payloadOut)
{
    payloadOut = reply->readAll();

    if (reply->error() == QNetworkReply::NoError)
        return true;

    // TickTick returns its own JSON error body alongside the HTTP status, and
    // it says far more than "Bad Request" does.
    const QJsonDocument doc = QJsonDocument::fromJson(payloadOut);
    QString detail;
    if (doc.isObject()) {
        const QJsonObject object = doc.object();
        detail = object.value(QStringLiteral("errorMessage")).toString();
        if (detail.isEmpty())
            detail = object.value(QStringLiteral("error")).toString();
    }
    if (detail.isEmpty())
        detail = reply->errorString();

    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 401) {
        // There is no refresh token, so this is not something the app can
        // quietly recover from — it needs the user back in a browser.
        emit requestFailed(tr("TickTick rejected the saved login. Reconnect to continue."));
    } else {
        emit requestFailed(detail);
    }
    return false;
}

void TickTickClient::fetchProjects()
{
    QNetworkReply *reply = network()->get(jsonRequest(QStringLiteral("/project"), m_accessToken));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        QByteArray payload;
        if (!checkReply(reply, payload))
            return;
        emit projectsFetched(parseProjectList(payload));
    });
}

void TickTickClient::createProject(const QString &name)
{
    QNetworkReply *reply = network()->post(jsonRequest(QStringLiteral("/project"), m_accessToken),
                                           buildProjectBody(name));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        QByteArray payload;
        if (!checkReply(reply, payload))
            return;

        const Project project = parseProject(payload);
        if (project.id.isEmpty()) {
            emit requestFailed(tr("TickTick did not return the new project."));
            return;
        }
        emit projectCreated(project);
    });
}

void TickTickClient::updateTask(const QString &taskId, const QString &projectId,
                                const QString &title, const QString &content)
{
    // Checked before sending rather than after failing: oversize content comes
    // back as a 500, which is indistinguishable from a transient server fault
    // and would otherwise be retried forever.
    if (content.toUtf8().size() > kMaxContentBytes) {
        emit taskRejectedPermanently(
            taskId,
            tr("This note is too large for TickTick (over %1 KB) and was not uploaded.")
                .arg(kMaxContentBytes / 1000));
        return;
    }

    QNetworkReply *reply =
        network()->post(jsonRequest(QStringLiteral("/task/") + taskId, m_accessToken),
                        buildTaskUpdateBody(taskId, projectId, title, content));
    connect(reply, &QNetworkReply::finished, this, [this, reply, taskId]() {
        reply->deleteLater();
        QByteArray payload;
        if (!checkReply(reply, payload))
            return;
        emit taskUpdated(taskId);
    });
}

QString TickTickClient::completeTaskPath(const QString &projectId, const QString &taskId)
{
    return QStringLiteral("/project/") + projectId + QStringLiteral("/task/") + taskId
        + QStringLiteral("/complete");
}

void TickTickClient::completeTask(const QString &projectId, const QString &taskId)
{
    // An empty JSON object, not no body at all: the request already declares
    // application/json, and a POST with nothing parseable risks a 400.
    QNetworkReply *reply =
        network()->post(jsonRequest(completeTaskPath(projectId, taskId), m_accessToken),
                        QByteArrayLiteral("{}"));
    connect(reply, &QNetworkReply::finished, this, [this, reply, taskId]() {
        reply->deleteLater();
        QByteArray payload;
        if (!checkReply(reply, payload))
            return;
        // The success body is empty; there is nothing to parse beyond the
        // status checkReply has already validated.
        emit taskCompleted(taskId);
    });
}

void TickTickClient::createTask(const QString &clientRef, const QString &projectId,
                                const QString &title, const QString &content)
{
    QNetworkReply *reply = network()->post(jsonRequest(QStringLiteral("/task"), m_accessToken),
                                           buildTaskCreateBody(projectId, title, content));
    connect(reply, &QNetworkReply::finished, this, [this, reply, clientRef]() {
        reply->deleteLater();
        QByteArray payload;
        if (!checkReply(reply, payload)) {
            emit taskCreateFailed(clientRef);
            return;
        }

        const Task task = parseTask(payload);
        if (task.id.isEmpty()) {
            // A 2xx with no id would otherwise leave the caller waiting on a
            // taskCreated that never arrives, with a note it can never upload.
            emit requestFailed(tr("TickTick did not return the new task."));
            emit taskCreateFailed(clientRef);
            return;
        }
        emit taskCreated(clientRef, task);
    });
}

void TickTickClient::fetchTasksInProject(const QString &projectId)
{
    QNetworkReply *reply =
        network()->post(jsonRequest(QStringLiteral("/task/filter"), m_accessToken),
                        buildTaskFilterBody(projectId));
    connect(reply, &QNetworkReply::finished, this, [this, reply, projectId]() {
        reply->deleteLater();
        QByteArray payload;
        if (!checkReply(reply, payload))
            return;

        const QList<Task> tasks = parseTaskList(payload);
        if (tasks.size() >= kLikelyResultCap)
            emit resultsPossiblyTruncated(tasks.size());
        emit tasksFetched(projectId, tasks);
    });
}
