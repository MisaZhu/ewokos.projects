#!/usr/bin/env bash
#
# Configure and build the vendored Qt 5.15 tree for EwokOS.
#
# Output is build/qtbuild-$(ARCH)-$(HW)/, which is exactly what
# projects/qt/Makefile compiles the "ewokos" QPA plugin, qtdemo and apps/
# against: its QT_BUILD is $(CURDIR)/build/qtbuild-$(ARCH)-$(HW), and its probe
# for "is Qt there" is bin/moc in that directory.  Until this script has run
# once, that probe fails and `make` at projects/ prints a note and skips qt -
# the image simply ships without the Qt apps.
#
# Usage:
#   ./build.sh                  # syncqt + configure + make
#   ./build.sh -j 8             # same, overriding the job count
#   ./build.sh --configure      # stop after configure
#   ./build.sh --reconfigure    # ignore an existing configure and redo it
#   ./build.sh --clean          # remove the build tree first
#   ARCH=arm HW=raspix ./build.sh
#   EWOK_SDK=/some/other/sdk ./build.sh
#
# ---------------------------------------------------------------------------
# Four things this script has to do that configure will not do for us.
#
# 1. syncqt, and before configure rather than after.
#
#    configure runs syncqt only for a git checkout - configure:697 guards it
#    with `[ -e "$relpath/.git" ]`, and qtbase-5.15/ is a vendored snapshot with
#    no .git of its own.  Two consequences, both fatal if left alone:
#
#      - the forwarding headers are never written.  Every #include in this port
#        goes through one: <QtCore/qglobal.h>, the versioned private set
#        (QtCore/5.15.19/QtCore/private/...), and <qpa/qplatformwindow.h>.  The
#        QPA plugin includes all three kinds.
#      - configure:758 then tells the bootstrap qmake
#        `INC_PATH = $(SOURCE_PATH)/include`, i.e. it assumes the release
#        tarball's pre-synced include/ directory.  Ours is gitignored precisely
#        because syncqt regenerates it, so the path would not exist.
#
#    Full syncqt, not the `-minimal -module QtCore` form configure itself uses:
#    that one exists to satisfy the bootstrap and would leave QtGui, QtWidgets,
#    QtFontDatabaseSupport and the qpa/ private headers unsynced.
#
#    And with an explicit -outdir.  syncqt.pl's positional argument only locates
#    sync.profile; where the headers are *written* is $out_basedir, which it
#    initialises to getcwd() (syncqt.pl:77).  Run from projects/qt/ without
#    -outdir it silently syncs every module into projects/qt/include/, right
#    next to the repo's own include/qt/ewokosqt.h - 1300 generated files in a
#    version-controlled directory, with no error to say it happened.
#
# 2. The mkspec and the patches have to be present *inside* the Qt tree.
#
#    projects/qt/mkspecs/ewokos-g++ is the version-controlled original, but
#    qmake only looks in $$QT_SOURCE_TREE/mkspecs, so it is copied there.  The
#    copy matters to the Makefile too: it sets QT_MKSPEC to the in-tree path
#    because qplatformdefs.h there includes "../common/posix/qplatformdefs.h",
#    and only Qt's own mkspecs/ has a common/.  Both steps are idempotent - the
#    copy overwrites, and each patch is tested forwards and in reverse before
#    anything is written - so re-running this script on a configured tree is
#    safe.
#
# 3. CROSS_COMPILE as an environment variable, not a -device-option.
#
#    -device-option only exists for -device builds and configure rejects it
#    alongside -xplatform.  The mkspec covers this by reading the environment
#    (isEmpty(CROSS_COMPILE): CROSS_COMPILE = $$(CROSS_COMPILE)), which is why
#    this script exports it rather than passing it.  Same for EWOK_SDK, which
#    the mkspec turns into the only -I and -L Qt is built with; it is taken from
#    the environment so one mkspec serves virt, raspi5 and the arm machines.
#
# 4. A feature list, because auto-detection is not enough.
#
#    Most of what EwokOS lacks, configure finds out by itself: the library tests
#    for dl, X11, EGL, glib, icu and fontconfig simply fail to compile against
#    this SDK, and the features fall off.  The flags below are the ones where
#    detection is wrong or where the default is a module nobody here links:
#
#      -no-feature-process   the only one that is a hard build failure rather
#                            than a preference.  Its condition excludes winrt,
#                            uikit, integrity, vxworks and rtems - five OSes, and
#                            never the feature itself - so on EwokOS it comes out
#                            ON, which drags in io/forkfd_qt.cpp.  That needs C11
#                            <stdatomic.h>, waitid, SA_SIGINFO and CLD_EXITED,
#                            and the SDK has none of them (there is not even a
#                            stdatomic.h to include).  patches/0001 gates forkfd
#                            on qtConfig(process) so the two stay in step; this
#                            flag is the other half of that.  Nothing in the
#                            plugin, the demo or apps/ uses QProcess, and
#                            QDesktopServices::openUrl already has a ::system()
#                            fallback behind #if !QT_CONFIG(process).
#
#      -qt-zlib              the SDK ships zlib.h and libz.a, so the system test
#                            would pass and QtCore would link -lz.  The Makefile's
#                            QT_LIBS does not list it; it only reaches -lz
#                            incidentally through EWOK_LIB_GRAPH.  Bundling keeps
#                            Qt's link closure inside the archives it names.
#
#      --iconv=no            the SDK has no iconv.h, so this is belt to the
#                            detection's brace - but QStringConverter's iconv
#                            path is one that would otherwise be selected by a
#                            test that has never been run against this libc.
#
#      -no-feature-{network,sql,testlib,concurrent,printer}
#                            whole modules.  The plugin and the apps need
#                            Core, Gui and Widgets plus the platformsupport
#                            archives; these are build time and nothing else.
#                            Each is a real feature name, which is what the
#                            -no-feature- builtin validates against.
#
#      xml is deliberately NOT in that list, though it used to be.  QtXml is
#                            three .cpp files sitting on top of QtCore -
#                            dom/qdom.cpp, dom/qdomhelpers.cpp, sax/qxml.cpp -
#                            and apps/simulide reads and writes every .simu
#                            circuit through QDomDocument: 70 call sites across
#                            circuit.cpp, subcircuit.cpp, chip.cpp and
#                            componentselector.cpp.  Building the module is both
#                            cheaper and more faithful than rewriting those onto
#                            QXmlStreamReader.
#
#                            It costs the rest of the tree nothing.  QT_NO_XML
#                            appears nowhere in qtbase's sources - grep it - so
#                            switching the feature on changes no instruction in
#                            any existing QtCore/QtGui/QtWidgets object; the only
#                            C++-visible effect is that qconfig_p.h's
#                            QT_FEATURE_xml goes from -1 to 1 and a generated
#                            QtXml/qtxml-config.h appears.  (It does make make
#                            rebuild everything that includes qglobal_p.h, which
#                            is most of the tree - a one-off.)  And because the
#                            link is static and grouped, an app that never
#                            touches QDom pulls in none of libQt5Xml.a.
#
#      ... and dropping the flag is only half of it.  This snapshot's
#                            configure.json had its "subconfigs" pruned to
#                            corelib/gui/widgets; upstream 5.15.19 lists nine.
#                            "src/xml" has to be put back.  Two different
#                            configure.json files are involved and they do
#                            different jobs:
#
#                              the top-level "xml" feature is a privateFeature.
#                                It writes QT_FEATURE_xml into qconfig_p.h, and
#                                that is what src.pro:179's qtConfig(xml) reads to
#                                decide whether src/xml joins SUBDIRS.  Removing
#                                -no-feature-xml gets you this far, and this far
#                                only.
#                              src/xml/configure.json holds the "dom" feature, a
#                                publicFeature.  configure only walks a subconfig
#                                that "subconfigs" names - qtConfProcessOutput in
#                                mkspecs/features/qt_configure.prf is what writes
#                                build/src/<module>/qt<module>-config.h, and it is
#                                never reached for a module nobody listed.
#
#                            Leave it out and the failure is not "xml is off":
#                            src.pro has already put the module in SUBDIRS, so
#                            make enters src/xml, and qtxmlglobal.h:45 dies with
#                            "QtXml/qtxml-config.h: No such file or directory" on
#                            the first of the three compiles.  A configure-time
#                            omission that surfaces as a build error two
#                            directories away from its cause.
#
#      -no-{opengl,egl,eglfs,xcb,evdev,libinput,tslib,mtdev,linuxfb,kms,gbm,
#           vulkan,directfb,fontconfig,xkbcommon,openvg,angle}
#                            every windowing/input/GL backend but `minimal`,
#                            which the mkspec already names as
#                            QT_QPA_DEFAULT_PLATFORM.  The real backend is the
#                            ewokos plugin, built by the Makefile outside Qt.
#                            Note -no-linuxfb does not cost Qt5FbSupport:
#                            platformsupport.pro lists fbconvenience
#                            unconditionally, and qfbvthandler.cpp inside it is
#                            inert here (guarded on Q_OS_LINUX).
#
#      -qt-{libpng,libjpeg,harfbuzz,pcre,doubleconversion}
#                            bundled, so the archives are the ones the Makefile
#                            names - qtlibpng, qtlibjpeg, qtharfbuzz, qtpcre2.
#                            For harfbuzz and pcre2 the snapshot leaves no
#                            choice: the SDK ships neither.
#
#      -system-freetype      freetype is the exception, and not by preference.
#                            src/3rdparty/freetype is absent from this pruned
#                            snapshot, so -qt-freetype makes src.pro add a
#                            sub-3rdparty-freetype target whose .pro does not
#                            exist - "Cannot find file: .../freetype.pro", and
#                            make dies before gui is reached.  The SDK does ship
#                            libfreetype.a with ft2build.h and freetype/, which
#                            is what the tree was pruned for.  Nothing is lost:
#                            src.pro gates the bundled copy on
#                            !qtConfig(system-freetype) but gates fontdatabases
#                            on qtConfig(freetype), which stays true - and the
#                            plugin needs that, since its font database is
#                            QtFontDatabaseSupport's freetype one
#                            (<QtFontDatabaseSupport/private/qfreetypefontdatabase_p.h>).
#
#      -no-pch               the mkspec's own note: a PCH would have to be built
#                            with the same -nostdinc++/-fno-exceptions/-fno-rtti
#                            set and buys nothing on a one-off cross build.
#
# The spelling of the bundled-library flags is not a matter of taste.  configure's
# parser hands the *next* argument to string, addString and optionalString options
# and to nothing else - qtConfCommandline_enum, which is what zlib/libpng/pcre/
# iconv are, does `isEmpty(val): val = yes` and never looks at nextok.  So
# "-libpng qt" sets libpng to yes and then trips over the orphan "qt" as
# "Invalid command line parameter".  The two forms enum does accept are the
# (qt|system)- prefix, which the parser splits into opt and val itself, and
# --opt=value.
#
# Deliberately NOT passed:
#
#   -c++std          the mkspec pins every QMAKE_CXXFLAGS_CXX* to -std=c++14, so
#                    whatever level Qt asks for is the level it gets.  Passing
#                    -c++std would only restate that in a second place, and the
#                    two could drift.
#   -no-feature-dlopen / -library
#                    QMAKE_LIBS_DYNLOAD is empty in the mkspec, the SDK has no
#                    dlfcn.h and no libdl.a, so the test fails and `library`
#                    follows it.  qplugin.h has no QT_NO_LIBRARY guard, so
#                    QT_STATICPLUGIN - the form the plugin is built with - is
#                    unaffected either way.
#   -no-accessibility / -no-feature-desktopservices
#                    both are wanted: the Makefile links Qt5AccessibilitySupport,
#                    and QDesktopServices::openUrl stays available to the apps.
#
set -euo pipefail

