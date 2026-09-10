/***************************************************************************
 *   This file is part of the SimulIDE port to EwokOS.                     *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, see <http://www.gnu.org/licenses/>.  *
 *                                                                         *
 ***************************************************************************/

#include "qprocess_shim.h"

#include <QElapsedTimer>
#include <QStringList>
#include <QVector>

// <sys/select.h> brings <sys/time.h> and <signal.h> with it, so struct timeval,
// fd_set and kill all come from this one line; <sys/wait.h> for waitpid and
// WEXITSTATUS, <fcntl.h> for the O_* flags setStandardOutputFile needs, and
// <unistd.h> for fork, execvp, pipe, dup2, chdir, read, close, _exit and the two
// *_FILENO constants.
#include <sys/select.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

// "avra -W NoRegDef -l /some/dir/out.lst /some/dir/in.asm" into an argv.
//
// Qt does this in QProcessPrivate::splitArgs for the single-string overload of
// start(), which is the only overload this tree calls, and the rules below are
// its rules: whitespace separates, a quoted run is one argument whether or not
// it contains whitespace, a backslash escapes the character after it outside
// quotes and only '"' inside double quotes.
//
// The quoting matters less than it looks.  avrasmdebugger.cpp:82 wraps every
// path in addQuotes() under `#ifndef Q_OS_UNIX`, and this is a Unix build, so
// upstream hands the splitter bare paths and a directory with a space in its
// name would already split wrongly under the real QProcess.  Matching that
// behaviour is the point rather than improving on it: the difference between
// this and Qt should be the missing forkfd, not the argument parsing.
//
// A quote opens an argument even when nothing follows it, so `cmd ""` yields an
// empty argv entry.  inWord tracks that separately from cur being non-empty,
// which is the one place a simpler "split on spaces and trim" would differ.
static QStringList splitCommand( const QString &command )
{
    QStringList args;
    QString     cur;
    QChar       quote;
    bool        inWord = false;

    const int n = command.length();

    for( int i = 0; i < n; ++i )
    {
        const QChar c = command.at(i);

        if( !quote.isNull() )                        // inside '...' or "..."
        {
            if( c == quote ) quote = QChar();        // the closing one
            else if( c == '\\' && quote == '"' && i+1 < n )
                cur.append( command.at(++i) );       // \" and \\ inside "..."
            else cur.append( c );
            continue;
        }

        if( c == ' ' || c == '\t' )
        {
            if( inWord ) { args.append( cur ); cur.clear(); inWord = false; }
            continue;
        }

        if( c == '"' || c == '\'' ) { quote = c; inWord = true; continue; }

        if( c == '\\' && i+1 < n )
        {
            cur.append( command.at(++i) );
            inWord = true;
            continue;
        }

        cur.append( c );
        inWord = true;
    }

    if( inWord ) args.append( cur );
    return args;
}

QProcess::QProcess( QObject* parent )
          : QObject( parent )
          , m_outFd( -1 )
          , m_errFd( -1 )
          , m_pid( -1 )
          , m_running( false )
          , m_exitCode( 0 )
{
}

QProcess::~QProcess()
{
    // A child still running when the object goes away would keep a pid that
    // nothing holds any more, so it is killed and reaped here rather than left.
    // basedebugger.h holds QProcess by value and codeeditor.cpp declares four of
    // them as locals that go out of scope immediately after waitForFinished, so
    // the normal path arrives here with m_running already false.
    if( m_running && m_pid >= 0 ) ::kill( m_pid, SIGKILL );

    if( m_outFd >= 0 ) ::close( m_outFd );
    if( m_errFd >= 0 ) ::close( m_errFd );

    if( m_pid >= 0 )
    {
        int status = 0;
        ::waitpid( m_pid, &status, 0 );
    }
}

void QProcess::setWorkingDirectory( const QString &dir )
{
    m_workDir = dir;
}

void QProcess::setStandardOutputFile( const QString &name )
{
    m_outFile = name;
}

