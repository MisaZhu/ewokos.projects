/*
 * Highlighter - see highlighter.h.  The rule sets are deliberately small:
 * keywords, numbers, strings and comments per language, in CodeMirror's
 * default theme colors, which is what a notepadqq window shows out of the
 * box.
 */

#include "highlighter.h"

#include <QFileInfo>

Highlighter::Highlighter(QTextDocument *doc)
    : QSyntaxHighlighter(doc), lang_(PlainText)
{
    // CodeMirror "default" theme.
    keywordFormat_.setForeground(QColor(0x77, 0x00, 0x88)); // .cm-keyword #708
    keywordFormat_.setFontWeight(QFont::Bold);
    defFormat_.setForeground(QColor(0x00, 0x00, 0xff));     // .cm-def     #00f
    numberFormat_.setForeground(QColor(0x11, 0x66, 0x44));  // .cm-number  #164
    stringFormat_.setForeground(QColor(0xaa, 0x11, 0x11));  // .cm-string  #a11
    commentFormat_.setForeground(QColor(0xaa, 0x55, 0x00)); // .cm-comment #a50
    metaFormat_.setForeground(QColor(0x55, 0x55, 0x55));    // .cm-meta    #555
    tagFormat_.setForeground(QColor(0x11, 0x77, 0x00));     // .cm-tag     #170
    attrFormat_.setForeground(QColor(0x00, 0x00, 0xcc));    // .cm-attribute #00c
}

QString Highlighter::languageName(Language lang)
{
    switch (lang) {
    case Cpp:        return QStringLiteral("C++");
    case JavaScript: return QStringLiteral("JavaScript");
    case Python:     return QStringLiteral("Python");
    case Shell:      return QStringLiteral("Shell");
    case Xml:        return QStringLiteral("XML/HTML");
    case Ini:        return QStringLiteral("Config (ini)");
    default:         return QStringLiteral("Plain text");
    }
}

Highlighter::Language Highlighter::detect(const QString &fileName)
{
    const QString ext = QFileInfo(fileName).suffix().toLower();
    const QString base = QFileInfo(fileName).fileName().toLower();

    if (ext == "c" || ext == "h" || ext == "cpp" || ext == "cc" ||
        ext == "cxx" || ext == "hpp" || ext == "hh" || ext == "inl")
        return Cpp;
    if (ext == "js" || ext == "json" || ext == "ts")
        return JavaScript;
    if (ext == "py")
        return Python;
    if (ext == "sh" || base == "makefile" || ext == "mk")
        return Shell;
    if (ext == "xml" || ext == "html" || ext == "htm" || ext == "svg" ||
        ext == "ui" || ext == "qrc")
        return Xml;
    if (ext == "ini" || ext == "conf" || ext == "cfg" || ext == "inf")
        return Ini;
    return PlainText;
}

void Highlighter::setLanguage(Language lang)
{
    if (lang == lang_)
        return;
    lang_ = lang;
    rebuildRules();
    rehighlight();
}

void Highlighter::addRule(const QString &pattern, const QTextCharFormat &format)
{
    Rule r;
    r.pattern = QRegularExpression(pattern);
    r.format = format;
    rules_.append(r);
}