# configure refuses to run with any of these set (configure:75), and an
# inherited QMAKESPEC from another Qt on the machine is the common case.
unset QMAKESPEC XQMAKESPEC QMAKEPATH QMAKEFEATURES

ARCH="${ARCH:-aarch64}"
HW="${HW:-virt}"

QT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$QT_DIR/../.." && pwd)"
QT_SRC="$QT_DIR/qtbase-5.15"
BUILD="$QT_DIR/build/qtbuild-$ARCH-$HW"
EWOK_SDK="${EWOK_SDK:-$ROOT/system/build_$ARCH/$HW}"

# The prefixes system/platform/*/make.rule uses, so that Qt is compiled by the
# same toolchain as the rest of the image.
if [[ -z "${CROSS_COMPILE:-}" ]]; then
    case "$ARCH" in
        aarch64) CROSS_COMPILE="aarch64-none-elf-" ;;
        arm)     CROSS_COMPILE="arm-none-eabi-" ;;
        x86)     CROSS_COMPILE="x86_64-elf-" ;;
        riscv)   CROSS_COMPILE="riscv64-unknown-elf-" ;;
        *)
            echo "ERROR: no default CROSS_COMPILE for ARCH=$ARCH; set it explicitly." >&2
            exit 1
            ;;
    esac
