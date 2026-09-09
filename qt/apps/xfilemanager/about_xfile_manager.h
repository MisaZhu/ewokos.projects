#ifndef ABOUT_XFILE_MANAGER_H
#define ABOUT_XFILE_MANAGER_H

#include <QDialog>

namespace Ui {
class About_XFile_Manager;
}

class About_XFile_Manager : public QDialog
{
    Q_OBJECT

public:
    explicit About_XFile_Manager(QWidget *parent = nullptr);
    ~About_XFile_Manager();

private slots:
    void on_contribute_button_clicked();

private:
    Ui::About_XFile_Manager *ui;
};

#endif // ABOUT_XFILE_MANAGER_H
