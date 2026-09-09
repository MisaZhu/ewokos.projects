#ifndef NQQ_HIGHLIGHTER_H
#define NQQ_HIGHLIGHTER_H

/*
 * Language-aware highlighting.  Upstream delegates this to CodeMirror's
 * per-language modes; here a QSyntaxHighlighter carries a small rule set
 * per language, in CodeMirror's default theme colors (keyword #708,
 * string #a11, comment #a50, number #164, def #00f, tag #170, attr #00c)
 * so highlighted files read the same as they do in notepadqq.  Multi-line
 * constructs (block comments, python triple-quoted strings) are tracked
 * through the block state, so they color correctly across lines.
 */

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>

class Highlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    // The representative slice of upstream's language list that the shipped
    // rule sets cover; everything else opens as plain text.
    enum Language {
        PlainText = 0,
        Cpp,        // C and C++
        JavaScript, // and JSON
        Python,
        Shell,
        Xml,        // and HTML/SVG
        Ini,        // key=value config files
        LanguageCount
    };

    explicit Highlighter(QTextDocument *doc);

    Language language() const { return lang_; }
    void setLanguage(Language lang);

    static QString languageName(Language lang);
    static Language detect(const QString &fileName);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    void rebuildRules();
    void addRule(const QString &pattern, const QTextCharFormat &format);
    void highlightPythonStrings(const QString &text);
    void highlightBlockComments(const QString &text);

    Language lang_;
    QVector<Rule> rules_;
    QRegularExpression blockStart_;   // /* or <!--
    QRegularExpression blockEnd_;     // */ or -->

    QTextCharFormat keywordFormat_;
    QTextCharFormat defFormat_;
    QTextCharFormat numberFormat_;
    QTextCharFormat stringFormat_;
    QTextCharFormat commentFormat_;
    QTextCharFormat metaFormat_;
    QTextCharFormat tagFormat_;
    QTextCharFormat attrFormat_;
};

#endif // NQQ_HIGHLIGHTER_H
