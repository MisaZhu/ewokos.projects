#ifndef NQQ_EDITOR_H
#define NQQ_EDITOR_H

/*
 * One document - upstream's Editor is a CodeMirror instance per tab; here
 * it is a QPlainTextEdit with the CodeMirror chrome reproduced natively:
 * the line-number gutter, the active-line highlight, ctrl+wheel zoom and a
 * Highlighter on the document.  The file path, the untitled name ("new 1"),
 * the EOL convention and the language ride along as document state.
 */

#include <QPlainTextEdit>

#include "highlighter.h"

class LineNumberArea;

class Editor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit Editor(QWidget *parent = nullptr);

    QString filePath() const { return filePath_; }
    QString docName() const;            // file name, or the "new N" name
    void setUntitledName(const QString &name) { untitledName_ = name; }
    bool isUntitled() const { return filePath_.isEmpty(); }

    bool load(const QString &path, QString *error);
    bool save(const QString &path, QString *error);

    bool eolCrLf() const { return eolCrLf_; }
    void setEolCrLf(bool crlf);

    Highlighter::Language language() const { return highlighter_->language(); }
    void setLanguage(Highlighter::Language lang) { highlighter_->setLanguage(lang); }

    void duplicateLine();               // upstream Ctrl+D
    void deleteLine();                  // upstream Ctrl+L
    void zoomReset();

    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent *event);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect &rect, int dy);
    void highlightCurrentLine();

    LineNumberArea *lineNumberArea_;
    Highlighter *highlighter_;
    QString filePath_;
    QString untitledName_;
    bool eolCrLf_;
    int basePointSize_;
};

#endif // NQQ_EDITOR_H
