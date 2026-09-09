#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QFileSystemModel>
#include <QStack>
#include <QStandardPaths>
#include "about_xfile_manager.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_home_clicked();

    void on_C_drive_clicked();

    void on_Doc_btn_clicked();

    void on_images_btn_clicked();

    void on_Music_btn_clicked();

    void on_Video_btn_clicked();

    void on_Downlaod_btn_clicked();

    void on_back_btn_clicked();

    void on_forward_btn_clicked();

    void on_listView_doubleClicked(const QModelIndex &index);

    void on_copy_path_btn_clicked();

    void on_system_vol_btn_clicked();

    void on_home_main_btn_clicked();

    //void handleItemDoubleClicked(const QModelIndex &index);

    void on_info_svg_clicked();

private:
    void setupFileSystemView(const QString &path);
    void navigateTo(const QString &path);
    void navigateToStandard(QStandardPaths::StandardLocation location);

    Ui::MainWindow *ui;

    // Declare the file system model here
    QFileSystemModel *fileSystemModel;

    // Current path for navigation
    QString currentPath;

    // Stacks for navigation history
    QStack<QString> *backStack;
    QStack<QString> *forwardStack;

    //About_XFile_Manager pointer
    About_XFile_Manager *about_xfile;
};
#endif // MAINWINDOW_H
