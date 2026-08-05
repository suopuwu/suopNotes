#include <catch2/catch_test_macros.hpp>

#include "TickTickClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>

// Wire format for the Open API. Verified against the live service, which does
// not always match its documentation.

SCENARIO("A task pull is scoped to one project and to open tasks only",
         "[ticktick][client]")
{
    GIVEN("a project id")
    {
        const QByteArray body = TickTickClient::buildTaskFilterBody("proj-1");
        const QJsonObject object = QJsonDocument::fromJson(body).object();

        THEN("only that project is queried")
        {
            // Everything is scoped to the app's own project so the integration
            // never touches the user's other tasks.
            const QJsonArray ids = object.value("projectIds").toArray();
            REQUIRE(ids.size() == 1);
            CHECK(ids.at(0).toString() == "proj-1");
        }

        THEN("only open tasks are requested")
        {
            // Completing a task in TickTick is how the user says they are done
            // with it, so its note stops being shown — the pull's reconcile
            // then closes the window. Asking for status 2 as well (as an
            // earlier build did) left checked-off tasks on screen with no way
            // to dismiss them short of deleting them outright.
            const QJsonArray status = object.value("status").toArray();
            REQUIRE(status.size() == 1);
            CHECK(status.at(0).toInt() == 0);
        }
    }
}

SCENARIO("Project responses are read", "[ticktick][client]")
{
    GIVEN("a project list")
    {
        const auto projects = TickTickClient::parseProjectList(
            R"([{"id":"p1","name":"suopNotes"},{"id":"p2","name":"Groceries"}])");

        THEN("each project's id and name are taken")
        {
            REQUIRE(projects.size() == 2);
            CHECK(projects.at(0).id == "p1");
            CHECK(projects.at(0).name == "suopNotes");
            CHECK(projects.at(1).name == "Groceries");
        }
    }

    GIVEN("a list containing an entry with no id")
    {
        const auto projects =
            TickTickClient::parseProjectList(R"([{"name":"nameless"},{"id":"p2","name":"ok"}])");

        THEN("the unusable entry is dropped rather than kept as a blank")
        {
            REQUIRE(projects.size() == 1);
            CHECK(projects.at(0).id == "p2");
        }
    }

    GIVEN("a response that is not an array")
    {
        THEN("nothing is returned and nothing crashes")
        {
            CHECK(TickTickClient::parseProjectList("{}").isEmpty());
            CHECK(TickTickClient::parseProjectList("<html>502</html>").isEmpty());
        }
    }

    GIVEN("a single created project")
    {
        const auto project = TickTickClient::parseProject(R"({"id":"p9","name":"suopNotes"})");

        THEN("it is read")
        {
            CHECK(project.id == "p9");
            CHECK(project.name == "suopNotes");
        }
    }
}

SCENARIO("Task responses are read from either shape the API returns",
         "[ticktick][client]")
{
    GIVEN("the bare array that /task/filter returns")
    {
        const auto tasks = TickTickClient::parseTaskList(
            R"([{"id":"t1","projectId":"p1","title":"Shopping","content":"milk","status":0}])");

        THEN("the fields that make up a note are taken")
        {
            REQUIRE(tasks.size() == 1);
            CHECK(tasks.at(0).id == "t1");
            CHECK(tasks.at(0).projectId == "p1");
            CHECK(tasks.at(0).title == "Shopping");
            CHECK(tasks.at(0).content == "milk");
            CHECK(tasks.at(0).status == 0);
        }
    }

    GIVEN("the wrapped object that a project's data endpoint returns")
    {
        const auto tasks = TickTickClient::parseTaskList(
            R"({"project":{"id":"p1"},"tasks":[{"id":"t1","title":"One"}]})");

        THEN("the tasks are found inside it")
        {
            // Accepting both shapes means callers do not have to track which
            // endpoint produced a payload.
            REQUIRE(tasks.size() == 1);
            CHECK(tasks.at(0).id == "t1");
        }
    }

    GIVEN("a completed task")
    {
        const auto tasks = TickTickClient::parseTaskList(R"([{"id":"t1","status":2}])");

        THEN("its completed status is preserved")
        {
            REQUIRE(tasks.size() == 1);
            CHECK(tasks.at(0).status == 2);
        }
    }

    GIVEN("an entry with no id")
    {
        const auto tasks = TickTickClient::parseTaskList(R"([{"title":"orphan"}])");

        THEN("it is dropped — there would be nothing to map it to")
        {
            CHECK(tasks.isEmpty());
        }
    }

    GIVEN("junk")
    {
        THEN("nothing is returned")
        {
            CHECK(TickTickClient::parseTaskList("").isEmpty());
            CHECK(TickTickClient::parseTaskList("not json").isEmpty());
        }
    }
}

