#include <catch2/catch_test_macros.hpp>

#include "TickTickTaskList.h"

// TickTickTaskList is where the whole "one note, many tasks" design lives —
// pure and network-free by design (see its header), so every rule the
// aggregated task note follows is pinned here rather than only exercised
// live against a real TickTick account.

namespace {
TickTickClient::Task taskWithTitle(const QString &id, const QString &title)
{
    TickTickClient::Task task;
    task.id = id;
    task.title = title;
    return task;
}
}

SCENARIO("The note always opens with an empty compose slot on top", "[ticktick][tasklist]")
{
    GIVEN("no tasks at all")
    {
        THEN("the note is a single blank checkbox line, not an empty document")
        {
            // The slot is where a task is typed, so a brand new user with an
            // empty list still has somewhere to start.
            CHECK(TickTickTaskList::render({}) == "- [ ] ");
        }

        THEN("its one task id is the compose slot's empty id")
        {
            CHECK(TickTickTaskList::taskIdsFor({}) == QStringList{""});
        }
    }

    GIVEN("a compose slot the user has half-typed into")
    {
        THEN("a render carries that text through instead of blanking it")
        {
            // A scheduled pull landing mid-sentence must not wipe a task the
            // user is halfway through writing — unlike a task line, there is no
            // server copy to get it back from.
            CHECK(TickTickTaskList::render({}, "Call den")
                  == "- [ ] Call den");
        }
    }
}

SCENARIO("Open tasks render newest first, under the compose slot", "[ticktick][tasklist]")
{
    GIVEN("three open tasks in the order TickTick answered with")
    {
        const QList<TickTickClient::Task> tasks = {
            taskWithTitle("t1", "Buy milk"),
            taskWithTitle("t2", "Walk dog"),
            taskWithTitle("t3", "File taxes"),
        };

        THEN("the compose slot leads, then each task as its own unchecked line, reversed")
        {
            // Reversed (feature request: "the sort should be flipped") so the
            // newest tasks sit next to the slot they were created in.
            CHECK(TickTickTaskList::render(tasks)
                  == "- [ ] \n- [ ] File taxes\n- [ ] Walk dog\n- [ ] Buy milk");
        }

        THEN("the task ids are index-aligned with those lines, compose slot first")
        {
            // If these two ever disagreed about order, every rename and every
            // tick would address the wrong task.
            CHECK(TickTickTaskList::taskIdsFor(tasks) == QStringList{"", "t3", "t2", "t1"});
        }
    }

    GIVEN("a task with an empty title")
    {
        const QList<TickTickClient::Task> tasks = {taskWithTitle("t1", "")};

        THEN("it still produces a line rather than being dropped")
        {
            // A dropped line here would silently lose a task from the list the
            // user sees, and would desync every later index against
            // previousTaskIds for the rest of the session.
            CHECK(TickTickTaskList::render(tasks) == "- [ ] \n- [ ] ");
        }
    }

    GIVEN("a task whose title contains a newline")
    {
        const QList<TickTickClient::Task> tasks = {
            taskWithTitle("t1", "Buy\nmilk"),
            taskWithTitle("t2", "Walk dog"),
        };

        THEN("the title is flattened to a single line")
        {
            // A literal newline in the title would render as TWO checkbox items
            // for one task, corrupting the index alignment diff() relies on for
            // every task that follows it.
            const QString rendered = TickTickTaskList::render(tasks);
            CHECK_FALSE(rendered.contains("Buy\nmilk"));
            CHECK(TickTickTaskList::linesOf(rendered).size() == 3); // slot + 2 tasks
        }
    }
}

