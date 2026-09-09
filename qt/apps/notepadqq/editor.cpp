/*
 * Editor - see editor.h.  The gutter machinery is the standard
 * QPlainTextEdit line-number pattern; the colors are CodeMirror's default
 * theme (gutter #f7f7f7 with #999 numbers, active line #e8f2ff) so a tab
 * looks the way a notepadqq tab does.
 */

#include "editor.h"

#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QTextBlock>
#include <QWheelEvent>

class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(Editor *editor)
        : QWidget(editor), editor_(editor) {}

    QSize sizeHint() const override
    {
        return QSize(editor_->lineNumberAreaWidth(), 0);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        editor_->lineNumberAreaPaintEvent(event);
    }

private:
    Editor *editor_;
};

Editor::Editor(QWidget *parent)
    : QPlainTextEdit(parent), eolCrLf_(false)
{
    // No fontconfig on EwokOS, so the generic "Monospace" alias would fall
    // back to the proportional theme font; name a real installed family.
#ifdef __EWOKOS__
    QFont f("DejaVu Sans Mono");
#else
    QFont f("Monospace");
#endif
    f.setStyleHint(QFont::Monospace);
    f.setPointSize(12);
    setFont(f);
    basePointSize_ = f.pointSize();
    setTabStopDistance(4 * QFontMetricsF(f).horizontalAdvance(QLatin1Char(' ')));

    // Upstream default: no wrap (View > Word wrap toggles it).
    setLineWrapMode(QPlainTextEdit::NoWrap);

    highlighter_ = new Highlighter(document());

    lineNumberArea_ = new LineNumberArea(this);
    connect(this, &QPlainTextEdit::blockCountChanged,
            this, [this](int) { updateLineNumberAreaWidth(); });
    connect(this, &QPlainTextEdit::updateRequest,
            this, &Editor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged,
            this, &Editor::highlightCurrentLine);
    updateLineNumberAreaWidth();
    highlightCurrentLine();
}

QString Editor::docName() const
{
    return isUntitled() ? untitledName_ : QFileInfo(filePath_).fileName();
}

// UTF-8 in, UTF-8 out - the encoding readout in the status bar is honest.
// The EOL convention is detected on load and preserved on save, the way
// upstream keeps a file's format unless told to convert.
bool Editor::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QString text = QString::fromUtf8(file.readAll());
    eolCrLf_ = text.contains(QLatin1String("\r\n"));
    text.remove(QLatin1Char('\r'));
    setPlainText(text);

    filePath_ = QFileInfo(path).absoluteFilePath();
    setLanguage(Highlighter::detect(filePath_));
    document()->setModified(false);
    return true;
}

bool Editor::save(const QString &path, QString *error)
{
    QString text = toPlainText();
    if (eolCrLf_)
        text.replace(QLatin1Char('\n'), QLatin1String("\r\n"));

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QByteArray bytes = text.toUtf8();
    if (file.write(bytes) != bytes.size()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.close();

    filePath_ = QFileInfo(path).absoluteFilePath();
    setLanguage(Highlighter::detect(filePath_));
    document()->setModified(false);
    return true;
}

void Editor::setEolCrLf(bool crlf)
{
    if (eolCrLf_ == crlf)
        return;
    eolCrLf_ = crlf;
    document()->setModified(true);      // a conversion is a pending change
}

void Editor::duplicateLine()
{
    QTextCursor cur = textCursor();
    const int col = cur.positionInBlock();
    const QString line = cur.block().text();
    cur.beginEditBlock();
    cur.movePosition(QTextCursor::EndOfBlock);
    cur.insertText(QLatin1String("\n") + line);
    cur.setPosition(cur.block().position() + qMin(col, int(line.length())));
    cur.endEditBlock();
    setTextCursor(cur);
}

void Editor::deleteLine()
{
    QTextCursor cur = textCursor();
    cur.beginEditBlock();
    cur.movePosition(QTextCursor::StartOfBlock);
    cur.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
    if (cur.anchor() == cur.position())  // last line: take the trailing text
        cur.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    cur.removeSelectedText();
    cur.endEditBlock();
}

void Editor::zoomReset()
{
    QFont f = font();
    f.setPointSize(basePointSize_);
    setFont(f);
    setTabStopDistance(4 * QFontMetricsF(f).horizontalAdvance(QLatin1Char(' ')));
}

void Editor::wheelEvent(QWheelEvent *event)
{
    // QPlainTextEdit only zooms on ctrl+wheel when read-only; upstream
    // zooms while editing too.
    if (event->modifiers() & Qt::ControlModifier) {
        const int delta = event->angleDelta().y();
        if (delta > 0)
            zoomIn();
        else if (delta < 0)
            zoomOut();
        event->accept();
        return;
    }
    QPlainTextEdit::wheelEvent(event);
}

// ---- line-number gutter ------------------------------------------------------

int Editor::lineNumberAreaWidth() const
{
    int digits = 1;
    for (int max = qMax(1, blockCount()); max >= 10; max /= 10)
        ++digits;
    return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void Editor::updateLineNumberAreaWidth()
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void Editor::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy)
        lineNumberArea_->scroll(0, dy);
    else
        lineNumberArea_->update(0, rect.y(), lineNumberArea_->width(),
                                rect.height());
    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth();
}

void Editor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    const QRect cr = contentsRect();
    lineNumberArea_->setGeometry(
        QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void Editor::lineNumberAreaPaintEvent(QPaintEvent *event)
{
    QPainter painter(lineNumberArea_);
    painter.fillRect(event->rect(), QColor(0xf7, 0xf7, 0xf7));
    painter.setPen(QColor(0xd0, 0xd0, 0xd0));
    painter.drawLine(event->rect().topRight(), event->rect().bottomRight());

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    painter.setPen(QColor(0x99, 0x99, 0x99));
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            painter.drawText(0, top, lineNumberArea_->width() - 6,
                             fontMetrics().height(), Qt::AlignRight,
                             QString::number(blockNumber + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

void Editor::highlightCurrentLine()
{
    QList<QTextEdit::ExtraSelection> selections;
    if (!isReadOnly()) {
        QTextEdit::ExtraSelection sel;
        sel.format.setBackground(QColor(0xe8, 0xf2, 0xff)); // CM activeline
        sel.format.setProperty(QTextFormat::FullWidthSelection, true);
        sel.cursor = textCursor();
        sel.cursor.clearSelection();
        selections.append(sel);
    }
    setExtraSelections(selections);
}
