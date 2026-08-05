#include "TickTickTaskList.h"

#include "core/MarkdownList.h"

namespace TickTickTaskList {

namespace {

// A parsed rendered/edited line: whether it is a checkbox at all (every line
// this feature writes or expects to read back is one, but user text can be
// anything mid-edit), whether it is ticked, and its label — the text after
// the marker, or the whole line verbatim for anything that isn't a checkbox
// (a non-checkbox line has no marker to strip).
struct ParsedLine
{
    bool isCheckbox = false;
    bool checked = false;
    QString label;
};

// One rendered line. Flattened to a single line, not because a task can't have
// a multi-line title (it never does, in practice) but because a newline here
// would render as TWO checkbox items for one task, and every index after it
// would then diff against the wrong task for the rest of the session.
QString checkboxLine(const QString &title)
{
    QString flattened = title;
    flattened.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return QStringLiteral("- [ ] ") + flattened;
}

ParsedLine parseLine(const QString &line)
{
    const MarkdownList::Marker marker = MarkdownList::parse(line);
    ParsedLine result;
    result.isCheckbox = marker.kind == MarkdownList::Kind::Checkbox;
    result.checked = result.isCheckbox && marker.checked;
    result.label = result.isCheckbox ? line.mid(marker.length) : line;
    return result;
}

// Which indices of `previousLines` are missing from `currentLines`, on the
// assumption that current is previous with only deletions applied — or an
// empty result if that assumption does not hold, or holds in more than one way.
//
// Ambiguity is the case worth spelling out. Two open tasks can share a title,
// so ["milk", "milk", "eggs"] -> ["milk", "eggs"] genuinely does not say WHICH
// "milk" was removed, and the two answers complete different tasks. The test
// for it is exact rather than heuristic: match greedily from the left and
// greedily from the right, and accept only when the two agree — an embedding is
// unique precisely when its leftmost and rightmost forms are the same.
QList<int> removedIndices(const QStringList &previousLines, const QStringList &currentLines)
{
    QList<int> fromLeft;
    int j = 0;
    for (int i = 0; i < previousLines.size(); ++i) {
        if (j < currentLines.size() && currentLines.at(j) == previousLines.at(i))
            ++j;
        else
            fromLeft.append(i);
    }
    // Not a pure deletion at all (something was also edited, or reordered).
    if (j != currentLines.size())
        return {};

    QList<int> fromRight;
    j = currentLines.size() - 1;
    for (int i = previousLines.size() - 1; i >= 0; --i) {
        if (j >= 0 && currentLines.at(j) == previousLines.at(i))
            --j;
        else
            fromRight.prepend(i);
    }
    if (fromLeft != fromRight)
        return {};

    return fromLeft;
}

}

QString composeTaskId()
{
    return QString();
}

QString render(const QList<TickTickClient::Task> &tasks, const QString &composeTitle)
{
    QStringList lines;
    lines.reserve(tasks.size() + 1);

    // The compose slot, always, even for an empty task list — unlike the old
    // render, where zero tasks meant an empty document. An empty document has
    // nowhere to type a first task, which is exactly the state a brand new
    // user is in.
    lines.append(checkboxLine(composeTitle));

    // Reversed: newest first (see the header). rbegin/rend rather than a
    // reversed copy, since nothing else here needs the list in either order.
    for (auto it = tasks.crbegin(); it != tasks.crend(); ++it)
        lines.append(checkboxLine(it->title));

    return lines.join(QLatin1Char('\n'));
}

QStringList taskIdsFor(const QList<TickTickClient::Task> &tasks)
{
    QStringList ids;
    ids.reserve(tasks.size() + 1);
    ids.append(composeTaskId());
    // The same reversal render() applies, in the same function pair, so the
    // two cannot disagree about which line belongs to which task.
    for (auto it = tasks.crbegin(); it != tasks.crend(); ++it)
        ids.append(it->id);
    return ids;
}

QString composeTitleOf(const QString &text)
{
    const QStringList lines = linesOf(text);
    if (lines.isEmpty())
        return QString();

    const ParsedLine first = parseLine(lines.at(kComposeIndex));
    // A first line that isn't a checkbox at all means the note isn't in the
    // shape this file renders — nothing to carry through, and inventing a
    // compose title out of arbitrary text would be worse than starting blank.
    return first.isCheckbox ? first.label : QString();
}

QString withComposedTaskCommitted(const QString &currentText, const QString &title)
{
    QStringList lines = linesOf(currentText);
    if (lines.isEmpty())
        lines.append(checkboxLine(QString()));

    // The slot is emptied rather than removed, so the note still has somewhere
    // to type the next task the instant this one is committed.
    lines[kComposeIndex] = checkboxLine(QString());
    lines.insert(kComposeIndex + 1, checkboxLine(title));
    return lines.join(QLatin1Char('\n'));
}

QStringList linesOf(const QString &text)
{
    if (text.isEmpty())
        return {};
    return text.split(QLatin1Char('\n'));
}

Intent diff(const QStringList &previousLines, const QStringList &previousTaskIds,
            const QString &currentText)
{
    Intent intent;
    const QStringList currentLines = linesOf(currentText);

    if (currentLines.size() == previousLines.size()) {
        // Trusted only when EXACTLY one line with a task behind it differs —
        // see the header. Two or more such lines differing (a reorder, a
        // multi-line paste over the same line count) is indistinguishable from
        // a guess here, so it is left as "no intent" for the next pull to
        // resolve.
        //
        // A differing line whose previous task id is empty — the compose slot
        // (kComposeIndex), or every line while the baseline ids are the
        // misaligned-cache fallback (see TickTickSync::commitComposedTask) —
        // does NOT count. Typing there is already defined as meaning nothing
        // to sync, so it must not be able to turn a clean single-line rename
        // happening alongside it into an uninterpretable edit: before this
        // rule, renaming a task while the slot held half-typed text read as
        // two changed lines, was refused, and the reconciling pull quietly
        // reverted the rename.
        int changedIndex = -1;
        int changedCount = 0;
        for (int i = 0; i < currentLines.size(); ++i) {
            if (currentLines.at(i) == previousLines.at(i))
                continue;
            if (i < previousTaskIds.size() && previousTaskIds.at(i).isEmpty())
                continue;
            ++changedCount;
            changedIndex = i;
        }

        if (changedCount == 0) {
            // Nothing changed that could be sent: every line is identical, or
            // the only differences sit on lines with no task behind them (the
            // routine case being text in the compose slot). Either way this is
            // a fully-interpreted state — see Intent::understood.
            intent.understood = true;
            return intent;
        }

        if (changedCount != 1)
            return intent;
        if (changedIndex < 0 || changedIndex >= previousTaskIds.size())
            return intent;

        // From here on, whatever this branch returns is a full reading of the
        // edit. The changed line is known to have a real task id — empty-id
        // lines were excluded from the count above.
        intent.understood = true;

        const QString &taskId = previousTaskIds.at(changedIndex);

        const ParsedLine before = parseLine(previousLines.at(changedIndex));
        const ParsedLine after = parseLine(currentLines.at(changedIndex));

        // A newly-ticked box wins over a simultaneous label edit — the tick
        // is the stronger signal, and the task is going away anyway (see
        // Intent's own comment), so there is no point also reporting a
        // rename for a task about to be marked done.
        if (!before.checked && after.checked) {
            intent.completedTaskIds.append(taskId);
            return intent;
        }

        if (before.label != after.label)
            intent.renamedTasks.append({taskId, after.label});
        return intent;
    }

    if (currentLines.size() < previousLines.size()) {
        // Removals — either whole-line deletes, or (the common case) lines the
        // completion fade has just taken away after the user ticked them. Told
        // apart from a marker merely backspaced apart upstream, not here:
        // NoteWindow.qml runs DocumentHandler::repairCheckboxList on every edit
        // BEFORE this text ever reaches diff(), so a disturbed marker is
        // already back to a well-formed checkbox line — same text, same line
        // count — by the time this runs.
        const QList<int> removed = removedIndices(previousLines, currentLines);
        if (removed.isEmpty())
            return intent;   // ambiguous, or not a pure deletion — the next pull resolves it

        // An unambiguous embedding is a full reading of the removal, whether
        // or not any of it turns into a completion below — see
        // Intent::understood.
        intent.understood = true;
        intent.removedIndices = removed;
        for (const int index : removed) {
            if (index >= previousTaskIds.size())
                continue;
            // The compose slot, gone. Nothing to complete — it never had a task
            // — but it still belongs in removedIndices so the caller's id array
            // stays index-aligned. In practice Backspace refuses to delete it
            // (see NoteWindow.qml's tryRemoveTaskLine), so this covers a paste
            // or an undo that took it out from under us, and the next render
            // puts a fresh slot back.
            if (previousTaskIds.at(index).isEmpty())
                continue;
            // A line that was ALREADY ticked in the previous render had its
            // completion dispatched by the flush that recorded it that way
            // (render() never writes "[x]", so a ticked baseline line can only
            // have come from a flush that saw the tick and acted on it).
            // Reporting it again would send a second complete for a task that
            // is already closed.
            const ParsedLine before = parseLine(previousLines.at(index));
            if (before.checked)
                continue;
            intent.completedTaskIds.append(previousTaskIds.at(index));
        }
        return intent;
    }

    // More lines than before: a paste, or an undo of a removal. NOT a create —
    // creates come from commitComposedTask alone now (see Intent's header), and
    // with Enter no longer inserting newlines in this note there is no
    // keystroke that produces an extra line on purpose. Returning an empty,
    // not-understood Intent is the safety property: the caller must not move
    // its baseline to this text (see Intent::understood), so the next pull
    // restores the note from the server rather than this guessing what was
    // meant.
    return intent;
}

}