void Highlighter::rebuildRules()
{
    rules_.clear();
    blockStart_ = QRegularExpression();
    blockEnd_ = QRegularExpression();

    switch (lang_) {
    case Cpp:
        addRule("\\b(alignas|alignof|asm|auto|bool|break|case|catch|char|"
                "class|const|constexpr|const_cast|continue|decltype|default|"
                "delete|do|double|dynamic_cast|else|enum|explicit|extern|"
                "false|float|for|friend|goto|if|inline|int|long|mutable|"
                "namespace|new|noexcept|nullptr|operator|override|private|"
                "protected|public|register|reinterpret_cast|return|short|"
                "signed|sizeof|static|static_cast|struct|switch|template|"
                "this|throw|true|try|typedef|typeid|typename|union|unsigned|"
                "using|virtual|void|volatile|wchar_t|while|"
                "int8_t|int16_t|int32_t|int64_t|uint8_t|uint16_t|uint32_t|"
                "uint64_t|size_t|ssize_t|NULL)\\b", keywordFormat_);
        addRule("\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*\\()", defFormat_);
        addRule("\\b(0[xX][0-9a-fA-F]+|\\d+\\.?\\d*([eE][+-]?\\d+)?[uUlLfF]*)\\b",
                numberFormat_);
        addRule("\"(\\\\.|[^\"\\\\])*\"", stringFormat_);
        addRule("'(\\\\.|[^'\\\\])*'", stringFormat_);
        addRule("^\\s*#\\s*\\w+", metaFormat_);
        addRule("//[^\n]*", commentFormat_);
        blockStart_ = QRegularExpression("/\\*");
        blockEnd_ = QRegularExpression("\\*/");
        break;

    case JavaScript:
        addRule("\\b(async|await|break|case|catch|class|const|continue|"
                "debugger|default|delete|do|else|export|extends|false|"
                "finally|for|function|if|import|in|instanceof|let|new|null|"
                "of|return|static|super|switch|this|throw|true|try|typeof|"
                "undefined|var|void|while|with|yield)\\b", keywordFormat_);
        addRule("\\b[A-Za-z_$][A-Za-z0-9_$]*(?=\\s*\\()", defFormat_);
        addRule("\\b(0[xX][0-9a-fA-F]+|\\d+\\.?\\d*([eE][+-]?\\d+)?)\\b",
                numberFormat_);
        addRule("\"(\\\\.|[^\"\\\\])*\"", stringFormat_);
        addRule("'(\\\\.|[^'\\\\])*'", stringFormat_);
        addRule("`(\\\\.|[^`\\\\])*`", stringFormat_);
        addRule("//[^\n]*", commentFormat_);
        blockStart_ = QRegularExpression("/\\*");
        blockEnd_ = QRegularExpression("\\*/");
        break;

    case Python:
        addRule("\\b(and|as|assert|async|await|break|class|continue|def|del|"
                "elif|else|except|False|finally|for|from|global|if|import|"
                "in|is|lambda|None|nonlocal|not|or|pass|raise|return|True|"
                "try|while|with|yield|self)\\b", keywordFormat_);
        addRule("\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*\\()", defFormat_);
        addRule("\\b(0[xX][0-9a-fA-F]+|\\d+\\.?\\d*([eE][+-]?\\d+)?[jJ]?)\\b",
                numberFormat_);
        addRule("\"(\\\\.|[^\"\\\\])*\"", stringFormat_);
        addRule("'(\\\\.|[^'\\\\])*'", stringFormat_);
        addRule("#[^\n]*", commentFormat_);
        break;

    case Shell:
        addRule("\\b(if|then|elif|else|fi|for|while|until|do|done|case|esac|"
                "in|function|select|time|break|continue|return|exit|export|"
                "local|readonly|shift|test|echo|cd|set|unset|source|alias|"
                "eval|exec|trap)\\b", keywordFormat_);
        addRule("\\$\\{[^}\n]*\\}|\\$\\w+|\\$[#?$!@*]", defFormat_);
        addRule("\\b\\d+\\b", numberFormat_);
        addRule("\"(\\\\.|[^\"\\\\])*\"", stringFormat_);
        addRule("'[^'\n]*'", stringFormat_);
        addRule("#[^\n]*", commentFormat_);
        break;

    case Xml:
        addRule("</?\\s*[\\w:.-]+|/?>", tagFormat_);
        addRule("\\b[\\w:.-]+(?=\\s*=)", attrFormat_);
        addRule("&\\w+;", numberFormat_);
        addRule("\"[^\"\n]*\"", stringFormat_);
        addRule("'[^'\n]*'", stringFormat_);
        blockStart_ = QRegularExpression("<!--");
        blockEnd_ = QRegularExpression("-->");
        break;

    case Ini:
        addRule("^[^=\n]+(?==)", attrFormat_);
        addRule("^\\s*\\[[^\\]\n]*\\]", keywordFormat_);
        addRule("^\\s*[#;][^\n]*", commentFormat_);
        break;

    default:
        break;
    }
}

// Block states carried between lines.
enum BlockState {
    Outside = -1,
    InBlockComment = 1,   // /* ... */ or <!-- ... -->
    InPySingle = 2,       // ''' ... '''
    InPyDouble = 3        // """ ... """
};

void Highlighter::highlightBlock(const QString &text)
{
    for (const Rule &rule : rules_) {
        QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            setFormat(m.capturedStart(), m.capturedLength(), rule.format);
        }
    }

    setCurrentBlockState(Outside);

    if (lang_ == Python)
        highlightPythonStrings(text);
    else if (blockStart_.isValid() && !blockStart_.pattern().isEmpty())
        highlightBlockComments(text);
}

// The classic Qt syntax-highlighter pattern: on state 1 the line starts
// inside the comment, otherwise search for the opener; repeat past each
// closer, and leave state 1 behind when no closer is found.
void Highlighter::highlightBlockComments(const QString &text)
{
    int startIndex = 0;
    if (previousBlockState() != InBlockComment)
        startIndex = text.indexOf(blockStart_);

    while (startIndex >= 0) {
        QRegularExpressionMatch endMatch;
        const int endIndex = text.indexOf(blockEnd_, startIndex, &endMatch);
        int length;
        if (endIndex == -1) {
            setCurrentBlockState(InBlockComment);
            length = text.length() - startIndex;
        } else {
            length = endIndex - startIndex + endMatch.capturedLength();
        }
        setFormat(startIndex, length, commentFormat_);
        startIndex = text.indexOf(blockStart_,
                                  startIndex + qMax(length, 1));
    }
}

// Same walk for python's ''' / """ strings; the two delimiters carry
// distinct states so a ''' inside a """-string does not close it.
void Highlighter::highlightPythonStrings(const QString &text)
{
    static const QString kSingle = QStringLiteral("'''");
    static const QString kDouble = QStringLiteral("\"\"\"");

    int pos = 0;
    int state = previousBlockState();

    while (pos <= text.length()) {
        if (state == InPySingle || state == InPyDouble) {
            const QString &delim = (state == InPySingle) ? kSingle : kDouble;
            const int end = text.indexOf(delim, pos);
            if (end == -1) {
                setFormat(pos, text.length() - pos, stringFormat_);
                setCurrentBlockState(state);
                return;
            }
            setFormat(pos, end + 3 - pos, stringFormat_);
            pos = end + 3;
            state = Outside;
        } else {
            const int s = text.indexOf(kSingle, pos);
            const int d = text.indexOf(kDouble, pos);
            if (s == -1 && d == -1)
                return;
            if (d == -1 || (s != -1 && s < d)) {
                setFormat(s, 3, stringFormat_);
                pos = s + 3;
                state = InPySingle;
            } else {
                setFormat(d, 3, stringFormat_);
                pos = d + 3;
                state = InPyDouble;
            }
        }
    }
}
