#include "MarkdownSerialization.h"
#include "TextFormatting.h"

#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextList>

#include <algorithm>

namespace MarkdownSerialization
{

namespace
{

// A block counts as bare vertical spacing (not markdown structure) only
// when it's a genuinely empty, unformatted paragraph — not part of a list,
// blockquote, heading, or fenced code block. That keeps blank lines that
// are meaningful content (e.g. inside a fenced code block) exactly as Qt's
// writer already renders them.
bool isBlankSpacerBlock(const QTextBlock &block)
{
    if (!block.text().isEmpty())
        return false;
    if (block.textList())
        return false;
    const QTextBlockFormat format = block.blockFormat();
    if (format.hasProperty(QTextFormat::BlockQuoteLevel))
        return false;
    if (format.hasProperty(QTextFormat::BlockCodeLanguage) || format.hasProperty(QTextFormat::BlockCodeFence))
        return false;
    if (format.headingLevel() != 0)
        return false;
    // An empty table cell's block has no text of its own, but it is real
    // table structure, not vertical spacing between top-level blocks —
    // without this check it would be miscounted as a spacer and run-split
    // logic below would slice the run boundary through the middle of the
    // table.
    if (QTextCursor(block).currentTable())
        return false;
    return true;
}

bool isCodeFormatted(const QTextBlock &block)
{
    const QTextBlockFormat format = block.blockFormat();
    return format.hasProperty(QTextFormat::BlockCodeLanguage) || format.hasProperty(QTextFormat::BlockCodeFence);
}

struct Run
{
    int startBlockNumber;
    int endBlockNumber;
};

QString markdownForRun(QTextDocument *document, const Run &run)
{
    const QTextBlock startBlock = document->findBlockByNumber(run.startBlockNumber);
    const QTextBlock endBlock = document->findBlockByNumber(run.endBlockNumber);

    // A run is homogeneous by construction (see the run-splitting loop
    // above) — checking the first block is enough to know every block in
    // it is a hand-rolled list item. Their marker text is already exactly
    // correct GFM syntax, but Qt's own markdown WRITER treats any plain
    // paragraph merely starting with what looks like list syntax as
    // ambiguous and defensively escapes it ("1. " -> "1\. ", "- " -> "\- ",
    // confirmed empirically) since it has no way to know the block was
    // deliberately meant to render as a list marker rather than being
    // coincidental prose. Bypassing Qt's writer entirely here sidesteps
    // that unconditionally, without risking unescaping a genuinely-plain
    // paragraph elsewhere that just happens to look similar (see
    // TextFormatting::isHandRolledListBlock's own comment).
    if (TextFormatting::isHandRolledListBlock(startBlock)) {
        QStringList lines;
        for (QTextBlock block = startBlock; block.isValid(); block = block.next()) {
            lines << block.text();
            if (block == endBlock)
                break;
        }
        return lines.join(QLatin1Char('\n'));
    }

    const bool startsInTable = QTextCursor(startBlock).currentTable() != nullptr;

    QTextCursor cursor(document);
    // If a selection's very first block is fenced-code-formatted and there's
    // no preceding block in the selection at all, Qt's markdown writer
    // silently drops the opening fence (confirmed empirically — it only
    // needs one character of context, not the preceding block's actual
    // content). Extending the start back into the previous block's trailing
    // separator sidesteps it; that separator character isn't part of any
    // block's text, so it doesn't leak into the output.
    cursor.setPosition(run.startBlockNumber > 0 ? startBlock.position() - 1 : startBlock.position());
    cursor.setPosition(endBlock.position() + endBlock.length() - 1, QTextCursor::KeepAnchor);

    QString markdown = cursor.selection().toMarkdown();
    // The opposite quirk from the fence case above: when a run's very first
    // block is table structure, Qt's writer unconditionally prefixes one
    // blank line of its own before the table's "|...|" syntax — even though
    // this file already inserts its own controlled blank-line separator
    // between runs. Left alone, every table would gain one extra phantom
    // blank line above it on every single save (confirmed empirically).
    if (startsInTable && markdown.startsWith(QLatin1Char('\n')))
        markdown = markdown.mid(1);
    // A close cousin of the quirk above, but for a table that ISN'T the
    // run's first block: Qt's writer doubles the blank line separating
    // ordinary content (a paragraph, a heading, ...) from a table that
    // directly follows it — two blank lines where every other block-to-
    // block transition (including a table followed BY a paragraph) only
    // ever gets one (confirmed empirically). A run never contains a real,
    // user-authored multi-blank-line spacer internally — any deliberate
    // spacer block forces its own run boundary in the splitting loop above
    // — so any 2+ blank lines directly before a table row here are always
    // this writer artifact, never intentional spacing, and collapsing them
    // to the CommonMark-minimum single blank line is always correct.
    static const QRegularExpression doubledBlankBeforeTable(QStringLiteral("\\n{3,}(?=\\|)"));
    markdown.replace(doubledBlankBeforeTable, QStringLiteral("\n\n"));
    while (markdown.endsWith(QLatin1Char('\n')))
        markdown.chop(1);
    return markdown;
}

} // namespace

QString toMarkdownPreservingBlankLines(QTextDocument *document)
{
    QList<Run> runs;
    QList<int> interiorGapSpacers; // spacer blocks between runs[i] and runs[i + 1]
    int leadingSpacers = 0;
    int trailingSpacers = 0;

    bool seenContent = false;
    int pendingSpacers = 0;
    int runStart = -1;
    bool runIsCode = false;
    bool runIsHandRolledList = false;
    int blockNumber = 0;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next(), ++blockNumber) {
        if (isBlankSpacerBlock(block)) {
            ++pendingSpacers;
            continue;
        }
        const bool blockIsCode = isCodeFormatted(block);
        const bool blockIsHandRolledList = TextFormatting::isHandRolledListBlock(block);
        if (!seenContent) {
            leadingSpacers = pendingSpacers;
            runStart = blockNumber;
            runIsCode = blockIsCode;
            runIsHandRolledList = blockIsHandRolledList;
        } else if (pendingSpacers > 0 || blockIsCode != runIsCode || blockIsHandRolledList != runIsHandRolledList) {
            // A code-format transition forces a run boundary even with zero
            // spacer blocks between them: Qt's writer, when a fenced code
            // block sits tight against a plain block in the same pass,
            // silently drops the blank line that's supposed to separate them
            // (confirmed empirically) — splitting the run guarantees at
            // least the CommonMark-minimum blank line gets written back in.
            // A hand-rolled-list transition forces one for a different
            // reason — see markdownForRun's own handling of such runs.
            runs.append({runStart, blockNumber - 1 - pendingSpacers});
            interiorGapSpacers.append(pendingSpacers);
            runStart = blockNumber;
            runIsCode = blockIsCode;
            runIsHandRolledList = blockIsHandRolledList;
        }
        pendingSpacers = 0;
        seenContent = true;
    }

