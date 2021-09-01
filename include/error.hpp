#pragma once

#include <string>
#include <ostream>

namespace cpool {

enum class generic_error_code {
    no_error = 0,
    generic_error
};

class error {

public:
    error() :
        error_code_(0),
        message_()
    {}

    error(std::error_code error_code) :
        error_code_(static_cast<int>(error_code.value())),
        message_(error_code.message())
    {}

    error(std::string error_message) :
        error_code_(static_cast<int>(generic_error_code::generic_error)),
        message_(error_message)
    {}

    error(const char* error_message) :
        error_code_(static_cast<int>(generic_error_code::generic_error)),
        message_(error_message)
    {}
    
    error(std::error_code error_code, std::string error_message) :
        error_code_(error_code.value()),
        message_(error_message)
    {}
    
    error(int error_code, std::string error_message) :
        error_code_(error_code),
        message_(error_message)
    {}

    int error_code() const {
        return error_code_;
    }

    std::string message() const {
        return message_;
    }

    std::string what() const {
        return message();
    }

    std::runtime_error as_exception() const {
        return std::runtime_error(what());
    }

    bool operator==(const error &rhs) const{
        return (
            error_code_ == rhs.error_code_ &&
            message_ == rhs.message_
        );
    }

    bool operator==(const std::error_code &rhs) const{
        return (
            error_code_ == rhs.value() &&
            message_ == rhs.message()
        );
    }

    bool operator!=(const error &rhs) const {
        return !(*this == rhs);
    }

    bool operator!() const {
        return !((bool)*this);
    }

    explicit operator bool() const {
            return (error_code_ != 0);
        }

    friend std::ostream& operator<< (std::ostream &os, const error& error) {
        return os << "error_code: " << error.error_code_ << ", message: " << error.message_;
    }

private:
    int error_code_;
    std::string message_;
};

static const error no_error;

}