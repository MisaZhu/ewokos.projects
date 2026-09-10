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

#ifndef QPROCESS_SHIM_H
#define QPROCESS_SHIM_H

#include <QObject>
#include <QString>
#include <QByteArray>

// A QProcess for a Qt that has none.
//
// This Qt was configured with -no-feature-process, and that is not a preference
// that could be revisited.  Qt's QProcess on Unix is built on forkfd, its own
// vendored copy in src/corelib/io/, which needs C11 <stdatomic.h>, waitid,
// SA_SIGINFO and CLD_EXITED.  The SDK has none of the four - there is not even
// a stdatomic.h to include - so the feature is off, QT_CONFIG(process) and
// QT_CONFIG(processenvironment) are both false, and the class is simply absent:
// the QtCore umbrella header guards its `#include "qprocess.h"` on
// QT_CONFIG(processenvironment), so the name never reaches a translation unit.
//
// What that cost was src/gui/editorwidget/.  Twenty-seven files of upstream's
// firmware editor, restored whole, and every one of the assembler debuggers in
// it declares a QProcess to run its tool chain with - avra, gpasm, gcbasic,
// arduino-cli, make.  Eight members between them, no enumerations, no statics:
//
//   start  waitForFinished  readAllStandardOutput  readAllStandardError
//   setWorkingDirectory  setStandardOutputFile  canReadLine  readLine
//
// plus three constructors, and one signal that basedebugger.cpp:51 connects to.
// Supplying exactly that keeps all twenty-seven files byte-identical to
// upstream, which is the whole reason this exists as a header rather than as
// twenty-seven edits.
//
// Not popen(), though that is the obvious thing to reach for and the reason
// this comment is as long as it is.  EwokOS's libc declares popen in stdio.h
// and then stubs it: system/basic/libc/libewoksys/src/stdio/popen.c sets errno
// to ENOSYS and returns NULL, with the reason in its own first line - "EwokOS
// has no shell pipeline support".  system() is stubbed the same way, in
// libgloss/compat.c.  The rootfs has one shell, /bin/shell, and its getopt
// string is "i": a non-option argument is opened as a script file to run, so
// there is no -c and nothing to hand a command string to.  Both stubs would
// have compiled and then failed at run time with an empty output buffer, which
// is the worst outcome available - the editor would have reported a tool chain
// it had never actually tried to run.
//
// What EwokOS does have is everything underneath a shell: fork (compat.c:1195,
// through _fork and proc_fork, which also does the vfs_on_fork bookkeeping),
// execvp with its own PATH search defaulting to /bin:/sbin, pipe, dup2, chdir,
// waitpid, select and kill.  So this does what Qt's own qprocess_unix.cpp does
// - fork, redirect, exec, wait - and simply uses waitpid where Qt uses forkfd.
// It is a real process spawner, not a placeholder: if the tool chain named in
// the editor's settings were installed, it would run.
//
// Two consequences of there being no shell are worth knowing when reading the
// .cpp.  A command arrives as one string, "avra -l out.lst in.asm", and has to
// be split into an argv here rather than left to /bin/sh; splitCommand() does
// that with the same quote and backslash handling Qt's QProcessPrivate::splitArgs
// uses.  And redirection, working directory and the two output streams have to
// be set up by hand in the child, since there is no `cd dir && cmd >f 2>&1` to
// write them as.
//
// Injected with -include from the Makefile rather than included by anyone, for
// the reason given there: nothing in the editor subtree writes
// `#include <QProcess>`, because upstream never had to.
//
// QObject, and with Q_OBJECT, because basedebugger.cpp:51 does
//
//   connect( &m_compProcess, SIGNAL(readyRead()), SLOT(ProcRead()) );
//
// The sender has to be a QObject* for that to compile at all.  It also has to
// be one that really has the signal: SIGNAL() is a string, so a class without
// the meta-object entry would compile, connect would return false at run time,
// and every BaseDebugger constructed would print
// "QObject::connect: No such signal QProcess::readyRead()" to stderr.  Declaring
// it costs one moc run on this header and makes ProcRead() do what upstream
// intended.

class QProcess : public QObject
{
    Q_OBJECT

    public:
        // One constructor with a defaulted parent covers all three spellings in
        // the tree: `QProcess m_compProcess;` as a member, `QProcess checkComp(
        // this );` with a real parent, and `QProcess compGcb( 0l );` with a null
        // one.  It is explicit because Qt's is.
        explicit QProcess( QObject* parent = nullptr );
        ~QProcess();

        // Both are recorded and applied in the child between fork and exec, not
        // at the time they are called - which is also when Qt applies them, and
        // matters here because inodebugger.cpp calls setStandardOutputFile
        // immediately before start.
        void setWorkingDirectory( const QString &dir );
        void setStandardOutputFile( const QString &name );

        void start( const QString &command );

        // msecs < 0 waits without a deadline, which is what all ten call sites
        // in the tree ask for.  A non-negative one is honoured rather than
        // ignored: the timeout path kills and reaps the child before returning
        // false, because returning without reaping would leave a zombie that
        // nothing else in this class will ever collect.
        bool waitForFinished( int msecs = 30000 );

        QByteArray readAllStandardOutput();
        QByteArray readAllStandardError();

        bool       canReadLine() const;
        QByteArray readLine();

        int exitCode() const { return m_exitCode; }

    signals:
        // Emitted once, after waitForFinished has drained both pipes and before
        // it returns.  Qt emits it repeatedly as data arrives on the event loop;
        // this class has no event loop involvement and the callers here all
        // block on waitForFinished first, so one emission covering the whole
        // output is the same thing seen from ProcRead().
        void readyRead();

    private:
        Q_DISABLE_COPY( QProcess )

        // Read both pipes until they close, or until msecs have gone by.
        // Separate function because waitForFinished calls it on the way to
        // reaping and the destructor calls it if the object goes away with a
        // child still running.
        //
        // Returns false on timeout only, and waitForFinished needs the
        // distinction: a child that has not finished has to be killed before it
        // is reaped, or the next start() overwrites m_pid and the child is never
        // waited for.  The destructor ignores it, which is correct there rather
        // than an oversight - it has already decided to tear the object down.
        bool drain( int msecs );
        void reap();

        QString    m_workDir;
        QString    m_outFile;

        // Read ends of the two pipes; -1 once closed.  pid_t is an int on every
        // architecture this tree targets, and keeping it an int means the header
        // does not have to pull in <sys/types.h> for one member.
        int        m_outFd;
        int        m_errFd;
        int        m_pid;

        bool       m_running;
        int        m_exitCode;

        // Everything read so far and not yet handed to a caller.  readAll* and
        // readLine take from these and clear what they take, which is how Qt's
        // read buffers behave and what basedebugger.cpp's
        // `while( canReadLine() ) ... readLine()` loop assumes.
        QByteArray m_outBuf;
        QByteArray m_errBuf;
};

#endif
