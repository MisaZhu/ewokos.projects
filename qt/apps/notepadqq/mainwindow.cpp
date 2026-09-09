/*
 * MainWindow - the notepadqq shell.  See mainwindow.h for the mapping to
 * the upstream window.
 */

#include "mainwindow.h"
#include "editor.h"
#include "searchbar.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QRegularExpression>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTextBlock>
#include <QToolBar>
#include <QVBoxLayout>

static QString startPath()
{
    const QString home = QDir::homePath();
    return (!home.isEmpty() && QDir(home).exists()) ? home : QStringLiteral("/");
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), untitledCounter_(0)
{
    resize(760, 480);

    tabs_ = new QTabWidget(this);
    tabs_->setTabsClosable(true);
    tabs_->setMovable(true);
    tabs_->setDocumentMode(true);
    connect(tabs_, &QTabWidget::currentChanged,
            this, &MainWindow::onTabChanged);
    connect(tabs_, &QTabWidget::tabCloseRequested,
            this, &MainWindow::closeTab);

    searchBar_ = new SearchBar(this);
    searchBar_->hide();
    connect(searchBar_, &SearchBar::searchRequested,
            this, [this](bool backward) { doFind(backward); });
    connect(searchBar_, &SearchBar::replaceRequested,
            this, &MainWindow::doReplace);
    connect(searchBar_, &SearchBar::replaceAllRequested,
            this, &MainWindow::doReplaceAll);
    connect(searchBar_, &SearchBar::closed, this, [this]() {
        if (currentEditor())
            currentEditor()->setFocus();
    });

    QWidget *central = new QWidget(this);
    QVBoxLayout *lay = new QVBoxLayout(central);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(tabs_, 1);
    lay->addWidget(searchBar_, 0);
    setCentralWidget(central);

    createActions();
    createToolBar();
    createMenus();
    createStatusBar();

    // Upstream always starts with (at least) one open document.
    addEditorTab();
}

// ---- construction -----------------------------------------------------------