fi

# Exported, not passed: see point 3 above.
export CROSS_COMPILE EWOK_SDK

# Jobs: -j wins, then $JOBS, then the host's core count.
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
CONFIGURE_ONLY=no
RECONFIGURE=no
CLEAN=no

while [[ $# -gt 0 ]]; do
    case "$1" in
        -j)          JOBS="$2"; shift 2 ;;
        -j*)         JOBS="${1#-j}"; shift ;;
        --configure) CONFIGURE_ONLY=yes; shift ;;
        --reconfigure) RECONFIGURE=yes; shift ;;
        --clean)     CLEAN=yes; shift ;;
        -h|--help)   sed -n '2,32p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)           echo "ERROR: unknown argument '$1' (try --help)" >&2; exit 1 ;;
    esac
done

die() { echo "ERROR: $*" >&2; exit 1; }

# ---------------------------------------------------------------------------
echo "[1/5] Preflight"

[[ -d "$QT_SRC/src/corelib" ]] \
    || die "$QT_SRC is not a Qt source tree - is the qtbase-5.15 snapshot checked out?"
command -v "${CROSS_COMPILE}g++" >/dev/null 2>&1 \
    || die "${CROSS_COMPILE}g++ not in PATH - install the EwokOS toolchain or set CROSS_COMPILE"
