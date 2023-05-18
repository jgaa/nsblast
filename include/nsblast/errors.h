#pragma once

#include <stdexcept>

namespace nsblast {

class Exception : public std::runtime_error {
public:
    Exception(const std::string& what, const std::string& httpMessage) noexcept
        : std::runtime_error(what), httpMessage_{httpMessage} {}

    Exception(const std::string& what) noexcept
        : std::runtime_error(what) {}


    virtual int httpError() const noexcept = 0;
    virtual std::string httpMessage() const noexcept {
        return httpMessage_.empty() ? what() : httpMessage_;
    }

    virtual std::string message() const noexcept {
        return what();
    }

private:
    const std::string httpMessage_ = {};
};

class AlreadyExistException : public Exception {
public:
    AlreadyExistException(const std::string& what) noexcept
        : Exception(what) {}

    AlreadyExistException(const std::string& what, const std::string& httpMessage) noexcept
        : Exception(what, httpMessage) {}

    int httpError() const noexcept override { return 409; }
};

class NotFoundException : public Exception {
public:
    NotFoundException(const std::string& what) noexcept
        : Exception(what) {}

    NotFoundException(const std::string& what, const std::string& httpMessage) noexcept
        : Exception(what, httpMessage) {}

    int httpError() const noexcept override { return 404; }
};

class MissingIdException : public Exception {
public:
    MissingIdException(const std::string& what) noexcept
        : Exception(what) {}

    MissingIdException(const std::string& what, const std::string& httpMessage) noexcept
        : Exception(what, httpMessage) {}

    int httpError() const noexcept override { return 400; }
};

class ConstraintException : public Exception {
public:
    ConstraintException(const std::string& what) noexcept
        : Exception(what) {}

    ConstraintException(const std::string& what, const std::string& httpMessage) noexcept
        : Exception(what, httpMessage) {}

    int httpError() const noexcept override { return 400; }
};

class DeniedException : public Exception {
public:
    DeniedException(const std::string& what = "Access Denied") noexcept
        : Exception(what) {}

    DeniedException(const std::string& what, const std::string& httpMessage) noexcept
        : Exception(what, httpMessage) {}

    int httpError() const noexcept override { return 403; }
};


class InternalErrorException : public Exception {
public:
    InternalErrorException(const std::string& what) noexcept
        : Exception(what) {}

    InternalErrorException(const std::string& what, const std::string& httpMessage) noexcept
        : Exception(what, httpMessage) {}

    int httpError() const noexcept override { return 500; }
};

} // ns
