#include <qpa/qplatformintegrationplugin.h>

#include <QtCore/qplugin.h>

#include "qewokosintegration.h"

QT_BEGIN_NAMESPACE

/*
 * The plugin entry point.
 *
 * EwokOS has no dlopen - the kernel has no such call and configure was given
 * -no-feature-dlopen - so this is a static plugin: it is archived into
 * libewokosqpa.a, linked into the application, and pulled in by the
 * application's Q_IMPORT_PLUGIN(EwokosIntegrationPlugin).  The metadata below is
 * what makes the factory recognise the key, and it is the same metadata a shared
 * plugin would carry, read from ewokos.json rather than from a .so section.
 *
 * The IID's trailing 5.3 is the interface version Qt itself defines in
 * qplatformintegrationplugin.h and has kept at 5.3 since then; it is not this
 * port's Qt version and must not be "corrected" to match.
 */
class EwokosIntegrationPlugin : public QPlatformIntegrationPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QPlatformIntegrationFactoryInterface_iid FILE "ewokos.json")

public:
    QPlatformIntegration *create(const QString &key, const QStringList &paramList) override;
};

QPlatformIntegration *EwokosIntegrationPlugin::create(const QString &key, const QStringList &paramList)
{
    /* The factory lowercases the key before it gets here, but the comparison is
       case-insensitive anyway: QT_QPA_PLATFORM=ewokos, =EwokOS and =EWOKOS are
       all the same request and there is nothing else this plugin could be asked
       to build. */
    if (key.compare(QLatin1String("ewokos"), Qt::CaseInsensitive) == 0)
        return new EwokosIntegration(paramList);

    return nullptr;
}

QT_END_NAMESPACE

/* moc emits this at global scope (QT_NAMESPACE is not defined, so
   QT_BEGIN_NAMESPACE above opened nothing) once QT_STATICPLUGIN selects the
   static form of QT_MOC_EXPORT_PLUGIN. */
extern const QStaticPlugin qt_static_plugin_EwokosIntegrationPlugin();

/*
 * The one call an EwokOS application makes to bring this plugin up, before it
 * constructs a QGuiApplication.  It is a function rather than the usual
 * Q_IMPORT_PLUGIN(EwokosIntegrationPlugin) for a reason that is easy to miss and
 * expensive to hit:
 *
 *   Q_IMPORT_PLUGIN expands to
 *
 *       extern const QStaticPlugin qt_static_plugin_X();
 *       class StaticXPluginInstance { public: StaticXPluginInstance() {
 *           qRegisterStaticPluginFunction(qt_static_plugin_X()); } };
 *       static StaticXPluginInstance staticXInstance;
 *
 *   and that last line is a namespace-scope object with a non-trivial
 *   constructor.  EwokOS links with -nostartfiles because it supplies its own
 *   runtime entry point (see mkspecs/ewokos-g++/qmake.conf), so nothing runs
 *   .init_array and the object is never constructed.  The plugin would sit in the
 *   binary, unreferenced, and QGuiApplication would abort with "This application
 *   failed to start because no Qt platform plugin could be initialized".
 *
 * Calling it from here instead is the same registration at a point that is
 * guaranteed to execute.  Everything after it is stock Qt: the plugin lands in
 * QPluginLoader::staticPlugins(), QPlatformIntegrationFactory's directLoader
 * finds it by the key in ewokos.json, and QT_QPA_PLATFORM selects it.
 *
 * QT_QPA_PLATFORM has to be set rather than left to QT_QPA_DEFAULT_PLATFORM_NAME
 * because that macro was compiled into libQt5Gui.a as "minimal" - which is what
 * the mkspec asked for while this plugin did not exist yet - and redefining it
 * on an application's command line does not reach the library that was built
 * with the other value.  qgetenv() runs inside createPlatformIntegration(), so
 * setting it here, before the QApplication exists, is in time.
 *
 * QT_ENABLE_REGEXP_JIT turns off the PCRE2/sljit regex JIT, which cannot work on
 * EwokOS and kills the process outright if it is allowed to try:
 *
 *   QRegularExpression::optimizePattern()  -> pcre2_jit_compile_16()
 *     -> sljit_generate_code() -> __clear_cache() -> libgcc's
 *        __aarch64_sync_cache_range(), whose first instruction is
 *        "mrs x2, ctr_el0" to discover the cache line sizes.
 *
 * EwokOS leaves SCTLR_EL1.UCT set, so an EL0 read of CTR_EL0 takes a synchronous
 * exception (EC 0x18) that the kernel does not emulate - it dumps core.  Even
 * past that, the flush itself is DC CVAU / IC IVAU from EL0, which SCTLR_EL1.UCI
 * traps the same way, so the JIT has no working path to making freshly written
 * code visible to the instruction cache.  QRegularExpression is used during
 * QApplication startup, so this fires before any window appears.
 *
 * isJitEnabled() reads the variable once, lazily, on the first pattern that is
 * compiled - well after this function returns - so setting it here is in time,
 * and the interpreter path is a supported configuration rather than a
 * degradation: it is what Qt already does for a debug build.
 */
void ewokosQtInit()
{
    qRegisterStaticPluginFunction(qt_static_plugin_EwokosIntegrationPlugin());

    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "ewokos");

    /* Left overridable in the other direction for anyone who wants to test a
       kernel that does allow EL0 cache maintenance. */
    if (qEnvironmentVariableIsEmpty("QT_ENABLE_REGEXP_JIT"))
        qputenv("QT_ENABLE_REGEXP_JIT", "0");
}

#include "main.moc"