    if (!seenContent)
        return QString(); // Nothing but blank spacer blocks (or a fresh, empty note).

    runs.append({runStart, blockNumber - 1 - pendingSpacers});
    trailingSpacers = pendingSpacers;

    QStringList runMarkdowns;
    runMarkdowns.reserve(runs.size());
    for (const Run &run : std::as_const(runs))
        runMarkdowns.append(markdownForRun(document, run));

    QString result = QStringLiteral("\n").repeated(leadingSpacers);
    for (int i = 0; i < runMarkdowns.size(); ++i) {
        result += runMarkdowns.at(i);
        if (i + 1 < runMarkdowns.size()) {
            // CommonMark needs at least one blank line to keep adjacent
            // blocks separate; only spacer counts beyond that minimum add
            // extra visible blank lines.
            const int blankLines = std::max(interiorGapSpacers.at(i), 1);
            result += QStringLiteral("\n").repeated(blankLines + 1);
        }
    }
    result += QStringLiteral("\n").repeated(trailingSpacers > 0 ? trailingSpacers + 1 : 1);
    return result;
}

namespace
{

struct RawStructure
{
    int leadingSpacers = 0;
    int trailingSpacers = 0;
    QList<int> interiorGapSpacers; // spacer lines between chunks[i] and chunks[i + 1]
    QStringList chunks;
};

// Splits raw markdown source into chunks of consecutive non-blank lines,
// recording how many blank lines separate each pair — while treating lines
// inside a fenced code block as never-blank, so a blank line that's part of
// a pasted code snippet isn't mistaken for spacing between notes content.
RawStructure analyzeRawStructure(const QString &markdown)
{
    RawStructure structure;
    if (markdown.isEmpty())
        return structure;

    QStringList lines = markdown.split(QLatin1Char('\n'));
    // A trailing "\n" (the normal end-of-file convention) splits into one
    // bogus empty trailing element that the user never typed as a blank
    // line — drop exactly one so it isn't counted as trailing spacing.
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();

    static const QRegularExpression fenceLine(QStringLiteral("^\\s*(`{3,}|~{3,})"));
    QList<bool> isBlank(lines.size(), false);
    bool insideFence = false;
    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        isBlank[i] = line.trimmed().isEmpty() && !insideFence;
        if (fenceLine.match(line).hasMatch())
            insideFence = !insideFence;
    }

