#ifndef NOMACS_MAINWINDOW_H
#define NOMACS_MAINWINDOW_H

/*
 * The nomacs main window: menu bar (File/Edit/View/Help), a small toolbar,
 * the viewport in the centre, the thumbnail preview strip along the bottom
 * and a status bar with the position / size / zoom readout - the same shape
 * as upstream's DkNoMacs window, minus what has no substrate on EwokOS
 * (metadata panels, plugins, network sync, printing).
 */

#include <QMainWindow>

#include "imageloader.h"

class QAction;
class QLabel;
class QTimer;
class ThumbnailBar;
class Viewport;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString &path, QWidget *parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void createActions();
    void createMenus();
    void createToolBar();

    void openFile();
    void openFolder();
    void saveAs();
    void renameFile();
    void deleteFile();
    void toggleFullScreen();
    void toggleSlideshow();
    void about();

    void onImageChanged();      // loader -> viewport, title, status bar
    void onFolderChanged();     // loader -> thumbnail strip
    void updateStatusBar();

    ImageLoader *loader_;
    Viewport *viewport_;
    ThumbnailBar *thumbs_;
    QTimer *slideshowTimer_;

    QLabel *statusFile_;        // "3/24  photo.jpg"
    QLabel *statusInfo_;        // "1024x768  213.4 kB  50%"

    QAction *actOpen_;
    QAction *actOpenFolder_;
    QAction *actSaveAs_;
    QAction *actRename_;
    QAction *actDelete_;
    QAction *actReload_;
    QAction *actQuit_;
    QAction *actRotateCw_;
    QAction *actRotateCcw_;
    QAction *actFlipH_;
    QAction *actFlipV_;
    QAction *actPrev_;
    QAction *actNext_;
    QAction *actFirst_;
    QAction *actLast_;
    QAction *actZoomIn_;
    QAction *actZoomOut_;
    QAction *actZoomFit_;
    QAction *actZoom100_;
    QAction *actFullScreen_;
    QAction *actSlideshow_;
    QAction *actShowThumbs_;
    QAction *actAbout_;
};

#endif // NOMACS_MAINWINDOW_H