SCENARIO("The compose slot's in-progress text can be read back out", "[ticktick][tasklist]")
{
    GIVEN("a note whose slot has been typed into")
    {
        const QString text = "- [ ] Call dentist\n- [ ] Buy milk";

        THEN("composeTitleOf reports just the slot's label")
        {
            CHECK(TickTickTaskList::composeTitleOf(text) == "Call dentist");
        }
    }

    GIVEN("a note whose slot is empty")
    {
        THEN("composeTitleOf reports nothing to carry through")
        {
            CHECK(TickTickTaskList::composeTitleOf("- [ ] \n- [ ] Buy milk").isEmpty());
        }
    }

    GIVEN("an empty document, and one whose first line is not a checkbox")
    {
        THEN("both report nothing, rather than inventing a title from stray text")
        {
            CHECK(TickTickTaskList::composeTitleOf("").isEmpty());
            CHECK(TickTickTaskList::composeTitleOf("just some text").isEmpty());
        }
    }
}

SCENARIO("Committing the compose slot moves its text down and leaves a fresh slot",
         "[ticktick][tasklist]")
{
    GIVEN("a note with a typed slot and two existing tasks")
    {
        const QString text = "- [ ] Call dentist\n- [ ] Walk dog\n- [ ] Buy milk";

        WHEN("that slot's text is committed")
        {
            const QString committed =
                TickTickTaskList::withComposedTaskCommitted(text, "Call dentist");

            THEN("the new task becomes line 1 and line 0 is an empty slot again")
            {
                // The slot is emptied rather than removed, so a second task can
                // be typed the instant the first is committed.
                CHECK(committed
                      == "- [ ] \n- [ ] Call dentist\n- [ ] Walk dog\n- [ ] Buy milk");
            }
        }
    }
}

SCENARIO("Ticking a box is read back as a completion", "[ticktick][tasklist]")
{
    GIVEN("a compose slot and three rendered tasks")
    {
        const QStringList previousLines = {
            "- [ ] ", "- [ ] Buy milk", "- [ ] Walk dog", "- [ ] File taxes"};
        const QStringList previousTaskIds = {"", "t1", "t2", "t3"};

        WHEN("the first task line is ticked")
        {
            const QString edited =
                "- [ ] \n- [x] Buy milk\n- [ ] Walk dog\n- [ ] File taxes";
            const auto intent = TickTickTaskList::diff(previousLines, previousTaskIds, edited);

            THEN("exactly that task's id is reported completed, nothing else")
            {
                REQUIRE(intent.completedTaskIds.size() == 1);
                CHECK(intent.completedTaskIds.first() == "t1");
                CHECK(intent.renamedTasks.isEmpty());
                // A tick is a fully-interpreted edit — the caller's baseline
                // is safe to advance to it.
                CHECK(intent.understood);
            }
        }

        WHEN("the SECOND of the three task lines is ticked")
        {
            const QString edited =
                "- [ ] \n- [ ] Buy milk\n- [x] Walk dog\n- [ ] File taxes";
            const auto intent = TickTickTaskList::diff(previousLines, previousTaskIds, edited);

            THEN("the second task's id is reported, not the first's")
            {
                // The off-by-one that would complete the wrong task — and which
                // the compose slot shifting every task down by one makes easier
                // to get wrong, not harder.
                REQUIRE(intent.completedTaskIds.size() == 1);
                CHECK(intent.completedTaskIds.first() == "t2");
                CHECK(intent.understood);
            }
        }
    }
}

SCENARIO("Editing a line's label is read back as a rename", "[ticktick][tasklist]")
{
    GIVEN("a compose slot and two rendered tasks")
    {
        const QStringList previousLines = {"- [ ] ", "- [ ] Buy milk", "- [ ] Walk dog"};
        const QStringList previousTaskIds = {"", "t1", "t2"};

        WHEN("the first task line's text is changed, box left unticked")
        {
            const QString edited = "- [ ] \n- [ ] Buy oat milk\n- [ ] Walk dog";
            const auto intent = TickTickTaskList::diff(previousLines, previousTaskIds, edited);

            THEN("that task is reported renamed, with the new title")
            {
                REQUIRE(intent.renamedTasks.size() == 1);
                CHECK(intent.renamedTasks.first().first == "t1");
                CHECK(intent.renamedTasks.first().second == "Buy oat milk");
                CHECK(intent.completedTaskIds.isEmpty());
                // A rename is a fully-interpreted edit too.
                CHECK(intent.understood);
            }
        }
    }
}

