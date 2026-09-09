/*
    EwokOS re-implementation of Konsole's Pty class - see Pty.h for why.

    The shell is hosted the way sshd hosts one: two pipe pairs, fork(), dup2()
    onto 0/1/2 (plus VFS_BACKUP_FD0, which EwokOS libc uses to restore stdio),
    then proc_exec().  A QTimer pumps the output pipe: EwokOS pipes cannot be
    watched by the ewokos QPA event dispatcher, and their read semantics are
    inverted vs POSIX (empty non-blocking read returns 0, real EOF returns -1),
    so EOF is detected via POLLHUP and waitpid(WNOHANG) exactly like sshd does.

    Follows the license of the file it replaces (GPLv2+).
*/

// Own
#include "Pty.h"

// Qt
#include <QTimer>
#include <QtDebug>

// System
extern "C" {
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/errno.h>
#include <sys/wait.h>
#include <setenv.h>
#include <ewoksys/vfs.h>
#include <ewoksys/proc.h>
#include <ewoksys/core.h>
}

using Konsole::Pty;

/* How often the output pipe is drained.  30ms tracks the xterm console's
 * update timer and keeps a full-speed `cat` from flooding the emulation. */
static const int PUMP_INTERVAL_MS = 30;

static int set_fd_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        flags = 0;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

Pty::Pty(QObject* parent)
    : QObject(parent)
    , _shellPid(-1)
    , _running(false)
    , _finishedReported(false)
    , _hup(false)
    , _exitStatus(NormalExit)
    , _pumpTimer(nullptr)
    , _windowColumns(0)
    , _windowLines(0)
    , _eraseChar(0)
    , _xonXoff(true)
    , _utf8(true)
{
    _stdinPipe[0] = _stdinPipe[1] = -1;
    _stdoutPipe[0] = _stdoutPipe[1] = -1;

    _pumpTimer = new QTimer(this);
    _pumpTimer->setInterval(PUMP_INTERVAL_MS);
    connect(_pumpTimer, &QTimer::timeout, this, &Pty::pump);
}

Pty::~Pty()
{
    if (_running && _shellPid > 1) {
        ::kill(_shellPid, SIGKILL);
        /* reap so the kernel can hold the funeral (see sshd: proc_get_uuid
         * stays set until the parent reaps). */
        ::waitpid(_shellPid, nullptr, 0);
    }
    closeFds();
}

void Pty::closeFds()
{
    for (int* fd : { &_stdinPipe[0], &_stdinPipe[1],
                     &_stdoutPipe[0], &_stdoutPipe[1] }) {
        if (*fd >= 0) {
            ::close(*fd);
            *fd = -1;
        }
    }
}

int Pty::start(const QString& program,
               const QStringList& arguments,
               const QStringList& environment,
               ulong winid,
               bool addToUtmp)
{
    Q_UNUSED(winid);
    Q_UNUSED(addToUtmp);

    if (_running)
        return -1;

    if (::pipe(_stdinPipe) != 0 || ::pipe(_stdoutPipe) != 0) {
        qWarning() << "Pty::start: pipe() failed:" << strerror(errno);
        closeFds();
        return -1;
    }

    /* Build the command line before forking: proc_exec() takes one string,
     * and the child must not touch Qt objects after fork(). */
    QStringList cmdParts;
    cmdParts << program;
    /* Session prepends the program itself as argv[0]; skip that duplicate. */
    for (int i = 0; i < arguments.size(); ++i) {
        if (i == 0 && arguments.at(i) == program)
            continue;
        cmdParts << arguments.at(i);
    }
    QByteArray cmd = cmdParts.join(QLatin1Char(' ')).toLocal8Bit();

    /* KEY=value pairs -> two flat arrays the child can walk without Qt. */
    QList<QByteArray> envBytes;
    envBytes.reserve(environment.size());
    for (const QString& entry : environment) {
        const int eq = entry.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        envBytes << entry.left(eq).toLocal8Bit()
                 << entry.mid(eq + 1).toLocal8Bit();
    }

    /* Flattened before the fork for the same no-Qt-in-the-child reason. */
    QByteArray workDir = _workingDir.toLocal8Bit();

    int pid = ::fork();
    if (pid < 0) {
        qWarning() << "Pty::start: fork() failed:" << strerror(errno);
        closeFds();
        return -1;
    }

    if (pid == 0) {
        /* Child: wire the pipes to stdio the way sshd's become_login_process
         * does, publish the env, and become the shell. */
        if (::dup2(_stdinPipe[0], 0) < 0 ||
                ::dup2(_stdoutPipe[1], 1) < 0 ||
                ::dup2(_stdoutPipe[1], 2) < 0 ||
                ::dup2(_stdinPipe[0], VFS_BACKUP_FD0) < 0) {
            ::exit(2);
        }
        ::close(VFS_BACKUP_FD1);
        ::close(_stdinPipe[0]);
        ::close(_stdinPipe[1]);
        ::close(_stdoutPipe[0]);
        ::close(_stdoutPipe[1]);

        setenv("CONSOLE_ID", "qterminal");
        core_set_env("CONSOLE_ID", "qterminal");
        for (int i = 0; i + 1 < envBytes.size(); i += 2) {
            setenv(envBytes.at(i).constData(), envBytes.at(i + 1).constData());
            core_set_env(envBytes.at(i).constData(), envBytes.at(i + 1).constData());
        }

        if (!workDir.isEmpty())
            ::chdir(workDir.constData());

        proc_exec(cmd.constData());
        ::exit(-1); /* not reached on success */
    }

    /* Parent: keep only our ends, non-blocking. */
    ::close(_stdinPipe[0]);
    _stdinPipe[0] = -1;
    ::close(_stdoutPipe[1]);
    _stdoutPipe[1] = -1;

    if (set_fd_nonblock(_stdinPipe[1]) < 0 ||
            set_fd_nonblock(_stdoutPipe[0]) < 0) {
        qWarning() << "Pty::start: set nonblock failed:" << strerror(errno);
    }

    _shellPid = pid;
    _running = true;
    _finishedReported = false;
    _hup = false;
    _pumpTimer->start();
    return 0;
}