    const int n = isBlank.size();
    int i = 0;
    while (i < n && isBlank.at(i)) {
        ++structure.leadingSpacers;
        ++i;
    }
    while (i < n) {
        QStringList chunkLines;
        while (i < n && !isBlank.at(i)) {
            chunkLines << lines.at(i);
            ++i;
        }
        structure.chunks << chunkLines.join(QLatin1Char('\n'));

        int gap = 0;
        while (i < n && isBlank.at(i)) {
            ++gap;
            ++i;
        }
        if (i < n)
            structure.interiorGapSpacers << gap;
        else
            structure.trailingSpacers = gap;
    }
    return structure;
}

// QTextCursor::insertBlock() with no arguments copies the CURRENT block's
// format onto the new one — without this, a heading or code-fence format
// bleeds forward into what's meant to be a bare spacer block.
void insertPlainSpacerBlock(QTextCursor &cursor)
{
    cursor.insertBlock();
    cursor.setBlockFormat(QTextBlockFormat());
}

// Parses one chunk on its own (so its formatting is exactly what Qt's own
// parser would produce) and merges it into the document at the cursor.
void insertChunk(QTextCursor &cursor, const QString &chunkText)
{
    QTextDocument chunkDocument;
    chunkDocument.setMarkdown(chunkText);
    // Converts Qt's own native QTextList-based numbered/checkbox items
    // (freshly parsed above) into the hand-rolled, selectable-marker
    // representation this app actually edits with — see TextFormatting.h's
    // own comment on convertNativeListsToHandRolled for why, and why this
    // must run before the textList() check below even considers this
    // chunk's first block (a hand-rolled block never carries a QTextList
    // object attachment, so it's exempt from the phantom-item risk that
    // check exists to guard against — it's just a plain block with custom
    // properties, the same as a heading).
    TextFormatting::convertNativeListsToHandRolled(&chunkDocument);
    const QTextBlock firstChunkBlock = chunkDocument.begin();
    if (firstChunkBlock.textList()) {
        // A list item's block format carries a QTextList *object* attachment,
        // not just plain properties — copying it (the way headings/code
        // fences are handled below) onto the cursor's pre-existing empty
        // destination block doesn't join that block to the real list being
        // inserted; it turns it into its own permanent phantom empty list
        // item sitting above the real first item (confirmed empirically).
        // Consuming the leftover empty block first — same move
        // TextFormatting::insertTable makes before a table lands — lets
        // insertFragment() create the real first item fresh, with no
        // leftover block for it to strand.
        if (cursor.atBlockStart() && cursor.atBlockEnd())
            cursor.deletePreviousChar();
    } else {
        // insertFragment() merges the fragment's first block's text into the
        // cursor's current (already-existing, plain) block, but does NOT
        // carry that first block's own format onto the destination — it
        // silently keeps whatever format the destination block already had.
        // Align them first so a chunk starting with, say, a fenced code
        // block doesn't get its first line demoted to a plain paragraph.
        cursor.setBlockFormat(firstChunkBlock.blockFormat());
    }
    cursor.insertFragment(QTextDocumentFragment(&chunkDocument));
}

} // namespace

void setMarkdownPreservingBlankLines(QTextDocument *document, const QString &markdown)
{
    if (markdown.isEmpty()) {
        document->clear();
        return;
    }

    const RawStructure structure = analyzeRawStructure(markdown);

    document->clear();
    QTextCursor cursor(document);

    // The document already starts with one (empty, plain) block — use it as
    // the first spacer/content block instead of inserting an extra one.
    bool usedInitialBlock = false;
    for (int k = 0; k < structure.leadingSpacers; ++k) {
        if (!usedInitialBlock)
            usedInitialBlock = true;
        else
            insertPlainSpacerBlock(cursor);
    }
    if (structure.leadingSpacers > 0 || usedInitialBlock)
        insertPlainSpacerBlock(cursor);

    for (int i = 0; i < structure.chunks.size(); ++i) {
        if (i > 0) {
            const int gap = structure.interiorGapSpacers.at(i - 1);
            // A single blank line is the minimum CommonMark needs just to
            // keep two blocks apart at all — the same minimum a plain
            // single-Enter paragraph break (zero spacer blocks) is written
            // with (see toMarkdownPreservingBlankLines' own qMax(gap, 1)).
            // The two are indistinguishable on disk, so resolve the
            // ambiguity toward the far more common case: no extra spacer
            // block. Only two or more blank lines are unambiguous evidence
            // of real, deliberate spacer blocks.
            const int spacerBlocksToRestore = gap > 1 ? gap : 0;
            for (int k = 0; k < spacerBlocksToRestore; ++k)
                insertPlainSpacerBlock(cursor);
            insertPlainSpacerBlock(cursor);
        }
        insertChunk(cursor, structure.chunks.at(i));
    }
    for (int k = 0; k < structure.trailingSpacers; ++k)
        insertPlainSpacerBlock(cursor);
}

} // namespace MarkdownSerialization