SCENARIO("Typing in the compose slot changes nothing on TickTick", "[ticktick][tasklist]")
{
    // The slot has no task behind it, so reading its text as a rename would
    // rename whichever task happened to be listed first. Turning it into a real
    // task takes an explicit commit — see TickTickSync::commitComposedTask.
    GIVEN("a compose slot and two rendered tasks")
    {
        const QStringList previousLines = {"- [ ] ", "- [ ] Buy milk", "- [ ] Walk dog"};
        const QStringList previousTaskIds = {"", "t1", "t2"};

        WHEN("the user types a new task title into the slot")
        {
            const QString edited = "- [ ] Call dentist\n- [ ] Buy milk\n- [ ] Walk dog";
            const auto intent = TickTickTaskList::diff(previousLines, previousTaskIds, edited);

            THEN("no intent at all is reported, but the edit was fully understood")
            {
                CHECK(intent.completedTaskIds.isEmpty());
                CHECK(intent.renamedTasks.isEmpty());
                CHECK(intent.removedIndices.isEmpty());
                // "Nothing to send" here is a deliberate reading, not a
                // refusal — the caller's baseline is safe to advance to this
                // text (see TickTickSync::flushPendingUploads). This is what
                // stops a tick moments later from diffing against stale slot
                // text and being dropped as "too ambiguous".
                CHECK(intent.understood);
            }
        }

        WHEN("the slot's own box is somehow ticked")
        {
            const QString edited = "- [x] Call dentist\n- [ ] Buy milk\n- [ ] Walk dog";
            const auto intent = TickTickTaskList::diff(previousLines, previousTaskIds, edited);

            THEN("still nothing is reported — there is no task to complete")
            {
                CHECK(intent.completedTaskIds.isEmpty());
                CHECK(intent.renamedTasks.isEmpty());
                CHECK(intent.understood);
            }
        }

        WHEN("a half-typed slot is cleared back to a blank line")
        {
            const QStringList typedLines = {
                "- [ ] Call dentist", "- [ ] Buy milk", "- [ ] Walk dog"};
            const QStringList typedTaskIds = {"", "t1", "t2"};
            const QString edited = "- [ ] \n- [ ] Buy milk\n- [ ] Walk dog";
            const auto intent = TickTickTaskList::diff(typedLines, typedTaskIds, edited);

            THEN("no intent is reported, and clearing is understood just like typing was")
            {
                CHECK(intent.completedTaskIds.isEmpty());
                CHECK(intent.renamedTasks.isEmpty());
                CHECK(intent.removedIndices.isEmpty());
                CHECK(intent.understood);
            }
        }
    }
}

SCENARIO("An extra line is never read as a create", "[ticktick][tasklist]")
{
    // Creates come from commitComposedTask alone now. With Enter no longer
    // inserting newlines in this note (feature request: "enter should be
    // disabled"), an extra line can only come from a paste or an undo — neither
    // of which means "make a task".
    GIVEN("a compose slot and two rendered tasks")
    {
        const QStringList previousLines = {"- [ ] ", "- [ ] Buy milk", "- [ ] Walk dog"};
        const QStringList previousTaskIds = {"", "t1", "t2"};

        WHEN("a third line appears at the end")
        {
            const QString edited =
                "- [ ] \n- [ ] Buy milk\n- [ ] Walk dog\n- [ ] Call dentist";
            const auto intent = TickTickTaskList::diff(previousLines, previousTaskIds, edited);

            THEN("nothing is reported, and the next pull is what straightens the note out")
            {
                CHECK(intent.completedTaskIds.isEmpty());
                CHECK(intent.renamedTasks.isEmpty());
                CHECK(intent.removedIndices.isEmpty());
                // An extra line (undo of a removal, or a paste) is exactly the
                // case diff() must NOT claim to have understood — the caller
                // must not advance its baseline to this text, or the note
                // stays permanently desynced until the next pull happens to
                // land. See TickTickSync::flushPendingUploads.
                CHECK_FALSE(intent.understood);
            }
        }
    }
}

