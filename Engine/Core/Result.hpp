#pragma once

#include <string>
#include <utility>
#include <variant>

namespace ld {

enum class ErrorCode {
    invalidArgument,
    notFound,
    corrupted,
    denied,
    limit,
    conflict,
    unavailable,
    network,
    internal
};

struct Error {
    ErrorCode code = ErrorCode::internal;
    std::string message;
    static Error make(ErrorCode code, std::string message) { return {code, std::move(message)}; }
};

template <typename T>
class Result {
public:
    Result(T value) : storage(std::move(value)) {}
    Result(Error error) : storage(std::move(error)) {}
    bool ok() const { return std::holds_alternative<T>(storage); }
    explicit operator bool() const { return ok(); }
    T& value() { return std::get<T>(storage); }
    const T& value() const { return std::get<T>(storage); }
    T& operator*() { return value(); }
    const T& operator*() const { return value(); }
    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }
    const Error& error() const { return std::get<Error>(storage); }
    T valueOr(T fallback) const { return ok() ? value() : std::move(fallback); }

private:
    std::variant<T, Error> storage;
};

template <>
class Result<void> {
public:
    Result() = default;
    Result(Error error) : failure(std::move(error)), failed(true) {}
    bool ok() const { return !failed; }
    explicit operator bool() const { return ok(); }
    const Error& error() const { return failure; }

private:
    Error failure;
    bool failed = false;
};

inline Error fail(ErrorCode code, std::string message) { return Error{code, std::move(message)}; }

}
