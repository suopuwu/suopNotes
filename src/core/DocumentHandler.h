#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>

// Thin QML-facing wrapper: extracts a QTextCursor over the TextArea's
// underlying QTextDocument and delegates to the pure, independently-tested
// TextFormatting functions. Also owns markdown load/save, mirroring the
// exact QTextDocument::toMarkdown()/setMarkdown() mechanism verified in the
// project's WYSIWYG spike, rather than relying on unverified assumptions
// about TextArea's own text-property serialization.
class DocumentHandler : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
public:
    explicit DocumentHandler(QObject *parent = nullptr);

    QQuickTextDocument *document() const;
    void setDocument(QQuickTextDocument *document);

    Q_INVOKABLE QString toMarkdown() const;
    Q_INVOKABLE void setMarkdown(const QString &markdown);

    Q_INVOKABLE void toggleBold(int selectionStart, int selectionEnd);
    Q_INVOKABLE void toggleItalic(int selectionStart, int selectionEnd);
    Q_INVOKABLE void toggleStrikethrough(int selectionStart, int selectionEnd);
    Q_INVOKABLE void toggleBulletList(int selectionStart, int selectionEnd);
    Q_INVOKABLE void toggleNumberedList(int selectionStart, int selectionEnd);
    Q_INVOKABLE void setHeadingLevel(int selectionStart, int selectionEnd, int level);
    Q_INVOKABLE void insertTable(int position);
    Q_INVOKABLE void insertTableRow(int position);
    Q_INVOKABLE void deleteTableRow(int position);
    Q_INVOKABLE void insertTableColumn(int position);
    Q_INVOKABLE void deleteTableColumn(int position);
    Q_INVOKABLE void deleteTable(int position);

    // Read-only state queries backing the toolbar's "is this active at the
    // current selection/cursor" indicator (e.g. a highlighted Bold button
    // when the selection is already bold). Each mirrors the exact cursor
    // state its matching toggle*/setHeadingLevel call above reads, via
    // TextFormatting's own read-only counterparts.
    Q_INVOKABLE bool isBold(int selectionStart, int selectionEnd) const;
    Q_INVOKABLE bool isItalic(int selectionStart, int selectionEnd) const;
    Q_INVOKABLE bool isStrikethrough(int selectionStart, int selectionEnd) const;
    Q_INVOKABLE int headingLevel(int selectionStart, int selectionEnd) const;
    Q_INVOKABLE bool isBulletList(int selectionStart, int selectionEnd) const;
    Q_INVOKABLE bool isNumberedList(int selectionStart, int selectionEnd) const;

    // Enter/Return inside a list item (bullet or numbered): continues the
    // list with a new item, or — if the current item is empty — exits the
    // list back to a plain paragraph, mirroring standard word-processor
    // behavior. Returns false (does nothing) when the cursor isn't in a
    // list at all, so QML's key handler knows to fall through to the
    // TextArea's own default Return handling.
    Q_INVOKABLE bool tryContinueList(int cursorPosition);

    // Called after every keystroke: detects "- "/"* " or "1. " typed at the
    // start of an otherwise-plain line and converts it into a real bullet/
    // numbered list, consuming the typed marker text. This is the one
    // exception to the "never live-convert typed markdown" rule the toolbar
    // formatting otherwise follows (see TextFormatting.h) — list shorthand
    // is universal enough (Notion, Word, GitHub's own editor) that users
    // expect it to just work. Returns true if a conversion happened.
    Q_INVOKABLE bool tryAutoConvertListShorthand(int cursorPosition);

signals:
    void documentChanged();
    // Emitted after any call above that can change the format active at the
    // cursor/selection (the toggles, setHeadingLevel, and the two list-aware
    // Enter/autoconvert helpers when they actually mutate something) — lets
    // QML re-poll the isXxx()/headingLevel() queries above even when the
    // selection itself hasn't moved, e.g. right after clicking Bold with a
    // selection still held (see SelectionToolbar.qml's runAction()).
    void formattingChanged();

private:
    QTextCursor cursorFor(int selectionStart, int selectionEnd) const;

    QPointer<QQuickTextDocument> m_document;
};
