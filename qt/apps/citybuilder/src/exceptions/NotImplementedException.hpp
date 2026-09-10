#ifndef NOTIMPLEMENTEDEXCEPTION_HPP
#define NOTIMPLEMENTEDEXCEPTION_HPP

#include "src/exceptions/Exception.hpp"

class NotImplementedException : public Exception
{
    public:
        [[noreturn]] NotImplementedException(const QString& message);
};

#endif // NOTIMPLEMENTEDEXCEPTION_HPP
