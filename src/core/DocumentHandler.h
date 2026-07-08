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
    Q_INVOKABLE void setHeadingLevel(int selectionStart, int selectionEnd, int level);

signals:
    void documentChanged();

private:
    QTextCursor cursorFor(int selectionStart, int selectionEnd) const;

    QPointer<QQuickTextDocument> m_document;
};
