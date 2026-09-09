/*
 * File operations and native launching for the PCManFM-Qt port.
 * See fileops.h for what stands in for which libfm-qt piece.
 */

#include "fileops.h"

#include <functional>

#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QVBoxLayout>

#include <stdlib.h>
#include <unistd.h>

#include <ewoksys/proc.h>

namespace FileOps {

// ---- clipboard --------------------------------------------------------------

static QStringList s_clipPaths;
static bool s_clipCut = false;

void setClipboard(const QStringList &paths, bool cut)
{
    s_clipPaths = paths;
    s_clipCut = cut;
}

QStringList clipboardPaths() { return s_clipPaths; }
bool clipboardIsCut()        { return s_clipCut; }
bool clipboardEmpty()        { return s_clipPaths.isEmpty(); }
void clearClipboard()        { s_clipPaths.clear(); s_clipCut = false; }

// ---- recursive copy/move/delete ---------------------------------------------

// One tick of progress per filesystem entry, so the count and the work loop
// have to walk the very same set: the entry itself plus, for a directory,
// everything below it.
static int countEntries(const QString &path)
{
    int n = 1;
    QFileInfo info(path);
    if (info.isDir() && !info.isSymLink()) {
        QDirIterator it(path, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            ++n;
        }
    }
    return n;
}

static bool tick(QProgressDialog &dlg)
{
    dlg.setValue(dlg.value() + 1);
    QCoreApplication::processEvents();
    return !dlg.wasCanceled();
}

static bool copyEntry(const QString &src, const QString &dst,
                      QProgressDialog &dlg, QString &err)
{
    QFileInfo info(src);
    if (info.isDir() && !info.isSymLink()) {
        if (!QDir().mkpath(dst)) {
            err = QObject::tr("Cannot create folder %1").arg(dst);
            return false;
        }
        if (!tick(dlg))
            return false;
        const QFileInfoList entries = QDir(src).entryInfoList(
            QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
        for (const QFileInfo &e : entries) {
            if (!copyEntry(e.absoluteFilePath(), dst + "/" + e.fileName(), dlg, err))
                return false;
        }
        return true;
    }
    if (QFile::exists(dst))
        QFile::remove(dst);
    if (!QFile::copy(src, dst)) {
        err = QObject::tr("Cannot copy %1").arg(src);
        return false;
    }
    return tick(dlg);
}

static bool deleteEntry(const QString &path, QProgressDialog &dlg, QString &err)
{
    QFileInfo info(path);
    if (info.isDir() && !info.isSymLink()) {
        const QFileInfoList entries = QDir(path).entryInfoList(
            QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
        for (const QFileInfo &e : entries) {
            if (!deleteEntry(e.absoluteFilePath(), dlg, err))
                return false;
        }
        if (!QDir().rmdir(path)) {
            err = QObject::tr("Cannot remove folder %1").arg(path);
            return false;
        }
        return tick(dlg);
    }
    if (!QFile::remove(path)) {
        err = QObject::tr("Cannot delete %1").arg(path);
        return false;
    }
    return tick(dlg);
}

// Copying something into itself would recurse forever; upstream refuses the
// same way.
static bool destInsideSource(const QString &src, const QString &destDir)
{
    const QString s = QDir(src).absolutePath() + "/";
    const QString d = QDir(destDir).absolutePath() + "/";
    return d.startsWith(s);
}

static QString destName(const QString &src, const QString &destDir)
{
    return QDir(destDir).absolutePath() + "/" + QFileInfo(src).fileName();
}

static bool runOp(const QString &title, const QStringList &sources, QWidget *parent,
                  const std::function<bool(const QString &, QProgressDialog &, QString &)> &op)
{
    int total = 0;
    for (const QString &s : sources)
        total += countEntries(s);

    QProgressDialog dlg(title, QObject::tr("Cancel"), 0, total, parent);
    dlg.setWindowModality(Qt::WindowModal);
    dlg.setMinimumDuration(500);
    dlg.setValue(0);

    QString err;
    for (const QString &s : sources) {
        if (!op(s, dlg, err)) {
            dlg.close();
            if (!err.isEmpty())
                QMessageBox::warning(parent, QObject::tr("Error"), err);
            return false;
        }
    }
    dlg.setValue(total);
    return true;
}

bool copyPaths(const QStringList &sources, const QString &destDir, QWidget *parent)
{
    for (const QString &s : sources) {
        if (destInsideSource(s, destDir)) {
            QMessageBox::warning(parent, QObject::tr("Error"),
                                 QObject::tr("Cannot copy a folder into itself."));
            return false;
        }
    }
    return runOp(QObject::tr("Copying files..."), sources, parent,
                 [&destDir](const QString &src, QProgressDialog &dlg, QString &err) {
                     return copyEntry(src, destName(src, destDir), dlg, err);
                 });
}

bool movePaths(const QStringList &sources, const QString &destDir, QWidget *parent)
{
    for (const QString &s : sources) {
        if (destInsideSource(s, destDir)) {
            QMessageBox::warning(parent, QObject::tr("Error"),
                                 QObject::tr("Cannot move a folder into itself."));
            return false;
        }
    }
    return runOp(QObject::tr("Moving files..."), sources, parent,
                 [&destDir](const QString &src, QProgressDialog &dlg, QString &err) {
                     const QString dst = destName(src, destDir);
                     if (dst == src)
                         return true;
                     // Same-filesystem rename first - EwokOS has one rootfs, so
                     // this is the common case; fall back to copy + delete.
                     if (QFile::rename(src, dst))
                         return tick(dlg);
                     if (!copyEntry(src, dst, dlg, err))
                         return false;
                     return deleteEntry(src, dlg, err);
                 });
}

bool deletePaths(const QStringList &paths, QWidget *parent)
{
    return runOp(QObject::tr("Deleting files..."), paths, parent,
                 [](const QString &p, QProgressDialog &dlg, QString &err) {
                     return deleteEntry(p, dlg, err);
                 });
}

// ---- launching --------------------------------------------------------------

void launch(const QString &cmdLine, const QString &appName)
{
    const QByteArray cmd = cmdLine.toLocal8Bit();
    const QByteArray app = appName.toLocal8Bit();
    int pid = fork();
    if (pid == 0) {
        proc_detach();
        setenv("X_APP_NAME", app.constData());
        proc_exec(cmd.constData());
        exit(0);
    }
}

static bool isElf(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray magic = f.read(4);
    return magic == QByteArrayLiteral("\x7f" "ELF");
}

// ext (with the leading dot) -> open_with command, from the same registry
// FileWidget/xfinder use.
static QString openWithFor(const QString &fileName)
{
    static bool loaded = false;
    static QList<QPair<QString, QString>> types;
    if (!loaded) {
        loaded = true;
        QFile f("/usr/system/filetypes.json");
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
            for (const QJsonValue &v : arr) {
                const QJsonObject o = v.toObject();
                const QString ext = o.value("ext").toString();
                const QString cmd = o.value("open_with").toString();
                if (!ext.isEmpty() && !cmd.isEmpty())
                    types.append(qMakePair(ext, cmd));
            }
        }
    }
    for (const auto &t : types) {
        if (fileName.endsWith(t.first, Qt::CaseInsensitive))
            return t.second;
    }
    return QString();
}

void openFile(const QString &path, QWidget *parent)
{
    if (isElf(path)) {
        launch(path, path);
        return;
    }
    const QString cmd = openWithFor(QFileInfo(path).fileName());
    if (cmd.isEmpty()) {
        QMessageBox::information(parent, QObject::tr("Open"),
                                 QObject::tr("No application registered for %1.")
                                     .arg(QFileInfo(path).fileName()));
        return;
    }
    launch(QString("%1 \"%2\"").arg(cmd, path), cmd);
}

// ---- properties -------------------------------------------------------------

QString humanSize(qint64 bytes)
{
    if (bytes >= 1024LL * 1024 * 1024)
        return QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 1) + " GB";
    if (bytes >= 1024 * 1024)
        return QString::number(bytes / (1024.0 * 1024), 'f', 1) + " MB";
    if (bytes >= 1024)
        return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    return QString::number(bytes) + " B";
}

static QString permString(QFile::Permissions p)
{
    QString s;
    s += (p & QFile::ReadOwner)  ? 'r' : '-';
    s += (p & QFile::WriteOwner) ? 'w' : '-';
    s += (p & QFile::ExeOwner)   ? 'x' : '-';
    s += (p & QFile::ReadGroup)  ? 'r' : '-';
    s += (p & QFile::WriteGroup) ? 'w' : '-';
    s += (p & QFile::ExeGroup)   ? 'x' : '-';
    s += (p & QFile::ReadOther)  ? 'r' : '-';
    s += (p & QFile::WriteOther) ? 'w' : '-';
    s += (p & QFile::ExeOther)   ? 'x' : '-';
    return s;
}

void showProperties(const QString &path, QWidget *parent)
{
    QFileInfo info(path);

    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("File Properties"));

