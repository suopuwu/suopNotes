#pragma once

#include <QList>
#include <QObject>
#include <QString>

class QNetworkAccessManager;

// Thin wrapper over the TickTick Open API. Knows the wire format and nothing
// about notes — mapping tasks to notes is TickTickSync's job.
//
// Everything here was checked against the live API rather than its docs, which
// are incomplete in both directions (they omit expires_in, and they predate the
// task/filter endpoint that most third-party summaries still lack).
class TickTickClient : public QObject
{
    Q_OBJECT

public:
    explicit TickTickClient(QObject *parent = nullptr);

    struct Project {
        QString id;
        QString name;
    };

    struct Task {
        QString id;
        QString projectId;
        QString title;
        QString content;
        // TickTick's own encoding: 0 = normal, 2 = completed.
        int status = 0;
    };

    void setAccessToken(const QString &accessToken);
    bool hasAccessToken() const;

    // Test seam: replaces the lazily-created QNetworkAccessManager, so specs
    // can observe outbound requests and serve canned replies instead of every
    // test run knocking on the live TickTick API. Takes ownership. Production
    // never calls this — the default manager is created on first use.
    void setNetworkAccessManager(QNetworkAccessManager *manager);

    // GET /open/v1/project
    void fetchProjects();
    // POST /open/v1/project  — used once, to create the notes project.
    void createProject(const QString &name);
    // POST /open/v1/task/filter, scoped to one project.
    void fetchTasksInProject(const QString &projectId);
    // POST /open/v1/task — creates a task and reports back the one TickTick
    // minted, id included.
    //
    // clientRef is echoed straight back on taskCreated and never sent over the
    // wire. The API offers no correlation token of its own, and the caller has
    // already opened a local note by the time this answers — it needs to know
    // WHICH note the returned task id belongs to, and with several creations
    // possibly in flight, reply order cannot be assumed to match call order.
    void createTask(const QString &clientRef, const QString &projectId,
                    const QString &title, const QString &content);
    // POST /open/v1/task/{id} — updates title and content only. Every other
    // field is left out of the body so a due date, priority or tag the user set
    // in TickTick is not clobbered by a note edit that knows nothing about it.
    void updateTask(const QString &taskId, const QString &projectId,
                    const QString &title, const QString &content);
    // POST /open/v1/project/{projectId}/task/{taskId}/complete — TickTick's
    // dedicated completion endpoint, with an empty body.
    //
    // Deliberately NOT a status field on updateTask: that body is kept to
    // title and content precisely so a note edit cannot clobber a due date or
    // priority (see above), and the update endpoint is not documented to honor
    // status at all. A separate request keeps both properties.
    void completeTask(const QString &projectId, const QString &taskId);

    // --- pure helpers ---

    static QByteArray buildProjectBody(const QString &name);
    static QByteArray buildTaskFilterBody(const QString &projectId);
    static QByteArray buildTaskCreateBody(const QString &projectId, const QString &title,
                                          const QString &content);
    static QList<Project> parseProjectList(const QByteArray &json);
    static QList<Task> parseTaskList(const QByteArray &json);
    static Project parseProject(const QByteArray &json);
    static Task parseTask(const QByteArray &json);

    static QByteArray buildTaskUpdateBody(const QString &taskId, const QString &projectId,
                                          const QString &title, const QString &content);

    // The path segment for a completion, split out so its shape is unit-
    // testable without a network round trip — the same reason every other
    // body builder here is static.
    static QString completeTaskPath(const QString &projectId, const QString &taskId);

    // Largest content that is known to survive the round trip. 200KB was
    // verified byte-identical against the live API; 500KB failed with an HTTP
    // 500. Guarding here matters because that 500 is PERMANENT — a 500 normally
    // invites a retry, and retrying this one would spin forever on a note that
    // can never be accepted.
    static constexpr int kMaxContentBytes = 200000;

    // The observed ceiling on a single response. Three unrelated broad queries
    // all came back at exactly 200 against an account with far fewer notes, and
    // the API documents no paging, so a full 200 means results were very likely
    // truncated rather than that the account holds exactly 200.
    static constexpr int kLikelyResultCap = 200;

signals:
    void projectsFetched(const QList<TickTickClient::Project> &projects);
    void projectCreated(const TickTickClient::Project &project);
    // Carries the project id the fetch was FOR (the request's own scope, not
    // anything read back from the payload): task lists can be empty, so the
    // payload alone cannot say which list answered — and the caller needs to
    // know, or a response landing after the user switched lists would be
    // rendered as if it belonged to the new one.
    void tasksFetched(const QString &projectId, const QList<TickTickClient::Task> &tasks);
    void taskUpdated(const QString &taskId);
    // A completion TickTick accepted. Carries the task id because the reply
    // body is empty, so there is nothing else to identify it by.
    void taskCompleted(const QString &taskId);
    // clientRef is whatever createTask() was handed; see there.
    void taskCreated(const QString &clientRef, const TickTickClient::Task &task);
    // A create that did not produce a task. Emitted alongside requestFailed
    // (which carries the human-readable reason) rather than instead of it: the
    // caller needs to know WHICH pending create is no longer in flight, or it
    // can never safely retry that one without risking a duplicate task.
    void taskCreateFailed(const QString &clientRef);
    // Any failure, already turned into something worth showing a user.
    void requestFailed(const QString &reason);
    // A failure that will never succeed if repeated. Kept separate from
    // requestFailed so the caller knows to stop rather than queue a retry.
    void taskRejectedPermanently(const QString &taskId, const QString &reason);
    // Emitted when a response came back at exactly the cap — the caller should
    // say so rather than silently presenting a partial list as complete.
    void resultsPossiblyTruncated(int returnedCount);

private:
    QNetworkAccessManager *network();
    // Shared reply handling: turns transport errors, HTTP errors and TickTick's
    // own JSON error bodies into one requestFailed() with a usable message.
    // Returns false when the reply was an error and has been reported.
    bool checkReply(class QNetworkReply *reply, QByteArray &payloadOut);

    QNetworkAccessManager *m_network = nullptr;
    QString m_accessToken;
};
