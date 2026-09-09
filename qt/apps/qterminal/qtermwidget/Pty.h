/*
    EwokOS re-implementation of Konsole's Pty class.

    The original Pty derived from KPtyProcess -> KProcess -> QProcess and drove
    a Unix98 pseudo-terminal.  EwokOS has neither QProcess (Qt is configured
    -no-feature-process: no waitid/SA_SIGINFO for forkfd) nor /dev/ptmx.  What
    it does have is exactly what sshd uses to host a login shell: a pipe pair,
    fork(), dup2() onto fd 0/1/2 and proc_exec().  /bin/shell performs its own
    line editing and echo on whatever fd 0/1 are, so no line discipline is
    needed on this side; raw keystrokes go down the stdin pipe and everything
    the shell prints comes back up the stdout pipe into the VT102 emulation.

    The interface is kept source-compatible with what Session.cpp calls; the
    QProcess enums Session relied on are provided here under the same names.

    This file is part of the QTerminal port to EwokOS and follows the license
    of the file it replaces (GPLv2+, see the original Pty.h from qtermwidget).
*/

#ifndef PTY_H
#define PTY_H

// Qt
#include <QObject>
#include <QSize>
#include <QStringList>

class QTimer;

namespace Konsole {

/**
 * The Pty class is used to start the terminal process,
 * send data to it and receive data from it.
 *
 * To use this class, construct an instance and connect to the sendData
 * slot and receivedData signal to send data to or receive data from
 * the process.
 *
 * To start the terminal process, call the start() method with the
 * name of the program to start and appropriate arguments.
 */
class Pty: public QObject
{
    Q_OBJECT

public:
    /* Stand-ins for the QProcess enums Session.cpp compares against. */
    enum ExitStatus { NormalExit, CrashExit };
    enum ProcessState { NotRunning, Running };

    explicit Pty(QObject* parent = nullptr);
    ~Pty() override;

    /**
     * Starts the terminal process.  Returns 0 on success, -1 otherwise.
     *
     * @param program Path to the program to start
     * @param arguments Arguments to pass (argv[0] included, as Session builds it)
     * @param environment A list of key=value pairs added to the child env
     * @param winid Ignored on EwokOS (no WINDOWID)
     * @param addToUtmp Ignored on EwokOS (no utmp)
     */
    int start(const QString& program,
              const QStringList& arguments,
              const QStringList& environment,
              ulong winid,
              bool addToUtmp);

    /** No-op placeholders kept for Session.cpp source compatibility. */
    void setEmptyPTYProperties();
    void setWriteable(bool writeable);
    void setFlowControlEnabled(bool on);
    bool flowControlEnabled() const;

    /**
     * Remembers the window size; there is no TIOCSWINSZ on a pipe, so this
     * only feeds windowSize() (used by Session::refresh()).
     */
    void setWindowSize(int lines, int cols);
    QSize windowSize() const;

    void setErase(char erase);
    char erase() const;

    /** With pipes there is no foreground process group; the shell pid serves. */
    int foregroundProcessGroup() const;

    /* The slice of the QProcess API Session.cpp uses. */
    ProcessState state() const;
    bool isRunning() const;
    qint64 processId() const;
    ExitStatus exitStatus() const;
    bool waitForFinished(int msecs = 30000);

    /** QProcess-shaped: the child chdir()s here right before proc_exec(). */
    void setWorkingDirectory(const QString& dir);

public slots:
    void setUtf8Mode(bool on);

    /** Suspends (lock=true) or resumes draining of the shell output pipe. */
    void lockPty(bool lock);

    /** Sends raw bytes down the shell's stdin pipe. */
    void sendData(const char* buffer, int length);

signals:
    /**
     * Emitted when a new block of data arrives from the shell output pipe.
     */
    void receivedData(const char* buffer, int length);

    /** Emitted once when the shell process is gone. */
    void finished(int exitCode, Konsole::Pty::ExitStatus exitStatus);

private slots:
    /** Timer-driven pump: poll and drain the output pipe, reap the child. */
    void pump();

private:
    void closeFds();
    void reportFinished(ExitStatus status);

    int     _stdinPipe[2];   /* [0] child reads, [1] we write   */
    int     _stdoutPipe[2];  /* [0] we read, [1] child writes   */
    int     _shellPid;
    bool    _running;
    bool    _finishedReported;
    bool    _hup;            /* POLLHUP seen: write end closed  */
    ExitStatus _exitStatus;
    QTimer* _pumpTimer;

    int  _windowColumns;
    int  _windowLines;
    char _eraseChar;
    bool _xonXoff;
    bool _utf8;
    QString _workingDir;
};

}

#endif // PTY_H