SCENARIO("Deleting a line completes that task", "[ticktick][tasklist]")
{
    // Decision, drain 57a85c8d, modification 3: a line going away means the
    // user is done with it — routed through TickTickSync::completeTask, never a
    // remote delete. This is the same completedTaskIds bucket a tick uses; the
    // caller (TickTickSync) dispatches both the same way. Backspacing an
    // emptied-out line away (NoteWindow.tryRemoveTaskLine) arrives here too.
    GIVEN("a compose slot and three rendered tasks")
    {
        const QStringList previousLines = {
            "- [ ] ", "- [ ] Buy milk", "- [ ] Walk dog", "- [ ] File taxes"};
        const QStringList previousTaskIds = {"", "t1", "t2", "t3"};

        WHEN("the middle task line is deleted outright")
        {
            const QString edited = "- [ ] \n- [ ] Buy milk\n- [ ] File taxes";
            const auto intent = TickTickTaskList::diff(previousLines, previousTaskIds, edited);

            THEN("the removed task is completed, and removedIndices reports its "
                 "PREVIOUS index (2), not just its id")
            {
                REQUIRE(intent.completedTaskIds.size() == 1);
                CHECK(intent.completedTaskIds.first() == "t2");
                CHECK(intent.renamedTasks.isEmpty());
                CHECK(intent.removedIndices == QList<int>{2});
                CHECK(intent.understood);
            }
        }
    }
}

SCENARIO("Losing the compose slot completes nothing", "[ticktick][tasklist]")
{
    // Backspace refuses to delete the slot (NoteWindow.tryRemoveTaskLine), so
    // this covers a paste or an undo taking it out. It still has to land in
    // removedIndices, or the caller's ids stop being index-aligned.
    GIVEN("a compose slot and two rendered tasks")
    {
        const QStringList previousLines = {"- [ ] ", "- [ ] Buy milk", "- [ ] Walk dog"};
        const QStringList previousTaskIds = {"", "t1", "t2"};

        WHEN("the slot line disappears")
        {
            const QString edited = "- [ ] Buy milk\n- [ ] Walk dog";
            const auto intent = TickTickTaskList::diff(previousLines, previousTaskIds, edited);

            THEN("no task is completed, but the removal is still reported")
            {
                CHECK(intent.completedTaskIds.isEmpty());
                CHECK(intent.removedIndices == QList<int>{0});
                CHECK(intent.understood);
            }
        }
    }
}

SCENARIO("Ticking several boxes in one burst removes and completes all of them",
         "[ticktick][tasklist]")
{
    // This is what the completion fade actually buys: without it, anything but
    // a single removal fell through to "no intent", so a burst of ticks would
    // vanish locally and stay open in TickTick until the next pull put them
    // back.
    GIVEN("a compose slot and four rendered tasks")
    {
        const QStringList previousLines = {"- [ ] ",
                                           "- [ ] Buy milk",
                                           "- [ ] Walk dog",
                                           "- [ ] File taxes",
                                           "- [ ] Call dentist"};
        const QStringList previousTaskIds = {"", "t1", "t2", "t3", "t4"};

        WHEN("three of the four task lines are removed in one edit")
        {
            const QString edited = "- [ ] \n- [ ] Walk dog";
            const auto intent = TickTickTaskList::diff(previousLines, previousTaskIds, edited);

            THEN("all three removed tasks' ids are reported, in order, and "
                 "removedIndices lists their three previous indices")
            {
                CHECK(intent.completedTaskIds == QStringList{"t1", "t3", "t4"});
                CHECK(intent.removedIndices == QList<int>{1, 3, 4});
                CHECK(intent.renamedTasks.isEmpty());
                CHECK(intent.understood);
            }
        }
    }
}

