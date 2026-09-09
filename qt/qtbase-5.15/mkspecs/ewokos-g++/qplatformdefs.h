/****************************************************************************
**
** EwokOS platform definitions for Qt.
**
** Modelled on mkspecs/linux-g++/qplatformdefs.h, with the differences forced
** by what EwokOS's libc actually provides.  The SDK headers were checked one by
** one rather than assumed, and two of linux-g++'s includes are not there:
**
**   <features.h>  glibc's feature-selection header.  EwokOS's libc is newlib
**                 derived with its own <cdefs-compat.h>/<bsd_cdefs.h>, and has
**                 no features.h at all.  Nothing here needs it: it exists to
**                 let glibc expose _GNU_SOURCE extensions, and the EwokOS
**                 headers expose what they expose unconditionally.
**
**   <dlfcn.h>     absent, and deliberately so - EwokOS has no dlopen.  Qt
**                 reaches it only through QT_LIBS_DYNLOAD/QLibrary, which the
**                 mkspec leaves empty and configure turns off.
**
** Everything else linux-g++ includes does exist in the SDK and is kept:
** unistd.h pthread.h dirent.h fcntl.h grp.h pwd.h signal.h and the sys/ and
** netinet/ set, which means QSharedMemory's SysV IPC path, QProcess's
** fork/exec path and QFileSystemEngine's POSIX path all have their headers.
**
** Three headers linux-g++ does NOT include are added here, because EwokOS's
** libc does not make them reachable the way glibc does.  On glibc they arrive
** transitively - <unistd.h> pulls in <errno.h> and <string.h>, and
** <sys/time.h> pulls in <time.h> - so Qt's corelib sources have grown used to
** using ENOENT, struct timespec and strcoll without including anything.  None
** of those transitive includes exist here, and this header is the single place
** Qt expects platform include differences to be resolved:
**
**   <errno.h>   ENOENT/EIO, used by qresource.cpp:1518 and :1523.
**   <time.h>    struct timespec, and tzname/timezone/daylight.  <sys/time.h>
**               alone declares timespec, but qtestsupport_core.cpp:57 defines
**               one and includes nothing but its own header, so it depends
**               entirely on this file.  qdatetime.cpp reads the three timezone
**               globals directly.
**   <string.h>  strcoll, declared but not reachable for qstring.cpp:6582.
**
** QT_USE_XOPEN_LFS_EXTENSIONS is deliberately NOT defined, so
** common/posix/qplatformdefs.h takes the plain struct stat / off_t branch.
** EwokOS has no stat64/off64_t/open64 family, and defining the macro would
** select exactly those names.
**
****************************************************************************/

#ifndef QPLATFORMDEFS_H
#define QPLATFORMDEFS_H

// Get Qt defines/settings

#include "qglobal.h"

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include <pthread.h>
#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/time.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>

#include "../common/posix/qplatformdefs.h"

#undef QT_SOCKLEN_T
#define QT_SOCKLEN_T            socklen_t

#define QT_SNPRINTF             ::snprintf
#define QT_VSNPRINTF            ::vsnprintf

#endif // QPLATFORMDEFS_H
