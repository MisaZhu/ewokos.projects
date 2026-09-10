#ifndef UNEXPECTEDEXCEPTION_HPP
#define UNEXPECTEDEXCEPTION_HPP

#include "src/exceptions/EngineException.hpp"

class UnexpectedException : public EngineException
{
    public:
        [[noreturn]] UnexpectedException(const QString& message);
};

#endif // UNEXPECTEDEXCEPTION_HPP