void QProcess::start( const QString &command )
{
    if( m_running ) return;

    const QStringList args = splitCommand( command );
    if( args.isEmpty() ) return;

    // stdout either goes to a pipe this class reads or to the file
    // setStandardOutputFile named.  stderr always goes to a pipe: nothing in the
    // tree redirects it, and readAllStandardError has to be able to answer.
    const bool toFile = !m_outFile.isEmpty();

    int outPipe[2] = { -1, -1 };
    int errPipe[2] = { -1, -1 };

    if( !toFile && ::pipe( outPipe ) != 0 ) return;

    if( ::pipe( errPipe ) != 0 )
    {
        if( !toFile ) { ::close( outPipe[0] ); ::close( outPipe[1] ); }
        return;
    }

    const int pid = int( ::fork() );

    if( pid < 0 )
    {
        if( !toFile ) { ::close( outPipe[0] ); ::close( outPipe[1] ); }
        ::close( errPipe[0] );
        ::close( errPipe[1] );
        return;
    }

    if( pid == 0 )
    {
        // The child.  Every path below ends in execvp or _exit and none of them
        // returns: falling out of this block would carry on running the parent's
        // Qt event loop, its Simulator thread and its heap in a process that has
        // already been forked, which is the classic way to get two of everything.
        if( !m_workDir.isEmpty() )
            ::chdir( m_workDir.toLocal8Bit().constData() );

        if( toFile )
        {
            const int fd = ::open( m_outFile.toLocal8Bit().constData(),
                                   O_WRONLY | O_CREAT | O_TRUNC, 0644 );
            if( fd >= 0 )
            {
                ::dup2( fd, STDOUT_FILENO );
                if( fd > STDOUT_FILENO ) ::close( fd );
            }
            // A failed open leaves stdout as it was rather than _exit-ing.  Qt
            // would report the process as failed to start; here the command still
            // runs and its output goes wherever the parent's stdout went, which
            // for inodebugger.cpp:227 - the only caller - means the .lst file is
            // missing and the parse of it finds nothing.  Both outcomes are a
            // message in the editor's output panel.
        }
        else ::dup2( outPipe[1], STDOUT_FILENO );

        ::dup2( errPipe[1], STDERR_FILENO );

        if( outPipe[0] >= 0 ) ::close( outPipe[0] );
        if( outPipe[1] >= 0 ) ::close( outPipe[1] );
        ::close( errPipe[0] );
        ::close( errPipe[1] );

        // The QByteArrays have to be kept somewhere that outlives the calls that
        // made them, or argv would be a vector of pointers into freed temporaries
        // by the time execvp read it.  Hence two vectors rather than one loop.
        QVector<QByteArray> raw;
        raw.reserve( args.size() );
        for( int i = 0; i < args.size(); ++i )
            raw.append( args.at(i).toLocal8Bit() );

        QVector<char*> argv;
        argv.reserve( args.size() + 1 );
        for( int i = 0; i < raw.size(); ++i ) argv.append( raw[i].data() );
        argv.append( nullptr );

        ::execvp( argv[0], argv.data() );

        // 127 because that is what a shell uses for "command not found", and the
        // number is load-bearing for the way the editor detects a missing tool
        // chain.  avrasmdebugger.cpp:69 runs `<compilerPath>avra` with no
        // arguments purely to see whether it exists, then checks stdout for
        // "VERSION"; picasmdebugger.cpp:50 does the same looking for "OPTIONS".
        // A program that never started produces no stdout at all, the contains()
        // fails, and toolChainNotFound() prints the honest message.  Any exit
        // code would do for that, since it is the empty stdout that decides, but
        // 127 is the one that would also be right if anything ever did look.
        ::_exit( 127 );
    }

    // The parent keeps the read ends and closes the write ends.  Closing the
    // write end here is what makes the child's exit visible as EOF: while either
    // end of a pipe is open for writing, a read on it blocks instead of
    // returning 0, and drain() would wait forever on a child that had finished.
    if( toFile ) m_outFd = -1;
    else
    {
        ::close( outPipe[1] );
        m_outFd = outPipe[0];
    }
    ::close( errPipe[1] );
    m_errFd = errPipe[0];

    m_pid      = pid;
    m_running  = true;
    m_exitCode = 0;
    m_outBuf.clear();
    m_errBuf.clear();
}