void Pty::pump()
{
    if (!_running)
        return;

    /* Drain whatever the shell printed.  POLLIN gates the read: an empty
     * EwokOS pipe reads as 0 (not EAGAIN), so the return value alone cannot
     * distinguish "no data" from EOF - POLLHUP and waitpid decide that. */
    char buf[4096];
    /* ONLCR scratch: every '\n' can grow to two bytes, so worst case doubles. */
    char out[2 * sizeof(buf)];
    for (;;) {
        struct pollfd pfd;
        pfd.fd = _stdoutPipe[0];
        pfd.events = POLLIN | POLLHUP | POLLERR;
        pfd.revents = 0;

        int rc = ::poll(&pfd, 1, 0);
        if (rc <= 0)
            break;
        if (pfd.revents & POLLHUP)
            _hup = true;
        if ((pfd.revents & POLLIN) == 0)
            break;

        ssize_t n = ::read(_stdoutPipe[0], buf, sizeof(buf));
        if (n <= 0)
            break;
        /* EwokOS pipes carry the shell's raw bytes, and a bare '\n' is all a
         * program emits.  On Unix the pty's line discipline (ONLCR) rewrites
         * that to "\r\n" so the VT100 cursor returns to column 0; this Pty is
         * two plain pipes with no termios, so without the translation every
         * newline drops one row at the previous column and the whole session
         * staircases.  Emulate ONLCR on the way into the emulation. */
        int m = 0;
        for (ssize_t i = 0; i < n && m + 2 <= (int)sizeof(out); ++i) {
            if (buf[i] == '\n')
                out[m++] = '\r';
            out[m++] = buf[i];
        }
        emit receivedData(out, m);
    }

    /* Reap and report exit.  waitpid(WNOHANG) rather than proc_get_uuid:
     * the kernel keeps the uuid until the zombie is reaped. */
    if (_shellPid > 1) {
        int st = 0;
        pid_t rc = ::waitpid(_shellPid, &st, WNOHANG);
        if (rc != 0) {
            _shellPid = -1;
            reportFinished(NormalExit);
            return;
        }
    }
    if (_hup && _shellPid <= 1)
        reportFinished(NormalExit);
}

void Pty::reportFinished(ExitStatus status)
{
    if (_finishedReported)
        return;
    _finishedReported = true;
    _running = false;
    _exitStatus = status;
    _pumpTimer->stop();
    closeFds();
    emit finished(0, status);
}

void Pty::sendData(const char* buffer, int length)
{
    if (!_running || _stdinPipe[1] < 0 || length <= 0)
        return;

    int off = 0;
    int spins = 0;
    while (off < length) {
        ssize_t n = ::write(_stdinPipe[1], buffer + off, length - off);
        if (n > 0) {
            off += (int)n;
            spins = 0;
            continue;
        }
        if (errno == EAGAIN || errno == EINTR || n == 0) {
            /* Bounded retry: the pipe is full when the shell stopped reading;
             * a dead shell must not hang the GUI thread. */
            if (++spins > 500)
                break;
            proc_usleep(1000);
            continue;
        }
        break;
    }
}

void Pty::lockPty(bool lock)
{
    if (lock)
        _pumpTimer->stop();
    else if (_running)
        _pumpTimer->start();
}

/* ---- bookkeeping-only properties ---------------------------------------- */

void Pty::setEmptyPTYProperties()
{
}

void Pty::setWriteable(bool writeable)
{
    Q_UNUSED(writeable);
}

void Pty::setFlowControlEnabled(bool on)
{
    _xonXoff = on;
}

bool Pty::flowControlEnabled() const
{
    return _xonXoff;
}

void Pty::setWindowSize(int lines, int cols)
{
    _windowLines = lines;
    _windowColumns = cols;
}

QSize Pty::windowSize() const
{
    return { _windowColumns, _windowLines };
}

void Pty::setErase(char erase)
{
    _eraseChar = erase;
}

char Pty::erase() const
{
    return _eraseChar;
}

void Pty::setUtf8Mode(bool on)
{
    _utf8 = on;
}

void Pty::setWorkingDirectory(const QString& dir)
{
    _workingDir = dir;
}

int Pty::foregroundProcessGroup() const
{
    return (_shellPid > 1) ? _shellPid : 0;
}

/* ---- the QProcess-shaped slice Session.cpp consumes ---------------------- */

Pty::ProcessState Pty::state() const
{
    return _running ? Running : NotRunning;
}

bool Pty::isRunning() const
{
    return _running;
}

qint64 Pty::processId() const
{
    return (_shellPid > 1) ? _shellPid : 0;
}

Pty::ExitStatus Pty::exitStatus() const
{
    return _exitStatus;
}

bool Pty::waitForFinished(int msecs)
{
    if (!_running)
        return true;

    int waited = 0;
    while (_shellPid > 1) {
        int st = 0;
        pid_t rc = ::waitpid(_shellPid, &st, WNOHANG);
        if (rc != 0) {
            _shellPid = -1;
            reportFinished(NormalExit);
            return true;
        }
        if (msecs >= 0 && waited >= msecs)
            return false;
        proc_usleep(10000);
        waited += 10;
    }
    return true;
}