SCENARIO("A line that was already ticked in the previous render does not complete twice",
         "[ticktick][tasklist]")
{
    // A ticked line in the previous render can only mean a flush already saw it
    // and dispatched completeTask for it (render() never writes "[x]" — see
    // TickTickTaskList::render). If its removal reported ANOTHER completion, the
    // task would be completed twice.
    GIVEN("three rendered tasks, the middle one already ticked from a prior flush")
    {
        const QStringList previousLines = {
            "- [ ] Buy milk", "- [x] Walk dog", "- [ ] File taxes"};
        const QStringList previousTaskIds = {"t1", "t2", "t3"};

        WHEN("the already-ticked line is removed (the completion fade took it away)")
        {
            const QString edited = "- [ ] Buy milk\n- [ ] File taxes";
            const auto intent = TickTickTaskList::diff(previousLines, previousTaskIds, edited);

            THEN("no completion is reported for it, but removedIndices still reports the "
                 "removal — so the caller's remembered id array still moves")
            {
                // The first half: don't complete twice.
                CHECK(intent.completedTaskIds.isEmpty());
                // The second half: the baseline still has to move, or the ids
                // desync from the lines for the rest of the session.
                CHECK(intent.removedIndices == QList<int>{1});
                CHECK(intent.understood);
            }
        }
    }
}

SCENARIO("Removing one of two lines with identical text is too ambiguous to act on",
         "[ticktick][tasklist]")
{
    // Two open tasks can share a title, so which one the remaining line refers
    // to is genuinely unknowable from the text alone — acting on a guess risks
    // completing the wrong task.
    GIVEN("two open tasks that render identically")
    {
        const QStringList previousLines = {"- [ ] milk", "- [ ] milk"};
        const QStringList previousTaskIds = {"t1", "t2"};

        WHEN("one of the two identical lines is removed")
        {
            const QString edited = "- [ ] milk";
            const auto intent = TickTickTaskList::diff(previousLines, previousTaskIds, edited);

            THEN("no intent at all is reported, and removedIndices is empty")
            {
                // This is the property that stops the wrong task being
                // completed: neither answer is safer than the other, so neither
                // is guessed.
                CHECK(intent.completedTaskIds.isEmpty());
                CHECK(intent.removedIndices.isEmpty());
                CHECK(intent.renamedTasks.isEmpty());
                CHECK_FALSE(intent.understood);
            }
        }
    }
}

SCENARIO("Removing a line and editing another in the same edit is not a pure deletion",
         "[ticktick][tasklist]")
{
    GIVEN("three rendered tasks")
    {
        const QStringList previousLines = {
            "- [ ] Buy milk", "- [ ] Walk dog", "- [ ] File taxes"};
        const QStringList previousTaskIds = {"t1", "t2", "t3"};

        WHEN("the middle line is removed AND the remaining first line is also edited")
        {
            const QString edited = "- [ ] Buy oat milk\n- [ ] File taxes";
            const auto intent = TickTickTaskList::diff(previousLines, previousTaskIds, edited);

            THEN("no intent is reported at all — the left-greedy walk ends with leftover "
                 "current lines it could not match")
            {
                CHECK(intent.completedTaskIds.isEmpty());
                CHECK(intent.renamedTasks.isEmpty());
                CHECK(intent.removedIndices.isEmpty());
                CHECK_FALSE(intent.understood);
            }
        }
    }
}

SCENARIO("Reordering lines is read as no intent at all", "[ticktick][tasklist]")
{
    // The safety property that stops the wrong task being completed or renamed:
    // a same-length document where more than one line moved is
    // indistinguishable from a guess, so nothing is dispatched and the next pull
    // is what straightens the note back out.
    GIVEN("three rendered tasks")
    {
        const QStringList previousLines = {
            "- [ ] Buy milk", "- [ ] Walk dog", "- [ ] File taxes"};
        const QStringList previousTaskIds = {"t1", "t2", "t3"};

        WHEN("the first two lines are swapped")
        {
            const QString edited =
                "- [ ] Walk dog\n- [ ] Buy milk\n- [ ] File taxes";
            const auto intent = TickTickTaskList::diff(previousLines, previousTaskIds, edited);

            THEN("nothing is reported — not a completion, rename, or removal")
            {
                CHECK(intent.completedTaskIds.isEmpty());
                CHECK(intent.renamedTasks.isEmpty());
                CHECK(intent.removedIndices.isEmpty());
                // A swap is exactly the "indistinguishable from a guess" case
                // — the caller must hold its baseline where it is and
                // reconcile with a pull rather than pretend this text is now
                // agreed upon.
                CHECK_FALSE(intent.understood);
            }
        }
    }
}

