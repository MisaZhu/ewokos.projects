#include "about_xfile_manager.h"
#include "ui_about_xfile_manager.h"
#include <QDesktopServices>
#include <QUrl>

About_XFile_Manager::About_XFile_Manager(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::About_XFile_Manager)
{
    ui->setupUi(this);
}

About_XFile_Manager::~About_XFile_Manager()
{
    delete ui;
}

void About_XFile_Manager::on_contribute_button_clicked()
{
    QDesktopServices::openUrl(QUrl("https://github.com/arshc0der/XFile_Manager"));
}