void MainWindow::createActions()
{
    QStyle *st = style();

    actNew_ = new QAction(st->standardIcon(QStyle::SP_FileIcon),
                          tr("New"), this);
    actNew_->setShortcut(QKeySequence::New);
    connect(actNew_, &QAction::triggered, this, &MainWindow::newTab);

    actOpen_ = new QAction(st->standardIcon(QStyle::SP_DialogOpenButton),
                           tr("Open..."), this);
    actOpen_->setShortcut(QKeySequence::Open);
    connect(actOpen_, &QAction::triggered, this, &MainWindow::openFile);

    actSave_ = new QAction(st->standardIcon(QStyle::SP_DialogSaveButton),
                           tr("Save"), this);
    actSave_->setShortcut(QKeySequence::Save);
    connect(actSave_, &QAction::triggered,
            this, [this]() { save(currentEditor()); });

    actSaveAs_ = new QAction(tr("Save as..."), this);
    actSaveAs_->setShortcut(QKeySequence("Ctrl+Shift+S"));
    connect(actSaveAs_, &QAction::triggered,
            this, [this]() { saveAs(currentEditor()); });

    actSaveAll_ = new QAction(tr("Save all"), this);
    connect(actSaveAll_, &QAction::triggered, this, &MainWindow::saveAll);

    actCloseTab_ = new QAction(tr("Close"), this);
    actCloseTab_->setShortcut(QKeySequence("Ctrl+W"));
    connect(actCloseTab_, &QAction::triggered,
            this, [this]() { closeTab(tabs_->currentIndex()); });

    actCloseAll_ = new QAction(tr("Close all"), this);
    connect(actCloseAll_, &QAction::triggered, this, &MainWindow::closeAllTabs);

    actQuit_ = new QAction(tr("Exit"), this);
    actQuit_->setShortcut(QKeySequence::Quit);
    connect(actQuit_, &QAction::triggered, this, &QWidget::close);

    actUndo_ = new QAction(st->standardIcon(QStyle::SP_ArrowBack),
                           tr("Undo"), this);
    actUndo_->setShortcut(QKeySequence::Undo);
    connect(actUndo_, &QAction::triggered,
            this, [this]() { if (currentEditor()) currentEditor()->undo(); });

    actRedo_ = new QAction(st->standardIcon(QStyle::SP_ArrowForward),
                           tr("Redo"), this);
    actRedo_->setShortcut(QKeySequence::Redo);
    connect(actRedo_, &QAction::triggered,
            this, [this]() { if (currentEditor()) currentEditor()->redo(); });

    actCut_ = new QAction(tr("Cut"), this);
    actCut_->setShortcut(QKeySequence::Cut);
    connect(actCut_, &QAction::triggered,
            this, [this]() { if (currentEditor()) currentEditor()->cut(); });

    actCopy_ = new QAction(tr("Copy"), this);
    actCopy_->setShortcut(QKeySequence::Copy);
    connect(actCopy_, &QAction::triggered,
            this, [this]() { if (currentEditor()) currentEditor()->copy(); });

    actPaste_ = new QAction(tr("Paste"), this);
    actPaste_->setShortcut(QKeySequence::Paste);
    connect(actPaste_, &QAction::triggered,
            this, [this]() { if (currentEditor()) currentEditor()->paste(); });

    actSelectAll_ = new QAction(tr("Select all"), this);
    actSelectAll_->setShortcut(QKeySequence::SelectAll);
    connect(actSelectAll_, &QAction::triggered, this,
            [this]() { if (currentEditor()) currentEditor()->selectAll(); });

    // Upstream keeps notepad++'s bindings: Ctrl+D duplicate, Ctrl+L delete.
    actDuplicateLine_ = new QAction(tr("Duplicate line"), this);
    actDuplicateLine_->setShortcut(QKeySequence("Ctrl+D"));
    connect(actDuplicateLine_, &QAction::triggered, this,
            [this]() { if (currentEditor()) currentEditor()->duplicateLine(); });

    actDeleteLine_ = new QAction(tr("Delete line"), this);
    actDeleteLine_->setShortcut(QKeySequence("Ctrl+L"));
    connect(actDeleteLine_, &QAction::triggered, this,
            [this]() { if (currentEditor()) currentEditor()->deleteLine(); });

    actEolUnix_ = new QAction(tr("Convert to Unix (LF)"), this);
    actEolUnix_->setCheckable(true);
    connect(actEolUnix_, &QAction::triggered,
            this, [this]() { setEolCrLf(false); });

    actEolWindows_ = new QAction(tr("Convert to Windows (CR LF)"), this);
    actEolWindows_->setCheckable(true);
    connect(actEolWindows_, &QAction::triggered,
            this, [this]() { setEolCrLf(true); });

    QActionGroup *eolGroup = new QActionGroup(this);
    eolGroup->addAction(actEolUnix_);
    eolGroup->addAction(actEolWindows_);

    actFind_ = new QAction(st->standardIcon(QStyle::SP_FileDialogContentsView),
                           tr("Search..."), this);
    actFind_->setShortcut(QKeySequence::Find);
    connect(actFind_, &QAction::triggered,
            this, [this]() { searchBar_->activate(false); });

    actFindNext_ = new QAction(tr("Find next"), this);
    actFindNext_->setShortcut(QKeySequence(Qt::Key_F3));
    connect(actFindNext_, &QAction::triggered,
            this, [this]() { doFind(false); });

    actFindPrev_ = new QAction(tr("Find previous"), this);
    actFindPrev_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3));
    connect(actFindPrev_, &QAction::triggered,
            this, [this]() { doFind(true); });

    actReplace_ = new QAction(tr("Replace..."), this);
    actReplace_->setShortcut(QKeySequence("Ctrl+H"));
    connect(actReplace_, &QAction::triggered,
            this, [this]() { searchBar_->activate(true); });

    actGoToLine_ = new QAction(tr("Go to line..."), this);
    actGoToLine_->setShortcut(QKeySequence("Ctrl+G"));
    connect(actGoToLine_, &QAction::triggered, this, &MainWindow::goToLine);

    actWordWrap_ = new QAction(tr("Word wrap"), this);
    actWordWrap_->setCheckable(true);
    connect(actWordWrap_, &QAction::toggled,
            this, &MainWindow::toggleWordWrap);

    actZoomIn_ = new QAction(tr("Zoom in"), this);
    actZoomIn_->setShortcut(QKeySequence::ZoomIn);
    connect(actZoomIn_, &QAction::triggered,
            this, [this]() { if (currentEditor()) currentEditor()->zoomIn(); });

    actZoomOut_ = new QAction(tr("Zoom out"), this);
    actZoomOut_->setShortcut(QKeySequence::ZoomOut);
    connect(actZoomOut_, &QAction::triggered,
            this, [this]() { if (currentEditor()) currentEditor()->zoomOut(); });

    actZoomReset_ = new QAction(tr("Restore default zoom"), this);
    actZoomReset_->setShortcut(QKeySequence("Ctrl+0"));
    connect(actZoomReset_, &QAction::triggered, this,
            [this]() { if (currentEditor()) currentEditor()->zoomReset(); });

    actFullScreen_ = new QAction(tr("Full screen"), this);
    actFullScreen_->setShortcut(QKeySequence(Qt::Key_F11));
    connect(actFullScreen_, &QAction::triggered,
            this, &MainWindow::toggleFullScreen);

    actAbout_ = new QAction(tr("About Notepadqq"), this);
    connect(actAbout_, &QAction::triggered, this, &MainWindow::about);

    // One checkable action per shipped language, upstream's Language menu.
    languageGroup_ = new QActionGroup(this);
    for (int i = 0; i < Highlighter::LanguageCount; ++i) {
        const Highlighter::Language lang = Highlighter::Language(i);
        QAction *act = new QAction(Highlighter::languageName(lang), this);
        act->setCheckable(true);
        act->setData(i);
        connect(act, &QAction::triggered,
                this, [this, lang]() { setLanguage(lang); });
        languageGroup_->addAction(act);
    }
}