SCENARIO("An untouched document produces an empty Intent", "[ticktick][tasklist]")
{
    GIVEN("rendered tasks with no edits applied")
    {
        const QStringList previousLines = {"- [ ] ", "- [ ] Buy milk", "- [ ] Walk dog"};
        const QStringList previousTaskIds = {"", "t1", "t2"};
        const QString unchanged = "- [ ] \n- [ ] Buy milk\n- [ ] Walk dog";

        THEN("nothing is reported, so an idle save uploads nothing")
        {
            const auto intent = TickTickTaskList::diff(previousLines, previousTaskIds, unchanged);
            CHECK(intent.completedTaskIds.isEmpty());
            CHECK(intent.renamedTasks.isEmpty());
            CHECK(intent.removedIndices.isEmpty());
            // Genuinely unchanged is understood by definition — there is
            // nothing here for the caller to reconcile.
            CHECK(intent.understood);
        }
    }

    GIVEN("no tasks and an empty document")
    {
        THEN("an empty document still diffs as unchanged, not as N removals")
        {
            // linesOf("") is zero lines, matching an empty previousTaskIds — if
            // these two disagreed (as a bare QString::split("\n") would), an
            // idle empty note would misread as a completion every time.
            const auto intent = TickTickTaskList::diff({}, {}, "");
            CHECK(intent.completedTaskIds.isEmpty());
            CHECK(intent.renamedTasks.isEmpty());
            CHECK(intent.removedIndices.isEmpty());
            CHECK(intent.understood);
        }
    }
}

SCENARIO("A tick's completion depends on whether the baseline has already moved past a "
         "compose-slot edit",
         "[ticktick][tasklist]")
{
    // The regression this item fixes, expressed at the diff() level with no
    // TickTickSync fixture needed: typing into the compose slot and then
    // ticking a different task must complete that task PROVIDED the caller
    // advanced its baseline for the understood slot edit — see
    // TickTickSync::flushPendingUploads. Against a baseline that never moved
    // (today's bug), the very next edit diffs against TWO changed lines and
    // is read as no intent at all.
    GIVEN("a compose slot and one open task")
    {
        const QStringList renderedLines = {"- [ ] ", "- [ ] Walk dog"};
        const QStringList renderedTaskIds = {"", "id-walk"};

        WHEN("the slot is typed into, then a baseline that advanced with that edit is diffed "
             "against a tick of the task")
        {
            const QStringList baselineAfterSlotEdit = {"- [ ] Call dentist", "- [ ] Walk dog"};
            const QString tickedText = "- [ ] Call dentist\n- [x] Walk dog";
            const auto intent =
                TickTickTaskList::diff(baselineAfterSlotEdit, renderedTaskIds, tickedText);

            THEN("the task completes")
            {
                CHECK(intent.completedTaskIds == QStringList{"id-walk"});
                CHECK(intent.understood);
            }
        }

        WHEN("the same tick is diffed against the STALE pre-edit baseline instead")
        {
            const QString tickedText = "- [ ] Call dentist\n- [x] Walk dog";
            const auto intent = TickTickTaskList::diff(renderedLines, renderedTaskIds, tickedText);

            THEN("the task STILL completes — the compose slot's own change does not count "
                 "toward the one-changed-line rule, so the tick is the only real edit "
                 "either way")
            {
                // This used to read as two changed lines and be refused, which
                // meant a rename or tick made while the slot held half-typed
                // text was silently dropped whenever the baseline had not yet
                // advanced past the slot edit (and, worse, at commit time —
                // where the slot is ALWAYS mid-edit). Exempting empty-id lines
                // from the count makes both orderings agree.
                CHECK(intent.completedTaskIds == QStringList{"id-walk"});
                CHECK(intent.understood);
            }
        }
    }
}