    QFormLayout *form = new QFormLayout;
    form->addRow(QObject::tr("Name:"), new QLabel(info.fileName().isEmpty()
                                                      ? QStringLiteral("/")
                                                      : info.fileName()));
    form->addRow(QObject::tr("Location:"), new QLabel(info.absolutePath()));
    if (info.isDir()) {
        const int items = QDir(path).entryList(
            QDir::AllEntries | QDir::NoDotAndDotDot).count();
        form->addRow(QObject::tr("Type:"), new QLabel(QObject::tr("Folder")));
        form->addRow(QObject::tr("Contents:"), new QLabel(
            QObject::tr("%1 items").arg(items)));
    } else {
        form->addRow(QObject::tr("Type:"), new QLabel(info.suffix().isEmpty()
            ? QObject::tr("File")
            : QObject::tr("%1 file").arg(info.suffix().toUpper())));
        form->addRow(QObject::tr("Size:"), new QLabel(
            QString("%1 (%2 bytes)").arg(humanSize(info.size())).arg(info.size())));
    }
    form->addRow(QObject::tr("Modified:"), new QLabel(
        info.lastModified().toString("yyyy-MM-dd hh:mm:ss")));
    form->addRow(QObject::tr("Permissions:"), new QLabel(
        permString(info.permissions())));

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok);
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);

    QVBoxLayout *layout = new QVBoxLayout(&dlg);
    layout->addLayout(form);
    layout->addWidget(box);

    dlg.exec();
}

} // namespace FileOps