SCENARIO("Creating a task lets TickTick mint the id", "[ticktick][client]")
{
    GIVEN("a create body")
    {
        const QByteArray body = TickTickClient::buildTaskCreateBody("p1", "Shopping", "milk");
        const QJsonObject object = QJsonDocument::fromJson(body).object();

        THEN("the target list, title and content are sent")
        {
            CHECK(object.value("projectId").toString() == "p1");
            CHECK(object.value("title").toString() == "Shopping");
            CHECK(object.value("content").toString() == "milk");
        }

        THEN("no id is sent")
        {
            // This is the one request where the id comes back rather than
            // going out — sending one would be inventing a task id locally.
            CHECK_FALSE(object.contains("id"));
        }
    }

    GIVEN("the task TickTick returns")
    {
        const auto task = TickTickClient::parseTask(
            R"({"id":"t9","projectId":"p1","title":"Shopping","content":"milk","status":0})");

        THEN("its freshly minted id is read")
        {
            // Without this id the local note could never be updated again —
            // every later edit would have nothing to address.
            CHECK(task.id == "t9");
            CHECK(task.projectId == "p1");
            CHECK(task.title == "Shopping");
        }
    }

    GIVEN("a create response that is not an object")
    {
        THEN("no task is produced, rather than a blank one")
        {
            // A task with an empty id is what the caller checks for to decide
            // the create failed, so junk must not parse into something that
            // looks almost usable.
            CHECK(TickTickClient::parseTask("[]").id.isEmpty());
            CHECK(TickTickClient::parseTask("<html>502</html>").id.isEmpty());
        }
    }
}

SCENARIO("Completing a task addresses TickTick's own completion endpoint",
         "[ticktick][client]")
{
    GIVEN("a project id and a task id")
    {
        THEN("the path is the completion endpoint")
        {
            CHECK(TickTickClient::completeTaskPath("p1", "t1") == "/project/p1/task/t1/complete");
        }

        THEN("it is not the update endpoint")
        {
            // Completion is a distinct request rather than a field on the
            // update body — see buildTaskUpdateBody below for why.
            CHECK(TickTickClient::completeTaskPath("p1", "t1") != "/task/t1");
        }

        THEN("an update body still carries no status field")
        {
            // This is the regression that matters: it pins the rule that a
            // note edit must never touch status/priority/due date, which is
            // exactly why completion needed a separate endpoint at all.
            const QByteArray body = TickTickClient::buildTaskUpdateBody("t1", "p1", "T", "C");
            CHECK_FALSE(QJsonDocument::fromJson(body).object().contains("status"));
        }
    }
}

SCENARIO("A task update sends only the fields a note knows about",
         "[ticktick][client]")
{
    GIVEN("an update body")
    {
        const QByteArray body =
            TickTickClient::buildTaskUpdateBody("t1", "p1", "Shopping", "milk");
        const QJsonObject object = QJsonDocument::fromJson(body).object();

        THEN("the task is identified by id and project")
        {
            CHECK(object.value("id").toString() == "t1");
            CHECK(object.value("projectId").toString() == "p1");
        }

        THEN("title and content are sent")
        {
            CHECK(object.value("title").toString() == "Shopping");
            CHECK(object.value("content").toString() == "milk");
        }

        THEN("nothing else is sent")
        {
            // A note knows nothing about due dates, priorities, tags or
            // reminders. Including them would overwrite whatever the user set
            // in TickTick with blanks on the next keystroke.
            CHECK(object.keys() == QStringList{"content", "id", "projectId", "title"});
        }
    }
}

SCENARIO("Content too large for TickTick is refused rather than retried",
         "[ticktick][client]")
{
    GIVEN("a client and a note larger than the verified ceiling")
    {
        TickTickClient client;
        client.setAccessToken("token");

        QSignalSpy rejectedSpy(&client, &TickTickClient::taskRejectedPermanently);

        WHEN("it is uploaded")
        {
            client.updateTask("t1", "p1", "Title",
                              QString(TickTickClient::kMaxContentBytes + 1, QLatin1Char('x')));

            THEN("it is rejected as permanent, before any request is made")
            {
                // TickTick answers oversize content with an HTTP 500, which is
                // indistinguishable from a transient fault — so a naive queue
                // would retry it forever on a note that can never be accepted.
                REQUIRE(rejectedSpy.count() == 1);
                CHECK(rejectedSpy.at(0).at(0).toString() == "t1");
                CHECK_FALSE(rejectedSpy.at(0).at(1).toString().isEmpty());
            }
        }
    }
}
