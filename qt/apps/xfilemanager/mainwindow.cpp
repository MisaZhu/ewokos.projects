/*
 * XFile Manager, ported to EwokOS.
 *
 * Upstream: https://github.com/arshc0der/XFile_Manager (MIT, see LICENSE in
 * this directory).  Port-specific changes are confined to navigation: EwokOS
 * exports no $HOME and has no passwd database, so QStandardPaths locations
 * come back empty or non-existent and navigateToStandard() falls back to the
 * filesystem root instead of leaving a dead sidebar button.
 */

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileSystemModel>
#include <QListView>
#include <QTreeView>
#include <QIcon>
#include <QDir>
#include <QStandardPaths>
#include <QStack>
#include <QDesktopServices>
#include <QUrl>
#include <QClipboard>
#include <QApplication>
#include <QTimer>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QDebug>
#include <QSysInfo>
#include <QLabel>

// Custom QFileSystemModel to override folder icons
class CustomFileSystemModel : public QFileSystemModel {
public:
    explicit CustomFileSystemModel(QObject *parent = nullptr) : QFileSystemModel(parent) {}

    // Override data function to provide custom folder icons
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override {
        if (role == Qt::DecorationRole && isDir(index)) {
            return QIcon(":/Res/icons/empty_folder.png");  // Custom folder icon
        }
        return QFileSystemModel::data(index, role);
    }
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , currentPath("")  // Initialize currentPath
{
    ui->setupUi(this);

    // Initialize history stacks for navigation
    backStack = new QStack<QString>();
    forwardStack = new QStack<QString>();

    // Create and set up the custom file system model
    fileSystemModel = new CustomFileSystemModel(this);
    fileSystemModel->setRootPath(QDir::rootPath());  // Set the model's root path

    // Set up the QTreeView and QListView with the model
    ui->treeView->setModel(fileSystemModel);
    ui->listView->setModel(fileSystemModel);

    // Configure QListView appearance
    ui->listView->setViewMode(QListView::IconMode);
    ui->listView->setIconSize(QSize(64, 64));  // Set icon size
    ui->listView->setGridSize(QSize(100, 100)); // Set grid size

    // Fetch and display system information in a scrollable area
    QWidget *infoWidget = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(infoWidget);

    // Fetch and display various system information
    layout->addWidget(new QLabel("Operating System: " + QSysInfo::prettyProductName(), this));
    layout->addWidget(new QLabel("CPU Architecture: " + QSysInfo::currentCpuArchitecture(), this));
    layout->addWidget(new QLabel("Kernel Type: " + QSysInfo::kernelType(), this));
    layout->addWidget(new QLabel("Kernel Version: " + QSysInfo::kernelVersion(), this));
    layout->addWidget(new QLabel("Current Host Name: " + QSysInfo::machineHostName(), this));
    layout->addWidget(new QLabel("Product Type: " + QSysInfo::productType(), this));
    layout->addWidget(new QLabel("Product Version: " + QSysInfo::productVersion(), this));
    layout->addWidget(new QLabel("Machine Type: " + QSysInfo::currentCpuArchitecture(), this));
    layout->addWidget(new QLabel("System Locale: " + QLocale::system().name(), this));
    layout->addWidget(new QLabel("User Name: " + QDir::home().dirName(), this));

    // Set layout for the infoWidget
    infoWidget->setLayout(layout);

    // Set the infoWidget in the scroll area and make it resizable
    ui->scrollArea_system_info->setWidget(infoWidget);
    ui->scrollArea_system_info->setWidgetResizable(true);
}

MainWindow::~MainWindow()
{
    delete ui;  // Clean up UI
}

void MainWindow::setupFileSystemView(const QString &path) {
    ui->current_path->setText(path); // Display the current path

    // Set the root directory for both views
    ui->treeView->setRootIndex(fileSystemModel->index(path));
    ui->listView->setRootIndex(fileSystemModel->index(path));
}

void MainWindow::on_home_clicked() {
    navigateToStandard(QStandardPaths::DesktopLocation); // Navigate to Home
}

void MainWindow::on_C_drive_clicked() {
    navigateTo(QDir::rootPath()); // Navigate to C drive
}

void MainWindow::on_Doc_btn_clicked() {
    navigateToStandard(QStandardPaths::DocumentsLocation); // Navigate to Documents
}

void MainWindow::on_images_btn_clicked() {
    navigateToStandard(QStandardPaths::PicturesLocation); // Navigate to Pictures
}

void MainWindow::on_Music_btn_clicked() {
    navigateToStandard(QStandardPaths::MusicLocation); // Navigate to Music
}

void MainWindow::on_Video_btn_clicked() {
    navigateToStandard(QStandardPaths::MoviesLocation); // Navigate to Videos
}

void MainWindow::on_Downlaod_btn_clicked() {
    navigateToStandard(QStandardPaths::DownloadLocation); // Navigate to Downloads
}

void MainWindow::navigateTo(const QString &path) {
    if (!path.isEmpty()) {
        // Push the current path to the back stack before navigating
        if (!currentPath.isEmpty()) {
            backStack->push(currentPath);
        }

        // Clear the forward stack when navigating to a new path
        forwardStack->clear();

        // Set the new path and update the views
        currentPath = path;
        setupFileSystemView(path);
    }
}

void MainWindow::navigateToStandard(QStandardPaths::StandardLocation location) {
    // See the header comment: without $HOME or getpwuid() these paths are
    // empty (or point at directories nobody created) on EwokOS.
    QString path = QStandardPaths::writableLocation(location);
    if (path.isEmpty() || !QDir(path).exists())
        path = QDir::rootPath();
    navigateTo(path);
}

void MainWindow::on_back_btn_clicked() {
    if (!backStack->isEmpty()) {
        // Move current path to forward stack
        forwardStack->push(currentPath);

        // Get the last path from the back stack
        currentPath = backStack->pop();

        // Update the view
        setupFileSystemView(currentPath);
    }
}

void MainWindow::on_forward_btn_clicked() {
    if (!forwardStack->isEmpty()) {
        // Move current path to back stack
        backStack->push(currentPath);

        // Get the next path from the forward stack
        currentPath = forwardStack->pop();

        // Update the view
        setupFileSystemView(currentPath);
    }
}

void MainWindow::on_listView_doubleClicked(const QModelIndex &index) {
    if (!index.isValid()) return; // Ensure valid index

    QString filePath = fileSystemModel->filePath(index);

    // Check if it's a directory
    if (fileSystemModel->isDir(index)) {
        // Navigate to the directory
        navigateTo(filePath);
    } else {
        // Open the file using the default application
        if (currentPath != filePath) { // Prevent opening the same file twice
            QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
        }
    }
}

void MainWindow::on_copy_path_btn_clicked()
{
    // Get the current path from the label
    QString current_path = ui->current_path->text();

    // Get the application's clipboard
    QClipboard *clipboard = QApplication::clipboard();

    // Copy the text to the clipboard
    clipboard->setText(current_path);

    // Change button text to "copied!"
    ui->copy_path_btn->setText("copied!");

    // Create a QTimer to revert the text after 0.5 seconds (500 milliseconds)
    QTimer::singleShot(500, this, [this]() {
        ui->copy_path_btn->setText("Copy Path"); // Revert button text
    });
}

void MainWindow::on_system_vol_btn_clicked()
{
    // Reset the file system model and set the root path
    fileSystemModel = new CustomFileSystemModel(this);
    fileSystemModel->setRootPath(QDir::rootPath());

    // Set up the QTreeView and QListView with the new model
    ui->treeView->setModel(fileSystemModel);
    ui->listView->setModel(fileSystemModel);

    // Configure QListView appearance
    ui->listView->setViewMode(QListView::IconMode);
    ui->listView->setIconSize(QSize(64, 64));  // Set icon size
    ui->listView->setGridSize(QSize(100, 100)); // Set grid size
}

void MainWindow::on_home_main_btn_clicked()
{
    // Clear any existing items in the list widget
    ui->listWidget->clear();

    // Create a widget to hold the buttons
    QWidget *buttonWidget = new QWidget(this);
    QGridLayout *layout = new QGridLayout(buttonWidget); // Create a grid layout

    // Create a mapping of standard folders to their corresponding QStandardPaths::StandardLocation
    QMap<QString, QStandardPaths::StandardLocation> standardFolders; // Pair of folder name and standard location
    standardFolders.insert("Desktop", QStandardPaths::DesktopLocation);
    standardFolders.insert("Documents", QStandardPaths::DocumentsLocation);
    standardFolders.insert("Downloads", QStandardPaths::DownloadLocation);
    standardFolders.insert("Pictures", QStandardPaths::PicturesLocation);
    standardFolders.insert("Music", QStandardPaths::MusicLocation);
    standardFolders.insert("Videos", QStandardPaths::MoviesLocation);

    int row = 0;
    int col = 0;

    // Add buttons to the grid layout
    for (const auto &folder : standardFolders.keys()) {
        const QString &folderName = folder;

        QPushButton *button = new QPushButton(folderName);
        button->setIcon(QIcon(":/Res/icons/" + folderName.toLower() + "_folder.png")); // Dynamically set the icon based on folder name
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed); // Keep button size fixed

        // Connect the button's clicked signal to a slot to handle the action
        connect(button, &QPushButton::clicked, this, [this, folderName, standardFolders]() {
            // Navigate to the appropriate folder using the mapped value
            navigateToStandard(standardFolders[folderName]);
        });

        layout->addWidget(button, row, col); // Add the button to the grid layout

        col++; // Move to the next column

        // Move to the next row if the column limit is reached (e.g., 3 columns)
        if (col >= 3) {
            col = 0;
            row++;
        }
    }

    // Set the layout for the button widget
    buttonWidget->setLayout(layout);

    // Create a QListWidgetItem to hold the button widget
    QListWidgetItem *item = new QListWidgetItem();
    item->setSizeHint(buttonWidget->sizeHint()); // Set size hint for the item

    // Add the item to the list widget and set the button widget as its item widget
    ui->listWidget->addItem(item);
    ui->listWidget->setItemWidget(item, buttonWidget); // Set the button widget as the widget for the item
}


void MainWindow::on_info_svg_clicked()
{
    //showing the about window to user
    about_xfile = new About_XFile_Manager(this);
    about_xfile->show();
}