command -v perl >/dev/null 2>&1 || die "perl not in PATH - syncqt.pl needs it"

# The four archives the mkspec's EWOK_LIBC group resolves against.  Without all
# of them even configure's "can the toolchain link a minimal program" fails, and
# it fails as that message rather than as a missing-library one.
[[ -d "$EWOK_SDK/include" ]] || die "no SDK at $EWOK_SDK - build system/ first (ARCH=$ARCH HW=$HW)"
for lib in libewoksys.a libc.a libgloss.a libcxx.a; do
    [[ -f "$EWOK_SDK/lib/$lib" ]] || die "$EWOK_SDK/lib/$lib missing - build system/ first"
done

echo "      ARCH=$ARCH HW=$HW"
echo "      Qt source : $QT_SRC"
echo "      build tree: $BUILD"
echo "      SDK       : $EWOK_SDK"
echo "      compiler  : ${CROSS_COMPILE}g++"

# ---------------------------------------------------------------------------
echo "[2/5] Installing the mkspec and the patches into the Qt tree"

# See point 2 above.  Plain cp rather than cp -R: -R into an existing directory
# would nest the second run's copy inside the first one's.
mkdir -p "$QT_SRC/mkspecs/ewokos-g++"
cp "$QT_DIR"/mkspecs/ewokos-g++/* "$QT_SRC/mkspecs/ewokos-g++/"
echo "      mkspecs/ewokos-g++ -> $QT_SRC/mkspecs/ewokos-g++"

# Three-way test per patch, because a marker grep can only ever speak for the
# one patch that adds the marker: it says nothing about 0002 once 0001 is in,
# so every later patch would be skipped on an already-ported tree.
#   forward --dry-run succeeds  -> not yet applied, apply it
#   reverse --dry-run succeeds  -> already applied, leave it alone
#   neither                     -> the tree has drifted from what the patch
#                                  expects; fail rather than build half-ported
#                                  sources and debug the wreckage later.
for p in "$QT_DIR"/patches/*.patch; do
    [[ -e "$p" ]] || continue
    name="$(basename "$p")"
    if (cd "$QT_SRC" && patch -p1 --forward -s --dry-run < "$p") >/dev/null 2>&1; then
        echo "      $name: applying"
        (cd "$QT_SRC" && patch -p1 --forward < "$p")
    elif (cd "$QT_SRC" && patch -p1 -R -s --dry-run < "$p") >/dev/null 2>&1; then
        echo "      $name: already applied"
    else
        die "$name applies neither forwards nor in reverse - $QT_SRC has drifted"
    fi
done

# ---------------------------------------------------------------------------
echo "[3/5] syncqt - writing the forwarding headers into the source tree"

# MODULE_VERSION from .qmake.conf rather than a literal here: syncqt.pl -version
# decides the "5.15.19" level of the private include paths, and the Makefile's
# QT_VER has to agree with it or <QtCore/private/...> stops resolving.
QT_VER="$(sed -n 's/^MODULE_VERSION *= *//p' "$QT_SRC/.qmake.conf")"
[[ -n "$QT_VER" ]] || die "could not read MODULE_VERSION from $QT_SRC/.qmake.conf"

perl "$QT_SRC/bin/syncqt.pl" -version "$QT_VER" -outdir "$QT_SRC" "$QT_SRC" >/dev/null
[[ -f "$QT_SRC/include/QtCore/qglobal.h" ]] \
    || die "syncqt ran but produced no $QT_SRC/include/QtCore/qglobal.h"
[[ -d "$QT_SRC/include/QtGui/$QT_VER/QtGui/qpa" ]] \
    || die "syncqt produced no private qpa/ headers under include/QtGui/$QT_VER"