void MainWindow::createMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(actNew_);
    file->addAction(actOpen_);
    file->addSeparator();
    file->addAction(actSave_);
    file->addAction(actSaveAs_);
    file->addAction(actSaveAll_);
    file->addSeparator();
    file->addAction(actCloseTab_);
    file->addAction(actCloseAll_);
    file->addSeparator();
    file->addAction(actQuit_);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(actUndo_);
    edit->addAction(actRedo_);
    edit->addSeparator();
    edit->addAction(actCut_);
    edit->addAction(actCopy_);
    edit->addAction(actPaste_);
    edit->addAction(actSelectAll_);
    edit->addSeparator();
    edit->addAction(actDuplicateLine_);
    edit->addAction(actDeleteLine_);
    edit->addSeparator();
    QMenu *eol = edit->addMenu(tr("EOL format"));
    eol->addAction(actEolUnix_);
    eol->addAction(actEolWindows_);

    QMenu *search = menuBar()->addMenu(tr("&Search"));
    search->addAction(actFind_);
    search->addAction(actFindNext_);
    search->addAction(actFindPrev_);
    search->addAction(actReplace_);
    search->addSeparator();
    search->addAction(actGoToLine_);

    QMenu *view = menuBar()->addMenu(tr("&View"));
    view->addAction(actWordWrap_);
    view->addSeparator();
    view->addAction(actZoomIn_);
    view->addAction(actZoomOut_);
    view->addAction(actZoomReset_);
    view->addSeparator();
    view->addAction(actFullScreen_);

    QMenu *language = menuBar()->addMenu(tr("&Language"));
    language->addActions(languageGroup_->actions());

    QMenu *help = menuBar()->addMenu(tr("&?"));
    help->addAction(actAbout_);
}

void MainWindow::createToolBar()
{
    QToolBar *bar = addToolBar(tr("Main"));
    bar->setMovable(false);
    bar->addAction(actNew_);
    bar->addAction(actOpen_);
    bar->addAction(actSave_);
    bar->addSeparator();
    bar->addAction(actUndo_);
    bar->addAction(actRedo_);
    bar->addSeparator();
    bar->addAction(actFind_);
}

