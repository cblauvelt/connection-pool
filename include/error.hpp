#pragma once

#include <string>
#include <ostream>

namespace cpool {

namespace error {

enum class generic_error_code {
    no_error = 0,
    generic_error
};

template <typename T = generic_error_code>
class error {

public:
    error();
    error(T error_code, std::string message);
    error(std::string message);

    T error_code() const;

    std::string message() const;
    std::string what() const;
    std::runtime_error as_exception() const;

    bool operator==(const error<T> &rhs) const;
    bool operator!=(const error<T> &rhs) const;
    bool operator!() const;

    explicit operator bool() const {
        return (error_code_ != static_cast<T>(0));
    }

    friend std::ostream& operator<< (std::ostream &os, const error<T>& error) {
        return os << "error_code: " << (int)error.error_code_ << ", message: " << error.message_;
    }

private:
    T error_code_;
    std::string message_;
};

static const error<> no_error;

template <typename T>
error<T>::error() :
    error_code_(static_cast<T>(0)),
    message_()
{}

template <typename T>
error<T>::error(T error_code, std::string message) :
    error_code_(error_code),
    message_(message)
{}

template <typename T>
T error<T>::error_code() const {
    return error_code_;
}

template <typename T>
std::string error<T>::message() const {
    return message_;
}

template <typename T>
std::string error<T>::what() const {
    return message();
}

template <typename T>
std::runtime_error error<T>::as_exception() const {
    return std::runtime_error(what());
}

template <typename T>
bool error<T>::operator==(const error<T> &rhs) const {
    return (
        error_code_ == rhs.error_code_ &&
        message_ == rhs.message_
    );
}

template <typename T>
bool error<T>::operator!=(const error<T> &rhs) const {
    return !(*this == rhs);
}
    

template <typename T>
bool error<T>::operator!() const {
    return !((bool)*this);
}
    

}

}