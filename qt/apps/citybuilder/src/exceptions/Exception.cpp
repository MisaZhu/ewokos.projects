#include "Exception.hpp"

#include <QtCore/QDebug>

#include <cstdlib>



Exception::Exception(const QString& message) :
    message(message),
    stdMessage(message.toStdString())
{
    // -fno-exceptions: this fault cannot be thrown, and no C++ exception
    // runtime is linked.  Upstream these were uncaught and ended in
    // std::terminate; log and abort to reproduce that without exceptions.  The
    // message reaching the console is also what makes a bad config or an
    // unported path diagnosable during bring-up.
    qWarning("CityBuilderEngine fatal: %s", stdMessage.c_str());
    abort();
}



const QString& Exception::getMessage() const
{
    return message;
}



const char* Exception::what() const noexcept
{
    return stdMessage.c_str();
}
