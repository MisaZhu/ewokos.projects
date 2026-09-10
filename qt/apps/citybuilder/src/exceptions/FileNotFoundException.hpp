#ifndef FILENOTFOUNDEXCEPTION_HPP
#define FILENOTFOUNDEXCEPTION_HPP

#include "src/exceptions/Exception.hpp"

class FileNotFoundException : public Exception
{
    public:
        [[noreturn]] FileNotFoundException(const QString& filePath);
};

#endif // FILENOTFOUNDEXCEPTION_HPP
