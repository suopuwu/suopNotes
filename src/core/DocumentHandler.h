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

private:
    QTextCursor cursorFor(int selectionStart, int selectionEnd) const;

    QPointer<QQuickTextDocument> m_document;
};