// Returns false only on timeout.  A select() that fails for a reason other than
// EINTR breaks out and still reports success, because the child has to be reaped
// either way and the caller cannot do anything more useful with the distinction.
bool QProcess::drain( int msecs )
{
    QElapsedTimer timer;
    const bool noDeadline = ( msecs < 0 );
    if( !noDeadline ) timer.start();

    // Both streams have to be watched at once.  Reading stdout to EOF and then
    // stderr would deadlock whenever the child filled the stderr pipe first: it
    // would block writing to a pipe nobody was reading, and this loop would block
    // reading a stream that would never close.  Compiler output is exactly the
    // case where that happens - a failing assembler writes a wall of diagnostics
    // to stderr and nothing to stdout.
    while( m_outFd >= 0 || m_errFd >= 0 )
    {
        int slice = 200;
        if( !noDeadline )
        {
            const qint64 left = qint64(msecs) - timer.elapsed();
            if( left <= 0 ) return false;
            slice = int( left < 200 ? left : 200 );
        }

        fd_set set;
        FD_ZERO( &set );
        int nfds = 0;
        if( m_outFd >= 0 )
        {
            FD_SET( m_outFd, &set );
            if( m_outFd + 1 > nfds ) nfds = m_outFd + 1;
        }
        if( m_errFd >= 0 )
        {
            FD_SET( m_errFd, &set );
            if( m_errFd + 1 > nfds ) nfds = m_errFd + 1;
        }

        struct timeval tv;
        tv.tv_sec  = slice / 1000;
        tv.tv_usec = ( slice % 1000 ) * 1000;

        const int ready = ::select( nfds, &set, nullptr, nullptr, &tv );

        if( ready < 0 )
        {
            if( errno == EINTR ) continue;
            break;
        }
        // A zero return is this slice's 200ms expiring with nothing to read, not
        // the deadline: the deadline is the left <= 0 test at the top, and with
        // noDeadline set there is no deadline at all.  Continuing is what makes
        // waitForFinished(-1) wait indefinitely.
        if( ready == 0 ) continue;

        char buf[4096];

        if( m_outFd >= 0 && FD_ISSET( m_outFd, &set ) )
        {
            const int n = int( ::read( m_outFd, buf, sizeof(buf) ) );
            if( n > 0 ) m_outBuf.append( buf, n );
            else { ::close( m_outFd ); m_outFd = -1; }
        }
        if( m_errFd >= 0 && FD_ISSET( m_errFd, &set ) )
        {
            const int n = int( ::read( m_errFd, buf, sizeof(buf) ) );
            if( n > 0 ) m_errBuf.append( buf, n );
            else { ::close( m_errFd ); m_errFd = -1; }
        }
    }
    return true;
}

void QProcess::reap()
{
    m_running = false;
    if( m_pid < 0 ) return;

    int status = 0;
    ::waitpid( m_pid, &status, 0 );

    // WEXITSTATUS is `(status) & 0xff` here and WIFEXITED is the constant 1:
    // this kernel delivers only STOP and KILL, so there is no signal-death case
    // to distinguish and every status is an exit status.
    m_exitCode = WEXITSTATUS( status );
    m_pid      = -1;
}

bool QProcess::waitForFinished( int msecs )
{
    if( !m_running ) return true;

    const bool finished = drain( msecs );

    if( !finished )
    {
        // Kill before reaping.  Returning false without doing this would leave a
        // live child whose pid is about to be forgotten: the next start() would
        // overwrite m_pid and the child would never be waited for.
        ::kill( m_pid, SIGKILL );
        if( m_outFd >= 0 ) { ::close( m_outFd ); m_outFd = -1; }
        if( m_errFd >= 0 ) { ::close( m_errFd ); m_errFd = -1; }
    }

    reap();

    // After the buffers are full and before returning, so that the slot sees the
    // complete output.  BaseDebugger::ProcRead, which is what is connected to
    // this at basedebugger.cpp:51, walks canReadLine()/readLine() until the
    // buffer has no whole line left in it.
    if( finished ) emit readyRead();

    return finished;
}

QByteArray QProcess::readAllStandardOutput()
{
    const QByteArray out = m_outBuf;
    m_outBuf.clear();
    return out;
}

QByteArray QProcess::readAllStandardError()
{
    const QByteArray out = m_errBuf;
    m_errBuf.clear();
    return out;
}

bool QProcess::canReadLine() const
{
    // A trailing run with no newline is not a line and stays in the buffer, which
    // is Qt's behaviour and what makes basedebugger.cpp's while loop stop at the
    // right place rather than one entry early or one late.
    return m_outBuf.indexOf('\n') >= 0;
}

QByteArray QProcess::readLine()
{
    const int i = m_outBuf.indexOf('\n');
    if( i < 0 ) return QByteArray();

    const QByteArray line = m_outBuf.left( i + 1 );
    m_outBuf.remove( 0, i + 1 );
    return line;
}

#include "moc_qprocess_shim.cpp"