void MainWindow::createStatusBar()
{
    statusLanguage_ = new QLabel(this);
    statusPosition_ = new QLabel(this);
    statusLength_ = new QLabel(this);
    statusEol_ = new QLabel(this);
    statusEncoding_ = new QLabel(QStringLiteral("UTF-8"), this);

    statusBar()->addWidget(statusLanguage_, 1);
    statusBar()->addPermanentWidget(statusLength_);
    statusBar()->addPermanentWidget(statusPosition_);
    statusBar()->addPermanentWidget(statusEol_);
    statusBar()->addPermanentWidget(statusEncoding_);
}

// ---- tabs -------------------------------------------------------------------

Editor *MainWindow::currentEditor() const
{
    return qobject_cast<Editor *>(tabs_->currentWidget());
}

Editor *MainWindow::editorAt(int index) const
{
    return qobject_cast<Editor *>(tabs_->widget(index));
}

Editor *MainWindow::addEditorTab()
{
    Editor *editor = new Editor(this);
    editor->setUntitledName(tr("new %1").arg(++untitledCounter_));

    connect(editor->document(), &QTextDocument::modificationChanged,
            this, [this]() { onModificationChanged(); });
    connect(editor, &QPlainTextEdit::cursorPositionChanged,
            this, &MainWindow::updateStatusBar);
    connect(editor, &QPlainTextEdit::textChanged,
            this, &MainWindow::updateStatusBar);

    const int index = tabs_->addTab(editor, editor->docName());
    tabs_->setCurrentIndex(index);
    editor->setFocus();
    return editor;
}

void MainWindow::newTab()
{
    addEditorTab();
}

void MainWindow::openFile()
{
    const Editor *cur = currentEditor();
    const QString dir = (cur && !cur->isUntitled())
        ? QFileInfo(cur->filePath()).absolutePath() : startPath();
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Open"), dir, tr("All files (*)"));
    for (const QString &path : paths)
        openPath(path);
}

void MainWindow::openPath(const QString &path)
{
    const QString abs = QFileInfo(path).absoluteFilePath();

    // Upstream re-focuses an already-open document instead of duplicating.
    for (int i = 0; i < tabs_->count(); ++i) {
        if (editorAt(i)->filePath() == abs) {
            tabs_->setCurrentIndex(i);
            return;
        }
    }

    // Reuse the tab if it is a pristine untitled one - upstream does.
    Editor *editor = currentEditor();
    const bool reuse = editor && editor->isUntitled() &&
                       !editor->document()->isModified() &&
                       editor->document()->isEmpty();
    if (!reuse)
        editor = addEditorTab();

    QString error;
    if (!editor->load(abs, &error)) {
        if (!reuse)
            closeTab(tabs_->indexOf(editor));
        QMessageBox::critical(this, tr("Open"),
                              tr("Cannot open %1: %2").arg(abs, error));
        return;
    }
    updateTabTitle(editor);
    onTabChanged(tabs_->currentIndex());
}

bool MainWindow::save(Editor *editor)
{
    if (!editor)
        return false;
    if (editor->isUntitled())
        return saveAs(editor);

    QString error;
    if (!editor->save(editor->filePath(), &error)) {
        QMessageBox::critical(this, tr("Save"),
                              tr("Cannot save %1: %2")
                                  .arg(editor->filePath(), error));
        return false;
    }
    updateTabTitle(editor);
    return true;
}

bool MainWindow::saveAs(Editor *editor)
{
    if (!editor)
        return false;
    const QString dir = editor->isUntitled()
        ? startPath() + QLatin1Char('/') + editor->docName()
        : editor->filePath();
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save as"), dir, tr("All files (*)"));
    if (path.isEmpty())
        return false;

    QString error;
    if (!editor->save(path, &error)) {
        QMessageBox::critical(this, tr("Save"),
                              tr("Cannot save %1: %2").arg(path, error));
        return false;
    }
    updateTabTitle(editor);
    onTabChanged(tabs_->currentIndex());
    return true;
}

void MainWindow::saveAll()
{
    for (int i = 0; i < tabs_->count(); ++i) {
        Editor *editor = editorAt(i);
        if (editor->document()->isModified())
            save(editor);
    }
}