# Belt to -outdir's brace.  The two checks above would pass on a later run off
# headers an earlier run put in the wrong place, and a getcwd() sync is not
# visible any other way - it does not fail, it just writes 1300 files somewhere
# they are tracked.
[[ ! -e "$QT_DIR/include/QtCore" ]] \
    || die "syncqt wrote into $QT_DIR/include/ - it ignored -outdir; remove the Qt* dirs from there"
echo "      Qt $QT_VER headers synced into $QT_SRC/include"

# ---------------------------------------------------------------------------
echo "[4/5] configure"

# --clean is here rather than left to `rm -rf build/` because a stale
# config.tests/ tree is not harmless: configure only prepends `make clean` to a
# test's build when that test's Makefile already exists, so a half-finished run
# and a fresh one take different paths through the same test.
if [[ "$CLEAN" == yes && -d "$BUILD" ]]; then
    echo "      removing $BUILD"
    rm -rf "$BUILD"
fi

mkdir -p "$BUILD"

if [[ -f "$BUILD/Makefile" && "$RECONFIGURE" != yes ]]; then
    echo "      $BUILD already configured (pass --reconfigure to redo it)"
else
    # -prefix points at the build tree itself.  Nothing here ever runs
    # `make install` - the Makefile links straight out of $BUILD/lib and
    # $BUILD/bin - so the prefix only has to be a path that cannot be mistaken
    # for a real target-side installation.  Left unset it would default to
    # /usr/local/Qt-$QT_VER, which on the target is a directory that does not
    # exist and on the host is not this build.
    (
        cd "$BUILD"
        "$QT_SRC/configure" \
            -xplatform ewokos-g++ \
            -prefix "$BUILD" \
            -release \
            -static \
            -opensource -confirm-license \
            -no-pch \
            -nomake tests \
            -nomake examples \
            \
            -no-dbus \
            -no-feature-network \
            -no-feature-sql \
            -no-feature-testlib \
            -no-feature-concurrent \
            -no-feature-printer \
            \
            -no-feature-process \
            --iconv=no \
            -no-icu \
            -no-glib \
            -no-inotify \
            -no-eventfd \
            -no-syslog \
            -no-journald \
            -no-zstd \
            -qt-zlib \
            -qt-pcre \
            -qt-doubleconversion \
            \
            -no-opengl \
            -no-egl \
            -no-eglfs \
            -no-openvg \
            -no-angle \
            -no-vulkan \
            -no-xcb \
            -no-xkbcommon \
            -no-evdev \
            -no-libinput \
            -no-tslib \
            -no-mtdev \
            -no-linuxfb \
            -no-kms \
            -no-gbm \
            -no-directfb \
            -no-fontconfig \
            -no-feature-sessionmanager \
            -qt-libpng \
            -qt-libjpeg \
            -system-freetype \
            -qt-harfbuzz
    )
    echo "      configured"
fi

if [[ "$CONFIGURE_ONLY" == yes ]]; then
    echo "      --configure given, stopping here"
    exit 0
fi

# ---------------------------------------------------------------------------
echo "[5/5] make -j$JOBS"

# gmake first: macOS's /usr/bin/make is 3.81, which predates several things
# Qt's generated makefiles do without complaint but not all of them.
MAKE_BIN="${MAKE:-}"
if [[ -z "$MAKE_BIN" ]]; then
    if command -v gmake >/dev/null 2>&1; then MAKE_BIN=gmake; else MAKE_BIN=make; fi
fi
echo "      using $MAKE_BIN ($($MAKE_BIN --version 2>/dev/null | head -1))"

(cd "$BUILD" && "$MAKE_BIN" -j"$JOBS")

# ---------------------------------------------------------------------------
# The Makefile's probe is bin/moc, so that is what gets checked - and it has to
# be runnable on the *host*, since the Makefile invokes it to generate the
# plugin's and the apps' moc output.  It is: src/tools/moc/moc.pro is
# option(host_build) + force_bootstrap, so cross-building still compiles moc
# with the host compiler.  Verifying that here turns "the build finished but
# make still skips qt" into an error with a reason.
[[ -x "$BUILD/bin/moc" ]] || die "$BUILD/bin/moc was not built"
if ! "$BUILD/bin/moc" -v >/dev/null 2>&1; then
    die "$BUILD/bin/moc exists but will not run on this host - it was built for the target, not the host"
fi

echo
echo "Done.  Qt $QT_VER is in $BUILD"
echo "  host tools : $BUILD/bin/{moc,uic,rcc}"
echo "  libraries  : $BUILD/lib/libQt5*.a"
echo "Next: make -C projects/qt"
