#ifndef NQQ_MAINWINDOW_H
#define NQQ_MAINWINDOW_H

/*
 * The notepadqq main window: menu bar (File/Edit/Search/View/Language/?),
 * a small toolbar, the closable+movable document tabs, the bottom
 * search/replace bar and the status bar with the language, Ln/Col,
 * length/lines, EOL-format and encoding readouts - the same shape as
 * upstream's MainWindow, minus what has no substrate on EwokOS (sessions,
 * remote files, the extension system, printing, multiple windows).
 */

#include <QMainWindow>

#include "highlighter.h"

class Editor;
class QAction;
class QActionGroup;
class QLabel;
class QTabWidget;
class SearchBar;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    void openPath(const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createActions();
    void createMenus();
    void createToolBar();
    void createStatusBar();

    Editor *currentEditor() const;
    Editor *editorAt(int index) const;
    Editor *addEditorTab();             // a fresh "new N" tab

    void newTab();
    void openFile();
    bool save(Editor *editor);
    bool saveAs(Editor *editor);
    void saveAll();
    bool maybeClose(Editor *editor);    // save-prompt; false = cancelled
    void closeTab(int index);
    void closeAllTabs();

    bool doFind(bool backward, bool wrap = true);
    void doReplace();
    void doReplaceAll();
    void goToLine();

    void setLanguage(Highlighter::Language lang);
    void setEolCrLf(bool crlf);
    void toggleWordWrap(bool wrap);
    void toggleFullScreen();
    void about();

    void onTabChanged(int index);
    void onModificationChanged();
    void updateTabTitle(Editor *editor);
    void updateWindowTitle();
    void updateStatusBar();
    void updateDocDependentActions();

    QTabWidget *tabs_;
    SearchBar *searchBar_;
    int untitledCounter_;

    QLabel *statusLanguage_;    // "C++"
    QLabel *statusPosition_;    // "Ln 12, Col 4  Sel 0"
    QLabel *statusLength_;      // "length: 512  lines: 40"
    QLabel *statusEol_;         // "Unix (LF)" / "Windows (CR LF)"
    QLabel *statusEncoding_;    // "UTF-8"

    QAction *actNew_;
    QAction *actOpen_;
    QAction *actSave_;
    QAction *actSaveAs_;
    QAction *actSaveAll_;
    QAction *actCloseTab_;
    QAction *actCloseAll_;
    QAction *actQuit_;
    QAction *actUndo_;
    QAction *actRedo_;
    QAction *actCut_;
    QAction *actCopy_;
    QAction *actPaste_;
    QAction *actSelectAll_;
    QAction *actDuplicateLine_;
    QAction *actDeleteLine_;
    QAction *actEolUnix_;
    QAction *actEolWindows_;
    QAction *actFind_;
    QAction *actFindNext_;
    QAction *actFindPrev_;
    QAction *actReplace_;
    QAction *actGoToLine_;
    QAction *actWordWrap_;
    QAction *actZoomIn_;
    QAction *actZoomOut_;
    QAction *actZoomReset_;
    QAction *actFullScreen_;
    QAction *actAbout_;
    QActionGroup *languageGroup_;
};

#endif // NQQ_MAINWINDOW_H