bool MainWindow::maybeClose(Editor *editor)
{
    if (!editor->document()->isModified())
        return true;

    tabs_->setCurrentWidget(editor);
    const QMessageBox::StandardButton ret = QMessageBox::warning(
        this, tr("Save changes"),
        tr("%1 has unsaved changes. Save them?").arg(editor->docName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (ret == QMessageBox::Save)
        return save(editor);
    return ret == QMessageBox::Discard;
}

void MainWindow::closeTab(int index)
{
    Editor *editor = editorAt(index);
    if (!editor || !maybeClose(editor))
        return;
    tabs_->removeTab(index);
    editor->deleteLater();

    // Upstream never shows an empty tab bar.
    if (tabs_->count() == 0)
        addEditorTab();
}

void MainWindow::closeAllTabs()
{
    for (int i = tabs_->count() - 1; i >= 0; --i)
        closeTab(i);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    for (int i = 0; i < tabs_->count(); ++i) {
        if (!maybeClose(editorAt(i))) {
            event->ignore();
            return;
        }
    }
    event->accept();
}

// ---- search -----------------------------------------------------------------

bool MainWindow::doFind(bool backward, bool wrap)
{
    Editor *editor = currentEditor();
    const QString term = searchBar_->searchText();
    if (!editor || term.isEmpty())
        return false;

    QTextDocument::FindFlags flags;
    if (backward)
        flags |= QTextDocument::FindBackward;
    if (searchBar_->caseSensitive())
        flags |= QTextDocument::FindCaseSensitively;
    if (searchBar_->wholeWords())
        flags |= QTextDocument::FindWholeWords;

    QTextCursor found;
    if (searchBar_->useRegex()) {
        QRegularExpression re(term,
            searchBar_->caseSensitive()
                ? QRegularExpression::NoPatternOption
                : QRegularExpression::CaseInsensitiveOption);
        if (!re.isValid()) {
            searchBar_->setNotFound(true);
            return false;
        }
        found = editor->document()->find(re, editor->textCursor(), flags);
    } else {
        found = editor->document()->find(term, editor->textCursor(), flags);
    }

    if (found.isNull() && wrap) {
        // Wrap around once, from the far end.
        QTextCursor from(editor->document());
        if (backward)
            from.movePosition(QTextCursor::End);
        if (searchBar_->useRegex()) {
            QRegularExpression re(term,
                searchBar_->caseSensitive()
                    ? QRegularExpression::NoPatternOption
                    : QRegularExpression::CaseInsensitiveOption);
            found = editor->document()->find(re, from, flags);
        } else {
            found = editor->document()->find(term, from, flags);
        }
    }

    if (found.isNull()) {
        searchBar_->setNotFound(true);
        return false;
    }
    searchBar_->setNotFound(false);
    editor->setTextCursor(found);
    return true;
}

void MainWindow::doReplace()
{
    Editor *editor = currentEditor();
    if (!editor)
        return;
    // Replace the current match (if the selection is one), then move on.
    QTextCursor cur = editor->textCursor();
    if (cur.hasSelection()) {
        const QString sel = cur.selectedText();
        const QString term = searchBar_->searchText();
        bool matches;
        if (searchBar_->useRegex()) {
            QRegularExpression re(QLatin1String("\\A(?:") + term +
                                  QLatin1String(")\\z"),
                searchBar_->caseSensitive()
                    ? QRegularExpression::NoPatternOption
                    : QRegularExpression::CaseInsensitiveOption);
            matches = re.isValid() && re.match(sel).hasMatch();
        } else {
            matches = QString::compare(sel, term,
                searchBar_->caseSensitive() ? Qt::CaseSensitive
                                            : Qt::CaseInsensitive) == 0;
        }
        if (matches)
            cur.insertText(searchBar_->replaceText());
    }
    doFind(false);
}

void MainWindow::doReplaceAll()
{
    Editor *editor = currentEditor();
    if (!editor)
        return;

    QTextCursor cur = editor->textCursor();
    cur.movePosition(QTextCursor::Start);
    editor->setTextCursor(cur);

    int count = 0;
    cur.beginEditBlock();
    while (doFind(false, false)) {
        QTextCursor match = editor->textCursor();
        match.insertText(searchBar_->replaceText());
        ++count;
    }
    cur.endEditBlock();

    statusBar()->showMessage(tr("%1 occurrence(s) replaced").arg(count), 3000);
}

void MainWindow::goToLine()
{
    Editor *editor = currentEditor();
    if (!editor)
        return;
    bool ok = false;
    const int line = QInputDialog::getInt(
        this, tr("Go to line"), tr("Line number:"),
        editor->textCursor().blockNumber() + 1,
        1, editor->document()->blockCount(), 1, &ok);
    if (!ok)
        return;
    QTextCursor cur(editor->document()->findBlockByNumber(line - 1));
    editor->setTextCursor(cur);
    editor->setFocus();
}

// ---- state ------------------------------------------------------------------

void MainWindow::setLanguage(Highlighter::Language lang)
{
    if (currentEditor())
        currentEditor()->setLanguage(lang);
    updateStatusBar();
}

void MainWindow::setEolCrLf(bool crlf)
{
    if (currentEditor())
        currentEditor()->setEolCrLf(crlf);
    updateStatusBar();
}

void MainWindow::toggleWordWrap(bool wrap)
{
    for (int i = 0; i < tabs_->count(); ++i)
        editorAt(i)->setLineWrapMode(wrap ? QPlainTextEdit::WidgetWidth
                                          : QPlainTextEdit::NoWrap);
}

void MainWindow::toggleFullScreen()
{
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
}

void MainWindow::about()
{
    QMessageBox::about(this, tr("About Notepadqq"),
        tr("<b>Notepadqq</b> for EwokOS<br><br>"
           "A rewrite of <a href=\"https://github.com/notepadqq/"
           "notepadqq\">notepadqq</a> (GPL-3.0) on plain QtWidgets: "
           "upstream's CodeMirror/QtWebEngine editor core has no "
           "substrate on EwokOS, so the editor is a QPlainTextEdit "
           "with native highlighting."));
}

// ---- readouts ---------------------------------------------------------------

void MainWindow::onTabChanged(int index)
{
    Q_UNUSED(index);
    updateWindowTitle();
    updateStatusBar();
    updateDocDependentActions();
}

void MainWindow::onModificationChanged()
{
    for (int i = 0; i < tabs_->count(); ++i)
        updateTabTitle(editorAt(i));
    updateWindowTitle();
}

void MainWindow::updateTabTitle(Editor *editor)
{
    const int index = tabs_->indexOf(editor);
    if (index < 0)
        return;
    QString title = editor->docName();
    if (editor->document()->isModified())
        title += QLatin1Char('*');      // upstream's unsaved marker
    tabs_->setTabText(index, title);
    tabs_->setTabToolTip(index, editor->isUntitled() ? editor->docName()
                                                     : editor->filePath());
}

void MainWindow::updateWindowTitle()
{
    Editor *editor = currentEditor();
    if (!editor) {
        setWindowTitle(QStringLiteral("Notepadqq"));
        return;
    }
    setWindowTitle(tr("%1%2 - Notepadqq")
                       .arg(editor->docName(),
                            editor->document()->isModified()
                                ? QStringLiteral("*") : QString()));
}

void MainWindow::updateStatusBar()
{
    Editor *editor = currentEditor();
    if (!editor)
        return;

    statusLanguage_->setText(Highlighter::languageName(editor->language()));

    const QTextCursor cur = editor->textCursor();
    statusPosition_->setText(tr("Ln %1, Col %2  Sel %3")
        .arg(cur.blockNumber() + 1)
        .arg(cur.positionInBlock() + 1)
        .arg(cur.selectionEnd() - cur.selectionStart()));

    statusLength_->setText(tr("length: %1  lines: %2")
        .arg(editor->document()->characterCount() - 1)
        .arg(editor->document()->blockCount()));

    statusEol_->setText(editor->eolCrLf() ? tr("Windows (CR LF)")
                                          : tr("Unix (LF)"));
    updateDocDependentActions();
}

void MainWindow::updateDocDependentActions()
{
    Editor *editor = currentEditor();
    if (!editor)
        return;
    actEolWindows_->setChecked(editor->eolCrLf());
    actEolUnix_->setChecked(!editor->eolCrLf());
    const QList<QAction *> langActions = languageGroup_->actions();
    const int lang = int(editor->language());
    if (lang >= 0 && lang < langActions.size())
        langActions.at(lang)->setChecked(true);
}
