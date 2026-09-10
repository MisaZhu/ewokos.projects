#ifndef EXCEPTION_HPP
#define EXCEPTION_HPP

#include <string>
#include <QtCore/QString>

//
// Port note (EwokOS): upstream this class derived from QException and every
// subclass was `throw`n.  The EwokOS Qt build - and this app with it - is
// compiled -fno-exceptions -fno-rtti, and no C++ exception runtime is linked
// (__cxa_throw / __gxx_personality_v0 / __cxa_begin_catch are absent; libgcc
// only carries the low-level unwinder).  So nothing here can be thrown or
// caught, and QException is unavailable anyway (it is gated behind Qt's
// `future` config, which is disabled).
//
// The classes are kept as message carriers.  The constructor logs the message
// and aborts, reproducing what an *uncaught* throw did upstream (the app had no
// handler for these outside one spot in Map.cpp, so they ended in
// std::terminate).  Every former `throw XException(msg);` became
// `(void)XException(msg);` - constructing the temporary runs this and does not
// return.  The `(void)` cast is what keeps `XException(identifier)` from being
// reparsed as a declaration (most vexing parse).  The single genuinely
// recoverable throw, Map::getBestBuildingEntryPoint, was rewritten to return a
// null pointer instead of constructing an exception.
//
class Exception
{
    private:
        QString message;
        std::string stdMessage;

    public:
        // [[noreturn]]: the constructor logs and abort()s (see Exception.cpp),
        // so it never returns.  Marking it - and every subclass constructor -
        // tells the compiler that a `(void)XException(msg)` statement, this
        // port's replacement for upstream's `throw XException(msg)`, does not
        // fall through.  That is what silences -Wreturn-type in the
        // value-returning resolveXxx() functions whose last statement upstream
        // was a throw (Direction.cpp, BuildingStatus.cpp and friends).
        [[noreturn]] Exception(const QString& message);
        virtual ~Exception() = default;

        const QString& getMessage() const;

        virtual const char* what() const noexcept;
};

#endif // EXCEPTION_HPP
