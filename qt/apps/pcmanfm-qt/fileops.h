#ifndef PCMANFM_FILEOPS_H
#define PCMANFM_FILEOPS_H

/*
 * The file-operation layer of the port: what upstream gets from libfm-qt's
 * Fm::FileOperation and Fm::FileLauncher is provided here on plain Qt file
 * APIs plus the native EwokOS launch mechanism (fork + proc_exec against
 * /usr/system/filetypes.json, the same contract xfinder implements).
 *
 * The clipboard is application-internal.  The EwokOS QPA plugin has no
 * cross-process clipboard to talk to, so QClipboard would only ever reach
 * this process anyway - a static paths+cut pair says so honestly.
 */

#include <QString>
#include <QStringList>

class QWidget;

namespace FileOps {

// cut/copy/paste state
void setClipboard(const QStringList &paths, bool cut);
QStringList clipboardPaths();
bool clipboardIsCut();
bool clipboardEmpty();
void clearClipboard();

// Recursive operations with a QProgressDialog; all return false when the
// user cancelled or any entry failed (after showing a message box).
bool copyPaths(const QStringList &sources, const QString &destDir, QWidget *parent);
bool movePaths(const QStringList &sources, const QString &destDir, QWidget *parent);
bool deletePaths(const QStringList &paths, QWidget *parent);

// Launch "cmd args..." as a detached EwokOS process.  appName becomes
// X_APP_NAME, which the X server uses to label the window.
void launch(const QString &cmdLine, const QString &appName);

// Open a regular file: ELF executables run directly, everything else goes
// through the open_with entry of /usr/system/filetypes.json.
void openFile(const QString &path, QWidget *parent);

void showProperties(const QString &path, QWidget *parent);

QString humanSize(qint64 bytes);

} // namespace FileOps

#endif // PCMANFM_FILEOPS_H
