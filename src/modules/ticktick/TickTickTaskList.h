#pragma once

#include "TickTickClient.h"

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

// The aggregated task note's content, in both directions.
//
// Pure and static on purpose: this is where the whole "one note, many tasks"
// design actually lives, and it must be testable without a network, a cache
// or an event loop — the same reason TickTickClient's body builders and
// TickTickModule's id helpers are static.
namespace TickTickTaskList
{

// The index of the compose slot — the always-present blank checkbox line at
// the very top of the note that the user types a new task into (feature
// request: "let's add an empty task at the top so the user can easily add a
// new task"). It is line 0 of every render, it has no task behind it, and
// composeTaskId() below is the id reserved for saying so.
//
// It is a real line of the note's text rather than a piece of chrome drawn
// above it because the note IS a plain-text editor: there is nowhere else to
// put a typable line. Everything that could go wrong with that — a pull
// wiping half-typed text, a stray edit being read as a rename of whatever
// task happens to be first — is handled by the two rules this file enforces:
// render() carries the in-progress text through, and diff() never derives any
// intent at all from this line. Turning what is typed here into a real task is
// the one thing that does NOT happen automatically: it takes an explicit
// commit (Enter, or the check button the line's checkbox becomes), which is
// TickTickSync::commitComposedTask.
constexpr int kComposeIndex = 0;

// The task id reserved for the compose slot, i.e. "this line has no task
// behind it". Empty, so an index-aligned id array can carry it without any
// separate bookkeeping, and so `previousTaskIds.at(i).isEmpty()` is the one
// test everything uses for "don't touch this line".
QString composeTaskId();

// Renders the note: the compose slot first, carrying `composeTitle` (whatever
// the user had already typed into it — empty for a plain blank slot), then one
// GFM checkbox line per open task, NEWEST FIRST.
//
// Newest first (feature request: "the sort should be flipped") means the
// reverse of the order TickTick answers with, which puts freshly-created tasks
// next to the slot they were created in rather than at the far end of the list.
//
// Every line is "- [ ] <title>"; nothing is ever rendered ticked, because the
// pull only asks for open tasks (see TickTickClient::buildTaskFilterBody) — a
// ticked line in this note always means "the user just ticked it".
QString render(const QList<TickTickClient::Task> &tasks, const QString &composeTitle = QString());

// The task ids for render()'s output, index-aligned with its lines: the
// compose slot's empty id, then each task's id newest-first. Paired with
// render() rather than left to the caller so the reversal and the leading slot
// are decided in exactly one place — those two arrays drifting apart is the
// one way this whole feature breaks.
QStringList taskIdsFor(const QList<TickTickClient::Task> &tasks);

// The text currently sitting in `text`'s compose slot, so a re-render can
// carry it through instead of wiping what the user was midway through typing.
// Returns an empty string when the note has no compose slot yet (an empty
// document, or one whose first line is somehow not a checkbox — either way
// there is nothing in progress to preserve).
QString composeTitleOf(const QString &text);

// The note text that commits `title` out of the compose slot: a fresh empty
// slot on top, `title` as a new task line directly under it, and every other
// line of `currentText` unchanged below that. Pure, so the exact text a commit
// produces is pinned by a test rather than only observable through a live sync
// — see TickTickSync::commitComposedTask for the surrounding bookkeeping.
QString withComposedTaskCommitted(const QString &currentText, const QString &title);

// Splits rendered/edited note text into lines the same way everywhere in
// this module: an empty string is ZERO lines, not one empty line. Plain
// QString::split("\n") disagrees with that for "" (it returns one empty
// element), which would desync a line count against an empty task list's
// task-id array (always genuinely empty) the moment nothing is open. Both
// TickTickSync (building the remembered render) and diff() below (reading
// the edited text) must agree on this, or the two arrays this whole feature
// leans on stop being index-aligned.
QStringList linesOf(const QString &text);

// What the user did to the note, derived by comparing the text now against
// the text last rendered plus the task ids that produced it.
//
// completedTaskIds covers TWO different gestures that both mean "I'm done
// with this task" (Decision, drain 57a85c8d, modification 3): ticking the
// box (the line stays, now showing "[x]"), and deleting the whole line
// outright (the line is gone). Both are dispatched through
// TickTickSync::completeTask — nothing here ever asks TickTick to DELETE a
// task; completing is the only destructive-looking action this note can
// cause, and even that only closes the task, never removes it.
//
// There is deliberately no "created" member. A create is no longer something
// this function can infer — it is an explicit gesture (see kComposeIndex), and
// TickTickSync::commitComposedTask is its only entry point. That removes the
// whole guess this used to make: "one extra line at the end" now happens for
// reasons that are not a new task at all (a paste, an undo), and with Enter no
// longer inserting newlines in this note there is no keystroke left that could
// legitimately mean it.
struct Intent
{
    QStringList completedTaskIds;                 // lines ticked, or lines removed outright
    QList<QPair<QString, QString>> renamedTasks;  // {taskId, newTitle}
    // Indices INTO THE PREVIOUS RENDER of every line that is gone now,
    // ascending — whether or not it produced a completion. The caller drops
    // exactly these from its remembered task ids so lines and ids stay
    // index-aligned; a removal that completes nothing (an already-ticked line,
    // see diff) still has to be dropped, and inferring the index back from a
    // task id cannot do that.
    QList<int> removedIndices;
    // Whether diff() fully interpreted the edit, even when that interpretation
    // was "there is deliberately nothing to send" (typing into the compose
    // slot, or the text simply not having changed at all). False means diff()
    // gave up rather than guess (a reorder, several lines touched at once, an
    // ambiguous removal, or a paste/undo that added lines) — in that case the
    // caller's remembered baseline no longer matches the note and MUST NOT be
    // advanced to currentLines, unlike the true case, where advancing it is
    // exactly what keeps the next edit diffing against real text instead of
    // stale text. See TickTickSync::flushPendingUploads, the only reader.
    bool understood = false;
};

// `previousLines`/`previousTaskIds` are the last render, index-aligned (see
// linesOf above for how "no lines" is represented). `currentText` is the
// note's live text right now.
//
// The index-based rule this implements, in order of how much it trusts the
// edit:
//  - Line count unchanged, and every line WITH A TASK BEHIND IT that differs
//    from its previous counterpart at the same index gained an "x" (nothing
//    else about it changed) → each such line is a completion. Trusted for any
//    number of lines at once: each is read against itself at its own index,
//    never against another line, so there is nothing to guess even when
//    several gained an "x" in the same edit — the routine case being a
//    second box ticked before the completion fade has removed the first (see
//    NoteWindow.qml's tickTickCompletionFade). A single differing line that
//    ISN'T a pure tick (a rename, an uncheck) is trusted only when it is the
//    ONLY real line that differs; more than one real line differing that
//    isn't explained purely by ticks (e.g. two lines swapped, or a tick
//    alongside an unrelated rename) is indistinguishable from a guess and
//    falls through to "no intent" below instead. A differing line whose
//    previous id is empty (the compose slot) never counts toward any of
//    this: typing there is already defined as unsendable, so it must not be
//    able to make a clean single-line rename alongside it look ambiguous.
//  - Any number of lines removed, provided the remaining lines embed into the
//    previous ones UNAMBIGUOUSLY (the same leftmost/rightmost-match agreement
//    a whole burst of ticks needs — see alignRemoval) → every removed line's
//    task is a completion (see Intent's own comment on completedTaskIds),
//    EXCEPT a removed line that was already ticked in the previous render,
//    which reports no completion (it was already dispatched by the flush
//    that ticked it) but still lands in removedIndices so the caller's id
//    array stays index-aligned. A surviving line counts as "embedding" even
//    when it has gained an "x" since the previous render — reported as a
//    completion too, but NOT in removedIndices, since it is still there: the
//    completion fade removes one line at a time, so a later tick's line can
//    still be sitting on screen, checked, while an earlier tick's line in the
//    same edit has already faded away.
//  - Anything else (a reorder, an ambiguous removal, any line inserted, a
//    removal mixed with an edit elsewhere, a merge from backspacing two lines
//    together) → no intent at all, and the next pull is what straightens the
//    note back out. Silently guessing which task got hit would risk completing
//    — or renaming — the wrong one, which is worse than doing nothing until
//    the next pull.
//
// The compose slot is exempt from all of the above: a line whose previous task
// id is empty (see kComposeIndex) never produces a completion or a rename, no
// matter what was typed into it. It has no task to address, and treating its
// text as a rename of something would rename whichever task happened to be
// listed first.
Intent diff(const QStringList &previousLines,
            const QStringList &previousTaskIds,
            const QString &currentText);

}
